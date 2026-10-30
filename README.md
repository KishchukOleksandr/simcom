# libsimcom

[![Standard: C11](https://img.shields.io/badge/C-11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

`libsimcom` is a bare-metal, MISRA-compliant ISO C11 library for controlling and interfacing with SIMCom cellular modems (primarily the A76XX series) in resource-constrained embedded systems.

---

## Key Features

- **Zero Dynamic Memory Allocation**: No `malloc`/`free`, no heap, zero VLAs. All structures, buffers, and states are statically allocated by the caller or configured via compile-time bounds.
- **Strictly Non-Blocking Execution**: Driven by a state machine stepped through `Simcom_Poll()`. Safe for MCU superloops and RTOS tasks.
- **ISR-Safe Lock-Free Ring Buffer**: Thread-safe single-producer single-consumer (SPSC) RX path via `Simcom_FeedBytes()` paired with asynchronous UART TX callbacks.
- **Dual-Path AT Parser & Lexer**: Clean separation between asynchronous unsolicited result codes (URCs) and active command request/response transactions.
- **Two-Tier Fault Recovery**: Automatic Tier 1 soft re-synchronization (`AT`) and Tier 2 logical reset (`AT+CRESET` / `RDY`).
- **Hardware & MCU Agnostic**: Pure ISO C11 without vendor SDK dependencies. All hardware interactions are abstracted through `simcom_hal_if_t`.
- **Bounded Stack Usage**: Enforced worst-case stack limits (`<= 128 Bytes` for API calls, `<= 32 Bytes` for ISR helpers).

---

## Directory Structure

```text
├── cmake/              # CMake build scripts and stack usage analysis
├── docs/               # Architecture documents, examples, and AT command specs
├── examples/           # Reference applications (Raspberry Pi 5 + Waveshare A7670E HAT)
├── inc/                # Public header files
│   └── internal/       # Private/internal headers
├── specs/              # Feature specifications and implementation plans
├── src/                # Implementation sources
│   └── profiles/       # Hardware profiles (e.g., A76XX)
├── tests/              # Host unit tests, Unity test harness, and mocks
└── trm/                # Technical Reference Manuals (datasheets & AT guides)
```

---

## Reference Example: Raspberry Pi 5 + Waveshare A7670E Cat-1/GNSS HAT

A complete runnable example project is provided in `examples/rpi5_a7670e/` demonstrating BCM GPIO 4 (`PWRKEY`) startup sequencing, non-blocking `/dev/ttyAMA0` UART communication, SIM PIN unlocking, device/network inspection, and continuous URC monitoring. See **[docs/rpi5_a7670e.md](docs/rpi5_a7670e.md)** for hardware jumper setup and usage instructions.

---

## Building and Testing

### Prerequisites

- **CMake**: >= 3.20
- **C Compiler**: GCC or Clang supporting C11 (`-std=c11`)
- **Build tool**: Ninja or Make

### Build

```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Running Tests

```bash
ctest --test-dir build --output-on-failure
```

---

## License

This project is licensed under the MIT License.

