/**
 * @file test_logging_config.c
 * @brief Unit tests for User Story 4: Diagnostic logging callback, severity filtering,
 *        and NULL logger execution (T029).
 */

#include "unity.h"
#include "mock_simcom_hal.h"
#include <string.h>

static simcom_handle_t s_modem;
static mock_hal_ctx_t s_hal_ctx;

void setUp(void)
{
    mock_hal_init(&s_hal_ctx);
}

void tearDown(void)
{
}

static void test_logging_captures_tx_rx_urc_and_fsm_when_debug_enabled(void)
{
    simcom_runtime_cfg_t cfg = {
        .default_cmd_timeout_ms = 1000U,
        .net_query_timeout_ms = 5000U,
        .max_cmd_retries = 2U,
        .tier1_sync_retries = 3U,
        .tier2_reset_timeout_ms = 10000U,
        .min_log_level = SIMCOM_LOG_LEVEL_DEBUG
    };
    simcom_init_t init = {
        .hal = mock_hal_make_if(&s_hal_ctx),
        .profile = NULL,
        .runtime_cfg = &cfg,
        .log_fn = mock_hal_log_cb,
        .event_cb = mock_hal_event_cb
    };
    s_hal_ctx.tick_ms = 1234U;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_Init(&s_modem, &init));
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_StartInit(&s_modem));

    /* Step 0: AT -> OK, plus an unsolicited RDY */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nRDY\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    bool saw_fsm = false;
    bool saw_tx = false;
    bool saw_urc = false;
    bool saw_rx = false;

    for (size_t i = 0U; i < s_hal_ctx.log_count; ++i) {
        TEST_ASSERT_EQUAL_UINT32(1234U, s_hal_ctx.logs[i].timestamp_ms);
        if (s_hal_ctx.logs[i].direction == SIMCOM_LOG_DIR_FSM) {
            saw_fsm = true;
        } else if (s_hal_ctx.logs[i].direction == SIMCOM_LOG_DIR_TX) {
            saw_tx = true;
            TEST_ASSERT_EQUAL_STRING("AT\r\n", s_hal_ctx.logs[i].payload);
        } else if (s_hal_ctx.logs[i].direction == SIMCOM_LOG_DIR_URC) {
            saw_urc = true;
            TEST_ASSERT_EQUAL_STRING("RDY", s_hal_ctx.logs[i].payload);
        } else if (s_hal_ctx.logs[i].direction == SIMCOM_LOG_DIR_RX) {
            saw_rx = true;
            TEST_ASSERT_EQUAL_STRING("OK", s_hal_ctx.logs[i].payload);
        } else {
            /* Other */
        }
    }

    TEST_ASSERT_TRUE(saw_fsm);
    TEST_ASSERT_TRUE(saw_tx);
    TEST_ASSERT_TRUE(saw_urc);
    TEST_ASSERT_TRUE(saw_rx);
}

static void test_severity_threshold_suppresses_lower_levels(void)
{
    simcom_runtime_cfg_t cfg = {
        .default_cmd_timeout_ms = 500U,
        .net_query_timeout_ms = 5000U,
        .max_cmd_retries = 1U,
        .tier1_sync_retries = 3U,
        .tier2_reset_timeout_ms = 10000U,
        .min_log_level = SIMCOM_LOG_LEVEL_WARNING
    };
    simcom_init_t init = {
        .hal = mock_hal_make_if(&s_hal_ctx),
        .profile = NULL,
        .runtime_cfg = &cfg,
        .log_fn = mock_hal_log_cb,
        .event_cb = NULL
    };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_Init(&s_modem, &init));
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_StartInit(&s_modem));

    /* Normal AT -> OK produces DEBUG and INFO logs, which MUST be suppressed */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);
    TEST_ASSERT_EQUAL_UINT(0U, s_hal_ctx.log_count);

    /* Now trigger a timeout on ATE0 -> WARNING log MUST be emitted */
    (void)Simcom_Poll(&s_modem);
    mock_hal_advance_tick(&s_hal_ctx, 500U);
    (void)Simcom_Poll(&s_modem);
    TEST_ASSERT_TRUE(s_hal_ctx.log_count > 0U);
    TEST_ASSERT_EQUAL(SIMCOM_LOG_LEVEL_WARNING, s_hal_ctx.logs[0].level);
}

static void test_null_logging_callback_executes_cleanly(void)
{
    simcom_init_t init = {
        .hal = mock_hal_make_if(&s_hal_ctx),
        .profile = NULL,
        .runtime_cfg = NULL,
        .log_fn = NULL,
        .event_cb = NULL
    };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_Init(&s_modem, &init));
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_StartInit(&s_modem));
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_Poll(&s_modem));
    TEST_ASSERT_EQUAL_UINT(0U, s_hal_ctx.log_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_logging_captures_tx_rx_urc_and_fsm_when_debug_enabled);
    RUN_TEST(test_severity_threshold_suppresses_lower_levels);
    RUN_TEST(test_null_logging_callback_executes_cleanly);
    return UNITY_END();
}
