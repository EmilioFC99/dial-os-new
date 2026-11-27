#ifndef DIALOS_LIBRARY_SCANNER_H
#define DIALOS_LIBRARY_SCANNER_H

#include "library_db.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_SCAN_MAX_DEPTH 16u

struct library_track_input {
    const char *file_path;
    const char *relative_path;
    const char *title;
    const char *artist;
    const char *album_artist;
    const char *album;
    const char *genre;
    uint16_t year;
    uint16_t disc_number;
    uint16_t track_number;
    uint32_t duration_seconds;
    int64_t added_at_seconds;
    uint64_t file_size_bytes;
    uint8_t format_kind;
    uint8_t cover_source_kind;
    const char *cover_path;
    uint32_t cover_data_offset;
    uint32_t cover_data_length;
    uint64_t audio_start_offset;
    uint64_t audio_payload_bytes;
};

/* Exposed for unit testing of native binary tag parsers */
bool library_parse_audio_file_metadata(const char *file_path, const char *root_path,
                                       struct library_track_input *input_out,
                                       char *string_scratch, size_t scratch_capacity);
bool library_compute_audio_fingerprint(const char *file_path, uint64_t audio_start_offset,
                                       uint64_t audio_payload_bytes, uint64_t *fingerprint_out);
int64_t library_normalize_fat_mtime(int64_t raw_mtime_seconds, int32_t kernel_tz_minutes_west);

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
    int32_t kernel_tz_minutes_west;
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
