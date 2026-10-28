/**
 * @file test_network_status.c
 * @brief Unit tests for User Story 3: Network registration queries & URCs,
 *        signal quality (CSQ), operator (COPS), and system info (CPSI) (T025).
 */

#include "unity.h"
#include "mock_simcom_hal.h"
#include <string.h>

static simcom_handle_t s_modem;
static mock_hal_ctx_t s_hal_ctx;

static void bring_modem_to_ready(void)
{
    (void)Simcom_StartInit(&s_modem);
    for (uint8_t step = 0U; step < 7U; ++step) {
        (void)Simcom_Poll(&s_modem);
        if (step == 2U) {
            mock_feed_str(&s_modem, "\r\n+CFUN: 1\r\n\r\nOK\r\n");
        } else {
            mock_feed_str(&s_modem, "\r\nOK\r\n");
        }
        (void)Simcom_Poll(&s_modem);
    }
    mock_hal_clear_tx(&s_hal_ctx);
    mock_hal_clear_events(&s_hal_ctx);
}

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
    bring_modem_to_ready();
}

void tearDown(void)
{
}

static void test_network_status_full_query_workflow(void)
{
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_RequestNetworkStatus(&s_modem));

    /* Step 0: AT+CREG? -> registered home (1) */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+CREG: 1,1\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 1: AT+CGREG? -> searching (2) */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+CGREG: 1,2\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 2: AT+CEREG? -> roaming (5) */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+CEREG: 1,5\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 3: AT+CSQ -> rssi=31 (-51 dBm), ber=99 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+CSQ: 31,99\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 4: AT+COPS? -> CHN-UNICOM, AcT=7 (LTE) */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+COPS: 0,0,\"CHN-UNICOM\",7\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 5: AT+CPSI? -> LTE,Online,... */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem,
                  "\r\n+CPSI: LTE,Online,460-01,0x230A,175499523,318,EUTRAN-BAND3,1650,5,0,21,67,255,19\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    simcom_net_status_t net;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_GetNetworkStatus(&s_modem, &net));
    TEST_ASSERT_EQUAL(SIMCOM_REG_HOME, net.cs_reg);
    TEST_ASSERT_EQUAL(SIMCOM_REG_SEARCHING, net.ps_reg);
    TEST_ASSERT_EQUAL(SIMCOM_REG_ROAMING, net.eps_reg);
    TEST_ASSERT_EQUAL_UINT8(31U, net.rssi_raw);
    TEST_ASSERT_EQUAL_INT16(-51, net.rssi_dbm);
    TEST_ASSERT_EQUAL_UINT8(99U, net.ber_raw);
    TEST_ASSERT_EQUAL(SIMCOM_ACT_LTE, net.act);
    TEST_ASSERT_EQUAL_STRING("CHN-UNICOM", net.operator_name);
    TEST_ASSERT_EQUAL_STRING("LTE", net.system_mode);

    TEST_ASSERT_EQUAL_UINT(1U, s_hal_ctx.event_count);
    TEST_ASSERT_EQUAL(SIMCOM_EVENT_NET_STATUS_READY, s_hal_ctx.events[0].event);
}

static void test_asynchronous_registration_urc_updates_cache_and_notifies_app(void)
{
    /* Feed unsolicited +CREG: 2 (searching) then +CEREG: 1 (home) while idle */
    mock_feed_str(&s_modem, "\r\n+CREG: 2\r\n\r\n+CEREG: 1,\"230A\",\"0A751B03\",7\r\n");
    (void)Simcom_Poll(&s_modem);

    simcom_net_status_t net;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_GetNetworkStatus(&s_modem, &net));
    TEST_ASSERT_EQUAL(SIMCOM_REG_SEARCHING, net.cs_reg);
    TEST_ASSERT_EQUAL(SIMCOM_REG_HOME, net.eps_reg);
    TEST_ASSERT_EQUAL_UINT(2U, s_hal_ctx.event_count);
    TEST_ASSERT_EQUAL(SIMCOM_EVENT_NET_REG_URC_CHANGED, s_hal_ctx.events[0].event);
    TEST_ASSERT_EQUAL(SIMCOM_EVENT_NET_REG_URC_CHANGED, s_hal_ctx.events[1].event);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_network_status_full_query_workflow);
    RUN_TEST(test_asynchronous_registration_urc_updates_cache_and_notifies_app);
    return UNITY_END();
}
