# libsimcom Documentation {#mainpage}

Welcome to the documentation for **libsimcom** — a portable, zero-heap, non-blocking C11 library engineered for controlling **SIMCom A76XX series** cellular LTE/GSM modems on bare-metal microcontrollers and embedded RTOS systems.

---

## Key Highlights

- **Zero Dynamic Memory Allocation**: No `malloc`, `calloc`, or heap usage. All state, buffers, and handles are caller-allocated or static.
- **Strictly Non-Blocking**: Public API operations execute in bounded $O(1)$ / $O(N)$ time. The finite state machine (FSM) is driven deterministically via `Simcom_Poll()`.
- **Lock-Free SPSC Ring Buffer**: ISR-safe byte ingestion via `Simcom_FeedBytes()` with single-producer single-consumer atomicity.
- **Dual-Path AT Parser**: Isolates asynchronous Unsolicited Result Codes (URCs) from in-flight command transactions without blocking or dropping telemetry.
- **Two-Tier Fault Recovery**: Automatic recovery escalation ladder (soft re-synchronization followed by logical modem reset).
- **Embedded C Standard Compliance**: ISO C11 (`-std=c11`), MISRA C:2012, and BARR-C:2018 type safety. Strict Worst-Case Stack Usage (WCSU) <= 128 bytes.

---

## Documentation Guide

This documentation is organized into three major sections:

1. **[Algorithms & Architecture](algorithms.md)**
   - Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
   - Dual-Path AT Parser and Lexer (URC vs. Command responses)
   - Non-Blocking Finite State Machine (FSM) and Tick Subtraction
   - Two-Tier Fault Recovery Ladder

2. **[Usage Examples & Integration](examples.md)**
   - Bare-Metal Superloop and UART ISR Hookup
   - Modem Initialization & Configuration Workflow
   - SIM Card Verification and PIN Unlocking
   - Signal Quality and Network Registration Monitoring
   - Diagnostic Logging Integration

3. **[Raspberry Pi 5 & Waveshare A7670E HAT Example](rpi5_a7670e.md)**
   - Complete runnable Linux reference project (`examples/rpi5_a7670e/`)
   - Waveshare A7670E HAT jumper settings (`PWR` to `D4`), Raspberry Pi 5 UART0 & RP1 GPIO 4 setup
   - CLI options, SIM PIN unlock, and live network/signal status report

4. **[AT Command Reference](base_cmd.md)**
   - Quick reference for SIMCom A76XX series AT commands.

5. **[API Reference](files.html)**
   - Complete documentation of public headers, types, and functions:
     - `simcom.h`: Main public interface and lifecycle functions
     - `simcom_types.h`: Data types, handles, status codes, and callbacks
     - `simcom_cfg.h`: Static buffer sizes and compile-time limits
     - `simcom_profile.h`: Modem command profile descriptors
     - `rpi5_app.h`: Portable Raspberry Pi 5 + Waveshare A7670E example coordinator API

---

## Quick Example Preview

```c
#include "simcom.h"

static simcom_handle_t g_modem;

/* 1. Feed bytes from UART RX ISR */
void USART1_IRQHandler(void) {
    if (USART1->ISR & USART_ISR_RXNE) {
        uint8_t byte = (uint8_t)(USART1->RDR);
        Simcom_FeedBytes(&g_modem, &byte, 1);
    }
}

/* 2. Periodic superloop processing */
void App_Loop(void) {
    Simcom_Poll(&g_modem);
}
```

For full setup code including HAL binding, asynchronous callbacks, and state checks, see **[Usage Examples & Integration](examples.md)**.
