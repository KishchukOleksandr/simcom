/**
 * @file simcom_fsm.h
 * @brief Non-blocking AT transaction engine, workflow steppers, and Tier 1/2 recovery FSM.
 */

#ifndef SIMCOM_FSM_H
#define SIMCOM_FSM_H

#include "simcom_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIMCOM_WF_NONE = 0,
    SIMCOM_WF_INIT = 1,
    SIMCOM_WF_DEVICE_INFO = 2,
    SIMCOM_WF_SIM_STATUS = 3,
    SIMCOM_WF_SIM_PIN = 4,
    SIMCOM_WF_NET_STATUS = 5
} simcom_workflow_id_t;

typedef enum {
    SIMCOM_AT_IDLE = 0,
    SIMCOM_AT_TX_STREAMING = 1,
    SIMCOM_AT_WAIT_RESPONSE = 2,
    SIMCOM_AT_COMPLETED_OK = 3,
    SIMCOM_AT_COMPLETED_ERROR = 4
} simcom_at_tx_state_t;

void simcom_fsm_init(simcom_handle_t * const me);
simcom_status_t simcom_fsm_start_workflow(simcom_handle_t * const me, simcom_workflow_id_t wf);
void simcom_fsm_step(simcom_handle_t * const me);

void simcom_fsm_on_solicited_line(simcom_handle_t * const me, const uint8_t * const line, size_t len);
void simcom_fsm_on_terminal_ok(simcom_handle_t * const me);
void simcom_fsm_on_terminal_error(simcom_handle_t * const me, uint16_t cme_code);

#ifdef __cplusplus
}
#endif

#endif /* SIMCOM_FSM_H */
