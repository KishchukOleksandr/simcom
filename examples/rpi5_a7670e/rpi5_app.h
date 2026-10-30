/**
 * @file rpi5_app.h
 * @brief Portable application coordinator and CLI interface for Raspberry Pi 5 & Waveshare A7670E HAT (002-rpi5-a7670e-example).
 *
 * Compliance:
 *   - Core libsimcom headers and FSM remain 100% unmodified (FR-008).
 *   - Pure ISO C11 with zero dynamic memory allocation and fixed-width types (Principle 1, Section 3).
 */

#ifndef RPI5_APP_H
#define RPI5_APP_H

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

/** @brief Default Raspberry Pi 5 40-pin header UART device path (GPIO14 TXD0 / GPIO15 RXD0). */
#define RPI5_DEFAULT_SERIAL_PORT        "/dev/ttyAMA0"

/** @brief Default SIMCom A7670E UART baud rate. */
#define RPI5_DEFAULT_BAUD_RATE          (115200U)

/** @brief Default Raspberry Pi BCM GPIO pin wired to Waveshare HAT PWRKEY (Header Pin 7, jumper D4). */
#define RPI5_DEFAULT_PWRKEY_GPIO_PIN    (4U)

/** @brief Duration in ms to hold BCM GPIO 4 HIGH (asserting PWRKEY low) during startup power-on. */
#define RPI5_DEFAULT_PWRKEY_PULSE_MS    (600U)

/** @brief Post-pulse settle duration in ms before starting the AT command handshake. */
#define RPI5_DEFAULT_PWRKEY_BOOT_MS     (3500U)

/** @brief Periodic network & signal refresh interval in ms during continuous monitoring mode. */
#define RPI5_DEFAULT_MONITOR_PERIOD_MS  (10000U)

/* ========================================================================== */
/* Runtime Configuration & CLI Contract (FR-003, FR-005, FR-007)              */
/* ========================================================================== */

/**
 * @brief User-configurable runtime options parsed from CLI flags.
 */
typedef struct {
    const char *serial_port;            /**< Serial device path (-p / --port) */
    uint32_t baud_rate;                 /**< UART baud rate (-b / --baud) */
    const char *gpio_chip;              /**< Optional GPIO character device path (-g / --gpio-chip) */
    uint32_t pwrkey_gpio_pin;           /**< BCM GPIO pin number for PWRKEY (-k / --pwrkey-pin) */
    uint32_t pwrkey_pulse_ms;           /**< GPIO 4 HIGH pulse duration in ms */
    uint32_t pwrkey_boot_wait_ms;       /**< Post-pulse boot wait duration in ms */
    uint32_t monitor_interval_ms;       /**< Periodic refresh interval in ms in continuous mode */
    const char *sim_pin;                /**< Optional 4..8 digit SIM PIN string (-P / --pin) */
    simcom_log_level_t log_level;       /**< Diagnostic log severity threshold (-v / -q) */
    bool run_once;                      /**< Exit immediately after initial report (-1 / --once) */
    bool show_help;                     /**< Display CLI help and exit 0 (-h / --help) */
} rpi5_example_cfg_t;

/* ========================================================================== */
/* Platform Hardware Adapter Contract (FR-002)                                */
/* ========================================================================== */

/**
 * @brief Host platform callbacks decoupling the example coordinator from OS system calls.
 */
typedef struct {
    /** @brief Non-blocking serial transmit callback returning number of bytes written (0..len). */
    size_t (*uart_tx)(const uint8_t *data, size_t len, void *ctx);

    /** @brief Monotonic millisecond clock source. */
    uint32_t (*get_tick_ms)(void *ctx);

    /** @brief Drive BCM GPIO 4 (high=true asserts PWRKEY low, high=false releases PWRKEY). */
    bool (*gpio_set_pwrkey)(bool high, void *ctx);

    /** @brief Output a NUL-terminated formatted text line to the console. */
    void (*console_print)(const char *text, void *ctx);

    /** @brief Opaque platform context pointer forwarded to callbacks. */
    void *ctx;
} rpi5_platform_if_t;

/* ========================================================================== */
/* Application Workflow State Machine (FR-004, FR-005)                        */
/* ========================================================================== */

/**
 * @brief Non-blocking application workflow states.
 */
typedef enum {
    RPI5_APP_STATE_IDLE = 0,            /**< Initialized, waiting for Rpi5App_Start() */
    RPI5_APP_STATE_PWRKEY_ASSERT = 1,   /**< Holding BCM GPIO 4 HIGH for pwrkey_pulse_ms */
    RPI5_APP_STATE_PWRKEY_WAIT_BOOT = 2,/**< Holding BCM GPIO 4 LOW for pwrkey_boot_wait_ms */
    RPI5_APP_STATE_MODEM_INIT = 3,      /**< Running Simcom_StartInit() handshake */
    RPI5_APP_STATE_QUERY_DEV_INFO = 4,  /**< Running Simcom_RequestDeviceInfo() */
    RPI5_APP_STATE_QUERY_SIM = 5,       /**< Running Simcom_RequestSimStatus() */
    RPI5_APP_STATE_SUBMIT_PIN = 6,      /**< Running single-shot Simcom_SubmitSimPin() */
    RPI5_APP_STATE_QUERY_NET = 7,       /**< Running initial Simcom_RequestNetworkStatus() */
    RPI5_APP_STATE_MONITOR = 8,         /**< Continuous URC & periodic signal monitoring loop */
    RPI5_APP_STATE_DONE = 9,            /**< Clean exit state (exit_code == 0) */
    RPI5_APP_STATE_ERROR = 10           /**< Terminal fault/error state (exit_code != 0) */
} rpi5_app_state_t;

/**
 * @brief Static application coordinator instance structure.
 */
typedef struct {
    simcom_handle_t modem;              /**< Embedded libsimcom instance handle */
    rpi5_example_cfg_t cfg;             /**< Active runtime configuration */
    rpi5_platform_if_t platform;        /**< Bound platform hardware adapter */
    rpi5_app_state_t state;             /**< Current coordinator workflow state */
    uint32_t phase_start_ms;            /**< Entry timestamp of current phase or last refresh */
    bool pin_attempted;                 /**< Single-shot guard ensuring PIN is submitted at most once */
    bool initial_report_printed;        /**< Tracks whether the initial full status report was printed */
    int32_t exit_code;                  /**< Process exit code (0 on success, non-zero on error) */
} rpi5_app_t;

/* ========================================================================== */
/* Public Application Coordinator API                                         */
/* ========================================================================== */

/**
 * @brief Populate default configuration values for Raspberry Pi 5 + Waveshare A7670E HAT.
 * @param[out] cfg Configuration struct to initialize with defaults.
 */
void Rpi5App_DefaultConfig(rpi5_example_cfg_t * const cfg);

/**
 * @brief Parse command-line arguments into rpi5_example_cfg_t.
 * @param[in]  argc    Argument count (cast from main's argc).
 * @param[in]  argv    Argument string vector.
 * @param[out] out_cfg Output configuration structure.
 * @return SIMCOM_STATUS_OK on valid arguments, SIMCOM_STATUS_INVALID_ARG on invalid option.
 */
simcom_status_t Rpi5App_ParseArgs(
    int32_t argc,
    const char * const argv[],
    rpi5_example_cfg_t * const out_cfg
);

/**
 * @brief Print CLI usage help via the platform console callback.
 * @param[in] prog_name Program executable name (argv[0]).
 * @param[in] platform  Platform interface with valid console_print callback.
 */
void Rpi5App_PrintHelp(const char *prog_name, const rpi5_platform_if_t * const platform);

/**
 * @brief Initialize the Raspberry Pi 5 example coordinator and underlying libsimcom instance.
 * @param[in,out] app      Caller-allocated application coordinator struct.
 * @param[in]     cfg      Runtime configuration.
 * @param[in]     platform Bound platform hardware adapter callbacks.
 * @return SIMCOM_STATUS_OK on success, SIMCOM_STATUS_INVALID_ARG if required pointers/hooks are NULL.
 */
simcom_status_t Rpi5App_Init(
    rpi5_app_t * const app,
    const rpi5_example_cfg_t * const cfg,
    const rpi5_platform_if_t * const platform
);

/**
 * @brief Begin the mandatory GPIO 4 PWRKEY startup sequence followed by modem initialization.
 * @param[in,out] app Initialized application coordinator.
 * @return SIMCOM_STATUS_OK if started, SIMCOM_STATUS_ERROR if GPIO assertion fails.
 */
simcom_status_t Rpi5App_Start(rpi5_app_t * const app);

/**
 * @brief Feed incoming serial bytes into the coordinator's libsimcom instance.
 * @param[in,out] app  Initialized application coordinator.
 * @param[in]     data Received byte buffer.
 * @param[in]     len  Number of bytes received.
 * @return SIMCOM_STATUS_OK or SIMCOM_STATUS_BUFFER_FULL.
 */
simcom_status_t Rpi5App_FeedBytes(
    rpi5_app_t * const app,
    const uint8_t * const data,
    size_t len
);

/**
 * @brief Non-blocking periodic poll step driving GPIO PWRKEY timing, Simcom_Poll(), and workflow transitions.
 * @param[in,out] app Initialized application coordinator.
 */
void Rpi5App_Poll(rpi5_app_t * const app);

/**
 * @brief Request a clean shutdown (e.g., invoked from SIGINT / SIGTERM handler).
 * @param[in,out] app Initialized application coordinator.
 */
void Rpi5App_RequestStop(rpi5_app_t * const app);

/**
 * @brief Check whether the coordinator has reached a terminal state (DONE or ERROR).
 * @param[in] app Initialized application coordinator.
 * @return true if finished, false otherwise.
 */
bool Rpi5App_IsFinished(const rpi5_app_t * const app);

#ifdef __cplusplus
}
#endif

#endif /* RPI5_APP_H */
