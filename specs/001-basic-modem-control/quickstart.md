# Quickstart & Validation Guide: Basic Modem Initialization and Control (`001-basic-modem-control`)

**Date**: 2026-10-21  
**Spec**: `specs/001-basic-modem-control/spec.md`  
**Data Model**: `specs/001-basic-modem-control/data-model.md`  
**Contracts**: `specs/001-basic-modem-control/contracts/`

---

## 1. Prerequisites

- **C11 Compiler**: GCC (`gcc` >= 10) or Clang (`clang` >= 13) supporting `-std=c11` and `-fstack-usage`.
- **Build System**: CMake (>= 3.20) + Ninja or Make.
- **Static Analysis Tools**: `cppcheck` (>= 2.7) and `clang-tidy`.
- **Unit Test Harness**: Unity (vendored or fetched under `tests/unity/`).

---

## 2. Build & Verification Commands

Run all commands from the repository root (`d:\Pets\simcom\simcom`):

```powershell
# 1. Configure host build with strict compiler warnings and tests enabled
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DSIMCOM_BUILD_TESTS=ON

# 2. Compile library and test suites (enforces zero warnings with -Werror)
cmake --build build

# 3. Execute all unit and chaos/edge-case test suites
ctest --test-dir build --output-on-failure

# 4. Run static analysis gates (Constitution Section 4.3)
cppcheck --enable=all --std=c11 --error-exitcode=1 -I inc src/
clang-tidy src/*.c src/profiles/*.c -- -Iinc -std=c11
```

---

## 3. End-to-End Validation Scenarios

### Scenario 1: Basic Initialization & Fault Recovery Escalation (User Story 1)

- **Contract Reference**: `Simcom_Init()`, `Simcom_StartInit()`, `Simcom_Poll()`, `Simcom_FeedBytes()` in `specs/001-basic-modem-control/contracts/simcom_api_contract.h`.
- **Happy Path Validation**:
  1. Bind `uart_tx_async` and `get_tick_ms` in `simcom_init_t` and call `Simcom_Init(&modem, &init)` → returns `SIMCOM_STATUS_OK`.
  2. Call `Simcom_StartInit(&modem)` → returns `SIMCOM_STATUS_PENDING`; state transitions to `SIMCOM_STATE_INITIALIZING`.
  3. Step `Simcom_Poll(&modem)` while feeding canned A76XX responses (`OK` for `AT`, `ATE0`, `+CFUN: 1\r\nOK`, `OK` for `AT+CMEE=2`, `AT+CREG=1`, `AT+CGREG=1`, `AT+CEREG=1`) via `Simcom_FeedBytes()`.
  4. **Expected Outcome**: `Simcom_GetState(&modem)` reaches `SIMCOM_STATE_READY` within `< 3000 ms` simulated time (SC-001) and fires `SIMCOM_EVENT_INIT_COMPLETE`.
- **Unresponsive Modem & Tier 1 → Tier 2 Recovery Validation**:
  1. Withold `OK` responses and advance mock tick past `default_cmd_timeout_ms` across `max_cmd_retries`.
  2. **Expected Outcome**: Instance transitions to `SIMCOM_STATE_RECOVERY_TIER1` (transmits `tier1_sync_retries` probe `AT\r\n` commands), then escalates to `SIMCOM_STATE_RECOVERY_TIER2` (transmits `AT+CRESET\r\n` and waits `tier2_reset_timeout_ms` for `RDY`), and upon timeout transitions to `SIMCOM_STATE_FAULT` with `Simcom_GetFault(&modem) == SIMCOM_FAULT_MODEM_UNRESPONSIVE` and emits `SIMCOM_EVENT_FAULT`.

### Scenario 2: Device Identity & SIM Card Interrogation / Single-Shot PIN (User Story 2)

- **Contract Reference**: `Simcom_RequestDeviceInfo()`, `Simcom_RequestSimStatus()`, `Simcom_SubmitSimPin()` in `specs/001-basic-modem-control/contracts/simcom_api_contract.h`.
- **Validation Steps**:
  1. In `SIMCOM_STATE_READY`, invoke `Simcom_RequestDeviceInfo(&modem)` and feed `ATI` / identity lines (`Manufacturer: INCORPORATED`, `Model: A7600C`, `Revision: A7600C_V1.0`, `IMEI: 351602000330570`, `OK`).
  2. **Expected Outcome**: `Simcom_GetDeviceInfo()` returns `is_valid == true` with exact field strings (SC-003).
  3. Invoke `Simcom_RequestSimStatus(&modem)`:
     - Feed `+CPIN: READY\r\nOK\r\n` followed by `+ICCID: 89860318760238610932\r\nOK\r\n` → `state == SIMCOM_SIM_STATE_READY`, `is_iccid_valid == true`.
     - Feed `+CME ERROR: 10\r\n` → `state == SIMCOM_SIM_STATE_ABSENT`, modem returns to `SIMCOM_STATE_READY`.
     - Feed `+CPIN: SIM PIN\r\nOK\r\n` → `state == SIMCOM_SIM_STATE_PIN_REQUIRED`.
  4. Invoke `Simcom_SubmitSimPin(&modem, "1234")` when in `SIMCOM_SIM_STATE_PIN_REQUIRED` and simulate `+CME ERROR: 16` (incorrect password) or timeout.
  5. **Expected Outcome**: `AT+CPIN="1234"` is transmitted **exactly once** (zero automatic retries, FR-004), internal `pin_scratch` buffer is zeroed immediately after TX, and failure is reported via `SIMCOM_EVENT_SIM_PIN_RESULT`.

### Scenario 3: Network Registration, URC Dispatch & Signal Quality (User Story 3)

- **Contract Reference**: `Simcom_RequestNetworkStatus()`, `Simcom_GetNetworkStatus()` in `specs/001-basic-modem-control/contracts/simcom_api_contract.h`.
- **Validation Steps**:
  1. Feed an unsolicited `+CEREG: 5\r\n` while idle or mid-command (`AT+CSQ`).
  2. **Expected Outcome**: `SIMCOM_EVENT_NET_REG_URC_CHANGED` fires immediately during `Simcom_Poll()`, updating `net_status.eps_reg` to `SIMCOM_REG_ROAMING` without corrupting the active `AT+CSQ` command (FR-008, FR-010).
  3. Call `Simcom_RequestNetworkStatus(&modem)` and feed responses for `+CREG?`, `+CGREG?`, `+CEREG?`, `+CSQ: 24,0`, `+COPS: 0,0,"CHN-UNICOM",7`, and `+CPSI: LTE,Online,...`.
  4. **Expected Outcome**: `Simcom_GetNetworkStatus()` reports `rssi_raw == 24`, `rssi_dbm == -65`, `act == SIMCOM_ACT_LTE`, `operator_name == "CHN-UNICOM"`, and `system_mode == "LTE"`.

### Scenario 4: Diagnostic Logging & Chaos Resilience (User Story 4 & Constitution Gate 2)

- **Validation Steps**:
  1. Register `log_fn` with `min_log_level = SIMCOM_LOG_LEVEL_WARNING`; verify `DEBUG` and `INFO` events are suppressed while `WARNING` (e.g., RX overflow or command retry) and `ERROR` events invoke `log_fn`.
  2. Set `log_fn = NULL`; verify all operations succeed with zero errors.
  3. Feed a 1024-byte garbage line without `\r\n` into the 512-byte RX buffer via `Simcom_FeedBytes()`, followed by `\r\nOK\r\n`.
  4. **Expected Outcome**: `Simcom_FeedBytes()` returns `SIMCOM_STATUS_BUFFER_FULL` without overwriting memory; `Simcom_Poll()` discards bytes until the `\r\n` boundary and cleanly parses the subsequent `OK\r\n`.
  5. Inspect compiler-generated `.su` stack usage files in `build/`.
  6. **Expected Outcome**: Every public API function uses `<= 128 Bytes` stack and `Simcom_FeedBytes()` uses `<= 32 Bytes` stack.
