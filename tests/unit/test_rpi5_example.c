/**
 * @file test_rpi5_example.c
 * @brief Unit tests for Raspberry Pi 5 & Waveshare A7670E HAT application coordinator (T008, T009, T013).
 */

#include "unity.h"
#include "rpi5_app.h"
#include <string.h>

typedef struct {
    uint32_t tick_ms;
    bool pwrkey_high;
    bool pwrkey_fail_next;
    uint32_t pwrkey_toggle_count;
    char tx_log[2048];
    size_t tx_len;
    char console_log[8192];
    size_t console_len;
} mock_rpi5_platform_t;

static rpi5_app_t s_app;
static mock_rpi5_platform_t s_plat_ctx;
static rpi5_platform_if_t s_plat_if;
static rpi5_example_cfg_t s_cfg;

static size_t mock_plat_uart_tx(const uint8_t *data, size_t len, void *ctx)
{
    mock_rpi5_platform_t * const m = (mock_rpi5_platform_t *)ctx;
    for (size_t i = 0U; (i < len) && ((m->tx_len + 1U) < sizeof(m->tx_log)); i++) {
        m->tx_log[m->tx_len] = (char)data[i];
        m->tx_len++;
    }
    m->tx_log[m->tx_len] = '\0';
    return len;
}

static uint32_t mock_plat_get_tick_ms(void *ctx)
{
    const mock_rpi5_platform_t * const m = (const mock_rpi5_platform_t *)ctx;
    return m->tick_ms;
}

static bool mock_plat_gpio_set_pwrkey(bool high, void *ctx)
{
    mock_rpi5_platform_t * const m = (mock_rpi5_platform_t *)ctx;
    if (m->pwrkey_fail_next) {
        return false;
    }
    m->pwrkey_high = high;
    m->pwrkey_toggle_count++;
    return true;
}

static void mock_plat_console_print(const char *text, void *ctx)
{
    mock_rpi5_platform_t * const m = (mock_rpi5_platform_t *)ctx;
    const size_t n = strlen(text);
    for (size_t i = 0U; (i < n) && ((m->console_len + 1U) < sizeof(m->console_log)); i++) {
        m->console_log[m->console_len] = text[i];
        m->console_len++;
    }
    m->console_log[m->console_len] = '\0';
}

static void feed_str(const char * const s)
{
    (void)Rpi5App_FeedBytes(&s_app, (const uint8_t *)s, strlen(s));
}

static void step_pwrkey_and_init_to_dev_info(void)
{
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Rpi5App_Start(&s_app));
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_PWRKEY_ASSERT, s_app.state);
    TEST_ASSERT_TRUE(s_plat_ctx.pwrkey_high);
    TEST_ASSERT_EQUAL_UINT32(1U, s_plat_ctx.pwrkey_toggle_count);

    /* Advance 599 ms -> still holding GPIO 4 HIGH */
    s_plat_ctx.tick_ms += 599U;
    Rpi5App_Poll(&s_app);
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_PWRKEY_ASSERT, s_app.state);
    TEST_ASSERT_TRUE(s_plat_ctx.pwrkey_high);

    /* Advance +1 ms (600 ms total) -> releases GPIO 4 LOW and enters boot wait */
    s_plat_ctx.tick_ms += 1U;
    Rpi5App_Poll(&s_app);
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_PWRKEY_WAIT_BOOT, s_app.state);
    TEST_ASSERT_FALSE(s_plat_ctx.pwrkey_high);
    TEST_ASSERT_EQUAL_UINT32(2U, s_plat_ctx.pwrkey_toggle_count);

    /* Advance 3500 ms boot wait -> starts Simcom_StartInit() */
    s_plat_ctx.tick_ms += 3500U;
    Rpi5App_Poll(&s_app);
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_MODEM_INIT, s_app.state);

    /* Complete 7-step init: AT, ATE0, AT+CFUN? (+CFUN: 1), AT+CMEE=2, AT+CREG=1, AT+CGREG=1, AT+CEREG=1 */
    Rpi5App_Poll(&s_app);
    feed_str("\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    Rpi5App_Poll(&s_app);
    feed_str("\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CFUN: 1\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    Rpi5App_Poll(&s_app);
    feed_str("\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    Rpi5App_Poll(&s_app);
    feed_str("\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    Rpi5App_Poll(&s_app);
    feed_str("\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    Rpi5App_Poll(&s_app);
    feed_str("\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    TEST_ASSERT_EQUAL(RPI5_APP_STATE_QUERY_DEV_INFO, s_app.state);

    /* Complete ATI device info query */
    Rpi5App_Poll(&s_app);
    feed_str("\r\nManufacturer: SIMCOM_Ltd\r\n"
             "Model: A7670E\r\n"
             "Revision: A7670E-LNXY-UBL\r\n"
             "IMEI: 867530912345678\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    TEST_ASSERT_EQUAL(RPI5_APP_STATE_QUERY_SIM, s_app.state);
}

static void complete_network_queries(void)
{
    /* AT+CREG? */
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CREG: 1,1\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    /* AT+CGREG? */
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CGREG: 1,1\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    /* AT+CEREG? */
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CEREG: 1,1\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    /* AT+CSQ */
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CSQ: 24,0\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    /* AT+COPS? */
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+COPS: 0,0,\"Vodafone\",7\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    /* AT+CPSI? */
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CPSI: LTE,Online,262-02,0x1A2B,123456,100,EUTRAN-BAND20\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);
}

void setUp(void)
{
    (void)memset(&s_plat_ctx, 0, sizeof(s_plat_ctx));
    s_plat_if.uart_tx = mock_plat_uart_tx;
    s_plat_if.get_tick_ms = mock_plat_get_tick_ms;
    s_plat_if.gpio_set_pwrkey = mock_plat_gpio_set_pwrkey;
    s_plat_if.console_print = mock_plat_console_print;
    s_plat_if.ctx = &s_plat_ctx;

    Rpi5App_DefaultConfig(&s_cfg);
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Rpi5App_Init(&s_app, &s_cfg, &s_plat_if));
}

void tearDown(void)
{
}

/* ========================================================================== */
/* User Story 1 Tests (T009)                                                  */
/* ========================================================================== */

static void test_us1_end_to_end_pwrkey_inspection_and_monitor_mode(void)
{
    step_pwrkey_and_init_to_dev_info();

    /* Complete SIM status query (READY + ICCID) */
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CPIN: READY\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    Rpi5App_Poll(&s_app);
    feed_str("\r\n+ICCID: 89860012345678901234\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    TEST_ASSERT_EQUAL(RPI5_APP_STATE_QUERY_NET, s_app.state);

    complete_network_queries();

    /* Default mode transitions to continuous MONITOR and prints the full report */
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_MONITOR, s_app.state);
    TEST_ASSERT_TRUE(s_app.initial_report_printed);
    TEST_ASSERT_FALSE(Rpi5App_IsFinished(&s_app));

    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "A7670E"));
    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "867530912345678"));
    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "89860012345678901234"));
    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "Vodafone"));
    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "-65 dBm"));

    /* Feed live +CEREG URC during continuous monitoring */
    feed_str("\r\n+CEREG: 5\r\n");
    Rpi5App_Poll(&s_app);
    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "EPS=REGISTERED_ROAMING"));

    /* Graceful stop via Rpi5App_RequestStop (SIGINT/SIGTERM) */
    Rpi5App_RequestStop(&s_app);
    TEST_ASSERT_TRUE(Rpi5App_IsFinished(&s_app));
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_DONE, s_app.state);
    TEST_ASSERT_EQUAL_INT32(0, s_app.exit_code);
}

static void test_us1_unresponsive_modem_fault_reports_jumper_hint(void)
{
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Rpi5App_Start(&s_app));
    s_plat_ctx.tick_ms += 600U;
    Rpi5App_Poll(&s_app);
    s_plat_ctx.tick_ms += 3500U;
    Rpi5App_Poll(&s_app);

    /* Never feed any UART responses; advance clock through retries + Tier 1 + Tier 2 */
    for (uint32_t i = 0U; (i < 40U) && !Rpi5App_IsFinished(&s_app); i++) {
        Rpi5App_Poll(&s_app);
        s_plat_ctx.tick_ms += 11000U;
        Rpi5App_Poll(&s_app);
    }

    TEST_ASSERT_TRUE(Rpi5App_IsFinished(&s_app));
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_ERROR, s_app.state);
    TEST_ASSERT_EQUAL_INT32(2, s_app.exit_code);
    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "MODEM_UNRESPONSIVE"));
    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "PWR->D4"));
}

/* ========================================================================== */
/* User Story 2 Tests (T013)                                                  */
/* ========================================================================== */

static void test_us2_cli_argument_parsing_and_validation(void)
{
    rpi5_example_cfg_t cfg;
    const char * const valid_argv[] = {
        "simcom_rpi5_a7670e",
        "--port", "/dev/ttyUSB2",
        "--baud", "230400",
        "--gpio-chip", "/dev/gpiochip0",
        "--pwrkey-pin", "17",
        "--pin", "1234",
        "--verbose",
        "--once"
    };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Rpi5App_ParseArgs(13, valid_argv, &cfg));
    TEST_ASSERT_EQUAL_STRING("/dev/ttyUSB2", cfg.serial_port);
    TEST_ASSERT_EQUAL_UINT32(230400U, cfg.baud_rate);
    TEST_ASSERT_EQUAL_STRING("/dev/gpiochip0", cfg.gpio_chip);
    TEST_ASSERT_EQUAL_UINT32(17U, cfg.pwrkey_gpio_pin);
    TEST_ASSERT_EQUAL_STRING("1234", cfg.sim_pin);
    TEST_ASSERT_EQUAL(SIMCOM_LOG_LEVEL_DEBUG, cfg.log_level);
    TEST_ASSERT_TRUE(cfg.run_once);
    TEST_ASSERT_FALSE(cfg.show_help);

    /* --help flag */
    const char * const help_argv[] = { "simcom_rpi5_a7670e", "-h" };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Rpi5App_ParseArgs(2, help_argv, &cfg));
    TEST_ASSERT_TRUE(cfg.show_help);

    /* Reject invalid baud rate */
    const char * const bad_baud[] = { "simcom_rpi5_a7670e", "--baud", "12345" };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_INVALID_ARG, Rpi5App_ParseArgs(3, bad_baud, &cfg));

    /* Reject malformed PIN (non-digit or too short) */
    const char * const bad_pin[] = { "simcom_rpi5_a7670e", "--pin", "12a4" };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_INVALID_ARG, Rpi5App_ParseArgs(3, bad_pin, &cfg));

    /* Reject unknown flag */
    const char * const bad_flag[] = { "simcom_rpi5_a7670e", "--unknown" };
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_INVALID_ARG, Rpi5App_ParseArgs(2, bad_flag, &cfg));
}

static void test_us2_sim_pin_unlock_and_once_flag(void)
{
    s_cfg.sim_pin = "4321";
    s_cfg.run_once = true;
    s_cfg.log_level = SIMCOM_LOG_LEVEL_DEBUG;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Rpi5App_Init(&s_app, &s_cfg, &s_plat_if));

    step_pwrkey_and_init_to_dev_info();

    /* First AT+CPIN? returns SIM PIN */
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CPIN: SIM PIN\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    /* Coordinator must transition to RPI5_APP_STATE_SUBMIT_PIN */
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_SUBMIT_PIN, s_app.state);
    TEST_ASSERT_TRUE(s_app.pin_attempted);

    /* Step 0 of PIN unlock: AT+CPIN="4321" -> OK */
    Rpi5App_Poll(&s_app);
    feed_str("\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    /* Step 1 of PIN unlock: internal AT+CPIN? refresh -> READY */
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CPIN: READY\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    /* Coordinator re-queries SIM status to fetch ICCID */
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_QUERY_SIM, s_app.state);
    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CPIN: READY\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    Rpi5App_Poll(&s_app);
    feed_str("\r\n+ICCID: 89445011223344556677\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    TEST_ASSERT_EQUAL(RPI5_APP_STATE_QUERY_NET, s_app.state);
    complete_network_queries();

    /* Because --once was enabled, app must finish immediately in DONE state */
    TEST_ASSERT_TRUE(Rpi5App_IsFinished(&s_app));
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_DONE, s_app.state);
    TEST_ASSERT_EQUAL_INT32(0, s_app.exit_code);
    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "89445011223344556677"));
}

static void test_us2_sim_pin_required_without_pin_skips_safely(void)
{
    s_cfg.sim_pin = NULL;
    s_cfg.run_once = true;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Rpi5App_Init(&s_app, &s_cfg, &s_plat_if));

    step_pwrkey_and_init_to_dev_info();

    Rpi5App_Poll(&s_app);
    feed_str("\r\n+CPIN: SIM PIN\r\n\r\nOK\r\n");
    Rpi5App_Poll(&s_app);

    /* Must NOT enter SUBMIT_PIN; must warn and move to QUERY_NET */
    TEST_ASSERT_FALSE(s_app.pin_attempted);
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_QUERY_NET, s_app.state);
    TEST_ASSERT_NOT_NULL(strstr(s_plat_ctx.console_log, "Skipping PIN submission"));

    complete_network_queries();
    TEST_ASSERT_TRUE(Rpi5App_IsFinished(&s_app));
    TEST_ASSERT_EQUAL(RPI5_APP_STATE_DONE, s_app.state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_us1_end_to_end_pwrkey_inspection_and_monitor_mode);
    RUN_TEST(test_us1_unresponsive_modem_fault_reports_jumper_hint);
    RUN_TEST(test_us2_cli_argument_parsing_and_validation);
    RUN_TEST(test_us2_sim_pin_unlock_and_once_flag);
    RUN_TEST(test_us2_sim_pin_required_without_pin_skips_safely);
    return UNITY_END();
}
