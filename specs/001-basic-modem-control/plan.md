# Implementation Plan: Basic Modem Initialization and Control

**Branch**: `001-basic-modem-control` | **Date**: 2026-10-21 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-basic-modem-control/spec.md`

## Summary

Implement the foundational bare-metal C11 library (`libsimcom`) for initializing and controlling SIMCom A76XX series cellular modems. The library provides a strictly non-blocking, zero-heap-allocation finite state machine (FSM) stepped via `Simcom_Poll()`, an ISR-safe lock-free SPSC ring buffer RX path (`Simcom_FeedBytes()`) paired with an asynchronous UART TX hook, a dual-path AT lexer that separates asynchronous URCs from active command transactions, a two-tier fault recovery ladder (Tier 1 soft re-sync and Tier 2 logical reset), SIM card readiness/ICCID/single-shot PIN handling, hardware identity retrieval, network registration/signal/carrier monitoring, and a severity-filtered diagnostic logging callback. All A76XX-specific command strings and timeouts are isolated behind a `.rodata` modem profile descriptor (`simcom_modem_profile_t`).

## Technical Context

**Language/Version**: ISO C11 (`-std=c11`, GNU extensions forbidden per Constitution Section 3)

**Primary Dependencies**: None at runtime — permitted standard headers only: `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<limits.h>`, and the safe memory subset of `<string.h>` (`memcpy`, `memmove`, `memset`, `memcmp`).

**Storage**: N/A (in-memory static structs allocated by caller; compile-time bounds in `inc/simcom_cfg.h`).

**Testing**: CMake + CTest + Unity unit/mock test harness on x86/ARM64 host, plus `cppcheck` and `clang-tidy` static analysis gates.

**Target Platform**: Bare-metal embedded MCUs (No-OS superloop + ISR/DMA UART drivers) and host x86/ARM64 verification targets. Target modem hardware: SIMCom A76XX series (AT Command Manual V2.04).

**Project Type**: Portable bare-metal embedded C static library (`libsimcom`).

**Performance Goals**:
- Individual `Simcom_Poll()` and public API calls complete in `< 5 ms` (typically `< 50 µs` on Cortex-M) with $O(1)$ or bounded $O(N)$ complexity (SC-005).
- Basic modem handshake and initialization completes within `< 3 s` when powered and responding (SC-001).
- Network registration URC updates dispatched within `< 5 s` of network state change (SC-004).

**Constraints**:
- Zero dynamic memory allocation (`malloc`/`calloc`/`realloc`/`free` and VLAs forbidden; SC-007).
- Worst-Case Stack Usage (WCSU): `<= 128 Bytes` per public API call, `<= 32 Bytes` for ISR helper `Simcom_FeedBytes()` (`-fstack-usage`).
- Zero compiler warnings under `-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef -Wcast-qual -fno-common -fstack-usage`.
- MISRA C:2012 & BARR-C:2018 compliant fixed-width types (`uint8_t`, `uint16_t`, `uint32_t`, `int16_t`, `int32_t`, `size_t`, `bool`).

**Scale/Scope**: Core AT engine, SPSC ring buffer, dual-path lexer, URC dispatcher, A76XX profile, 5 asynchronous workflows (Init, Device Info, SIM Status, PIN Submit, Network Status), 2-tier recovery FSM, and comprehensive Unity host test suite (~1,500–2,500 LOC implementation + tests).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Gate / Principle | Pre-Research Status | Post-Design Status | Verification Notes |
| :--- | :--- | :--- | :--- |
| **Principle 1: Zero Dynamic Memory Allocation** | PASS | PASS | `simcom_handle_t` statically owns all buffers; bounds checked via `_Static_assert` in `inc/simcom_cfg.h`; profiles/URC tables in `.rodata`. |
| **Principle 2: Non-Blocking Execution & FSM** | PASS | PASS | All workflows stepped via `Simcom_Poll()`; timeouts tracked via unsigned `(now - start) >= timeout_ms` subtraction. |
| **Principle 3: Dual-Path AT Parser (Cmd vs. URC)** | PASS | PASS | Lock-free SPSC ring buffer fed by `Simcom_FeedBytes()`; two-mode lexer (`LINE` / `RAW_BYTES`) and URC dispatch table run strictly inside `Simcom_Poll()`. |
| **Principle 4: Hardware & MCU Agnosticism** | PASS (with scoped deferral) | PASS (with scoped deferral) | Zero vendor headers; all I/O and ticks routed through `simcom_hal_if_t`. GPIO power/reset hooks (`pwrkey`, `reset`, `status`) deferred with Tiers 3–4 per Spec Assumptions (see Complexity Tracking). |
| **Principle 5: Defensive String Handling** | PASS | PASS | Unsafe `<string.h>`/`<stdio.h>` functions banned; custom bounds-checked span tokenizer (`simcom_parser.c`) operates on `(const uint8_t *buf, size_t len)`. |
| **Principle 6: Multi-Stage Fault Escalation** | PASS (with scoped deferral) | PASS (with scoped deferral) | Per-command timeouts & retries + Tier 1 (`AT` re-sync) + Tier 2 (`AT+CRESET` + `RDY` wait) implemented; Tiers 3–4 hardware/power recovery deferred per FR-015 (see Complexity Tracking). |
| **Section 3: Toolchain & Stack Budget** | PASS | PASS | C11, fixed-width types, strict warning flags, and WCSU `<= 128B` / `<= 32B` enforced in contracts and CMake build. |
| **Section 4: Verification & Chaos Testing** | PASS | PASS | Unity host test suite covers 1-byte fragmented RX, mid-command URCs, RX buffer overflow resync, CME errors, single-shot PIN, and Tier 1–2 recovery. |

## Project Structure

### Documentation (this feature)

```text
specs/001-basic-modem-control/
├── plan.md                          # This file (/speckit-plan command output)
├── research.md                      # Phase 0 output (/speckit-plan command)
├── data-model.md                    # Phase 1 output (/speckit-plan command)
├── quickstart.md                    # Phase 1 output (/speckit-plan command)
├── contracts/                       # Phase 1 output (/speckit-plan command)
│   ├── simcom_cfg_contract.h        # Compile-time configuration & _Static_assert contract
│   ├── simcom_types_contract.h      # Public types, HAL, logger, profile, and handle contract
│   └── simcom_api_contract.h        # Public non-blocking C API contract
└── tasks.md                         # Phase 2 output (/speckit-tasks command - NOT created by /speckit-plan)
```

### Source Code (repository root)

```text
inc/
├── simcom.h                         # Public API entry points (from simcom_api_contract.h)
├── simcom_cfg.h                     # Compile-time capacities, defaults, and _Static_asserts
├── simcom_types.h                   # Public types, enums, HAL/log interfaces, simcom_handle_t
├── simcom_profile.h                 # Modem profile descriptor interface and A76XX declaration
└── internal/
    ├── simcom_ringbuf.h             # Lock-free SPSC byte ring buffer helpers
    ├── simcom_lexer.h               # Dual-path AT line/raw-byte tokenizer & URC classifier
    ├── simcom_parser.h              # Bounds-checked span tokenizers & safe integer parsers
    ├── simcom_fsm.h                 # Internal AT transaction & workflow/recovery FSM steppers
    └── simcom_log.h                 # Internal diagnostic logging helper prototype

src/
├── simcom.c                         # Public API implementation & handle validation
├── simcom_ringbuf.c                 # ISR-safe SPSC ring buffer & overflow tracking
├── simcom_lexer.c                   # Line assembly, overflow resync, and URC dispatch table
├── simcom_parser.c                  # Defensive span-based AT response parsers (+CPIN, +CSQ, etc.)
├── simcom_fsm.c                     # Non-blocking workflow engine & Tier 1 / Tier 2 recovery
├── simcom_log.c                     # Severity-filtered diagnostic logging helper
└── profiles/
    └── simcom_profile_a76xx.c       # Immutable .rodata descriptor for SIMCom A76XX series

tests/
├── CMakeLists.txt                   # Host test runner configuration
├── unity/                           # Unity unit test framework (lightweight C test runner)
├── mocks/
│   ├── mock_simcom_hal.h            # Deterministic mock UART TX, tick clock, and log capture
│   └── mock_simcom_hal.c
└── unit/
    ├── test_ringbuf.c               # SPSC ring buffer, wrap-around, and overflow tests
    ├── test_parser.c                # Bounds-checked token/integer parsing & malformed input tests
    ├── test_lexer_urc.c             # Fragmented 1-byte RX, overflow resync, mid-command URC tests
    ├── test_init_recovery.c         # Handshake, echo off, CFUN, and Tier 1 -> Tier 2 -> Fault tests
    ├── test_sim_device_info.c       # ATI/IMEI, CPIN states, ICCID, and single-shot PIN tests
    ├── test_network_status.c        # CREG/CGREG/CEREG queries & URCs, CSQ, COPS, CPSI tests
    └── test_logging_config.c        # Log level filtering, null logger, and runtime config tests

CMakeLists.txt                       # Root CMake build with C11, strict warning flags, and stack usage
```

**Structure Decision**: Single portable embedded C library layout (`inc/`, `src/`, `src/profiles/`, `tests/`) at repository root, matching Constitution Section 5 (`inc/` header contracts) and isolating series-specific definitions under `src/profiles/simcom_profile_a76xx.c`.

## Complexity Tracking

> **Constitution deviations explicitly scoped and approved in Feature Specification (`spec.md` Clarifications & Assumptions)**

| Deviation / Scoped Deferral | Why Needed | Simpler / Scoped Alternative Adopted |
| :--- | :--- | :--- |
| **Principle 4 & 6 Partial Deferral**: GPIO control hooks (`gpio_write_pwrkey`, `gpio_write_reset`, `gpio_read_status`, `vbat_enable`) and Hardware Recovery Tiers 3–4 | Spec Clarification (2026-10-21), FR-015, and Assumption 2 scope `001-basic-modem-control` to UART-level initialization, communication, and logical recovery (Tiers 1–2), leaving physical board power sequencing to the host application in v1. | Implement Tiers 1 (soft `AT` re-sync) and 2 (logical `AT+CRESET` + `RDY` wait) and report `SIMCOM_FAULT_MODEM_UNRESPONSIVE` if both are exhausted; reserve lifecycle states and HAL extension points for Tiers 3–4 in a dedicated hardware-power feature. |
| **Principle 4 RX Hook Refinement**: Push entry point `Simcom_FeedBytes()` instead of polling RX inside HAL | Spec Clarification (2026-10-21) and FR-001 specify a push model where the application calls `Simcom_FeedBytes()` from its UART ISR/DMA callback into the library-owned SPSC ring buffer. | Satisfies Principle 3's mandatory ISR-safe lock-free SPSC ring buffer requirement directly inside `libsimcom` so the application only implements `uart_tx_async` and `get_tick_ms`. |
