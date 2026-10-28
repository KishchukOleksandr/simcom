/**
 * @file simcom_profile.h
 * @brief Modem profile abstraction interface for libsimcom (FR-014).
 *
 * Isolates series-specific AT commands, expected URC tokens, and timeouts in
 * immutable .rodata descriptors so additional SIMCom families can be added
 * without modifying core parser or FSM logic.
 */

#ifndef SIMCOM_PROFILE_H
#define SIMCOM_PROFILE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Immutable series-specific modem profile descriptor (.rodata).
 */
typedef struct {
    const char *series_name;            /**< Series name, e.g. "A76XX" */
    const char *cmd_handshake;          /**< Handshake command, e.g. "AT" */
    const char *cmd_echo_off;           /**< Disable echo command, e.g. "ATE0" */
    const char *cmd_cfun_query;         /**< Query functionality command, e.g. "AT+CFUN?" */
    const char *cmd_cfun_full;          /**< Set full functionality command, e.g. "AT+CFUN=1" */
    const char *cmd_cmee_numeric;       /**< Enable numeric CME errors, e.g. "AT+CMEE=2" */
    const char *cmd_iccid_query;        /**< Read SIM ICCID command, e.g. "AT+CICCID" */
    const char *cmd_sys_info;           /**< Inquire serving cell info, e.g. "AT+CPSI?" */
    const char *cmd_logical_reset;      /**< Tier 2 logical reset command, e.g. "AT+CRESET" */
    const char *boot_ready_urc;         /**< Unsolicited ready banner on reboot, e.g. "RDY" */
    uint32_t handshake_timeout_ms;      /**< Per-probe timeout for AT handshake (ms) */
    uint32_t reset_boot_timeout_ms;     /**< Max wait for RDY after logical reset (ms) */
} simcom_modem_profile_t;

/** @brief Built-in immutable profile descriptor for the SIMCom A76XX series. */
extern const simcom_modem_profile_t g_simcom_profile_a76xx;

#ifdef __cplusplus
}
#endif

#endif /* SIMCOM_PROFILE_H */
