# Phase 0 Research: Raspberry Pi 5 & Waveshare A7670E HAT Example (`002-rpi5-a7670e-example`)

**Date**: 2026-10-29  
**Spec**: `specs/002-rpi5-a7670e-example/spec.md`  
**Constitution**: `.specify/memory/constitution.md`

---

## 1. Raspberry Pi 5 GPIO 4 (`PWRKEY`) Control Mechanism

- **Decision**: Drive BCM GPIO 4 (Physical Pin 7) on the Raspberry Pi 5 using the standard Linux GPIO Character Device v2 ABI (`<linux/gpio.h>`, `GPIO_V2_GET_LINE_IOCTL` and `GPIO_V2_LINE_SET_VALUES_IOCTL`), auto-detecting the RP1 GPIO controller chip (`/dev/gpiochip4` with automatic fallback to `/dev/gpiochip0`) and allowing CLI override (`--gpio-chip`, `--pwrkey-pin`).
- **Rationale**:
  - **Raspberry Pi 5 RP1 Architecture**: On Raspberry Pi 5, the 40-pin header GPIOs are routed through the RP1 southbridge. Depending on the Raspberry Pi OS Bookworm kernel version (6.6 vs. 6.12+), the 40-pin header is exposed as `/dev/gpiochip4` or aliased as `/dev/gpiochip0`. Legacy `/sys/class/gpio` sysfs is deprecated and disabled by default in modern kernels.
  - **Zero Third-Party Library Dependency**: Using the built-in Linux kernel header `<linux/gpio.h>` avoids requiring users to install `libgpiod-dev` and eliminates breaking API incompatibilities between `libgpiod` v1.x (Debian Bullseye) and `libgpiod` v2.x (Debian Bookworm/Trixie).
  - **Waveshare A7670E `PWRKEY` Polarity & Timing**: When the Waveshare HAT `PWR` jumper is set to `D4`, Raspberry Pi BCM GPIO 4 drives an onboard NPN transistor connected to the A7670E `PWRKEY` pin. Driving GPIO 4 **HIGH (`1`)** pulls `PWRKEY` low (asserted); driving GPIO 4 **LOW (`0`)** releases `PWRKEY`. Holding GPIO 4 HIGH for **600 ms** (above the 500 ms turn-on spec and safely below the 2500 ms power-down threshold) followed by a **3500 ms** boot settle wait reliably wakes the module without powering down an already-awake module.
- **Alternatives Considered**:
  - *Legacy `/sys/class/gpio` sysfs*: Rejected because `CONFIG_GPIO_SYSFS` is disabled in Raspberry Pi 5 kernels.
  - *External `libgpiod` shared library*: Rejected because `libgpiod` v1 and v2 have incompatible C APIs across Linux distributions, whereas `<linux/gpio.h>` ioctls are kernel-ABI stable and require zero external packages.

---

## 2. Non-Blocking Linux Serial I/O (`termios` + `poll`)

- **Decision**: Open the serial device (default `/dev/ttyAMA0`, configurable via `--port`) with `O_RDWR | O_NOCTTY | O_NONBLOCK`, configure raw 8N1 mode via POSIX `<termios.h>` (`cfmakeraw()`, `CLOCAL | CREAD`, hardware flow control disabled `~CRTSCTS`, default `B115200`), and pace the superloop using `poll()` with a `5 ms` timeout on the serial file descriptor.
- **Rationale**:
  - **Raspberry Pi 5 UART0**: On Raspberry Pi 5, enabling `dtparam=uart0=on` in `/boot/firmware/config.txt` exposes the 40-pin header UART (`GPIO14` TXD0 / `GPIO15` RXD0) at `/dev/ttyAMA0`.
  - **Low CPU Utilization (< 5%) & Fast URC Response (< 100 ms)**: Using `poll(&pfd, 1, 5)` inside the superloop sleeps the process for up to 5 ms when no UART bytes are arriving (keeping idle CPU usage `< 1%`, well under SC-003's 5% limit), while waking immediately on incoming UART bytes to call `Simcom_FeedBytes()` and `Simcom_Poll()` within `< 5 ms`.
  - **Non-Blocking Transmit Hook**: `uart_tx_async` calls `write(fd, data, len)` on the non-blocking descriptor. If `write()` returns fewer bytes or `EAGAIN`/`EWOULDBLOCK`, it returns the partial byte count (`0..len`), which `libsimcom` naturally resumes on the next `Simcom_Poll()` cycle.
- **Alternatives Considered**:
  - *Busy-wait superloop without `poll()`*: Rejected because an unthrottled `while(1)` loop consumes 100% of one CPU core on Linux, violating SC-003.
  - *Multi-threaded RX reader thread*: Unnecessary complexity; `poll()` + `read()` -> `Simcom_FeedBytes()` -> `Simcom_Poll()` on a single thread is deterministic and eliminates thread synchronization overhead.

---

## 3. Application Workflow Coordinator FSM

- **Decision**: Implement a non-blocking application-level state machine (`rpi5_app_t`) that sequences the required demonstration workflows in order using `libsimcom`'s `simcom_event_cb_t` notifications:
  1. `RPI5_APP_STATE_PWRKEY_PULSE` (drive GPIO 4 HIGH for 600 ms)
  2. `RPI5_APP_STATE_PWRKEY_WAIT_BOOT` (drive GPIO 4 LOW, wait 3500 ms while draining any early UART bytes/banners)
  3. `RPI5_APP_STATE_MODEM_INIT` (`Simcom_StartInit()` → wait for `SIMCOM_EVENT_INIT_COMPLETE`)
  4. `RPI5_APP_STATE_QUERY_DEV_INFO` (`Simcom_RequestDeviceInfo()` → wait for `SIMCOM_EVENT_DEVICE_INFO_READY`)
  5. `RPI5_APP_STATE_QUERY_SIM` (`Simcom_RequestSimStatus()` → wait for `SIMCOM_EVENT_SIM_STATUS_READY`)
  6. `RPI5_APP_STATE_SUBMIT_PIN` (if `SIMCOM_SIM_STATE_PIN_REQUIRED` and `--pin` provided: `Simcom_SubmitSimPin()` → wait for `SIMCOM_EVENT_SIM_PIN_RESULT`, then re-query SIM status for ICCID)
  7. `RPI5_APP_STATE_QUERY_NET` (`Simcom_RequestNetworkStatus()` → wait for `SIMCOM_EVENT_NET_STATUS_READY`, then print full Modem Status Report)
  8. `RPI5_APP_STATE_MONITOR` (if `--once` not set: remain in continuous monitoring loop, printing asynchronous `SIMCOM_EVENT_NET_REG_URC_CHANGED` events immediately and periodically calling `Simcom_RequestNetworkStatus()` every 10 seconds until `SIGINT`/`SIGTERM`).
- **Rationale**:
  - Keeps `libsimcom` core library 100% unmodified (per Clarification 3 and FR-008).
  - Isolating the application coordinator FSM (`rpi5_app.c`) from the Linux OS calls (`rpi5_platform_linux.c`) allows the entire application workflow, CLI argument parser, and console report formatter to be compiled and unit-tested on any host OS (including Windows/MinGW and Linux CI) using Unity.
- **Alternatives Considered**:
  - *Monolithic `main.c` mixing Linux `termios`/`ioctl` calls directly with workflow logic*: Rejected because it prevents running automated unit tests on non-Linux hosts or CI runners without physical GPIO/UART devices.
