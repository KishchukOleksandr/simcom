/**
 * @file simcom_fsm.c
 * @brief Non-blocking AT transaction engine, workflow steppers, and Tier 1/2 recovery FSM.
 *
 * Compliance:
 *   - Principle 1: Static memory only, command tables in .rodata.
 *   - Principle 2: Non-blocking FSM stepped via Simcom_Poll(), unsigned wrap-around-safe ticks.
 *   - Principle 6 / FR-015: Tier 1 soft re-sync + Tier 2 logical reset -> MODEM_UNRESPONSIVE.
 *   - Section 3: Fixed-width types only, WCSU <= 128 Bytes.
 */

#include "simcom_fsm.h"
#include "simcom_lexer.h"
#include "simcom_parser.h"
#include "simcom_log.h"
#include <string.h>

static uint32_t get_now_ms(const simcom_handle_t * const me)
{
    if ((me != NULL) && (me->hal.get_tick_ms != NULL)) {
        return me->hal.get_tick_ms(me->hal.user_ctx);
    }
    return 0U;
}

static void emit_event(const simcom_handle_t * const me, simcom_event_type_t ev, simcom_status_t status)
{
    if ((me != NULL) && (me->event_cb != NULL)) {
        me->event_cb(ev, status, me->hal.user_ctx);
    }
}

static void queue_raw_cmd(
    simcom_handle_t * const me,
    const char * const cmd_str,
    uint32_t timeout_ms,
    uint8_t retries)
{
    const size_t cmd_len = simcom_cstr_nlen(cmd_str, SIMCOM_CFG_TX_CMD_BUF_SIZE - 3U);
    if (cmd_len > 0U) {
        simcom_mem_copy(me->tx_buf, cmd_str, cmd_len);
    }
    me->tx_buf[cmd_len] = (uint8_t)'\r';
    me->tx_buf[cmd_len + 1U] = (uint8_t)'\n';
    me->tx_buf[cmd_len + 2U] = 0U;
    me->tx_len = cmd_len + 2U;
    me->tx_offset = 0U;
    me->cmd_timeout_ms = timeout_ms;
    me->cmd_retries_left = retries;
    me->cmd_response_matched = false;
    me->cmd_last_cme_error = 0U;
    me->cmd_start_tick_ms = get_now_ms(me);
    me->at_tx_state = (uint8_t)SIMCOM_AT_TX_STREAMING;
}

static void queue_pin_cmd_and_wipe(simcom_handle_t * const me)
{
    const char * const prefix = "AT+CPIN=\"";
    const size_t plen = 9U;
    const size_t pin_len = simcom_cstr_nlen(me->pin_scratch, SIMCOM_CFG_PIN_MAX_LEN);

    simcom_mem_copy(me->tx_buf, prefix, plen);
    if (pin_len > 0U) {
        simcom_mem_copy(&me->tx_buf[plen], me->pin_scratch, pin_len);
    }
    me->tx_buf[plen + pin_len] = (uint8_t)'"';
    me->tx_buf[plen + pin_len + 1U] = (uint8_t)'\r';
    me->tx_buf[plen + pin_len + 2U] = (uint8_t)'\n';
    me->tx_buf[plen + pin_len + 3U] = 0U;
    me->tx_len = plen + pin_len + 3U;
    me->tx_offset = 0U;

    /* Immediately wipe sensitive PIN from scratchpad (FR-004) */
    simcom_mem_zero(me->pin_scratch, sizeof(me->pin_scratch));

    me->cmd_timeout_ms = me->cfg.default_cmd_timeout_ms;
    me->cmd_retries_left = 0U; /* Single attempt ONLY — never retry PIN */
    me->cmd_response_matched = false;
    me->cmd_last_cme_error = 0U;
    me->cmd_start_tick_ms = get_now_ms(me);
    me->at_tx_state = (uint8_t)SIMCOM_AT_TX_STREAMING;
}

static void start_init_step(simcom_handle_t * const me, uint8_t step)
{
    const simcom_modem_profile_t * const prof = me->profile;
    const uint32_t tmo = me->cfg.default_cmd_timeout_ms;
    const uint8_t retries = me->cfg.max_cmd_retries;
    me->step_index = step;

    switch (step) {
        case 0U:
            queue_raw_cmd(me, prof->cmd_handshake, tmo, retries);
            break;
        case 1U:
            queue_raw_cmd(me, prof->cmd_echo_off, tmo, retries);
            break;
        case 2U:
            me->cfun_state = 0xFFU;
            queue_raw_cmd(me, prof->cmd_cfun_query, tmo, retries);
            break;
        case 3U:
            queue_raw_cmd(me, prof->cmd_cfun_full, tmo, retries);
            break;
        case 4U:
            queue_raw_cmd(me, prof->cmd_cmee_numeric, tmo, retries);
            break;
        case 5U:
            queue_raw_cmd(me, "AT+CREG=1", tmo, retries);
            break;
        case 6U:
            queue_raw_cmd(me, "AT+CGREG=1", tmo, retries);
            break;
        case 7U:
        default:
            queue_raw_cmd(me, "AT+CEREG=1", tmo, retries);
            break;
    }
}

static void start_device_info_step(simcom_handle_t * const me, uint8_t step)
{
    const uint32_t tmo = me->cfg.default_cmd_timeout_ms;
    const uint8_t retries = me->cfg.max_cmd_retries;
    me->step_index = step;

    switch (step) {
        case 0U:
            queue_raw_cmd(me, "ATI", tmo, retries);
            break;
        case 1U:
            queue_raw_cmd(me, "AT+CGMM", tmo, retries);
            break;
        case 2U:
            queue_raw_cmd(me, "AT+CGMR", tmo, retries);
            break;
        case 3U:
        default:
            queue_raw_cmd(me, "AT+CGSN", tmo, retries);
            break;
    }
}

static void finish_device_info_workflow(simcom_handle_t * const me)
{
    me->device_info.is_valid = (me->device_info.model[0] != '\0') &&
                               (me->device_info.revision[0] != '\0') &&
                               (me->device_info.imei[0] != '\0');
    me->state = SIMCOM_STATE_READY;
    me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
    me->at_tx_state = (uint8_t)SIMCOM_AT_IDLE;
    emit_event(me,
               SIMCOM_EVENT_DEVICE_INFO_READY,
               me->device_info.is_valid ? SIMCOM_STATUS_OK : SIMCOM_STATUS_ERROR);
}

static void advance_device_info_workflow(simcom_handle_t * const me)
{
    if ((me->step_index < 1U) && (me->device_info.model[0] == '\0')) {
        start_device_info_step(me, 1U);
        return;
    }
    if ((me->step_index < 2U) && (me->device_info.revision[0] == '\0')) {
        start_device_info_step(me, 2U);
        return;
    }
    if ((me->step_index < 3U) && (me->device_info.imei[0] == '\0')) {
        start_device_info_step(me, 3U);
        return;
    }
    finish_device_info_workflow(me);
}

static void start_net_status_step(simcom_handle_t * const me, uint8_t step)
{
    const uint32_t net_tmo = me->cfg.net_query_timeout_ms;
    const uint32_t cmd_tmo = me->cfg.default_cmd_timeout_ms;
    const uint8_t retries = me->cfg.max_cmd_retries;
    me->step_index = step;

    switch (step) {
        case 0U:
            queue_raw_cmd(me, "AT+CREG?", net_tmo, retries);
            break;
        case 1U:
            queue_raw_cmd(me, "AT+CGREG?", net_tmo, retries);
            break;
        case 2U:
            queue_raw_cmd(me, "AT+CEREG?", net_tmo, retries);
            break;
        case 3U:
            queue_raw_cmd(me, "AT+CSQ", cmd_tmo, retries);
            break;
        case 4U:
            queue_raw_cmd(me, "AT+COPS?", net_tmo, retries);
            break;
        case 5U:
        default:
            queue_raw_cmd(me, me->profile->cmd_sys_info, net_tmo, retries);
            break;
    }
}

void simcom_fsm_init(simcom_handle_t * const me)
{
    if (me != NULL) {
        me->state = SIMCOM_STATE_UNINITIALIZED;
        me->pre_recovery_state = SIMCOM_STATE_UNINITIALIZED;
        me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
        me->step_index = 0U;
        me->tier1_attempts = 0U;
        me->fault_code = SIMCOM_FAULT_NONE;
        me->at_tx_state = (uint8_t)SIMCOM_AT_IDLE;
        me->rdy_received = false;
    }
}

simcom_status_t simcom_fsm_start_workflow(simcom_handle_t * const me, simcom_workflow_id_t wf)
{
    if (me == NULL) {
        return SIMCOM_STATUS_INVALID_ARG;
    }

    if (wf == SIMCOM_WF_INIT) {
        if ((me->state == SIMCOM_STATE_INITIALIZING) ||
            (me->state == SIMCOM_STATE_BUSY_WORKFLOW) ||
            (me->state == SIMCOM_STATE_RECOVERY_TIER1) ||
            (me->state == SIMCOM_STATE_RECOVERY_TIER2)) {
            return SIMCOM_STATUS_BUSY;
        }
        me->state = SIMCOM_STATE_INITIALIZING;
        me->active_workflow = (uint8_t)SIMCOM_WF_INIT;
        me->fault_code = SIMCOM_FAULT_NONE;
        me->rdy_received = false;
        simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_INFO, SIMCOM_LOG_DIR_FSM, "INIT_START");
        start_init_step(me, 0U);
        return SIMCOM_STATUS_PENDING;
    }

    if ((me->state == SIMCOM_STATE_INITIALIZING) ||
        (me->state == SIMCOM_STATE_BUSY_WORKFLOW) ||
        (me->state == SIMCOM_STATE_RECOVERY_TIER1) ||
        (me->state == SIMCOM_STATE_RECOVERY_TIER2)) {
        return SIMCOM_STATUS_BUSY;
    }

    if (me->state != SIMCOM_STATE_READY) {
        return SIMCOM_STATUS_NOT_READY;
    }

    me->state = SIMCOM_STATE_BUSY_WORKFLOW;
    me->active_workflow = (uint8_t)wf;
    me->step_index = 0U;

    switch (wf) {
        case SIMCOM_WF_DEVICE_INFO:
            simcom_mem_zero(&me->device_info, sizeof(me->device_info));
            start_device_info_step(me, 0U);
            break;

        case SIMCOM_WF_SIM_STATUS:
            me->sim_info.last_cme_error = 0U;
            queue_raw_cmd(me, "AT+CPIN?", me->cfg.default_cmd_timeout_ms, me->cfg.max_cmd_retries);
            break;

        case SIMCOM_WF_SIM_PIN:
            queue_pin_cmd_and_wipe(me);
            break;

        case SIMCOM_WF_NET_STATUS:
            start_net_status_step(me, 0U);
            break;

        case SIMCOM_WF_NONE:
        case SIMCOM_WF_INIT:
        default:
            me->state = SIMCOM_STATE_READY;
            me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
            return SIMCOM_STATUS_INVALID_ARG;
    }

    return SIMCOM_STATUS_PENDING;
}

void simcom_fsm_on_solicited_line(simcom_handle_t * const me, const uint8_t * const line, size_t len)
{
    if ((me == NULL) || (line == NULL) || (len == 0U)) {
        return;
    }
    if (me->at_tx_state != (uint8_t)SIMCOM_AT_WAIT_RESPONSE) {
        return;
    }

    simcom_log_emit(me, SIMCOM_LOG_LEVEL_DEBUG, SIMCOM_LOG_DIR_RX, line, len);

    /* Ignore echoed AT command lines if modem echo is still enabled prior to ATE0 */
    if (simcom_span_starts_with(line, len, "AT")) {
        return;
    }

    const simcom_workflow_id_t wf = (simcom_workflow_id_t)me->active_workflow;

    if ((wf == SIMCOM_WF_INIT) && (me->step_index == 2U)) {
        uint8_t fun = 0U;
        if (simcom_parse_cfun_resp(line, len, &fun)) {
            me->cfun_state = fun;
            me->cmd_response_matched = true;
        }
        return;
    }

    if (wf == SIMCOM_WF_DEVICE_INFO) {
        if (me->step_index == 0U) {
            if (simcom_parse_ati_line(line, len, &me->device_info)) {
                me->cmd_response_matched = true;
            }
        } else if (me->step_index == 1U) {
            if (simcom_span_copy_to_cstr(line, len, me->device_info.model, sizeof(me->device_info.model))) {
                me->cmd_response_matched = true;
            }
        } else if (me->step_index == 2U) {
            if (simcom_span_copy_to_cstr(line, len, me->device_info.revision, sizeof(me->device_info.revision))) {
                me->cmd_response_matched = true;
            }
        } else if (me->step_index == 3U) {
            if (simcom_parse_digits_line(line, len, me->device_info.imei, sizeof(me->device_info.imei))) {
                me->cmd_response_matched = true;
            }
        } else {
            /* No action */
        }
        return;
    }

    if ((wf == SIMCOM_WF_SIM_STATUS) && (me->step_index == 1U)) {
        if (simcom_parse_iccid_resp(line, len, me->sim_info.iccid, sizeof(me->sim_info.iccid))) {
            me->sim_info.is_iccid_valid = true;
            me->cmd_response_matched = true;
        }
        return;
    }

    if (wf == SIMCOM_WF_NET_STATUS) {
        if (me->step_index == 3U) {
            uint8_t rssi = 99U;
            int16_t dbm = 0;
            uint8_t ber = 99U;
            if (simcom_parse_csq_resp(line, len, &rssi, &dbm, &ber)) {
                me->net_status.rssi_raw = rssi;
                me->net_status.rssi_dbm = dbm;
                me->net_status.ber_raw = ber;
                me->cmd_response_matched = true;
            }
        } else if (me->step_index == 4U) {
            if (simcom_parse_cops_resp(line,
                                       len,
                                       me->net_status.operator_name,
                                       sizeof(me->net_status.operator_name),
                                       &me->net_status.act)) {
                me->cmd_response_matched = true;
            }
        } else if (me->step_index == 5U) {
            if (simcom_parse_cpsi_resp(line,
                                       len,
                                       me->net_status.system_mode,
                                       sizeof(me->net_status.system_mode),
                                       &me->net_status.act)) {
                me->cmd_response_matched = true;
            }
        } else {
            /* Steps 0..2 (+CREG?, +CGREG?, +CEREG?) handled in dual-path lexer */
        }
    }
}

void simcom_fsm_on_terminal_ok(simcom_handle_t * const me)
{
    if ((me == NULL) || (me->at_tx_state != (uint8_t)SIMCOM_AT_WAIT_RESPONSE)) {
        return;
    }
    /* In Tier 2 reset, AT+CRESET may emit OK before RDY; keep waiting for RDY */
    if (me->state == SIMCOM_STATE_RECOVERY_TIER2) {
        return;
    }
    me->at_tx_state = (uint8_t)SIMCOM_AT_COMPLETED_OK;
}

void simcom_fsm_on_terminal_error(simcom_handle_t * const me, uint16_t cme_code)
{
    if ((me == NULL) || (me->at_tx_state != (uint8_t)SIMCOM_AT_WAIT_RESPONSE)) {
        return;
    }
    me->cmd_last_cme_error = cme_code;
    me->at_tx_state = (uint8_t)SIMCOM_AT_COMPLETED_ERROR;
}

static void enter_tier1_recovery(simcom_handle_t * const me)
{
    me->pre_recovery_state = me->state;
    me->state = SIMCOM_STATE_RECOVERY_TIER1;
    me->step_index = 0U;
    me->tier1_attempts = 1U;
    simcom_lexer_reset_line(me);
    simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_WARNING, SIMCOM_LOG_DIR_FSM, "RECOVERY_TIER1");
    queue_raw_cmd(me, me->profile->cmd_handshake, me->profile->handshake_timeout_ms, 0U);
}

static void enter_tier2_recovery(simcom_handle_t * const me)
{
    me->state = SIMCOM_STATE_RECOVERY_TIER2;
    me->step_index = 0U;
    me->rdy_received = false;
    simcom_lexer_reset_line(me);
    simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_ERROR, SIMCOM_LOG_DIR_FSM, "RECOVERY_TIER2_RESET");
    queue_raw_cmd(me, me->profile->cmd_logical_reset, me->cfg.tier2_reset_timeout_ms, 0U);
}

static void enter_unresponsive_fault(simcom_handle_t * const me)
{
    me->state = SIMCOM_STATE_FAULT;
    me->fault_code = SIMCOM_FAULT_MODEM_UNRESPONSIVE;
    me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
    me->at_tx_state = (uint8_t)SIMCOM_AT_IDLE;
    simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_ERROR, SIMCOM_LOG_DIR_FSM, "FAULT_MODEM_UNRESPONSIVE");
    emit_event(me, SIMCOM_EVENT_FAULT, SIMCOM_STATUS_ERROR);
}

static void handle_workflow_cmd_ok(simcom_handle_t * const me)
{
    me->at_tx_state = (uint8_t)SIMCOM_AT_IDLE;

    if (me->state == SIMCOM_STATE_RECOVERY_TIER1) {
        if (me->step_index == 0U) {
            me->step_index = 1U;
            queue_raw_cmd(me, me->profile->cmd_echo_off, me->cfg.default_cmd_timeout_ms, 0U);
        } else {
            simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_INFO, SIMCOM_LOG_DIR_FSM, "RECOVERY_TIER1_OK");
            if (me->pre_recovery_state == SIMCOM_STATE_INITIALIZING) {
                me->state = SIMCOM_STATE_INITIALIZING;
                me->active_workflow = (uint8_t)SIMCOM_WF_INIT;
                start_init_step(me, 0U);
            } else {
                me->state = SIMCOM_STATE_READY;
                me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
            }
        }
        return;
    }

    const simcom_workflow_id_t wf = (simcom_workflow_id_t)me->active_workflow;

    switch (wf) {
        case SIMCOM_WF_INIT:
            if (me->step_index == 2U) {
                if (me->cfun_state == 1U) {
                    start_init_step(me, 4U);
                } else {
                    start_init_step(me, 3U);
                }
            } else if (me->step_index < 7U) {
                start_init_step(me, (uint8_t)(me->step_index + 1U));
            } else {
                me->state = SIMCOM_STATE_READY;
                me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
                me->fault_code = SIMCOM_FAULT_NONE;
                simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_INFO, SIMCOM_LOG_DIR_FSM, "INIT_READY");
                emit_event(me, SIMCOM_EVENT_INIT_COMPLETE, SIMCOM_STATUS_OK);
            }
            break;

        case SIMCOM_WF_DEVICE_INFO:
            advance_device_info_workflow(me);
            break;

        case SIMCOM_WF_SIM_STATUS:
            if (me->step_index == 0U) {
                if (me->sim_info.state == SIMCOM_SIM_STATE_READY) {
                    me->step_index = 1U;
                    me->sim_info.is_iccid_valid = false;
                    queue_raw_cmd(me,
                                  me->profile->cmd_iccid_query,
                                  me->cfg.default_cmd_timeout_ms,
                                  me->cfg.max_cmd_retries);
                } else {
                    me->sim_info.is_iccid_valid = false;
                    me->state = SIMCOM_STATE_READY;
                    me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
                    emit_event(me, SIMCOM_EVENT_SIM_STATUS_READY, SIMCOM_STATUS_OK);
                }
            } else {
                me->state = SIMCOM_STATE_READY;
                me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
                emit_event(me,
                           SIMCOM_EVENT_SIM_STATUS_READY,
                           me->sim_info.is_iccid_valid ? SIMCOM_STATUS_OK : SIMCOM_STATUS_ERROR);
            }
            break;

        case SIMCOM_WF_SIM_PIN:
            me->sim_info.state = SIMCOM_SIM_STATE_READY;
            me->sim_info.last_cme_error = 0U;
            me->state = SIMCOM_STATE_READY;
            me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
            emit_event(me, SIMCOM_EVENT_SIM_PIN_RESULT, SIMCOM_STATUS_OK);
            break;

        case SIMCOM_WF_NET_STATUS:
            if (me->step_index < 5U) {
                start_net_status_step(me, (uint8_t)(me->step_index + 1U));
            } else {
                me->state = SIMCOM_STATE_READY;
                me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
                emit_event(me, SIMCOM_EVENT_NET_STATUS_READY, SIMCOM_STATUS_OK);
            }
            break;

        case SIMCOM_WF_NONE:
        default:
            break;
    }
}

static void handle_workflow_cmd_error(simcom_handle_t * const me)
{
    me->at_tx_state = (uint8_t)SIMCOM_AT_IDLE;

    if (me->state == SIMCOM_STATE_RECOVERY_TIER1) {
        if (me->tier1_attempts < me->cfg.tier1_sync_retries) {
            me->tier1_attempts++;
            queue_raw_cmd(me, me->profile->cmd_handshake, me->profile->handshake_timeout_ms, 0U);
        } else {
            enter_tier2_recovery(me);
        }
        return;
    }

    const simcom_workflow_id_t wf = (simcom_workflow_id_t)me->active_workflow;

    if (wf == SIMCOM_WF_SIM_STATUS) {
        if (me->step_index == 0U) {
            me->sim_info.last_cme_error = me->cmd_last_cme_error;
            me->sim_info.state = (me->cmd_last_cme_error != 0U)
                                 ? simcom_map_cme_to_sim_state(me->cmd_last_cme_error)
                                 : SIMCOM_SIM_STATE_ABSENT;
            me->sim_info.is_iccid_valid = false;
            me->state = SIMCOM_STATE_READY;
            me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
            emit_event(me, SIMCOM_EVENT_SIM_STATUS_READY, SIMCOM_STATUS_OK);
            return;
        }
        me->sim_info.is_iccid_valid = false;
        me->state = SIMCOM_STATE_READY;
        me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
        emit_event(me, SIMCOM_EVENT_SIM_STATUS_READY, SIMCOM_STATUS_ERROR);
        return;
    }

    if (wf == SIMCOM_WF_SIM_PIN) {
        /* Never retry PIN; report failure and update lock state */
        me->sim_info.last_cme_error = me->cmd_last_cme_error;
        if (me->cmd_last_cme_error != 0U) {
            me->sim_info.state = simcom_map_cme_to_sim_state(me->cmd_last_cme_error);
        } else {
            me->sim_info.state = SIMCOM_SIM_STATE_PUK_REQUIRED;
        }
        me->state = SIMCOM_STATE_READY;
        me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
        emit_event(me, SIMCOM_EVENT_SIM_PIN_RESULT, SIMCOM_STATUS_ERROR);
        return;
    }

    if ((wf == SIMCOM_WF_INIT) && (me->step_index >= 5U)) {
        /* Optional registration URC enablement on some firmware states may return ERROR if SIM missing; continue init */
        if (me->step_index < 7U) {
            start_init_step(me, (uint8_t)(me->step_index + 1U));
        } else {
            me->state = SIMCOM_STATE_READY;
            me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
            simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_INFO, SIMCOM_LOG_DIR_FSM, "INIT_READY");
            emit_event(me, SIMCOM_EVENT_INIT_COMPLETE, SIMCOM_STATUS_OK);
        }
        return;
    }

    if (wf == SIMCOM_WF_NET_STATUS) {
        if (me->step_index < 5U) {
            start_net_status_step(me, (uint8_t)(me->step_index + 1U));
        } else {
            me->state = SIMCOM_STATE_READY;
            me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
            emit_event(me, SIMCOM_EVENT_NET_STATUS_READY, SIMCOM_STATUS_OK);
        }
        return;
    }

    if (wf == SIMCOM_WF_DEVICE_INFO) {
        advance_device_info_workflow(me);
        return;
    }

    me->fault_code = SIMCOM_FAULT_PROTOCOL_ERROR;
    me->state = SIMCOM_STATE_READY;
    me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
}

static void handle_cmd_timeout(simcom_handle_t * const me)
{
    if (me->state == SIMCOM_STATE_RECOVERY_TIER2) {
        enter_unresponsive_fault(me);
        return;
    }

    if (me->state == SIMCOM_STATE_RECOVERY_TIER1) {
        if (me->tier1_attempts < me->cfg.tier1_sync_retries) {
            me->tier1_attempts++;
            simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_WARNING, SIMCOM_LOG_DIR_FSM, "TIER1_RETRY");
            queue_raw_cmd(me, me->profile->cmd_handshake, me->profile->handshake_timeout_ms, 0U);
        } else {
            enter_tier2_recovery(me);
        }
        return;
    }

    /* Single-shot PIN submission MUST NEVER retry on timeout (FR-004) */
    if (me->active_workflow == (uint8_t)SIMCOM_WF_SIM_PIN) {
        emit_event(me, SIMCOM_EVENT_SIM_PIN_RESULT, SIMCOM_STATUS_ERROR);
        me->active_workflow = (uint8_t)SIMCOM_WF_NONE;
        enter_tier1_recovery(me);
        return;
    }

    if (me->cmd_retries_left > 0U) {
        me->cmd_retries_left--;
        me->tx_offset = 0U;
        me->cmd_start_tick_ms = get_now_ms(me);
        me->at_tx_state = (uint8_t)SIMCOM_AT_TX_STREAMING;
        simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_WARNING, SIMCOM_LOG_DIR_FSM, "CMD_TIMEOUT_RETRY");
        return;
    }

    enter_tier1_recovery(me);
}

void simcom_fsm_step(simcom_handle_t * const me)
{
    if (me == NULL) {
        return;
    }

    /* Check if Tier 2 reset received RDY URC */
    if ((me->state == SIMCOM_STATE_RECOVERY_TIER2) && me->rdy_received) {
        me->rdy_received = false;
        me->state = SIMCOM_STATE_INITIALIZING;
        me->active_workflow = (uint8_t)SIMCOM_WF_INIT;
        simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_INFO, SIMCOM_LOG_DIR_FSM, "RECOVERY_TIER2_RDY_REINIT");
        start_init_step(me, 0U);
    }

    /* Step 1: Stream pending TX command bytes non-blockingly */
    if (me->at_tx_state == (uint8_t)SIMCOM_AT_TX_STREAMING) {
        const size_t rem = me->tx_len - me->tx_offset;
        if (rem > 0U) {
            if (me->tx_offset == 0U) {
                simcom_log_emit(me, SIMCOM_LOG_LEVEL_DEBUG, SIMCOM_LOG_DIR_TX, me->tx_buf, me->tx_len);
            }
            const size_t sent = me->hal.uart_tx_async(&me->tx_buf[me->tx_offset], rem, me->hal.user_ctx);
            if (sent <= rem) {
                me->tx_offset += sent;
            } else {
                me->tx_offset = me->tx_len;
            }
        }
        if (me->tx_offset >= me->tx_len) {
            me->cmd_start_tick_ms = get_now_ms(me);
            me->at_tx_state = (uint8_t)SIMCOM_AT_WAIT_RESPONSE;
        } else {
            /* Check if partial TX streaming itself timed out */
            const uint32_t now = get_now_ms(me);
            if ((now - me->cmd_start_tick_ms) >= me->cmd_timeout_ms) {
                handle_cmd_timeout(me);
            }
        }
        return;
    }

    /* Step 2: Process terminal OK or ERROR if received during lexer poll */
    if (me->at_tx_state == (uint8_t)SIMCOM_AT_COMPLETED_OK) {
        handle_workflow_cmd_ok(me);
        return;
    }

    if (me->at_tx_state == (uint8_t)SIMCOM_AT_COMPLETED_ERROR) {
        handle_workflow_cmd_error(me);
        return;
    }

    /* Step 3: Check response timeout using wrap-around-safe unsigned subtraction (Principle 2) */
    if (me->at_tx_state == (uint8_t)SIMCOM_AT_WAIT_RESPONSE) {
        const uint32_t now = get_now_ms(me);
        if ((now - me->cmd_start_tick_ms) >= me->cmd_timeout_ms) {
            handle_cmd_timeout(me);
        }
    }
}
