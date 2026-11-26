#include "home_screen.h"

#include "render_primitives.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WHEEL_WRAP_NORMALIZE_THRESHOLD 16.0f
#define WHEEL_SETTLE_EPSILON 0.001f
#define FLICK_VELOCITY_PROJECTION_SCALE 0.045f
#define FLICK_MAX_PROJECTION_SLOTS 0.45f
#define VELOCITY_FRAME_REFERENCE_MS 16.6667f
#define VELOCITY_MIN_DT_MS 8.0f
#define VELOCITY_MAX_DT_MS 80.0f

#define HOME_SETTINGS_ROW_LEFT_X 91
#define HOME_SETTINGS_ROW_RIGHT_X 269
#define HOME_SETTINGS_ROW0_TOP_Y 170
#define HOME_SETTINGS_ROW0_BOTTOM_Y 194

#define HOME_CTA_TOP_Y 230
#define HOME_CTA_BOTTOM_Y 306
#define HOME_CTA_HALF_WIDTH 94

uint8_t home_normalize_volume_percent(uint8_t raw_volume) {
    uint32_t clamped = raw_volume > VOLUME_HARD_MAX ? VOLUME_HARD_MAX : raw_volume;
    uint32_t percent = (clamped * 100u + (VOLUME_HARD_MAX / 2u)) / VOLUME_HARD_MAX;
    if (percent > 100u) {
        percent = 100u;
    }
    return (uint8_t)percent;
}

int32_t home_wrap_app_slot(int32_t virtual_slot) {
    int32_t wrapped = virtual_slot % (int32_t)HOME_APP_COUNT;
    if (wrapped < 0) {
        wrapped += (int32_t)HOME_APP_COUNT;
    }
    return wrapped;
}

float home_cyclic_focus_weight(int32_t app_index, float scroll_position) {
    float wrapped_scroll = fmodf(scroll_position, (float)HOME_APP_COUNT);
    if (wrapped_scroll < 0.0f) {
        wrapped_scroll += (float)HOME_APP_COUNT;
    }
    float distance = fabsf((float)app_index - wrapped_scroll);
    if (distance > (float)HOME_APP_COUNT * 0.5f) {
        distance = (float)HOME_APP_COUNT - distance;
    }
    return render_clamp_f32(1.0f - distance, 0.0f, 1.0f);
}

void home_compute_slot_layout(int32_t virtual_slot, int32_t focused_slot, float scroll_position,
                              float entry_progress, float launch_progress, float elapsed_seconds,
                              struct home_slot_layout *layout_out) {
    if (layout_out == NULL) {
        return;
    }

    float delta_slot = (float)virtual_slot - scroll_position;
    float abs_delta = fabsf(delta_slot);
    float focus_weight = render_clamp_f32(1.0f - abs_delta, 0.0f, 1.0f);
    bool is_focused = (virtual_slot == focused_slot);
    float entry_ease = render_ease_out_expo(entry_progress);
    float launch_part =
        render_ease_in_out_cubic(render_normalize_range(launch_progress, 0.0f, 0.52f));

    float entry_side_spread = 0.0f;
    if (!is_focused) {
        float direction = (delta_slot >= 0.0f) ? 1.0f : -1.0f;
        entry_side_spread = (1.0f - entry_ease) * direction * 110.0f;
    }

    float launch_side_spread = 0.0f;
    if (!is_focused) {
        float direction = 1.0f;
        if (delta_slot < 0.0f || (delta_slot == 0.0f && virtual_slot < focused_slot)) {
            direction = -1.0f;
        }
        launch_side_spread = direction * 165.0f * launch_part;
    }

    float center_x =
        (float)SCREEN_CENTER_X + delta_slot * 128.0f + entry_side_spread + launch_side_spread;

    float bob_amplitude = render_lerp_f32(1.6f, 3.2f, focus_weight);
    float bob_offset_y = sinf(elapsed_seconds * 1.9f + (float)virtual_slot * 1.3f) * bob_amplitude;
    float clamped_delta = abs_delta < 1.7f ? abs_delta : 1.7f;
    float center_y = 148.0f + clamped_delta * 14.0f + bob_offset_y * (1.0f - launch_part);
    if (is_focused && entry_progress < 1.0f) {
        center_y = render_lerp_f32((float)SCREEN_CENTER_Y, center_y, entry_ease);
    }

    float half_size;
    if (abs_delta <= 1.0f) {
        half_size = render_lerp_f32(45.0f, 25.0f, render_ease_in_out_cubic(abs_delta));
    } else {
        half_size = render_lerp_f32(25.0f, 16.0f, render_clamp_f32(abs_delta - 1.0f, 0.0f, 1.0f));
    }
    if (is_focused && entry_progress < 1.0f) {
        half_size = render_lerp_f32(30.0f, half_size, entry_ease);
    }

    float yaw_sway = sinf(elapsed_seconds * 1.25f + (float)virtual_slot * 1.7f) *
                     render_lerp_f32(0.18f, 0.28f, focus_weight);
    float entry_yaw = is_focused ? (1.0f - entry_ease) * (HALF_CIRCLE_RADIANS * 0.5f) : 0.0f;
    float angle_y = yaw_sway + delta_slot * (HALF_CIRCLE_RADIANS * 0.52f) + entry_yaw;
    float angle_x = 0.16f + cosf(elapsed_seconds * 0.95f + (float)virtual_slot) * 0.055f;
    float angle_z =
        sinf(elapsed_seconds * 0.75f + (float)virtual_slot) * 0.025f + delta_slot * 0.06f;

    float alpha = 1.0f - render_normalize_range(abs_delta, 1.05f, 1.62f);
    if (!is_focused) {
        alpha *= (1.0f - launch_part);
    }

    layout_out->virtual_slot = virtual_slot;
    layout_out->app_id = (enum home_app_id)home_wrap_app_slot(virtual_slot);
    layout_out->is_focused = is_focused;
    layout_out->delta_slot = delta_slot;
    layout_out->focus_weight = focus_weight;
    layout_out->center_x = center_x;
    layout_out->center_y = center_y;
    layout_out->half_size = half_size;
    layout_out->angle_x = angle_x;
    layout_out->angle_y = angle_y;
    layout_out->angle_z = angle_z;
    layout_out->alpha = alpha;
    layout_out->bob_offset_y = bob_offset_y;
}

static bool has_active_vinyl(const struct home_controller *controller) {
    return controller->telemetry.playback_mode == HOME_PLAYBACK_PLAYING ||
           controller->telemetry.playback_mode == HOME_PLAYBACK_PAUSED;
}

static void launch_focused_app(struct home_controller *controller) {
    int32_t nearest = (int32_t)lroundf(controller->scroll_position);
    enum home_app_id app_id = (enum home_app_id)home_wrap_app_slot(nearest);

    controller->target_slot = nearest;
    controller->focused_slot = nearest;
    controller->launched_app_id = app_id;
    controller->back_swipe_progress = 0.0f;

    if (app_id == HOME_APP_NOW_PLAYING && controller->telemetry.playback_mode == HOME_PLAYBACK_IDLE) {
        controller->launch_target = 0.0f;
        controller->launch_progress = 0.0f;
        controller->active_screen = ACTIVE_SCREEN_NOW_PLAYING_SHELL;
        return;
    }

    controller->launch_target = 1.0f;
}

static void return_to_home_from_shell(struct home_controller *controller, enum home_app_id app_id) {
    int32_t slot_index = (int32_t)app_id;
    controller->active_screen = ACTIVE_SCREEN_HOME;
    controller->scroll_position = (float)slot_index;
    controller->target_slot = slot_index;
    controller->focused_slot = slot_index;
    controller->launched_app_id = app_id;
    controller->entry_progress = 1.0f;
    controller->launch_progress = 1.0f;
    controller->launch_target = 0.0f;
    controller->back_swipe_progress = 0.0f;
}

static void normalize_wheel_revolutions(struct home_controller *controller) {
    if (controller->pointer_mode == HOME_POINTER_NONE &&
        fabsf(controller->scroll_position - (float)controller->target_slot) < WHEEL_SETTLE_EPSILON &&
        fabsf(controller->scroll_position) >= WHEEL_WRAP_NORMALIZE_THRESHOLD) {
        int32_t wrapped = home_wrap_app_slot(controller->target_slot);
        controller->scroll_position = (float)wrapped;
        controller->target_slot = wrapped;
        controller->focused_slot = wrapped;
    }
}

void home_controller_initialize(struct home_controller *controller) {
    if (controller == NULL) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    controller->active_screen = ACTIVE_SCREEN_HOME;
    controller->entry_progress = 1.0f;
    controller->telemetry.volume_percent = home_normalize_volume_percent(VOLUME_DEFAULT);
    controller->telemetry.volume_limit_percent = home_normalize_volume_percent(MAX_VOLUME_DEFAULT);
}

void home_controller_enter_from_boot(struct home_controller *controller) {
    if (controller == NULL) {
        return;
    }
    controller->active_screen = ACTIVE_SCREEN_HOME;
    controller->scroll_position = 0.0f;
    controller->target_slot = 0;
    controller->focused_slot = 0;
    controller->entry_progress = 0.0f;
    controller->launch_progress = 0.0f;
    controller->launch_target = 0.0f;
    controller->launched_app_id = HOME_APP_NOW_PLAYING;
    controller->back_swipe_progress = 0.0f;
    controller->pointer_mode = HOME_POINTER_NONE;
}

void home_controller_step(struct home_controller *controller, float delta_seconds) {
    if (controller == NULL || delta_seconds <= 0.0f) {
        return;
    }

    controller->elapsed_seconds += delta_seconds;

    if (controller->entry_progress < 1.0f) {
        controller->entry_progress = render_clamp_f32(
            controller->entry_progress + delta_seconds / HOME_ENTRY_DURATION_SECONDS, 0.0f, 1.0f);
    }

    if (controller->telemetry.playback_mode == HOME_PLAYBACK_PLAYING) {
        controller->vinyl_rotation_radians =
            fmodf(controller->vinyl_rotation_radians + delta_seconds * VINYL_SPIN_RADIANS_PER_SECOND,
                  FULL_CIRCLE_RADIANS);
        if (controller->telemetry.duration_ms > 0u) {
            controller->elapsed_fractional_ms += delta_seconds * (float)MILLISECONDS_PER_SECOND;
            uint32_t whole_ms = (uint32_t)controller->elapsed_fractional_ms;
            if (whole_ms > 0u) {
                controller->elapsed_fractional_ms -= (float)whole_ms;
                uint64_t next_elapsed =
                    (uint64_t)controller->telemetry.elapsed_ms + (uint64_t)whole_ms;
                controller->telemetry.elapsed_ms =
                    next_elapsed > controller->telemetry.duration_ms
                        ? controller->telemetry.duration_ms
                        : (uint32_t)next_elapsed;
            }
        }
    } else {
        controller->elapsed_fractional_ms = 0.0f;
    }

    if (controller->pointer_mode != HOME_POINTER_CAROUSEL) {
        float snap_blend = 1.0f - expf(-HOME_SNAP_SPRING_RATE * delta_seconds);
        controller->scroll_position =
            render_lerp_f32(controller->scroll_position, (float)controller->target_slot, snap_blend);
        if (fabsf(controller->scroll_position - (float)controller->target_slot) <
            WHEEL_SETTLE_EPSILON) {
            controller->scroll_position = (float)controller->target_slot;
        }
        controller->focused_slot = (int32_t)lroundf(controller->scroll_position);
        normalize_wheel_revolutions(controller);
    }

    if (controller->active_screen == ACTIVE_SCREEN_HOME &&
        fabsf(controller->launch_progress - controller->launch_target) > 0.0001f) {
        float step = delta_seconds / HOME_LAUNCH_DURATION_SECONDS;
        if (controller->launch_target > controller->launch_progress) {
            controller->launch_progress =
                render_clamp_f32(controller->launch_progress + step, 0.0f, 1.0f);
        } else {
            controller->launch_progress =
                render_clamp_f32(controller->launch_progress - step, 0.0f, 1.0f);
        }

        if (controller->launch_target >= 1.0f && controller->launch_progress >= 1.0f) {
            if (controller->launched_app_id == HOME_APP_NOW_PLAYING &&
                has_active_vinyl(controller)) {
                controller->active_screen = ACTIVE_SCREEN_NOW_PLAYING_SHELL;
            } else if (controller->launched_app_id == HOME_APP_LIBRARY) {
                controller->active_screen = ACTIVE_SCREEN_LIBRARY_SHELL;
            }
        }
    }

    if (controller->pointer_mode != HOME_POINTER_BACK_SWIPE &&
        controller->back_swipe_progress > 0.0f) {
        controller->back_swipe_progress = render_clamp_f32(
            controller->back_swipe_progress - delta_seconds * BACK_SWIPE_RETRACT_RATE, 0.0f, 1.0f);
    }
}

static bool is_back_swipe_active_context(const struct home_controller *controller) {
    if (controller->active_screen != ACTIVE_SCREEN_HOME) {
        return true;
    }
    return controller->launch_target > 0.5f;
}

static void handle_carousel_tap(struct home_controller *controller, int32_t tap_x, int32_t tap_y) {
    int32_t center_slot = (int32_t)lroundf(controller->scroll_position);

    for (int32_t offset = -1; offset <= 1; offset++) {
        int32_t candidate_slot = center_slot + offset;
        struct home_slot_layout layout;
        home_compute_slot_layout(candidate_slot, controller->focused_slot,
                                 controller->scroll_position, controller->entry_progress,
                                 controller->launch_progress, controller->elapsed_seconds, &layout);
        float delta_x = (float)tap_x - layout.center_x;
        float delta_y = (float)tap_y - layout.center_y;
        float hit_radius = layout.half_size * 1.25f + 8.0f;
        if (delta_x * delta_x + delta_y * delta_y <= hit_radius * hit_radius) {
            if (offset == 0) {
                launch_focused_app(controller);
            } else {
                controller->target_slot = candidate_slot;
                controller->focused_slot = candidate_slot;
            }
            return;
        }
    }

    controller->target_slot = center_slot;
    controller->focused_slot = center_slot;
}

static enum home_controller_event handle_overlay_tap(struct home_controller *controller,
                                                     int32_t tap_x, int32_t tap_y) {
    if (controller->active_screen == ACTIVE_SCREEN_NOW_PLAYING_SHELL &&
        controller->telemetry.playback_mode == HOME_PLAYBACK_IDLE) {
        if (tap_y >= HOME_CTA_TOP_Y && tap_y <= HOME_CTA_BOTTOM_Y &&
            abs(tap_x - SCREEN_CENTER_X) <= HOME_CTA_HALF_WIDTH) {
            controller->active_screen = ACTIVE_SCREEN_LIBRARY_SHELL;
            controller->launched_app_id = HOME_APP_LIBRARY;
        }
        return HOME_EVENT_NONE;
    }

    if (controller->active_screen == ACTIVE_SCREEN_HOME && controller->launch_target > 0.5f) {
        if (controller->launched_app_id == HOME_APP_NOW_PLAYING &&
            controller->telemetry.playback_mode == HOME_PLAYBACK_STORAGE) {
            if (tap_y >= HOME_CTA_TOP_Y && tap_y <= HOME_CTA_BOTTOM_Y &&
                abs(tap_x - SCREEN_CENTER_X) <= HOME_CTA_HALF_WIDTH) {
                controller->launch_target = 0.0f;
            }
            return HOME_EVENT_NONE;
        }
        if (controller->launched_app_id == HOME_APP_SETTINGS &&
            tap_x >= HOME_SETTINGS_ROW_LEFT_X && tap_x <= HOME_SETTINGS_ROW_RIGHT_X &&
            tap_y >= HOME_SETTINGS_ROW0_TOP_Y && tap_y <= HOME_SETTINGS_ROW0_BOTTOM_Y) {
            return HOME_EVENT_TOGGLE_APPEARANCE;
        }
    }

    return HOME_EVENT_NONE;
}

enum home_controller_event home_controller_handle_touch(struct home_controller *controller,
                                                        const struct touch_event *event) {
    if (controller == NULL || event == NULL || event->kind == TOUCH_EVENT_NONE) {
        return HOME_EVENT_NONE;
    }

    if (event->kind == TOUCH_EVENT_DOWN) {
        controller->pointer_start_x = event->x;
        controller->pointer_start_y = event->y;
        controller->pointer_last_x = event->x;
        controller->pointer_last_timestamp_ms = event->timestamp_ms;
        controller->pointer_start_scroll = controller->scroll_position;
        controller->pointer_velocity_x = 0.0f;
        controller->max_pointer_drift_px = 0.0f;
        controller->pointer_started_in_left_edge = ((float)event->x <= LEFT_EDGE_SWIPE_ZONE_PX);

        if (is_back_swipe_active_context(controller)) {
            bool require_edge = (controller->active_screen != ACTIVE_SCREEN_HOME);
            if (!require_edge || controller->pointer_started_in_left_edge) {
                controller->pointer_mode = HOME_POINTER_BACK_SWIPE;
                controller->back_swipe_progress = 0.0f;
            } else {
                controller->pointer_mode = HOME_POINTER_NONE;
            }
        } else {
            controller->pointer_mode = HOME_POINTER_CAROUSEL;
        }
        return HOME_EVENT_NONE;
    }

    float drift_x = (float)(event->x - controller->pointer_start_x);
    float drift_y = (float)(event->y - controller->pointer_start_y);
    float radial_drift = sqrtf(drift_x * drift_x + drift_y * drift_y);
    if (radial_drift > controller->max_pointer_drift_px) {
        controller->max_pointer_drift_px = radial_drift;
    }

    if (event->kind == TOUCH_EVENT_MOVE) {
        if (controller->pointer_mode == HOME_POINTER_CAROUSEL) {
            float delta_x = (float)(event->x - controller->pointer_last_x);
            float dt_ms = (float)(event->timestamp_ms > controller->pointer_last_timestamp_ms
                                      ? event->timestamp_ms - controller->pointer_last_timestamp_ms
                                      : 26ull);
            dt_ms = render_clamp_f32(dt_ms, VELOCITY_MIN_DT_MS, VELOCITY_MAX_DT_MS);
            float normalized_vx = delta_x * (VELOCITY_FRAME_REFERENCE_MS / dt_ms);
            controller->pointer_velocity_x =
                render_lerp_f32(controller->pointer_velocity_x, normalized_vx, 0.65f);
            controller->scroll_position =
                controller->pointer_start_scroll - drift_x / HOME_DRAG_PIXELS_PER_DETENT;
            controller->focused_slot = (int32_t)lroundf(controller->scroll_position);
        } else if (controller->pointer_mode == HOME_POINTER_BACK_SWIPE) {
            controller->back_swipe_progress =
                drift_x > 0.0f ? render_clamp_f32(drift_x / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f)
                               : 0.0f;
        }
        controller->pointer_last_x = event->x;
        controller->pointer_last_timestamp_ms = event->timestamp_ms;
        return HOME_EVENT_NONE;
    }

    /* TOUCH_EVENT_UP */
    enum home_pointer_mode active_mode = controller->pointer_mode;
    controller->pointer_mode = HOME_POINTER_NONE;

    if (controller->max_pointer_drift_px <= HOME_TAP_DRIFT_LIMIT_PX) {
        controller->back_swipe_progress = 0.0f;
        if (is_back_swipe_active_context(controller)) {
            return handle_overlay_tap(controller, event->x, event->y);
        }
        handle_carousel_tap(controller, event->x, event->y);
        return HOME_EVENT_NONE;
    }

    if (active_mode == HOME_POINTER_CAROUSEL) {
        float projected_offset =
            render_clamp_f32(controller->pointer_velocity_x * FLICK_VELOCITY_PROJECTION_SCALE,
                             -FLICK_MAX_PROJECTION_SLOTS, FLICK_MAX_PROJECTION_SLOTS);
        float projected_slot = controller->scroll_position - projected_offset;
        int32_t snapped_slot = (int32_t)lroundf(projected_slot);
        controller->target_slot = snapped_slot;
        controller->focused_slot = snapped_slot;
        return HOME_EVENT_NONE;
    }

    if (active_mode == HOME_POINTER_BACK_SWIPE) {
        if (controller->back_swipe_progress >= BACK_SWIPE_COMMIT_THRESHOLD) {
            if (controller->active_screen == ACTIVE_SCREEN_NOW_PLAYING_SHELL) {
                return_to_home_from_shell(controller, HOME_APP_NOW_PLAYING);
            } else if (controller->active_screen == ACTIVE_SCREEN_LIBRARY_SHELL) {
                return_to_home_from_shell(controller, HOME_APP_LIBRARY);
            } else {
                controller->launch_target = 0.0f;
                controller->back_swipe_progress = 0.0f;
            }
        }
    }

    return HOME_EVENT_NONE;
}

void home_controller_get_snapshot(const struct home_controller *controller,
                                  enum appearance_mode appearance_mode,
                                  struct home_render_snapshot *snapshot_out) {
    if (controller == NULL || snapshot_out == NULL) {
        return;
    }
    snapshot_out->active_screen = controller->active_screen;
    snapshot_out->elapsed_seconds = controller->elapsed_seconds;
    snapshot_out->scroll_position = controller->scroll_position;
    snapshot_out->entry_progress = controller->entry_progress;
    snapshot_out->launch_progress = controller->launch_progress;
    snapshot_out->launch_target = controller->launch_target;
    snapshot_out->launched_app_id = controller->launched_app_id;
    snapshot_out->back_swipe_progress = controller->back_swipe_progress;
    snapshot_out->vinyl_rotation_radians = controller->vinyl_rotation_radians;
    snapshot_out->telemetry = controller->telemetry;
    snapshot_out->appearance_mode = appearance_mode;
}
