#ifndef DIALOS_HOME_SCREEN_H
#define DIALOS_HOME_SCREEN_H

#include "constants.h"
#include "protocol.h"
#include "theme.h"

#include <stdbool.h>
#include <stdint.h>

enum home_app_id {
    HOME_APP_NOW_PLAYING = 0,
    HOME_APP_LIBRARY = 1,
    HOME_APP_MODES = 2,
    HOME_APP_SETTINGS = 3,
    HOME_APP_COUNT = 4
};

enum active_screen_id {
    ACTIVE_SCREEN_HOME = 0,
    ACTIVE_SCREEN_NOW_PLAYING_SHELL = 1,
    ACTIVE_SCREEN_LIBRARY_SHELL = 2
};

enum home_playback_mode {
    HOME_PLAYBACK_IDLE = 0,
    HOME_PLAYBACK_PLAYING = 1,
    HOME_PLAYBACK_PAUSED = 2,
    HOME_PLAYBACK_STORAGE = 3
};

enum home_controller_event {
    HOME_EVENT_NONE = 0,
    HOME_EVENT_TOGGLE_APPEARANCE = 1
};

enum home_pointer_mode {
    HOME_POINTER_NONE = 0,
    HOME_POINTER_CAROUSEL = 1,
    HOME_POINTER_BACK_SWIPE = 2
};

struct home_telemetry {
    enum home_playback_mode playback_mode;
    char track_title[TRACK_TITLE_CAPACITY];
    uint32_t elapsed_ms;
    uint32_t duration_ms;
    uint8_t volume_percent;
    uint8_t volume_limit_percent;
    bool battery_valid;
    uint8_t battery_percent;
    bool sd_mounted;
    bool sd_free_valid;
    uint64_t sd_free_megabytes;
    uint32_t track_count;
    uint32_t album_count;
};

struct home_slot_layout {
    int32_t virtual_slot;
    enum home_app_id app_id;
    bool is_focused;
    float delta_slot;
    float focus_weight;
    float center_x;
    float center_y;
    float half_size;
    float angle_x;
    float angle_y;
    float angle_z;
    float alpha;
    float bob_offset_y;
};

struct home_render_snapshot {
    enum active_screen_id active_screen;
    float elapsed_seconds;
    float scroll_position;
    float entry_progress;
    float launch_progress;
    float launch_target;
    enum home_app_id launched_app_id;
    float back_swipe_progress;
    float vinyl_rotation_radians;
    struct home_telemetry telemetry;
    enum appearance_mode appearance_mode;
};

struct home_controller {
    enum active_screen_id active_screen;
    float elapsed_seconds;
    float scroll_position;
    int32_t target_slot;
    int32_t focused_slot;
    float entry_progress;
    float launch_progress;
    float launch_target;
    enum home_app_id launched_app_id;
    float back_swipe_progress;
    float vinyl_rotation_radians;
    float elapsed_fractional_ms;
    enum home_pointer_mode pointer_mode;
    int32_t pointer_start_x;
    int32_t pointer_start_y;
    int32_t pointer_last_x;
    uint64_t pointer_last_timestamp_ms;
    float pointer_start_scroll;
    float pointer_velocity_x;
    float max_pointer_drift_px;
    bool pointer_started_in_left_edge;
    struct home_telemetry telemetry;
};

uint8_t home_normalize_volume_percent(uint8_t raw_volume);
int32_t home_wrap_app_slot(int32_t virtual_slot);
float home_cyclic_focus_weight(int32_t app_index, float scroll_position);
void home_compute_slot_layout(int32_t virtual_slot, int32_t focused_slot, float scroll_position,
                              float entry_progress, float launch_progress, float elapsed_seconds,
                              struct home_slot_layout *layout_out);

void home_controller_initialize(struct home_controller *controller);
void home_controller_enter_from_boot(struct home_controller *controller);
void home_controller_step(struct home_controller *controller, float delta_seconds);
enum home_controller_event home_controller_handle_touch(struct home_controller *controller,
                                                        const struct touch_event *event);
void home_controller_get_snapshot(const struct home_controller *controller,
                                  enum appearance_mode appearance_mode,
                                  struct home_render_snapshot *snapshot_out);

#endif
