/**
 * @file test_init_recovery.c
 * @brief Unit and chaos tests for User Story 1: Initialization, partial TX writes,
 *        runtime config overrides, and Tier 1 -> Tier 2 -> Fault recovery ladder (T017).
 */

#include "unity.h"
#include "mock_simcom_hal.h"
#include <string.h>

static simcom_handle_t s_modem;
static mock_hal_ctx_t s_hal_ctx;

void setUp(void)
{
    mock_hal_init(&s_hal_ctx);
    simcom_init_t init = {
        .hal = mock_hal_make_if(&s_hal_ctx),
        .profile = NULL,
        .runtime_cfg = NULL,
        .log_fn = mock_hal_log_cb,
        .event_cb = mock_hal_event_cb
    };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_Init(&s_modem, &init));
}

void tearDown(void)
{
}

static void complete_normal_init(void)
{
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_StartInit(&s_modem));

    /* Step 0: AT */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 1: ATE0 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 2: AT+CFUN? -> return +CFUN: 0 so Step 3 (AT+CFUN=1) is exercised */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+CFUN: 0\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 3: AT+CFUN=1 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 4: AT+CMEE=2 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 5: AT+CREG=1 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 6: AT+CGREG=1 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 7: AT+CEREG=1 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);
}

static void test_init_happy_path_with_cfun_activation(void)
{
    complete_normal_init();

    TEST_ASSERT_EQUAL(SIMCOM_STATE_READY, Simcom_GetState(&s_modem));
    TEST_ASSERT_EQUAL(SIMCOM_FAULT_NONE, Simcom_GetFault(&s_modem));
    TEST_ASSERT_TRUE(mock_hal_tx_contains(&s_hal_ctx, "AT\r\n"));
    TEST_ASSERT_TRUE(mock_hal_tx_contains(&s_hal_ctx, "ATE0\r\n"));
    TEST_ASSERT_TRUE(mock_hal_tx_contains(&s_hal_ctx, "AT+CFUN?\r\n"));
    TEST_ASSERT_TRUE(mock_hal_tx_contains(&s_hal_ctx, "AT+CFUN=1\r\n"));
    TEST_ASSERT_TRUE(mock_hal_tx_contains(&s_hal_ctx, "AT+CMEE=2\r\n"));
    TEST_ASSERT_TRUE(mock_hal_tx_contains(&s_hal_ctx, "AT+CREG=1\r\n"));
    TEST_ASSERT_TRUE(mock_hal_tx_contains(&s_hal_ctx, "AT+CGREG=1\r\n"));
    TEST_ASSERT_TRUE(mock_hal_tx_contains(&s_hal_ctx, "AT+CEREG=1\r\n"));

    TEST_ASSERT_EQUAL_UINT(1U, s_hal_ctx.event_count);
    TEST_ASSERT_EQUAL(SIMCOM_EVENT_INIT_COMPLETE, s_hal_ctx.events[0].event);
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, s_hal_ctx.events[0].status);
}

static void test_partial_uart_tx_streaming_across_polls(void)
{
    /* Limit UART TX to 2 bytes per poll call */
    s_hal_ctx.tx_max_chunk = 2U;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_StartInit(&s_modem));

    /* "AT\r\n" is 4 bytes -> requires 2 poll calls to finish TX */
    (void)Simcom_Poll(&s_modem);
    TEST_ASSERT_EQUAL_UINT(2U, s_hal_ctx.tx_len);
    (void)Simcom_Poll(&s_modem);
    TEST_ASSERT_EQUAL_UINT(4U, s_hal_ctx.tx_len);
    TEST_ASSERT_EQUAL_STRING("AT\r\n", (const char *)s_hal_ctx.tx_buf);
}

static void test_unresponsive_modem_escalates_tier1_tier2_to_fault(void)
{
    simcom_runtime_cfg_t custom_cfg = {
        .default_cmd_timeout_ms = 500U,
        .net_query_timeout_ms = 2000U,
        .max_cmd_retries = 1U,
        .tier1_sync_retries = 2U,
        .tier2_reset_timeout_ms = 3000U,
        .min_log_level = SIMCOM_LOG_LEVEL_DEBUG
    };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_SetConfig(&s_modem, &custom_cfg));
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_StartInit(&s_modem));

    /* Initial AT TX + 1 command retry */
    (void)Simcom_Poll(&s_modem); /* Sends initial AT */
    mock_hal_advance_tick(&s_hal_ctx, 500U);
    (void)Simcom_Poll(&s_modem); /* Timeout -> triggers retry 1 */
    (void)Simcom_Poll(&s_modem); /* Sends retry 1 AT */

    mock_hal_advance_tick(&s_hal_ctx, 500U);
    (void)Simcom_Poll(&s_modem); /* Retries exhausted -> enters SIMCOM_STATE_RECOVERY_TIER1 */
    TEST_ASSERT_EQUAL(SIMCOM_STATE_RECOVERY_TIER1, Simcom_GetState(&s_modem));

    /* Tier 1 attempt 1 */
    (void)Simcom_Poll(&s_modem); /* Sends Tier 1 probe 1 */
    mock_hal_advance_tick(&s_hal_ctx, 500U);
    (void)Simcom_Poll(&s_modem); /* Timeout -> queues Tier 1 probe 2 */

    /* Tier 1 attempt 2 */
    (void)Simcom_Poll(&s_modem); /* Sends Tier 1 probe 2 */
    mock_hal_advance_tick(&s_hal_ctx, 500U);
    (void)Simcom_Poll(&s_modem); /* Tier 1 exhausted -> enters SIMCOM_STATE_RECOVERY_TIER2 */
    TEST_ASSERT_EQUAL(SIMCOM_STATE_RECOVERY_TIER2, Simcom_GetState(&s_modem));

    /* Tier 2 transmits AT+CRESET */
    (void)Simcom_Poll(&s_modem);
    TEST_ASSERT_TRUE(mock_hal_tx_contains(&s_hal_ctx, "AT+CRESET\r\n"));

    /* Advance past tier2_reset_timeout_ms without RDY */
    mock_hal_advance_tick(&s_hal_ctx, 3000U);
    (void)Simcom_Poll(&s_modem);

    TEST_ASSERT_EQUAL(SIMCOM_STATE_FAULT, Simcom_GetState(&s_modem));
    TEST_ASSERT_EQUAL(SIMCOM_FAULT_MODEM_UNRESPONSIVE, Simcom_GetFault(&s_modem));
    TEST_ASSERT_EQUAL_UINT(1U, s_hal_ctx.event_count);
    TEST_ASSERT_EQUAL(SIMCOM_EVENT_FAULT, s_hal_ctx.events[0].event);
}

static void test_tier2_logical_reset_recovers_on_rdy_urc(void)
{
    simcom_runtime_cfg_t custom_cfg = {
        .default_cmd_timeout_ms = 500U,
        .net_query_timeout_ms = 2000U,
        .max_cmd_retries = 0U,
        .tier1_sync_retries = 1U,
        .tier2_reset_timeout_ms = 5000U,
        .min_log_level = SIMCOM_LOG_LEVEL_INFO
    };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_SetConfig(&s_modem, &custom_cfg));
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_StartInit(&s_modem));

    /* Initial AT times out (0 retries) -> Tier 1 */
    (void)Simcom_Poll(&s_modem);
    mock_hal_advance_tick(&s_hal_ctx, 500U);
    (void)Simcom_Poll(&s_modem);
    TEST_ASSERT_EQUAL(SIMCOM_STATE_RECOVERY_TIER1, Simcom_GetState(&s_modem));

    /* Tier 1 probe times out -> Tier 2 */
    (void)Simcom_Poll(&s_modem);
    mock_hal_advance_tick(&s_hal_ctx, 500U);
    (void)Simcom_Poll(&s_modem);
    TEST_ASSERT_EQUAL(SIMCOM_STATE_RECOVERY_TIER2, Simcom_GetState(&s_modem));

    /* Tier 2 sends AT+CRESET; modem responds OK and then emits RDY */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nOK\r\n\r\nRDY\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Should automatically transition back to INITIALIZING! */
    TEST_ASSERT_EQUAL(SIMCOM_STATE_INITIALIZING, Simcom_GetState(&s_modem));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_happy_path_with_cfun_activation);
    RUN_TEST(test_partial_uart_tx_streaming_across_polls);
    RUN_TEST(test_unresponsive_modem_escalates_tier1_tier2_to_fault);
    RUN_TEST(test_tier2_logical_reset_recovers_on_rdy_urc);
    return UNITY_END();
}
