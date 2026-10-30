# Tasks: Raspberry Pi 5 & Waveshare A7670E HAT Example Project

**Input**: Design documents from `/specs/002-rpi5-a7670e-example/`

**Prerequisites**: [plan.md](plan.md) (required), [spec.md](spec.md) (required for user stories), [research.md](research.md), [data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Included per Project Constitution Section 4 and `plan.md` (100% off-target host verification of the portable `rpi5_app` coordinator FSM, CLI parser, GPIO 4 `PWRKEY` timing sequence, and SIM PIN workflow via Unity in `tests/unit/test_rpi5_example.c`).

**Organization**: Tasks are grouped by user story (`US1`–`US3`) to enable independent implementation, verification, and incremental delivery.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (`[US1]`, `[US2]`, `[US3]`)
- Exact repository-relative file paths are included in every task description

---

## Phase 1: Setup (Shared Infrastructure & Build Integration)

**Purpose**: Initialize example directory structure, CMake build options, and header contracts for the portable application coordinator and Linux platform adapter.

- [X] T001 Create example directory `examples/rpi5_a7670e/` per `specs/002-rpi5-a7670e-example/plan.md`
- [X] T002 Add `SIMCOM_BUILD_EXAMPLES` option (default `ON`) and conditional `add_subdirectory(examples/rpi5_a7670e)` in `CMakeLists.txt` without modifying core `libsimcom` targets (`FR-008`)
- [X] T003 [P] Create `examples/rpi5_a7670e/CMakeLists.txt` defining the portable `simcom_rpi5_app` static library target (compiled with `SIMCOM_WARNING_FLAGS` on all hosts) and the `simcom_rpi5_a7670e` executable target (compiled on `CMAKE_SYSTEM_NAME STREQUAL "Linux"` with `_POSIX_C_SOURCE=200809L`)
- [X] T004 [P] Create portable application coordinator header `examples/rpi5_a7670e/rpi5_app.h` (from `specs/002-rpi5-a7670e-example/contracts/rpi5_example_contract.h`) defining default constants, `rpi5_example_cfg_t`, `rpi5_platform_if_t`, `rpi5_app_state_t`, `rpi5_app_t`, and `Rpi5App_*` API declarations
- [X] T005 [P] Create Linux platform adapter header `examples/rpi5_a7670e/rpi5_platform.h` declaring `rpi5_linux_ctx_t` (`uart_fd`, `gpio_chip_fd`, `gpio_line_fd`, `boot_epoch_ms`), `Rpi5Platform_Open()`, `Rpi5Platform_PollRx()`, `Rpi5Platform_Close()`, and `Rpi5Platform_BindCallbacks()`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Safe bounds-checked string/formatting primitives, Linux UART/GPIO/clock platform driver, and Unity test harness registration required before user story implementation.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [X] T006 Implement `Rpi5App_DefaultConfig()` and defensive zero-heap string/formatting helpers (bounded string append, unsigned/signed integer-to-ASCII conversion, and enum-to-string mappers for `simcom_sim_state_t`, `simcom_reg_state_t`, `simcom_act_t`, and `simcom_fault_t`) in `examples/rpi5_a7670e/rpi5_app.c`
- [X] T007 [P] Implement Linux platform hardware adapter (`Rpi5Platform_Open` with non-blocking 8N1 `termios` UART configuration and `<linux/gpio.h>` v2 character device `GPIO_V2_GET_LINE_IOCTL` / `GPIO_V2_LINE_SET_VALUES_IOCTL` on `/dev/gpiochip4` with `/dev/gpiochip0` fallback for BCM GPIO 4, `clock_gettime(CLOCK_MONOTONIC)` tick callback, `poll()`-paced `Rpi5Platform_PollRx`, and `Rpi5Platform_Close` cleanup) in `examples/rpi5_a7670e/rpi5_platform_linux.c`
- [X] T008 [P] Register `test_rpi5_example` test executable in `tests/CMakeLists.txt` (linking `simcom_rpi5_app`, `simcom`, and `unity`) and create mock platform fixture (`mock_rpi5_platform_t` capturing `uart_tx`, `gpio_set_pwrkey` transitions, `console_print` log buffer, and simulated `tick_ms`) in `tests/unit/test_rpi5_example.c`

**Checkpoint**: Foundation ready — `simcom_rpi5_app` target, Linux platform driver, and `test_rpi5_example` harness compile cleanly.

---

## Phase 3: User Story 1 - End-to-End Modem Bring-Up and Status Inspection on Raspberry Pi 5 (Priority: P1) 🎯 MVP

**Goal**: Drive Raspberry Pi 5 BCM GPIO 4 (`PWRKEY`) to power on the Waveshare A7670E HAT, initialize the modem over UART, retrieve and print device identity, SIM status/ICCID, and cellular network/signal metrics, and run a continuous non-blocking monitoring loop with graceful `SIGINT`/`SIGTERM` shutdown.

**Independent Test**: Run `test_rpi5_example` on host (or `./build/examples/rpi5_a7670e/simcom_rpi5_a7670e` on Raspberry Pi 5) and verify the `600 ms` GPIO 4 HIGH pulse + `3500 ms` boot settle wait, `Simcom_StartInit()` handshake, sequential `DeviceInfo` → `SimStatus` → `NetworkStatus` execution, formatted console status report, continuous URC/signal monitoring, and clean shutdown on `Rpi5App_RequestStop()`.

### Tests for User Story 1 ⚠️

> **NOTE: Write these tests FIRST, ensure they FAIL before completing User Story 1 implementation**

- [X] T009 [P] [US1] Write unit tests in `tests/unit/test_rpi5_example.c` verifying: (1) mandatory GPIO 4 `PWRKEY` `600 ms` HIGH pulse and `3500 ms` LOW boot settle wait before `Simcom_StartInit()`, (2) end-to-end happy-path progression through `MODEM_INIT` → `QUERY_DEV_INFO` → `QUERY_SIM` → `QUERY_NET` → `MONITOR`, (3) formatted Modem Status Report output containing `A7670E` identity, ICCID, and network/signal fields, (4) live `+CEREG:` URC logging and periodic refresh in `RPI5_APP_STATE_MONITOR`, (5) `Rpi5App_RequestStop()` graceful termination, and (6) `SIMCOM_EVENT_FAULT` transition to `RPI5_APP_STATE_ERROR` with non-zero `exit_code`

### Implementation for User Story 1

- [X] T010 [US1] Implement `Rpi5App_Init()`, `Rpi5App_Start()`, `Rpi5App_FeedBytes()`, `Rpi5App_Poll()`, `Rpi5App_RequestStop()`, and `Rpi5App_IsFinished()` driving the non-blocking coordinator FSM (`RPI5_APP_STATE_PWRKEY_ASSERT` → `RPI5_APP_STATE_PWRKEY_WAIT_BOOT` → `RPI5_APP_STATE_MODEM_INIT` → `RPI5_APP_STATE_QUERY_DEV_INFO` → `RPI5_APP_STATE_QUERY_SIM` → `RPI5_APP_STATE_QUERY_NET` → `RPI5_APP_STATE_MONITOR` / `RPI5_APP_STATE_DONE` / `RPI5_APP_STATE_ERROR`) in `examples/rpi5_a7670e/rpi5_app.c`
- [X] T011 [US1] Implement formatted console output for the Modem Status Report (Manufacturer, Model, Revision, IMEI, SIM state, ICCID, CS/PS/EPS registration, RSSI/dBm, BER, Operator Name, RAT, Serving Cell Mode), asynchronous `SIMCOM_EVENT_NET_REG_URC_CHANGED` notifications, and actionable fault diagnostics (hinting at `PWR` to `D4` and `TXD`/`RXD` jumpers on `SIMCOM_FAULT_MODEM_UNRESPONSIVE`) in `examples/rpi5_a7670e/rpi5_app.c`
- [X] T012 [US1] Implement the Raspberry Pi 5 executable entry point (`main()`, `SIGINT`/`SIGTERM` handlers invoking `Rpi5App_RequestStop()`, `Rpi5Platform_Open()`, `5 ms` `poll()`-paced superloop stepping `Rpi5Platform_PollRx()` and `Rpi5App_Poll()`, and `Rpi5Platform_Close()`) in `examples/rpi5_a7670e/main.c`

**Checkpoint**: User Story 1 (MVP) is complete and independently verifiable via `ctest -R test_rpi5_example`.

---

## Phase 4: User Story 2 - Configurable Connection Parameters and SIM PIN Unlocking (Priority: P2)

**Goal**: Allow runtime configuration of serial port path, baud rate, GPIO chip/pin, SIM PIN code, single-shot `--once` mode, and diagnostic log verbosity (`--verbose` / `--quiet` / `--help`) via CLI flags, and perform single-shot SIM PIN unlocking when a locked SIM is detected and `--pin` is provided.

**Independent Test**: Pass custom CLI flags (`--port`, `--baud`, `--pin`, `--once`, `--verbose`, `--help`) to `Rpi5App_ParseArgs()` and run the coordinator against a `SIMCOM_SIM_STATE_PIN_REQUIRED` modem response both with and without `--pin`, verifying single-shot `AT+CPIN="<pin>"` unlock + ICCID refresh when `--pin` is supplied, and safe warning without lockout when `--pin` is omitted.

### Tests for User Story 2 ⚠️

- [X] T013 [P] [US2] Write unit tests in `tests/unit/test_rpi5_example.c` verifying: (1) `Rpi5App_ParseArgs()` for `-p/--port`, `-b/--baud`, `-g/--gpio-chip`, `-k/--pwrkey-pin`, `-P/--pin`, `-1/--once`, `-v/--verbose`, `-q/--quiet`, `-h/--help`, and rejection of unknown flags, missing option values, invalid baud rates, and malformed PIN strings, (2) single-shot `RPI5_APP_STATE_SUBMIT_PIN` transition when SIM is `PIN_REQUIRED` and `--pin` is set, followed by ICCID re-query, (3) skipping PIN submission with warning when SIM is `PIN_REQUIRED` and `--pin` is `NULL`, and (4) `--once` immediate transition to `RPI5_APP_STATE_DONE` after printing the initial status report

### Implementation for User Story 2

- [X] T014 [US2] Implement bounds-checked CLI argument parser `Rpi5App_ParseArgs()` (validating supported baud rates and 4–8 digit numeric `--pin` strings) and usage help printer `Rpi5App_PrintHelp()` in `examples/rpi5_a7670e/rpi5_app.c`
- [X] T015 [US2] Implement conditional single-shot SIM PIN unlock state `RPI5_APP_STATE_SUBMIT_PIN` (enforcing `pin_attempted` single-shot guard, calling `Simcom_SubmitSimPin()`, and re-running `Simcom_RequestSimStatus()` on `SIMCOM_EVENT_SIM_PIN_RESULT` success to retrieve ICCID) and severity-filtered `simcom_log_fn_t` diagnostic console formatter (`[TX]`, `[RX]`, `[URC]`, `[FSM]`) in `examples/rpi5_a7670e/rpi5_app.c`

**Checkpoint**: User Stories 1 AND 2 are both complete and verified via `ctest -R test_rpi5_example`.

---

## Phase 5: User Story 3 - Hardware Setup and Onboarding Guide for Raspberry Pi 5 & Waveshare A7670E HAT (Priority: P3)

**Goal**: Provide step-by-step documentation for connecting and configuring the Waveshare A7670E Cat-1/GNSS HAT on a Raspberry Pi 5, building the example, and interpreting the output.

**Independent Test**: Follow `docs/rpi5_a7670e.md` to verify all jumper settings (`PWR` to `D4`, `TXD`/`RXD`), Raspberry Pi 5 `/boot/firmware/config.txt` settings, build commands, CLI options, and Doxygen HTML generation.

### Implementation for User Story 3

- [X] T016 [P] [US3] Create comprehensive hardware and software onboarding guide in `docs/rpi5_a7670e.md` covering Waveshare A7670E Cat-1/GNSS HAT features, yellow jumper placement (`PWR` → `D4` for BCM GPIO 4, `TXD`/`RXD` cross-connection), LTE antenna and nano-SIM installation, Raspberry Pi 5 `/boot/firmware/config.txt` (`enable_uart=1`, `dtparam=uart0=on`), `dialout`/`gpio` group permissions, build and run commands, CLI flag reference table, sample console output, and troubleshooting
- [X] T017 [P] [US3] Update `docs/index.md`, `docs/examples.md`, `README.md`, and `Doxyfile` to link and include `docs/rpi5_a7670e.md` and `examples/rpi5_a7670e/rpi5_app.h` in the generated documentation

**Checkpoint**: All user stories (`US1`–`US3`) are complete and documented.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: End-to-end build, test, Doxygen generation, and knowledge graph verification.

- [X] T018 Account for Microsoft x64 ABI 32-byte shadow space on Windows hosts in `cmake/check_stack_usage.cmake`, build all targets, and execute the full CTest suite (`ctest --test-dir build --output-on-failure`) ensuring `test_rpi5_example` and all existing unit tests pass 100% with zero compiler warnings
- [X] T019 [P] Generate Doxygen HTML documentation (`doxygen Doxyfile`) and verify zero warnings across all headers and markdown pages including `docs/rpi5_a7670e.md` and `examples/rpi5_a7670e/rpi5_app.h`
- [X] T020 Validate all scenarios in `specs/002-rpi5-a7670e-example/quickstart.md` and run `graphify update .` to refresh the repository knowledge graph

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — can start immediately (`T001` → `T002` → `T003`, `T004`, `T005` in parallel).
- **Phase 2 (Foundational)**: Depends on Phase 1 (`T001`–`T005`) — BLOCKS all user stories (`T006`, `T007`, `T008`).
- **Phase 3 (User Story 1 - P1 MVP)**: Depends on Phase 2 completion (`T009` → `T010` → `T011` → `T012`).
- **Phase 4 (User Story 2 - P2)**: Depends on Phase 2 completion and integrates into `examples/rpi5_a7670e/rpi5_app.c` after `US1` (`T013` → `T014` → `T015`).
- **Phase 5 (User Story 3 - P3)**: Depends on Phase 1 contract definitions; can run in parallel with `US1`/`US2` (`T016`, `T017`).
- **Phase 6 (Polish)**: Depends on Phases 3–5 completion (`T018`–`T020`).

### User Story Dependencies

- **User Story 1 (P1 - MVP)**: Starts immediately after Phase 2 Foundational. Delivers full GPIO 4 `PWRKEY` startup, modem init, identity/SIM/network inspection, continuous monitoring, and Raspberry Pi 5 executable.
- **User Story 2 (P2)**: Builds on the coordinator in `examples/rpi5_a7670e/rpi5_app.c` to add CLI argument overrides (`--port`, `--baud`, `--pin`, `--once`, `--verbose`) and conditional single-shot SIM PIN unlock.
- **User Story 3 (P3)**: Documentation-only (`docs/rpi5_a7670e.md`, `docs/index.md`, `docs/examples.md`, `README.md`, `Doxyfile`) — completely independent of C implementation tasks once CLI flags are defined in `T004`.

### Parallel Opportunities

- **Phase 1**: `T003` (`examples/rpi5_a7670e/CMakeLists.txt`), `T004` (`examples/rpi5_a7670e/rpi5_app.h`), and `T005` (`examples/rpi5_a7670e/rpi5_platform.h`) touch separate files and can be executed in parallel.
- **Phase 2**: `T006` (`examples/rpi5_a7670e/rpi5_app.c`), `T007` (`examples/rpi5_a7670e/rpi5_platform_linux.c`), and `T008` (`tests/unit/test_rpi5_example.c`) touch separate files and can be executed in parallel.
- **Phase 5**: `T016` (`docs/rpi5_a7670e.md`) and `T017` (`docs/index.md`, `docs/examples.md`, `README.md`, `Doxyfile`) can run in parallel with Phase 3/4 code implementation.

---

## Parallel Example: Phase 1 & Phase 2

```bash
# Parallel Header & Build Definitions (Phase 1):
Task T003: "Create examples/rpi5_a7670e/CMakeLists.txt"
Task T004: "Create portable application coordinator header examples/rpi5_a7670e/rpi5_app.h"
Task T005: "Create Linux platform adapter header examples/rpi5_a7670e/rpi5_platform.h"

# Parallel Foundational Implementation (Phase 2):
Task T006: "Implement Rpi5App_DefaultConfig() and safe string helpers in examples/rpi5_a7670e/rpi5_app.c"
Task T007: "Implement Linux platform hardware adapter in examples/rpi5_a7670e/rpi5_platform_linux.c"
Task T008: "Register test_rpi5_example in tests/CMakeLists.txt and create mock fixture in tests/unit/test_rpi5_example.c"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (`T001`–`T005`).
2. Complete Phase 2: Foundational (`T006`–`T008`).
3. Complete Phase 3: User Story 1 (`T009`–`T012`).
4. **STOP and VALIDATE**: Run `ctest -R test_rpi5_example --output-on-failure` to verify the complete GPIO 4 `PWRKEY` power-on, initialization, device/SIM/network inspection, and continuous monitoring workflow.

### Incremental Delivery

1. **Foundation (`T001`–`T008`)**: Build system, contracts, Linux UART/GPIO driver, and test harness ready.
2. **MVP (`T009`–`T012` - US1)**: End-to-end Raspberry Pi 5 + Waveshare A7670E bring-up and status inspection working with defaults.
3. **Configurability & PIN Unlock (`T013`–`T015` - US2)**: Runtime CLI flags, single-shot `--once` mode, verbose AT logging, and SIM PIN unlocking added.
4. **Onboarding Documentation & Polish (`T016`–`T020` - US3 & Polish)**: Hardware jumper & Raspberry Pi 5 setup guide integrated into Doxygen and verified.
