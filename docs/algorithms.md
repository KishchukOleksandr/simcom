# Algorithms & Architecture {#algorithms_page}

This document details the core algorithms, concurrency model, and architectural patterns implemented in **libsimcom**.

---

## 1. Zero-Allocation Lock-Free SPSC Ring Buffer

### Motivation
Embedded cellular modem drivers receive asynchronous serial byte streams from a UART peripheral. Data ingestion typically occurs in a high-frequency UART Interrupt Service Routine (ISR) or DMA half-transfer/transfer-complete interrupt. Processing full AT command syntax or multi-line responses directly inside the ISR is strictly prohibited on bare-metal systems because:
1. Parsing requires non-trivial processing time, which would increase interrupt latency.
2. Logging or memory operations in ISR context can trigger race conditions and priority inversions.

### Algorithm Design
`libsimcom` implements a Single-Producer Single-Consumer (SPSC) circular byte ring buffer embedded directly in `simcom_handle_t` (`me->rx_buf[SIMCOM_CFG_RX_BUF_SIZE]`):

```
 Producer (UART ISR)                          Consumer (Simcom_Poll / Superloop)
  Simcom_FeedBytes()                             simcom_ringbuf_pop()
          │                                               ▲
          ▼                                               │
   [rx_head index]  ───>  [ Circular Buffer ]  ───>  [rx_tail index]
```

- **Producer (`Simcom_FeedBytes`)**: Only modifies `me->rx_head` (and sets `me->rx_overflow` when full). It reads `me->rx_tail` solely to check for buffer full conditions.
- **Consumer (`simcom_ringbuf_pop` inside `Simcom_Poll`)**: Only modifies `me->rx_tail`. It reads `me->rx_head` solely to check for buffer empty conditions.
- **Capacity**: `N - 1` bytes (where `N = SIMCOM_CFG_RX_BUF_SIZE`). The full condition is identified when `(rx_head + 1) % N == rx_tail`.
- **Lock-Free Concurrency**: Because the producer only writes to `rx_head` and the consumer only writes to `rx_tail`, no mutexes or global interrupt disables (`__disable_irq()`) are required, ensuring zero ISR latency and deterministic execution (O(K) where K is the number of ingested bytes).

---

## 2. Dual-Path AT Lexer & Parser

Modems emit two distinct categories of ASCII strings across the same serial channel:
1. **Command Transaction Responses**: Replies to commands initiated by the host microcontroller (`OK`, `ERROR`, `+CME ERROR: <err>`, or command payload data like `+CSQ: 24,99`).
2. **Unsolicited Result Codes (URCs)**: Asynchronous status notifications spontaneously pushed by the modem (e.g., `+CREG: 1`, `+CMTI: ...`, `*ATREADY: 1`, `UNDER-VOLTAGE WARNNING`).

If a driver blindly expects an `OK` after sending an AT command, an incoming URC arriving mid-transaction can corrupt or terminate the parser prematurely.

```
Incoming Stream ──> [simcom_ringbuf] ──> [Line / Raw Lexer]
                                               │
               ┌───────────────────────────────┴───────────────────────────────┐
               ▼                                                               ▼
        [ URC Matcher ]                                          [ Active Transaction ]
  - Static .rodata prefixes                               - Expected response matcher
  - Asynchronous event dispatcher                         - Terminal status (OK/ERROR)
  - State cache update (+CREG, +CSQ)                      - Completes active workflow
```

### Lexer Modes
- **Line Mode (`SIMCOM_LEXER_MODE_LINE`)**: Assembles characters into `me->line_buf` until a delimiter sequence (`\r\n` or standalone `\r` / `\n`) is encountered.
- **Raw Bytes Mode (`SIMCOM_LEXER_MODE_RAW_BYTES`)**: Used for data streams of known byte count (such as HTTP/socket payload chunks).
- **Dual-Path Routing**:
  1. Every assembled line is first checked against the static `.rodata` URC prefix table (e.g. `+CREG:`, `+CGREG:`, `+CEREG:`, `+CSQ:`, `+CPIN:`, `*ATREADY:`, `SMS DONE`).
  2. If identified as a URC, the driver immediately updates cached network/SIM state and dispatches the registered URC callback `urc_cb`. The active transaction remains unaffected.
  3. If not an asynchronous URC, the line is forwarded to the active workflow's transaction parser to match against expected response prefixes, data extractors, or terminal codes (`OK`, `ERROR`, `+CME ERROR:`).

---

## 3. Non-Blocking Event-Driven Finite State Machine (FSM)

### Non-Blocking Polling Model
All asynchronous operations in `libsimcom` are driven cooperatively by calling `Simcom_Poll()` from the application's main superloop:

```c
while (1) {
    App_RunSensors();
    Simcom_Poll(&g_modem);
    App_LowPowerSleep();
}
```

No blocking loops, thread sleeps, or busy-waits (`delay_ms()`) are permitted within `libsimcom`. Each call to `Simcom_Poll()`:
1. Pulls available characters from the SPSC ring buffer.
2. Steps the lexer to parse lines or raw chunks.
3. Tests running timers using unsigned delta math.
4. Advances the active workflow FSM.

### Overflow-Safe Tick Math
All timeout evaluations utilize standard modular unsigned integer subtraction:

```text
delta_t = (now_ms - start_ms)
```

```c
const uint32_t now = me->hal.get_ms();
if ((uint32_t)(now - me->step_start_ms) >= me->step_timeout_ms) {
    /* Step timeout exceeded */
}
```

This guarantees correct behavior across 32-bit tick wrap-arounds (which occurs every ~49.7 days on 1 kHz systems) without edge cases.

---

## 4. Two-Tier Fault Recovery Ladder

Cellular modems operating in harsh RF environments or during power brownouts can freeze, desynchronize serial autobaud rates, or drop active commands. Rather than crashing or remaining stuck in a pending state, `libsimcom` implements a structured two-tier escalation recovery ladder:

```
[ Active Command / Workflow ]
            │ Timeout or CME Error
            ▼
[ Tier 1: Soft Re-Synchronization ]
  - Send AT pulse burst
  - Wait for modem "OK" acknowledgment
            │
            ├─► Recovered ──► Retry or fail gracefully
            │
            ▼ Fails (retries exhausted)
[ Tier 2: Logical Modem Reset ]
  - Issue AT+CRESET
  - Reset ring buffer & lexer state
  - Wait for *ATREADY: 1 or timeout
            │
            ├─► Recovered ──► Transition to SIMCOM_STATE_READY
            │
            ▼ Fails
[ Terminal SIMCOM_STATE_FAULT ]
  - Invoke fault callback
  - Application can trigger hardware power cycle
```

1. **Tier 1 (Soft Re-Sync)**: Sends clean `AT\r\n` probes to clear half-transmitted command fragments, flush line buffers, and confirm the modem is responding to basic commands.
2. **Tier 2 (Logical Reset)**: Issues `AT+CRESET\r\n` to instruct the baseband processor to perform a software reboot, reinitializing internal registers and waiting for the modem's startup ready indicator (`*ATREADY: 1` or timeout).
3. **Hardware Escalation**: If Tier 2 fails, the modem enters `SIMCOM_STATE_FAULT` with fault code `SIMCOM_FAULT_MODEM_UNRESPONSIVE`. The application's supervisory logic or hardware watchdog can then cycle physical power or toggle the hardware reset pin.
