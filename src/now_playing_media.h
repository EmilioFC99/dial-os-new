#ifndef DIALOS_NOW_PLAYING_MEDIA_H
#define DIALOS_NOW_PLAYING_MEDIA_H

#include "constants.h"
#include "library_cover_cache.h"
#include "library_db.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOW_PLAYING_COVER_SIZE_PX 360u
#define NOW_PLAYING_COVER_PIXEL_COUNT (NOW_PLAYING_COVER_SIZE_PX * NOW_PLAYING_COVER_SIZE_PX)
#define LYRICS_MAX_CUES 512u
#define LYRICS_LINE_CAPACITY 160u

enum lyrics_mode {
    LYRICS_MODE_AUTO = 0,
    LYRICS_MODE_TIMED = 1,
    LYRICS_MODE_UNTIMED = 2,
    LYRICS_MODE_NONE = 3
};

enum lyrics_source_kind {
    LYRICS_SOURCE_NONE = 0,
    LYRICS_SOURCE_SIDECAR_LRC = 1,
    LYRICS_SOURCE_EMBEDDED = 2
};

struct lyrics_cue {
    uint32_t time_ms;
    bool has_timestamp;
    char text[LYRICS_LINE_CAPACITY];
};

struct lyrics_document {
    bool has_timed;
    enum lyrics_source_kind source_kind;
    uint32_t cue_count;
    struct lyrics_cue cues[LYRICS_MAX_CUES];
    char source_note[64];
};

struct lyrics_resolved_state {
    enum lyrics_mode effective_mode;
    int32_t active_index;
    int32_t upcoming_index;
    bool is_intro_cue;
    char active_text[LYRICS_LINE_CAPACITY];
    char upcoming_text[LYRICS_LINE_CAPACITY];
    char status_note[64];
};

struct now_playing_media {
    struct library_db *database;
    uint16_t *active_cover_pixels;
    uint64_t active_cover_album_uid;
    bool active_cover_ready;

    uint16_t *peek_cover_pixels;
    uint64_t peek_cover_album_uid;
    bool peek_cover_ready;

    struct lyrics_document active_lyrics;
    char active_lyrics_path[LIBRARY_DB_PATH_BYTES];
    bool active_lyrics_ready;

    pthread_t worker_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool worker_running;
    bool worker_stop;

    bool has_pending_active;
    int64_t req_active_album_id;
    uint64_t req_active_album_uid;
    char req_active_track_path[LIBRARY_DB_PATH_BYTES];

    bool has_pending_peek;
    int64_t req_peek_album_id;
    uint64_t req_peek_album_uid;

    /* Staging buffers written by Core 1 and swapped under mutex */
    uint16_t *staging_cover_pixels;
    struct lyrics_document staging_lyrics;
};

void now_playing_media_initialize(struct now_playing_media *media, struct library_db *database);
void now_playing_media_destroy(struct now_playing_media *media);

void now_playing_media_request_active(struct now_playing_media *media, int64_t album_id,
                                      uint64_t album_uid, const char *track_path);
void now_playing_media_request_peek(struct now_playing_media *media, int64_t album_id,
                                    uint64_t album_uid);
void now_playing_media_clear(struct now_playing_media *media);

const uint16_t *now_playing_media_get_active_cover(struct now_playing_media *media,
                                                   uint64_t album_uid,
                                                   struct library_cover_cache *fallback_cache,
                                                   uint32_t *size_out);
const uint16_t *now_playing_media_get_peek_cover(struct now_playing_media *media,
                                                 uint64_t album_uid,
                                                 struct library_cover_cache *fallback_cache,
                                                 uint32_t *size_out);

bool now_playing_media_parse_lrc_text(const char *text, size_t length,
                                      struct lyrics_document *document_out);
bool now_playing_media_load_lyrics_for_track(const char *track_path,
                                             struct lyrics_document *document_out);
void now_playing_media_resolve_cue(const struct lyrics_document *document,
                                   enum lyrics_mode requested_mode, uint32_t elapsed_ms,
                                   uint32_t duration_ms,
                                   struct lyrics_resolved_state *resolved_out);
struct lyrics_badge_details {
    char badge_label[24];
    char banner_title[48];
    char banner_subtitle[64];
    bool is_timed;
    bool has_lyrics;
    bool is_ready;
};

void now_playing_media_get_lyrics_badge(struct now_playing_media *media, const char *track_path,
                                        char *label_out, size_t label_capacity,
                                        bool *is_timed_out, bool *has_lyrics_out);
void now_playing_media_get_lyrics_details(struct now_playing_media *media,
                                          const char *track_path,
                                          struct lyrics_badge_details *details_out);

#endif
