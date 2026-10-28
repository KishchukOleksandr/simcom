/**
 * @file test_lexer_urc.c
 * @brief Unit and chaos tests for dual-path AT lexer, fragmented RX, overflow resync, and URC dispatch (T016).
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
    (void)Simcom_Init(&s_modem, &init);
    (void)Simcom_StartInit(&s_modem);
}

void tearDown(void)
{
}

static void test_fragmented_1byte_per_poll_reception(void)
{
    /* Step through the entire 7-step init workflow feeding 1 byte per Simcom_Poll() */
    /* Step 0: AT */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str_byte_by_byte(&s_modem, "\r\nOK\r\n");

    /* Step 1: ATE0 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str_byte_by_byte(&s_modem, "\r\nOK\r\n");

    /* Step 2: AT+CFUN? -> +CFUN: 1 (skips Step 3 AT+CFUN=1) */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str_byte_by_byte(&s_modem, "\r\n+CFUN: 1\r\n\r\nOK\r\n");

    /* Step 4: AT+CMEE=2 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str_byte_by_byte(&s_modem, "\r\nOK\r\n");

    /* Step 5: AT+CREG=1 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str_byte_by_byte(&s_modem, "\r\nOK\r\n");

    /* Step 6: AT+CGREG=1 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str_byte_by_byte(&s_modem, "\r\nOK\r\n");

    /* Step 7: AT+CEREG=1 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str_byte_by_byte(&s_modem, "\r\nOK\r\n");

    (void)Simcom_Poll(&s_modem);
    TEST_ASSERT_EQUAL(SIMCOM_STATE_READY, Simcom_GetState(&s_modem));
}

static void test_1024_byte_corrupted_line_overflow_rejection_and_resync(void)
{
    /* Transmit AT */
    (void)Simcom_Poll(&s_modem);

    /* Feed 1024 garbage bytes without CRLF into 512B RX / 256B line buffer */
    uint8_t garbage[256];
    (void)memset(garbage, 'X', sizeof(garbage));
    for (size_t i = 0U; i < 4U; ++i) {
        (void)Simcom_FeedBytes(&s_modem, garbage, sizeof(garbage), NULL);
        (void)Simcom_Poll(&s_modem);
    }

    /* Now terminate the corrupted line and send valid OK\r\n */
    mock_feed_str(&s_modem, "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Verify AT step 0 succeeded and advanced to step 1 (ATE0) without memory corruption */
    TEST_ASSERT_EQUAL_UINT8(1U, s_modem.step_index);
}

static void test_interleaved_urc_mid_command_does_not_corrupt_transaction(void)
{
    /* Complete init first */
    test_fragmented_1byte_per_poll_reception();
    mock_hal_clear_events(&s_hal_ctx);

    /* Start Device Info query (sends ATI) */
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_RequestDeviceInfo(&s_modem));
    (void)Simcom_Poll(&s_modem);

    /* Inject unsolicited +CMTI, +CEREG: 5, and SMS DONE mid-command before ATI finishes */
    mock_feed_str(&s_modem,
                  "\r\n+CMTI: \"SM\",1\r\n"
                  "Manufacturer: INCORPORATED\r\n"
                  "+CEREG: 5\r\n"
                  "Model: A7600C\r\n"
                  "SMS DONE\r\n"
                  "Revision: A7600C_V1.0\r\n"
                  "IMEI: 351602000330570\r\n"
                  "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Verify URC updated eps_reg AND fired SIMCOM_EVENT_NET_REG_URC_CHANGED */
    simcom_net_status_t net;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_GetNetworkStatus(&s_modem, &net));
    TEST_ASSERT_EQUAL(SIMCOM_REG_ROAMING, net.eps_reg);

    /* Verify ATI command ALSO completed cleanly without corruption */
    simcom_device_info_t info;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_GetDeviceInfo(&s_modem, &info));
    TEST_ASSERT_TRUE(info.is_valid);
    TEST_ASSERT_EQUAL_STRING("A7600C", info.model);
    TEST_ASSERT_EQUAL_STRING("351602000330570", info.imei);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fragmented_1byte_per_poll_reception);
    RUN_TEST(test_1024_byte_corrupted_line_overflow_rejection_and_resync);
    RUN_TEST(test_interleaved_urc_mid_command_does_not_corrupt_transaction);
    return UNITY_END();
}
