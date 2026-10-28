/**
 * @file mock_simcom_hal.h
 * @brief Deterministic mock HAL, clock, and event/log spy for libsimcom host tests.
 */

#ifndef MOCK_SIMCOM_HAL_H
#define MOCK_SIMCOM_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "simcom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_TX_BUF_CAP         (2048U)
#define MOCK_MAX_EVENTS         (64U)
#define MOCK_MAX_LOGS           (128U)
#define MOCK_LOG_PAYLOAD_MAX    (128U)

typedef struct {
    simcom_event_type_t event;
    simcom_status_t status;
} mock_event_record_t;

typedef struct {
    simcom_log_level_t level;
    uint32_t timestamp_ms;
    simcom_log_dir_t direction;
    char payload[MOCK_LOG_PAYLOAD_MAX];
    size_t len;
} mock_log_record_t;

typedef struct {
    uint8_t tx_buf[MOCK_TX_BUF_CAP];
    size_t tx_len;
    size_t tx_max_chunk;            /**< Max bytes accepted per uart_tx_async call (0 = unlimited) */
    uint32_t tick_ms;
    uint32_t critical_depth;
    bool pwrkey_asserted;
    bool reset_asserted;
    bool status_pin_high;

    mock_event_record_t events[MOCK_MAX_EVENTS];
    size_t event_count;

    mock_log_record_t logs[MOCK_MAX_LOGS];
    size_t log_count;
} mock_hal_ctx_t;

void mock_hal_init(mock_hal_ctx_t * const ctx);
void mock_hal_clear_tx(mock_hal_ctx_t * const ctx);
void mock_hal_clear_events(mock_hal_ctx_t * const ctx);
void mock_hal_clear_logs(mock_hal_ctx_t * const ctx);
void mock_hal_advance_tick(mock_hal_ctx_t * const ctx, uint32_t delta_ms);
bool mock_hal_tx_contains(const mock_hal_ctx_t * const ctx, const char * const substr);

simcom_hal_if_t mock_hal_make_if(mock_hal_ctx_t * const ctx);
void mock_hal_event_cb(simcom_event_type_t event, simcom_status_t status, void *user_ctx);
void mock_hal_log_cb(
    simcom_log_level_t level,
    uint32_t timestamp_ms,
    simcom_log_dir_t direction,
    const uint8_t *payload,
    size_t len,
    void *user_ctx
);

void mock_feed_str(simcom_handle_t * const me, const char * const str);
void mock_feed_str_byte_by_byte(simcom_handle_t * const me, const char * const str);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_SIMCOM_HAL_H */
