/**
 * @file simcom_cfg_contract.h
 * @brief Compile-time configuration contract for libsimcom (001-basic-modem-control).
 *
 * Target location in repository: inc/simcom_cfg.h
 * Compliance: ISO C11, MISRA C:2012, BARR-C:2018, Zero Dynamic Allocation (Principle 1).
 */

#ifndef SIMCOM_CFG_H
#define SIMCOM_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Static Buffer Capacities (Override via compiler -D flags or editing header) */
/* ========================================================================== */

/** @brief RX SPSC ring buffer size in bytes (fed by Simcom_FeedBytes). */
#ifndef SIMCOM_CFG_RX_BUF_SIZE
#define SIMCOM_CFG_RX_BUF_SIZE          (512U)
#endif

/** @brief Maximum single AT response / URC line assembly buffer in bytes. */
#ifndef SIMCOM_CFG_LINE_BUF_SIZE
#define SIMCOM_CFG_LINE_BUF_SIZE        (256U)
#endif

/** @brief Maximum active AT command transmit buffer in bytes. */
#ifndef SIMCOM_CFG_TX_CMD_BUF_SIZE
#define SIMCOM_CFG_TX_CMD_BUF_SIZE      (128U)
#endif

/** @brief Max length (including NUL) for manufacturer, model, revision, operator strings. */
#ifndef SIMCOM_CFG_STR_MAX_LEN
#define SIMCOM_CFG_STR_MAX_LEN          (32U)
#endif

/** @brief Max length (including NUL) for 20-digit SIM ICCID string. */
#ifndef SIMCOM_CFG_ICCID_MAX_LEN
#define SIMCOM_CFG_ICCID_MAX_LEN        (24U)
#endif

/** @brief Max length (including NUL) for 15-digit IMEI serial number string. */
#ifndef SIMCOM_CFG_IMEI_MAX_LEN
#define SIMCOM_CFG_IMEI_MAX_LEN         (20U)
#endif

/** @brief Max length (including NUL) for 4-to-8 digit SIM PIN string. */
#ifndef SIMCOM_CFG_PIN_MAX_LEN
#define SIMCOM_CFG_PIN_MAX_LEN          (9U)
#endif

/* ========================================================================== */
/* Default Operational Timing & Retry Thresholds                              */
/* ========================================================================== */

#ifndef SIMCOM_CFG_DEFAULT_CMD_TIMEOUT_MS
#define SIMCOM_CFG_DEFAULT_CMD_TIMEOUT_MS   (1000U)
#endif

#ifndef SIMCOM_CFG_NET_QUERY_TIMEOUT_MS
#define SIMCOM_CFG_NET_QUERY_TIMEOUT_MS     (5000U)
#endif

#ifndef SIMCOM_CFG_MAX_CMD_RETRIES
#define SIMCOM_CFG_MAX_CMD_RETRIES          (2U)
#endif

#ifndef SIMCOM_CFG_TIER1_SYNC_RETRIES
#define SIMCOM_CFG_TIER1_SYNC_RETRIES       (3U)
#endif

#ifndef SIMCOM_CFG_TIER2_RESET_TIMEOUT_MS
#define SIMCOM_CFG_TIER2_RESET_TIMEOUT_MS   (10000U)
#endif

/* ========================================================================== */
/* Compile-Time Invariant Enforcement (Constitution Principle 1)              */
/* ========================================================================== */

_Static_assert((SIMCOM_CFG_RX_BUF_SIZE >= 128U) && (SIMCOM_CFG_RX_BUF_SIZE <= 4096U),
               "SIMCOM_CFG_RX_BUF_SIZE must be between 128 and 4096 bytes");

_Static_assert((SIMCOM_CFG_LINE_BUF_SIZE >= 64U) && (SIMCOM_CFG_LINE_BUF_SIZE <= 1024U),
               "SIMCOM_CFG_LINE_BUF_SIZE must be between 64 and 1024 bytes");

_Static_assert((SIMCOM_CFG_TX_CMD_BUF_SIZE >= 32U) && (SIMCOM_CFG_TX_CMD_BUF_SIZE <= 512U),
               "SIMCOM_CFG_TX_CMD_BUF_SIZE must be between 32 and 512 bytes");

_Static_assert(SIMCOM_CFG_STR_MAX_LEN >= 24U,
               "SIMCOM_CFG_STR_MAX_LEN must be at least 24 bytes");

_Static_assert(SIMCOM_CFG_ICCID_MAX_LEN >= 21U,
               "SIMCOM_CFG_ICCID_MAX_LEN must hold at least 20 digits + NUL");

_Static_assert(SIMCOM_CFG_IMEI_MAX_LEN >= 16U,
               "SIMCOM_CFG_IMEI_MAX_LEN must hold at least 15 digits + NUL");

_Static_assert(SIMCOM_CFG_PIN_MAX_LEN == 9U,
               "SIMCOM_CFG_PIN_MAX_LEN must be 9 bytes (up to 8 digits + NUL)");

#ifdef __cplusplus
}
#endif

#endif /* SIMCOM_CFG_H */
