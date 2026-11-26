#ifndef DIALOS_LOGGER_H
#define DIALOS_LOGGER_H

#include "constants.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*logger_flush_callback)(void *context, const char *text, size_t length,
                                      bool sync_to_disk);

struct event_logger {
    char buffer[LOGGER_BUFFER_CAPACITY_BYTES];
    size_t pending_length;
    uint32_t dropped_lines;
};

void logger_initialize(struct event_logger *logger);
void logger_log_line(struct event_logger *logger, uint64_t uptime_ms, const char *category,
                     const char *format, ...);
bool logger_has_pending(const struct event_logger *logger);
bool logger_flush(struct event_logger *logger, logger_flush_callback flush_callback,
                  void *context, bool sync_to_disk);

#endif
