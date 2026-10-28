/**
 * @file simcom_log.c
 * @brief Severity-filtered diagnostic logging implementation (FR-011, SC-006).
 *
 * Compliance: O(1) short-circuit when disabled or below threshold; zero sprintf/printf.
 */

#include "simcom_log.h"

#define SIMCOM_LOG_MAX_CSTR_SCAN    (256U)

void simcom_log_emit(
    const simcom_handle_t * const me,
    simcom_log_level_t level,
    simcom_log_dir_t direction,
    const uint8_t * const payload,
    size_t len)
{
    if ((me == NULL) || (me->log_fn == NULL)) {
        return;
    }
    if ((level >= SIMCOM_LOG_LEVEL_NONE) || (level < me->cfg.min_log_level)) {
        return;
    }

    const uint32_t ts = (me->hal.get_tick_ms != NULL)
                        ? me->hal.get_tick_ms(me->hal.user_ctx)
                        : 0U;
    me->log_fn(level, ts, direction, payload, len, me->hal.user_ctx);
}

void simcom_log_emit_cstr(
    const simcom_handle_t * const me,
    simcom_log_level_t level,
    simcom_log_dir_t direction,
    const char * const cstr)
{
    if ((me == NULL) || (me->log_fn == NULL) || (cstr == NULL)) {
        return;
    }
    if ((level >= SIMCOM_LOG_LEVEL_NONE) || (level < me->cfg.min_log_level)) {
        return;
    }

    size_t len = 0U;
    while ((len < SIMCOM_LOG_MAX_CSTR_SCAN) && (cstr[len] != '\0')) {
        len++;
    }
    simcom_log_emit(me, level, direction, (const uint8_t *)cstr, len);
}
