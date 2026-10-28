/**
 * @file simcom_parser.c
 * @brief Defensive bounds-checked span tokenizers and AT response parsers.
 *
 * Compliance:
 *   - Principle 5: Zero unsafe <string.h> / <stdio.h> routines; explicit spans only.
 *   - Section 3: Fixed-width types only; WCSU <= 128 Bytes.
 */

#include "simcom_parser.h"
#include <limits.h>
#include <string.h>

#define SIMCOM_MAX_CSTR_SCAN    (256U)

static bool is_ws_byte(uint8_t ch)
{
    return (ch == (uint8_t)' ') || (ch == (uint8_t)'\t') ||
           (ch == (uint8_t)'\r') || (ch == (uint8_t)'\n');
}

static bool is_digit_byte(uint8_t ch)
{
    return (ch >= (uint8_t)'0') && (ch <= (uint8_t)'9');
}

void simcom_mem_zero(void * const dst, size_t len)
{
    if (dst == NULL) {
        return;
    }
    uint8_t * const bytes = (uint8_t *)dst;
    for (size_t i = 0U; i < len; ++i) {
        bytes[i] = 0U;
    }
}

void simcom_mem_copy(void * const dst, const void * const src, size_t len)
{
    if ((dst == NULL) || (src == NULL)) {
        return;
    }
    uint8_t * const d = (uint8_t *)dst;
    const uint8_t * const s = (const uint8_t *)src;
    for (size_t i = 0U; i < len; ++i) {
        d[i] = s[i];
    }
}

size_t simcom_cstr_nlen(const char * const str, size_t max_len)
{
    if (str == NULL) {
        return 0U;
    }
    size_t len = 0U;
    while ((len < max_len) && (str[len] != '\0')) {
        len++;
    }
    return len;
}

bool simcom_span_starts_with(const uint8_t * const buf, size_t len, const char * const prefix)
{
    if ((buf == NULL) || (prefix == NULL)) {
        return false;
    }
    const size_t plen = simcom_cstr_nlen(prefix, SIMCOM_MAX_CSTR_SCAN);
    if ((plen == 0U) || (len < plen)) {
        return false;
    }
    return (memcmp(buf, prefix, plen) == 0);
}

bool simcom_span_equals(const uint8_t * const buf, size_t len, const char * const target)
{
    if ((buf == NULL) || (target == NULL)) {
        return false;
    }
    const size_t tlen = simcom_cstr_nlen(target, SIMCOM_MAX_CSTR_SCAN);
    if (len != tlen) {
        return false;
    }
    return (tlen == 0U) ? true : (memcmp(buf, target, tlen) == 0);
}

void simcom_span_trim_ws(const uint8_t ** const p_buf, size_t * const p_len)
{
    if ((p_buf == NULL) || (*p_buf == NULL) || (p_len == NULL)) {
        return;
    }
    const uint8_t *ptr = *p_buf;
    size_t len = *p_len;

    while ((len > 0U) && is_ws_byte(ptr[0])) {
        ptr++;
        len--;
    }
    while ((len > 0U) && is_ws_byte(ptr[len - 1U])) {
        len--;
    }

    *p_buf = ptr;
    *p_len = len;
}

bool simcom_span_next_token(
    const uint8_t ** const p_cursor,
    size_t * const p_rem,
    char delim,
    const uint8_t ** const out_tok,
    size_t * const out_tok_len)
{
    if ((p_cursor == NULL) || (*p_cursor == NULL) || (p_rem == NULL) ||
        (out_tok == NULL) || (out_tok_len == NULL)) {
        return false;
    }

    const uint8_t * const start = *p_cursor;
    const size_t rem = *p_rem;
    if (rem == 0U) {
        return false;
    }

    bool in_quotes = false;
    size_t idx = 0U;
    while (idx < rem) {
        const uint8_t ch = start[idx];
        if (ch == (uint8_t)'"') {
            in_quotes = !in_quotes;
        } else if ((!in_quotes) && (ch == (uint8_t)delim)) {
            break;
        } else {
            /* Continue scanning */
        }
        idx++;
    }

    const uint8_t *tok_ptr = start;
    size_t tok_len = idx;
    simcom_span_trim_ws(&tok_ptr, &tok_len);

    *out_tok = tok_ptr;
    *out_tok_len = tok_len;

    if (idx < rem) {
        *p_cursor = &start[idx + 1U];
        *p_rem = rem - (idx + 1U);
    } else {
        *p_cursor = &start[rem];
        *p_rem = 0U;
    }
    return true;
}

bool simcom_span_copy_to_cstr(
    const uint8_t * const tok,
    size_t tok_len,
    char * const out_str,
    size_t out_max)
{
    if ((out_str == NULL) || (out_max == 0U)) {
        return false;
    }
    if ((tok == NULL) && (tok_len > 0U)) {
        out_str[0] = '\0';
        return false;
    }

    const uint8_t *trimmed = tok;
    size_t trimmed_len = tok_len;
    if (trimmed != NULL) {
        simcom_span_trim_ws(&trimmed, &trimmed_len);
    }

    const size_t copy_len = (trimmed_len < (out_max - 1U)) ? trimmed_len : (out_max - 1U);
    if ((copy_len > 0U) && (trimmed != NULL)) {
        simcom_mem_copy(out_str, trimmed, copy_len);
    }
    out_str[copy_len] = '\0';
    return (trimmed_len < out_max);
}

bool simcom_span_unquote_to_cstr(
    const uint8_t * const tok,
    size_t tok_len,
    char * const out_str,
    size_t out_max)
{
    if ((tok == NULL) || (out_str == NULL) || (out_max == 0U)) {
        return false;
    }
    const uint8_t *ptr = tok;
    size_t len = tok_len;
    simcom_span_trim_ws(&ptr, &len);

    if ((len >= 2U) && (ptr[0] == (uint8_t)'"') && (ptr[len - 1U] == (uint8_t)'"')) {
        ptr++;
        len -= 2U;
    }
    return simcom_span_copy_to_cstr(ptr, len, out_str, out_max);
}

bool simcom_span_parse_u32(
    const uint8_t * const tok,
    size_t tok_len,
    uint32_t * const out_val)
{
    if ((tok == NULL) || (out_val == NULL)) {
        return false;
    }
    const uint8_t *ptr = tok;
    size_t len = tok_len;
    simcom_span_trim_ws(&ptr, &len);

    if (len == 0U) {
        return false;
    }

    uint32_t acc = 0U;
    for (size_t i = 0U; i < len; ++i) {
        if (!is_digit_byte(ptr[i])) {
            return false;
        }
        const uint32_t digit = (uint32_t)(ptr[i] - (uint8_t)'0');
        if (acc > ((UINT32_MAX - digit) / 10U)) {
            return false;
        }
        acc = (acc * 10U) + digit;
    }

    *out_val = acc;
    return true;
}

bool simcom_span_parse_i32(
    const uint8_t * const tok,
    size_t tok_len,
    int32_t * const out_val)
{
    if ((tok == NULL) || (out_val == NULL)) {
        return false;
    }
    const uint8_t *ptr = tok;
    size_t len = tok_len;
    simcom_span_trim_ws(&ptr, &len);

    if (len == 0U) {
        return false;
    }

    bool negative = false;
    if (ptr[0] == (uint8_t)'-') {
        negative = true;
        ptr++;
        len--;
    } else if (ptr[0] == (uint8_t)'+') {
        ptr++;
        len--;
    } else {
        /* Unsigned prefix */
    }

    uint32_t mag = 0U;
    if (!simcom_span_parse_u32(ptr, len, &mag)) {
        return false;
    }

    if (negative) {
        if (mag > 2147483648UL) {
            return false;
        }
        if (mag == 2147483648UL) {
            *out_val = INT32_MIN;
        } else {
            *out_val = -(int32_t)mag;
        }
    } else {
        if (mag > (uint32_t)INT32_MAX) {
            return false;
        }
        *out_val = (int32_t)mag;
    }
    return true;
}

static bool parse_prefixed_error_code(
    const uint8_t * const line,
    size_t len,
    const char * const prefix,
    uint16_t * const out_code)
{
    if ((line == NULL) || (prefix == NULL) || (out_code == NULL)) {
        return false;
    }
    if (!simcom_span_starts_with(line, len, prefix)) {
        return false;
    }
    const size_t plen = simcom_cstr_nlen(prefix, SIMCOM_MAX_CSTR_SCAN);
    const uint8_t *rest = &line[plen];
    size_t rest_len = len - plen;
    simcom_span_trim_ws(&rest, &rest_len);

    uint32_t val = 0U;
    if (simcom_span_parse_u32(rest, rest_len, &val) && (val <= (uint32_t)UINT16_MAX)) {
        *out_code = (uint16_t)val;
    } else {
        /* Textual CME/CMS error string when AT+CMEE=2 is not yet active */
        *out_code = 0xFFFFU;
    }
    return true;
}

bool simcom_parse_cme_error(const uint8_t * const line, size_t len, uint16_t * const out_code)
{
    return parse_prefixed_error_code(line, len, "+CME ERROR:", out_code);
}

bool simcom_parse_cms_error(const uint8_t * const line, size_t len, uint16_t * const out_code)
{
    return parse_prefixed_error_code(line, len, "+CMS ERROR:", out_code);
}

bool simcom_parse_cfun_resp(const uint8_t * const line, size_t len, uint8_t * const out_fun)
{
    if ((line == NULL) || (out_fun == NULL)) {
        return false;
    }
    if (!simcom_span_starts_with(line, len, "+CFUN:")) {
        return false;
    }
    const uint8_t *cursor = &line[6U];
    size_t rem = len - 6U;
    const uint8_t *tok = NULL;
    size_t tok_len = 0U;
    uint32_t val = 0U;

    if (!simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len)) {
        return false;
    }
    if (!simcom_span_parse_u32(tok, tok_len, &val) || (val > 255U)) {
        return false;
    }
    *out_fun = (uint8_t)val;
    return true;
}

bool simcom_parse_cpin_resp(const uint8_t * const line, size_t len, simcom_sim_state_t * const out_state)
{
    if ((line == NULL) || (out_state == NULL)) {
        return false;
    }
    if (!simcom_span_starts_with(line, len, "+CPIN:")) {
        return false;
    }
    const uint8_t *code = &line[6U];
    size_t code_len = len - 6U;
    simcom_span_trim_ws(&code, &code_len);

    if (simcom_span_equals(code, code_len, "READY")) {
        *out_state = SIMCOM_SIM_STATE_READY;
    } else if (simcom_span_equals(code, code_len, "SIM PIN")) {
        *out_state = SIMCOM_SIM_STATE_PIN_REQUIRED;
    } else if (simcom_span_equals(code, code_len, "SIM PUK")) {
        *out_state = SIMCOM_SIM_STATE_PUK_REQUIRED;
    } else if (simcom_span_equals(code, code_len, "SIM CRASH") ||
               simcom_span_equals(code, code_len, "NOT INSERTED")) {
        *out_state = simcom_span_equals(code, code_len, "NOT INSERTED")
                     ? SIMCOM_SIM_STATE_ABSENT
                     : SIMCOM_SIM_STATE_FAULTY;
    } else {
        *out_state = SIMCOM_SIM_STATE_FAULTY;
    }
    return true;
}

simcom_sim_state_t simcom_map_cme_to_sim_state(uint16_t cme_code)
{
    switch (cme_code) {
        case 10U: /* SIM not inserted */
            return SIMCOM_SIM_STATE_ABSENT;
        case 11U: /* SIM PIN required */
            return SIMCOM_SIM_STATE_PIN_REQUIRED;
        case 12U: /* SIM PUK required */
        case 16U: /* Incorrect password */
            return SIMCOM_SIM_STATE_PUK_REQUIRED;
        case 13U: /* SIM failure */
        case 15U: /* SIM wrong */
        default:
            return SIMCOM_SIM_STATE_FAULTY;
    }
}

bool simcom_parse_iccid_resp(const uint8_t * const line, size_t len, char * const out_iccid, size_t out_max)
{
    if ((line == NULL) || (out_iccid == NULL) || (out_max == 0U)) {
        return false;
    }
    size_t plen = 0U;
    if (simcom_span_starts_with(line, len, "+ICCID:")) {
        plen = 7U;
    } else if (simcom_span_starts_with(line, len, "+CICCID:")) {
        plen = 8U;
    } else {
        return false;
    }

    const uint8_t *val = &line[plen];
    size_t val_len = len - plen;
    simcom_span_trim_ws(&val, &val_len);
    if ((val_len < 10U) || (val_len >= out_max)) {
        return false;
    }
    return simcom_span_unquote_to_cstr(val, val_len, out_iccid, out_max);
}

bool simcom_parse_ati_line(const uint8_t * const line, size_t len, simcom_device_info_t * const info)
{
    if ((line == NULL) || (info == NULL)) {
        return false;
    }

    if (simcom_span_starts_with(line, len, "Manufacturer:")) {
        return simcom_span_copy_to_cstr(&line[13U], len - 13U, info->manufacturer, sizeof(info->manufacturer));
    }
    if (simcom_span_starts_with(line, len, "Model:")) {
        return simcom_span_copy_to_cstr(&line[6U], len - 6U, info->model, sizeof(info->model));
    }
    if (simcom_span_starts_with(line, len, "Revision:")) {
        return simcom_span_copy_to_cstr(&line[9U], len - 9U, info->revision, sizeof(info->revision));
    }
    if (simcom_span_starts_with(line, len, "IMEI:")) {
        return simcom_parse_digits_line(&line[5U], len - 5U, info->imei, sizeof(info->imei));
    }
    return false;
}

bool simcom_parse_digits_line(const uint8_t * const line, size_t len, char * const out_str, size_t out_max)
{
    if ((line == NULL) || (out_str == NULL) || (out_max == 0U)) {
        return false;
    }
    const uint8_t *ptr = line;
    size_t plen = len;
    simcom_span_trim_ws(&ptr, &plen);

    if ((plen < 14U) || (plen >= out_max)) {
        return false;
    }
    for (size_t i = 0U; i < plen; ++i) {
        if (!is_digit_byte(ptr[i])) {
            return false;
        }
    }
    return simcom_span_copy_to_cstr(ptr, plen, out_str, out_max);
}

bool simcom_parse_csq_resp(
    const uint8_t * const line,
    size_t len,
    uint8_t * const out_rssi,
    int16_t * const out_dbm,
    uint8_t * const out_ber)
{
    if ((line == NULL) || (out_rssi == NULL) || (out_dbm == NULL) || (out_ber == NULL)) {
        return false;
    }
    if (!simcom_span_starts_with(line, len, "+CSQ:")) {
        return false;
    }

    const uint8_t *cursor = &line[5U];
    size_t rem = len - 5U;
    const uint8_t *tok_rssi = NULL;
    size_t len_rssi = 0U;
    const uint8_t *tok_ber = NULL;
    size_t len_ber = 0U;

    if (!simcom_span_next_token(&cursor, &rem, ',', &tok_rssi, &len_rssi)) {
        return false;
    }
    if (!simcom_span_next_token(&cursor, &rem, ',', &tok_ber, &len_ber)) {
        return false;
    }

    uint32_t rssi_val = 0U;
    uint32_t ber_val = 0U;
    if (!simcom_span_parse_u32(tok_rssi, len_rssi, &rssi_val) ||
        !simcom_span_parse_u32(tok_ber, len_ber, &ber_val)) {
        return false;
    }
    if (((rssi_val > 31U) && (rssi_val != 99U)) ||
        ((ber_val > 7U) && (ber_val != 99U))) {
        return false;
    }

    *out_rssi = (uint8_t)rssi_val;
    *out_ber = (uint8_t)ber_val;
    if (rssi_val <= 31U) {
        *out_dbm = (int16_t)(-113 + (2 * (int16_t)rssi_val));
    } else {
        *out_dbm = 0;
    }
    return true;
}

static simcom_reg_state_t map_u32_to_reg_state(uint32_t stat_val)
{
    switch (stat_val) {
        case 0U: return SIMCOM_REG_NOT_REGISTERED;
        case 1U: return SIMCOM_REG_HOME;
        case 2U: return SIMCOM_REG_SEARCHING;
        case 3U: return SIMCOM_REG_DENIED;
        case 5U: return SIMCOM_REG_ROAMING;
        case 4U:
        default: return SIMCOM_REG_UNKNOWN;
    }
}

bool simcom_parse_xreg_line(
    const uint8_t * const line,
    size_t len,
    const char * const prefix,
    bool * const out_is_query_resp,
    simcom_reg_state_t * const out_stat)
{
    if ((line == NULL) || (prefix == NULL) || (out_is_query_resp == NULL) || (out_stat == NULL)) {
        return false;
    }
    if (!simcom_span_starts_with(line, len, prefix)) {
        return false;
    }

    const size_t plen = simcom_cstr_nlen(prefix, SIMCOM_MAX_CSTR_SCAN);
    const uint8_t *cursor = &line[plen];
    size_t rem = len - plen;

    const uint8_t *tok = NULL;
    size_t tok_len = 0U;
    uint32_t val1 = 0U;
    if (!simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len) ||
        !simcom_span_parse_u32(tok, tok_len, &val1)) {
        return false;
    }

    /* Check if a second numeric token <stat> follows (solicited query: +CREG: <n>,<stat>[,<lac>,<ci>]) */
    uint32_t val2 = 0U;
    if (simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len) &&
        simcom_span_parse_u32(tok, tok_len, &val2) &&
        (val2 <= 5U)) {
        *out_is_query_resp = true;
        *out_stat = map_u32_to_reg_state(val2);
        return true;
    }

    /* Otherwise, val1 is <stat> from an unsolicited URC (+CREG: <stat>[,<lac>,<ci>]) */
    if (val1 <= 5U) {
        *out_is_query_resp = false;
        *out_stat = map_u32_to_reg_state(val1);
        return true;
    }
    return false;
}

bool simcom_parse_cops_resp(
    const uint8_t * const line,
    size_t len,
    char * const out_oper,
    size_t oper_max,
    simcom_act_t * const out_act)
{
    if ((line == NULL) || (out_oper == NULL) || (oper_max == 0U) || (out_act == NULL)) {
        return false;
    }
    if (!simcom_span_starts_with(line, len, "+COPS:")) {
        return false;
    }

    const uint8_t *cursor = &line[6U];
    size_t rem = len - 6U;
    const uint8_t *tok = NULL;
    size_t tok_len = 0U;

    /* Token 1: <mode> */
    if (!simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len)) {
        return false;
    }
    uint32_t mode = 0U;
    if (!simcom_span_parse_u32(tok, tok_len, &mode)) {
        return false;
    }

    out_oper[0] = '\0';
    *out_act = SIMCOM_ACT_UNKNOWN;

    /* Token 2: <format> (optional if not registered) */
    if (!simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len)) {
        return true;
    }

    /* Token 3: <oper> */
    if (!simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len)) {
        return false;
    }
    if (!simcom_span_unquote_to_cstr(tok, tok_len, out_oper, oper_max)) {
        return false;
    }

    /* Token 4: <AcT> (optional) */
    if (simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len)) {
        uint32_t act_val = 0U;
        if (simcom_span_parse_u32(tok, tok_len, &act_val)) {
            if (act_val == 0U) {
                *out_act = SIMCOM_ACT_GSM;
            } else if (act_val == 2U) {
                *out_act = SIMCOM_ACT_UTRAN;
            } else if (act_val == 7U) {
                *out_act = SIMCOM_ACT_LTE;
            } else {
                *out_act = SIMCOM_ACT_UNKNOWN;
            }
        }
    }
    return true;
}

bool simcom_parse_cpsi_resp(
    const uint8_t * const line,
    size_t len,
    char * const out_sys_mode,
    size_t mode_max,
    simcom_act_t * const out_act)
{
    if ((line == NULL) || (out_sys_mode == NULL) || (mode_max == 0U) || (out_act == NULL)) {
        return false;
    }
    if (!simcom_span_starts_with(line, len, "+CPSI:")) {
        return false;
    }

    const uint8_t *cursor = &line[6U];
    size_t rem = len - 6U;
    const uint8_t *tok = NULL;
    size_t tok_len = 0U;

    if (!simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len)) {
        return false;
    }
    if (!simcom_span_copy_to_cstr(tok, tok_len, out_sys_mode, mode_max)) {
        return false;
    }

    if (simcom_span_equals(tok, tok_len, "LTE")) {
        *out_act = SIMCOM_ACT_LTE;
    } else if (simcom_span_equals(tok, tok_len, "GSM")) {
        *out_act = SIMCOM_ACT_GSM;
    } else if (simcom_span_equals(tok, tok_len, "WCDMA") ||
               simcom_span_equals(tok, tok_len, "UTRAN")) {
        *out_act = SIMCOM_ACT_UTRAN;
    } else {
        /* Keep existing or unknown if NO SERVICE */
    }
    return true;
}
