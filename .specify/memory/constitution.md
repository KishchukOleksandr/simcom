# Project Constitution: Bare-Metal SIMCom GSM/LTE C Library (`libsimcom`)

**Version:** 1.0.0  
**Last Updated:** 2026-10-15  
**Target Domain:** Bare-Metal Embedded Systems (No OS / Superloop / ISR-Driven)  
**Target Hardware:** SIMCom Cellular Modems (SIM800x, SIM7000x, SIM7070/7080x, SIM7600x, A76xx)

---

## 1. Purpose & Architectural Philosophy

This constitution defines the non-negotiable engineering laws governing `libsimcom`. Every specification, plan, header contract, and implementation file MUST comply with these principles. Any design or pull request violating a **MUST** or **MUST NOT** directive is automatically rejected.

---

## 2. Core Principles

### Principle 1: Zero Dynamic Memory Allocation (Static Determinism)
* **Rationale:** Bare-metal cellular devices operate unattended for years. Heap fragmentation from variable-length AT responses, SMS payloads, or HTTP/MQTT packets causes catastrophic, unrecoverable runtime faults.
* **Rule:**
  * The library **MUST NOT** invoke `malloc()`, `calloc()`, `realloc()`, `free()`, or Variable Length Arrays (VLAs).
  * All instance handles, ring buffers, AT command queues, and payload scratchpads **MUST** be statically allocated by the caller or bounded at compile time via `simcom_cfg.h`.
  * Constant AT command strings and URC lookup tables **MUST** reside in Flash (`.rodata` / `const char`), never copied into RAM unnecessarily.
* **How to Apply:** Pass caller-allocated context structs (`simcom_handle_t * const me`) to all API functions. Enforce buffer bounds using `_Static_assert` in configuration headers.

### Principle 2: Strictly Non-Blocking Execution & FSM Architecture
* **Rationale:** Cellular network attachment, PDP context activation, and TCP/TLS handshakes can take between 5 and 180 seconds. Blocking the MCU superloop starves watchdogs, sensors, and safety-critical control loops.
* **Rule:**
  * The library **MUST NOT** use blocking delay loops (`delay_ms()`), busy-wait polling on UART registers, or unbounded `while()` loops.
  * Everypublic API function **MUST** execute in deterministic $O(1)$ or bounded $O(N)$ time (where $N$ is the static buffer size) and return immediately with a status code (`SIMCOM_STATUS_OK`, `SIMCOM_STATUS_PENDING`, `SIMCOM_STATUS_BUSY`).
  * Multi-step modem workflows (e.g., Power-On -> SIM Check -> CREG/CGREG Wait -> PDP Context Active -> Socket Open) **MUST** be implemented as hierarchical or flat Finite State Machines (FSMs) stepped via a periodic `Simcom_Poll(simcom_handle_t * const me)` call.
* **How to Apply:** Track command timeouts using a monotonic millisecond tick (`get_tick_ms()`) injected via the HAL. Compare elapsed ticks using unsigned wrap-around-safe subtraction: `(current_tick - start_tick) >= timeout_ms`.

### Principle 3: Dual-Path AT Parser (Command/Response vs. Asynchronous URCs)
* **Rationale:** SIMCom modems emit Unsolicited Result Codes (e.g., `+CMTI:`, `+CIPRXGET:`, `+CEREG:`, `RING`, `RDY`, `UNDER-VOLTAGE WARNNING`) at arbitrary moments—including between an active `AT+...` transmission and its terminal `OK`/`ERROR` response, or mid-prompt (`>`).
* **Rule:**
  * The AT parser **MUST** decouple unsolicited event handling (URCs) from synchronous command-response state tracking.
  * Incoming UART bytes **MUST** be ingested via an ISR-safe lock-free SPSC (Single-Producer Single-Consumer) ring buffer or DMA circular buffer.
  * The line/frame tokenizer **MUST** execute strictly inside the background `Simcom_Poll()` context, never inside the UART RX ISR.
  * Binary transfer modes (e.g., `AT+CIPSEND` waiting for `>`, `AT+CIPRXGET=2`, or `AT+HTTPREAD`) **MUST** switch the tokenizer from CRLF-delimited mode to exact byte-count mode to prevent binary `0x0D 0x0A` bytes from prematurely terminating reception.
* **How to Apply:** Implement a two-mode lexer (`LEXER_MODE_LINE` and `LEXER_MODE_RAW_BYTES`) paired with a static URC dispatch table that fires application callbacks without corrupting the active AT command transaction state.

### Principle 4: Total Hardware & MCU Agnosticism (Dependency Injection)
* **Rationale:** The library must run unchanged across STM32, NXP, Microchip, TI, RISC-V, and x86/Linux host unit-test runners without `#ifdef STM32...` clutter.
* **Rule:**
  * Source files **MUST NOT** include vendor HAL headers (e.g., `stm32f4xx_hal.h`, `driver/uart.h`) or access MCU peripheral registers directly.
  * All hardware interactions **MUST** be routed through an injected `simcom_hal_if_t` function-pointer interface provided during `Simcom_Init()`.
* **How to Apply:** Require the following mandatory hooks in `simcom_hal_if_t`:
  * `uart_tx_async(const uint8_t *data, size_t len)`
  * `gpio_write_pwrkey(bool asserted)`
  * `gpio_write_reset(bool asserted)`
  * `gpio_read_status(void)` (optional/nullable for boards without `STATUS` pin wired)
  * `get_tick_ms(void)`
  * `enter_critical(void)` / `exit_critical(void)`

### Principle 5: Defensive String Handling & Memory Safety
* **Rationale:** Cellular networks and SMS messages are untrusted external attack surfaces. Malformed network responses or oversized SMS/HTTP payloads will cause buffer overflows if parsed with standard C library functions.
* **Rule:**
  * The library **MUST NOT** use unsafe `<string.h>` or `<stdio.h>` functions: `strcpy`, `strcat`, `sprintf`, `vsprintf`, `sscanf`, `gets`, or `atoi` (which exhibits undefined behavior on overflow).
  * All tokenization and integer parsing **MUST** operate on explicit pointer-and-length spans (`const uint8_t *buf, size_t len`) with strict boundary validation.
* **How to Apply:** Write custom, bounds-checked AT token parsers (extracting comma-separated parameters and quoted strings) and safe numeric converters returning explicit status codes.

### Principle 6: Multi-Stage Fault Escalation & Physical Recovery
* **Rationale:** SIMCom modems frequently hang due to RF brownouts, base-station handoff failures, or internal firmware deadlocks where `AT` commands stop responding entirely.
* **Rule:**
  * Every AT command **MUST** have an explicit, per-command timeout specification (e.g., `AT` = 1000 ms, `AT+CIICR` = 85000 ms) and a retry limit.
  * The library **MUST** implement a tiered recovery escalation ladder:
    1. **Tier 1 (Soft Recovery):** Re-synchronize parser (`+++` escape sequence or dummy `AT\r\n` retries).
    2. **Tier 2 (Logical Reset):** Issue `AT+CFUN=1,1` or `AT+CRESET` and await `RDY`.
    3. **Tier 3 (Hardware Pulse):** Drive `RESET_N` or `PWRKEY` pin low for the datasheet-specified duration (e.g., 1.2s pulse for SIM800, 2.5s for SIM70xx) via non-blocking FSM states.
    4. **Tier 4 (Hard Power Cut):** Invoke optional HAL power-railfet toggle callback (`vbat_enable(false)` -> wait Capacitor Discharge -> `vbat_enable(true)`).
* **How to Apply:** Track consecutive command timeouts in the instance handle and transition the master FSM to `SIMCOM_STATE_RECOVERY_TIER_X` automatically when thresholds are breached.

---

## 3. Tech Stack & Toolchain Constraints

| Category              | Constraint / Requirement                                                                                                                                                |
| :-------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Language Standard** | ISO C99 or C11 (`-std=c11`). GNU extensions (`typeof`, statement expressions) are **FORBIDDEN**.                                                                        |
| **Permitted Headers** | `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<limits.h>`, and safe subset of `<string.h>` (`memcpy`, `memmove`, `memset`, `memcmp`).                                     |
| **Coding Standard**   | **MISRA C:2012** & **BARR-C:2018** compliant. Fixed-width types (`uint8_t`, `uint16_t`, `uint32_t`, `int32_t`) mandatory. Raw `int`, `short`, `long` are **FORBIDDEN**. |
| **Compiler Warnings** | Must compile with zero warnings under: `-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef -Wcast-qual -fno-common -fstack-usage`.        |
| **Stack Budget**      | Worst-Case Stack Usage (WCSU) per public API call **MUST NOT** exceed `128 Bytes`. ISR helper calls **MUST NOT** exceed `32 Bytes`.                                     |

---

## 4. Verification & Testing Gates

No module feature is complete without passing off-target host verification prior to hardware testing:

1. **Unit & Mock Harness:** 100% of FSM transitions, AT tokenizers, and URC dispatchers **MUST** be testable on an x86/ARM64 host PC using **Unity + CMock** (or Ceedling) by mocking `simcom_hal_if_t`.
2. **Chaos / Edge-Case Injection:** Every feature test suite **MUST** include tests for:
   * Fragmented UART reception (1 byte per `Simcom_Poll()` call).
   * Interleaved URC injection mid-command (e.g., receiving `+CMTI: "SM",1\r\n` immediately after sending `AT+CSQ\r\n` and before `+CSQ: 24,0\r\n\r\nOK\r\n`).
   * Command timeout and automatic transition through Tier 1–3 hardware recovery.
   * Buffer overflow rejection (feeding a 1024-byte corrupted line into a 256-byte RX buffer without memory corruption).
   * `+CME ERROR:` and `+CMS ERROR:` code extraction and mapping.
3. **Static Analysis:** Code **MUST** pass `cppcheck --enable=all --error-exitcode=1` and `clang-tidy` without suppressions unless explicitly documented in the spec.

---

## 5. Governance & Spec-Kit Workflow Rules

1. **Spec First:** No `.c` implementation file may be created or modified without an approved specification in `specs/<feature>/spec.md` and header contract in `inc/`.
2. **Modem Variant Abstraction:** Differences between SIMCom series (e.g., SIM800 CIP TCP stack vs. SIM7600 NETOPEN stack vs. SIM7080 CA TCP stack) **MUST** be isolated in modular profile tables or sub-specs, never via tangled `#ifdef` spaghetti inside the core AT engine.