/**
 * @file test_ringbuf.c
 * @brief Unit tests for ISR-safe lock-free SPSC RX ring buffer (T014).
 */

#include "unity.h"
#include "mock_simcom_hal.h"
#include "simcom_ringbuf.h"

static simcom_handle_t s_modem;
static mock_hal_ctx_t s_hal_ctx;

void setUp(void)
{
    mock_hal_init(&s_hal_ctx);
    simcom_init_t init = {
        .hal = mock_hal_make_if(&s_hal_ctx),
        .profile = NULL,
        .runtime_cfg = NULL,
        .log_fn = NULL,
        .event_cb = NULL
    };
    (void)Simcom_Init(&s_modem, &init);
}

void tearDown(void)
{
}

static void test_ringbuf_initial_state_is_empty(void)
{
    uint8_t byte_out = 0U;
    TEST_ASSERT_EQUAL_UINT(0U, simcom_ringbuf_count(&s_modem));
    TEST_ASSERT_EQUAL_UINT(SIMCOM_CFG_RX_BUF_SIZE - 1U, simcom_ringbuf_free_space(&s_modem));
    TEST_ASSERT_FALSE(simcom_ringbuf_pop(&s_modem, &byte_out));
    TEST_ASSERT_FALSE(s_modem.rx_overflow);
}

static void test_ringbuf_push_and_pop_preserves_fifo_order(void)
{
    const uint8_t payload[] = { 'A', 'T', '\r', '\n', 'O', 'K' };
    size_t fed = 0U;
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_FeedBytes(&s_modem, payload, sizeof(payload), &fed));
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), fed);
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), simcom_ringbuf_count(&s_modem));

    for (size_t i = 0U; i < sizeof(payload); ++i) {
        uint8_t out = 0U;
        TEST_ASSERT_TRUE(simcom_ringbuf_pop(&s_modem, &out));
        TEST_ASSERT_EQUAL_UINT8(payload[i], out);
    }
    TEST_ASSERT_EQUAL_UINT(0U, simcom_ringbuf_count(&s_modem));
}

static void test_ringbuf_wrap_around_across_boundary(void)
{
    uint8_t chunk[200];
    for (size_t i = 0U; i < sizeof(chunk); ++i) {
        chunk[i] = (uint8_t)(i & 0xFFU);
    }

    for (size_t cycle = 0U; cycle < 6U; ++cycle) {
        size_t fed = 0U;
        TEST_ASSERT_EQUAL(SIMCOM_STATUS_OK, Simcom_FeedBytes(&s_modem, chunk, sizeof(chunk), &fed));
        TEST_ASSERT_EQUAL_UINT(sizeof(chunk), fed);

        for (size_t i = 0U; i < sizeof(chunk); ++i) {
            uint8_t b = 0U;
            TEST_ASSERT_TRUE(simcom_ringbuf_pop(&s_modem, &b));
            TEST_ASSERT_EQUAL_UINT8(chunk[i], b);
        }
    }
    TEST_ASSERT_FALSE(s_modem.rx_overflow);
}

static void test_ringbuf_saturation_flags_overflow_and_drops_excess_safely(void)
{
    uint8_t oversized[SIMCOM_CFG_RX_BUF_SIZE + 64U];
    for (size_t i = 0U; i < sizeof(oversized); ++i) {
        oversized[i] = (uint8_t)('A' + (i % 26U));
    }

    size_t fed = 0U;
    simcom_status_t st = Simcom_FeedBytes(&s_modem, oversized, sizeof(oversized), &fed);
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_BUFFER_FULL, st);
    TEST_ASSERT_EQUAL_UINT(SIMCOM_CFG_RX_BUF_SIZE - 1U, fed);
    TEST_ASSERT_TRUE(s_modem.rx_overflow);

    /* Verify unread bytes were NOT overwritten */
    for (size_t i = 0U; i < (SIMCOM_CFG_RX_BUF_SIZE - 1U); ++i) {
        uint8_t b = 0U;
        TEST_ASSERT_TRUE(simcom_ringbuf_pop(&s_modem, &b));
        TEST_ASSERT_EQUAL_UINT8(oversized[i], b);
    }
    TEST_ASSERT_EQUAL_UINT(0U, simcom_ringbuf_count(&s_modem));
}

static void test_ringbuf_null_arguments_rejected(void)
{
    size_t fed = 99U;
    const uint8_t b = 'A';
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_INVALID_ARG, Simcom_FeedBytes(NULL, &b, 1U, &fed));
    TEST_ASSERT_EQUAL_UINT(0U, fed);
    TEST_ASSERT_EQUAL(SIMCOM_STATUS_INVALID_ARG, Simcom_FeedBytes(&s_modem, NULL, 4U, &fed));
    TEST_ASSERT_EQUAL_UINT(0U, fed);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ringbuf_initial_state_is_empty);
    RUN_TEST(test_ringbuf_push_and_pop_preserves_fifo_order);
    RUN_TEST(test_ringbuf_wrap_around_across_boundary);
    RUN_TEST(test_ringbuf_saturation_flags_overflow_and_drops_excess_safely);
    RUN_TEST(test_ringbuf_null_arguments_rejected);
    return UNITY_END();
}
