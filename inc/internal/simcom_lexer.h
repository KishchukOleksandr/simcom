/**
 * @file simcom_lexer.h
 * @brief Dual-path AT line/raw-byte lexer and URC classifier (Principle 3, FR-010).
 */

#ifndef SIMCOM_LEXER_H
#define SIMCOM_LEXER_H

#include "simcom_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void simcom_lexer_init(simcom_handle_t * const me);
void simcom_lexer_reset_line(simcom_handle_t * const me);
void simcom_lexer_poll(simcom_handle_t * const me);

#ifdef __cplusplus
}
#endif

#endif /* SIMCOM_LEXER_H */
