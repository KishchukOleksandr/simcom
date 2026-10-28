/**
 * @file unity.c
 * @brief Lightweight Unity test runner implementation.
 */

#include "unity.h"
#include <inttypes.h>

UNITY_STORAGE_T Unity;

void UnityBegin(const char *filename)
{
    Unity.TestFile = filename;
    Unity.CurrentTestName = NULL;
    Unity.CurrentTestLineNumber = 0U;
    Unity.NumberOfTests = 0U;
    Unity.TestFailures = 0U;
    Unity.TestIgnores = 0U;
    Unity.CurrentTestFailed = 0U;
    Unity.CurrentTestIgnored = 0U;
}

int UnityEnd(void)
{
    printf("\n-----------------------\n");
    printf("%" PRIu32 " Tests %" PRIu32 " Failures %" PRIu32 " Ignored\n",
           Unity.NumberOfTests, Unity.TestFailures, Unity.TestIgnores);
    if (Unity.TestFailures == 0U) {
        printf("OK\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}

void UnityDefaultTestRun(UnityTestFunction Func, const char *FuncName, const uint32_t FuncLineNum)
{
    Unity.CurrentTestName = FuncName;
    Unity.CurrentTestLineNumber = FuncLineNum;
    Unity.NumberOfTests++;
    Unity.CurrentTestFailed = 0U;
    Unity.CurrentTestIgnored = 0U;

    if (setjmp(Unity.AbortFrame) == 0) {
        setUp();
        Func();
    }
    if (Unity.CurrentTestFailed == 0U) {
        tearDown();
    }

    if (Unity.CurrentTestFailed != 0U) {
        Unity.TestFailures++;
    } else {
        printf("%s:%" PRIu32 ":%s:PASS\n", Unity.TestFile, FuncLineNum, FuncName);
    }
}

void UnityFail(const char *msg, const uint32_t lineNumber)
{
    Unity.CurrentTestFailed = 1U;
    printf("%s:%" PRIu32 ":%s:FAIL: %s\n",
           Unity.TestFile,
           lineNumber,
           (Unity.CurrentTestName != NULL) ? Unity.CurrentTestName : "test",
           (msg != NULL) ? msg : "Assertion failed");
    longjmp(Unity.AbortFrame, 1);
}

void UnityAssertBits(const bool condition, const char *msg, const uint32_t lineNumber)
{
    if (!condition) {
        UnityFail(msg, lineNumber);
    }
}

void UnityAssertEqualNumber(const int64_t expected, const int64_t actual, const char *msg, const uint32_t lineNumber)
{
    if (expected != actual) {
        Unity.CurrentTestFailed = 1U;
        printf("%s:%" PRIu32 ":%s:FAIL: Expected %" PRId64 " Was %" PRId64 "%s%s\n",
               Unity.TestFile,
               lineNumber,
               (Unity.CurrentTestName != NULL) ? Unity.CurrentTestName : "test",
               expected,
               actual,
               (msg != NULL) ? " : " : "",
               (msg != NULL) ? msg : "");
        longjmp(Unity.AbortFrame, 1);
    }
}

void UnityAssertEqualUint(const uint64_t expected, const uint64_t actual, const char *msg, const uint32_t lineNumber)
{
    if (expected != actual) {
        Unity.CurrentTestFailed = 1U;
        printf("%s:%" PRIu32 ":%s:FAIL: Expected %" PRIu64 " (0x%" PRIx64 ") Was %" PRIu64 " (0x%" PRIx64 ")%s%s\n",
               Unity.TestFile,
               lineNumber,
               (Unity.CurrentTestName != NULL) ? Unity.CurrentTestName : "test",
               expected,
               expected,
               actual,
               actual,
               (msg != NULL) ? " : " : "",
               (msg != NULL) ? msg : "");
        longjmp(Unity.AbortFrame, 1);
    }
}

void UnityAssertEqualString(const char *expected, const char *actual, const char *msg, const uint32_t lineNumber)
{
    if ((expected == NULL) && (actual == NULL)) {
        return;
    }
    if ((expected == NULL) || (actual == NULL) || (strcmp(expected, actual) != 0)) {
        Unity.CurrentTestFailed = 1U;
        printf("%s:%" PRIu32 ":%s:FAIL: Expected '%s' Was '%s'%s%s\n",
               Unity.TestFile,
               lineNumber,
               (Unity.CurrentTestName != NULL) ? Unity.CurrentTestName : "test",
               (expected != NULL) ? expected : "NULL",
               (actual != NULL) ? actual : "NULL",
               (msg != NULL) ? " : " : "",
               (msg != NULL) ? msg : "");
        longjmp(Unity.AbortFrame, 1);
    }
}

void UnityAssertEqualMemory(const void *expected, const void *actual, const size_t length, const char *msg, const uint32_t lineNumber)
{
    if (length == 0U) {
        return;
    }
    if ((expected == NULL) || (actual == NULL) || (memcmp(expected, actual, length) != 0)) {
        UnityFail((msg != NULL) ? msg : "Memory mismatch", lineNumber);
    }
}
