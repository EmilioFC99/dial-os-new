#include "playback_engine.h"

#include <stdio.h>
#include <string.h>

#define PREVIOUS_RESTART_THRESHOLD_MS 3000u

static void copy_text_bounded(char *destination, size_t capacity, const char *source) {
    if (destination == NULL || capacity == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    size_t length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1u;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static void clear_effects(struct playback_engine_effects *effects_out) {
    if (effects_out != NULL) {
        memset(effects_out, 0, sizeof(*effects_out));
    }
}

static void set_toast(struct playback_engine_effects *effects_out, const char *text) {
    if (effects_out == NULL || text == NULL) {
        return;
    }
    effects_out->has_toast = true;
    copy_text_bounded(effects_out->toast_text, sizeof(effects_out->toast_text), text);
}

static uint8_t desired_ipc_play_mode(const struct playback_engine *engine) {
    if (engine->repeat_mode == PLAYBACK_REPEAT_ONE && !engine->ephemeral_active) {
        return IPC_PLAY_MODE_SINGLE_LOOP;
    }
    return IPC_PLAY_MODE_LIST_ONCE;
}

static void sync_play_mode_effect(struct playback_engine *engine,
                                  struct playback_engine_effects *effects_out) {
    /* Stock 0654 controls backlight brightness (hw_echo_set_brightness), not play mode;
     * DialOS handles Repeat OFF / ALL / ONE directly in handle_natural_eof(). */
    (void)effects_out;
    engine->active_ipc_play_mode = desired_ipc_play_mode(engine);
}

static void arm_slice_launch(struct playback_engine *engine,
                             struct playback_engine_effects *effects_out) {
    engine->slice_start_slot = engine->current_slot;
    uint32_t remaining =
        engine->queue_total > engine->current_slot ? (engine->queue_total - engine->current_slot) : 0u;
    engine->slice_count =
        remaining > PLAYBACK_M3U_SLICE_TRACKS ? PLAYBACK_M3U_SLICE_TRACKS : remaining;
    engine->awaiting_slice_load = true;
    engine->audio_started = false;
    if (effects_out != NULL) {
        effects_out->launch_slice = true;
        effects_out->slice_start_slot = engine->slice_start_slot;
    }
}

static void stop_to_idle_with_toast(struct playback_engine *engine, const char *toast_text,
                                    struct playback_engine_effects *effects_out) {
    engine->has_active_track = false;
    engine->is_playing = false;
    engine->ephemeral_active = false;
    engine->current_slot = 0u;
    engine->queue_total = 0u;
    engine->elapsed_ms = 0u;
    engine->duration_ms = 0u;
    engine->slice_start_slot = 0u;
    engine->slice_count = 0u;
    engine->awaiting_slice_load = false;
    engine->audio_started = false;
    engine->has_pending_resume_seek = false;
    engine->pending_resume_seek_ms = 0u;
    memset(&engine->active_track, 0, sizeof(engine->active_track));
    engine->persistence_dirty = true;
    if (effects_out != NULL) {
        effects_out->send_stop = true;
        effects_out->track_changed = true;
        if (toast_text != NULL) {
            set_toast(effects_out, toast_text);
        }
    }
}

static void drop_ephemeral_if_present(struct playback_engine *engine) {
    if (!engine->ephemeral_active || engine->database == NULL) {
        return;
    }
    uint32_t new_total = engine->queue_total;
    if (library_db_drop_queue_slot(engine->database, engine->current_slot, &new_total)) {
        engine->queue_total = new_total;
    }
    engine->ephemeral_active = false;
}

static void load_slot_track(struct playback_engine *engine, uint32_t slot, bool keep_playing,
                            struct playback_engine_effects *effects_out) {
    struct library_db_track_details next_track;
    if (!library_db_get_queue_track(engine->database, slot, &next_track)) {
        stop_to_idle_with_toast(engine, "NO TRACKS LEFT", effects_out);
        return;
    }
    int64_t previous_album_id = engine->active_track.album_id;
    bool had_previous = engine->has_active_track;
    int32_t direction = (slot >= engine->current_slot) ? 1 : -1;

    engine->current_slot = slot;
    engine->active_track = next_track;
    engine->has_active_track = true;
    engine->is_playing = keep_playing;
    engine->elapsed_ms = 0u;
    engine->duration_ms = next_track.duration_seconds * MILLISECONDS_PER_SECOND;
    engine->has_pending_resume_seek = false;
    engine->pending_resume_seek_ms = 0u;
    engine->persistence_dirty = true;

    sync_play_mode_effect(engine, effects_out);
    if (effects_out != NULL) {
        effects_out->track_changed = true;
        if (had_previous && previous_album_id == next_track.album_id) {
            effects_out->show_same_album_banner = true;
            effects_out->vinyl_nudge_radians = direction > 0 ? 0.14f : -0.14f;
        } else if (had_previous) {
            effects_out->start_cross_album_slide = true;
            effects_out->slide_direction = direction;
            effects_out->vinyl_nudge_radians = direction > 0 ? 0.42f : -0.42f;
        }
    }
    if (keep_playing) {
        arm_slice_launch(engine, effects_out);
    } else {
        engine->slice_count = 0u;
        engine->awaiting_slice_load = false;
        engine->audio_started = false;
        if (effects_out != NULL) {
            effects_out->send_stop = true;
        }
    }
}

void playback_engine_initialize(struct playback_engine *engine, struct library_db *database) {
    if (engine == NULL) {
        return;
    }
    memset(engine, 0, sizeof(*engine));
    engine->database = database;
    engine->active_ipc_play_mode = IPC_PLAY_MODE_LIST_ONCE;
}

const char *playback_engine_context_badge(enum library_db_collection_kind kind) {
    switch (kind) {
        case LIBRARY_DB_COLLECTION_TRACKS_ALBUM:
            return "ALBUM";
        case LIBRARY_DB_COLLECTION_TRACKS_ARTIST:
            return "ARTIST";
        case LIBRARY_DB_COLLECTION_TRACKS_GENRE:
            return "GENRE";
        case LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST:
            return "PLAYLIST";
        case LIBRARY_DB_COLLECTION_TRACKS_FAVORITES:
            return "FAVORITES";
        case LIBRARY_DB_COLLECTION_TRACKS_ALL:
        default:
            return "ALL SONGS";
    }
}

bool playback_engine_restore_from_db(struct playback_engine *engine, uint8_t *volume_out,
                                     bool *has_volume_out) {
    if (engine == NULL || engine->database == NULL) {
        return false;
    }
    struct library_db_playback_state saved;
    if (!library_db_load_playback_state(engine->database, &saved)) {
        return false;
    }
    if (has_volume_out != NULL) {
        *has_volume_out = saved.has_volume;
    }
    if (volume_out != NULL && saved.has_volume) {
        *volume_out = saved.volume_level;
    }
    engine->shuffle_enabled = saved.shuffle_enabled;
    engine->repeat_mode = saved.repeat_mode;

    if (!saved.has_active_queue) {
        return true;
    }

    uint32_t total = library_db_playback_queue_count(engine->database);
    if (total == 0u || saved.current_slot >= total) {
        return true;
    }
    struct library_db_track_details details;
    if (!library_db_get_queue_track(engine->database, saved.current_slot, &details)) {
        return true;
    }
    if (saved.active_track_id > 0 && details.track_id != saved.active_track_id) {
        return true;
    }

    engine->has_active_track = true;
    engine->is_playing = false; /* Reboot always restores paused */
    engine->context.kind = saved.context_kind;
    engine->context.context_id = saved.context_id;
    copy_text_bounded(engine->context_title, sizeof(engine->context_title), saved.context_title);
    engine->current_slot = saved.current_slot;
    engine->queue_total = total;
    engine->active_track = details;
    engine->duration_ms = details.duration_seconds * MILLISECONDS_PER_SECOND;
    engine->elapsed_ms =
        (engine->duration_ms > 0u && saved.elapsed_ms > engine->duration_ms)
            ? engine->duration_ms
            : saved.elapsed_ms;
    if (engine->elapsed_ms > 0u) {
        engine->has_pending_resume_seek = true;
        engine->pending_resume_seek_ms = engine->elapsed_ms;
    }
    engine->awaiting_slice_load = false;
    return true;
}

void playback_engine_save_to_db(const struct playback_engine *engine, uint8_t volume_level) {
    if (engine == NULL || engine->database == NULL) {
        return;
    }
    struct library_db_playback_state state;
    memset(&state, 0, sizeof(state));
    state.has_volume = true;
    state.volume_level = volume_level > VOLUME_HARD_MAX ? VOLUME_HARD_MAX : volume_level;
    state.shuffle_enabled = engine->shuffle_enabled;
    state.repeat_mode = engine->repeat_mode;
    state.has_active_queue = engine->has_active_track;
    state.context_kind = engine->context.kind;
    state.context_id = engine->context.context_id;
    copy_text_bounded(state.context_title, sizeof(state.context_title), engine->context_title);
    state.current_slot = engine->current_slot;
    state.active_track_id = engine->active_track.track_id;
    state.elapsed_ms = engine->elapsed_ms;
    (void)library_db_save_playback_state(engine->database, &state);
}

bool playback_engine_launch_context(struct playback_engine *engine,
                                    const struct library_db_collection *collection,
                                    const char *context_title, int64_t anchor_track_id,
                                    bool has_shuffle_override, bool shuffle_override,
                                    struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || engine->database == NULL || collection == NULL) {
        return false;
    }

    bool target_shuffle = has_shuffle_override ? shuffle_override : engine->shuffle_enabled;
    bool same_loaded_track =
        (engine->has_active_track && anchor_track_id > 0 &&
         engine->active_track.track_id == anchor_track_id && !engine->ephemeral_active);
    uint32_t preserved_elapsed = same_loaded_track ? engine->elapsed_ms : 0u;
    bool was_awaiting_or_playing =
        (same_loaded_track && (engine->is_playing || engine->awaiting_slice_load));

    uint32_t slot = 0u;
    uint32_t total = 0u;
    if (!library_db_build_playback_queue(engine->database, collection, anchor_track_id,
                                         target_shuffle, &slot, &total) ||
        total == 0u) {
        return false;
    }

    struct library_db_track_details track_details;
    if (!library_db_get_queue_track(engine->database, slot, &track_details)) {
        return false;
    }

    engine->shuffle_enabled = target_shuffle;
    engine->ephemeral_active = false;
    engine->context = *collection;
    copy_text_bounded(engine->context_title, sizeof(engine->context_title),
                      context_title != NULL ? context_title
                                            : playback_engine_context_badge(collection->kind));
    engine->current_slot = slot;
    engine->queue_total = total;
    engine->active_track = track_details;
    engine->has_active_track = true;
    engine->is_playing = true;
    engine->duration_ms = track_details.duration_seconds * MILLISECONDS_PER_SECOND;
    engine->elapsed_ms = preserved_elapsed;
    engine->persistence_dirty = true;

    sync_play_mode_effect(engine, effects_out);

    if (same_loaded_track && !engine->has_pending_resume_seek) {
        /* Same track already loaded in mq_player: resume without restarting audio. */
        if (!was_awaiting_or_playing && effects_out != NULL) {
            effects_out->send_play_pause = true;
        }
        return true;
    }

    if (preserved_elapsed > 0u) {
        engine->has_pending_resume_seek = true;
        engine->pending_resume_seek_ms = preserved_elapsed;
    } else {
        engine->has_pending_resume_seek = false;
        engine->pending_resume_seek_ms = 0u;
    }
    if (effects_out != NULL) {
        effects_out->track_changed = true;
        effects_out->vinyl_nudge_radians = 0.24f;
    }
    arm_slice_launch(engine, effects_out);
    return true;
}

bool playback_engine_toggle_play_pause(struct playback_engine *engine,
                                       struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || !engine->has_active_track) {
        return false;
    }

    if (!engine->is_playing && engine->slice_count == 0u) {
        /* Resuming from reboot or Repeat Off end-of-context: launch slice from current_slot. */
        engine->is_playing = true;
        if (engine->elapsed_ms > 0u) {
            engine->has_pending_resume_seek = true;
            engine->pending_resume_seek_ms = engine->elapsed_ms;
        }
        sync_play_mode_effect(engine, effects_out);
        arm_slice_launch(engine, effects_out);
        engine->persistence_dirty = true;
        return true;
    }

    engine->is_playing = !engine->is_playing;
    engine->persistence_dirty = true;
    if (effects_out != NULL) {
        effects_out->send_play_pause = true;
    }
    return true;
}

bool playback_engine_skip(struct playback_engine *engine, int32_t direction,
                          struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || engine->database == NULL || !engine->has_active_track ||
        engine->queue_total == 0u || direction == 0) {
        return false;
    }

    int32_t step = direction > 0 ? 1 : -1;

    /* 3-second Previous rule: strictly > 3.0s restarts current track at 0:00. */
    if (step < 0 && engine->elapsed_ms > PREVIOUS_RESTART_THRESHOLD_MS) {
        engine->elapsed_ms = 0u;
        engine->has_pending_resume_seek = false;
        engine->persistence_dirty = true;
        if (effects_out != NULL) {
            if (engine->slice_count > 0u) {
                effects_out->send_seek = true;
                effects_out->seek_ms = 0u;
            }
            effects_out->vinyl_nudge_radians = -0.14f;
        }
        return true;
    }

    bool was_ephemeral = engine->ephemeral_active;
    int64_t previous_track_id = engine->active_track.track_id;
    uint32_t slot_after_drop = engine->current_slot;
    if (was_ephemeral) {
        drop_ephemeral_if_present(engine);
        if (engine->queue_total == 0u) {
            stop_to_idle_with_toast(engine, "NO TRACKS LEFT", effects_out);
            return true;
        }
        if (step > 0) {
            /* Dropping the active slot shifts the next track into slot_after_drop. */
            if (slot_after_drop < engine->queue_total) {
                load_slot_track(engine, slot_after_drop, engine->is_playing, effects_out);
                return true;
            }
        }
        if (slot_after_drop > 0u && step < 0) {
            load_slot_track(engine, slot_after_drop - 1u, engine->is_playing, effects_out);
            return true;
        }
    }

    if (step > 0) {
        if (engine->current_slot + 1u < engine->queue_total) {
            load_slot_track(engine, engine->current_slot + 1u, engine->is_playing, effects_out);
            return true;
        }
        if (engine->repeat_mode == PLAYBACK_REPEAT_ALL) {
            if (engine->shuffle_enabled && engine->queue_total > 1u) {
                (void)library_db_reshuffle_playback_queue(engine->database, 0, previous_track_id,
                                                          &engine->queue_total);
            }
            load_slot_track(engine, 0u, engine->is_playing, effects_out);
            return true;
        }
        /* Repeat OFF or ONE: boundary bounce */
        if (effects_out != NULL) {
            effects_out->boundary_bounce = true;
            effects_out->bounce_direction = 1;
            effects_out->vinyl_nudge_radians = -0.14f;
            char toast[64];
            snprintf(toast, sizeof(toast), "END OF %s",
                     playback_engine_context_badge(engine->context.kind));
            set_toast(effects_out, toast);
        }
        return true;
    }

    /* step < 0 */
    if (engine->current_slot > 0u) {
        load_slot_track(engine, engine->current_slot - 1u, engine->is_playing, effects_out);
        return true;
    }
    if (engine->repeat_mode == PLAYBACK_REPEAT_ALL) {
        uint32_t last_slot = engine->queue_total - 1u;
        load_slot_track(engine, last_slot, engine->is_playing, effects_out);
        return true;
    }
    if (effects_out != NULL) {
        effects_out->boundary_bounce = true;
        effects_out->bounce_direction = -1;
        effects_out->vinyl_nudge_radians = 0.14f;
        char toast[64];
        snprintf(toast, sizeof(toast), "START OF %s",
                 playback_engine_context_badge(engine->context.kind));
        set_toast(effects_out, toast);
    }
    return true;
}

bool playback_engine_jump_to_slot(struct playback_engine *engine, uint32_t target_slot,
                                  struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || engine->database == NULL || target_slot >= engine->queue_total) {
        return false;
    }
    if (engine->has_active_track && target_slot == engine->current_slot && !engine->ephemeral_active) {
        if (!engine->is_playing) {
            return playback_engine_toggle_play_pause(engine, effects_out);
        }
        return true;
    }
    if (engine->ephemeral_active) {
        uint32_t dropped_slot = engine->current_slot;
        drop_ephemeral_if_present(engine);
        if (engine->queue_total == 0u) {
            stop_to_idle_with_toast(engine, "NO TRACKS LEFT", effects_out);
            return true;
        }
        if (target_slot > dropped_slot) {
            target_slot--;
        }
        if (target_slot >= engine->queue_total) {
            target_slot = engine->queue_total - 1u;
        }
    }
    load_slot_track(engine, target_slot, true, effects_out);
    if (effects_out != NULL) {
        effects_out->vinyl_nudge_radians = 0.36f;
    }
    return true;
}

bool playback_engine_seek_ms(struct playback_engine *engine, uint32_t target_ms,
                             struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || !engine->has_active_track) {
        return false;
    }
    uint32_t max_ms = engine->duration_ms > 500u ? (engine->duration_ms - 500u) : engine->duration_ms;
    if (target_ms > max_ms) {
        target_ms = max_ms;
    }
    engine->elapsed_ms = target_ms;
    engine->persistence_dirty = true;
    if (engine->slice_count == 0u) {
        engine->has_pending_resume_seek = (target_ms > 0u);
        engine->pending_resume_seek_ms = target_ms;
        return true;
    }
    if (effects_out != NULL) {
        effects_out->send_seek = true;
        effects_out->seek_ms = target_ms;
    }
    return true;
}

bool playback_engine_toggle_shuffle(struct playback_engine *engine,
                                    struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL) {
        return false;
    }
    engine->shuffle_enabled = !engine->shuffle_enabled;
    engine->persistence_dirty = true;

    if (engine->has_active_track && engine->database != NULL) {
        if (engine->shuffle_enabled) {
            (void)library_db_reshuffle_playback_queue(engine->database,
                                                      engine->active_track.track_id, 0,
                                                      &engine->queue_total);
            engine->current_slot = 0u;
        } else {
            (void)library_db_restore_playback_queue_order(
                engine->database, &engine->context, engine->active_track.track_id,
                engine->ephemeral_active, &engine->current_slot, &engine->queue_total);
        }
        /* Re-arm rolling slice silently only when slice advances or on next track?
         * Because mq_player holds the old M3U list, re-arm only if remaining slice differs?
         * Actually, if we are currently playing, we must keep audio uninterrupted;
         * if slice_count > 1, we re-sync on natural EOF or skip, OR if Repeat One is off,
         * wait: if current track ends in mq_player's old slice, it would play the old slot 1!
         * We guard against that in handle_ipc_status() by checking if the new a202 path matches
         * current_slot + 1, and if not, immediately launching the new slice! */
    }

    set_toast(effects_out, engine->shuffle_enabled ? "SHUFFLE // ON" : "SHUFFLE // OFF");
    return true;
}

bool playback_engine_cycle_repeat(struct playback_engine *engine,
                                  struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL) {
        return false;
    }
    if (engine->repeat_mode == PLAYBACK_REPEAT_OFF) {
        engine->repeat_mode = PLAYBACK_REPEAT_ALL;
        set_toast(effects_out, "REPEAT // ALL");
    } else if (engine->repeat_mode == PLAYBACK_REPEAT_ALL) {
        engine->repeat_mode = PLAYBACK_REPEAT_ONE;
        set_toast(effects_out, "REPEAT // ONE");
    } else {
        engine->repeat_mode = PLAYBACK_REPEAT_OFF;
        set_toast(effects_out, "REPEAT // OFF");
    }
    engine->persistence_dirty = true;
    sync_play_mode_effect(engine, effects_out);
    return true;
}

bool playback_engine_toggle_active_favorite(struct playback_engine *engine, bool *is_favorite_out,
                                            struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || engine->database == NULL || !engine->has_active_track) {
        return false;
    }
    bool new_fav = false;
    if (!library_db_toggle_favorite(engine->database, LIBRARY_DB_FAVORITE_TRACK,
                                    engine->active_track.track_id, &new_fav)) {
        return false;
    }
    engine->active_track.is_favorite = new_fav;
    if (is_favorite_out != NULL) {
        *is_favorite_out = new_fav;
    }
    if (engine->context.kind == LIBRARY_DB_COLLECTION_TRACKS_FAVORITES) {
        playback_engine_on_favorites_mutated(engine, NULL);
    }
    set_toast(effects_out, new_fav ? "ADDED TO FAVORITES" : "REMOVED FROM FAVORITES");
    return true;
}

static void reconcile_active_collection(struct playback_engine *engine) {
    uint32_t new_slot = engine->current_slot;
    uint32_t new_total = engine->queue_total;
    bool became_ephemeral = false;
    if (library_db_reconcile_playback_queue(engine->database, &engine->context,
                                            engine->active_track.track_id, engine->shuffle_enabled,
                                            &new_slot, &new_total, &became_ephemeral)) {
        engine->current_slot = new_slot;
        engine->queue_total = new_total;
        engine->ephemeral_active = became_ephemeral;
        engine->persistence_dirty = true;
    }
}

void playback_engine_on_playlist_mutated(struct playback_engine *engine, int64_t playlist_id,
                                         bool deleted,
                                         struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || engine->database == NULL || !engine->has_active_track ||
        engine->context.kind != LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST ||
        engine->context.context_id != playlist_id) {
        return;
    }
    if (deleted) {
        stop_to_idle_with_toast(engine, "PLAYLIST DELETED", effects_out);
        return;
    }
    reconcile_active_collection(engine);
    sync_play_mode_effect(engine, effects_out);
}

void playback_engine_on_favorites_mutated(struct playback_engine *engine,
                                          struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || engine->database == NULL || !engine->has_active_track ||
        engine->context.kind != LIBRARY_DB_COLLECTION_TRACKS_FAVORITES) {
        return;
    }
    reconcile_active_collection(engine);
    sync_play_mode_effect(engine, effects_out);
}

static void handle_natural_eof(struct playback_engine *engine,
                               struct playback_engine_effects *effects_out) {
    if (!engine->has_active_track || engine->database == NULL) {
        return;
    }
    if (engine->ephemeral_active) {
        uint32_t slot_after_drop = engine->current_slot;
        drop_ephemeral_if_present(engine);
        if (engine->queue_total == 0u) {
            stop_to_idle_with_toast(engine, "NO TRACKS LEFT", effects_out);
            return;
        }
        if (slot_after_drop < engine->queue_total) {
            load_slot_track(engine, slot_after_drop, true, effects_out);
            return;
        }
        if (engine->repeat_mode == PLAYBACK_REPEAT_ALL) {
            load_slot_track(engine, 0u, true, effects_out);
            return;
        }
        load_slot_track(engine, 0u, false, effects_out);
        char toast[64];
        snprintf(toast, sizeof(toast), "END OF %s",
                 playback_engine_context_badge(engine->context.kind));
        set_toast(effects_out, toast);
        return;
    }

    if (engine->repeat_mode == PLAYBACK_REPEAT_ONE) {
        engine->elapsed_ms = 0u;
        arm_slice_launch(engine, effects_out);
        return;
    }

    if (engine->current_slot + 1u < engine->queue_total) {
        load_slot_track(engine, engine->current_slot + 1u, true, effects_out);
        return;
    }

    if (engine->repeat_mode == PLAYBACK_REPEAT_ALL) {
        int64_t last_track_id = engine->active_track.track_id;
        if (engine->shuffle_enabled && engine->queue_total > 1u) {
            (void)library_db_reshuffle_playback_queue(engine->database, 0, last_track_id,
                                                      &engine->queue_total);
        }
        load_slot_track(engine, 0u, true, effects_out);
        return;
    }

    /* Repeat OFF at last track: reset to slot 0 at 0:00 paused + END OF <CONTEXT> */
    load_slot_track(engine, 0u, false, effects_out);
    char toast[64];
    snprintf(toast, sizeof(toast), "END OF %s",
             playback_engine_context_badge(engine->context.kind));
    set_toast(effects_out, toast);
}

void playback_engine_handle_ipc_status(struct playback_engine *engine,
                                       const struct playback_status_update *status,
                                       struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || status == NULL || !engine->has_active_track) {
        return;
    }

    if (!engine->is_playing && engine->slice_count == 0u && !engine->awaiting_slice_load &&
        status->has_state && status->state == IPC_PLAYBACK_PLAYING) {
        /* Ignore stale playing frames after Repeat OFF stops at end of queue. */
        return;
    }

    if (status->has_track_path) {
        if (strcmp(status->track_path, engine->active_track.path) == 0) {
            engine->awaiting_slice_load = false;
            if (status->duration_ms > 0u) {
                engine->duration_ms = status->duration_ms;
            }
            if (status->has_state && status->state == IPC_PLAYBACK_PLAYING) {
                engine->is_playing = true;
                if (!status->is_loading_state) {
                    engine->audio_started = true;
                }
                if (engine->has_pending_resume_seek && effects_out != NULL) {
                    effects_out->send_seek = true;
                    effects_out->seek_ms = engine->pending_resume_seek_ms;
                    engine->elapsed_ms = engine->pending_resume_seek_ms;
                    engine->has_pending_resume_seek = false;
                    engine->pending_resume_seek_ms = 0u;
                }
            } else if (status->has_state && status->state == IPC_PLAYBACK_PAUSED) {
                engine->is_playing = false;
            }
            return;
        }

        if (engine->awaiting_slice_load) {
            /* Ignore stale track frame from previous slice while new 0100 is loading. */
            return;
        }

        /* Gapless auto-advance inside the active 32-track slice! */
        uint32_t expected_next_slot = engine->current_slot + 1u;
        struct library_db_track_details expected_next;
        if (!engine->ephemeral_active && expected_next_slot < engine->queue_total &&
            library_db_get_queue_track(engine->database, expected_next_slot, &expected_next) &&
            strcmp(status->track_path, expected_next.path) == 0) {
            int64_t prev_album = engine->active_track.album_id;
            engine->current_slot = expected_next_slot;
            engine->active_track = expected_next;
            engine->is_playing = true;
            engine->audio_started = !status->is_loading_state;
            engine->elapsed_ms = 0u;
            engine->duration_ms = status->duration_ms > 0u
                                      ? status->duration_ms
                                      : expected_next.duration_seconds * MILLISECONDS_PER_SECOND;
            engine->persistence_dirty = true;
            if (effects_out != NULL) {
                effects_out->track_changed = true;
                if (prev_album == expected_next.album_id) {
                    effects_out->show_same_album_banner = true;
                    effects_out->vinyl_nudge_radians = 0.14f;
                } else {
                    effects_out->start_cross_album_slide = true;
                    effects_out->slide_direction = 1;
                    effects_out->vinyl_nudge_radians = 0.42f;
                }
            }
            return;
        }

        /* Slice diverged (e.g. user toggled shuffle mid-slice): re-sync from SQLite queue! */
        handle_natural_eof(engine, effects_out);
        return;
    }

    if (status->has_state) {
        if (status->state == IPC_PLAYBACK_STOPPED) {
            if (engine->awaiting_slice_load) {
                return;
            }
            handle_natural_eof(engine, effects_out);
            return;
        }
        if (status->state == IPC_PLAYBACK_PLAYING) {
            engine->is_playing = true;
            if (!status->is_loading_state && !engine->awaiting_slice_load) {
                engine->audio_started = true;
            }
            if (engine->has_pending_resume_seek && !engine->awaiting_slice_load &&
                effects_out != NULL) {
                effects_out->send_seek = true;
                effects_out->seek_ms = engine->pending_resume_seek_ms;
                engine->elapsed_ms = engine->pending_resume_seek_ms;
                engine->has_pending_resume_seek = false;
                engine->pending_resume_seek_ms = 0u;
            }
        } else if (status->state == IPC_PLAYBACK_PAUSED) {
            engine->is_playing = false;
        }
    }
}

bool playback_engine_handle_ipc_elapsed(struct playback_engine *engine, uint32_t elapsed_ms) {
    if (engine == NULL || !engine->has_active_track || engine->awaiting_slice_load) {
        return false;
    }
    if (!engine->audio_started && !engine->has_pending_resume_seek && elapsed_ms > 1500u) {
        return false;
    }
    if (engine->duration_ms > 0u && elapsed_ms > engine->duration_ms) {
        elapsed_ms = engine->duration_ms;
    }
    if (elapsed_ms > 0u) {
        engine->audio_started = true;
    }
    engine->elapsed_ms = elapsed_ms;
    return true;
}

void playback_engine_handle_ipc_stop(struct playback_engine *engine,
                                     struct playback_engine_effects *effects_out) {
    clear_effects(effects_out);
    if (engine == NULL || !engine->has_active_track || engine->awaiting_slice_load) {
        return;
    }
    handle_natural_eof(engine, effects_out);
}

void playback_engine_peek_skip(const struct playback_engine *engine, int32_t direction,
                               struct playback_skip_preview *preview_out) {
    if (preview_out == NULL) {
        return;
    }
    memset(preview_out, 0, sizeof(*preview_out));
    if (engine == NULL || engine->database == NULL || !engine->has_active_track ||
        engine->queue_total == 0u || direction == 0) {
        preview_out->action = PLAYBACK_SKIP_BLOCKED;
        return;
    }
    int32_t step = direction > 0 ? 1 : -1;
    if (step < 0 && engine->elapsed_ms > PREVIOUS_RESTART_THRESHOLD_MS) {
        preview_out->action = PLAYBACK_SKIP_RESTART;
        preview_out->same_album = true;
        preview_out->target_track = engine->active_track;
        return;
    }

    if (step > 0) {
        if (engine->current_slot + 1u < engine->queue_total) {
            if (library_db_get_queue_track(engine->database, engine->current_slot + 1u,
                                           &preview_out->target_track)) {
                preview_out->action = PLAYBACK_SKIP_TRACK;
                preview_out->same_album =
                    (preview_out->target_track.album_id == engine->active_track.album_id);
                return;
            }
        }
        if (engine->repeat_mode == PLAYBACK_REPEAT_ALL) {
            if (engine->shuffle_enabled && engine->queue_total > 1u) {
                preview_out->action = PLAYBACK_SKIP_RESHUFFLE_WRAP;
                preview_out->same_album = true;
                return;
            }
            if (library_db_get_queue_track(engine->database, 0u, &preview_out->target_track)) {
                preview_out->action = PLAYBACK_SKIP_TRACK;
                preview_out->same_album =
                    (preview_out->target_track.album_id == engine->active_track.album_id);
                return;
            }
        }
        preview_out->action = PLAYBACK_SKIP_BLOCKED;
        return;
    }

    /* step < 0 */
    if (engine->current_slot > 0u) {
        if (library_db_get_queue_track(engine->database, engine->current_slot - 1u,
                                       &preview_out->target_track)) {
            preview_out->action = PLAYBACK_SKIP_TRACK;
            preview_out->same_album =
                (preview_out->target_track.album_id == engine->active_track.album_id);
            return;
        }
    }
    if (engine->repeat_mode == PLAYBACK_REPEAT_ALL) {
        if (library_db_get_queue_track(engine->database, engine->queue_total - 1u,
                                       &preview_out->target_track)) {
            preview_out->action = PLAYBACK_SKIP_TRACK;
            preview_out->same_album =
                (preview_out->target_track.album_id == engine->active_track.album_id);
            return;
        }
    }
    preview_out->action = PLAYBACK_SKIP_BLOCKED;
}
