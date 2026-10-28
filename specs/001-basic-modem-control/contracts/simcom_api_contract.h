/**
 * @file simcom_api_contract.h
 * @brief Public non-blocking API contract for libsimcom (001-basic-modem-control).
 *
 * Target location in repository: inc/simcom.h
 * Compliance:
 *   - Principle 1: Zero dynamic memory allocation (caller-allocated simcom_handle_t).
 *   - Principle 2: Strictly non-blocking O(1)/bounded O(N) execution stepped via Simcom_Poll().
 *   - Section 3: Worst-Case Stack Usage <= 128 Bytes (public API), <= 32 Bytes (ISR helper).
 */

#ifndef SIMCOM_H
#define SIMCOM_H

#include "simcom_types_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Lifecycle & Execution Control (FR-001, FR-002, FR-003, FR-013, FR-015)     */
/* ========================================================================== */

/**
 * @brief Initialize a static modem instance handle and bind HAL/logging callbacks.
 *
 * Does not transmit any AT commands yet; call Simcom_StartInit() to begin the
 * asynchronous modem handshake and setup sequence.
 *
 * @param[in,out] me   Caller-allocated modem instance handle.
 * @param[in]     init Initialization descriptor (HAL hooks, optional profile, config, logger).
 * @return SIMCOM_STATUS_OK on success, SIMCOM_STATUS_INVALID_ARG if me or mandatory HAL hooks are NULL.
 */
simcom_status_t Simcom_Init(simcom_handle_t * const me, const simcom_init_t * const init);

/**
 * @brief Update runtime operational timing, retry, and logging threshold parameters.
 *
 * @param[in,out] me  Initialized modem instance handle.
 * @param[in]     cfg New runtime configuration parameters.
 * @return SIMCOM_STATUS_OK on success, SIMCOM_STATUS_INVALID_ARG if parameters are out of bounds.
 */
simcom_status_t Simcom_SetConfig(simcom_handle_t * const me, const simcom_runtime_cfg_t * const cfg);

/**
 * @brief Start the asynchronous modem initialization and configuration sequence.
 *
 * Sequence: AT -> ATE0 -> AT+CFUN? / AT+CFUN=1 -> AT+CMEE=2 -> AT+CREG=1 -> AT+CGREG=1 -> AT+CEREG=1.
 * Escalates through Tier 1 (soft re-sync) and Tier 2 (logical reset) on timeout.
 *
 * @param[in,out] me Initialized modem instance handle.
 * @return SIMCOM_STATUS_PENDING if started, SIMCOM_STATUS_BUSY if a workflow is already active.
 */
simcom_status_t Simcom_StartInit(simcom_handle_t * const me);

/**
 * @brief Non-blocking periodic poll routine.
 *
 * Consumes bytes from the SPSC RX ring buffer, runs the dual-path AT lexer,
 * dispatches asynchronous URCs, advances active command transactions, and
 * steps the master workflow and fault recovery state machines.
 *
 * @param[in,out] me Initialized modem instance handle.
 * @return SIMCOM_STATUS_OK on normal poll step, SIMCOM_STATUS_INVALID_ARG if me is NULL.
 */
simcom_status_t Simcom_Poll(simcom_handle_t * const me);

/**
 * @brief ISR-safe push entry point for feeding received UART bytes into the library.
 *
 * WCSU budget: <= 32 Bytes. Executes in O(len) bounded copy into the SPSC ring buffer.
 * Never parses data or invokes callbacks. On buffer saturation, stores as many bytes
 * as fit, flags rx_overflow for resynchronization on the next line boundary, and
 * returns SIMCOM_STATUS_BUFFER_FULL.
 *
 * @param[in,out] me       Initialized modem instance handle.
 * @param[in]     data     Pointer to received byte buffer.
 * @param[in]     len      Number of bytes to push.
 * @param[out]    out_fed  Optional pointer receiving actual number of bytes written (nullable).
 * @return SIMCOM_STATUS_OK if all bytes stored, SIMCOM_STATUS_BUFFER_FULL on overflow.
 */
simcom_status_t Simcom_FeedBytes(
    simcom_handle_t * const me,
    const uint8_t * const data,
    size_t len,
    size_t * const out_fed
);

/**
 * @brief Query the current lifecycle state and active fault code of the modem instance.
 */
simcom_lifecycle_state_t Simcom_GetState(const simcom_handle_t * const me);
simcom_fault_t Simcom_GetFault(const simcom_handle_t * const me);

/* ========================================================================== */
/* Device Identity & SIM Card Operations (FR-004, FR-005, FR-006)             */
/* ========================================================================== */

/**
 * @brief Start asynchronous query for hardware identity (Manufacturer, Model, Revision, IMEI).
 *
 * Emits SIMCOM_EVENT_DEVICE_INFO_READY on completion; results retrievable via Simcom_GetDeviceInfo().
 */
simcom_status_t Simcom_RequestDeviceInfo(simcom_handle_t * const me);

/**
 * @brief Copy cached device identity metadata into caller-supplied struct.
 */
simcom_status_t Simcom_GetDeviceInfo(
    const simcom_handle_t * const me,
    simcom_device_info_t * const out_info
);

/**
 * @brief Start asynchronous SIM status check (AT+CPIN?) and ICCID read (AT+CICCID if READY).
 *
 * Emits SIMCOM_EVENT_SIM_STATUS_READY on completion; results retrievable via Simcom_GetSimInfo().
 */
simcom_status_t Simcom_RequestSimStatus(simcom_handle_t * const me);

/**
 * @brief Submit SIM PIN password explicitly (single attempt, NEVER retried automatically).
 *
 * Valid only when SIM state is SIMCOM_SIM_STATE_PIN_REQUIRED.
 * Clears internal PIN scratchpad immediately after transmitting AT+CPIN="<pin>".
 *
 * @param[in,out] me  Initialized modem instance handle in READY state.
 * @param[in]     pin NUL-terminated numeric PIN string (4 to 8 digits, '0'..'9').
 * @return SIMCOM_STATUS_PENDING if accepted, SIMCOM_STATUS_INVALID_ARG if PIN format invalid,
 *         SIMCOM_STATUS_NOT_READY if SIM is not in PIN_REQUIRED state.
 */
simcom_status_t Simcom_SubmitSimPin(simcom_handle_t * const me, const char * const pin);

/**
 * @brief Copy cached SIM card status and ICCID into caller-supplied struct.
 */
simcom_status_t Simcom_GetSimInfo(
    const simcom_handle_t * const me,
    simcom_sim_info_t * const out_sim
);

/* ========================================================================== */
/* Network Registration, Signal Quality & Operator Queries (FR-007..FR-009)   */
/* ========================================================================== */

/**
 * @brief Start asynchronous query for registration (CREG/CGREG/CEREG), signal (CSQ),
 *        operator (COPS?), and serving cell system information (CPSI?).
 *
 * Emits SIMCOM_EVENT_NET_STATUS_READY on completion; results retrievable via Simcom_GetNetworkStatus().
 */
simcom_status_t Simcom_RequestNetworkStatus(simcom_handle_t * const me);

/**
 * @brief Copy cached network registration, signal quality, and operator info into caller struct.
 *
 * Note: cs_reg, ps_reg, and eps_reg are also updated asynchronously whenever
 * +CREG, +CGREG, or +CEREG URCs arrive during Simcom_Poll().
 */
simcom_status_t Simcom_GetNetworkStatus(
    const simcom_handle_t * const me,
    simcom_net_status_t * const out_net
);

#ifdef __cplusplus
}
#endif

#endif /* SIMCOM_H */
