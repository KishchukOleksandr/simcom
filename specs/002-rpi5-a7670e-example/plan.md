# Implementation Plan: Raspberry Pi 5 & Waveshare A7670E HAT Example Project

**Branch**: `002-rpi5-a7670e-example` | **Date**: 2026-10-29 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/002-rpi5-a7670e-example/spec.md`

## Summary

Implement a complete, runnable reference example (`examples/rpi5_a7670e/`) and hardware onboarding guide (`docs/rpi5_a7670e.md`) demonstrating how to integrate `libsimcom` on a **Raspberry Pi 5** connected to a **Waveshare A7670E Cat-1/GNSS HAT**. The example is architected in two cleanly separated layers:
1. **Portable C11 Application Coordinator (`rpi5_app.c`)**: Zero-heap, non-blocking workflow coordinator state machine and CLI argument parser (`--port`, `--baud`, `--pin`, `--gpio-chip`, `--pwrkey-pin`, `--once`, `--verbose`, `--quiet`, `--help`) that sequences the mandatory startup BCM GPIO 4 (`PWRKEY`) power-on pulse, `Simcom_StartInit()`, `Simcom_RequestDeviceInfo()`, `Simcom_RequestSimStatus()`, conditional single-shot `Simcom_SubmitSimPin()`, `Simcom_RequestNetworkStatus()`, formatted console status reporting, and continuous URC/signal monitoring.
2. **Linux/Raspberry Pi 5 Platform Adapter (`rpi5_platform_linux.c` & `main.c`)**: Implements non-blocking raw 8N1 UART I/O (`<termios.h>`, `<poll.h>`), Linux Kernel GPIO Character Device v2 ioctl control for Raspberry Pi 5 RP1 BCM GPIO 4 (`/dev/gpiochip4` with fallback to `/dev/gpiochip0`), monotonic millisecond tick clock (`clock_gettime(CLOCK_MONOTONIC)`), and graceful `SIGINT`/`SIGTERM` resource cleanup.

All existing `libsimcom` core headers (`inc/`) and implementation files (`src/`) remain 100% unmodified (`FR-008`).

## Technical Context

**Language/Version**: ISO C11 (`-std=c11` for core and portable coordinator; `_POSIX_C_SOURCE=200809L` in the Linux platform adapter `rpi5_platform_linux.c` only).

**Primary Dependencies**: `libsimcom` static library; standard Linux/POSIX headers (`<termios.h>`, `<poll.h>`, `<unistd.h>`, `<fcntl.h>`, `<time.h>`, `<signal.h>`, `<sys/ioctl.h>`, `<linux/gpio.h>`) on Raspberry Pi 5. Zero external third-party library packages required.

**Storage**: N/A (in-memory static structs `rpi5_app_t` and `simcom_handle_t`).

**Testing**: CMake + CTest + Unity unit test suite (`tests/unit/test_rpi5_example.c`) testing the complete `rpi5_app` CLI parser, GPIO 4 `PWRKEY` timing sequence, full inspection workflow, PIN unlock, URC monitoring, and fault handling on any host OS (Windows/Linux).

**Target Platform**: Raspberry Pi 5 (Raspberry Pi OS Bookworm / Linux kernel 6.6+) with Waveshare A7670E Cat-1/GNSS HAT; portable coordinator verified on x86_64/ARM64 Windows and Linux hosts.

**Project Type**: Embedded C library reference application (`examples/rpi5_a7670e/`) + hardware setup documentation (`docs/rpi5_a7670e.md`).

**Performance Goals**:
- Full hardware power-on (`600 ms` GPIO 4 pulse + `3500 ms` boot settle) and modem inspection completes within `< 15 s` of launch (SC-002).
- Continuous monitoring loop maintains `< 5%` single-core CPU utilization on Raspberry Pi 5 while reacting to incoming URCs within `< 100 ms` via `5 ms` `poll()` pacing (SC-003).

**Constraints**:
- Core `libsimcom` headers (`inc/`) and source files (`src/`) MUST NOT be modified (FR-008, Clarification 3).
- Portable coordinator (`rpi5_app.c`) adheres to zero dynamic memory allocation, fixed-width types, and strict compiler warning flags (`-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef -Wcast-qual`).

**Scale/Scope**: 1 portable coordinator module (`rpi5_app.h` / `rpi5_app.c`), 1 Linux platform adapter (`rpi5_platform.h` / `rpi5_platform_linux.c` + `main.c`), 1 Unity test suite (`tests/unit/test_rpi5_example.c`), and 1 comprehensive hardware & software guide (`docs/rpi5_a7670e.md` linked into Doxygen and `README.md`) (~700–1,000 LOC total).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Gate / Principle | Pre-Research Status | Post-Design Status | Verification Notes |
| :--- | :--- | :--- | :--- |
| **Principle 1: Zero Dynamic Memory Allocation** | PASS | PASS | `rpi5_app_t` statically embeds `simcom_handle_t` and all state; zero `malloc`/`free` calls anywhere in the example or library. |
| **Principle 2: Non-Blocking Execution & FSM** | PASS | PASS | Both `PWRKEY` startup timing and all modem workflows are stepped non-blockingly via `Rpi5App_Poll()` and unsigned `(now - start) >= duration` tick math. |
| **Principle 3: Dual-Path AT Parser (Cmd vs. URC)** | PASS | PASS | Serial bytes read in the superloop are fed via `Rpi5App_FeedBytes()` → `Simcom_FeedBytes()`; `Simcom_Poll()` dispatches `+CREG`/`+CGREG`/`+CEREG` URCs during continuous monitoring without disrupting periodic `Simcom_RequestNetworkStatus()` refreshes. |
| **Principle 4: Hardware & MCU Agnosticism** | PASS | PASS | `libsimcom` remains 100% OS-agnostic; OS-specific POSIX/Linux calls (`termios`, `linux/gpio.h`) are confined strictly to `examples/rpi5_a7670e/rpi5_platform_linux.c` behind `rpi5_platform_if_t`. |
| **Principle 5: Defensive String Handling** | PASS | PASS | CLI argument parsing and console formatting in `rpi5_app.c` use explicit bounds-checked buffer helpers without unsafe `strcpy`/`sprintf`/`sscanf`/`atoi`. |
| **Principle 6: Multi-Stage Fault Escalation** | PASS | PASS | Mandatory startup GPIO 4 `PWRKEY` pulse wakes the HAT in the example platform layer (Clarification 3), while `libsimcom` executes Tiers 1 & 2 recovery and reports `SIMCOM_FAULT_MODEM_UNRESPONSIVE` on failure. |
| **Section 4: Verification & Testing Gates** | PASS | PASS | `tests/unit/test_rpi5_example.c` tests the full `Rpi5App` FSM, CLI parser, PIN unlock guard, and fault paths using Unity on the host PC. |

## Project Structure

### Documentation (this feature)

```text
specs/002-rpi5-a7670e-example/
├── plan.md                          # This file (/speckit-plan command output)
├── research.md                      # Phase 0 output (/speckit-plan command)
├── data-model.md                    # Phase 1 output (/speckit-plan command)
├── quickstart.md                    # Phase 1 output (/speckit-plan command)
├── contracts/                       # Phase 1 output (/speckit-plan command)
│   └── rpi5_example_contract.h      # Application coordinator, CLI, and platform adapter contract
└── tasks.md                         # Phase 2 output (/speckit-tasks command - NOT created by /speckit-plan)
```

### Source Code (repository root)

```text
examples/
└── rpi5_a7670e/
    ├── CMakeLists.txt               # Builds simcom_rpi5_app library (all hosts) and simcom_rpi5_a7670e executable (Linux)
    ├── rpi5_app.h                   # Portable coordinator & CLI interface (from rpi5_example_contract.h)
    ├── rpi5_app.c                   # Portable non-blocking workflow FSM, safe CLI parser, and status formatter
    ├── rpi5_platform.h              # Linux platform context & setup/teardown declarations
    ├── rpi5_platform_linux.c        # Raspberry Pi 5 termios UART, poll(), clock_gettime, and linux/gpio.h v2 ioctl
    └── main.c                       # Entry point, SIGINT/SIGTERM handling, and poll-paced superloop

docs/
├── index.md                         # Updated with link to Raspberry Pi 5 + Waveshare A7670E HAT guide
├── examples.md                      # Updated with cross-reference to the RPi 5 example project
└── rpi5_a7670e.md                   # Complete hardware jumper, RPi 5 UART/GPIO config, build, and run guide

tests/
├── CMakeLists.txt                   # Updated to register test_rpi5_example
└── unit/
    └── test_rpi5_example.c          # Host unit tests for CLI parsing, GPIO 4 PWRKEY FSM, PIN flow, and status report

CMakeLists.txt                       # Updated with SIMCOM_BUILD_EXAMPLES option and Doxyfile input update
Doxyfile                             # Updated to include docs/rpi5_a7670e.md and examples/rpi5_a7670e/rpi5_app.h
```

**Structure Decision**: Place the example under `examples/rpi5_a7670e/` and the hardware/onboarding guide under `docs/rpi5_a7670e.md`. Splitting `rpi5_app.c` (portable C11 coordinator) from `rpi5_platform_linux.c` (Linux kernel/POSIX syscalls) allows `test_rpi5_example` to compile and execute on all host platforms (Windows, macOS, Linux) while building the full `simcom_rpi5_a7670e` binary on Linux/Raspberry Pi 5 targets.

## Complexity Tracking

> **No Constitution violations.** All core library rules remain strictly enforced, and Linux POSIX/GPIO headers are isolated exclusively inside `examples/rpi5_a7670e/rpi5_platform_linux.c`.
