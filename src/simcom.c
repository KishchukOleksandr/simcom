/**
 * @file simcom.c
 * @brief Public non-blocking C API implementation for libsimcom (001-basic-modem-control).
 *
 * Compliance:
 *   - Principle 1: Zero dynamic memory allocation.
 *   - Principle 2: Non-blocking O(1)/bounded O(N) execution.
 *   - Section 3: Fixed-width types, WCSU <= 128B (public API), <= 32B (Simcom_FeedBytes).
 */

#include "simcom.h"
#include "simcom_ringbuf.h"
#include "simcom_lexer.h"
#include "simcom_parser.h"
#include "simcom_fsm.h"
#include <string.h>

static bool validate_runtime_cfg(const simcom_runtime_cfg_t * const cfg)
{
    if (cfg == NULL) {
        return false;
    }
    if ((cfg->default_cmd_timeout_ms < 100U) || (cfg->default_cmd_timeout_ms > 180000U)) {
        return false;
    }
    if ((cfg->net_query_timeout_ms < 500U) || (cfg->net_query_timeout_ms > 180000U)) {
        return false;
    }
    if (cfg->max_cmd_retries > 10U) {
        return false;
    }
    if ((cfg->tier1_sync_retries < 1U) || (cfg->tier1_sync_retries > 10U)) {
        return false;
    }
    if ((cfg->tier2_reset_timeout_ms < 1000U) || (cfg->tier2_reset_timeout_ms > 60000U)) {
        return false;
    }
    if ((uint32_t)cfg->min_log_level > (uint32_t)SIMCOM_LOG_LEVEL_NONE) {
        return false;
    }
    return true;
}

simcom_status_t Simcom_Init(simcom_handle_t * const me, const simcom_init_t * const init)
{
    if ((me == NULL) || (init == NULL)) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    if ((init->hal.uart_tx_async == NULL) || (init->hal.get_tick_ms == NULL)) {
        return SIMCOM_STATUS_INVALID_ARG;
    }

    simcom_mem_zero(me, sizeof(*me));

    me->hal = init->hal;
    me->log_fn = init->log_fn;
    me->event_cb = init->event_cb;
    me->profile = (init->profile != NULL) ? init->profile : &g_simcom_profile_a76xx;

    if (init->runtime_cfg != NULL) {
        if (!validate_runtime_cfg(init->runtime_cfg)) {
            return SIMCOM_STATUS_INVALID_ARG;
        }
        me->cfg = *(init->runtime_cfg);
    } else {
        me->cfg.default_cmd_timeout_ms = SIMCOM_CFG_DEFAULT_CMD_TIMEOUT_MS;
        me->cfg.net_query_timeout_ms = SIMCOM_CFG_NET_QUERY_TIMEOUT_MS;
        me->cfg.max_cmd_retries = (uint8_t)SIMCOM_CFG_MAX_CMD_RETRIES;
        me->cfg.tier1_sync_retries = (uint8_t)SIMCOM_CFG_TIER1_SYNC_RETRIES;
        me->cfg.tier2_reset_timeout_ms = SIMCOM_CFG_TIER2_RESET_TIMEOUT_MS;
        me->cfg.min_log_level = SIMCOM_LOG_LEVEL_INFO;
    }

    me->net_status.rssi_raw = 99U;
    me->net_status.ber_raw = 99U;

    simcom_ringbuf_init(me);
    simcom_lexer_init(me);
    simcom_fsm_init(me);

    return SIMCOM_STATUS_OK;
}

simcom_status_t Simcom_SetConfig(simcom_handle_t * const me, const simcom_runtime_cfg_t * const cfg)
{
    if ((me == NULL) || (!validate_runtime_cfg(cfg))) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    me->cfg = *cfg;
    return SIMCOM_STATUS_OK;
}

simcom_status_t Simcom_StartInit(simcom_handle_t * const me)
{
    if (me == NULL) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    if ((me->hal.uart_tx_async == NULL) || (me->hal.get_tick_ms == NULL) || (me->profile == NULL)) {
        return SIMCOM_STATUS_NOT_READY;
    }
    return simcom_fsm_start_workflow(me, SIMCOM_WF_INIT);
}

simcom_status_t Simcom_Poll(simcom_handle_t * const me)
{
    if (me == NULL) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    if (me->state == SIMCOM_STATE_UNINITIALIZED) {
        return SIMCOM_STATUS_NOT_READY;
    }

    simcom_lexer_poll(me);
    simcom_fsm_step(me);
    return SIMCOM_STATUS_OK;
}

simcom_status_t Simcom_FeedBytes(
    simcom_handle_t * const me,
    const uint8_t * const data,
    size_t len,
    size_t * const out_fed)
{
    return simcom_ringbuf_push(me, data, len, out_fed);
}

simcom_lifecycle_state_t Simcom_GetState(const simcom_handle_t * const me)
{
    if (me == NULL) {
        return SIMCOM_STATE_UNINITIALIZED;
    }
    return me->state;
}

simcom_fault_t Simcom_GetFault(const simcom_handle_t * const me)
{
    if (me == NULL) {
        return SIMCOM_FAULT_NONE;
    }
    return me->fault_code;
}

simcom_status_t Simcom_RequestDeviceInfo(simcom_handle_t * const me)
{
    return simcom_fsm_start_workflow(me, SIMCOM_WF_DEVICE_INFO);
}

simcom_status_t Simcom_GetDeviceInfo(
    const simcom_handle_t * const me,
    simcom_device_info_t * const out_info)
{
    if ((me == NULL) || (out_info == NULL)) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    *out_info = me->device_info;
    return me->device_info.is_valid ? SIMCOM_STATUS_OK : SIMCOM_STATUS_NOT_READY;
}

simcom_status_t Simcom_RequestSimStatus(simcom_handle_t * const me)
{
    return simcom_fsm_start_workflow(me, SIMCOM_WF_SIM_STATUS);
}

simcom_status_t Simcom_SubmitSimPin(simcom_handle_t * const me, const char * const pin)
{
    if ((me == NULL) || (pin == NULL)) {
        return SIMCOM_STATUS_INVALID_ARG;
    }

    const size_t pin_len = simcom_cstr_nlen(pin, SIMCOM_CFG_PIN_MAX_LEN);
    if ((pin_len < 4U) || (pin_len > 8U)) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    for (size_t i = 0U; i < pin_len; ++i) {
        if ((pin[i] < '0') || (pin[i] > '9')) {
            return SIMCOM_STATUS_INVALID_ARG;
        }
    }

    if ((me->state == SIMCOM_STATE_INITIALIZING) ||
        (me->state == SIMCOM_STATE_BUSY_WORKFLOW) ||
        (me->state == SIMCOM_STATE_RECOVERY_TIER1) ||
        (me->state == SIMCOM_STATE_RECOVERY_TIER2)) {
        return SIMCOM_STATUS_BUSY;
    }

    if ((me->state != SIMCOM_STATE_READY) ||
        (me->sim_info.state != SIMCOM_SIM_STATE_PIN_REQUIRED)) {
        return SIMCOM_STATUS_NOT_READY;
    }

    simcom_mem_copy(me->pin_scratch, pin, pin_len);
    me->pin_scratch[pin_len] = '\0';
    return simcom_fsm_start_workflow(me, SIMCOM_WF_SIM_PIN);
}

simcom_status_t Simcom_GetSimInfo(
    const simcom_handle_t * const me,
    simcom_sim_info_t * const out_sim)
{
    if ((me == NULL) || (out_sim == NULL)) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    *out_sim = me->sim_info;
    return (me->sim_info.state != SIMCOM_SIM_STATE_UNKNOWN) ? SIMCOM_STATUS_OK : SIMCOM_STATUS_NOT_READY;
}

simcom_status_t Simcom_RequestNetworkStatus(simcom_handle_t * const me)
{
    return simcom_fsm_start_workflow(me, SIMCOM_WF_NET_STATUS);
}

simcom_status_t Simcom_GetNetworkStatus(
    const simcom_handle_t * const me,
    simcom_net_status_t * const out_net)
{
    if ((me == NULL) || (out_net == NULL)) {
        return SIMCOM_STATUS_INVALID_ARG;
    }
    *out_net = me->net_status;
    return SIMCOM_STATUS_OK;
}
