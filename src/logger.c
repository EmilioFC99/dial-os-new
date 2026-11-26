#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void append_line_text(struct event_logger *logger, const char *line, size_t line_length) {
    if (line_length == 0u) {
        return;
    }
    if (logger->pending_length + line_length >= LOGGER_BUFFER_CAPACITY_BYTES) {
        logger->dropped_lines++;
        return;
    }
    memcpy(logger->buffer + logger->pending_length, line, line_length);
    logger->pending_length += line_length;
    logger->buffer[logger->pending_length] = '\0';
}

void logger_initialize(struct event_logger *logger) {
    if (logger == NULL) {
        return;
    }
    memset(logger, 0, sizeof(*logger));
    static const char SESSION_BANNER[] = "=== SESSION START ===\n";
    append_line_text(logger, SESSION_BANNER, sizeof(SESSION_BANNER) - 1u);
}

void logger_log_line(struct event_logger *logger, uint64_t uptime_ms, const char *category,
                     const char *format, ...) {
    if (logger == NULL || category == NULL || format == NULL) {
        return;
    }

    if (logger->dropped_lines > 0u) {
        char drop_line[64];
        int drop_written = snprintf(drop_line, sizeof(drop_line), "[%llums] LOG dropped=%u\n",
                                    (unsigned long long)uptime_ms, (unsigned)logger->dropped_lines);
        if (drop_written > 0 && (size_t)drop_written < sizeof(drop_line) &&
            logger->pending_length + (size_t)drop_written < LOGGER_BUFFER_CAPACITY_BYTES) {
            append_line_text(logger, drop_line, (size_t)drop_written);
            logger->dropped_lines = 0u;
        }
    }

    char message_buffer[LOGGER_LINE_MAX_BYTES];
    va_list args;
    va_start(args, format);
    int msg_written = vsnprintf(message_buffer, sizeof(message_buffer), format, args);
    va_end(args);
    if (msg_written < 0) {
        return;
    }

    char line_buffer[LOGGER_LINE_MAX_BYTES + 64u];
    int line_written = snprintf(line_buffer, sizeof(line_buffer), "[%llums] %s %s\n",
                                (unsigned long long)uptime_ms, category, message_buffer);
    if (line_written <= 0) {
        return;
    }
    size_t line_length = (size_t)line_written;
    if (line_length >= sizeof(line_buffer)) {
        line_length = sizeof(line_buffer) - 1u;
        line_buffer[line_length - 1u] = '\n';
    }
    append_line_text(logger, line_buffer, line_length);
}

bool logger_has_pending(const struct event_logger *logger) {
    return logger != NULL && logger->pending_length > 0u;
}

bool logger_flush(struct event_logger *logger, logger_flush_callback flush_callback,
                  void *context, bool sync_to_disk) {
    if (logger == NULL || flush_callback == NULL) {
        return false;
    }
    if (logger->pending_length == 0u) {
        return true;
    }
    if (!flush_callback(context, logger->buffer, logger->pending_length, sync_to_disk)) {
        return false;
    }
    logger->pending_length = 0u;
    logger->buffer[0] = '\0';
    return true;
}
