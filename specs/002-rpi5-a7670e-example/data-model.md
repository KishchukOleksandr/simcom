# Phase 1 Data Model: Raspberry Pi 5 & Waveshare A7670E HAT Example (`002-rpi5-a7670e-example`)

**Date**: 2026-10-29  
**Spec**: `specs/002-rpi5-a7670e-example/spec.md`  
**Research**: `specs/002-rpi5-a7670e-example/research.md`

All data structures in the portable application coordinator (`rpi5_app.h` / `rpi5_app.c`) use fixed-width C11 integer types (`uint8_t`, `uint16_t`, `uint32_t`, `int32_t`, `bool`, `size_t`) and static allocation in alignment with Constitution Principles 1, 2, and Section 3.

---

## 1. Example Runtime Configuration (`rpi5_example_cfg_t`)

Parsed from command-line arguments (`argc`, `argv`) prior to hardware initialization.

| Field | Type | Default | Validation Rule | Description |
| :--- | :--- | :--- | :--- | :--- |
| `serial_port` | `const char *` | `"/dev/ttyAMA0"` | Non-empty string | Serial device path (`--port <path>`). |
| `baud_rate` | `uint32_t` | `115200U` | One of `9600`, `19200`, `38400`, `57600`, `115200`, `230400`, `460800`, `921600` | UART baud rate (`--baud <rate>`). |
| `gpio_chip` | `const char *` | `NULL` (auto: `/dev/gpiochip4` → `/dev/gpiochip0`) | Nullable or non-empty path | GPIO character device path (`--gpio-chip <path>`). |
| `pwrkey_gpio_pin` | `uint32_t` | `4U` | `<= 53U` | BCM GPIO pin number wired to HAT `PWRKEY` (`--pwrkey-pin <num>`). |
| `pwrkey_pulse_ms` | `uint32_t` | `600U` | `>= 100U && <= 2000U` | Duration in ms to hold GPIO 4 HIGH during startup power-on. |
| `pwrkey_boot_wait_ms` | `uint32_t` | `3500U` | `>= 500U && <= 15000U` | Post-pulse wait in ms before starting AT handshake. |
| `monitor_interval_ms` | `uint32_t` | `10000U` | `>= 1000U && <= 300000U` | Interval between periodic network/signal refreshes in continuous mode. |
| `sim_pin` | `const char *` | `NULL` | Nullable; if set, 4–8 ASCII digits `'0'..'9'` | Optional SIM PIN code (`--pin <digits>`). |
| `log_level` | `simcom_log_level_t` | `SIMCOM_LOG_LEVEL_INFO` (`SIMCOM_LOG_LEVEL_DEBUG` if `--verbose`) | Valid `simcom_log_level_t` | Minimum diagnostic log threshold (`--verbose` / `--quiet`). |
| `run_once` | `bool` | `false` | Boolean | Exit immediately after initial status report if `true` (`--once`). |
| `show_help` | `bool` | `false` | Boolean | Print CLI usage help and exit `0` if `true` (`--help` / `-h`). |

---

## 2. Platform Hardware Adapter Interface (`rpi5_platform_if_t`)

Decouples the example's workflow state machine from Linux kernel/POSIX system calls so the application coordinator can run both on physical Raspberry Pi 5 hardware and in host unit tests.

| Field | Signature | Mandatory? | Description |
| :--- | :--- | :--- | :--- |
| `uart_tx` | `size_t (*)(const uint8_t *data, size_t len, void *ctx)` | **Yes** | Non-blocking serial write (forwarded from `simcom_hal_if_t.uart_tx_async`). |
| `get_tick_ms` | `uint32_t (*)(void *ctx)` | **Yes** | Monotonic millisecond clock (`CLOCK_MONOTONIC` on Linux). |
| `gpio_set_pwrkey` | `bool (*)(bool high, void *ctx)` | **Yes** | Drives BCM GPIO 4 (`high = true` asserts `PWRKEY`, `high = false` releases `PWRKEY`). |
| `console_print` | `void (*)(const char *text, void *ctx)` | **Yes** | Outputs formatted status lines and diagnostic logs to console/test capture buffer. |
| `ctx` | `void *` | Optional (`NULL` ok) | Opaque platform context (`rpi5_linux_ctx_t *` on Raspberry Pi 5, mock struct in tests). |

---

## 3. Application Coordinator State Machine (`rpi5_app_state_t` & `rpi5_app_t`)

### 3.1 Lifecycle States (`rpi5_app_state_t`)

| State | Value | Entry Action | Transition Condition |
| :--- | :--- | :--- | :--- |
| `RPI5_APP_STATE_IDLE` | `0` | Initialized via `Rpi5App_Init()` | `Rpi5App_Start()` → `RPI5_APP_STATE_PWRKEY_ASSERT` |
| `RPI5_APP_STATE_PWRKEY_ASSERT` | `1` | Assert GPIO 4 HIGH (`gpio_set_pwrkey(true)`) | `(now - phase_start_ms) >= pwrkey_pulse_ms` → `RPI5_APP_STATE_PWRKEY_WAIT_BOOT` |
| `RPI5_APP_STATE_PWRKEY_WAIT_BOOT` | `2` | Release GPIO 4 LOW (`gpio_set_pwrkey(false)`) | `(now - phase_start_ms) >= pwrkey_boot_wait_ms` (or `RDY` observed) → `RPI5_APP_STATE_MODEM_INIT` |
| `RPI5_APP_STATE_MODEM_INIT` | `3` | Call `Simcom_StartInit(&modem)` | `SIMCOM_EVENT_INIT_COMPLETE` (`OK`) → `RPI5_APP_STATE_QUERY_DEV_INFO`; on fault → `RPI5_APP_STATE_ERROR` |
| `RPI5_APP_STATE_QUERY_DEV_INFO` | `4` | Call `Simcom_RequestDeviceInfo(&modem)` | `SIMCOM_EVENT_DEVICE_INFO_READY` → print Device Identity & transition to `RPI5_APP_STATE_QUERY_SIM` |
| `RPI5_APP_STATE_QUERY_SIM` | `5` | Call `Simcom_RequestSimStatus(&modem)` | `SIMCOM_EVENT_SIM_STATUS_READY`: if `READY` → `RPI5_APP_STATE_QUERY_NET`; if `PIN_REQUIRED` & `sim_pin != NULL` & `!pin_attempted` → `RPI5_APP_STATE_SUBMIT_PIN`; else report SIM state & transition to `RPI5_APP_STATE_QUERY_NET` |
| `RPI5_APP_STATE_SUBMIT_PIN` | `6` | Call `Simcom_SubmitSimPin(&modem, sim_pin)` (`pin_attempted = true`) | `SIMCOM_EVENT_SIM_PIN_RESULT` (`OK`) → `RPI5_APP_STATE_QUERY_SIM` (to read ICCID); on failure → report error & transition to `RPI5_APP_STATE_QUERY_NET` |
| `RPI5_APP_STATE_QUERY_NET` | `7` | Call `Simcom_RequestNetworkStatus(&modem)` | `SIMCOM_EVENT_NET_STATUS_READY` → print complete Modem Status Report; if `run_once` → `RPI5_APP_STATE_DONE`; else → `RPI5_APP_STATE_MONITOR` |
| `RPI5_APP_STATE_MONITOR` | `8` | Record `last_net_poll_ms = now` | Every `monitor_interval_ms` when `Simcom_GetState(&modem) == SIMCOM_STATE_READY`, call `Simcom_RequestNetworkStatus(&modem)` and print updated telemetry on `SIMCOM_EVENT_NET_STATUS_READY`; also log `SIMCOM_EVENT_NET_REG_URC_CHANGED` immediately. |
| `RPI5_APP_STATE_DONE` | `9` | Clean completion of `--once` or user stop | Terminal state (`exit_code = 0`). |
| `RPI5_APP_STATE_ERROR` | `10` | Print actionable diagnostic message | Terminal error state (`exit_code != 0`). |

### 3.2 Coordinator Context (`rpi5_app_t`)

| Field | Type | Description |
| :--- | :--- | :--- |
| `modem` | `simcom_handle_t` | Embedded `libsimcom` static instance handle. |
| `cfg` | `rpi5_example_cfg_t` | Active runtime configuration. |
| `platform` | `rpi5_platform_if_t` | Bound platform adapter callbacks. |
| `state` | `rpi5_app_state_t` | Current application workflow state. |
| `phase_start_ms` | `uint32_t` | Timestamp of current state entry or last monitor query. |
| `pin_attempted` | `bool` | Single-shot guard ensuring SIM PIN is never submitted more than once. |
| `initial_report_printed` | `bool` | Tracks whether the initial full status summary has been emitted. |
| `exit_code` | `int32_t` | Process exit status (`0` = success, non-zero = fault/error). |
