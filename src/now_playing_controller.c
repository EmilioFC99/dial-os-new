#include "now_playing_controller.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NP_PI 3.14159265359f
#define NP_TWO_PI 6.28318530718f

static float clamp_f32(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static float approach_f32(float current, float target, float rate, float delta_seconds) {
    float factor = 1.0f - expf(-rate * delta_seconds);
    return current + (target - current) * factor;
}

static float wrap_angle_delta(float delta) {
    while (delta > NP_PI) {
        delta -= NP_TWO_PI;
    }
    while (delta < -NP_PI) {
        delta += NP_TWO_PI;
    }
    return delta;
}

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

static void clear_outputs(struct playback_engine_effects *effects_out, bool *volume_changed_out,
                          enum now_playing_action_kind *action_out) {
    if (effects_out != NULL) {
        memset(effects_out, 0, sizeof(*effects_out));
    }
    if (volume_changed_out != NULL) {
        *volume_changed_out = false;
    }
    if (action_out != NULL) {
        *action_out = NP_ACTION_NONE;
    }
}

static void refill_queue_window(struct now_playing_controller *controller, uint32_t center_slot) {
    if (controller->database == NULL || controller->engine == NULL ||
        controller->engine->queue_total == 0u) {
        controller->queue_window_start = 0u;
        controller->queue_window_count = 0u;
        return;
    }
    uint32_t half = NP_QUEUE_WINDOW_SIZE / 2u;
    uint32_t start = center_slot > half ? (center_slot - half) : 0u;
    if (start + NP_QUEUE_WINDOW_SIZE > controller->engine->queue_total &&
        controller->engine->queue_total > NP_QUEUE_WINDOW_SIZE) {
        start = controller->engine->queue_total - NP_QUEUE_WINDOW_SIZE;
    }
    controller->queue_window_start = start;
    controller->queue_window_count = library_db_fetch_queue_window(
        controller->database, start, NP_QUEUE_WINDOW_SIZE, controller->queue_window);
}

static void ensure_queue_window_covers(struct now_playing_controller *controller, uint32_t slot) {
    if (controller->engine == NULL || controller->engine->queue_total == 0u) {
        return;
    }
    if (controller->queue_window_count == 0u ||
        slot < controller->queue_window_start + NP_QUEUE_PREFETCH_MARGIN ||
        (slot + NP_QUEUE_PREFETCH_MARGIN >
             controller->queue_window_start + controller->queue_window_count &&
         controller->queue_window_start + controller->queue_window_count <
             controller->engine->queue_total)) {
        refill_queue_window(controller, slot);
    }
}

static void refresh_playlist_picker(struct now_playing_controller *controller) {
    if (controller->database == NULL || controller->engine == NULL ||
        !controller->engine->has_active_track) {
        controller->picker_count = 0u;
        controller->picker_total = 0u;
        return;
    }
    int64_t default_id = 0;
    (void)library_db_ensure_default_playlist(controller->database, &default_id);
    controller->picker_count = library_db_list_playlists_for_track(
        controller->database, controller->engine->active_track.track_id, 0u,
        NP_PICKER_WINDOW_SIZE, controller->picker_items, &controller->picker_total);
    controller->picker_scroll = 0.0f;
    controller->picker_pressed_row = -1;
}

static int32_t hit_test_quick_action_button(int32_t point_x, int32_t point_y) {
    static const float button_x[6] = {104.0f, 180.0f, 256.0f, 104.0f, 180.0f, 256.0f};
    static const float button_y[6] = {168.0f, 168.0f, 168.0f, 238.0f, 238.0f, 238.0f};
    const float hit_radius_sq = 29.0f * 29.0f;
    for (int32_t idx = 0; idx < 6; idx++) {
        float dx = (float)point_x - button_x[idx];
        float dy = (float)point_y - button_y[idx];
        if (dx * dx + dy * dy <= hit_radius_sq) {
            return idx;
        }
    }
    return -1;
}

static int32_t hit_test_picker_row(const struct now_playing_controller *controller, int32_t point_x,
                                   int32_t point_y) {
    if (point_x < 78 || point_x > 282) {
        return -1;
    }
    uint32_t visible =
        controller->picker_count < NP_PICKER_VISIBLE_ROWS ? controller->picker_count : NP_PICKER_VISIBLE_ROWS;
    uint32_t base_offset = (uint32_t)clamp_f32(roundf(controller->picker_scroll), 0.0f,
                                               (float)(controller->picker_count > NP_PICKER_VISIBLE_ROWS
                                                           ? controller->picker_count - NP_PICKER_VISIBLE_ROWS
                                                           : 0u));
    for (uint32_t row = 0u; row < visible; row++) {
        int32_t top = 132 + (int32_t)row * 35;
        if (point_y >= top && point_y <= top + 32) {
            return (int32_t)(base_offset + row);
        }
    }
    return -1;
}

static int32_t hit_test_queue_row(const struct now_playing_controller *controller, int32_t point_x,
                                  int32_t point_y) {
    if (controller->engine == NULL || controller->engine->queue_total == 0u ||
        point_y < 102 || point_y > 316) {
        return -1;
    }
    for (uint32_t idx = 0u; idx < controller->engine->queue_total; idx++) {
        float row_y = 130.0f + ((float)idx - controller->queue_scroll) * 58.0f;
        if (row_y < 86.0f || row_y > 330.0f) {
            continue;
        }
        float dy_center = fabsf(row_y - 196.0f);
        float half_width = clamp_f32(132.0f - dy_center * 0.16f, 92.0f, 132.0f);
        if ((float)point_x >= 180.0f - half_width && (float)point_x <= 180.0f + half_width &&
            (float)point_y >= row_y - 25.0f && (float)point_y <= row_y + 25.0f) {
            return (int32_t)idx;
        }
    }
    return -1;
}

static void format_duration_mm_ss(uint32_t seconds, char *out, size_t cap) {
    uint32_t mins = seconds / 60u;
    uint32_t secs = seconds % 60u;
    snprintf(out, cap, "%u:%02u", (unsigned)mins, (unsigned)secs);
}

static void trigger_same_album_banner(struct now_playing_controller *controller) {
    if (controller->engine == NULL || !controller->engine->has_active_track) {
        return;
    }
    if (controller->lyrics_open || controller->quick_actions_open ||
        controller->queue_sheet_open || controller->details_sheet_open ||
        controller->playlist_picker_open) {
        return;
    }
    controller->banner_active = true;
    controller->banner_timer = 0.0f;
    copy_text_bounded(controller->banner_title, sizeof(controller->banner_title),
                      controller->engine->active_track.title);
    char dur_text[16];
    format_duration_mm_ss(controller->engine->active_track.duration_seconds, dur_text,
                          sizeof(dur_text));
    if (controller->engine->context.kind == LIBRARY_DB_COLLECTION_TRACKS_ALBUM) {
        snprintf(controller->banner_subtitle, sizeof(controller->banner_subtitle),
                 "%02u / %02u · %s", (unsigned)(controller->engine->current_slot + 1u),
                 (unsigned)controller->engine->queue_total, dur_text);
    } else if (controller->engine->context.kind == LIBRARY_DB_COLLECTION_TRACKS_ARTIST) {
        snprintf(controller->banner_subtitle, sizeof(controller->banner_subtitle), "%s · %s",
                 controller->engine->active_track.album, dur_text);
    } else {
        snprintf(controller->banner_subtitle, sizeof(controller->banner_subtitle), "%s · %s",
                 controller->engine->active_track.artist, dur_text);
    }
}

void now_playing_controller_initialize(struct now_playing_controller *controller,
                                       struct playback_engine *engine,
                                       struct library_db *database,
                                       struct now_playing_media *media) {
    if (controller == NULL) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    controller->engine = engine;
    controller->database = database;
    controller->media = media;
    controller->quick_action_pressed_index = -1;
    controller->picker_pressed_row = -1;
    controller->queue_pressed_row = -1;
    controller->lyrics_last_cue_index = -1;
    controller->lyrics_prev_cue_index = -1;
    controller->open_transition_progress = 1.0f;
}

void now_playing_controller_enter(struct now_playing_controller *controller,
                                  enum now_playing_return_target return_target,
                                  enum now_playing_open_origin open_origin,
                                  bool skip_open_transition, float initial_vinyl_radians,
                                  uint8_t volume_level) {
    if (controller == NULL) {
        return;
    }
    controller->return_target = return_target;
    controller->open_origin = open_origin;
    controller->open_transition_progress = skip_open_transition ? 1.0f : 0.0f;
    controller->exit_transition_active = false;
    controller->exit_transition_progress = 0.0f;
    controller->vinyl_rotation_radians = initial_vinyl_radians;
    controller->vinyl_nudge_radians = 0.0f;
    controller->volume_level = volume_level;
    controller->quick_actions_open = false;
    controller->quick_actions_progress = 0.0f;
    controller->playlist_picker_open = false;
    controller->playlist_picker_progress = 0.0f;
    controller->details_sheet_open = false;
    controller->details_sheet_progress = 0.0f;
    controller->queue_sheet_open = false;
    controller->queue_sheet_progress = 0.0f;
    controller->lyrics_open = false;
    controller->lyrics_progress = 0.0f;
    controller->is_seeking = false;
    controller->seek_hold_seconds = 0.0f;
    controller->seek_charge_progress = 0.0f;
    controller->seek_expand_progress = 0.0f;
    controller->volume_hud_active = false;
    controller->volume_hud_alpha = 0.0f;
    controller->back_swipe_progress = 0.0f;
    controller->skip_drag_offset_x = 0.0f;
    controller->skip_peek_valid = false;
    controller->skip_slide_active = false;
    controller->pending_single_tap = false;
    controller->pointer_mode = NP_PTR_NONE;
    controller->pointer_active = false;
    if (controller->engine != NULL) {
        controller->interpolated_elapsed_ms = (float)controller->engine->elapsed_ms;
        controller->elapsed_anchor_ms = (float)controller->engine->elapsed_ms;
        controller->elapsed_since_anchor_seconds = 0.0f;
        controller->has_elapsed_anchor = false;
        controller->last_audio_started = controller->engine->audio_started;
    }
}

void now_playing_controller_show_volume_hud(struct now_playing_controller *controller,
                                            uint8_t volume_level) {
    if (controller == NULL) {
        return;
    }
    controller->volume_level = volume_level > VOLUME_HARD_MAX ? VOLUME_HARD_MAX : volume_level;
    controller->volume_hud_active = true;
    controller->volume_hud_hold_seconds = NP_VOLUME_HUD_HOLD_SECONDS;
    if (controller->volume_hud_alpha < 0.05f) {
        controller->volume_hud_alpha = 1.0f;
    }
}

void now_playing_controller_trigger_play_pulse(struct now_playing_controller *controller,
                                               bool is_playing) {
    if (controller == NULL) {
        return;
    }
    controller->play_pulse_alpha = 1.0f;
    controller->play_pulse_is_playing = is_playing;
}

void now_playing_controller_show_toast(struct now_playing_controller *controller, const char *text) {
    if (controller == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    controller->toast_active = true;
    controller->toast_timer = NP_TOAST_HOLD_SECONDS;
    controller->toast_alpha = 1.0f;
    copy_text_bounded(controller->toast_text, sizeof(controller->toast_text), text);
}

void now_playing_controller_apply_effects(struct now_playing_controller *controller,
                                          const struct playback_engine_effects *effects) {
    if (controller == NULL || effects == NULL) {
        return;
    }
    if (effects->vinyl_nudge_radians != 0.0f) {
        controller->vinyl_nudge_radians = effects->vinyl_nudge_radians;
    }
    if (effects->show_same_album_banner) {
        trigger_same_album_banner(controller);
    }
    if (effects->start_cross_album_slide) {
        controller->skip_slide_active = true;
        controller->skip_slide_direction = effects->slide_direction >= 0 ? 1 : -1;
        controller->skip_slide_progress = 0.0f;
        controller->skip_outgoing_angle = controller->vinyl_rotation_radians;
    }
    if (effects->has_toast) {
        now_playing_controller_show_toast(controller, effects->toast_text);
    }
    if ((effects->track_changed || effects->send_seek) && controller->engine != NULL) {
        controller->interpolated_elapsed_ms = (float)controller->engine->elapsed_ms;
        controller->elapsed_anchor_ms = (float)controller->engine->elapsed_ms;
        controller->elapsed_since_anchor_seconds = 0.0f;
        controller->has_elapsed_anchor = false;
        controller->last_audio_started = controller->engine->audio_started;
    }
    if (effects->track_changed && controller->engine != NULL) {
        controller->lyrics_last_cue_index = -1;
        controller->lyrics_prev_cue_index = -1;
        controller->lyrics_prev_cue_text[0] = '\0';
        controller->lyrics_cue_transition = 1.0f;
        if (!controller->engine->is_playing) {
            controller->vinyl_nudge_radians = 0.0f;
        }
    }
}

void now_playing_controller_on_ipc_elapsed(struct now_playing_controller *controller,
                                           uint32_t elapsed_ms) {
    if (controller == NULL) {
        return;
    }
    controller->elapsed_anchor_ms = (float)elapsed_ms;
    controller->elapsed_since_anchor_seconds = 0.0f;
    controller->has_elapsed_anchor = true;
    controller->last_audio_started = true;
    if (!controller->is_seeking &&
        fabsf(controller->interpolated_elapsed_ms - (float)elapsed_ms) > 250.0f) {
        controller->interpolated_elapsed_ms = (float)elapsed_ms;
    }
}

bool now_playing_controller_step_back(struct now_playing_controller *controller,
                                      enum now_playing_action_kind *action_out) {
    if (action_out != NULL) {
        *action_out = NP_ACTION_NONE;
    }
    if (controller == NULL) {
        return false;
    }
    if (controller->playlist_picker_open) {
        controller->playlist_picker_open = false;
        return true;
    }
    if (controller->details_sheet_open) {
        controller->details_sheet_open = false;
        return true;
    }
    if (controller->queue_sheet_open) {
        controller->queue_sheet_open = false;
        return true;
    }
    if (controller->quick_actions_open) {
        controller->quick_actions_open = false;
        return true;
    }
    if (controller->lyrics_open) {
        controller->lyrics_open = false;
        return true;
    }
    if (controller->return_target == NP_RETURN_LIBRARY && controller->engine != NULL &&
        controller->engine->has_active_track) {
        controller->exit_transition_active = true;
        controller->exit_transition_progress = 0.0f;
        controller->back_swipe_progress = 0.0f;
        controller->pointer_active = false;
        controller->pointer_mode = NP_PTR_NONE;
        return true;
    }
    if (action_out != NULL) {
        *action_out = NP_ACTION_EXIT;
    }
    return true;
}

const struct library_db_row *now_playing_controller_get_queue_row(
    const struct now_playing_controller *controller, uint32_t slot) {
    if (controller == NULL || slot < controller->queue_window_start) {
        return NULL;
    }
    uint32_t idx = slot - controller->queue_window_start;
    if (idx >= controller->queue_window_count) {
        return NULL;
    }
    return &controller->queue_window[idx];
}

void now_playing_controller_step(struct now_playing_controller *controller, float delta_seconds,
                                   struct playback_engine_effects *effects_out,
                                   bool *volume_changed_out,
                                   enum now_playing_action_kind *action_out) {
    clear_outputs(effects_out, volume_changed_out, action_out);
    if (controller == NULL) {
        return;
    }
    if (delta_seconds < 0.0f) {
        delta_seconds = 0.0f;
    } else if (delta_seconds > 0.05f) {
        delta_seconds = 0.05f;
    }
    controller->elapsed_seconds += delta_seconds;

    if (controller->exit_transition_active) {
        controller->exit_transition_progress =
            clamp_f32(controller->exit_transition_progress +
                          delta_seconds / NP_EXIT_DURATION_SECONDS,
                      0.0f, 1.0f);
        if (controller->exit_transition_progress >= 1.0f) {
            controller->exit_transition_active = false;
            if (action_out != NULL) {
                *action_out = NP_ACTION_EXIT;
            }
        }
    } else if (controller->open_transition_progress < 1.0f) {
        controller->open_transition_progress =
            clamp_f32(controller->open_transition_progress +
                          delta_seconds / NP_OPEN_DURATION_SECONDS,
                      0.0f, 1.0f);
    }

    bool playing = (controller->engine != NULL && controller->engine->has_active_track &&
                    controller->engine->is_playing && !controller->is_seeking);
    float target_vel = playing ? VINYL_SPIN_RADIANS_PER_SECOND : 0.0f;
    float rate = playing ? 5.2f : 3.8f;
    controller->vinyl_angular_velocity =
        approach_f32(controller->vinyl_angular_velocity, target_vel, rate, delta_seconds);
    if (!playing && fabsf(controller->vinyl_angular_velocity) < 0.002f) {
        controller->vinyl_angular_velocity = 0.0f;
    }
    controller->vinyl_rotation_radians += controller->vinyl_angular_velocity * delta_seconds;
    controller->vinyl_nudge_radians =
        approach_f32(controller->vinyl_nudge_radians, 0.0f, 8.5f, delta_seconds);

    if (controller->engine != NULL && controller->engine->has_active_track) {
        if (controller->is_seeking) {
            controller->interpolated_elapsed_ms = (float)controller->seek_preview_ms;
            controller->has_elapsed_anchor = false;
        } else if (controller->engine->is_playing) {
            if (!controller->engine->audio_started) {
                controller->interpolated_elapsed_ms = (float)controller->engine->elapsed_ms;
                controller->elapsed_anchor_ms = (float)controller->engine->elapsed_ms;
                controller->elapsed_since_anchor_seconds = 0.0f;
                controller->has_elapsed_anchor = false;
                controller->last_audio_started = false;
            } else {
                if (!controller->last_audio_started) {
                    controller->last_audio_started = true;
                    if (!controller->has_elapsed_anchor) {
                        controller->elapsed_anchor_ms = (float)controller->engine->elapsed_ms;
                        controller->elapsed_since_anchor_seconds = 0.0f;
                    }
                }
                controller->elapsed_since_anchor_seconds += delta_seconds;
                controller->interpolated_elapsed_ms += delta_seconds * 1000.0f;
                float target_ms = controller->elapsed_anchor_ms +
                                  controller->elapsed_since_anchor_seconds * 1000.0f;
                float diff = target_ms - controller->interpolated_elapsed_ms;
                if (fabsf(diff) > 350.0f) {
                    controller->interpolated_elapsed_ms = target_ms;
                } else {
                    controller->interpolated_elapsed_ms +=
                        diff * clamp_f32(delta_seconds * 8.0f, 0.0f, 1.0f);
                }
            }
            if (controller->engine->duration_ms > 0u &&
                controller->interpolated_elapsed_ms > (float)controller->engine->duration_ms) {
                controller->interpolated_elapsed_ms = (float)controller->engine->duration_ms;
            }
        } else {
            controller->interpolated_elapsed_ms = (float)controller->engine->elapsed_ms;
            controller->elapsed_anchor_ms = (float)controller->engine->elapsed_ms;
            controller->elapsed_since_anchor_seconds = 0.0f;
            controller->has_elapsed_anchor = false;
        }
    }

    /* Single-tap disambiguation timer */
    if (controller->pending_single_tap) {
        controller->pending_single_tap_timer += delta_seconds;
        if (controller->pending_single_tap_timer >= NP_DOUBLE_TAP_WINDOW_SECONDS) {
            controller->pending_single_tap = false;
            controller->pending_single_tap_timer = 0.0f;
            if (!controller->queue_sheet_open && !controller->details_sheet_open &&
                !controller->playlist_picker_open) {
                controller->quick_actions_open = true;
                controller->quick_actions_idle_timer = NP_QUICK_ACTIONS_AUTO_CLOSE_SECONDS;
            }
        }
    }

    /* Center hold-charge -> Seek Mode activation */
    if (controller->pointer_mode == NP_PTR_CENTER_TAP_OR_HOLD && controller->pointer_active &&
        controller->pointer_max_drift_px < 12.0f && controller->engine != NULL &&
        controller->engine->has_active_track) {
        controller->seek_hold_seconds += delta_seconds;
        if (controller->seek_hold_seconds >= NP_SEEK_CHARGE_DELAY_SECONDS) {
            controller->seek_charge_progress = clamp_f32(
                (controller->seek_hold_seconds - NP_SEEK_CHARGE_DELAY_SECONDS) /
                    (NP_SEEK_TRIGGER_SECONDS - NP_SEEK_CHARGE_DELAY_SECONDS),
                0.0f, 1.0f);
        }
        if (controller->seek_hold_seconds >= NP_SEEK_TRIGGER_SECONDS) {
            controller->pointer_mode = NP_PTR_SEEK;
            controller->is_seeking = true;
            controller->seek_over_lyrics = controller->lyrics_open;
            controller->seek_preview_ms = controller->engine->elapsed_ms;
            controller->seek_charge_progress = 1.0f;
            controller->pointer_last_angle = atan2f((float)controller->pointer_last_y - 180.0f,
                                                    (float)controller->pointer_last_x - 180.0f);
        }
    } else if (!controller->is_seeking) {
        controller->seek_hold_seconds = 0.0f;
        controller->seek_charge_progress =
            approach_f32(controller->seek_charge_progress, 0.0f, 14.0f, delta_seconds);
    }

    controller->seek_expand_progress = approach_f32(
        controller->seek_expand_progress, controller->is_seeking ? 1.0f : 0.0f,
        controller->is_seeking ? 18.0f : 13.0f, delta_seconds);

    /* Overlay springs */
    controller->quick_actions_progress = approach_f32(
        controller->quick_actions_progress, controller->quick_actions_open ? 1.0f : 0.0f, 14.0f,
        delta_seconds);
    controller->playlist_picker_progress = approach_f32(
        controller->playlist_picker_progress, controller->playlist_picker_open ? 1.0f : 0.0f,
        15.0f, delta_seconds);
    controller->details_sheet_progress = approach_f32(
        controller->details_sheet_progress, controller->details_sheet_open ? 1.0f : 0.0f, 14.0f,
        delta_seconds);
    controller->queue_sheet_progress = approach_f32(
        controller->queue_sheet_progress, controller->queue_sheet_open ? 1.0f : 0.0f, 14.0f,
        delta_seconds);
    controller->lyrics_progress = approach_f32(
        controller->lyrics_progress, controller->lyrics_open ? 1.0f : 0.0f, 10.5f, delta_seconds);

    if (controller->quick_actions_open && !controller->pointer_active &&
        !controller->playlist_picker_open && !controller->details_sheet_open) {
        controller->quick_actions_idle_timer -= delta_seconds;
        if (controller->quick_actions_idle_timer <= 0.0f) {
            controller->quick_actions_open = false;
            controller->quick_actions_idle_timer = 0.0f;
        }
    }

    /* Queue spring snap */
    if (controller->queue_sheet_open && controller->pointer_mode != NP_PTR_QUEUE_LIST &&
        controller->engine != NULL && controller->engine->queue_total > 0u) {
        int32_t max_slot = (int32_t)controller->engine->queue_total - 1;
        if (controller->queue_target_index < 0) {
            controller->queue_target_index = 0;
        } else if (controller->queue_target_index > max_slot) {
            controller->queue_target_index = max_slot;
        }
        controller->queue_scroll = approach_f32(
            controller->queue_scroll, (float)controller->queue_target_index, 13.5f, delta_seconds);
        uint32_t center_slot = (uint32_t)clamp_f32(roundf(controller->queue_scroll), 0.0f,
                                                   (float)max_slot);
        ensure_queue_window_covers(controller, center_slot);
    }

    /* Back-swipe & skip drag springs */
    if (controller->pointer_mode != NP_PTR_BACK_SWIPE) {
        controller->back_swipe_progress =
            approach_f32(controller->back_swipe_progress, 0.0f, 12.0f, delta_seconds);
    }
    if (controller->pointer_mode != NP_PTR_SKIP_DRAG) {
        controller->skip_drag_offset_x =
            approach_f32(controller->skip_drag_offset_x, 0.0f, 15.0f, delta_seconds);
    }
    if (controller->skip_slide_active) {
        controller->skip_slide_progress += delta_seconds / NP_SKIP_SLIDE_DURATION_SECONDS;
        if (controller->skip_slide_progress >= 1.0f) {
            controller->skip_slide_progress = 1.0f;
            controller->skip_slide_active = false;
        }
    }

    /* Volume HUD hold & fade */
    if (controller->volume_hud_active) {
        if (controller->pointer_mode != NP_PTR_VOLUME_BEZEL) {
            controller->volume_hud_hold_seconds -= delta_seconds;
            if (controller->volume_hud_hold_seconds <= 0.0f) {
                controller->volume_hud_alpha -= delta_seconds * 3.2f;
                if (controller->volume_hud_alpha <= 0.0f) {
                    controller->volume_hud_alpha = 0.0f;
                    controller->volume_hud_active = false;
                }
            } else {
                controller->volume_hud_alpha = 1.0f;
            }
        } else {
            controller->volume_hud_alpha = 1.0f;
        }
    }

    /* Transient pulse, banner, toast */
    if (controller->play_pulse_alpha > 0.0f) {
        controller->play_pulse_alpha =
            clamp_f32(controller->play_pulse_alpha - delta_seconds * 2.5f, 0.0f, 1.0f);
    }
    if (controller->banner_active) {
        controller->banner_timer += delta_seconds;
        if (controller->banner_timer >=
            NP_BANNER_IN_SECONDS + NP_BANNER_HOLD_SECONDS + NP_BANNER_OUT_SECONDS) {
            controller->banner_active = false;
        }
    }
    if (controller->toast_active) {
        controller->toast_timer -= delta_seconds;
        if (controller->toast_timer <= 0.0f) {
            controller->toast_alpha -= delta_seconds * 3.2f;
            if (controller->toast_alpha <= 0.0f) {
                controller->toast_alpha = 0.0f;
                controller->toast_active = false;
            }
        }
    }

    /* Lyrics cue transition tracking */
    if (controller->lyrics_open && controller->media != NULL && controller->engine != NULL) {
        struct lyrics_resolved_state resolved;
        bool lyrics_match = false;
        pthread_mutex_lock(&controller->media->mutex);
        lyrics_match =
            controller->media->active_lyrics_ready &&
            strcmp(controller->media->active_lyrics_path, controller->engine->active_track.path) ==
                0;
        if (lyrics_match) {
            now_playing_media_resolve_cue(&controller->media->active_lyrics,
                                          controller->lyrics_mode_pref,
                                          (uint32_t)controller->interpolated_elapsed_ms,
                                          controller->engine->duration_ms, &resolved);
        }
        pthread_mutex_unlock(&controller->media->mutex);
        if (lyrics_match) {
            if (resolved.active_index != controller->lyrics_last_cue_index) {
                controller->lyrics_prev_cue_index = controller->lyrics_last_cue_index;
                controller->lyrics_last_cue_index = resolved.active_index;
                controller->lyrics_cue_transition = 0.0f;
            }
            if (controller->lyrics_cue_transition < 1.0f) {
                controller->lyrics_cue_transition =
                    clamp_f32(controller->lyrics_cue_transition +
                                  delta_seconds / NP_LYRICS_TRANSITION_SECONDS,
                              0.0f, 1.0f);
            }
        }
    }
}

void now_playing_controller_handle_touch(struct now_playing_controller *controller,
                                         const struct touch_event *event,
                                         struct playback_engine_effects *effects_out,
                                         bool *volume_changed_out,
                                         enum now_playing_action_kind *action_out) {
    clear_outputs(effects_out, volume_changed_out, action_out);
    if (controller == NULL || event == NULL || controller->exit_transition_active) {
        return;
    }

    bool has_track = (controller->engine != NULL && controller->engine->has_active_track);

    if (event->kind == TOUCH_EVENT_DOWN) {
        controller->pointer_active = true;
        controller->pointer_start_x = event->x;
        controller->pointer_start_y = event->y;
        controller->pointer_last_x = event->x;
        controller->pointer_last_y = event->y;
        controller->pointer_last_timestamp_ms = event->timestamp_ms;
        controller->pointer_max_drift_px = 0.0f;

        if (!has_track) {
            if ((float)event->x <= LEFT_EDGE_SWIPE_ZONE_PX) {
                controller->pointer_mode = NP_PTR_LEFT_EDGE_PENDING;
            } else if (event->x >= 96 && event->x <= 264 && event->y >= 246 && event->y <= 288) {
                controller->pointer_mode = NP_PTR_IDLE_CTA;
                controller->idle_cta_pressed = true;
            } else {
                controller->pointer_mode = NP_PTR_NONE;
            }
            return;
        }

        if (controller->playlist_picker_open) {
            controller->pointer_mode = NP_PTR_PLAYLIST_PICKER;
            controller->pointer_start_scroll = controller->picker_scroll;
            controller->picker_pressed_row = hit_test_picker_row(controller, event->x, event->y);
            return;
        }
        if (controller->details_sheet_open) {
            controller->pointer_mode =
                ((float)event->x <= LEFT_EDGE_SWIPE_ZONE_PX) ? NP_PTR_LEFT_EDGE_PENDING : NP_PTR_NONE;
            return;
        }
        if (controller->queue_sheet_open) {
            if (event->y <= 104) {
                controller->pointer_mode = NP_PTR_QUEUE_HEADER;
            } else {
                controller->pointer_mode = NP_PTR_QUEUE_LIST;
                controller->pointer_start_scroll = controller->queue_scroll;
                controller->queue_velocity = 0.0f;
                controller->queue_pressed_row = hit_test_queue_row(controller, event->x, event->y);
            }
            return;
        }
        if (controller->quick_actions_open) {
            controller->pointer_mode = NP_PTR_QUICK_ACTIONS;
            controller->quick_action_pressed_index =
                hit_test_quick_action_button(event->x, event->y);
            return;
        }

        /* Bare vinyl or Lyrics */
        if ((float)event->x <= LEFT_EDGE_SWIPE_ZONE_PX) {
            controller->pointer_mode = NP_PTR_LEFT_EDGE_PENDING;
            return;
        }
        float dx = (float)event->x - 180.0f;
        float dy = (float)event->y - 180.0f;
        float radius = sqrtf(dx * dx + dy * dy);
        float angle = atan2f(dy, dx);
        if (radius >= NP_VOLUME_BEZEL_MIN_RADIUS_PX &&
            fabsf(angle) <= NP_VOLUME_BEZEL_MAX_ANGLE_RADIANS) {
            controller->pointer_mode = NP_PTR_VOLUME_PENDING;
            controller->pointer_last_angle = angle;
            controller->pointer_volume_accum = (float)controller->volume_level;
            return;
        }
        controller->pointer_mode = NP_PTR_CENTER_TAP_OR_HOLD;
        controller->seek_hold_seconds = 0.0f;
        controller->seek_charge_progress = 0.0f;
        return;
    }

    if (event->kind == TOUCH_EVENT_MOVE) {
        if (!controller->pointer_active) {
            return;
        }
        int32_t delta_x = event->x - controller->pointer_start_x;
        int32_t delta_y = event->y - controller->pointer_start_y;
        int32_t step_x = event->x - controller->pointer_last_x;
        int32_t step_y = event->y - controller->pointer_last_y;
        float drift = sqrtf((float)(delta_x * delta_x + delta_y * delta_y));
        if (drift > controller->pointer_max_drift_px) {
            controller->pointer_max_drift_px = drift;
        }

        switch (controller->pointer_mode) {
            case NP_PTR_LEFT_EDGE_PENDING:
                if (delta_x > 12 && (float)delta_x > fabsf((float)delta_y) * 1.2f) {
                    controller->pointer_mode = NP_PTR_BACK_SWIPE;
                    controller->back_swipe_progress =
                        clamp_f32((float)delta_x / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
                } else if (has_track && !controller->details_sheet_open &&
                           abs(delta_x) > 14 &&
                           fabsf((float)delta_x) > fabsf((float)delta_y) * 1.15f) {
                    controller->pointer_mode = NP_PTR_SKIP_DRAG;
                }
                break;

            case NP_PTR_BACK_SWIPE:
                controller->back_swipe_progress =
                    clamp_f32((float)delta_x / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
                break;

            case NP_PTR_VOLUME_PENDING:
                if (drift < NP_VOLUME_BEZEL_START_MOVE_PX) {
                    break;
                }
                if (delta_x < -14 && fabsf((float)delta_x) > fabsf((float)delta_y) * 1.55f) {
                    controller->pointer_mode = NP_PTR_SKIP_DRAG;
                    break;
                }
                controller->pointer_mode = NP_PTR_VOLUME_BEZEL;
                controller->volume_hud_active = true;
                controller->volume_hud_alpha = 1.0f;
                controller->volume_hud_hold_seconds = NP_VOLUME_HUD_HOLD_SECONDS;
                /* fall through to apply initial angle delta */

            case NP_PTR_VOLUME_BEZEL: {
                float angle = atan2f((float)event->y - 180.0f, (float)event->x - 180.0f);
                float delta_angle = wrap_angle_delta(angle - controller->pointer_last_angle);
                controller->pointer_last_angle = angle;
                controller->pointer_volume_accum = clamp_f32(
                    controller->pointer_volume_accum -
                        (delta_angle / NP_VOLUME_BEZEL_FULL_ARC_RADIANS) * (float)VOLUME_HARD_MAX,
                    0.0f, (float)VOLUME_HARD_MAX);
                uint8_t next_vol = (uint8_t)lroundf(controller->pointer_volume_accum);
                if (next_vol != controller->volume_level) {
                    controller->volume_level = next_vol;
                    if (volume_changed_out != NULL) {
                        *volume_changed_out = true;
                    }
                }
                controller->volume_hud_hold_seconds = NP_VOLUME_HUD_HOLD_SECONDS;
                break;
            }

            case NP_PTR_CENTER_TAP_OR_HOLD:
                if (controller->pointer_start_y >= (int32_t)NP_QUEUE_PULL_START_Y &&
                    delta_y < -20 && fabsf((float)delta_y) > fabsf((float)delta_x) * 1.3f) {
                    controller->pointer_mode = NP_PTR_QUEUE_PULL;
                } else if (abs(delta_x) > 14 &&
                           fabsf((float)delta_x) > fabsf((float)delta_y) * 1.15f) {
                    controller->pointer_mode = NP_PTR_SKIP_DRAG;
                }
                break;

            case NP_PTR_SKIP_DRAG: {
                controller->skip_drag_offset_x = clamp_f32((float)delta_x, -180.0f, 180.0f);
                int32_t dir = delta_x < 0 ? 1 : -1;
                if (!controller->skip_peek_valid || controller->skip_peek_direction != dir) {
                    playback_engine_peek_skip(controller->engine, dir, &controller->skip_peek_cache);
                    controller->skip_peek_valid = true;
                    controller->skip_peek_direction = dir;
                    if (controller->media != NULL &&
                        controller->skip_peek_cache.action == PLAYBACK_SKIP_TRACK &&
                        !controller->skip_peek_cache.same_album) {
                        now_playing_media_request_peek(
                            controller->media, controller->skip_peek_cache.target_track.album_id,
                            controller->skip_peek_cache.target_track.album_uid);
                    }
                }
                break;
            }

            case NP_PTR_SEEK: {
                float dx = (float)event->x - 180.0f;
                float dy = (float)event->y - 180.0f;
                float radius = sqrtf(dx * dx + dy * dy);
                float duration = (float)(controller->engine != NULL ? controller->engine->duration_ms : 0u);
                float max_ms = duration > (float)NP_SEEK_END_GUARD_MS
                                   ? (duration - (float)NP_SEEK_END_GUARD_MS)
                                   : duration;
                if (radius >= NP_SEEK_CORE_RADIUS_PX) {
                    float angle = atan2f(dy, dx);
                    float d_angle = wrap_angle_delta(angle - controller->pointer_last_angle);
                    controller->pointer_last_angle = angle;
                    float next_ms = (float)controller->seek_preview_ms + (d_angle / NP_TWO_PI) * duration;
                    controller->seek_preview_ms = (uint32_t)clamp_f32(next_ms, 0.0f, max_ms);
                    controller->vinyl_rotation_radians += d_angle * 0.65f;
                } else {
                    float next_ms = (float)controller->seek_preview_ms + ((float)step_x / 180.0f) * duration * 0.35f;
                    controller->seek_preview_ms = (uint32_t)clamp_f32(next_ms, 0.0f, max_ms);
                    controller->vinyl_rotation_radians += (float)step_x * 0.012f;
                }
                break;
            }

            case NP_PTR_QUICK_ACTIONS:
                if (delta_x > 14 && (float)delta_x > fabsf((float)delta_y) * 1.2f &&
                    (float)controller->pointer_start_x <= LEFT_EDGE_SWIPE_ZONE_PX) {
                    controller->pointer_mode = NP_PTR_BACK_SWIPE;
                    controller->back_swipe_progress =
                        clamp_f32((float)delta_x / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
                } else if (controller->pointer_start_y >= (int32_t)NP_QUEUE_PULL_START_Y &&
                           delta_y < -36 && fabsf((float)delta_y) > fabsf((float)delta_x) * 1.3f) {
                    controller->quick_actions_open = false;
                    controller->queue_sheet_open = true;
                    if (controller->engine != NULL) {
                        controller->queue_target_index = (int32_t)controller->engine->current_slot;
                        controller->queue_scroll = (float)controller->queue_target_index;
                        refill_queue_window(controller, controller->engine->current_slot);
                    }
                    controller->pointer_mode = NP_PTR_NONE;
                } else if (drift > 10.0f) {
                    controller->quick_action_pressed_index = -1;
                }
                break;

            case NP_PTR_PLAYLIST_PICKER:
                if (delta_x > 14 && (float)delta_x > fabsf((float)delta_y) * 1.2f &&
                    (float)controller->pointer_start_x <= LEFT_EDGE_SWIPE_ZONE_PX) {
                    controller->pointer_mode = NP_PTR_BACK_SWIPE;
                    controller->back_swipe_progress =
                        clamp_f32((float)delta_x / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
                } else if (controller->picker_count > NP_PICKER_VISIBLE_ROWS &&
                           fabsf((float)delta_y) > 8.0f) {
                    controller->picker_pressed_row = -1;
                    float max_scroll = (float)(controller->picker_count - NP_PICKER_VISIBLE_ROWS);
                    controller->picker_scroll = clamp_f32(
                        controller->pointer_start_scroll - (float)delta_y / 35.0f, 0.0f, max_scroll);
                }
                break;

            case NP_PTR_QUEUE_HEADER:
                if (delta_x > 24 && (float)delta_x > fabsf((float)delta_y) * 1.35f) {
                    controller->pointer_mode = NP_PTR_BACK_SWIPE;
                    controller->back_swipe_progress =
                        clamp_f32((float)delta_x / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
                } else if (delta_y > 40 && (float)delta_y > fabsf((float)delta_x) * 1.2f) {
                    controller->queue_sheet_open = false;
                    controller->pointer_mode = NP_PTR_NONE;
                }
                break;

            case NP_PTR_QUEUE_LIST: {
                if (controller->pointer_start_x <= 110 && delta_x > 28 &&
                    (float)delta_x > fabsf((float)delta_y) * 1.45f) {
                    controller->pointer_mode = NP_PTR_BACK_SWIPE;
                    controller->back_swipe_progress =
                        clamp_f32((float)delta_x / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
                    break;
                }
                if (controller->pointer_start_scroll <= 0.05f && delta_y > 68 &&
                    fabsf((float)delta_y) > fabsf((float)delta_x) * 1.2f) {
                    controller->queue_sheet_open = false;
                    controller->pointer_mode = NP_PTR_NONE;
                    break;
                }
                if (drift > 10.0f) {
                    controller->queue_pressed_row = -1;
                }
                uint32_t total = controller->engine != NULL ? controller->engine->queue_total : 0u;
                float max_scroll = total > 0u ? (float)(total - 1u) : 0.0f;
                controller->queue_scroll = clamp_f32(
                    controller->pointer_start_scroll - (float)delta_y / 58.0f, -0.38f,
                    max_scroll + 0.35f);
                uint64_t dt_ms = event->timestamp_ms > controller->pointer_last_timestamp_ms
                                     ? (event->timestamp_ms - controller->pointer_last_timestamp_ms)
                                     : 26ull;
                controller->queue_velocity = -(float)step_y / ((float)dt_ms * 0.058f);
                uint32_t center = (uint32_t)clamp_f32(roundf(controller->queue_scroll), 0.0f, max_scroll);
                ensure_queue_window_covers(controller, center);
                break;
            }

            default:
                break;
        }

        controller->pointer_last_x = event->x;
        controller->pointer_last_y = event->y;
        controller->pointer_last_timestamp_ms = event->timestamp_ms;
        return;
    }

    if (event->kind == TOUCH_EVENT_UP) {
        enum now_playing_pointer_mode mode = controller->pointer_mode;
        bool was_tap = (controller->pointer_max_drift_px <= HOME_TAP_DRIFT_LIMIT_PX);
        int32_t total_delta_y = event->y - controller->pointer_start_y;
        controller->pointer_active = false;
        controller->pointer_mode = NP_PTR_NONE;
        controller->idle_cta_pressed = false;

        if (mode == NP_PTR_IDLE_CTA) {
            if (was_tap && action_out != NULL) {
                *action_out = NP_ACTION_BROWSE_LIBRARY;
            }
            return;
        }
        if (mode == NP_PTR_BACK_SWIPE) {
            if (controller->back_swipe_progress >= BACK_SWIPE_COMMIT_THRESHOLD) {
                (void)now_playing_controller_step_back(controller, action_out);
            }
            return;
        }
        if (mode == NP_PTR_SEEK) {
            controller->is_seeking = false;
            controller->seek_hold_seconds = 0.0f;
            controller->seek_charge_progress = 0.0f;
            (void)playback_engine_seek_ms(controller->engine, controller->seek_preview_ms,
                                          effects_out);
            return;
        }
        if (mode == NP_PTR_SKIP_DRAG) {
            if (fabsf(controller->skip_drag_offset_x) >= NP_SKIP_COMMIT_PX) {
                int32_t dir = controller->skip_drag_offset_x < 0.0f ? 1 : -1;
                if (controller->engine != NULL) {
                    controller->skip_outgoing_album_uid = controller->engine->active_track.album_uid;
                    controller->skip_outgoing_motif_id = controller->engine->active_track.motif_id;
                }
                if (playback_engine_skip(controller->engine, dir, effects_out) &&
                    effects_out != NULL) {
                    now_playing_controller_apply_effects(controller, effects_out);
                }
                controller->skip_drag_offset_x = 0.0f;
            }
            controller->skip_peek_valid = false;
            return;
        }
        if (mode == NP_PTR_QUEUE_PULL) {
            if (total_delta_y <= -(int32_t)NP_QUEUE_PULL_COMMIT_PX) {
                controller->queue_sheet_open = true;
                if (controller->engine != NULL) {
                    controller->queue_target_index = (int32_t)controller->engine->current_slot;
                    controller->queue_scroll = (float)controller->queue_target_index;
                    refill_queue_window(controller, controller->engine->current_slot);
                }
            }
            return;
        }
        if (mode == NP_PTR_QUICK_ACTIONS) {
            int32_t pressed = controller->quick_action_pressed_index;
            controller->quick_action_pressed_index = -1;
            if (was_tap) {
                if (pressed < 0) {
                    controller->quick_actions_open = false;
                } else {
                    controller->quick_actions_idle_timer = NP_QUICK_ACTIONS_AUTO_CLOSE_SECONDS;
                    switch (pressed) {
                        case 0: { /* FAV */
                            bool fav = false;
                            if (playback_engine_toggle_active_favorite(controller->engine, &fav,
                                                                       effects_out) &&
                                effects_out != NULL) {
                                now_playing_controller_apply_effects(controller, effects_out);
                            }
                            break;
                        }
                        case 1: /* SHUFFLE */
                            if (playback_engine_toggle_shuffle(controller->engine, effects_out) &&
                                effects_out != NULL) {
                                now_playing_controller_apply_effects(controller, effects_out);
                            }
                            break;
                        case 2: /* REPEAT */
                            if (playback_engine_cycle_repeat(controller->engine, effects_out) &&
                                effects_out != NULL) {
                                now_playing_controller_apply_effects(controller, effects_out);
                            }
                            break;
                        case 3: /* MIX+ */
                            refresh_playlist_picker(controller);
                            controller->playlist_picker_open = true;
                            break;
                        case 4: /* LYRICS */
                            controller->lyrics_open = !controller->lyrics_open;
                            controller->quick_actions_open = false;
                            break;
                        case 5: /* INFO */
                            controller->details_sheet_open = true;
                            break;
                        default:
                            break;
                    }
                }
            }
            return;
        }
        if (mode == NP_PTR_PLAYLIST_PICKER) {
            int32_t row = controller->picker_pressed_row;
            controller->picker_pressed_row = -1;
            if (was_tap) {
                if (event->x < 66 || event->x > 294 || event->y < 78 || event->y > 280) {
                    controller->playlist_picker_open = false;
                } else if (row >= 0 && (uint32_t)row < controller->picker_count &&
                           controller->database != NULL && controller->engine != NULL &&
                           controller->engine->has_active_track) {
                    bool added = false;
                    int64_t pl_id = controller->picker_items[row].playlist_id;
                    if (library_db_toggle_track_in_playlist(
                            controller->database, pl_id, controller->engine->active_track.track_id,
                            &added)) {
                        char toast[64];
                        snprintf(toast, sizeof(toast), "%s %s",
                                 added ? "ADDED TO" : "REMOVED FROM",
                                 controller->picker_items[row].name);
                        now_playing_controller_show_toast(controller, toast);
                        playback_engine_on_playlist_mutated(controller->engine, pl_id, false,
                                                            effects_out);
                    }
                    controller->playlist_picker_open = false;
                }
            }
            return;
        }
        if (mode == NP_PTR_QUEUE_LIST) {
            int32_t row = controller->queue_pressed_row;
            controller->queue_pressed_row = -1;
            if (was_tap && row >= 0 && controller->engine != NULL &&
                (uint32_t)row < controller->engine->queue_total) {
                if (playback_engine_jump_to_slot(controller->engine, (uint32_t)row, effects_out) &&
                    effects_out != NULL) {
                    now_playing_controller_apply_effects(controller, effects_out);
                }
                controller->queue_sheet_open = false;
                return;
            }
            float projected = controller->queue_scroll +
                              clamp_f32(controller->queue_velocity * 0.04f, -0.45f, 0.45f);
            uint32_t total = controller->engine != NULL ? controller->engine->queue_total : 0u;
            int32_t max_slot = total > 0u ? (int32_t)(total - 1u) : 0;
            controller->queue_target_index =
                (int32_t)clamp_f32(roundf(projected), 0.0f, (float)max_slot);
            return;
        }

        /* Tap disambiguation on bare vinyl / lyrics (includes right-bezel tap fall-through!) */
        if (was_tap &&
            (mode == NP_PTR_CENTER_TAP_OR_HOLD || mode == NP_PTR_VOLUME_PENDING ||
             mode == NP_PTR_LEFT_EDGE_PENDING)) {
            if (controller->pending_single_tap) {
                controller->pending_single_tap = false;
                controller->pending_single_tap_timer = 0.0f;
                if (playback_engine_toggle_play_pause(controller->engine, effects_out)) {
                    now_playing_controller_trigger_play_pulse(controller,
                                                               controller->engine->is_playing);
                    if (effects_out != NULL) {
                        now_playing_controller_apply_effects(controller, effects_out);
                    }
                }
            } else {
                controller->pending_single_tap = true;
                controller->pending_single_tap_timer = 0.0f;
            }
        }
    }
}
