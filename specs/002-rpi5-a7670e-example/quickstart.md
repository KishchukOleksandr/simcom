# Quickstart & Validation Guide: Raspberry Pi 5 & Waveshare A7670E HAT Example (`002-rpi5-a7670e-example`)

**Date**: 2026-10-29  
**Spec**: `specs/002-rpi5-a7670e-example/spec.md`  
**Data Model**: `specs/002-rpi5-a7670e-example/data-model.md`  
**Contracts**: `specs/002-rpi5-a7670e-example/contracts/rpi5_example_contract.h`

---

## 1. Hardware Prerequisites & Waveshare A7670E HAT Setup

1. **Board & Module**:
   - Raspberry Pi 5 (running Raspberry Pi OS Bookworm or later) with official 5V/5A USB-C power supply.
   - [Waveshare A7670E Cat-1/GNSS HAT](https://www.waveshare.com/wiki/A7670E_Cat-1/GNSS_HAT#Features) seated on the Raspberry Pi 5 40-pin GPIO header.
   - Activated nano-SIM card inserted into the HAT SIM slot and LTE Main Antenna connected to the `MAIN` SMA connector.
2. **Waveshare A7670E HAT Yellow Jumper Configuration**:
   - **`PWR` → `D4`**: Connects the modem `PWRKEY` control circuit to Raspberry Pi **BCM GPIO 4** (Physical Pin 7).
   - **`TXD` → `RXD` / `RXD` → `TXD`** (Raspberry Pi 40-pin UART cross-connection per Waveshare silkscreen: connect HAT `RXD` to Pi `TXD0` GPIO14 and HAT `TXD` to Pi `RXD0` GPIO15).
3. **Raspberry Pi 5 OS Serial & GPIO Configuration**:
   - Enable the 40-pin header UART (`ttyAMA0`) in `/boot/firmware/config.txt`:
     ```ini
     enable_uart=1
     dtparam=uart0=on
     ```
   - Disable the Linux serial login shell on `ttyAMA0` via `sudo raspi-config` → *Interface Options* → *Serial Port* → *Login shell over serial: No*, *Serial port hardware enabled: Yes*, then reboot.
   - Ensure your user belongs to the `dialout` and `gpio` groups:
     ```bash
     sudo usermod -aG dialout,gpio $USER
     ```

---

## 2. Build & Verification Commands

### 2.1 On Raspberry Pi 5 (Target Hardware Build)

```bash
# 1. Configure CMake with examples and tests enabled
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSIMCOM_BUILD_EXAMPLES=ON -DSIMCOM_BUILD_TESTS=ON

# 2. Build core library, Raspberry Pi 5 example executable, and unit tests
cmake --build build

# 3. Run off-target unit test suite (including rpi5_app coordinator tests)
ctest --test-dir build --output-on-failure

# 4. Run the example on Raspberry Pi 5 (default: /dev/ttyAMA0, 115200 baud, GPIO 4 PWRKEY, continuous monitor)
./build/examples/rpi5_a7670e/simcom_rpi5_a7670e

# 5. Run single-shot inspection with verbose AT logging and SIM PIN
./build/examples/rpi5_a7670e/simcom_rpi5_a7670e --port /dev/ttyAMA0 --baud 115200 --pin 1234 --verbose --once
```

### 2.2 On Development Host (Windows / Linux CI Verification)

```powershell
# Configure and build all unit tests (including test_rpi5_example)
cmake -B build -DSIMCOM_BUILD_EXAMPLES=ON -DSIMCOM_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## 3. End-to-End Validation Scenarios

### Scenario 1: Hardware Power-On via GPIO 4 & Full Modem Inspection (User Story 1)

- **Contract Reference**: `Rpi5App_Init()`, `Rpi5App_Start()`, `Rpi5App_Poll()`, `Rpi5App_FeedBytes()` in `specs/002-rpi5-a7670e-example/contracts/rpi5_example_contract.h`.
- **Validation Steps**:
  1. Launch `simcom_rpi5_a7670e --once` on the Raspberry Pi 5 (or execute `test_rpi5_example` in host test suite).
  2. **Expected Outcome**:
     - Drives BCM GPIO 4 HIGH for `600 ms`, then LOW for `3500 ms` boot settle period.
     - Executes `Simcom_StartInit()`, transitions to `SIMCOM_STATE_READY`.
     - Queries and prints Device Identity (`Manufacturer`, `Model: A7670E`, `Revision`, `IMEI`).
     - Queries and prints SIM Card status (`READY`, 20-digit `ICCID`).
     - Queries and prints Network & Signal Telemetry (`CS/PS/EPS` registration states, `RSSI`/`dBm`, `BER`, `Operator Name`, `Access Technology`, `Serving Cell Mode`).
     - Exits with code `0` within `< 15 seconds` (SC-002).

### Scenario 2: CLI Arguments, SIM PIN Unlock, and Verbose Logging (User Story 2)

- **Contract Reference**: `Rpi5App_ParseArgs()` in `specs/002-rpi5-a7670e-example/contracts/rpi5_example_contract.h`.
- **Validation Steps**:
  1. Run with `--port /dev/ttyUSB2 --baud 115200 --pin 1234 --verbose --once`.
  2. When `AT+CPIN?` returns `+CPIN: SIM PIN`, the coordinator invokes `Simcom_SubmitSimPin(&modem, "1234")` exactly once, re-queries `AT+CPIN?` + `AT+CICCID`, and prints the unlocked ICCID.
  3. When `--pin` is omitted on a PIN-locked SIM, the coordinator logs a clear warning that a SIM PIN is required and skips `Simcom_SubmitSimPin()` without risking SIM lockout.

### Scenario 3: Continuous Monitoring & Graceful Signal Shutdown (User Story 1 & Edge Cases)

- **Contract Reference**: `Rpi5App_RequestStop()` in `specs/002-rpi5-a7670e-example/contracts/rpi5_example_contract.h`.
- **Validation Steps**:
  1. Run `simcom_rpi5_a7670e` without `--once` so it enters `RPI5_APP_STATE_MONITOR`.
  2. Observe idle CPU usage `< 5%` via `top` (SC-003) and immediate console logging when `+CREG:` / `+CEREG:` URCs arrive.
  3. Send `SIGINT` (`Ctrl+C`) or `SIGTERM`.
  4. **Expected Outcome**: Signal handler invokes `Rpi5App_RequestStop()`, loop exits cleanly, GPIO line and serial file descriptors are closed, and process exits with status `0` (SC-004).
