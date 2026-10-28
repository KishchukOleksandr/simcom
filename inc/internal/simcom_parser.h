/**
 * @file simcom_parser.h
 * @brief Defensive bounds-checked span tokenizers and AT response parsers (Principle 5).
 *
 * Compliance:
 *   - Principle 5: No strcpy, strcat, sprintf, sscanf, gets, or atoi.
 *   - Operates exclusively on explicit (const uint8_t *buf, size_t len) spans.
 */

#ifndef SIMCOM_PARSER_H
#define SIMCOM_PARSER_H

#include "simcom_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Low-Level Span & String Primitives                                         */
/* ========================================================================== */

void simcom_mem_zero(void * const dst, size_t len);
void simcom_mem_copy(void * const dst, const void * const src, size_t len);
size_t simcom_cstr_nlen(const char * const str, size_t max_len);
bool simcom_span_starts_with(const uint8_t * const buf, size_t len, const char * const prefix);
bool simcom_span_equals(const uint8_t * const buf, size_t len, const char * const target);
void simcom_span_trim_ws(const uint8_t ** const p_buf, size_t * const p_len);

bool simcom_span_next_token(
    const uint8_t ** const p_cursor,
    size_t * const p_rem,
    char delim,
    const uint8_t ** const out_tok,
    size_t * const out_tok_len
);

bool simcom_span_copy_to_cstr(
    const uint8_t * const tok,
    size_t tok_len,
    char * const out_str,
    size_t out_max
);

bool simcom_span_unquote_to_cstr(
    const uint8_t * const tok,
    size_t tok_len,
    char * const out_str,
    size_t out_max
);

bool simcom_span_parse_u32(
    const uint8_t * const tok,
    size_t tok_len,
    uint32_t * const out_val
);

bool simcom_span_parse_i32(
    const uint8_t * const tok,
    size_t tok_len,
    int32_t * const out_val
);

/* ========================================================================== */
/* Terminal & Error Code Parsers                                              */
/* ========================================================================== */

bool simcom_parse_cme_error(const uint8_t * const line, size_t len, uint16_t * const out_code);
bool simcom_parse_cms_error(const uint8_t * const line, size_t len, uint16_t * const out_code);

/* ========================================================================== */
/* Domain AT Response & URC Parsers                                           */
/* ========================================================================== */

bool simcom_parse_cfun_resp(const uint8_t * const line, size_t len, uint8_t * const out_fun);
bool simcom_parse_cpin_resp(const uint8_t * const line, size_t len, simcom_sim_state_t * const out_state);
simcom_sim_state_t simcom_map_cme_to_sim_state(uint16_t cme_code);
bool simcom_parse_iccid_resp(const uint8_t * const line, size_t len, char * const out_iccid, size_t out_max);

bool simcom_parse_ati_line(const uint8_t * const line, size_t len, simcom_device_info_t * const info);
bool simcom_parse_digits_line(const uint8_t * const line, size_t len, char * const out_str, size_t out_max);

bool simcom_parse_csq_resp(
    const uint8_t * const line,
    size_t len,
    uint8_t * const out_rssi,
    int16_t * const out_dbm,
    uint8_t * const out_ber
);

bool simcom_parse_xreg_line(
    const uint8_t * const line,
    size_t len,
    const char * const prefix,
    bool * const out_is_query_resp,
    simcom_reg_state_t * const out_stat
);

bool simcom_parse_cops_resp(
    const uint8_t * const line,
    size_t len,
    char * const out_oper,
    size_t oper_max,
    simcom_act_t * const out_act
);

bool simcom_parse_cpsi_resp(
    const uint8_t * const line,
    size_t len,
    char * const out_sys_mode,
    size_t mode_max,
    simcom_act_t * const out_act
);

#ifdef __cplusplus
}
#endif

#endif /* SIMCOM_PARSER_H */
