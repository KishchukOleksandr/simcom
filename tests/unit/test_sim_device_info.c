/**
 * @file test_sim_device_info.c
 * @brief Unit tests for User Story 2: Hardware identity (ATI/CGMM/CGMR/CGSN),
 *        SIM card status, ICCID retrieval, and single-shot PIN entry (T021).
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

static void test_device_info_from_ati_single_block(void)
{
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_RequestDeviceInfo(&s_modem));
    (void)Simcom_Poll(&s_modem);

    mock_feed_str(&s_modem,
                  "\r\nManufacturer: INCORPORATED\r\n"
                  "Model: A7600C\r\n"
                  "Revision: A7600C_V1.0\r\n"
                  "IMEI: 351602000330570\r\n"
                  "+GCAP: +CGSM,+FCLASS,+DS\r\n"
                  "\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    simcom_device_info_t info;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_GetDeviceInfo(&s_modem, &info));
    TEST_ASSERT_TRUE(info.is_valid);
    TEST_ASSERT_EQUAL_STRING("INCORPORATED", info.manufacturer);
    TEST_ASSERT_EQUAL_STRING("A7600C", info.model);
    TEST_ASSERT_EQUAL_STRING("A7600C_V1.0", info.revision);
    TEST_ASSERT_EQUAL_STRING("351602000330570", info.imei);
    TEST_ASSERT_EQUAL(SIMCOM_STATE_READY, Simcom_GetState(&s_modem));
}

static void test_device_info_fallback_to_cgmm_cgmr_cgsn(void)
{
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_RequestDeviceInfo(&s_modem));

    /* Step 0: ATI returns only Manufacturer */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nManufacturer: SIMCOM_Ltd\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 1: AT+CGMM fallback */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nA7670E\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 2: AT+CGMR fallback */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\nA7670E_V2.04\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 3: AT+CGSN fallback */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n861234567890123\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    simcom_device_info_t info;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_GetDeviceInfo(&s_modem, &info));
    TEST_ASSERT_EQUAL_STRING("A7670E", info.model);
    TEST_ASSERT_EQUAL_STRING("A7670E_V2.04", info.revision);
    TEST_ASSERT_EQUAL_STRING("861234567890123", info.imei);
}

static void test_sim_ready_queries_iccid_automatically(void)
{
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_RequestSimStatus(&s_modem));

    /* Step 0: AT+CPIN? -> +CPIN: READY */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+CPIN: READY\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    /* Step 1: AT+CICCID -> +ICCID: 89860318760238610932 */
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+ICCID: 89860318760238610932\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    simcom_sim_info_t sim;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_GetSimInfo(&s_modem, &sim));
    TEST_ASSERT_EQUAL(SIMCOM_SIM_STATE_READY, sim.state);
    TEST_ASSERT_TRUE(sim.is_iccid_valid);
    TEST_ASSERT_EQUAL_STRING("89860318760238610932", sim.iccid);
}

static void test_missing_sim_reports_absent_and_leaves_modem_ready(void)
{
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_RequestSimStatus(&s_modem));
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+CME ERROR: 10\r\n");
    (void)Simcom_Poll(&s_modem);

    simcom_sim_info_t sim;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_GetSimInfo(&s_modem, &sim));
    TEST_ASSERT_EQUAL(SIMCOM_SIM_STATE_ABSENT, sim.state);
    TEST_ASSERT_EQUAL_UINT16(10U, sim.last_cme_error);
    TEST_ASSERT_FALSE(sim.is_iccid_valid);
    TEST_ASSERT_EQUAL(SIMCOM_STATE_READY, Simcom_GetState(&s_modem));
}

static void test_pin_locked_sim_and_single_shot_pin_never_retries(void)
{
    /* 1. Detect SIM PIN locked state */
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_RequestSimStatus(&s_modem));
    (void)Simcom_Poll(&s_modem);
    mock_feed_str(&s_modem, "\r\n+CPIN: SIM PIN\r\n\r\nOK\r\n");
    (void)Simcom_Poll(&s_modem);

    simcom_sim_info_t sim;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_GetSimInfo(&s_modem, &sim));
    TEST_ASSERT_EQUAL(SIMCOM_SIM_STATE_PIN_REQUIRED, sim.state);

    /* 2. Reject invalid PIN formats */
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_INVALID_ARG, Simcom_SubmitSimPin(&s_modem, "12"));
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_INVALID_ARG, Simcom_SubmitSimPin(&s_modem, "12A4"));

    /* 3. Submit valid 4-digit PIN -> verify pin_scratch is immediately wiped and never retried on error */
    mock_hal_clear_tx(&s_hal_ctx);
    mock_hal_clear_events(&s_hal_ctx);
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_PENDING, Simcom_SubmitSimPin(&s_modem, "1234"));

    /* pin_scratch MUST be zeroed immediately upon queuing */
    for (size_t i = 0U; i < sizeof(s_modem.pin_scratch); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0U, (uint8_t)s_modem.pin_scratch[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(0U, s_modem.cmd_retries_left);

    (void)Simcom_Poll(&s_modem);
    TEST_ASSERT_EQUAL_STRING("AT+CPIN=\"1234\"\r\n", (const char *)s_hal_ctx.tx_buf);

    /* Simulate incorrect password (+CME ERROR: 16) */
    mock_feed_str(&s_modem, "\r\n+CME ERROR: 16\r\n");
    (void)Simcom_Poll(&s_modem);

    TEST_ASSERT_EQUAL(SIMCOM_STATE_READY, Simcom_GetState(&s_modem));
    TEST_ASSERT_EQUAL_UINT(1U, s_hal_ctx.event_count);
    TEST_ASSERT_EQUAL(SIMCOM_EVENT_SIM_PIN_RESULT, s_hal_ctx.events[0].event);
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_ERROR, s_hal_ctx.events[0].status);

    /* Ensure AT+CPIN="1234" was transmitted ONLY ONCE */
    TEST_ASSERT_EQUAL_STRING("AT+CPIN=\"1234\"\r\n", (const char *)s_hal_ctx.tx_buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_device_info_from_ati_single_block);
    RUN_TEST(test_device_info_fallback_to_cgmm_cgmr_cgsn);
    RUN_TEST(test_sim_ready_queries_iccid_automatically);
    RUN_TEST(test_missing_sim_reports_absent_and_leaves_modem_ready);
    RUN_TEST(test_pin_locked_sim_and_single_shot_pin_never_retries);
    return UNITY_END();
}
