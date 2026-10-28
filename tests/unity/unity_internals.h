/**
 * @file unity_internals.h
 * @brief Lightweight Unity C11 unit test internals for host verification.
 */

#ifndef UNITY_INTERNALS_H
#define UNITY_INTERNALS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*UnityTestFunction)(void);

typedef struct {
    const char *TestFile;
    const char *CurrentTestName;
    uint32_t CurrentTestLineNumber;
    uint32_t NumberOfTests;
    uint32_t TestFailures;
    uint32_t TestIgnores;
    uint32_t CurrentTestFailed;
    uint32_t CurrentTestIgnored;
    jmp_buf AbortFrame;
} UNITY_STORAGE_T;

extern UNITY_STORAGE_T Unity;

void setUp(void);
void tearDown(void);

void UnityBegin(const char *filename);
int UnityEnd(void);
void UnityDefaultTestRun(UnityTestFunction Func, const char *FuncName, const uint32_t FuncLineNum);

void UnityAssertEqualNumber(const int64_t expected, const int64_t actual, const char *msg, const uint32_t lineNumber);
void UnityAssertEqualUint(const uint64_t expected, const uint64_t actual, const char *msg, const uint32_t lineNumber);
void UnityAssertEqualString(const char *expected, const char *actual, const char *msg, const uint32_t lineNumber);
void UnityAssertEqualMemory(const void *expected, const void *actual, const size_t length, const char *msg, const uint32_t lineNumber);
void UnityAssertBits(const bool condition, const char *msg, const uint32_t lineNumber);
void UnityFail(const char *msg, const uint32_t lineNumber);

#ifdef __cplusplus
}
#endif

#endif /* UNITY_INTERNALS_H */
