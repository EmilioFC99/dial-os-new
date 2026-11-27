#ifndef DIALOS_LIBRARY_DB_H
#define DIALOS_LIBRARY_DB_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define LIBRARY_COVER_MOTIF_COUNT 6u

enum library_sort_mode {
    LIBRARY_SORT_ALPHABETICAL = 0,
    LIBRARY_SORT_REVERSE_ALPHABETICAL = 1,
    LIBRARY_SORT_LIKED_FIRST = 2,
    LIBRARY_SORT_RECENT = 3,
    LIBRARY_SORT_OLDEST = 4,
    LIBRARY_SORT_MODE_COUNT = 5
};

enum library_audio_format {
    LIBRARY_FORMAT_UNKNOWN = 0,
    LIBRARY_FORMAT_FLAC = 1,
    LIBRARY_FORMAT_MP3 = 2,
    LIBRARY_FORMAT_M4A = 3,
    LIBRARY_FORMAT_WAV = 4,
    LIBRARY_FORMAT_OTHER = 5
};

enum library_cover_source_kind {
    LIBRARY_COVER_NONE = 0,
    LIBRARY_COVER_SIDECAR = 1,
    LIBRARY_COVER_EMBEDDED_FLAC = 2,
    LIBRARY_COVER_EMBEDDED_MP3 = 3,
    LIBRARY_COVER_EMBEDDED_M4A = 4
};

struct library_seek_anchor {
    char key[24];
    char primary[16];
    char sub_label[16];
    char short_label[16];
    bool is_date_mode;
    bool is_favorite;
    uint32_t first_item_index;
};

uint64_t library_fnv1a_64(const char *text);
int32_t library_compare_ascii_casefold(const char *first, const char *second);
const char *library_sort_mode_toast_label(enum library_sort_mode mode);

#define LIBRARY_DB_FILE_NAME "library.db"
#define LIBRARY_DB_CORRUPT_FILE_NAME ".library.db.corrupt"
#define LIBRARY_DB_SCHEMA_VERSION 1
#define LIBRARY_DB_PAGE_CACHE_KIBIBYTES 1500
#define LIBRARY_DB_BUSY_TIMEOUT_MS 3000
#define LIBRARY_DB_ROW_TEXT_BYTES 96u
#define LIBRARY_DB_PATH_BYTES 384u
#define LIBRARY_DB_MAX_WINDOW_ROWS 128u
#define LIBRARY_DB_MAX_GENRES_PER_TRACK 8u

/* Values stored in albums.cover_cache_status. */
enum library_db_cover_status {
    LIBRARY_DB_COVER_PENDING = 0,
    LIBRARY_DB_COVER_CACHED = 1,
    LIBRARY_DB_COVER_FAILED = 2,
    LIBRARY_DB_COVER_NONE = 3
};

enum library_db_collection_kind {
    LIBRARY_DB_COLLECTION_ALBUMS = 0,
    LIBRARY_DB_COLLECTION_ALBUMS_FAVORITES = 1,
    LIBRARY_DB_COLLECTION_ALBUMS_ARTIST = 2,
    LIBRARY_DB_COLLECTION_ARTISTS = 3,
    LIBRARY_DB_COLLECTION_GENRES = 4,
    LIBRARY_DB_COLLECTION_PLAYLISTS = 5,
    LIBRARY_DB_COLLECTION_TRACKS_ALL = 6,
    LIBRARY_DB_COLLECTION_TRACKS_FAVORITES = 7,
    LIBRARY_DB_COLLECTION_TRACKS_ALBUM = 8,
    LIBRARY_DB_COLLECTION_TRACKS_ARTIST = 9,
    LIBRARY_DB_COLLECTION_TRACKS_GENRE = 10,
    LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST = 11
};

enum library_db_favorite_kind {
    LIBRARY_DB_FAVORITE_TRACK = 0,
    LIBRARY_DB_FAVORITE_ALBUM = 1,
    LIBRARY_DB_FAVORITE_ARTIST = 2
};

enum library_db_sort_scope {
    LIBRARY_DB_SORT_SCOPE_ALBUMS = 0,
    LIBRARY_DB_SORT_SCOPE_ARTISTS = 1
};

struct library_db {
    struct sqlite3 *handle;
    pthread_mutex_t mutex;
    char directory_path[LIBRARY_DB_PATH_BYTES];
};

/* Identifies one ordered list; context_id is the row id of the album/artist/genre/playlist. */
struct library_db_collection {
    enum library_db_collection_kind kind;
    int64_t context_id;
};

struct library_db_counts {
    uint32_t tracks;
    uint32_t albums;
    uint32_t artists;
    uint32_t genres;
    uint32_t playlists;
    uint32_t favorite_tracks;
    uint32_t favorite_albums;
    uint32_t favorite_artists;
};

struct library_db_track_input {
    const char *path;
    uint64_t audio_fingerprint;
    uint64_t file_size_bytes;
    int64_t mtime_seconds;
    uint8_t format_kind;
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
    uint8_t cover_source_kind;
    const char *cover_path;
    uint32_t cover_data_offset;
    uint32_t cover_data_length;
    int64_t cover_source_mtime;
    uint64_t cover_source_size;
};

/* Fixed-size row copied into the UI viewport; avoids any SQLite access while rendering. */
struct library_db_row {
    int64_t id;
    uint64_t uid;
    int64_t album_id;
    int64_t added_at_seconds;
    uint32_t count;
    uint32_t duration_seconds;
    uint16_t year;
    uint16_t track_number;
    uint16_t disc_number;
    uint8_t motif_id;
    uint8_t cover_source_kind;
    uint8_t cover_cache_status;
    bool is_favorite;
    char title[LIBRARY_DB_ROW_TEXT_BYTES];
    char subtitle[LIBRARY_DB_ROW_TEXT_BYTES];
};

struct library_db_cover_job {
    int64_t album_id;
    uint64_t album_uid;
    uint8_t cover_source_kind;
    uint32_t data_offset;
    uint32_t data_length;
    char source_path[LIBRARY_DB_PATH_BYTES];
};

typedef void (*library_db_track_stamp_callback)(void *callback_context, int64_t track_id,
                                                const char *path, uint64_t file_size_bytes,
                                                int64_t mtime_seconds, uint64_t audio_fingerprint);
typedef void (*library_db_m3u_stamp_callback)(void *callback_context, const char *path,
                                              uint64_t file_size_bytes, int64_t mtime_seconds);

/* Lifecycle. A corrupt database is quarantined and recreated empty. */
bool library_db_open(struct library_db *database, const char *directory_path);
void library_db_close(struct library_db *database);

/* Transactions. Writers must be serialized by the caller (scanner, then cover precache). */
bool library_db_begin(struct library_db *database);
bool library_db_commit(struct library_db *database);
void library_db_rollback(struct library_db *database);

/* Track CRUD. existing_track_id == 0 looks the track up by path, then inserts. */
bool library_db_upsert_track(struct library_db *database,
                             const struct library_db_track_input *input,
                             int64_t existing_track_id, int64_t *track_id_out);
bool library_db_update_track_location(struct library_db *database, int64_t track_id,
                                      const char *new_path, int64_t mtime_seconds);
bool library_db_delete_track(struct library_db *database, int64_t track_id);
bool library_db_get_track_path(struct library_db *database, int64_t track_id, char *path_out,
                               size_t capacity);
bool library_db_find_track_by_path(struct library_db *database, const char *path,
                                   int64_t *track_id_out, int64_t *album_id_out,
                                   uint64_t *album_uid_out);

/* Removes empty albums/artists/genres and refreshes counts and motifs. Deleted album uids are
 * reported so their cached covers can be unlinked; call again while the count equals capacity. */
bool library_db_recompute_aggregates(struct library_db *database, uint64_t *deleted_album_uids,
                                     uint32_t capacity, uint32_t *deleted_count_out);

void library_db_for_each_track_stamp(struct library_db *database,
                                     library_db_track_stamp_callback callback,
                                     void *callback_context);

bool library_db_read_counts(struct library_db *database, struct library_db_counts *counts_out);

/* User state. */
bool library_db_toggle_favorite(struct library_db *database, enum library_db_favorite_kind kind,
                                int64_t row_id, bool *is_favorite_out);
bool library_db_set_sort_mode(struct library_db *database, enum library_db_sort_scope scope,
                              enum library_sort_mode sort_mode);
enum library_sort_mode library_db_get_sort_mode(struct library_db *database,
                                                enum library_db_sort_scope scope);

/* Playlists. Playlists backed by an .m3u file are dismissed rather than deleted so a rescan
 * does not resurrect them. */
bool library_db_upsert_m3u_playlist(struct library_db *database, const char *name,
                                    const char *source_m3u_path, const char *const *track_paths,
                                    uint32_t track_count);
bool library_db_delete_playlist(struct library_db *database, int64_t playlist_id);
bool library_db_put_m3u_file(struct library_db *database, const char *path,
                             uint64_t file_size_bytes, int64_t mtime_seconds);
bool library_db_remove_m3u_file(struct library_db *database, const char *path);
void library_db_for_each_m3u_file(struct library_db *database,
                                  library_db_m3u_stamp_callback callback, void *callback_context);

/* Sliding-window browsing. sort_mode only applies to albums and artists. */
uint32_t library_db_count(struct library_db *database, const struct library_db_collection *collection);
uint32_t library_db_fetch_window(struct library_db *database,
                                 const struct library_db_collection *collection,
                                 enum library_sort_mode sort_mode, uint32_t offset, uint32_t limit,
                                 struct library_db_row *rows_out);
uint32_t library_db_build_seek_anchors(struct library_db *database,
                                       const struct library_db_collection *collection,
                                       enum library_sort_mode sort_mode,
                                       struct library_seek_anchor *anchors_out,
                                       uint32_t max_anchors);
/* Streams track paths (one per line) from start_offset to the end of the collection. */
bool library_db_write_context_m3u(struct library_db *database,
                                  const struct library_db_collection *collection,
                                  uint32_t start_offset, FILE *output, uint32_t *written_out);
bool library_db_write_playlist_shuffled_m3u(struct library_db *database, int64_t playlist_id,
                                            FILE *output, uint32_t *written_out);

/* Cover cache bookkeeping. */
uint32_t library_db_count_pending_covers(struct library_db *database);
uint32_t library_db_query_pending_covers(struct library_db *database,
                                         struct library_db_cover_job *jobs_out, uint32_t capacity);
bool library_db_get_album_cover_job(struct library_db *database, int64_t album_id,
                                    struct library_db_cover_job *job_out);
bool library_db_set_cover_status(struct library_db *database, int64_t album_id,
                                 enum library_db_cover_status status);
bool library_db_mark_cover_statuses_batch(struct library_db *database, const int64_t *album_ids,
                                          const uint8_t *statuses, uint32_t count);
/* Applies (or clears, when cover_path is NULL) the sidecar cover of every album whose tracks sit
 * directly in directory_path. Only albums whose sidecar actually changed become pending. */
bool library_db_apply_sidecar_cover(struct library_db *database, const char *directory_path,
                                    const char *cover_path, int64_t mtime_seconds,
                                    uint64_t file_size_bytes);

#endif
