/**
 * @file simcom_ringbuf.c
 * @brief Lock-free SPSC byte ring buffer implementation.
 *
 * Compliance: Principle 1 (Static memory), Principle 3 (ISR-safe lock-free SPSC),
 * Section 3 (WCSU <= 32 Bytes).
 */

#include "simcom_ringbuf.h"

void simcom_ringbuf_init(simcom_handle_t * const me)
{
    if (me != NULL) {
        me->rx_head = 0U;
        me->rx_tail = 0U;
        me->rx_overflow = false;
    }
}

void simcom_ringbuf_reset(simcom_handle_t * const me)
{
    if (me != NULL) {
        me->rx_tail = me->rx_head;
        me->rx_overflow = false;
    }
}

simcom_status_t simcom_ringbuf_push(
    simcom_handle_t * const me,
    const uint8_t * const data,
    size_t len,
    size_t * const out_fed)
{
    if ((me == NULL) || ((len > 0U) && (data == NULL))) {
        if (out_fed != NULL) {
            *out_fed = 0U;
        }
        return SIMCOM_STATUS_INVALID_ARG;
    }

    size_t head = me->rx_head;
    const size_t tail = me->rx_tail;
    size_t written = 0U;
    simcom_status_t status = SIMCOM_STATUS_OK;

    while (written < len) {
        const size_t next_head = (head + 1U) % SIMCOM_CFG_RX_BUF_SIZE;
        if (next_head == tail) {
            me->rx_overflow = true;
            status = SIMCOM_STATUS_BUFFER_FULL;
            break;
        }
        me->rx_buf[head] = data[written];
        head = next_head;
        written++;
    }

    me->rx_head = head;
    if (out_fed != NULL) {
        *out_fed = written;
    }
    return status;
}

bool simcom_ringbuf_pop(simcom_handle_t * const me, uint8_t * const out_byte)
{
    if ((me == NULL) || (out_byte == NULL)) {
        return false;
    }

    const size_t head = me->rx_head;
    const size_t tail = me->rx_tail;
    if (head == tail) {
        return false;
    }

    *out_byte = me->rx_buf[tail];
    me->rx_tail = (tail + 1U) % SIMCOM_CFG_RX_BUF_SIZE;
    return true;
}

size_t simcom_ringbuf_count(const simcom_handle_t * const me)
{
    if (me == NULL) {
        return 0U;
    }
    const size_t head = me->rx_head;
    const size_t tail = me->rx_tail;
    if (head >= tail) {
        return head - tail;
    }
    return (SIMCOM_CFG_RX_BUF_SIZE - tail) + head;
}

size_t simcom_ringbuf_free_space(const simcom_handle_t * const me)
{
    if (me == NULL) {
        return 0U;
    }
    return (SIMCOM_CFG_RX_BUF_SIZE - 1U) - simcom_ringbuf_count(me);
}
