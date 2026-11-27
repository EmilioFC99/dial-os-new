#ifndef DIALOS_NOW_PLAYING_CONTROLLER_H
#define DIALOS_NOW_PLAYING_CONTROLLER_H

#include "constants.h"
#include "library_db.h"
#include "now_playing_media.h"
#include "playback_engine.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

#define NP_OPEN_DURATION_SECONDS 0.62f
#define NP_EXIT_DURATION_SECONDS 0.34f
#define NP_DOUBLE_TAP_WINDOW_SECONDS 0.25f
#define NP_SEEK_CHARGE_DELAY_SECONDS 0.10f
#define NP_SEEK_TRIGGER_SECONDS 0.42f
#define NP_SEEK_CENTER_RADIUS_PX 90.0f
#define NP_SEEK_CORE_RADIUS_PX 22.0f
#define NP_SEEK_END_GUARD_MS 500u
#define NP_VOLUME_BEZEL_MIN_RADIUS_PX 104.0f
#define NP_VOLUME_BEZEL_MAX_ANGLE_RADIANS 1.08f
#define NP_VOLUME_BEZEL_START_MOVE_PX 6.0f
#define NP_VOLUME_BEZEL_FULL_ARC_RADIANS 1.10f
#define NP_VOLUME_HUD_HOLD_SECONDS 1.45f
#define NP_SKIP_COMMIT_PX 56.0f
#define NP_SKIP_SLIDE_DURATION_SECONDS 0.42f
#define NP_QUEUE_PULL_START_Y 245.0f
#define NP_QUEUE_PULL_COMMIT_PX 38.0f
#define NP_QUICK_ACTIONS_AUTO_CLOSE_SECONDS 4.6f
#define NP_TOAST_HOLD_SECONDS 1.6f
#define NP_BANNER_IN_SECONDS 0.24f
#define NP_BANNER_HOLD_SECONDS 2.0f
#define NP_BANNER_OUT_SECONDS 0.42f
#define NP_LYRICS_TRANSITION_SECONDS 0.22f
#define NP_QUEUE_WINDOW_SIZE 64u
#define NP_QUEUE_PREFETCH_MARGIN 16u
#define NP_PICKER_VISIBLE_ROWS 4u
#define NP_PICKER_WINDOW_SIZE 32u
#define NP_DETAILS_LYRICS_BADGE_HIT_LEFT 112
#define NP_DETAILS_LYRICS_BADGE_HIT_RIGHT 248
#define NP_DETAILS_LYRICS_BADGE_HIT_TOP 294
#define NP_DETAILS_LYRICS_BADGE_HIT_BOTTOM 340
#define NP_DETAILS_LYRICS_BADGE_HOLD_LEFT 100
#define NP_DETAILS_LYRICS_BADGE_HOLD_RIGHT 260
#define NP_DETAILS_LYRICS_BADGE_HOLD_TOP 286
#define NP_DETAILS_LYRICS_BADGE_HOLD_BOTTOM 348

enum now_playing_return_target {
    NP_RETURN_HOME = 0,
    NP_RETURN_LIBRARY = 1
};

enum now_playing_open_origin {
    NP_OPEN_FROM_HOME = 0,
    NP_OPEN_FROM_LIBRARY = 1,
    NP_OPEN_FROM_ALBUM_CRATE = 2
};

enum now_playing_action_kind {
    NP_ACTION_NONE = 0,
    NP_ACTION_EXIT = 1,
    NP_ACTION_BROWSE_LIBRARY = 2
};

enum now_playing_pointer_mode {
    NP_PTR_NONE = 0,
    NP_PTR_CENTER_TAP_OR_HOLD = 1,
    NP_PTR_LEFT_EDGE_PENDING = 2,
    NP_PTR_BACK_SWIPE = 3,
    NP_PTR_VOLUME_PENDING = 4,
    NP_PTR_VOLUME_BEZEL = 5,
    NP_PTR_SEEK = 6,
    NP_PTR_SKIP_DRAG = 7,
    NP_PTR_QUEUE_PULL = 8,
    NP_PTR_QUICK_ACTIONS = 9,
    NP_PTR_PLAYLIST_PICKER = 10,
    NP_PTR_QUEUE_HEADER = 11,
    NP_PTR_QUEUE_LIST = 12,
    NP_PTR_IDLE_CTA = 13,
    NP_PTR_DETAILS_LYRICS_BADGE = 14
};

struct now_playing_controller {
    struct playback_engine *engine;
    struct library_db *database;
    struct now_playing_media *media;

    enum now_playing_return_target return_target;
    enum now_playing_open_origin open_origin;
    float open_transition_progress;
    bool exit_transition_active;
    float exit_transition_progress;
    float elapsed_seconds;
    float interpolated_elapsed_ms;
    float elapsed_anchor_ms;
    float elapsed_since_anchor_seconds;
    bool has_elapsed_anchor;
    bool last_audio_started;
    uint8_t volume_level;

    /* Vinyl spin & inertia */
    float vinyl_rotation_radians;
    float vinyl_angular_velocity;
    float vinyl_nudge_radians;

    /* Overlay visibility & springs */
    bool quick_actions_open;
    float quick_actions_progress;
    float quick_actions_idle_timer;
    int32_t quick_action_pressed_index;

    bool playlist_picker_open;
    float playlist_picker_progress;
    struct library_db_playlist_membership picker_items[NP_PICKER_WINDOW_SIZE];
    uint32_t picker_count;
    uint32_t picker_total;
    float picker_scroll;
    int32_t picker_pressed_row;

    bool details_sheet_open;
    float details_sheet_progress;
    bool details_lyrics_banner_active;
    float details_lyrics_banner_alpha;

    bool queue_sheet_open;
    float queue_sheet_progress;
    float queue_scroll;
    int32_t queue_target_index;
    float queue_velocity;
    int32_t queue_pressed_row;
    struct library_db_row queue_window[NP_QUEUE_WINDOW_SIZE];
    uint32_t queue_window_start;
    uint32_t queue_window_count;

    bool lyrics_open;
    float lyrics_progress;
    enum lyrics_mode lyrics_mode_pref;
    int32_t lyrics_last_cue_index;
    int32_t lyrics_prev_cue_index;
    char lyrics_prev_cue_text[LYRICS_LINE_CAPACITY];
    float lyrics_cue_transition;

    /* Seek Mode */
    bool is_seeking;
    bool seek_over_lyrics;
    float seek_hold_seconds;
    float seek_charge_progress;
    float seek_expand_progress;
    uint32_t seek_preview_ms;

    /* Volume HUD */
    float volume_hud_alpha;
    float volume_hud_hold_seconds;
    bool volume_hud_active;

    /* Cross-album drag-peek & slide */
    float skip_drag_offset_x;
    struct playback_skip_preview skip_peek_cache;
    bool skip_peek_valid;
    int32_t skip_peek_direction;
    bool skip_slide_active;
    int32_t skip_slide_direction;
    float skip_slide_progress;
    uint64_t skip_outgoing_album_uid;
    uint8_t skip_outgoing_motif_id;
    float skip_outgoing_angle;

    /* Transient feedback */
    float play_pulse_alpha;
    bool play_pulse_is_playing;
    bool banner_active;
    float banner_timer;
    char banner_title[TRACK_TITLE_CAPACITY];
    char banner_subtitle[TRACK_METADATA_TEXT_CAPACITY];
    bool toast_active;
    float toast_timer;
    float toast_alpha;
    char toast_text[64];

    /* Back-swipe & tap disambiguation */
    float back_swipe_progress;
    bool pending_single_tap;
    float pending_single_tap_timer;
    bool idle_cta_pressed;

    /* Pointer tracking */
    enum now_playing_pointer_mode pointer_mode;
    bool pointer_active;
    int32_t pointer_start_x;
    int32_t pointer_start_y;
    int32_t pointer_last_x;
    int32_t pointer_last_y;
    uint64_t pointer_last_timestamp_ms;
    float pointer_max_drift_px;
    float pointer_last_angle;
    float pointer_start_scroll;
    float pointer_volume_accum;
};

void now_playing_controller_initialize(struct now_playing_controller *controller,
                                       struct playback_engine *engine,
                                       struct library_db *database,
                                       struct now_playing_media *media);
void now_playing_controller_enter(struct now_playing_controller *controller,
                                  enum now_playing_return_target return_target,
                                  enum now_playing_open_origin open_origin,
                                  bool skip_open_transition, float initial_vinyl_radians,
                                  uint8_t volume_level);
void now_playing_controller_step(struct now_playing_controller *controller, float delta_seconds,
                                   struct playback_engine_effects *effects_out,
                                   bool *volume_changed_out,
                                   enum now_playing_action_kind *action_out);
void now_playing_controller_handle_touch(struct now_playing_controller *controller,
                                         const struct touch_event *event,
                                         struct playback_engine_effects *effects_out,
                                         bool *volume_changed_out,
                                         enum now_playing_action_kind *action_out);
bool now_playing_controller_step_back(struct now_playing_controller *controller,
                                      enum now_playing_action_kind *action_out);
void now_playing_controller_apply_effects(struct now_playing_controller *controller,
                                          const struct playback_engine_effects *effects);
void now_playing_controller_on_ipc_elapsed(struct now_playing_controller *controller,
                                           uint32_t elapsed_ms);
void now_playing_controller_show_volume_hud(struct now_playing_controller *controller,
                                            uint8_t volume_level);
void now_playing_controller_trigger_play_pulse(struct now_playing_controller *controller,
                                               bool is_playing);
void now_playing_controller_show_toast(struct now_playing_controller *controller, const char *text);
const struct library_db_row *now_playing_controller_get_queue_row(
    const struct now_playing_controller *controller, uint32_t slot);

#endif
