/**
 * @file simcom_profile_a76xx.c
 * @brief Immutable .rodata profile descriptor for SIMCom A76XX series modems (FR-014).
 *
 * Reference: SIMCom A76XX Series AT Command Manual V2.04 (docs/base_cmd.md).
 */

#include "simcom_profile.h"

const simcom_modem_profile_t g_simcom_profile_a76xx = {
    .series_name          = "A76XX",
    .cmd_handshake        = "AT",
    .cmd_echo_off         = "ATE0",
    .cmd_cfun_query       = "AT+CFUN?",
    .cmd_cfun_full        = "AT+CFUN=1",
    .cmd_cmee_numeric     = "AT+CMEE=2",
    .cmd_iccid_query      = "AT+CICCID",
    .cmd_sys_info         = "AT+CPSI?",
    .cmd_logical_reset    = "AT+CRESET",
    .boot_ready_urc       = "RDY",
    .handshake_timeout_ms = 500U,
    .reset_boot_timeout_ms = 10000U
};
