#ifndef DIALOS_LIBRARY_SCANNER_H
#define DIALOS_LIBRARY_SCANNER_H

#include "library_catalog.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct library_db;

#define LIBRARY_SCAN_DEFAULT_BUDGET_NS 4000000ull
#define LIBRARY_SCAN_DELTA_BUDGET_NS 1000000ull
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
bool library_compute_audio_fingerprint(const char *file_path, uint64_t audio_start_offset,
                                       uint64_t audio_payload_bytes, uint64_t *fingerprint_out);

enum library_db_scan_phase {
    LIB_DB_SCAN_IDLE = 0,
    LIB_DB_SCAN_DISCOVERING = 1,
    LIB_DB_SCAN_PARSING = 2,
    LIB_DB_SCAN_COMMITTING = 3,
    LIB_DB_SCAN_READY = 4
};

struct library_db_scan_telemetry {
    uint32_t unchanged_count;
    uint32_t modified_count;
    uint32_t moved_count;
    uint32_t moved_edited_count;
    uint32_t added_count;
    uint32_t deleted_count;
    uint32_t files_opened;
    uint32_t db_writes;
    uint32_t m3u_parsed_count;
    uint32_t sidecars_updated_count;
};

struct library_db_scan_snapshot {
    enum library_db_scan_phase phase;
    uint32_t discovered_files;
    uint32_t delta_files;
    uint32_t scanned_files;
    uint32_t album_count;
    uint32_t track_count;
    bool music_root_exists;
    bool ready;
    bool has_changes;
    struct library_db_scan_telemetry telemetry;
};

struct library_db_scanner {
    struct library_db *database;
    char root_path[256];
    char covers_directory[320];

    pthread_t worker_thread;
    bool worker_running;
    pthread_mutex_t snapshot_mutex;

    _Atomic uint32_t atomic_phase;
    _Atomic uint32_t atomic_discovered_files;
    _Atomic uint32_t atomic_delta_files;
    _Atomic uint32_t atomic_scanned_files;
    _Atomic uint32_t atomic_album_count;
    _Atomic uint32_t atomic_track_count;
    _Atomic bool atomic_music_root_exists;
    _Atomic bool atomic_ready;
    _Atomic bool atomic_has_changes;
    _Atomic bool cancel_requested;

    struct library_db_scan_telemetry telemetry;
};

void library_db_scanner_initialize(struct library_db_scanner *scanner);
void library_db_scanner_destroy(struct library_db_scanner *scanner);
bool library_db_scanner_start(struct library_db_scanner *scanner, struct library_db *database,
                              const char *music_root, const char *covers_directory);
void library_db_scanner_cancel(struct library_db_scanner *scanner);
bool library_db_scanner_wait(struct library_db_scanner *scanner);
void library_db_scanner_poll(struct library_db_scanner *scanner,
                             struct library_db_scan_snapshot *snapshot_out);

#endif
