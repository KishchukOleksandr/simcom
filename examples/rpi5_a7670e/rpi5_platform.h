/**
 * @file rpi5_platform.h
 * @brief Linux/Raspberry Pi 5 hardware adapter declarations (UART termios, GPIO v2 ioctl, CLOCK_MONOTONIC).
 */

#ifndef RPI5_PLATFORM_H
#define RPI5_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "rpi5_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Linux platform runtime state for Raspberry Pi 5 UART and GPIO v2 character device handles.
 */
typedef struct {
    int32_t uart_fd;            /**< Non-blocking serial port file descriptor (-1 if closed) */
    int32_t gpio_chip_fd;       /**< Linux /dev/gpiochipN file descriptor (-1 if closed) */
    int32_t gpio_line_fd;       /**< Linux GPIO v2 line handle file descriptor (-1 if closed) */
    uint64_t boot_epoch_ms;     /**< Initial CLOCK_MONOTONIC timestamp in ms */
} rpi5_linux_ctx_t;

/**
 * @brief Initialize rpi5_linux_ctx_t descriptors to safe closed defaults (-1) and bind callbacks.
 * @param[out] linux_ctx Platform context struct.
 * @param[out] out_if    Platform callback table populated with Linux implementations.
 */
void Rpi5Platform_BindCallbacks(
    rpi5_linux_ctx_t * const linux_ctx,
    rpi5_platform_if_t * const out_if
);

/**
 * @brief Open and configure the Raspberry Pi 5 serial port (8N1 raw non-blocking) and BCM GPIO 4 line.
 * @param[in,out] linux_ctx Platform context struct.
 * @param[in]     cfg       Parsed runtime configuration (serial_port, baud_rate, gpio_chip, pwrkey_gpio_pin).
 * @return true on success, false if serial port or GPIO line could not be opened.
 */
bool Rpi5Platform_Open(
    rpi5_linux_ctx_t * const linux_ctx,
    const rpi5_example_cfg_t * const cfg
);

/**
 * @brief Wait up to timeout_ms for incoming UART bytes using poll(), read available bytes, and feed rpi5_app_t.
 * @param[in,out] linux_ctx Platform context struct.
 * @param[in,out] app       Application coordinator instance receiving bytes via Rpi5App_FeedBytes().
 * @param[in]     timeout_ms Maximum poll() sleep duration in milliseconds (e.g., 5 ms).
 */
void Rpi5Platform_PollRx(
    rpi5_linux_ctx_t * const linux_ctx,
    rpi5_app_t * const app,
    int32_t timeout_ms
);

/**
 * @brief Release GPIO line, close GPIO chip and UART file descriptors cleanly.
 * @param[in,out] linux_ctx Platform context struct.
 */
void Rpi5Platform_Close(rpi5_linux_ctx_t * const linux_ctx);

#ifdef __cplusplus
}
#endif

#endif /* RPI5_PLATFORM_H */
