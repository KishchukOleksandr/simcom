# Phase 0 Research: Basic Modem Initialization and Control (`001-basic-modem-control`)

**Date**: 2026-10-21  
**Spec**: `specs/001-basic-modem-control/spec.md`  
**Constitution**: `.specify/memory/constitution.md`

---

## 1. I/O Transport & ISR-Safe SPSC Ring Buffer (Push RX / Non-Blocking TX)

- **Decision**: Implement a push-based RX entry point `Simcom_FeedBytes(simcom_handle_t * const me, const uint8_t *data, size_t len)` backed by a statically allocated, lock-free Single-Producer Single-Consumer (SPSC) ring buffer owned by `simcom_handle_t`, paired with an application-provided non-blocking `uart_tx_async(const uint8_t *data, size_t len, void *user_ctx)` callback returning the number of bytes accepted (`size_t`).
- **Rationale**:
  - Embedded UART drivers most commonly receive data via RXNE interrupts or DMA half/full/idle-line callbacks. Pushing received bytes directly into an internal SPSC ring buffer keeps the ISR footprint under the constitution's 32-byte Worst-Case Stack Usage (WCSU) budget and avoids performing any parsing in interrupt context (Principle 3).
  - Having `uart_tx_async` return the number of bytes actually accepted allows `Simcom_Poll()` to incrementally stream commands across multiple poll cycles if the MCU's TX FIFO or DMA buffer is temporarily full (Edge Case: Partial Writes).
  - On RX ring buffer saturation, `Simcom_FeedBytes()` writes as many bytes as fit, sets an atomic/volatile `rx_overflow` flag on the parser state, and drops the excess bytes safely without overwriting unread data. During the next `Simcom_Poll()`, the line lexer discards characters up to and including the next `\r\n` boundary before resuming normal tokenization (Edge Case: RX Buffer Overflow).
- **Alternatives Considered**:
  - *Pull model (`uart_rx_read` callback polled in `Simcom_Poll`)*: Rejected during specification clarification because it forces the application to maintain its own duplicate ring buffer in addition to the library's line assembly buffer.
  - *Zero-copy DMA buffer sharing*: Rejected because pointer wrap-around and hardware-specific DMA register semantics leak MCU-specific constraints into the portable API, violating Principle 4.

---

## 2. Dual-Path AT Lexer & URC Dispatch Engine

- **Decision**: Implement a two-mode character lexer (`SIMCOM_LEXER_MODE_LINE` and `SIMCOM_LEXER_MODE_RAW_BYTES`) executed exclusively inside `Simcom_Poll()` that classifies complete lines into:
  1. **Terminal Result Codes** (`OK`, `ERROR`, `+CME ERROR: <code>`, `+CMS ERROR: <code>`)
  2. **Unsolicited Result Codes (URCs)** matched against a `.rodata` prefix table (`+CREG:`, `+CGREG:`, `+CEREG:`, `RDY`, `+CPIN:`, `*ATREADY:`, `SMS DONE`, `PB DONE`)
  3. **Solicited Information Responses** (e.g., `+CSQ:`, `+ICCID:`, `+COPS:`, `+CPSI:`, `Manufacturer:`, `Model:`, `Revision:`, `IMEI:`) routed to the active command descriptor's response parser.
- **Rationale**:
  - Note on dual-use prefixes (`+CREG:`, `+CGREG:`, `+CEREG:`, `+CPIN:`): In 3GPP AT protocols, `+CREG: <n>,<stat>[,...]` (two or more comma-separated parameters) is the solicited response to `AT+CREG?`, whereas `+CREG: <stat>[,<lac>,<ci>,<AcT>]` (where no `<n>` mode prefix is present, or when no `AT+CREG?` command is active) is an asynchronous URC. The dispatcher inspects both whether a matching query command is currently awaiting a response and the parameter structure of the line, guaranteeing that URCs arriving mid-command (e.g., `+CEREG: 1` arriving while awaiting `AT+CSQ` response) update the cached registration state and fire the URC callback without disturbing the active `AT+CSQ` transaction (Principle 3, FR-010).
- **Alternatives Considered**:
  - *Single regex/sscanf parser*: Forbidden by Principle 5 (`sscanf` is unsafe and pulls in heavy libc formatting routines) and cannot separate interleaved URCs from active command responses.

---

## 3. Non-Blocking Hierarchical FSM & Command Transaction Engine

- **Decision**: Structure execution around two coupled state machines stepped by `Simcom_Poll()`:
  1. **AT Transaction FSM (`simcom_at_fsm_t`)**: Manages a single command lifecycle (`IDLE` → `TX_PENDING` → `WAIT_RESPONSE` → `COMPLETED` / `TIMEOUT` / `ERROR`), handling partial TX writes, per-command response timeouts via unsigned tick subtraction `(current_tick - start_tick) >= timeout_ms`, and per-command retry counts.
  2. **Master Workflow FSM (`simcom_fsm_state_t`)**: Executes multi-step sequences driven by const sequence tables in `.rodata`:
     - **Initialization Sequence**: `AT` (handshake) → `ATE0` (echo off) → `AT+CFUN?` (check power functionality; if `!= 1`, issue `AT+CFUN=1`) → `AT+CMEE=2` (enable numeric `+CME ERROR` codes for reliable SIM/network fault parsing) → `AT+CREG=1` → `AT+CGREG=1` → `AT+CEREG=1` (enable network registration URCs per FR-008) → transition to `READY`.
     - **Device Identity Workflow**: Queries `ATI` first to parse labeled fields (`Manufacturer:`, `Model:`, `Revision:`, `IMEI:`), followed by conditional `AT+CGMM`, `AT+CGMR`, and `AT+CGSN` fallback steps if any field remains unpopulated in `simcom_device_info_t`.
     - **SIM Validation & ICCID Workflow**: Queries `AT+CPIN?`; if `READY`, automatically follows with `AT+CICCID` to populate `simcom_sim_info_t`; if `SIM PIN` / `SIM PUK` / `+CME ERROR: 10` (SIM not inserted) / `+CME ERROR: 13` (SIM failure), maps directly to `simcom_sim_state_t` without hanging.
     - **Explicit PIN Unlock Workflow**: Sends `AT+CPIN="<pin>"` with retry count forced to `0` (single attempt only, never retried per FR-004) and refreshes `AT+CPIN?`.
     - **Network & Signal Query Workflow**: Executes `AT+CREG?`, `AT+CGREG?`, `AT+CEREG?`, `AT+CSQ`, `AT+COPS?`, and `AT+CPSI?` to populate `simcom_net_status_t`.
- **Rationale**:
  - Declarative `.rodata` step tables minimize RAM usage (Principle 1), keep cyclomatic complexity low for MISRA C:2012 compliance, and guarantee every `Simcom_Poll()` call completes in bounded microseconds—well inside the 5 ms budget (SC-005) and 128-byte stack budget (Section 3).
- **Alternatives Considered**:
  - *RTOS thread with blocking semaphore waits*: Violates Principle 2 and Target Domain (Bare-Metal No-OS Superloop).
  - *Deeply nested switch-case without command descriptors*: Harder to maintain across modem families and duplicates timeout/retry bookkeeping.

---

## 4. SIMCom A76XX Modem Profile Abstraction

- **Decision**: Define an immutable `simcom_modem_profile_t` descriptor in Flash (`inc/simcom_profile.h`, implemented in `src/profiles/simcom_profile_a76xx.c`) containing series identification, default per-command timeouts, initialization sequence steps, identity query commands, ICCID query syntax (`AT+CICCID` returning `+ICCID:`), system info query syntax (`AT+CPSI?`), and Tier 2 logical reset command (`AT+CRESET\r\n` with fallback `AT+CFUN=1,1\r\n`) + expected boot banner (`RDY`).
- **Rationale**:
  - A76XX AT Command Manual V2.04 uses `AT+CICCID` (returning `+ICCID: <20-digit>`), `AT+CRESET` / `AT+CFUN=6` / `AT+CFUN=1,1` for reset, and `AT+CPSI?` for detailed serving cell metrics, whereas older SIM800 modules use `AT+CCID` and lack `+CEREG` / `+CPSI`. Isolating these in `simcom_profile_a76xx` satisfies FR-014 and Constitution Section 5 Rule 2 without `#ifdef` clutter.
- **Alternatives Considered**:
  - *Compile-time `#ifdef SIMCOM_SERIES_A76XX` macros in core parser*: Explicitly forbidden by Constitution Section 5 Rule 2.

---

## 5. Two-Tier Fault Escalation Ladder (Tiers 1 & 2)

- **Decision**:
  - **Command-Level Retry**: When an AT command times out, retry the command up to `cfg.max_cmd_retries` (except non-idempotent commands like `AT+CPIN="<pin>"`, which have `retries = 0`).
  - **Tier 1 (Soft Re-Synchronization)**: If command retries are exhausted, transition to `SIMCOM_STATE_RECOVERY_TIER1`. Flush RX parser line state and transmit up to `cfg.tier1_sync_retries` (default 3) probe `AT\r\n` commands with a 1000 ms timeout each. If `OK` is received, re-verify `ATE0` and resume/report recovered state.
  - **Tier 2 (Logical Reset)**: If Tier 1 fails to receive `OK`, transition to `SIMCOM_STATE_RECOVERY_TIER2`. Transmit the profile's logical reset command (`AT+CRESET\r\n`) and wait up to `cfg.tier2_reset_timeout_ms` (default 10000 ms) for the `RDY` URC (or poll `AT\r\n` after reboot delay), then re-run the initialization sequence.
  - **Exhausted Ladder**: If Tier 2 fails to re-establish responsiveness, transition to `SIMCOM_STATE_FAULT` with fault code `SIMCOM_FAULT_MODEM_UNRESPONSIVE` and invoke the application fault/event callback. Every subsequent `Simcom_Poll()` or API call returns immediately without blocking.
- **Rationale**:
  - Aligns strictly with FR-015 and Clarifications Session 2026-10-21 while preserving state enum placeholders and architectural hooks for Tiers 3–4 in future hardware-control features.
- **Alternatives Considered**:
  - *Infinite retry loop*: Risks hanging the application logic silently and masking hardware brownouts.

---

## 6. Configuration & Diagnostic Logging Design

- **Decision**:
  - Provide `inc/simcom_cfg.h` for compile-time static memory bounds (`SIMCOM_CFG_RX_BUF_SIZE`, `SIMCOM_CFG_LINE_BUF_SIZE`, `SIMCOM_CFG_MAX_CMD_LEN`, `SIMCOM_CFG_LOG_MAX_LEN`, default timeouts/retries) validated via C11 `_Static_assert`.
  - Allow runtime override of timing, retry counts, and minimum log severity via `simcom_runtime_cfg_t` passed inside `simcom_init_t` (and updatable at runtime via `Simcom_SetConfig()`).
  - Provide an optional logging callback `simcom_log_fn_t` receiving `(simcom_log_level_t level, uint32_t timestamp_ms, simcom_log_dir_t direction, const uint8_t *payload, size_t len, void *user_ctx)`. If `log_fn == NULL` or `level < min_log_level`, the check short-circuits in $O(1)$ with zero formatting cost (FR-011, SC-006).
- **Rationale**:
  - Separating compile-time buffer sizes (`simcom_cfg.h`) from runtime operational parameters (`simcom_runtime_cfg_t`) satisfies both Principle 1 (static determinism with `_Static_assert`) and FR-003 / User Story 1 Scenario 3 (custom timing, retry, and log threshold configuration).
- **Alternatives Considered**:
  - *`printf`-style variadic formatting inside the library*: Forbidden by Principle 5 (`vsprintf`/`vsnprintf` stack usage exceeds the 128-byte WCSU limit). Passing structured event codes and raw byte spans `(payload, len)` keeps stack usage minimal.

---

## 7. Build, Static Analysis & Off-Target Verification Setup

- **Decision**: Use CMake (`CMakeLists.txt`) with C11 standard (`-std=c11`) and strict compiler flags (`-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef -Wcast-qual -fno-common -fstack-usage`), Unity test framework for unit/chaos testing using a deterministic mock HAL (`tests/mocks/mock_simcom_hal.c`), and targets for `cppcheck` and `clang-tidy`.
- **Rationale**:
  - Meets Constitution Sections 3 and 4, enabling 100% off-target verification of fragmented 1-byte reception, mid-command URC injection, RX buffer overflow resynchronization, PIN single-shot safety, and Tier 1/Tier 2 recovery escalation.
