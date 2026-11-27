#ifndef DIALOS_LIBRARY_CONTROLLER_H
#define DIALOS_LIBRARY_CONTROLLER_H

#include "constants.h"
#include "library_cover_cache.h"
#include "library_db.h"
#include "protocol.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_LIST_TOP_SLOT_Y 126.0f
#define LIBRARY_LIST_ROW_PITCH_PX 74.0f
#define LIBRARY_LIST_FULL_SLOTS 2u
#define LIBRARY_CRATE_CENTER_Y 152.0f
#define LIBRARY_CRATE_SPACING_PX 130.0f
#define LIBRARY_DRAG_PX_PER_ROW 74.0f
#define LIBRARY_DRAG_PX_PER_PAIR 116.0f
#define LIBRARY_DRAG_PX_PER_ALBUM 108.0f
#define LIBRARY_ARC_RIM_MIN_RADIUS_PX 112.0f
#define LIBRARY_ARC_ENGAGE_MIN_RADIUS_PX 94.0f
#define LIBRARY_ARC_RADIANS_PER_STEP 0.36f
#define LIBRARY_SEEK_THRESHOLD_RADIANS 3.14159265359f
#define LIBRARY_SEEK_STEP_RADIANS 0.37699111843f
#define LIBRARY_RIM_SLICE_COUNT 17u
#define LIBRARY_TRANSITION_DURATION_SECONDS 0.28f
#define LIBRARY_ALBUMS_TRANSITION_SECONDS 0.52f
#define LIBRARY_ARTIST_TAB_FADE_SECONDS 0.20f
#define LIBRARY_DELETE_HOLD_SECONDS 1.10f
#define LIBRARY_LONG_PRESS_SECONDS 0.48f
#define LIBRARY_SORT_WHEEL_LONG_PRESS_SECONDS 0.36f
#define LIBRARY_QUICK_ACTION_AUTO_CLOSE_SECONDS 0.24f
#define LIBRARY_SORT_TOAST_DURATION_SECONDS 1.35f
#define LIBRARY_SORT_TOAST_FADE_SECONDS 0.35f
#define LIBRARY_TAP_DRIFT_LIMIT_PX 10.0f
#define LIBRARY_LIST_BACK_SWIPE_MAX_X 116.0f
#define LIBRARY_RIM_BACK_SWIPE_MAX_X 68.0f
#define LIBRARY_OVERLAY_BACK_SWIPE_MAX_X 74.0f

#define LIBRARY_ARTIST_TOGGLE_CENTER_Y 63.0f
#define LIBRARY_ARTIST_TOGGLE_WIDTH_PX 148.0f
#define LIBRARY_ARTIST_TOGGLE_HEIGHT_PX 20.0f
#define LIBRARY_ARTIST_TOGGLE_CHAMFER_PX 5.0f
#define LIBRARY_ARTIST_TOGGLE_HIT_TOP_Y 48.0f
#define LIBRARY_ARTIST_TOGGLE_HIT_BOTTOM_Y 77.0f
#define LIBRARY_ARTIST_TOGGLE_HIT_HALF_W 78.0f
#define LIBRARY_ARTIST_HEADER_DIVIDER_Y 79.0f

#define LIBRARY_CRATE_SORT_BTN_X 152.0f
#define LIBRARY_CRATE_SORT_BTN_Y 325.0f
#define LIBRARY_CRATE_NOTE_BTN_X 208.0f
#define LIBRARY_CRATE_NOTE_BTN_Y 325.0f
#define LIBRARY_ARTISTS_SORT_BTN_X 180.0f
#define LIBRARY_ARTISTS_SORT_BTN_Y 325.0f
#define LIBRARY_BOTTOM_BTN_RADIUS_PX 17.0f
#define LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX 22.0f

#define LIBRARY_QUICK_ACTION_BTN_Y 196.0f
#define LIBRARY_QUICK_ACTION_BTN_RADIUS_PX 34.0f
#define LIBRARY_QUICK_ACTION_BTN_HIT_RADIUS_PX 40.0f

#define LIBRARY_SORT_WHEEL_RADIUS_PX 76.0f
#define LIBRARY_SORT_WHEEL_SLOT_RADIUS_PX 16.5f
#define LIBRARY_SORT_WHEEL_ENGAGE_RADIUS_PX 26.0f
#define LIBRARY_SORT_WHEEL_START_ANGLE (-0.88f * 3.14159265359f)
#define LIBRARY_SORT_WHEEL_END_ANGLE (-0.12f * 3.14159265359f)

#define LIBRARY_VIEW_STACK_CAPACITY 8u
#define LIBRARY_MAX_SEEK_ANCHORS 64u
#define LIBRARY_VIEWPORT_WINDOW_SIZE 96u
#define LIBRARY_VIEWPORT_PREFETCH_MARGIN 24u

enum library_view_kind {
    LIB_VIEW_HUB = 0,
    LIB_VIEW_ALBUMS = 1,
    LIB_VIEW_ARTISTS = 2,
    LIB_VIEW_GENRES = 3,
    LIB_VIEW_PLAYLISTS = 4,
    LIB_VIEW_TRACKS = 5,
    LIB_VIEW_EMPTY = 6
};

enum library_track_source_type {
    LIB_TRACKS_ALBUM = 0,
    LIB_TRACKS_ALL = 1,
    LIB_TRACKS_FAVORITES = 2,
    LIB_TRACKS_ARTIST = 3,
    LIB_TRACKS_GENRE = 4,
    LIB_TRACKS_PLAYLIST = 5
};

enum library_hub_section_id {
    LIB_HUB_FAVORITES = 0,
    LIB_HUB_PLAYLISTS = 1,
    LIB_HUB_ARTISTS = 2,
    LIB_HUB_ALBUMS = 3,
    LIB_HUB_GENRES = 4,
    LIB_HUB_ALL_SONGS = 5,
    LIB_HUB_SECTION_COUNT = 6
};

enum library_tab_mode {
    LIB_TAB_ALBUMS = 0,
    LIB_TAB_SONGS = 1
};

enum library_pointer_mode {
    LIB_PTR_NONE = 0,
    LIB_PTR_LIST = 1,
    LIB_PTR_CRATE = 2,
    LIB_PTR_RIM_PENDING = 3,
    LIB_PTR_ARC_SCROLL = 4,
    LIB_PTR_LETTER_SEARCH = 5,
    LIB_PTR_BACK_SWIPE = 6,
    LIB_PTR_PLAYLIST_HOLD = 7,
    LIB_PTR_SORT_WHEEL = 8
};

enum library_bloom_direction {
    LIB_BLOOM_NONE = 0,
    LIB_BLOOM_HUB_TO_ALBUMS = 1,
    LIB_BLOOM_ALBUMS_TO_HUB = 2
};

enum library_quick_action_kind {
    LIB_QA_NONE = 0,
    LIB_QA_ALBUM = 1,
    LIB_QA_ARTIST = 2,
    LIB_QA_TRACK = 3
};

enum library_action_kind {
    LIBRARY_ACTION_NONE = 0,
    LIBRARY_ACTION_PLAY_TRACKS = 1,
    LIBRARY_ACTION_TOGGLE_PLAY_PAUSE = 2,
    LIBRARY_ACTION_STATE_DIRTY = 3,
    LIBRARY_ACTION_EXIT_TO_HOME = 4
};

struct library_hub_tile_rect {
    float center_x;
    float center_y;
    float width;
    float height;
    float alpha;
    bool is_peeking;
    float size_scale;
};

struct library_curved_row_layout {
    float row_y;
    float row_half_width;
    float row_height;
    float alpha;
    float preview_weight;
    bool is_preview;
};

struct library_crate_bars_window {
    uint32_t start_index;
    uint32_t window_count;
    uint32_t total_count;
};

struct library_sort_wheel_slot {
    enum library_sort_mode mode;
    float angle_radians;
    float center_x;
    float center_y;
};

struct library_view_frame {
    enum library_view_kind kind;
    enum library_track_source_type source_type;
    bool opened_from_hub_albums;
    bool is_artist_detail;
    bool is_favorites_detail;
    bool show_track_numbers;
    bool show_album_subtitle;
    int64_t context_id;
    char title[128];
    char subtitle[128];

    enum library_hub_section_id empty_icon;
    char empty_title[64];
    char empty_message[96];

    float scroll_position;
    int32_t target_index;
    float albums_scroll_position;
    int32_t albums_target_index;
    float songs_scroll_position;
    int32_t songs_target_index;

    struct library_db_collection collection;
    struct library_db_row parent_album_row;
    uint32_t item_count;
};

struct library_controller_action {
    enum library_action_kind kind;
    struct library_db_collection play_collection;
    uint32_t play_start_offset;
    bool play_has_shuffle_override;
    bool play_shuffle;
    bool play_from_album_crate;
    char play_context_title[LIBRARY_DB_ROW_TEXT_BYTES];
    char play_first_track_path[LIBRARY_DB_PATH_BYTES];
    char play_first_track_title[LIBRARY_DB_ROW_TEXT_BYTES];
    int64_t play_first_track_id;
    int64_t play_first_album_id;
    uint64_t play_first_album_uid;
    bool favorite_mutated;
    bool state_dirty;
};

struct library_controller {
    struct library_db *database;
    struct library_cover_cache *cover_cache;
    struct library_db_counts counts;
    enum library_sort_mode album_sort_mode;
    enum library_sort_mode artist_sort_mode;

    struct library_view_frame view_stack[LIBRARY_VIEW_STACK_CAPACITY];
    uint32_t stack_depth;

    /* Sliding-window viewport cache (read-only on Core 0, refilled on Core 1) */
    struct library_db_row viewport_rows[LIBRARY_VIEWPORT_WINDOW_SIZE];
    uint32_t viewport_start;
    uint32_t viewport_count;
    uint64_t viewport_generation;

    pthread_t prefetch_thread;
    pthread_mutex_t prefetch_mutex;
    pthread_cond_t prefetch_cond;
    bool prefetch_running;
    bool prefetch_stop;
    bool prefetch_has_request;
    struct library_db_collection prefetch_req_collection;
    enum library_sort_mode prefetch_req_sort_mode;
    uint32_t prefetch_req_start;
    uint64_t prefetch_req_generation;
    bool prefetch_ready;
    struct library_db_row prefetch_ready_rows[LIBRARY_VIEWPORT_WINDOW_SIZE];
    uint32_t prefetch_ready_start;
    uint32_t prefetch_ready_count;
    uint64_t prefetch_ready_generation;

    float elapsed_seconds;
    float transition_progress;
    int32_t transition_direction;
    enum library_bloom_direction bloom_direction;
    float bloom_progress;

    enum library_tab_mode preferred_artist_tab;
    enum library_tab_mode preferred_favorites_tab;
    float artist_mode_blend;
    float artist_tab_fade_progress;
    float favorites_mode_blend;
    float favorites_tab_fade_progress;

    float pressed_row_intensity;
    int32_t pressed_row_index;
    float tap_flash_intensity;
    int32_t tap_flash_index;
    float back_swipe_progress;

    /* Active playback mirror (synced from a202/a102 or optimistic launch) */
    bool has_active_track;
    int64_t active_track_id;
    int64_t active_album_id;
    uint64_t active_album_uid;
    bool is_playing;
    float vinyl_rotation_radians;

    /* Pointer & gesture tracking */
    enum library_pointer_mode pointer_mode;
    int32_t pointer_start_x;
    int32_t pointer_start_y;
    int32_t pointer_last_x;
    int32_t pointer_last_y;
    uint64_t pointer_last_timestamp_ms;
    float pointer_start_scroll;
    float pointer_velocity;
    float max_pointer_drift_px;
    bool pointer_started_in_left_edge;
    bool pointer_active;

    /* Long-press timers */
    bool long_press_armed;
    float long_press_elapsed_seconds;
    int32_t long_press_target_index;
    bool sort_long_press_armed;
    float sort_long_press_elapsed_seconds;

    /* Rim-arc & 17-slab Quick-Seek */
    float arc_start_angle;
    float arc_current_angle;
    float arc_signed_sweep;
    float arc_peak_sweep;
    float arc_feedback_alpha;
    float seek_hud_alpha;
    float rim_slice_pulls[LIBRARY_RIM_SLICE_COUNT];
    int32_t active_rim_slice;
    struct library_seek_anchor seek_anchors[LIBRARY_MAX_SEEK_ANCHORS];
    uint32_t seek_anchor_count;
    int32_t seek_anchor_index;

    /* Radial sort wheel & sort toast */
    bool sort_wheel_open;
    float sort_wheel_progress;
    float sort_wheel_anchor_x;
    float sort_wheel_anchor_y;
    int32_t sort_wheel_hovered_slot;
    bool sort_toast_active;
    float sort_toast_remaining_seconds;
    char sort_toast_text[48];

    /* Quick Actions overlay (Album / Artist / Track) */
    bool quick_actions_open;
    float quick_actions_progress;
    enum library_quick_action_kind quick_action_kind;
    struct library_db_row quick_action_row;
    bool quick_action_pressed;
    float quick_action_auto_close_timer;

    /* Playlist Action Sheet & Hold-to-Delete */
    bool playlist_sheet_open;
    float playlist_sheet_progress;
    int64_t playlist_sheet_id;
    char playlist_sheet_name[LIBRARY_DB_ROW_TEXT_BYTES];
    int32_t playlist_sheet_pressed_button; /* -1 none, 0 play, 1 shuffle, 2 delete */
    float delete_hold_progress;
    float delete_completed_flash;
    int64_t pending_deleted_playlist_id;
};

/* Pure geometry helpers */
void library_compute_hub_tile_rect(uint32_t tile_index, float scroll_pair_position,
                                   struct library_hub_tile_rect *rect_out);
void library_compute_curved_row_layout(uint32_t row_index, float scroll_position,
                                       struct library_curved_row_layout *layout_out);
int32_t library_resolve_rim_slice_index(float angle_radians);
uint32_t library_shortest_slice_distance(uint32_t first_slice, uint32_t second_slice);
void library_compute_windowed_crate_bars(uint32_t total_count, float scroll_position,
                                         struct library_crate_bars_window *window_out);
uint32_t library_compute_sort_wheel_slots_for_view(float anchor_x, float anchor_y,
                                                   bool exclude_liked_first,
                                                   struct library_sort_wheel_slot *slots_out);
int32_t library_resolve_sort_wheel_hovered_slot_for_view(float anchor_x, float anchor_y,
                                                         float touch_x, float touch_y,
                                                         bool exclude_liked_first);
void library_compute_sort_wheel_slots(float anchor_x, float anchor_y,
                                      struct library_sort_wheel_slot *slots_out);
int32_t library_resolve_sort_wheel_hovered_mode(float anchor_x, float anchor_y, float touch_x,
                                                float touch_y);
enum library_sort_mode library_controller_effective_sort_mode(
    const struct library_controller *controller, const struct library_view_frame *view);

/* Controller lifecycle */
void library_controller_initialize(struct library_controller *controller,
                                   struct library_db *database,
                                   struct library_cover_cache *cover_cache);
void library_controller_destroy(struct library_controller *controller);
void library_controller_stop_prefetch(struct library_controller *controller);
void library_controller_flush_prefetch(struct library_controller *controller);
void library_controller_reset_to_hub(struct library_controller *controller, bool reset_scroll);
void library_controller_refresh_catalog_views(struct library_controller *controller);
void library_controller_set_active_playback(struct library_controller *controller,
                                            const char *track_path, bool is_playing);
int64_t library_controller_take_deleted_playlist_id(struct library_controller *controller);
void library_controller_focus_album_if_crate(struct library_controller *controller,
                                             int64_t album_id);

const struct library_db_row *library_controller_get_row(const struct library_controller *controller,
                                                        uint32_t item_index);

void library_controller_step(struct library_controller *controller, float delta_seconds);
void library_controller_handle_touch(struct library_controller *controller,
                                     const struct touch_event *event,
                                     struct library_controller_action *action_out);
bool library_controller_is_settling(const struct library_controller *controller);
const struct library_view_frame *library_controller_current_view(
    const struct library_controller *controller);
uint32_t library_controller_current_max_scroll(const struct library_controller *controller);

#endif
