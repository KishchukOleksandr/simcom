/**
 * @file simcom_log.h
 * @brief Internal severity-filtered diagnostic logging helpers (FR-011).
 */

#ifndef SIMCOM_LOG_H
#define SIMCOM_LOG_H

#include "simcom_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void simcom_log_emit(
    const simcom_handle_t * const me,
    simcom_log_level_t level,
    simcom_log_dir_t direction,
    const uint8_t * const payload,
    size_t len
);

void simcom_log_emit_cstr(
    const simcom_handle_t * const me,
    simcom_log_level_t level,
    simcom_log_dir_t direction,
    const char * const cstr
);

#ifdef __cplusplus
}
#endif

#endif /* SIMCOM_LOG_H */
