/**
 * @file test_parser.c
 * @brief Unit and malformed-input tests for defensive span tokenizers (T015).
 */

#include "unity.h"
#include "simcom_parser.h"
#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_span_prefix_and_equals(void)
{
    const uint8_t line[] = "+CREG: 0,1";
    TEST_ASSERT_TRUE(simcom_span_starts_with(line, sizeof(line) - 1U, "+CREG:"));
    TEST_ASSERT_FALSE(simcom_span_starts_with(line, sizeof(line) - 1U, "+CEREG:"));
    TEST_ASSERT_TRUE(simcom_span_equals((const uint8_t *)"OK", 2U, "OK"));
    TEST_ASSERT_FALSE(simcom_span_equals((const uint8_t *)"OKAY", 4U, "OK"));
}

static void test_span_token_slicing_and_unquoting(void)
{
    const char *raw = " 0, 0, \"CHN-UNICOM,LTD\", 7 ";
    const uint8_t *cursor = (const uint8_t *)raw;
    size_t rem = strlen(raw);
    const uint8_t *tok = NULL;
    size_t tok_len = 0U;
    char buf[32];

    TEST_ASSERT_TRUE(simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len));
    TEST_ASSERT_TRUE(simcom_span_equals(tok, tok_len, "0"));

    TEST_ASSERT_TRUE(simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len));
    TEST_ASSERT_TRUE(simcom_span_equals(tok, tok_len, "0"));

    /* Comma inside quotes MUST NOT split token */
    TEST_ASSERT_TRUE(simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len));
    TEST_ASSERT_TRUE(simcom_span_unquote_to_cstr(tok, tok_len, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("CHN-UNICOM,LTD", buf);

    TEST_ASSERT_TRUE(simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len));
    TEST_ASSERT_TRUE(simcom_span_equals(tok, tok_len, "7"));
    TEST_ASSERT_FALSE(simcom_span_next_token(&cursor, &rem, ',', &tok, &tok_len));
}

static void test_safe_integer_parsers_and_overflow_rejection(void)
{
    uint32_t uval = 0U;
    int32_t ival = 0;

    TEST_ASSERT_TRUE(simcom_span_parse_u32((const uint8_t *)"4294967295", 10U, &uval));
    TEST_ASSERT_EQUAL_UINT32(4294967295UL, uval);

    /* Overflow > UINT32_MAX must fail safely */
    TEST_ASSERT_FALSE(simcom_span_parse_u32((const uint8_t *)"4294967296", 10U, &uval));
    TEST_ASSERT_FALSE(simcom_span_parse_u32((const uint8_t *)"12a3", 4U, &uval));
    TEST_ASSERT_FALSE(simcom_span_parse_u32((const uint8_t *)"", 0U, &uval));

    TEST_ASSERT_TRUE(simcom_span_parse_i32((const uint8_t *)" -113 ", 6U, &ival));
    TEST_ASSERT_EQUAL_INT32(-113, ival);
    TEST_ASSERT_FALSE(simcom_span_parse_i32((const uint8_t *)"-9999999999", 11U, &ival));
}

static void test_cme_and_cms_error_extraction(void)
{
    uint16_t code = 0U;
    const char *cme = "+CME ERROR: 10";
    const char *cms = "+CMS ERROR: 302";
    const char *cme_text = "+CME ERROR: SIM not inserted";

    TEST_ASSERT_TRUE(simcom_parse_cme_error((const uint8_t *)cme, strlen(cme), &code));
    TEST_ASSERT_EQUAL_UINT16(10U, code);

    TEST_ASSERT_TRUE(simcom_parse_cms_error((const uint8_t *)cms, strlen(cms), &code));
    TEST_ASSERT_EQUAL_UINT16(302U, code);

    TEST_ASSERT_TRUE(simcom_parse_cme_error((const uint8_t *)cme_text, strlen(cme_text), &code));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFU, code);
}

static void test_domain_response_parsers_and_malformed_rejection(void)
{
    uint8_t rssi = 0U;
    int16_t dbm = 0;
    uint8_t ber = 0U;

    const char *csq_ok = "+CSQ: 24,0";
    TEST_ASSERT_TRUE(simcom_parse_csq_resp((const uint8_t *)csq_ok, strlen(csq_ok), &rssi, &dbm, &ber));
    TEST_ASSERT_EQUAL_UINT8(24U, rssi);
    TEST_ASSERT_EQUAL_INT16(-65, dbm);
    TEST_ASSERT_EQUAL_UINT8(0U, ber);

    const char *csq_unk = "+CSQ: 99,99";
    TEST_ASSERT_TRUE(simcom_parse_csq_resp((const uint8_t *)csq_unk, strlen(csq_unk), &rssi, &dbm, &ber));
    TEST_ASSERT_EQUAL_UINT8(99U, rssi);
    TEST_ASSERT_EQUAL_INT16(0, dbm);

    /* Out of range RSSI (50) must be rejected */
    const char *csq_bad = "+CSQ: 50,0";
    TEST_ASSERT_FALSE(simcom_parse_csq_resp((const uint8_t *)csq_bad, strlen(csq_bad), &rssi, &dbm, &ber));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_span_prefix_and_equals);
    RUN_TEST(test_span_token_slicing_and_unquoting);
    RUN_TEST(test_safe_integer_parsers_and_overflow_rejection);
    RUN_TEST(test_cme_and_cms_error_extraction);
    RUN_TEST(test_domain_response_parsers_and_malformed_rejection);
    return UNITY_END();
}
