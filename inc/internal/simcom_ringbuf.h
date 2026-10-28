/**
 * @file simcom_ringbuf.h
 * @brief Lock-free Single-Producer Single-Consumer (SPSC) RX ring buffer helpers.
 *
 * Producer: Simcom_FeedBytes() (ISR / DMA context)
 * Consumer: Simcom_Poll() (Background superloop context)
 */

#ifndef SIMCOM_RINGBUF_H
#define SIMCOM_RINGBUF_H

#include "simcom_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void simcom_ringbuf_init(simcom_handle_t * const me);
void simcom_ringbuf_reset(simcom_handle_t * const me);
simcom_status_t simcom_ringbuf_push(
    simcom_handle_t * const me,
    const uint8_t * const data,
    size_t len,
    size_t * const out_fed
);
bool simcom_ringbuf_pop(simcom_handle_t * const me, uint8_t * const out_byte);
size_t simcom_ringbuf_count(const simcom_handle_t * const me);
size_t simcom_ringbuf_free_space(const simcom_handle_t * const me);

#ifdef __cplusplus
}
#endif

#endif /* SIMCOM_RINGBUF_H */
