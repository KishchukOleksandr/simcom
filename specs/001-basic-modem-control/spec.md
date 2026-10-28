# Feature Specification: Basic Modem Initialization and Control

**Feature Branch**: `001-basic-modem-control`

**Created**: 2026-10-21

**Status**: Draft

**Input**: User description: "A library for working with SIMCom GSM modems. Features: Basic modem initialization and configuration, network registration status check, and retrieval of carrier and SIM card information. I/O Interface: The user implements an interface for read and write operations. Configuration: Configuration file for library setup. Logging: Interface for logging modem communication."

## Clarifications

### Session 2026-10-21

- Q: How should received bytes get from the UART into the library? → A: Push model — the application calls a library "feed bytes" function from its ISR/DMA callback; the library owns the RX ring buffer; the application implements only the transmit (write) hook.
- Q: Which SIMCom series must v1 support and test? → A: A76XX series only, behind a modem-profile abstraction so other series can be added later.
- Q: What fault-recovery scope does this feature include? → A: Tier 1 (soft re-sync) and Tier 2 (logical reset) only; GPIO/power tiers (3–4) are deferred to a later feature and an exhausted ladder reports a "modem unresponsive" fault.
- Q: How are network registration and signal changes tracked? → A: Both — registration URCs are enabled and dispatched to an application callback on change; signal quality and operator data are retrieved via on-demand query requests.
- Q: What happens when the SIM is PIN-locked? → A: Library detects and reports the locked state and offers an explicit, application-invoked "enter PIN" request; no PUK handling and no automatic PIN retries.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Modem Initialization and Operational Setup (Priority: P1)

An embedded developer needs to initialize an attached SIMCom cellular modem, configure essential communication parameters (such as command echo suppression and functional power state), and verify two-way communication through the developer's custom I/O transport without relying on hardcoded communication hardware.

**Why this priority**: Without reliable communication handshake, echo suppression, and functional readiness, no other cellular or network operations can take place. This represents the primary foundational capability of the library.

**Independent Test**: The system can be initialized with a user-provided transmit handler and fed received bytes by the application; it transmits initialization commands, validates acknowledgment responses, and confirms that the modem is operational and ready to accept subsequent commands.

**Acceptance Scenarios**:

1. **Given** an unresponsive or newly powered modem and an initialized user I/O interface, **When** the initialization sequence is initiated, **Then** the library verifies modem connectivity via command handshaking, disables command echo, confirms full functionality mode, and reports a ready status.
2. **Given** a modem that fails to respond within the configured response timeout, **When** the initialization command sequence is sent, **Then** the library retries up to the configured retry limit, escalates through soft re-synchronization (Tier 1) and logical modem reset (Tier 2), and, if still unresponsive, reports a "modem unresponsive" fault without blocking the host system.
3. **Given** an active modem session, **When** the operational configuration specifies custom timing and retry parameters from a configuration file, **Then** the library applies those settings to all ongoing command transactions.

---

### User Story 2 - SIM Card Validation and Hardware Identity Retrieval (Priority: P2)

An application developer needs to determine if a valid SIM card is inserted and ready, retrieve the SIM's unique identification number (ICCID), and query the modem's manufacturer, model name, firmware revision, and serial number (IMEI) to verify hardware authenticity and provision device records.

**Why this priority**: Cloud platforms, cellular carriers, and device management systems require unique device identifiers (IMEI) and subscriber card identifiers (ICCID) before authorizing network usage or device registration.

**Independent Test**: With the modem initialized, querying device and SIM metadata yields populated hardware identifiers and SIM readiness state, or provides clear notification if the SIM card is missing or locked.

**Acceptance Scenarios**:

1. **Given** a modem with an unlocked and inserted SIM card, **When** SIM card status and hardware identity are queried, **Then** the library returns the SIM readiness status (ready), the ICCID number, the modem model, firmware revision, and IMEI string.
2. **Given** a modem with no SIM card inserted, **When** the SIM verification request is performed, **Then** the library reports a distinct missing-SIM condition and leaves the modem ready for subsequent retry checks once a card is inserted.
3. **Given** a SIM card protected by a PIN or PUK password, **When** SIM status is queried, **Then** the library identifies the locked status (e.g., waiting for PIN) and prevents invalid network attachment attempts.
4. **Given** a PIN-locked SIM, **When** the application explicitly submits a PIN, **Then** the library sends it exactly once and reports success (SIM ready) or failure (incorrect PIN / now PUK-locked) without any automatic retry.

---

### User Story 3 - Network Registration and Carrier Status Monitoring (Priority: P3)

An IoT telemetry system needs to verify that the modem is successfully registered with a cellular carrier, determine whether it is on its home network or roaming, monitor radio signal strength, and identify the network operator name and access technology (e.g., GSM, LTE) before initiating data transfers.

**Why this priority**: Cellular transmission attempts will fail if the modem is not attached to the carrier's network or if radio signal is insufficient. Periodic status inspection ensures that the application only attempts network tasks when connectivity is confirmed.

**Independent Test**: The application can trigger a network status check; the library queries registration states (circuit-switched and packet-switched), evaluates signal reception quality, and returns the operator identity and connection status.

**Acceptance Scenarios**:

1. **Given** a modem registered to an active cellular tower, **When** the network registration check is executed, **Then** the library reports successful network registration, identifies whether the connection is home or roaming, and provides the current operator name and access technology.
2. **Given** a modem currently searching for a carrier or experiencing registration denial, **When** registration status is queried, **Then** the library returns the searching or denied state alongside the current signal quality indicators.
3. **Given** fluctuating radio conditions, **When** signal quality is queried, **Then** the library returns the signal strength level and error rate metric, translating raw modem indicators into standardized signal ratings.
4. **Given** registration change notifications are enabled during initialization, **When** the modem's registration state changes (e.g., searching → registered roaming), **Then** the library updates its cached registration state and invokes the application's registration-change callback without requiring an explicit query.

---

### User Story 4 - Diagnostic Logging and Communication Auditing (Priority: P4)

A systems engineer needs to capture and inspect raw byte streams and formatted diagnostic events exchanged between the library and the cellular modem through a user-defined logging callback, enabling troubleshooting of field anomalies, timing issues, and protocol deviations.

**Why this priority**: Remote modems and cellular RF environments are prone to unexpected carrier drops, noise, and protocol quirks. A dedicated logging interface allows debugging without modifying the core library code.

**Independent Test**: When a logging callback is registered, every transmitted command, received response, unsolicited notification, and internal state transition is dispatched to the callback with timestamp and severity level.

**Acceptance Scenarios**:

1. **Given** a user-registered logging interface, **When** the library transmits a command or receives response data, **Then** the library forwards the log entry with an appropriate severity level (e.g., DEBUG, INFO, WARNING, ERROR) and raw payload details to the user logging handler.
2. **Given** a configuration specifying a minimum log severity threshold, **When** diagnostic events below the configured threshold occur, **Then** the library suppresses those events, preventing unnecessary processing or logging bandwidth consumption.
3. **Given** no logging callback is provided by the application, **When** library operations execute, **Then** the library performs all modem interactions normally without overhead or runtime errors.

---

### Edge Cases

- **Unresponsive Modem / Power Failure**: The modem does not respond to initial handshake commands because it is powered off, held in reset, or experiencing a power brownout.
- **Malformed or Incomplete Responses**: The modem emits truncated lines, garbled bytes, or unexpected control characters due to serial baud mismatches or noise.
- **Unsolicited Result Codes (URCs) Mid-Transaction**: The modem receives an incoming network notification (such as an incoming call alert, SMS indicator, or network registration update) while the library is awaiting a response to an active command.
- **SIM Card Removal or Failure**: The SIM card is physically removed, becomes unreadable, or encounters an internal crash during runtime operation.
- **Registration Rejection**: The cellular network actively denies registration due to invalid APN, expired subscription, or blacklisted IMEI.
- **Partial Writes / Fragmented Reception**: The user-supplied transmit hook accepts fewer bytes than requested, or received bytes arrive one at a time across many feed calls, requiring the library to complete transmission and reassemble responses over multiple poll cycles without losing data.
- **RX Buffer Overflow**: The application feeds bytes faster than poll cycles consume them; the library MUST discard overflow bytes safely, flag the overflow, and resynchronize on the next line boundary.
- **PIN Lockout Risk**: An incorrect application-submitted PIN reduces remaining attempts; the library MUST never resubmit a PIN on its own.
- **Slow Carrier Response Times**: Registration queries during network congestion take significantly longer than standard command timeouts, requiring configurable threshold boundaries.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST support initialization through a user-implemented, non-blocking transmit hook and an ISR-safe "feed received bytes" entry point (push model) into a library-owned RX ring buffer, without direct dependence on specific hardware or operating systems.
- **FR-002**: System MUST execute a deterministic handshake sequence to confirm modem responsiveness, disable command echo, and set standard operational power functionality.
- **FR-003**: System MUST provide an external configuration file or mechanism to adjust operational parameters including command response timeouts, maximum retry counts, and buffer capacity limits.
- **FR-004**: System MUST check SIM card status and categorize its state into ready, PIN required, PUK required, or absent/faulty, and MUST provide an application-invoked PIN submission request that is never retried automatically. PUK entry is out of scope.
- **FR-005**: System MUST retrieve and expose the Integrated Circuit Card Identifier (ICCID) from an inserted SIM card.
- **FR-006**: System MUST query and expose core device identity properties including manufacturer name, modem model identification, firmware revision, and serial number (IMEI).
- **FR-007**: System MUST query and report radio signal quality, including received signal strength indication (RSSI) and channel bit error rate (BER).
- **FR-008**: System MUST query and report network registration status across both circuit-switched (GSM) and packet-switched / EPS (GPRS/LTE) domains, distinguishing between home network, roaming, searching, and denied states. During initialization the library MUST enable registration change notifications and invoke an application callback whenever registration state changes.
- **FR-009**: System MUST retrieve carrier and operator information, including the active network operator name and access technology indicator.
- **FR-010**: System MUST decouple unsolicited result codes (asynchronous network notifications) from synchronous command-response processing so that unexpected events do not corrupt command transactions.
- **FR-011**: System MUST provide a dedicated logging interface allowing users to hook into diagnostic and communication events with configurable severity filtering (DEBUG, INFO, WARNING, ERROR).
- **FR-012**: System MUST operate without dynamic memory allocation, relying exclusively on static memory provided by the caller or configured at build time.
- **FR-013**: System MUST provide non-blocking execution semantics, enabling incremental processing of incoming and outgoing data through a periodic poll routine.
- **FR-014**: System MUST support the SIMCom A76XX series in this release, with all series-specific command details isolated in a modem-profile abstraction so additional series can be added without changing core logic.
- **FR-015**: System MUST implement a two-tier fault recovery ladder: Tier 1 (soft re-synchronization via repeated handshake attempts) and Tier 2 (logical modem reset with wait for the modem-ready indication). When both tiers are exhausted, the library MUST report a "modem unresponsive" fault to the application. Hardware-pin (Tier 3) and power-cut (Tier 4) recovery are out of scope for this feature.

### Key Entities

- **Modem Instance**: The persistent state container representing an attached SIMCom modem, maintaining its current lifecycle state, active command transaction, buffer offsets, and configuration references.
- **I/O Interface**: The abstraction combining a user-supplied non-blocking transmit callback with a library-provided, ISR-safe entry point through which the application pushes received bytes into the library-owned RX ring buffer.
- **Device Information**: The structured entity holding static metadata about the physical modem, including manufacturer name, model designation, firmware revision string, and unique IMEI identifier.
- **SIM Card Profile**: The entity representing the subscriber identity module, including operational status (ready, locked, missing), lock type, and unique ICCID identifier.
- **Network Registration State**: The entity capturing current cellular connection status, including registration state (home, roaming, searching, denied), radio signal strength metrics, operator name, and active radio access technology.
- **Logging Interface**: The abstraction structure through which the library dispatches formatted log messages, severity levels, and raw telemetry bytes to the host application's logging infrastructure.
- **Library Configuration**: The set of pre-defined operational boundaries, including buffer sizes, command timeouts, retry thresholds, and diagnostic verbosity levels.
- **Modem Profile**: The series-specific descriptor (A76XX in this release) defining command variants, per-command timeouts, and modem-ready indications used by the core engine.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Basic modem handshake and initialization completes within 3 seconds when the modem is powered and responding normally.
- **SC-002**: 100% of SIM card states (ready, PIN locked, missing, or damaged) are accurately identified and reported without hanging or freezing the host system.
- **SC-003**: Device identity attributes (IMEI, model, firmware revision) and SIM ICCID are extracted with 100% fidelity compared against direct hardware inspection.
- **SC-004**: Network registration status changes (e.g., transition from searching to registered home or roaming) are detected and updated within 5 seconds of the network event.
- **SC-005**: 100% of library execution is non-blocking, ensuring individual operational polling steps return control to the host application within 5 milliseconds.
- **SC-006**: Diagnostic logging captures all transmitted commands and received responses with zero data loss when the user's logging callback is active.
- **SC-007**: Zero dynamic memory allocations occur during the entire library lifecycle, guaranteeing deterministic memory footprints in embedded environments.

## Assumptions

- The target cellular modem is a SIMCom A76XX series module adhering to the A76XX AT Command Manual (V2.04) for basic control, SIM interrogation, and network registration. Other series (SIM800, SIM7000, SIM7070/7080, SIM7600) are out of scope for this release.
- Physical modem power sequencing (power rails, PWRKEY pulse timing, hardware reset pins) is the host application's responsibility in this release. This intentionally defers constitution Principle 6 Tiers 3–4 and the GPIO hooks of Principle 4 to a later feature; the deviation must be recorded in the plan's Complexity Tracking.
- The user application regularly executes the library's polling cycle to allow state machine advancement, timer tracking, and serial buffer processing.
- The serial communication link between the host microcontroller and the modem operates with hardware flow control or at a baud rate that prevents physical receiver overruns.
- The SIM card does not require over-the-air activation. PIN unlocking happens only when the application explicitly submits a PIN; PUK unlocking is out of scope.
