/**
 * @file mock_simcom_hal.c
 * @brief Implementation of deterministic mock HAL and test helpers.
 */

#include "mock_simcom_hal.h"
#include <string.h>

static size_t mock_uart_tx_async(const uint8_t *data, size_t len, void *user_ctx)
{
    mock_hal_ctx_t * const ctx = (mock_hal_ctx_t *)user_ctx;
    if ((ctx == NULL) || (data == NULL) || (len == 0U)) {
        return 0U;
    }

    size_t to_write = len;
    if ((ctx->tx_max_chunk > 0U) && (to_write > ctx->tx_max_chunk)) {
        to_write = ctx->tx_max_chunk;
    }

    const size_t space = (ctx->tx_len < (MOCK_TX_BUF_CAP - 1U))
                         ? ((MOCK_TX_BUF_CAP - 1U) - ctx->tx_len)
                         : 0U;
    if (to_write > space) {
        to_write = space;
    }

    if (to_write > 0U) {
        (void)memcpy(&ctx->tx_buf[ctx->tx_len], data, to_write);
        ctx->tx_len += to_write;
        ctx->tx_buf[ctx->tx_len] = 0U;
    }
    return to_write;
}

static uint32_t mock_get_tick_ms(void *user_ctx)
{
    const mock_hal_ctx_t * const ctx = (const mock_hal_ctx_t *)user_ctx;
    return (ctx != NULL) ? ctx->tick_ms : 0U;
}

static void mock_enter_critical(void *user_ctx)
{
    mock_hal_ctx_t * const ctx = (mock_hal_ctx_t *)user_ctx;
    if (ctx != NULL) {
        ctx->critical_depth++;
    }
}

static void mock_exit_critical(void *user_ctx)
{
    mock_hal_ctx_t * const ctx = (mock_hal_ctx_t *)user_ctx;
    if ((ctx != NULL) && (ctx->critical_depth > 0U)) {
        ctx->critical_depth--;
    }
}

static void mock_gpio_pwrkey(bool asserted, void *user_ctx)
{
    mock_hal_ctx_t * const ctx = (mock_hal_ctx_t *)user_ctx;
    if (ctx != NULL) {
        ctx->pwrkey_asserted = asserted;
    }
}

static void mock_gpio_reset(bool asserted, void *user_ctx)
{
    mock_hal_ctx_t * const ctx = (mock_hal_ctx_t *)user_ctx;
    if (ctx != NULL) {
        ctx->reset_asserted = asserted;
    }
}

static bool mock_gpio_status(void *user_ctx)
{
    const mock_hal_ctx_t * const ctx = (const mock_hal_ctx_t *)user_ctx;
    return (ctx != NULL) ? ctx->status_pin_high : true;
}

void mock_hal_init(mock_hal_ctx_t * const ctx)
{
    if (ctx != NULL) {
        (void)memset(ctx, 0, sizeof(*ctx));
        ctx->status_pin_high = true;
    }
}

void mock_hal_clear_tx(mock_hal_ctx_t * const ctx)
{
    if (ctx != NULL) {
        (void)memset(ctx->tx_buf, 0, sizeof(ctx->tx_buf));
        ctx->tx_len = 0U;
    }
}

void mock_hal_clear_events(mock_hal_ctx_t * const ctx)
{
    if (ctx != NULL) {
        (void)memset(ctx->events, 0, sizeof(ctx->events));
        ctx->event_count = 0U;
    }
}

void mock_hal_clear_logs(mock_hal_ctx_t * const ctx)
{
    if (ctx != NULL) {
        (void)memset(ctx->logs, 0, sizeof(ctx->logs));
        ctx->log_count = 0U;
    }
}

void mock_hal_advance_tick(mock_hal_ctx_t * const ctx, uint32_t delta_ms)
{
    if (ctx != NULL) {
        ctx->tick_ms += delta_ms;
    }
}

bool mock_hal_tx_contains(const mock_hal_ctx_t * const ctx, const char * const substr)
{
    if ((ctx == NULL) || (substr == NULL)) {
        return false;
    }
    return (strstr((const char *)ctx->tx_buf, substr) != NULL);
}

simcom_hal_if_t mock_hal_make_if(mock_hal_ctx_t * const ctx)
{
    simcom_hal_if_t hal;
    hal.uart_tx_async = mock_uart_tx_async;
    hal.get_tick_ms = mock_get_tick_ms;
    hal.enter_critical = mock_enter_critical;
    hal.exit_critical = mock_exit_critical;
    hal.gpio_write_pwrkey = mock_gpio_pwrkey;
    hal.gpio_write_reset = mock_gpio_reset;
    hal.gpio_read_status = mock_gpio_status;
    hal.user_ctx = ctx;
    return hal;
}

void mock_hal_event_cb(simcom_event_type_t event, simcom_status_t status, void *user_ctx)
{
    mock_hal_ctx_t * const ctx = (mock_hal_ctx_t *)user_ctx;
    if ((ctx != NULL) && (ctx->event_count < MOCK_MAX_EVENTS)) {
        ctx->events[ctx->event_count].event = event;
        ctx->events[ctx->event_count].status = status;
        ctx->event_count++;
    }
}

void mock_hal_log_cb(
    simcom_log_level_t level,
    uint32_t timestamp_ms,
    simcom_log_dir_t direction,
    const uint8_t *payload,
    size_t len,
    void *user_ctx)
{
    mock_hal_ctx_t * const ctx = (mock_hal_ctx_t *)user_ctx;
    if ((ctx != NULL) && (ctx->log_count < MOCK_MAX_LOGS)) {
        mock_log_record_t * const rec = &ctx->logs[ctx->log_count];
        rec->level = level;
        rec->timestamp_ms = timestamp_ms;
        rec->direction = direction;
        rec->len = len;
        const size_t copy_len = (len < (MOCK_LOG_PAYLOAD_MAX - 1U)) ? len : (MOCK_LOG_PAYLOAD_MAX - 1U);
        if ((payload != NULL) && (copy_len > 0U)) {
            (void)memcpy(rec->payload, payload, copy_len);
        }
        rec->payload[copy_len] = '\0';
        ctx->log_count++;
    }
}

void mock_feed_str(simcom_handle_t * const me, const char * const str)
{
    if ((me != NULL) && (str != NULL)) {
        const size_t len = strlen(str);
        (void)Simcom_FeedBytes(me, (const uint8_t *)str, len, NULL);
    }
}

void mock_feed_str_byte_by_byte(simcom_handle_t * const me, const char * const str)
{
    if ((me != NULL) && (str != NULL)) {
        const size_t len = strlen(str);
        for (size_t i = 0U; i < len; ++i) {
            (void)Simcom_FeedBytes(me, (const uint8_t *)&str[i], 1U, NULL);
            (void)Simcom_Poll(me);
        }
    }
}
