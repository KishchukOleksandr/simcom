/**
 * @file unity.h
 * @brief Public Unity unit test assertion API.
 */

#ifndef UNITY_H
#define UNITY_H

#include "unity_internals.h"

#define UNITY_BEGIN()                       UnityBegin(__FILE__)
#define UNITY_END()                         UnityEnd()
#define RUN_TEST(func)                      UnityDefaultTestRun((func), #func, (uint32_t)__LINE__)

#define TEST_FAIL_MESSAGE(msg)              UnityFail((msg), (uint32_t)__LINE__)
#define TEST_FAIL()                         UnityFail("Test failed", (uint32_t)__LINE__)

#define TEST_ASSERT(condition)              UnityAssertBits((bool)(condition), #condition, (uint32_t)__LINE__)
#define TEST_ASSERT_TRUE(condition)         UnityAssertBits((bool)(condition), "Expected TRUE Was FALSE", (uint32_t)__LINE__)
#define TEST_ASSERT_FALSE(condition)        UnityAssertBits(!(bool)(condition), "Expected FALSE Was TRUE", (uint32_t)__LINE__)
#define TEST_ASSERT_NULL(pointer)           UnityAssertBits(((pointer) == NULL), "Expected NULL", (uint32_t)__LINE__)
#define TEST_ASSERT_NOT_NULL(pointer)       UnityAssertBits(((pointer) != NULL), "Expected Non-NULL", (uint32_t)__LINE__)

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    UnityAssertEqualNumber((int64_t)(expected), (int64_t)(actual), NULL, (uint32_t)__LINE__)
#define TEST_ASSERT_EQUAL_INT16(expected, actual) \
    UnityAssertEqualNumber((int64_t)(int16_t)(expected), (int64_t)(int16_t)(actual), NULL, (uint32_t)__LINE__)
#define TEST_ASSERT_EQUAL_INT32(expected, actual) \
    UnityAssertEqualNumber((int64_t)(int32_t)(expected), (int64_t)(int32_t)(actual), NULL, (uint32_t)__LINE__)
#define TEST_ASSERT_EQUAL(expected, actual) \
    UnityAssertEqualNumber((int64_t)(expected), (int64_t)(actual), NULL, (uint32_t)__LINE__)

#define TEST_ASSERT_EQUAL_UINT(expected, actual) \
    UnityAssertEqualUint((uint64_t)(expected), (uint64_t)(actual), NULL, (uint32_t)__LINE__)
#define TEST_ASSERT_EQUAL_UINT8(expected, actual) \
    UnityAssertEqualUint((uint64_t)(uint8_t)(expected), (uint64_t)(uint8_t)(actual), NULL, (uint32_t)__LINE__)
#define TEST_ASSERT_EQUAL_UINT16(expected, actual) \
    UnityAssertEqualUint((uint64_t)(uint16_t)(expected), (uint64_t)(uint16_t)(actual), NULL, (uint32_t)__LINE__)
#define TEST_ASSERT_EQUAL_UINT32(expected, actual) \
    UnityAssertEqualUint((uint64_t)(uint32_t)(expected), (uint64_t)(uint32_t)(actual), NULL, (uint32_t)__LINE__)

#define TEST_ASSERT_EQUAL_STRING(expected, actual) \
    UnityAssertEqualString((expected), (actual), NULL, (uint32_t)__LINE__)

#define TEST_ASSERT_EQUAL_MEMORY(expected, actual, len) \
    UnityAssertEqualMemory((expected), (actual), (size_t)(len), NULL, (uint32_t)__LINE__)

#define TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, num_elements) \
    UnityAssertEqualMemory((expected), (actual), (size_t)(num_elements), NULL, (uint32_t)__LINE__)

#endif /* UNITY_H */
