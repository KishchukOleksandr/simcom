/**
 * @file simcom_lexer.c
 * @brief Dual-path AT line/raw-byte lexer and URC dispatch table implementation.
 *
 * Compliance:
 *   - Principle 1: URC lookup tables in .rodata.
 *   - Principle 3: Decouples asynchronous URCs from active AT command transactions.
 *   - Section 3: Fixed-width types, WCSU <= 128 Bytes.
 */

#include "simcom_lexer.h"
#include "simcom_ringbuf.h"
#include "simcom_parser.h"
#include "simcom_fsm.h"
#include "simcom_log.h"

/* Static .rodata list of standalone / prefix URCs that are always asynchronous */
static const char * const s_async_urc_prefixes[] = {
    "*ATREADY:",
    "SMS DONE",
    "PB DONE",
    "RING",
    "NO CARRIER",
    "+CMTI:",
    "+CIEV:",
    "UNDER-VOLTAGE WARNNING",
    "OVER-VOLTAGE WARNNING"
};

#define SIMCOM_ASYNC_URC_COUNT  (sizeof(s_async_urc_prefixes) / sizeof(s_async_urc_prefixes[0]))

void simcom_lexer_init(simcom_handle_t * const me)
{
    if (me != NULL) {
        me->lexer_mode = SIMCOM_LEXER_MODE_LINE;
        me->line_len = 0U;
        me->raw_bytes_remaining = 0U;
        me->resync_discard_line = false;
    }
}

void simcom_lexer_reset_line(simcom_handle_t * const me)
{
    if (me != NULL) {
        me->line_len = 0U;
        me->resync_discard_line = false;
    }
}

static bool handle_reg_line_or_urc(
    simcom_handle_t * const me,
    const uint8_t * const line,
    size_t len,
    const char * const prefix,
    simcom_reg_state_t * const target_field)
{
    bool is_query_resp = false;
    simcom_reg_state_t parsed_state = SIMCOM_REG_UNKNOWN;

    if (!simcom_parse_xreg_line(line, len, prefix, &is_query_resp, &parsed_state)) {
        return false;
    }

    const simcom_reg_state_t prev_state = *target_field;
    *target_field = parsed_state;

    if (is_query_resp && (me->at_tx_state == (uint8_t)SIMCOM_AT_WAIT_RESPONSE)) {
        simcom_log_emit(me, SIMCOM_LOG_LEVEL_DEBUG, SIMCOM_LOG_DIR_RX, line, len);
        me->cmd_response_matched = true;
    } else {
        /* Asynchronous registration URC (even if another command is active) */
        simcom_log_emit(me, SIMCOM_LOG_LEVEL_INFO, SIMCOM_LOG_DIR_URC, line, len);
        if ((parsed_state != prev_state) && (me->event_cb != NULL)) {
            me->event_cb(SIMCOM_EVENT_NET_REG_URC_CHANGED, SIMCOM_STATUS_OK, me->hal.user_ctx);
        }
    }
    return true;
}

static bool classify_and_dispatch_urc(
    simcom_handle_t * const me,
    const uint8_t * const line,
    size_t len)
{
    /* 1. Check boot ready banner (e.g., "RDY") */
    const char * const rdy_token = ((me->profile != NULL) && (me->profile->boot_ready_urc != NULL))
                                   ? me->profile->boot_ready_urc
                                   : "RDY";
    if (simcom_span_equals(line, len, rdy_token)) {
        me->rdy_received = true;
        simcom_log_emit(me, SIMCOM_LOG_LEVEL_INFO, SIMCOM_LOG_DIR_URC, line, len);
        return true;
    }

    /* 2. Check network registration lines (+CREG:, +CGREG:, +CEREG:) */
    if (handle_reg_line_or_urc(me, line, len, "+CREG:", &me->net_status.cs_reg)) {
        return true;
    }
    if (handle_reg_line_or_urc(me, line, len, "+CGREG:", &me->net_status.ps_reg)) {
        return true;
    }
    if (handle_reg_line_or_urc(me, line, len, "+CEREG:", &me->net_status.eps_reg)) {
        return true;
    }

    /* 3. Check +CPIN: (can be solicited response to AT+CPIN? or async SIM URC) */
    simcom_sim_state_t sim_state = SIMCOM_SIM_STATE_UNKNOWN;
    if (simcom_parse_cpin_resp(line, len, &sim_state)) {
        me->sim_info.state = sim_state;
        if ((me->at_tx_state == (uint8_t)SIMCOM_AT_WAIT_RESPONSE) &&
            (me->active_workflow == (uint8_t)SIMCOM_WF_SIM_STATUS) &&
            (me->step_index == 0U)) {
            simcom_log_emit(me, SIMCOM_LOG_LEVEL_DEBUG, SIMCOM_LOG_DIR_RX, line, len);
            me->cmd_response_matched = true;
        } else {
            simcom_log_emit(me, SIMCOM_LOG_LEVEL_INFO, SIMCOM_LOG_DIR_URC, line, len);
        }
        return true;
    }

    /* 4. Check static .rodata URC prefix table */
    for (size_t i = 0U; i < SIMCOM_ASYNC_URC_COUNT; ++i) {
        if (simcom_span_starts_with(line, len, s_async_urc_prefixes[i])) {
            simcom_log_emit(me, SIMCOM_LOG_LEVEL_INFO, SIMCOM_LOG_DIR_URC, line, len);
            return true;
        }
    }

    return false;
}

static void dispatch_complete_line(simcom_handle_t * const me, const uint8_t * const raw_line, size_t raw_len)
{
    const uint8_t *line = raw_line;
    size_t len = raw_len;
    simcom_span_trim_ws(&line, &len);

    if (len == 0U) {
        return;
    }

    /* 1. Terminal Result Codes */
    if (simcom_span_equals(line, len, "OK")) {
        simcom_log_emit(me, SIMCOM_LOG_LEVEL_DEBUG, SIMCOM_LOG_DIR_RX, line, len);
        simcom_fsm_on_terminal_ok(me);
        return;
    }

    if (simcom_span_equals(line, len, "ERROR")) {
        simcom_log_emit(me, SIMCOM_LOG_LEVEL_WARNING, SIMCOM_LOG_DIR_RX, line, len);
        simcom_fsm_on_terminal_error(me, 0U);
        return;
    }

    uint16_t err_code = 0U;
    if (simcom_parse_cme_error(line, len, &err_code) ||
        simcom_parse_cms_error(line, len, &err_code)) {
        simcom_log_emit(me, SIMCOM_LOG_LEVEL_WARNING, SIMCOM_LOG_DIR_RX, line, len);
        simcom_fsm_on_terminal_error(me, err_code);
        return;
    }

    /* 2. Asynchronous URCs & Dual-Path Registration/SIM Notifications */
    if (classify_and_dispatch_urc(me, line, len)) {
        return;
    }

    /* 3. Solicited Information Responses for the Active Command */
    simcom_fsm_on_solicited_line(me, line, len);
}

void simcom_lexer_poll(simcom_handle_t * const me)
{
    if (me == NULL) {
        return;
    }

    if (me->rx_overflow) {
        me->rx_overflow = false;
        me->resync_discard_line = true;
        me->line_len = 0U;
        simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_WARNING, SIMCOM_LOG_DIR_RX, "RX_OVERFLOW");
    }

    size_t budget = SIMCOM_CFG_RX_BUF_SIZE;
    uint8_t byte_val = 0U;

    while ((budget > 0U) && simcom_ringbuf_pop(me, &byte_val)) {
        budget--;

        if (me->lexer_mode == SIMCOM_LEXER_MODE_RAW_BYTES) {
            if (me->raw_bytes_remaining > 0U) {
                me->raw_bytes_remaining--;
            }
            if (me->raw_bytes_remaining == 0U) {
                me->lexer_mode = SIMCOM_LEXER_MODE_LINE;
            }
            continue;
        }

        if (me->resync_discard_line) {
            if (byte_val == (uint8_t)'\n') {
                me->resync_discard_line = false;
                me->line_len = 0U;
            }
            continue;
        }

        if (byte_val == (uint8_t)'\r') {
            continue;
        }

        if (byte_val == (uint8_t)'\n') {
            if (me->line_len > 0U) {
                me->line_buf[me->line_len] = 0U;
                dispatch_complete_line(me, me->line_buf, me->line_len);
                me->line_len = 0U;
            }
            continue;
        }

        if (me->line_len < (SIMCOM_CFG_LINE_BUF_SIZE - 1U)) {
            me->line_buf[me->line_len] = byte_val;
            me->line_len++;
        } else {
            /* Line exceeded static buffer without CRLF: discard until next line boundary */
            me->resync_discard_line = true;
            me->line_len = 0U;
            simcom_log_emit_cstr(me, SIMCOM_LOG_LEVEL_WARNING, SIMCOM_LOG_DIR_RX, "LINE_OVERFLOW");
        }
    }
}
