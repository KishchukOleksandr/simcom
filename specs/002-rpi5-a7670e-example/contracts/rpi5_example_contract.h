/**
 * @file rpi5_example_contract.h
 * @brief Interface and CLI contract for the Raspberry Pi 5 & Waveshare A7670E HAT example (002-rpi5-a7670e-example).
 *
 * Compliance:
 *   - Core libsimcom headers and FSM remain 100% unmodified (FR-008).
 *   - Application coordinator (rpi5_app.c) is pure ISO C11 with zero dynamic memory allocation,
 *     enabling deterministic host unit testing on Windows/Linux alongside Raspberry Pi 5 execution.
 */

#ifndef RPI5_EXAMPLE_CONTRACT_H
#define RPI5_EXAMPLE_CONTRACT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "simcom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Default Hardware Constants (Raspberry Pi 5 + Waveshare A7670E HAT)         */
/* ========================================================================== */

#define RPI5_DEFAULT_SERIAL_PORT        "/dev/ttyAMA0"
#define RPI5_DEFAULT_BAUD_RATE          (115200U)
#define RPI5_DEFAULT_PWRKEY_GPIO_PIN    (4U)        /**< BCM GPIO 4 (Header Pin 7, HAT jumper D4) */
#define RPI5_DEFAULT_PWRKEY_PULSE_MS    (600U)      /**< 600 ms HIGH pulse (asserts PWRKEY low) */
#define RPI5_DEFAULT_PWRKEY_BOOT_MS     (3500U)     /**< 3.5 s boot settle wait after releasing PWRKEY */
#define RPI5_DEFAULT_MONITOR_PERIOD_MS  (10000U)    /**< 10 s periodic network/signal refresh */

/* ========================================================================== */
/* Runtime Configuration & CLI Contract (FR-003, FR-005, FR-007)              */
/* ========================================================================== */

/**
 * Supported CLI Flags:
 *   -p, --port <path>          Serial device path (default: /dev/ttyAMA0)
 *   -b, --baud <rate>          UART baud rate (default: 115200)
 *   -g, --gpio-chip <path>     GPIO character device path (default: auto /dev/gpiochip4 -> /dev/gpiochip0)
 *   -k, --pwrkey-pin <num>     BCM GPIO pin number for PWRKEY (default: 4)
 *   -P, --pin <digits>         Optional 4..8 digit SIM PIN code for locked SIM cards
 *   -1, --once                 Run inspection sequence once and exit immediately
 *   -v, --verbose              Enable DEBUG-level raw AT TX/RX/URC/FSM logging
 *   -q, --quiet                Suppress INFO logs (show WARNING/ERROR and status report only)
 *   -h, --help                 Display CLI usage instructions and exit 0
 */
typedef struct {
    const char *serial_port;
    uint32_t baud_rate;
    const char *gpio_chip;
    uint32_t pwrkey_gpio_pin;
    uint32_t pwrkey_pulse_ms;
    uint32_t pwrkey_boot_wait_ms;
    uint32_t monitor_interval_ms;
    const char *sim_pin;
    simcom_log_level_t log_level;
    bool run_once;
    bool show_help;
} rpi5_example_cfg_t;

/* ========================================================================== */
/* Platform Hardware Adapter Contract (FR-002)                                */
/* ========================================================================== */

typedef struct {
    size_t (*uart_tx)(const uint8_t *data, size_t len, void *ctx);
    uint32_t (*get_tick_ms)(void *ctx);
    bool (*gpio_set_pwrkey)(bool high, void *ctx);
    void (*console_print)(const char *text, void *ctx);
    void *ctx;
} rpi5_platform_if_t;

/* ========================================================================== */
/* Application Workflow State Machine (FR-004, FR-005)                        */
/* ========================================================================== */

typedef enum {
    RPI5_APP_STATE_IDLE = 0,
    RPI5_APP_STATE_PWRKEY_ASSERT = 1,
    RPI5_APP_STATE_PWRKEY_WAIT_BOOT = 2,
    RPI5_APP_STATE_MODEM_INIT = 3,
    RPI5_APP_STATE_QUERY_DEV_INFO = 4,
    RPI5_APP_STATE_QUERY_SIM = 5,
    RPI5_APP_STATE_SUBMIT_PIN = 6,
    RPI5_APP_STATE_QUERY_NET = 7,
    RPI5_APP_STATE_MONITOR = 8,
    RPI5_APP_STATE_DONE = 9,
    RPI5_APP_STATE_ERROR = 10
} rpi5_app_state_t;

typedef struct {
    simcom_handle_t modem;
    rpi5_example_cfg_t cfg;
    rpi5_platform_if_t platform;
    rpi5_app_state_t state;
    uint32_t phase_start_ms;
    bool pin_attempted;
    bool initial_report_printed;
    int32_t exit_code;
} rpi5_app_t;

/* ========================================================================== */
/* Public Application Coordinator API                                         */
/* ========================================================================== */

/**
 * @brief Populate default configuration values for Raspberry Pi 5 + Waveshare A7670E HAT.
 */
void Rpi5App_DefaultConfig(rpi5_example_cfg_t * const cfg);

/**
 * @brief Parse command-line arguments into rpi5_example_cfg_t.
 * @return SIMCOM_STATUS_OK on valid arguments, SIMCOM_STATUS_INVALID_ARG on unknown/malformed option.
 */
simcom_status_t Rpi5App_ParseArgs(
    int32_t argc,
    const char * const argv[],
    rpi5_example_cfg_t * const out_cfg
);

/**
 * @brief Format CLI help text into caller buffer or print via platform console callback.
 */
void Rpi5App_PrintHelp(const char *prog_name, const rpi5_platform_if_t * const platform);

/**
 * @brief Initialize the Raspberry Pi 5 example coordinator and underlying libsimcom instance.
 */
simcom_status_t Rpi5App_Init(
    rpi5_app_t * const app,
    const rpi5_example_cfg_t * const cfg,
    const rpi5_platform_if_t * const platform
);

/**
 * @brief Begin the mandatory GPIO 4 PWRKEY startup sequence followed by modem initialization.
 */
simcom_status_t Rpi5App_Start(rpi5_app_t * const app);

/**
 * @brief Feed incoming serial bytes into the coordinator's libsimcom instance.
 */
simcom_status_t Rpi5App_FeedBytes(
    rpi5_app_t * const app,
    const uint8_t * const data,
    size_t len
);

/**
 * @brief Non-blocking periodic poll step driving GPIO PWRKEY timing, libsimcom_Poll(), and workflow transitions.
 */
void Rpi5App_Poll(rpi5_app_t * const app);

/**
 * @brief Request a clean shutdown (e.g., from SIGINT / SIGTERM handler).
 */
void Rpi5App_RequestStop(rpi5_app_t * const app);

/**
 * @brief Check whether the coordinator has reached a terminal state (DONE or ERROR).
 */
bool Rpi5App_IsFinished(const rpi5_app_t * const app);

#ifdef __cplusplus
}
#endif

#endif /* RPI5_EXAMPLE_CONTRACT_H */
