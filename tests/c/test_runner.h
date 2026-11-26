#ifndef DIALOS_TEST_RUNNER_H
#define DIALOS_TEST_RUNNER_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int test_failures_count;
extern int test_assertions_count;

#define TEST_ASSERT_TRUE(condition)                                                        \
    do {                                                                                   \
        test_assertions_count++;                                                           \
        if (!(condition)) {                                                                \
            fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__,         \
                    #condition);                                                           \
            test_failures_count++;                                                         \
            return;                                                                        \
        }                                                                                  \
    } while (0)

#define TEST_ASSERT_FALSE(condition) TEST_ASSERT_TRUE(!(condition))

#define TEST_ASSERT_EQ_U32(expected, actual)                                               \
    do {                                                                                   \
        uint32_t expected_value = (uint32_t)(expected);                                    \
        uint32_t actual_value = (uint32_t)(actual);                                        \
        test_assertions_count++;                                                           \
        if (expected_value != actual_value) {                                              \
            fprintf(stderr, "FAIL %s:%d: expected %u, got %u\n", __FILE__, __LINE__,       \
                    expected_value, actual_value);                                         \
            test_failures_count++;                                                         \
            return;                                                                        \
        }                                                                                  \
    } while (0)

#define TEST_ASSERT_EQ_U64(expected, actual)                                               \
    do {                                                                                   \
        uint64_t expected_value = (uint64_t)(expected);                                    \
        uint64_t actual_value = (uint64_t)(actual);                                        \
        test_assertions_count++;                                                           \
        if (expected_value != actual_value) {                                              \
            fprintf(stderr, "FAIL %s:%d: expected %llu, got %llu\n", __FILE__, __LINE__,   \
                    (unsigned long long)expected_value, (unsigned long long)actual_value); \
            test_failures_count++;                                                         \
            return;                                                                        \
        }                                                                                  \
    } while (0)

#define TEST_ASSERT_EQ_I32(expected, actual)                                               \
    do {                                                                                   \
        int32_t expected_value = (int32_t)(expected);                                      \
        int32_t actual_value = (int32_t)(actual);                                          \
        test_assertions_count++;                                                           \
        if (expected_value != actual_value) {                                              \
            fprintf(stderr, "FAIL %s:%d: expected %d, got %d\n", __FILE__, __LINE__,       \
                    expected_value, actual_value);                                         \
            test_failures_count++;                                                         \
            return;                                                                        \
        }                                                                                  \
    } while (0)

#define TEST_ASSERT_EQ_STR(expected, actual)                                               \
    do {                                                                                   \
        const char *expected_text = (expected);                                            \
        const char *actual_text = (actual);                                                \
        test_assertions_count++;                                                           \
        if (actual_text == NULL || strcmp(expected_text, actual_text) != 0) {              \
            fprintf(stderr, "FAIL %s:%d: expected '%s', got '%s'\n", __FILE__, __LINE__,   \
                    expected_text, actual_text != NULL ? actual_text : "(null)");          \
            test_failures_count++;                                                         \
            return;                                                                        \
        }                                                                                  \
    } while (0)

typedef void (*test_case_function)(void);

void run_protocol_tests(void);
void run_boot_sequence_tests(void);
void run_render_tests(void);
void run_app_tests(void);

#define RUN_TEST(function_name)                                                            \
    do {                                                                                   \
        int failures_before = test_failures_count;                                         \
        function_name();                                                                   \
        if (test_failures_count == failures_before) {                                      \
            printf("  PASS %s\n", #function_name);                                         \
        }                                                                                  \
    } while (0)

#endif
