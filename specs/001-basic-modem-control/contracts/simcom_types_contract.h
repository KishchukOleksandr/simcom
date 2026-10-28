/**
 * @file simcom_types_contract.h
 * @brief Data types, HAL contracts, logging contracts, and profile descriptors for libsimcom.
 *
 * Target locations in repository:
 *   - inc/simcom_types.h
 *   - inc/simcom_profile.h
 * Compliance: ISO C11, MISRA C:2012, BARR-C:2018 (fixed-width integer types only).
 */

#ifndef SIMCOM_TYPES_H
#define SIMCOM_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "simcom_cfg_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Return Status & Fault Enumerations                                         */
/* ========================================================================== */

typedef enum {
    SIMCOM_STATUS_OK = 0,               /**< Operation succeeded / accepted immediately */
    SIMCOM_STATUS_PENDING = 1,          /**< Asynchronous workflow started; step via Simcom_Poll */
    SIMCOM_STATUS_BUSY = 2,             /**< Another workflow is currently active */
    SIMCOM_STATUS_INVALID_ARG = 3,      /**< NULL pointer or out-of-range parameter */
    SIMCOM_STATUS_NOT_READY = 4,        /**< Modem not yet initialized or in fault state */
    SIMCOM_STATUS_BUFFER_FULL = 5,      /**< RX ring buffer overflowed; excess bytes dropped */
    SIMCOM_STATUS_ERROR = 6             /**< Domain or protocol error */
} simcom_status_t;

typedef enum {
    SIMCOM_FAULT_NONE = 0,              /**< No active fault */
    SIMCOM_FAULT_MODEM_UNRESPONSIVE = 1,/**< Tiers 1 & 2 exhausted without modem response */
    SIMCOM_FAULT_SIM_ERROR = 2,         /**< SIM absent, crashed, or PUK-locked */
    SIMCOM_FAULT_PROTOCOL_ERROR = 3     /**< Unexpected terminal ERROR / CME ERROR */
} simcom_fault_t;

typedef enum {
    SIMCOM_STATE_UNINITIALIZED = 0,
    SIMCOM_STATE_INITIALIZING = 1,
    SIMCOM_STATE_READY = 2,
    SIMCOM_STATE_BUSY_WORKFLOW = 3,
    SIMCOM_STATE_RECOVERY_TIER1 = 4,
    SIMCOM_STATE_RECOVERY_TIER2 = 5,
    SIMCOM_STATE_FAULT = 6
} simcom_lifecycle_state_t;

typedef enum {
    SIMCOM_LEXER_MODE_LINE = 0,         /**< CRLF-delimited line tokenization */
    SIMCOM_LEXER_MODE_RAW_BYTES = 1     /**< Exact byte-count mode (reserved for binary payloads) */
} simcom_lexer_mode_t;

/* ========================================================================== */
/* Logging Interface (FR-011)                                                 */
/* ========================================================================== */

typedef enum {
    SIMCOM_LOG_LEVEL_DEBUG = 0,
    SIMCOM_LOG_LEVEL_INFO = 1,
    SIMCOM_LOG_LEVEL_WARNING = 2,
    SIMCOM_LOG_LEVEL_ERROR = 3,
    SIMCOM_LOG_LEVEL_NONE = 4
} simcom_log_level_t;

typedef enum {
    SIMCOM_LOG_DIR_TX = 0,              /**< Host -> Modem raw AT command bytes */
    SIMCOM_LOG_DIR_RX = 1,              /**< Modem -> Host solicited response line */
    SIMCOM_LOG_DIR_URC = 2,             /**< Modem -> Host unsolicited result code */
    SIMCOM_LOG_DIR_FSM = 3              /**< Internal lifecycle / recovery transition event */
} simcom_log_dir_t;

typedef void (*simcom_log_fn_t)(
    simcom_log_level_t level,
    uint32_t timestamp_ms,
    simcom_log_dir_t direction,
    const uint8_t *payload,
    size_t len,
    void *user_ctx
);

/* ========================================================================== */
/* Hardware Abstraction Layer (HAL) Interface (FR-001, Principle 4)           */
/* ========================================================================== */

typedef struct {
    /**
     * @brief Non-blocking UART transmit hook.
     * @return Number of bytes accepted for transmission (0..len).
     */
    size_t (*uart_tx_async)(const uint8_t *data, size_t len, void *user_ctx);

    /**
     * @brief Monotonic millisecond tick source.
     */
    uint32_t (*get_tick_ms)(void *user_ctx);

    /**
     * @brief Optional critical section hooks (nullable).
     */
    void (*enter_critical)(void *user_ctx);
    void (*exit_critical)(void *user_ctx);

    /**
     * @brief Optional hardware GPIO hooks reserved for Tier 3/4 recovery (nullable in v1).
     */
    void (*gpio_write_pwrkey)(bool asserted, void *user_ctx);
    void (*gpio_write_reset)(bool asserted, void *user_ctx);
    bool (*gpio_read_status)(void *user_ctx);

    /**
     * @brief Caller context pointer forwarded to HAL, logging, and event callbacks.
     */
    void *user_ctx;
} simcom_hal_if_t;

/* ========================================================================== */
/* Domain Telemetry Entities (FR-004 through FR-009)                          */
/* ========================================================================== */

typedef struct {
    char manufacturer[SIMCOM_CFG_STR_MAX_LEN];
    char model[SIMCOM_CFG_STR_MAX_LEN];
    char revision[SIMCOM_CFG_STR_MAX_LEN];
    char imei[SIMCOM_CFG_IMEI_MAX_LEN];
    bool is_valid;
} simcom_device_info_t;

typedef enum {
    SIMCOM_SIM_STATE_UNKNOWN = 0,
    SIMCOM_SIM_STATE_READY = 1,
    SIMCOM_SIM_STATE_PIN_REQUIRED = 2,
    SIMCOM_SIM_STATE_PUK_REQUIRED = 3,
    SIMCOM_SIM_STATE_ABSENT = 4,
    SIMCOM_SIM_STATE_FAULTY = 5
} simcom_sim_state_t;

typedef struct {
    simcom_sim_state_t state;
    char iccid[SIMCOM_CFG_ICCID_MAX_LEN];
    uint16_t last_cme_error;
    bool is_iccid_valid;
} simcom_sim_info_t;

typedef enum {
    SIMCOM_REG_NOT_REGISTERED = 0,
    SIMCOM_REG_HOME = 1,
    SIMCOM_REG_SEARCHING = 2,
    SIMCOM_REG_DENIED = 3,
    SIMCOM_REG_UNKNOWN = 4,
    SIMCOM_REG_ROAMING = 5
} simcom_reg_state_t;

typedef enum {
    SIMCOM_ACT_UNKNOWN = 0,
    SIMCOM_ACT_GSM = 1,
    SIMCOM_ACT_UTRAN = 2,
    SIMCOM_ACT_LTE = 3
} simcom_act_t;

typedef struct {
    simcom_reg_state_t cs_reg;          /**< Circuit-switched (+CREG) state */
    simcom_reg_state_t ps_reg;          /**< Packet-switched (+CGREG) state */
    simcom_reg_state_t eps_reg;         /**< EPS/LTE (+CEREG) state */
    uint8_t rssi_raw;                   /**< Raw +CSQ <rssi> (0..31, 99=unknown) */
    int16_t rssi_dbm;                   /**< Signal strength in dBm (-113..-51, 0=unknown) */
    uint8_t ber_raw;                    /**< Raw +CSQ <ber> (0..7, 99=unknown) */
    simcom_act_t act;                   /**< Active Radio Access Technology */
    char operator_name[SIMCOM_CFG_STR_MAX_LEN]; /**< Registered carrier name from +COPS? */
    char system_mode[16];               /**< Serving cell mode from +CPSI? */
} simcom_net_status_t;

/* ========================================================================== */
/* Application Event Callbacks & Runtime Configuration                        */
/* ========================================================================== */

typedef enum {
    SIMCOM_EVENT_INIT_COMPLETE = 0,
    SIMCOM_EVENT_DEVICE_INFO_READY = 1,
    SIMCOM_EVENT_SIM_STATUS_READY = 2,
    SIMCOM_EVENT_SIM_PIN_RESULT = 3,
    SIMCOM_EVENT_NET_STATUS_READY = 4,
    SIMCOM_EVENT_NET_REG_URC_CHANGED = 5, /**< Asynchronous +CREG/+CGREG/+CEREG change (FR-008) */
    SIMCOM_EVENT_FAULT = 6                /**< Fault escalation / MODEM_UNRESPONSIVE (FR-015) */
} simcom_event_type_t;

typedef void (*simcom_event_cb_t)(
    simcom_event_type_t event,
    simcom_status_t result_status,
    void *user_ctx
);

typedef struct {
    uint32_t default_cmd_timeout_ms;
    uint32_t net_query_timeout_ms;
    uint8_t max_cmd_retries;
    uint8_t tier1_sync_retries;
    uint32_t tier2_reset_timeout_ms;
    simcom_log_level_t min_log_level;
} simcom_runtime_cfg_t;

/* ========================================================================== */
/* Modem Profile Abstraction (FR-014)                                         */
/* ========================================================================== */

typedef struct {
    const char *series_name;
    const char *cmd_handshake;
    const char *cmd_echo_off;
    const char *cmd_cfun_query;
    const char *cmd_cfun_full;
    const char *cmd_cmee_numeric;
    const char *cmd_iccid_query;
    const char *cmd_sys_info;
    const char *cmd_logical_reset;
    const char *boot_ready_urc;
    uint32_t handshake_timeout_ms;
    uint32_t reset_boot_timeout_ms;
} simcom_modem_profile_t;

/** @brief Built-in immutable profile descriptor for SIMCom A76XX series. */
extern const simcom_modem_profile_t g_simcom_profile_a76xx;

/* ========================================================================== */
/* Static Modem Instance Handle (`simcom_handle_t`)                           */
/* ========================================================================== */

typedef struct {
    simcom_hal_if_t hal;
    simcom_log_fn_t log_fn;
    simcom_event_cb_t event_cb;
    const simcom_modem_profile_t *profile;
    simcom_runtime_cfg_t cfg;

    /* Lock-free SPSC RX ring buffer */
    uint8_t rx_buf[SIMCOM_CFG_RX_BUF_SIZE];
    volatile size_t rx_head;
    volatile size_t rx_tail;
    volatile bool rx_overflow;

    /* Line Lexer */
    simcom_lexer_mode_t lexer_mode;
    uint8_t line_buf[SIMCOM_CFG_LINE_BUF_SIZE];
    size_t line_len;
    size_t raw_bytes_remaining;
    bool resync_discard_line;

    /* Active AT Command Transaction */
    uint8_t tx_buf[SIMCOM_CFG_TX_CMD_BUF_SIZE];
    size_t tx_len;
    size_t tx_offset;
    uint32_t cmd_start_tick_ms;
    uint32_t cmd_timeout_ms;
    uint8_t cmd_retries_left;
    uint8_t at_tx_state;

    /* Master Workflow & Recovery FSM */
    simcom_lifecycle_state_t state;
    simcom_lifecycle_state_t pre_recovery_state;
    uint8_t active_workflow;
    uint8_t step_index;
    uint8_t tier1_attempts;
    simcom_fault_t fault_code;

    /* Cached Domain Entities */
    simcom_device_info_t device_info;
    simcom_sim_info_t sim_info;
    simcom_net_status_t net_status;
    char pin_scratch[SIMCOM_CFG_PIN_MAX_LEN];
} simcom_handle_t;

typedef struct {
    simcom_hal_if_t hal;
    const simcom_modem_profile_t *profile; /**< Defaults to &g_simcom_profile_a76xx if NULL */
    const simcom_runtime_cfg_t *runtime_cfg; /**< Uses compile-time defaults if NULL */
    simcom_log_fn_t log_fn;                /**< Optional diagnostic logger (NULL to disable) */
    simcom_event_cb_t event_cb;            /**< Optional workflow/URC/fault event callback */
} simcom_init_t;

#ifdef __cplusplus
}
#endif

#endif /* SIMCOM_TYPES_H */
