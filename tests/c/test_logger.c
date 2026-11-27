#include "test_runner.h"

#include "logger.h"

#include <string.h>

struct fake_log_sink {
    char collected[32768];
    size_t collected_length;
    bool should_succeed;
    bool last_sync_flag;
    uint32_t flush_calls;
};

static bool fake_sink_append(void *context, const char *text, size_t length, bool sync_to_disk) {
    struct fake_log_sink *sink = (struct fake_log_sink *)context;
    sink->flush_calls++;
    sink->last_sync_flag = sync_to_disk;
    if (!sink->should_succeed) {
        return false;
    }
    if (sink->collected_length + length < sizeof(sink->collected)) {
        memcpy(sink->collected + sink->collected_length, text, length);
        sink->collected_length += length;
        sink->collected[sink->collected_length] = '\0';
    }
    return true;
}

static void test_logger_format_flush_and_retention(void) {
    struct event_logger logger;
    logger_initialize(&logger);

    TEST_ASSERT_TRUE(logger_has_pending(&logger));
    TEST_ASSERT_TRUE(strstr(logger.buffer, "=== SESSION START ===") != NULL);

    logger_log_line(&logger, 42ull, "BOOT", "start mode=%s", "dark");
    logger_log_line(&logger, 1250ull, "PERF", "frames=%u fps=%u", 60u, 600u);

    /* Failed flush (e.g. SD not mounted yet) preserves buffered lines */
    struct fake_log_sink sink = {0};
    sink.should_succeed = false;
    TEST_ASSERT_FALSE(logger_flush(&logger, fake_sink_append, &sink, false));
    TEST_ASSERT_TRUE(logger_has_pending(&logger));

    /* Successful flush delivers all retained lines and clears pending buffer */
    sink.should_succeed = true;
    TEST_ASSERT_TRUE(logger_flush(&logger, fake_sink_append, &sink, true));
    TEST_ASSERT_FALSE(logger_has_pending(&logger));
    TEST_ASSERT_TRUE(sink.last_sync_flag);
    TEST_ASSERT_TRUE(strstr(sink.collected, "[42ms] BOOT start mode=dark\n") != NULL);
    TEST_ASSERT_TRUE(strstr(sink.collected, "[1250ms] PERF frames=60 fps=600\n") != NULL);
}

static void test_logger_overflow_and_null_safety(void) {
    struct event_logger logger;
    logger_initialize(&logger);

    for (uint32_t index = 0u; index < 450u; index++) {
        logger_log_line(&logger, (uint64_t)index * 10ull, "TICK",
                        "sample line payload index=%u padding=1234567890", (unsigned)index);
    }

    TEST_ASSERT_TRUE(logger.pending_length < LOGGER_BUFFER_CAPACITY_BYTES);
    TEST_ASSERT_TRUE(logger.dropped_lines > 0u);

    struct fake_log_sink sink = {0};
    sink.should_succeed = true;
    TEST_ASSERT_TRUE(logger_flush(&logger, fake_sink_append, &sink, false));
    TEST_ASSERT_FALSE(logger_has_pending(&logger));

    /* Next log after flush records dropped-line summary */
    logger_log_line(&logger, 9000ull, "HOME", "ready");
    TEST_ASSERT_TRUE(logger_flush(&logger, fake_sink_append, &sink, false));
    TEST_ASSERT_TRUE(strstr(sink.collected, "LOG dropped=") != NULL);
    TEST_ASSERT_TRUE(strstr(sink.collected, "[9000ms] HOME ready\n") != NULL);

    /* Null safety */
    logger_initialize(NULL);
    logger_log_line(NULL, 0ull, "X", "y");
    TEST_ASSERT_FALSE(logger_has_pending(NULL));
    TEST_ASSERT_FALSE(logger_flush(NULL, fake_sink_append, &sink, false));
}

void run_logger_tests(void) {
    printf("Suite: logger\n");
    RUN_TEST(test_logger_format_flush_and_retention);
    RUN_TEST(test_logger_overflow_and_null_safety);
}
