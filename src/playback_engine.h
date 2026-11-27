#ifndef DIALOS_PLAYBACK_ENGINE_H
#define DIALOS_PLAYBACK_ENGINE_H

#include "constants.h"
#include "library_db.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

enum playback_skip_action {
    PLAYBACK_SKIP_BLOCKED = 0,
    PLAYBACK_SKIP_TRACK = 1,
    PLAYBACK_SKIP_RESTART = 2,
    PLAYBACK_SKIP_RESHUFFLE_WRAP = 3
};

struct playback_skip_preview {
    enum playback_skip_action action;
    bool same_album;
    struct library_db_track_details target_track;
};

struct playback_engine_effects {
    bool launch_slice;
    uint32_t slice_start_slot;
    bool send_play_pause;
    bool send_stop;
    bool send_seek;
    uint32_t seek_ms;
    bool send_play_mode;
    uint8_t ipc_play_mode;
    bool track_changed;
    bool show_same_album_banner;
    bool start_cross_album_slide;
    int32_t slide_direction;
    bool boundary_bounce;
    int32_t bounce_direction;
    float vinyl_nudge_radians;
    bool has_toast;
    char toast_text[64];
};

struct playback_engine {
    struct library_db *database;
    bool has_active_track;
    bool is_playing;
    bool ephemeral_active;
    bool shuffle_enabled;
    enum playback_repeat_mode repeat_mode;
    struct library_db_collection context;
    char context_title[TRACK_METADATA_TEXT_CAPACITY];
    uint32_t current_slot;
    uint32_t queue_total;
    struct library_db_track_details active_track;
    uint32_t elapsed_ms;
    uint32_t duration_ms;
    uint32_t slice_start_slot;
    uint32_t slice_count;
    bool awaiting_slice_load;
    bool audio_started;
    bool has_pending_resume_seek;
    uint32_t pending_resume_seek_ms;
    uint8_t active_ipc_play_mode;
    bool persistence_dirty;
};

void playback_engine_initialize(struct playback_engine *engine, struct library_db *database);
const char *playback_engine_context_badge(enum library_db_collection_kind kind);
bool playback_engine_restore_from_db(struct playback_engine *engine, uint8_t *volume_out,
                                     bool *has_volume_out);
void playback_engine_save_to_db(const struct playback_engine *engine, uint8_t volume_level);

bool playback_engine_launch_context(struct playback_engine *engine,
                                    const struct library_db_collection *collection,
                                    const char *context_title, int64_t anchor_track_id,
                                    bool has_shuffle_override, bool shuffle_override,
                                    struct playback_engine_effects *effects_out);
bool playback_engine_toggle_play_pause(struct playback_engine *engine,
                                       struct playback_engine_effects *effects_out);
bool playback_engine_skip(struct playback_engine *engine, int32_t direction,
                          struct playback_engine_effects *effects_out);
bool playback_engine_jump_to_slot(struct playback_engine *engine, uint32_t target_slot,
                                  struct playback_engine_effects *effects_out);
bool playback_engine_seek_ms(struct playback_engine *engine, uint32_t target_ms,
                             struct playback_engine_effects *effects_out);
bool playback_engine_toggle_shuffle(struct playback_engine *engine,
                                    struct playback_engine_effects *effects_out);
bool playback_engine_cycle_repeat(struct playback_engine *engine,
                                  struct playback_engine_effects *effects_out);
bool playback_engine_toggle_active_favorite(struct playback_engine *engine, bool *is_favorite_out,
                                            struct playback_engine_effects *effects_out);
void playback_engine_on_playlist_mutated(struct playback_engine *engine, int64_t playlist_id,
                                         bool deleted,
                                         struct playback_engine_effects *effects_out);
void playback_engine_on_favorites_mutated(struct playback_engine *engine,
                                          struct playback_engine_effects *effects_out);

void playback_engine_handle_ipc_status(struct playback_engine *engine,
                                       const struct playback_status_update *status,
                                       struct playback_engine_effects *effects_out);
void playback_engine_handle_ipc_elapsed(struct playback_engine *engine, uint32_t elapsed_ms);
void playback_engine_handle_ipc_stop(struct playback_engine *engine,
                                     struct playback_engine_effects *effects_out);
void playback_engine_peek_skip(const struct playback_engine *engine, int32_t direction,
                               struct playback_skip_preview *preview_out);

#endif
