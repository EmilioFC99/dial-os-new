#ifndef DIALOS_LIBRARY_SCANNER_H
#define DIALOS_LIBRARY_SCANNER_H

#include "library_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_SCAN_DEFAULT_BUDGET_NS 4000000ull
#define LIBRARY_SCAN_DELTA_BUDGET_NS 2000000ull
#define LIBRARY_SCAN_MAX_DEPTH 16u

enum library_scan_phase {
    LIB_SCAN_IDLE = 0,
    LIB_SCAN_TRY_CACHE = 1,
    LIB_SCAN_DISCOVERING = 2,
    LIB_SCAN_SCANNING = 3,
    LIB_SCAN_PARSING_M3U = 4,
    LIB_SCAN_FINALIZING = 5,
    LIB_SCAN_READY = 6,
    LIB_SCAN_DELTA_DISCOVERING = 7,
    LIB_SCAN_DELTA_CHECKING = 8
};

struct library_discovered_entry {
    char *path;
    int64_t mtime_seconds;
    uint64_t file_size_bytes;
    uint8_t format_kind;
};

struct library_dir_frame {
    void *dir_handle;
    char *dir_path;
};

struct library_scanner {
    enum library_scan_phase phase;
    bool music_root_exists;
    bool loaded_from_cache;
    bool delta_changed;
    char root_path[256];
    char index_dir_path[256];
    char index_temp_path[256];
    char index_final_path[256];

    struct library_dir_frame dir_stack[LIBRARY_SCAN_MAX_DEPTH];
    uint32_t dir_depth;

    struct library_discovered_entry *audio_entries;
    uint32_t discovered_files;
    uint32_t audio_capacity;
    uint32_t scanned_files;

    char **m3u_paths;
    uint32_t m3u_count;
    uint32_t m3u_capacity;
    uint32_t m3u_cursor;

    uint64_t delta_fingerprint;
    struct library_catalog *catalog;
};

void library_scanner_initialize(struct library_scanner *scanner, struct library_catalog *catalog);
void library_scanner_destroy(struct library_scanner *scanner);
void library_scanner_reset(struct library_scanner *scanner);

bool library_scanner_start(struct library_scanner *scanner, const char *root_path,
                           const char *index_dir_path, bool try_cache_first);
bool library_scanner_start_delta_check(struct library_scanner *scanner);

/* Advances cooperative scan within budget_ns; returns true if phase/counts changed */
bool library_scanner_step(struct library_scanner *scanner, uint64_t now_ns, uint64_t budget_ns);

bool library_scanner_is_busy(const struct library_scanner *scanner);
bool library_scanner_is_ready(const struct library_scanner *scanner);

/* Exposed for unit testing of native binary tag parsers */
bool library_parse_audio_file_metadata(const char *file_path, const char *root_path,
                                       struct library_track_input *input_out,
                                       char *string_scratch, size_t scratch_capacity);

#endif
