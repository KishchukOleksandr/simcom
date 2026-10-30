/**
 * @file rpi5_app.c
 * @brief Portable non-blocking workflow coordinator, CLI parser, and status formatter
 *        for the Raspberry Pi 5 & Waveshare A7670E Cat-1/GNSS HAT example (002-rpi5-a7670e-example).
 *
 * Compliance:
 *   - Principle 1: Zero dynamic memory allocation (static buffers only).
 *   - Principle 2: Non-blocking FSM stepped via Rpi5App_Poll() with unsigned tick subtraction.
 *   - Principle 5: Defensive bounds-checked string/number formatting (no sprintf/sscanf/strcpy/atoi).
 *   - Section 3: ISO C11 fixed-width integer types.
 */

#include "rpi5_app.h"
#include <string.h>

/* ========================================================================== */
/* Defensive Zero-Heap String & Number Helpers (T006, Principle 5)            */
/* ========================================================================== */

static size_t cstr_len_bounded(const char * const s, size_t max_len)
{
    if (s == NULL) {
        return 0U;
    }
    size_t i = 0U;
    while ((i < max_len) && (s[i] != '\0')) {
        i++;
    }
    return i;
}

static bool cstr_equals(const char * const a, const char * const b)
{
    if ((a == NULL) || (b == NULL)) {
        return false;
    }
    size_t i = 0U;
    while ((a[i] != '\0') && (b[i] != '\0')) {
        if (a[i] != b[i]) {
            return false;
        }
        i++;
    }
    return (a[i] == b[i]);
}

static void buf_append_str(char * const dst, size_t cap, size_t * const io_len, const char * const src)
{
    if ((dst == NULL) || (io_len == NULL) || (src == NULL) || (cap == 0U)) {
        return;
    }
    size_t pos = *io_len;
    size_t i = 0U;
    while ((src[i] != '\0') && ((pos + 1U) < cap)) {
        dst[pos] = src[i];
        pos++;
        i++;
    }
    dst[pos] = '\0';
    *io_len = pos;
}

static void buf_append_bytes(
    char * const dst,
    size_t cap,
    size_t * const io_len,
    const uint8_t * const src,
    size_t src_len)
{
    if ((dst == NULL) || (io_len == NULL) || (src == NULL) || (cap == 0U)) {
        return;
    }
    size_t pos = *io_len;
    for (size_t i = 0U; (i < src_len) && ((pos + 1U) < cap); i++) {
        const uint8_t ch = src[i];
        if ((ch >= 0x20U) && (ch <= 0x7EU)) {
            dst[pos] = (char)ch;
            pos++;
        }
    }
    dst[pos] = '\0';
    *io_len = pos;
}

static void buf_append_u32(char * const dst, size_t cap, size_t * const io_len, uint32_t val)
{
    char tmp[12];
    size_t count = 0U;
    if (val == 0U) {
        tmp[count] = '0';
        count++;
    } else {
        uint32_t v = val;
        while (v > 0U) {
            tmp[count] = (char)('0' + (v % 10U));
            count++;
            v /= 10U;
        }
    }
    size_t pos = *io_len;
    while ((count > 0U) && ((pos + 1U) < cap)) {
        count--;
        dst[pos] = tmp[count];
        pos++;
    }
    dst[pos] = '\0';
    *io_len = pos;
}

static void buf_append_i32(char * const dst, size_t cap, size_t * const io_len, int32_t val)
{
    if (val < 0) {
        buf_append_str(dst, cap, io_len, "-");
        const uint32_t mag = (uint32_t)(-(val + 1)) + 1U;
        buf_append_u32(dst, cap, io_len, mag);
    } else {
        buf_append_u32(dst, cap, io_len, (uint32_t)val);
    }
}

static bool parse_u32_strict(const char * const s, uint32_t * const out_val)
{
    if ((s == NULL) || (out_val == NULL) || (s[0] == '\0')) {
        return false;
    }
    uint64_t acc = 0U;
    size_t i = 0U;
    while (s[i] != '\0') {
        if ((s[i] < '0') || (s[i] > '9')) {
            return false;
        }
        acc = (acc * 10U) + (uint64_t)(uint8_t)(s[i] - '0');
        if (acc > 0xFFFFFFFFULL) {
            return false;
        }
        i++;
    }
    *out_val = (uint32_t)acc;
    return true;
}

static bool is_valid_baud_rate(uint32_t baud)
{
    return (baud == 9600U) ||
           (baud == 19200U) ||
           (baud == 38400U) ||
           (baud == 57600U) ||
           (baud == 115200U) ||
           (baud == 230400U) ||
           (baud == 460800U) ||
           (baud == 921600U);
}

static bool is_valid_pin_string(const char * const pin)
{
    if (pin == NULL) {
        return false;
    }
    const size_t len = cstr_len_bounded(pin, SIMCOM_CFG_PIN_MAX_LEN);
    if ((len < 4U) || (len > 8U)) {
        return false;
    }
    for (size_t i = 0U; i < len; i++) {
        if ((pin[i] < '0') || (pin[i] > '9')) {
            return false;
        }
    }
    return true;
}

/* ========================================================================== */
/* Enum-to-String Mappers (T006)                                              */
/* ========================================================================== */

static const char *sim_state_to_str(simcom_sim_state_t state)
{
    switch (state) {
    case SIMCOM_SIM_STATE_READY:        return "READY";
    case SIMCOM_SIM_STATE_PIN_REQUIRED: return "PIN_REQUIRED";
    case SIMCOM_SIM_STATE_PUK_REQUIRED: return "PUK_REQUIRED";
    case SIMCOM_SIM_STATE_ABSENT:       return "ABSENT";
    case SIMCOM_SIM_STATE_FAULTY:       return "FAULTY";
    case SIMCOM_SIM_STATE_UNKNOWN:
    default:                            return "UNKNOWN";
    }
}

static const char *reg_state_to_str(simcom_reg_state_t state)
{
    switch (state) {
    case SIMCOM_REG_NOT_REGISTERED: return "NOT_REGISTERED";
    case SIMCOM_REG_HOME:           return "REGISTERED_HOME";
    case SIMCOM_REG_SEARCHING:      return "SEARCHING";
    case SIMCOM_REG_DENIED:         return "DENIED";
    case SIMCOM_REG_ROAMING:        return "REGISTERED_ROAMING";
    case SIMCOM_REG_UNKNOWN:
    default:                        return "UNKNOWN";
    }
}

static const char *act_to_str(simcom_act_t act)
{
    switch (act) {
    case SIMCOM_ACT_GSM:     return "GSM";
    case SIMCOM_ACT_UTRAN:   return "UTRAN (3G)";
    case SIMCOM_ACT_LTE:     return "LTE (Cat-1)";
    case SIMCOM_ACT_UNKNOWN:
    default:                 return "UNKNOWN";
    }
}

static const char *fault_to_str(simcom_fault_t fault)
{
    switch (fault) {
    case SIMCOM_FAULT_NONE:               return "NONE";
    case SIMCOM_FAULT_MODEM_UNRESPONSIVE: return "MODEM_UNRESPONSIVE";
    case SIMCOM_FAULT_SIM_ERROR:          return "SIM_ERROR";
    case SIMCOM_FAULT_PROTOCOL_ERROR:     return "PROTOCOL_ERROR";
    default:                              return "UNKNOWN";
    }
}

static const char *log_dir_to_str(simcom_log_dir_t dir)
{
    switch (dir) {
    case SIMCOM_LOG_DIR_TX:  return "TX";
    case SIMCOM_LOG_DIR_RX:  return "RX";
    case SIMCOM_LOG_DIR_URC: return "URC";
    case SIMCOM_LOG_DIR_FSM:
    default:                 return "FSM";
    }
}

static void print_line(const rpi5_platform_if_t * const platform, const char * const text)
{
    if ((platform != NULL) && (platform->console_print != NULL) && (text != NULL)) {
        platform->console_print(text, platform->ctx);
    }
}

/* ========================================================================== */
/* Default Configuration & CLI Argument Parser (T006, T014)                   */
/* ========================================================================== */

void Rpi5App_DefaultConfig(rpi5_example_cfg_t * const cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->serial_port = RPI5_DEFAULT_SERIAL_PORT;
    cfg->baud_rate = RPI5_DEFAULT_BAUD_RATE;
    cfg->gpio_chip = NULL;
    cfg->pwrkey_gpio_pin = RPI5_DEFAULT_PWRKEY_GPIO_PIN;
    cfg->pwrkey_pulse_ms = RPI5_DEFAULT_PWRKEY_PULSE_MS;
    cfg->pwrkey_boot_wait_ms = RPI5_DEFAULT_PWRKEY_BOOT_MS;
    cfg->monitor_interval_ms = RPI5_DEFAULT_MONITOR_PERIOD_MS;
    cfg->sim_pin = NULL;
    cfg->log_level = SIMCOM_LOG_LEVEL_INFO;
    cfg->run_once = false;
    cfg->show_help = false;
}

simcom_status_t Rpi5App_ParseArgs(
    int32_t argc,
    const char * const argv[],
    rpi5_example_cfg_t * const out_cfg)
{
    if ((out_cfg == NULL) || ((argc > 0) && (argv == NULL))) {
        return SIMCOM_STATUS_INVALID_ARG;
    }

    Rpi5App_DefaultConfig(out_cfg);

    int32_t i = 1;
    while (i < argc) {
        const char * const arg = argv[i];
        if (arg == NULL) {
            return SIMCOM_STATUS_INVALID_ARG;
        }

        if (cstr_equals(arg, "-h") || cstr_equals(arg, "--help")) {
            out_cfg->show_help = true;
            return SIMCOM_STATUS_OK;
        } else if (cstr_equals(arg, "-1") || cstr_equals(arg, "--once")) {
            out_cfg->run_once = true;
        } else if (cstr_equals(arg, "-v") || cstr_equals(arg, "--verbose")) {
            out_cfg->log_level = SIMCOM_LOG_LEVEL_DEBUG;
        } else if (cstr_equals(arg, "-q") || cstr_equals(arg, "--quiet")) {
            out_cfg->log_level = SIMCOM_LOG_LEVEL_WARNING;
        } else if (cstr_equals(arg, "-p") || cstr_equals(arg, "--port")) {
            if (((i + 1) >= argc) || (argv[i + 1] == NULL) || (argv[i + 1][0] == '\0')) {
                return SIMCOM_STATUS_INVALID_ARG;
            }
            out_cfg->serial_port = argv[i + 1];
            i++;
        } else if (cstr_equals(arg, "-b") || cstr_equals(arg, "--baud")) {
            uint32_t baud = 0U;
            if (((i + 1) >= argc) || !parse_u32_strict(argv[i + 1], &baud) || !is_valid_baud_rate(baud)) {
                return SIMCOM_STATUS_INVALID_ARG;
            }
            out_cfg->baud_rate = baud;
            i++;
        } else if (cstr_equals(arg, "-g") || cstr_equals(arg, "--gpio-chip")) {
            if (((i + 1) >= argc) || (argv[i + 1] == NULL) || (argv[i + 1][0] == '\0')) {
                return SIMCOM_STATUS_INVALID_ARG;
            }
            out_cfg->gpio_chip = argv[i + 1];
            i++;
        } else if (cstr_equals(arg, "-k") || cstr_equals(arg, "--pwrkey-pin")) {
            uint32_t pin_num = 0U;
            if (((i + 1) >= argc) || !parse_u32_strict(argv[i + 1], &pin_num) || (pin_num > 53U)) {
                return SIMCOM_STATUS_INVALID_ARG;
            }
            out_cfg->pwrkey_gpio_pin = pin_num;
            i++;
        } else if (cstr_equals(arg, "-P") || cstr_equals(arg, "--pin")) {
            if (((i + 1) >= argc) || !is_valid_pin_string(argv[i + 1])) {
                return SIMCOM_STATUS_INVALID_ARG;
            }
            out_cfg->sim_pin = argv[i + 1];
            i++;
        } else {
            return SIMCOM_STATUS_INVALID_ARG;
        }
        i++;
    }

    return SIMCOM_STATUS_OK;
}

void Rpi5App_PrintHelp(const char *prog_name, const rpi5_platform_if_t * const platform)
{
    const char * const name = ((prog_name != NULL) && (prog_name[0] != '\0'))
                                ? prog_name
                                : "simcom_rpi5_a7670e";
    char line[128];
    size_t len = 0U;

    buf_append_str(line, sizeof(line), &len, "Usage: ");
    buf_append_str(line, sizeof(line), &len, name);
    buf_append_str(line, sizeof(line), &len, " [OPTIONS]\n");
    print_line(platform, line);

    print_line(platform,
               "Raspberry Pi 5 + Waveshare A7670E Cat-1/GNSS HAT Reference Example\n\n"
               "Options:\n"
               "  -p, --port <path>        Serial device path (default: /dev/ttyAMA0)\n"
               "  -b, --baud <rate>        UART baud rate (default: 115200)\n"
               "  -g, --gpio-chip <path>   GPIO character device (default: auto /dev/gpiochip4 -> /dev/gpiochip0)\n"
               "  -k, --pwrkey-pin <num>   BCM GPIO pin number for PWRKEY (default: 4)\n"
               "  -P, --pin <digits>       Optional 4..8 digit SIM PIN code for locked SIM cards\n"
               "  -1, --once               Run status inspection once and exit immediately\n"
               "  -v, --verbose            Enable DEBUG-level raw AT communication logging\n"
               "  -q, --quiet              Suppress INFO logs\n"
               "  -h, --help               Display this help message and exit\n");
}

/* ========================================================================== */
/* Console Status Report & Telemetry Formatters (T011, T015)                  */
/* ========================================================================== */

static void emit_key_val_line(
    const rpi5_platform_if_t * const platform,
    const char * const label,
    const char * const value)
{
    char line[112];
    size_t len = 0U;
    buf_append_str(line, sizeof(line), &len, "  ");
    buf_append_str(line, sizeof(line), &len, label);
    buf_append_str(line, sizeof(line), &len, value);
    buf_append_str(line, sizeof(line), &len, "\n");
    print_line(platform, line);
}

static void print_full_status_report(const rpi5_app_t * const app)
{
    simcom_device_info_t dev;
    simcom_sim_info_t sim;
    simcom_net_status_t net;
    char line[112];
    size_t len = 0U;

    (void)Simcom_GetDeviceInfo(&app->modem, &dev);
    (void)Simcom_GetSimInfo(&app->modem, &sim);
    (void)Simcom_GetNetworkStatus(&app->modem, &net);

    print_line(&app->platform, "\n============================================================\n");
    print_line(&app->platform, " Waveshare A7670E Cat-1/GNSS HAT - Modem Status Report\n");
    print_line(&app->platform, "============================================================\n");

    print_line(&app->platform, "[Device Identity]\n");
    emit_key_val_line(&app->platform, "Manufacturer : ", (dev.manufacturer[0] != '\0') ? dev.manufacturer : "N/A");
    emit_key_val_line(&app->platform, "Model        : ", (dev.model[0] != '\0') ? dev.model : "N/A");
    emit_key_val_line(&app->platform, "Revision     : ", (dev.revision[0] != '\0') ? dev.revision : "N/A");
    emit_key_val_line(&app->platform, "IMEI         : ", (dev.imei[0] != '\0') ? dev.imei : "N/A");

    print_line(&app->platform, "[SIM Card]\n");
    emit_key_val_line(&app->platform, "SIM State    : ", sim_state_to_str(sim.state));
    emit_key_val_line(&app->platform, "ICCID        : ", sim.is_iccid_valid ? sim.iccid : "N/A");

    print_line(&app->platform, "[Cellular Network & Signal]\n");
    emit_key_val_line(&app->platform, "CS Reg (+CREG)   : ", reg_state_to_str(net.cs_reg));
    emit_key_val_line(&app->platform, "PS Reg (+CGREG)  : ", reg_state_to_str(net.ps_reg));
    emit_key_val_line(&app->platform, "EPS Reg (+CEREG) : ", reg_state_to_str(net.eps_reg));
    emit_key_val_line(&app->platform, "Operator         : ", (net.operator_name[0] != '\0') ? net.operator_name : "N/A");
    emit_key_val_line(&app->platform, "Access Tech      : ", act_to_str(net.act));
    emit_key_val_line(&app->platform, "System Mode      : ", (net.system_mode[0] != '\0') ? net.system_mode : "N/A");

    len = 0U;
    buf_append_str(line, sizeof(line), &len, "  Signal Strength  : ");
    if (net.rssi_raw == 99U) {
        buf_append_str(line, sizeof(line), &len, "Unknown (RSSI=99)");
    } else {
        buf_append_i32(line, sizeof(line), &len, (int32_t)net.rssi_dbm);
        buf_append_str(line, sizeof(line), &len, " dBm (RSSI=");
        buf_append_u32(line, sizeof(line), &len, (uint32_t)net.rssi_raw);
        buf_append_str(line, sizeof(line), &len, ", BER=");
        buf_append_u32(line, sizeof(line), &len, (uint32_t)net.ber_raw);
        buf_append_str(line, sizeof(line), &len, ")");
    }
    buf_append_str(line, sizeof(line), &len, "\n");
    print_line(&app->platform, line);
    print_line(&app->platform, "============================================================\n");
}

/* ========================================================================== */
/* libsimcom HAL, Logging, and Event Callbacks (T010, T011, T015)             */
/* ========================================================================== */

static size_t app_hal_uart_tx(const uint8_t *data, size_t len, void *user_ctx)
{
    rpi5_app_t * const app = (rpi5_app_t *)user_ctx;
    if ((app == NULL) || (app->platform.uart_tx == NULL)) {
        return 0U;
    }
    return app->platform.uart_tx(data, len, app->platform.ctx);
}

static uint32_t app_hal_get_tick_ms(void *user_ctx)
{
    rpi5_app_t * const app = (rpi5_app_t *)user_ctx;
    if ((app == NULL) || (app->platform.get_tick_ms == NULL)) {
        return 0U;
    }
    return app->platform.get_tick_ms(app->platform.ctx);
}

static void app_log_callback(
    simcom_log_level_t level,
    uint32_t timestamp_ms,
    simcom_log_dir_t direction,
    const uint8_t *payload,
    size_t len,
    void *user_ctx)
{
    rpi5_app_t * const app = (rpi5_app_t *)user_ctx;
    if (app == NULL) {
        return;
    }
    /* Only emit raw TX/RX/FSM trace lines when verbose (DEBUG) or WARNING/ERROR */
    if ((app->cfg.log_level > SIMCOM_LOG_LEVEL_DEBUG) && (level < SIMCOM_LOG_LEVEL_WARNING)) {
        return;
    }

    char line[112];
    size_t pos = 0U;
    buf_append_str(line, sizeof(line), &pos, "[");
    buf_append_u32(line, sizeof(line), &pos, timestamp_ms);
    buf_append_str(line, sizeof(line), &pos, " ms][");
    buf_append_str(line, sizeof(line), &pos, log_dir_to_str(direction));
    buf_append_str(line, sizeof(line), &pos, "] ");
    buf_append_bytes(line, sizeof(line), &pos, payload, len);
    buf_append_str(line, sizeof(line), &pos, "\n");
    print_line(&app->platform, line);
}

static void start_net_query_or_fail(rpi5_app_t * const app)
{
    app->state = RPI5_APP_STATE_QUERY_NET;
    if (Simcom_RequestNetworkStatus(&app->modem) != SIMCOM_STATUS_PENDING) {
        print_line(&app->platform, "[ERROR] Failed to start network status query.\n");
        app->exit_code = 1;
        app->state = RPI5_APP_STATE_ERROR;
    }
}

static void app_event_callback(
    simcom_event_type_t event,
    simcom_status_t result_status,
    void *user_ctx)
{
    rpi5_app_t * const app = (rpi5_app_t *)user_ctx;
    if (app == NULL) {
        return;
    }

    switch (event) {
    case SIMCOM_EVENT_INIT_COMPLETE:
        if (result_status == SIMCOM_STATUS_OK) {
            if (app->cfg.log_level <= SIMCOM_LOG_LEVEL_INFO) {
                print_line(&app->platform, "[INFO] Modem initialized and ready. Querying device identity...\n");
            }
            app->state = RPI5_APP_STATE_QUERY_DEV_INFO;
            if (Simcom_RequestDeviceInfo(&app->modem) != SIMCOM_STATUS_PENDING) {
                app->exit_code = 1;
                app->state = RPI5_APP_STATE_ERROR;
            }
        } else {
            print_line(&app->platform, "[ERROR] Modem initialization failed.\n");
            app->exit_code = 1;
            app->state = RPI5_APP_STATE_ERROR;
        }
        break;

    case SIMCOM_EVENT_DEVICE_INFO_READY:
        app->state = RPI5_APP_STATE_QUERY_SIM;
        if (Simcom_RequestSimStatus(&app->modem) != SIMCOM_STATUS_PENDING) {
            app->exit_code = 1;
            app->state = RPI5_APP_STATE_ERROR;
        }
        break;

    case SIMCOM_EVENT_SIM_STATUS_READY: {
        simcom_sim_info_t sim;
        (void)Simcom_GetSimInfo(&app->modem, &sim);
        if (sim.state == SIMCOM_SIM_STATE_PIN_REQUIRED) {
            if ((app->cfg.sim_pin != NULL) && (!app->pin_attempted)) {
                app->pin_attempted = true;
                app->state = RPI5_APP_STATE_SUBMIT_PIN;
                print_line(&app->platform, "[INFO] SIM requires PIN. Submitting user-supplied PIN once...\n");
                if (Simcom_SubmitSimPin(&app->modem, app->cfg.sim_pin) != SIMCOM_STATUS_PENDING) {
                    print_line(&app->platform, "[ERROR] Failed to submit SIM PIN.\n");
                    start_net_query_or_fail(app);
                }
            } else {
                print_line(&app->platform,
                           "[WARN] SIM is PIN-locked and no --pin was supplied. Skipping PIN submission to avoid lockout.\n");
                start_net_query_or_fail(app);
            }
        } else {
            if (sim.state == SIMCOM_SIM_STATE_ABSENT) {
                print_line(&app->platform, "[WARN] No SIM card detected in Waveshare A7670E HAT slot.\n");
            } else if (sim.state == SIMCOM_SIM_STATE_PUK_REQUIRED) {
                print_line(&app->platform, "[WARN] SIM card is PUK-locked.\n");
            }
            start_net_query_or_fail(app);
        }
        break;
    }

    case SIMCOM_EVENT_SIM_PIN_RESULT:
        if (result_status == SIMCOM_STATUS_OK) {
            print_line(&app->platform, "[INFO] SIM PIN accepted. Refreshing SIM status & ICCID...\n");
            app->state = RPI5_APP_STATE_QUERY_SIM;
            if (Simcom_RequestSimStatus(&app->modem) != SIMCOM_STATUS_PENDING) {
                start_net_query_or_fail(app);
            }
        } else {
            print_line(&app->platform, "[ERROR] SIM PIN unlock failed. Proceeding without unlocked SIM.\n");
            start_net_query_or_fail(app);
        }
        break;

    case SIMCOM_EVENT_NET_STATUS_READY:
        print_full_status_report(app);
        app->initial_report_printed = true;
        if (app->cfg.run_once) {
            app->exit_code = 0;
            app->state = RPI5_APP_STATE_DONE;
        } else {
            if (app->state != RPI5_APP_STATE_MONITOR) {
                print_line(&app->platform,
                           "[INFO] Entering continuous monitoring mode (press Ctrl+C to stop)...\n");
            }
            app->state = RPI5_APP_STATE_MONITOR;
            app->phase_start_ms = app_hal_get_tick_ms(app);
        }
        break;

    case SIMCOM_EVENT_NET_REG_URC_CHANGED: {
        simcom_net_status_t net;
        char line[112];
        size_t len = 0U;
        (void)Simcom_GetNetworkStatus(&app->modem, &net);
        buf_append_str(line, sizeof(line), &len, "[URC] Network registration updated: CS=");
        buf_append_str(line, sizeof(line), &len, reg_state_to_str(net.cs_reg));
        buf_append_str(line, sizeof(line), &len, ", PS=");
        buf_append_str(line, sizeof(line), &len, reg_state_to_str(net.ps_reg));
        buf_append_str(line, sizeof(line), &len, ", EPS=");
        buf_append_str(line, sizeof(line), &len, reg_state_to_str(net.eps_reg));
        buf_append_str(line, sizeof(line), &len, "\n");
        print_line(&app->platform, line);
        break;
    }

    case SIMCOM_EVENT_FAULT: {
        const simcom_fault_t fault = Simcom_GetFault(&app->modem);
        char line[112];
        size_t len = 0U;
        buf_append_str(line, sizeof(line), &len, "[ERROR] Modem entered FAULT state: ");
        buf_append_str(line, sizeof(line), &len, fault_to_str(fault));
        buf_append_str(line, sizeof(line), &len, "\n");
        print_line(&app->platform, line);
        if (fault == SIMCOM_FAULT_MODEM_UNRESPONSIVE) {
            print_line(&app->platform,
                       "[HINT] Verify Waveshare A7670E HAT jumpers (PWR->D4, TXD/RXD), 5V power, and /dev/ttyAMA0.\n");
        }
        app->exit_code = 2;
        app->state = RPI5_APP_STATE_ERROR;
        break;
    }

    default:
        break;
    }
}

/* ========================================================================== */
/* Public Coordinator Lifecycle & Stepping Functions (T010)                   */
/* ========================================================================== */

simcom_status_t Rpi5App_Init(
    rpi5_app_t * const app,
    const rpi5_example_cfg_t * const cfg,
    const rpi5_platform_if_t * const platform)
{
    if ((app == NULL) || (cfg == NULL) || (platform == NULL)) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    if ((platform->uart_tx == NULL) ||
        (platform->get_tick_ms == NULL) ||
        (platform->gpio_set_pwrkey == NULL) ||
        (platform->console_print == NULL)) {
        return SIMCOM_STATUS_INVALID_ARG;
    }

    (void)memset(app, 0, sizeof(*app));
    app->cfg = *cfg;
    app->platform = *platform;
    app->state = RPI5_APP_STATE_IDLE;
    app->exit_code = 0;

    simcom_runtime_cfg_t rt_cfg = {
        .default_cmd_timeout_ms = SIMCOM_CFG_DEFAULT_CMD_TIMEOUT_MS,
        .net_query_timeout_ms = SIMCOM_CFG_NET_QUERY_TIMEOUT_MS,
        .max_cmd_retries = SIMCOM_CFG_MAX_CMD_RETRIES,
        .tier1_sync_retries = SIMCOM_CFG_TIER1_SYNC_RETRIES,
        .tier2_reset_timeout_ms = SIMCOM_CFG_TIER2_RESET_TIMEOUT_MS,
        .min_log_level = cfg->log_level
    };

    simcom_init_t init_desc = {
        .hal = {
            .uart_tx_async = app_hal_uart_tx,
            .get_tick_ms = app_hal_get_tick_ms,
            .enter_critical = NULL,
            .exit_critical = NULL,
            .gpio_write_pwrkey = NULL,
            .gpio_write_reset = NULL,
            .gpio_read_status = NULL,
            .user_ctx = app
        },
        .profile = NULL, /* Defaults to &g_simcom_profile_a76xx */
        .runtime_cfg = &rt_cfg,
        .log_fn = app_log_callback,
        .event_cb = app_event_callback
    };

    return Simcom_Init(&app->modem, &init_desc);
}

simcom_status_t Rpi5App_Start(rpi5_app_t * const app)
{
    if (app == NULL) {
        return SIMCOM_STATUS_INVALID_ARG;
    }

    if (app->cfg.log_level <= SIMCOM_LOG_LEVEL_INFO) {
        print_line(&app->platform, "[INFO] Asserting Raspberry Pi 5 BCM GPIO 4 (PWRKEY) to wake Waveshare A7670E HAT...\n");
    }

    if (!app->platform.gpio_set_pwrkey(true, app->platform.ctx)) {
        print_line(&app->platform, "[ERROR] Failed to assert BCM GPIO 4 (PWRKEY).\n");
        app->exit_code = 1;
        app->state = RPI5_APP_STATE_ERROR;
        return SIMCOM_STATUS_ERROR;
    }

    app->phase_start_ms = app_hal_get_tick_ms(app);
    app->state = RPI5_APP_STATE_PWRKEY_ASSERT;
    return SIMCOM_STATUS_OK;
}

simcom_status_t Rpi5App_FeedBytes(
    rpi5_app_t * const app,
    const uint8_t * const data,
    size_t len)
{
    if (app == NULL) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    return Simcom_FeedBytes(&app->modem, data, len, NULL);
}

void Rpi5App_Poll(rpi5_app_t * const app)
{
    if ((app == NULL) || Rpi5App_IsFinished(app)) {
        return;
    }

    const uint32_t now = app_hal_get_tick_ms(app);

    if (app->state == RPI5_APP_STATE_PWRKEY_ASSERT) {
        if ((uint32_t)(now - app->phase_start_ms) >= app->cfg.pwrkey_pulse_ms) {
            if (!app->platform.gpio_set_pwrkey(false, app->platform.ctx)) {
                print_line(&app->platform, "[ERROR] Failed to release BCM GPIO 4 (PWRKEY).\n");
                app->exit_code = 1;
                app->state = RPI5_APP_STATE_ERROR;
                return;
            }
            if (app->cfg.log_level <= SIMCOM_LOG_LEVEL_INFO) {
                print_line(&app->platform, "[INFO] Released GPIO 4 (PWRKEY). Waiting for modem boot settle...\n");
            }
            app->phase_start_ms = now;
            app->state = RPI5_APP_STATE_PWRKEY_WAIT_BOOT;
        }
        return;
    }

    if (app->state == RPI5_APP_STATE_PWRKEY_WAIT_BOOT) {
        /* Drain any early startup banners (e.g., RDY / *ATREADY) via Simcom_Poll */
        (void)Simcom_Poll(&app->modem);
        if ((uint32_t)(now - app->phase_start_ms) >= app->cfg.pwrkey_boot_wait_ms) {
            if (app->cfg.log_level <= SIMCOM_LOG_LEVEL_INFO) {
                print_line(&app->platform, "[INFO] Starting libsimcom asynchronous initialization...\n");
            }
            app->state = RPI5_APP_STATE_MODEM_INIT;
            if (Simcom_StartInit(&app->modem) != SIMCOM_STATUS_PENDING) {
                print_line(&app->platform, "[ERROR] Failed to start modem initialization.\n");
                app->exit_code = 1;
                app->state = RPI5_APP_STATE_ERROR;
                return;
            }
        }
        return;
    }

    /* Step the core libsimcom state machine and URC dispatcher */
    (void)Simcom_Poll(&app->modem);

    /* Periodic network/signal refresh in continuous monitoring mode */
    if ((app->state == RPI5_APP_STATE_MONITOR) &&
        (Simcom_GetState(&app->modem) == SIMCOM_STATE_READY)) {
        if ((uint32_t)(now - app->phase_start_ms) >= app->cfg.monitor_interval_ms) {
            app->phase_start_ms = now;
            (void)Simcom_RequestNetworkStatus(&app->modem);
        }
    }
}

void Rpi5App_RequestStop(rpi5_app_t * const app)
{
    if (app == NULL) {
        return;
    }
    if (!Rpi5App_IsFinished(app)) {
        print_line(&app->platform, "\n[INFO] Termination requested. Closing resources cleanly...\n");
        app->exit_code = 0;
        app->state = RPI5_APP_STATE_DONE;
    }
}

bool Rpi5App_IsFinished(const rpi5_app_t * const app)
{
    if (app == NULL) {
        return true;
    }
    return (app->state == RPI5_APP_STATE_DONE) || (app->state == RPI5_APP_STATE_ERROR);
}
