# Feature Specification: Raspberry Pi 5 & Waveshare A7670E HAT Example Project

**Feature Branch**: `002-rpi5-a7670e-example`

**Created**: 2026-10-29

**Status**: Draft

**Input**: User description: "add example project for raspberry PI5 and Waveshare A7670E Cat-1/GNSS HAT [A7670E Cat-1/GNSS HAT - Waveshare Wiki](https://www.waveshare.com/wiki/A7670E_Cat-1/GNSS_HAT#Features)"

## Clarifications

### Session 2026-10-29

- Q: How should the Raspberry Pi 5 example handle modem power-on (`PWR` jumper vs. GPIO 4)? → A: Mandatory GPIO 4 `PWRKEY` sequence — always drive Raspberry Pi 5 BCM GPIO 4 during startup to power on/wake the HAT before running serial modem initialization.
- Q: What functional scope should this example cover (Cellular only vs. GNSS)? → A: Cellular only (existing core library capabilities) — demonstrate modem power-on, initialization, device info, SIM/PIN status, and network/signal monitoring; GNSS is out of scope.
- Q: Where should `PWRKEY` control reside (Example platform layer vs. Core library HAL/FSM)? → A: Example platform layer only — the Raspberry Pi 5 example drives GPIO 4 (`PWRKEY`) before starting the modem library, leaving the core library headers and state machine unmodified.
- Q: What should the default execution mode be after the initial status inspection? → A: Continuous monitor by default (with `--once` flag) — complete the status summary, then stay in a continuous loop displaying live URCs and periodic signal updates until `Ctrl+C` (or exit immediately after summary if `--once` is passed).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - End-to-End Modem Bring-Up and Status Inspection on Raspberry Pi 5 (Priority: P1)

A hardware evaluator or embedded developer attaching a Waveshare A7670E Cat-1/GNSS HAT to a Raspberry Pi 5 needs a ready-to-run example application that powers on the modem via GPIO 4 (`PWRKEY`), initializes the modem over UART, verifies communication, retrieves device and SIM card identity, and displays live network registration and signal quality in the console.

**Why this priority**: New users adopting the modem control library need an immediate, working reference implementation on real hardware to verify their physical setup (HAT jumpers, GPIO power control, antennas, SIM card, and serial port connection) and see how the library's asynchronous workflows operate in practice.

**Independent Test**: Connect the Waveshare A7670E HAT (with `PWR` jumper set to `D4`) to a Raspberry Pi 5, launch the example application, and observe the application drive GPIO 4 to power on the modem, complete the initialization sequence, print hardware metadata (manufacturer, model, revision, IMEI), report SIM card readiness and ICCID, and output cellular network registration and signal strength metrics.

**Acceptance Scenarios**:

1. **Given** a Raspberry Pi 5 connected to a Waveshare A7670E HAT with the `PWR` jumper set to `D4` (BCM GPIO 4) and serial port connected, **When** the user launches the example application, **Then** the application executes the GPIO 4 `PWRKEY` power-on pulse, awaits modem boot readiness, establishes serial communication, completes the initialization sequence, and outputs confirmation of operational readiness.
2. **Given** an initialized Waveshare A7670E HAT with an active unlocked SIM card, **When** the example progresses through its inspection sequence, **Then** it retrieves and displays the modem manufacturer, model (`A7670E`), firmware revision, IMEI, SIM status, and ICCID.
3. **Given** a modem with an attached cellular antenna, **When** the example queries network status, **Then** it displays the current registration state (home, roaming, searching, or denied), radio signal strength (RSSI and dBm), active access technology, and registered network operator name.
4. **Given** the example application is running in continuous monitoring mode, **When** the modem emits unsolicited network registration changes or the user triggers a graceful stop (e.g., interrupt signal), **Then** the application logs the live network updates to the console and exits cleanly after releasing serial and GPIO resources.

---

### User Story 2 - Configurable Connection Parameters and SIM PIN Unlocking (Priority: P2)

A developer testing different hardware connection topologies (such as 40-pin GPIO UART versus USB Type-C virtual serial port) or using a PIN-protected SIM card needs to pass runtime parameters (serial device path, baud rate, optional SIM PIN, and diagnostic log verbosity) without modifying and recompiling the example source code.

**Why this priority**: The Waveshare A7670E HAT supports both UART via the Raspberry Pi 40-pin header and USB serial connections, and cellular SIM cards frequently have PIN protection enabled by carriers. Runtime configurability makes the example immediately usable across all standard Waveshare HAT configurations.

**Independent Test**: Launch the example application with command-line flags specifying an alternate serial port (e.g., USB serial port), custom log level, or SIM PIN code, and verify that the application applies those runtime settings without recompilation.

**Acceptance Scenarios**:

1. **Given** the user connects the Waveshare A7670E HAT via either the Raspberry Pi 5 GPIO UART header or the USB interface, **When** the user starts the example specifying a custom serial port path and baud rate, **Then** the example opens the specified port and communicates with the modem using those settings.
2. **Given** a SIM card that requires a PIN code and a user who provides the PIN at startup, **When** the example detects the SIM is in the PIN-required state, **Then** it submits the provided PIN once, verifies the SIM transitions to the ready state, and proceeds to read the ICCID and network status.
3. **Given** a SIM card that requires a PIN code and no PIN was provided by the user, **When** the example detects the PIN-required state, **Then** it clearly reports that a SIM PIN is required and halts SIM-dependent network steps without risking SIM lockout.
4. **Given** a user troubleshooting serial communication, **When** the user enables verbose diagnostic logging via a startup option, **Then** the application outputs timestamped communication logs and state transitions to the terminal.

---

### User Story 3 - Hardware Setup and Onboarding Guide for Raspberry Pi 5 & Waveshare A7670E HAT (Priority: P3)

A developer setting up the Waveshare A7670E Cat-1/GNSS HAT with a Raspberry Pi 5 for the first time needs clear documentation covering physical jumper placement (`PWR` connected to `D4`, `RXD`/`TXD` placement), Raspberry Pi 5 serial port and GPIO configuration, antenna connection, power-on behavior, and step-by-step instructions to build and run the example.

**Why this priority**: Raspberry Pi 5 introduced changes to both the onboard UART architecture and the RP1 GPIO controller compared to earlier Raspberry Pi models, and the Waveshare A7670E HAT requires specific onboard jumper configurations (`PWR` to `D4`, `RXD`/`TXD`) before software can power on and communicate with the modem.

**Independent Test**: A developer following the documentation from a fresh Raspberry Pi 5 setup can configure the hardware jumpers, enable the serial and GPIO interfaces, build the example project, and run it to successful modem registration without consulting external troubleshooting forums.

**Acceptance Scenarios**:

1. **Given** an unconfigured Raspberry Pi 5 and Waveshare A7670E HAT, **When** the user follows the example documentation, **Then** they have clear instructions for setting the HAT jumpers (`PWR` to `D4`), enabling the Raspberry Pi 5 UART and GPIO access, attaching the LTE antenna and nano-SIM, and powering the board.
2. **Given** the project repository on a Raspberry Pi 5 (or cross-compilation host), **When** the user follows the build and run instructions, **Then** the example compiles cleanly and executes against the connected HAT.

---

### Edge Cases

- **Invalid or Inaccessible Serial Port or GPIO Line**: The specified serial device or GPIO chip/line does not exist, is already locked by another process, or the current user lacks permissions (`dialout` / `gpio` groups) to open it; the example MUST report a clear, actionable error message and exit gracefully.
- **Modem Unresponsive After GPIO Power Sequence**: The HAT is attached to the Raspberry Pi 5 and GPIO 4 is pulsed, but the modem still does not respond over UART (e.g., `PWR` jumper not set to `D4`, `TXD`/`RXD` jumpers disconnected, or insufficient power supply); the example MUST run through the library's recovery tiers, log timeout warnings, and report an actionable "modem unresponsive" diagnostic hinting at jumper and power checks.
- **SIM Card Missing or PUK-Locked**: The nano-SIM slot on the Waveshare HAT is empty, inserted backwards, or PUK-locked; the example MUST still output the modem's hardware identity (Manufacturer, Model, IMEI) and clearly report the SIM condition without crashing.
- **No Cellular Antenna or Weak Signal**: The LTE main antenna is disconnected or the device is in a no-coverage area; the example MUST report the registration state as searching/unregistered along with unknown or weak signal indicators until timeout or user termination.
- **Graceful Termination Under Load**: The user presses `Ctrl+C` (`SIGINT`) or sends `SIGTERM` while an asynchronous modem workflow is in progress; the example MUST catch the signal, abort the loop cleanly, release GPIO and serial device handles, and exit with a clean status.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The project MUST provide a standalone runnable example application targeting the Raspberry Pi 5 paired with the Waveshare A7670E Cat-1/GNSS HAT.
- **FR-002**: The example MUST implement the platform-specific serial communication adapter, BCM GPIO 4 (`PWRKEY`) control sequence, and monotonic millisecond time source required to power on and drive the core modem library in a non-blocking superloop.
- **FR-003**: The example MUST allow users to configure the serial port device path (defaulting to the primary Raspberry Pi UART `/dev/ttyAMA0`), baud rate (defaulting to `115200`), optional SIM PIN code, diagnostic log verbosity, and single-shot execution mode (`--once`) via command-line arguments.
- **FR-004**: The example MUST execute and demonstrate the complete hardware power-on and core modem workflow sequence in order:
  1. Hardware power-on sequence by driving Raspberry Pi 5 BCM GPIO 4 (`PWRKEY`) and waiting for the Waveshare A7670E boot period.
  2. Modem initialization and communication handshake.
  3. Device hardware identity retrieval (manufacturer, model, firmware revision, IMEI).
  4. SIM card status verification, conditional single-shot PIN unlock (if locked and PIN supplied), and ICCID retrieval.
  5. Cellular network registration query, operator/access-technology query, and signal quality (RSSI/dBm, BER) inspection.
- **FR-005**: After completing the initial inspection sequence, the example MUST by default enter a continuous non-blocking monitoring loop that processes incoming unsolicited network notifications (URCs) and periodically refreshes signal/network status until terminated by the user (`Ctrl+C`), or exit immediately after the initial status report when `--once` is specified.
- **FR-006**: The example MUST handle operating system termination signals (`SIGINT`, `SIGTERM`) to release GPIO and serial port resources cleanly and exit without resource leaks.
- **FR-007**: The example MUST provide a help output (`--help` / `-h`) explaining all supported command-line options and usage examples.
- **FR-008**: The build system MUST provide an opt-in build option to compile the Raspberry Pi 5 example alongside the core library without modifying the core library headers/implementation or breaking existing host unit tests.
- **FR-009**: The project MUST include dedicated documentation for the Raspberry Pi 5 and Waveshare A7670E Cat-1/GNSS HAT covering hardware connections, jumper settings (`PWR` to `D4`), Raspberry Pi 5 UART and GPIO setup, permissions, build steps, and sample console output.

### Key Entities

- **Platform Hardware Adapter**: The host-side bridge that drives Raspberry Pi 5 BCM GPIO 4 (`PWRKEY`) to power on the HAT, opens the Raspberry Pi serial interface in non-blocking raw mode, feeds incoming modem bytes into the library's receive buffer, and transmits outgoing commands from the library to the serial port.
- **Example Runtime Configuration**: The set of user-supplied startup parameters including serial device path, baud rate, optional SIM PIN, logging verbosity level, and execution mode (continuous monitor vs. single-shot summary).
- **Modem Status Report**: The formatted console summary displaying the retrieved device identity (manufacturer, model, revision, IMEI), SIM card status (state, ICCID), and live cellular network telemetry (CS/PS/EPS registration states, operator name, radio access technology, and signal strength in dBm).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A developer with a Raspberry Pi 5 and Waveshare A7670E HAT can complete hardware setup, compile the example, and obtain a complete modem status report in under 15 minutes using only the included documentation.
- **SC-002**: When connected to a Waveshare A7670E HAT (with `PWR` jumper on `D4`), active SIM, and antenna, the example powers on the modem via GPIO 4, initializes it, and displays full device identity, SIM ICCID, and network status within 15 seconds of launch.
- **SC-003**: During continuous monitoring on a Raspberry Pi 5, the example maintains idle processor utilization below 5% of a single core while responding to unsolicited modem events within 100 milliseconds.
- **SC-004**: 100% of error conditions (invalid serial port or GPIO access, unresponsive modem, missing or locked SIM, and user interrupt signals) result in a clear diagnostic message and a clean process exit without hanging or crashing.
- **SC-005**: Adding and building the example project causes zero regressions in existing library functionality or verification suites.

## Assumptions

- The Raspberry Pi 5 runs a standard Linux-based operating system (such as Raspberry Pi OS).
- The Waveshare A7670E Cat-1/GNSS HAT is powered via the Raspberry Pi 5 40-pin GPIO header (with a sufficient 5V power supply) or USB Type-C port, and its onboard `PWR` jumper is connected to `D4` (routing `PWRKEY` to Raspberry Pi BCM GPIO 4).
- GNSS/GPS positioning features of the Waveshare A7670E HAT are out of scope for this example, which focuses on cellular modem initialization, SIM verification, device identity, and network registration supported by the core modem library.
