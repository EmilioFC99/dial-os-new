#include "library_controller.h"

#include "render_primitives.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIBRARY_DRAG_OVERSCROLL_SLOTS 0.35f
#define LIBRARY_SNAP_SPRING_RATE 13.5f
#define LIBRARY_OVERLAY_SPRING_RATE 14.0f
#define LIBRARY_PILL_SPRING_RATE 15.0f
#define LIBRARY_BACK_SWIPE_RETRACT_RATE 4.5f
#define LIBRARY_SLAB_RISE_RATE 30.0f
#define LIBRARY_SLAB_FALL_RATE 11.0f
#define LIBRARY_HUD_RISE_RATE 18.0f
#define LIBRARY_HUD_FALL_RATE 11.0f

static float clamp_f32(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static float lerp_f32(float start_value, float end_value, float factor) {
    return start_value + (end_value - start_value) * factor;
}

static float normalize_angle_delta(float target_angle, float base_angle) {
    float delta = target_angle - base_angle;
    while (delta > HALF_CIRCLE_RADIANS) {
        delta -= FULL_CIRCLE_RADIANS;
    }
    while (delta < -HALF_CIRCLE_RADIANS) {
        delta += FULL_CIRCLE_RADIANS;
    }
    return delta;
}

void library_compute_hub_tile_rect(uint32_t tile_index, float scroll_pair_position,
                                   struct library_hub_tile_rect *rect_out) {
    if (rect_out == NULL) {
        return;
    }
    uint32_t pair_row = tile_index / 2u;
    uint32_t column = tile_index % 2u;
    float delta_row = (float)pair_row - scroll_pair_position;
    float center_y = 152.0f + delta_row * 126.0f;
    bool is_peeking = (delta_row > 0.2f);
    float ramp = clamp_f32(delta_row, 0.0f, 1.0f);
    float width = lerp_f32(114.0f, 94.0f, ramp);
    float height = lerp_f32(112.0f, 88.0f, ramp);
    float half_spread = lerp_f32(64.0f, 54.0f, ramp);
    float center_x = (float)SCREEN_CENTER_X + (column == 0u ? -half_spread : half_spread);

    float alpha = 1.0f;
    if (delta_row < 0.0f) {
        alpha = 1.0f - render_ease_in_out_cubic(clamp_f32(-delta_row / 0.65f, 0.0f, 1.0f));
    } else if (delta_row > 0.0f) {
        float peek_alpha =
            lerp_f32(1.0f, 0.38f, render_ease_in_out_cubic(clamp_f32(delta_row, 0.0f, 1.0f)));
        float beyond = render_ease_in_out_cubic(clamp_f32((delta_row - 1.0f) / 0.55f, 0.0f, 1.0f));
        alpha = peek_alpha * (1.0f - beyond);
    }

    rect_out->center_x = center_x;
    rect_out->center_y = center_y;
    rect_out->width = width;
    rect_out->height = height;
    rect_out->alpha = alpha;
    rect_out->is_peeking = is_peeking;
    rect_out->size_scale = width / (is_peeking ? 94.0f : 114.0f);
}

void library_compute_curved_row_layout(uint32_t row_index, float scroll_position,
                                       struct library_curved_row_layout *layout_out) {
    if (layout_out == NULL) {
        return;
    }
    float slot_offset = (float)row_index - scroll_position;
    float row_y = LIBRARY_LIST_TOP_SLOT_Y + slot_offset * LIBRARY_LIST_ROW_PITCH_PX;
    float offset_y = row_y - (float)SCREEN_CENTER_Y;
    float chord_sq = 154.0f * 154.0f - offset_y * offset_y;
    float chord_half_width = chord_sq > 0.0f ? sqrtf(chord_sq) : 0.0f;
    float row_half_width = clamp_f32(chord_half_width - 18.0f, 66.0f, 126.0f);
    float row_height = 62.0f;
    float preview_weight = 0.0f;
    float alpha = 1.0f;

    if (slot_offset < 0.0f) {
        float exit_ratio = clamp_f32(-slot_offset / 0.68f, 0.0f, 1.0f);
        alpha = 1.0f - render_ease_in_out_cubic(exit_ratio);
        row_height = lerp_f32(62.0f, 46.0f, exit_ratio);
    } else if (slot_offset > 1.0f) {
        float preview_ramp = clamp_f32(slot_offset - 1.0f, 0.0f, 1.0f);
        float beyond = clamp_f32((slot_offset - 2.0f) / 0.62f, 0.0f, 1.0f);
        preview_weight = preview_ramp;
        row_height = lerp_f32(62.0f, 46.0f, render_ease_in_out_cubic(preview_ramp));
        row_half_width = fmaxf(66.0f, row_half_width - 8.0f * preview_ramp);
        alpha = lerp_f32(1.0f, 0.42f, render_ease_in_out_cubic(preview_ramp)) *
                (1.0f - render_ease_in_out_cubic(beyond));
    }

    layout_out->row_y = row_y;
    layout_out->row_half_width = row_half_width;
    layout_out->row_height = row_height;
    layout_out->alpha = alpha;
    layout_out->preview_weight = preview_weight;
    layout_out->is_preview = (preview_weight > 0.45f);
}

int32_t library_resolve_rim_slice_index(float angle_radians) {
    float normalized = fmodf(angle_radians - TOP_ANGLE_RADIANS, FULL_CIRCLE_RADIANS);
    if (normalized < 0.0f) {
        normalized += FULL_CIRCLE_RADIANS;
    }
    int32_t slice = (int32_t)floorf((normalized / FULL_CIRCLE_RADIANS) *
                                    (float)LIBRARY_RIM_SLICE_COUNT + 1e-6f);
    return slice % (int32_t)LIBRARY_RIM_SLICE_COUNT;
}

uint32_t library_shortest_slice_distance(uint32_t first_slice, uint32_t second_slice) {
    uint32_t diff = first_slice >= second_slice ? (first_slice - second_slice)
                                                : (second_slice - first_slice);
    uint32_t wrap = LIBRARY_RIM_SLICE_COUNT - diff;
    return diff < wrap ? diff : wrap;
}

void library_compute_windowed_crate_bars(uint32_t total_count, float scroll_position,
                                         struct library_crate_bars_window *window_out) {
    if (window_out == NULL) {
        return;
    }
    if (total_count == 0u) {
        window_out->start_index = 0u;
        window_out->window_count = 0u;
        window_out->total_count = 0u;
        return;
    }
    uint32_t window_count = total_count < 7u ? total_count : 7u;
    int32_t centered = (int32_t)lroundf(scroll_position) - (int32_t)(window_count / 2u);
    int32_t max_start = (int32_t)(total_count - window_count);
    if (centered < 0) {
        centered = 0;
    }
    if (centered > max_start) {
        centered = max_start;
    }
    window_out->start_index = (uint32_t)centered;
    window_out->window_count = window_count;
    window_out->total_count = total_count;
}

uint32_t library_compute_sort_wheel_slots_for_view(float anchor_x, float anchor_y,
                                                   bool exclude_liked_first,
                                                   struct library_sort_wheel_slot *slots_out) {
    if (slots_out == NULL) {
        return 0u;
    }
    enum library_sort_mode modes[LIBRARY_SORT_MODE_COUNT];
    uint32_t slot_count = 0u;
    for (uint32_t mode_index = 0u; mode_index < LIBRARY_SORT_MODE_COUNT; mode_index++) {
        enum library_sort_mode candidate = (enum library_sort_mode)mode_index;
        if (exclude_liked_first && candidate == LIBRARY_SORT_LIKED_FIRST) {
            continue;
        }
        modes[slot_count++] = candidate;
    }
    if (slot_count == 0u) {
        return 0u;
    }
    for (uint32_t slot_index = 0u; slot_index < slot_count; slot_index++) {
        float factor = (slot_count > 1u) ? ((float)slot_index / (float)(slot_count - 1u)) : 0.5f;
        float angle = lerp_f32(LIBRARY_SORT_WHEEL_START_ANGLE, LIBRARY_SORT_WHEEL_END_ANGLE, factor);
        slots_out[slot_index].mode = modes[slot_index];
        slots_out[slot_index].angle_radians = angle;
        slots_out[slot_index].center_x = anchor_x + cosf(angle) * LIBRARY_SORT_WHEEL_RADIUS_PX;
        slots_out[slot_index].center_y = anchor_y + sinf(angle) * LIBRARY_SORT_WHEEL_RADIUS_PX;
    }
    return slot_count;
}

int32_t library_resolve_sort_wheel_hovered_slot_for_view(float anchor_x, float anchor_y,
                                                         float touch_x, float touch_y,
                                                         bool exclude_liked_first) {
    float delta_x = touch_x - anchor_x;
    float delta_y = touch_y - anchor_y;
    if (hypotf(delta_x, delta_y) < LIBRARY_SORT_WHEEL_ENGAGE_RADIUS_PX || delta_y > 24.0f) {
        return -1;
    }
    float touch_angle = atan2f(delta_y, delta_x);
    struct library_sort_wheel_slot slots[LIBRARY_SORT_MODE_COUNT];
    uint32_t slot_count =
        library_compute_sort_wheel_slots_for_view(anchor_x, anchor_y, exclude_liked_first, slots);

    int32_t best_slot = -1;
    float best_abs_delta = 1e9f;
    for (uint32_t slot_index = 0u; slot_index < slot_count; slot_index++) {
        float abs_delta = fabsf(normalize_angle_delta(touch_angle, slots[slot_index].angle_radians));
        if (abs_delta <= 0.34f && abs_delta < best_abs_delta) {
            best_abs_delta = abs_delta;
            best_slot = (int32_t)slot_index;
        }
    }
    return best_slot;
}

void library_compute_sort_wheel_slots(float anchor_x, float anchor_y,
                                      struct library_sort_wheel_slot *slots_out) {
    (void)library_compute_sort_wheel_slots_for_view(anchor_x, anchor_y, false, slots_out);
}

int32_t library_resolve_sort_wheel_hovered_mode(float anchor_x, float anchor_y, float touch_x,
                                                float touch_y) {
    return library_resolve_sort_wheel_hovered_slot_for_view(anchor_x, anchor_y, touch_x, touch_y,
                                                            false);
}

enum library_sort_mode library_controller_effective_sort_mode(
    const struct library_controller *controller, const struct library_view_frame *view) {
    if (controller == NULL || view == NULL) {
        return LIBRARY_SORT_ALPHABETICAL;
    }
    if (view->kind == LIB_VIEW_ARTISTS) {
        return controller->artist_sort_mode;
    }
    if (view->kind == LIB_VIEW_ALBUMS) {
        enum library_sort_mode sort_mode = controller->album_sort_mode;
        if (view->is_favorites_detail && sort_mode == LIBRARY_SORT_LIKED_FIRST) {
            return LIBRARY_SORT_ALPHABETICAL;
        }
        return sort_mode;
    }
    return LIBRARY_SORT_ALPHABETICAL;
}

static uint32_t view_max_scroll(const struct library_view_frame *view) {
    if (view == NULL) {
        return 0u;
    }
    if (view->kind == LIB_VIEW_HUB) {
        return 2u;
    }
    if (view->kind == LIB_VIEW_ALBUMS) {
        return view->item_count > 1u ? (view->item_count - 1u) : 0u;
    }
    if (view->kind == LIB_VIEW_ARTISTS || view->kind == LIB_VIEW_GENRES ||
        view->kind == LIB_VIEW_PLAYLISTS || view->kind == LIB_VIEW_TRACKS) {
        return view->item_count > LIBRARY_LIST_FULL_SLOTS
                   ? (view->item_count - LIBRARY_LIST_FULL_SLOTS)
                   : 0u;
    }
    return 0u;
}

static uint32_t compute_window_start_for_scroll(uint32_t item_count, float scroll_position) {
    if (item_count <= LIBRARY_VIEWPORT_WINDOW_SIZE) {
        return 0u;
    }
    int32_t center = (int32_t)lroundf(scroll_position);
    if (center < 0) {
        center = 0;
    }
    int32_t half_window = (int32_t)(LIBRARY_VIEWPORT_WINDOW_SIZE / 2u);
    int32_t start = center - half_window;
    int32_t max_start = (int32_t)(item_count - LIBRARY_VIEWPORT_WINDOW_SIZE);
    if (start < 0) {
        start = 0;
    }
    if (start > max_start) {
        start = max_start;
    }
    return (uint32_t)start;
}

static void *prefetch_worker_main(void *argument) {
    struct library_controller *controller = (struct library_controller *)argument;
    for (;;) {
        pthread_mutex_lock(&controller->prefetch_mutex);
        while (!controller->prefetch_stop && !controller->prefetch_has_request) {
            pthread_cond_wait(&controller->prefetch_cond, &controller->prefetch_mutex);
        }
        if (controller->prefetch_stop) {
            pthread_mutex_unlock(&controller->prefetch_mutex);
            break;
        }
        struct library_db_collection req_collection = controller->prefetch_req_collection;
        enum library_sort_mode req_sort_mode = controller->prefetch_req_sort_mode;
        uint32_t req_start = controller->prefetch_req_start;
        uint64_t req_generation = controller->prefetch_req_generation;
        controller->prefetch_has_request = false;
        pthread_mutex_unlock(&controller->prefetch_mutex);

        struct library_db_row local_rows[LIBRARY_VIEWPORT_WINDOW_SIZE];
        uint32_t fetched = 0u;
        if (controller->database != NULL) {
            fetched = library_db_fetch_window(controller->database, &req_collection, req_sort_mode,
                                              req_start, LIBRARY_VIEWPORT_WINDOW_SIZE, local_rows);
        }

        pthread_mutex_lock(&controller->prefetch_mutex);
        if (!controller->prefetch_stop && req_generation == controller->viewport_generation) {
            memcpy(controller->prefetch_ready_rows, local_rows,
                   (size_t)fetched * sizeof(struct library_db_row));
            controller->prefetch_ready_start = req_start;
            controller->prefetch_ready_count = fetched;
            controller->prefetch_ready_generation = req_generation;
            controller->prefetch_ready = true;
        }
        pthread_cond_broadcast(&controller->prefetch_cond);
        pthread_mutex_unlock(&controller->prefetch_mutex);
    }
    return NULL;
}

static void start_prefetch_thread_if_needed(struct library_controller *controller) {
    if (controller->prefetch_running || controller->database == NULL) {
        return;
    }
    controller->prefetch_stop = false;
    if (pthread_create(&controller->prefetch_thread, NULL, prefetch_worker_main, controller) == 0) {
        controller->prefetch_running = true;
    }
}

void library_controller_stop_prefetch(struct library_controller *controller) {
    if (controller == NULL || !controller->prefetch_running) {
        return;
    }
    pthread_mutex_lock(&controller->prefetch_mutex);
    controller->prefetch_stop = true;
    pthread_cond_broadcast(&controller->prefetch_cond);
    pthread_mutex_unlock(&controller->prefetch_mutex);
    (void)pthread_join(controller->prefetch_thread, NULL);
    controller->prefetch_running = false;
}

static void swap_ready_prefetch_window(struct library_controller *controller) {
    if (!controller->prefetch_running) {
        return;
    }
    pthread_mutex_lock(&controller->prefetch_mutex);
    if (controller->prefetch_ready &&
        controller->prefetch_ready_generation == controller->viewport_generation) {
        memcpy(controller->viewport_rows, controller->prefetch_ready_rows,
               (size_t)controller->prefetch_ready_count * sizeof(struct library_db_row));
        controller->viewport_start = controller->prefetch_ready_start;
        controller->viewport_count = controller->prefetch_ready_count;
        controller->prefetch_ready = false;
    }
    pthread_mutex_unlock(&controller->prefetch_mutex);
}

static void maybe_schedule_viewport_prefetch(struct library_controller *controller,
                                             const struct library_view_frame *view) {
    if (controller == NULL || controller->database == NULL || view == NULL ||
        view->kind == LIB_VIEW_HUB || view->kind == LIB_VIEW_EMPTY || view->item_count == 0u) {
        return;
    }
    int32_t center_idx = (int32_t)lroundf(view->scroll_position);
    if (center_idx < 0) {
        center_idx = 0;
    }
    if ((uint32_t)center_idx >= view->item_count) {
        center_idx = (int32_t)(view->item_count - 1u);
    }

    uint32_t window_end = controller->viewport_start + controller->viewport_count;
    bool near_start = (controller->viewport_start > 0u) &&
                      ((uint32_t)center_idx <
                       controller->viewport_start + LIBRARY_VIEWPORT_PREFETCH_MARGIN);
    bool near_end = (window_end < view->item_count) &&
                    ((uint32_t)center_idx + LIBRARY_VIEWPORT_PREFETCH_MARGIN >= window_end);
    bool out_of_window = ((uint32_t)center_idx < controller->viewport_start) ||
                         ((uint32_t)center_idx >= window_end);
    if (!near_start && !near_end && !out_of_window) {
        return;
    }

    uint32_t target_start = compute_window_start_for_scroll(view->item_count, view->scroll_position);
    if (target_start == controller->viewport_start && controller->viewport_count > 0u) {
        return;
    }

    start_prefetch_thread_if_needed(controller);
    if (!controller->prefetch_running) {
        return;
    }
    enum library_sort_mode sort_mode = library_controller_effective_sort_mode(controller, view);
    pthread_mutex_lock(&controller->prefetch_mutex);
    controller->prefetch_req_collection = view->collection;
    controller->prefetch_req_sort_mode = sort_mode;
    controller->prefetch_req_start = target_start;
    controller->prefetch_req_generation = controller->viewport_generation;
    controller->prefetch_has_request = true;
    pthread_cond_signal(&controller->prefetch_cond);
    pthread_mutex_unlock(&controller->prefetch_mutex);
}

void library_controller_flush_prefetch(struct library_controller *controller) {
    if (controller == NULL || !controller->prefetch_running) {
        return;
    }
    pthread_mutex_lock(&controller->prefetch_mutex);
    while (controller->prefetch_has_request) {
        pthread_cond_wait(&controller->prefetch_cond, &controller->prefetch_mutex);
    }
    pthread_mutex_unlock(&controller->prefetch_mutex);
    swap_ready_prefetch_window(controller);
}

static void load_initial_viewport_sync(struct library_controller *controller,
                                       const struct library_view_frame *view) {
    controller->viewport_generation++;
    controller->viewport_start = 0u;
    controller->viewport_count = 0u;
    pthread_mutex_lock(&controller->prefetch_mutex);
    controller->prefetch_has_request = false;
    controller->prefetch_ready = false;
    pthread_mutex_unlock(&controller->prefetch_mutex);

    if (controller->database == NULL || view == NULL || view->kind == LIB_VIEW_HUB ||
        view->kind == LIB_VIEW_EMPTY || view->item_count == 0u) {
        return;
    }
    uint32_t start = compute_window_start_for_scroll(view->item_count, view->scroll_position);
    enum library_sort_mode sort_mode = library_controller_effective_sort_mode(controller, view);
    controller->viewport_start = start;
    controller->viewport_count =
        library_db_fetch_window(controller->database, &view->collection, sort_mode, start,
                                LIBRARY_VIEWPORT_WINDOW_SIZE, controller->viewport_rows);
}

const struct library_db_row *library_controller_get_row(const struct library_controller *controller,
                                                        uint32_t item_index) {
    if (controller == NULL || item_index < controller->viewport_start) {
        return NULL;
    }
    uint32_t local_index = item_index - controller->viewport_start;
    if (local_index >= controller->viewport_count) {
        return NULL;
    }
    return &controller->viewport_rows[local_index];
}

static void populate_view_from_db(struct library_controller *controller,
                                  struct library_view_frame *view) {
    if (controller == NULL || controller->database == NULL || view == NULL) {
        if (view != NULL) {
            view->item_count = 0u;
        }
        return;
    }

    switch (view->kind) {
        case LIB_VIEW_ALBUMS:
            if (view->is_artist_detail) {
                view->collection.kind = LIBRARY_DB_COLLECTION_ALBUMS_ARTIST;
                view->collection.context_id = view->context_id;
            } else if (view->is_favorites_detail) {
                view->collection.kind = LIBRARY_DB_COLLECTION_ALBUMS_FAVORITES;
                view->collection.context_id = 0;
            } else {
                view->collection.kind = LIBRARY_DB_COLLECTION_ALBUMS;
                view->collection.context_id = 0;
            }
            view->item_count = library_db_count(controller->database, &view->collection);
            break;
        case LIB_VIEW_ARTISTS:
            view->collection.kind = LIBRARY_DB_COLLECTION_ARTISTS;
            view->collection.context_id = 0;
            view->item_count = library_db_count(controller->database, &view->collection);
            snprintf(view->subtitle, sizeof(view->subtitle), "%u ARTISTS",
                     (unsigned)view->item_count);
            break;
        case LIB_VIEW_GENRES:
            view->collection.kind = LIBRARY_DB_COLLECTION_GENRES;
            view->collection.context_id = 0;
            view->item_count = library_db_count(controller->database, &view->collection);
            snprintf(view->subtitle, sizeof(view->subtitle), "%u GENRES",
                     (unsigned)view->item_count);
            break;
        case LIB_VIEW_PLAYLISTS:
            view->collection.kind = LIBRARY_DB_COLLECTION_PLAYLISTS;
            view->collection.context_id = 0;
            view->item_count = library_db_count(controller->database, &view->collection);
            snprintf(view->subtitle, sizeof(view->subtitle),
                     "%u MIXES \xC2\xB7 HOLD ROW TO MANAGE", (unsigned)view->item_count);
            break;
        case LIB_VIEW_TRACKS:
            if (view->source_type == LIB_TRACKS_ALL) {
                view->collection.kind = LIBRARY_DB_COLLECTION_TRACKS_ALL;
                view->collection.context_id = 0;
            } else if (view->source_type == LIB_TRACKS_FAVORITES) {
                view->collection.kind = LIBRARY_DB_COLLECTION_TRACKS_FAVORITES;
                view->collection.context_id = 0;
            } else if (view->source_type == LIB_TRACKS_ALBUM) {
                view->collection.kind = LIBRARY_DB_COLLECTION_TRACKS_ALBUM;
                view->collection.context_id = view->context_id;
            } else if (view->source_type == LIB_TRACKS_ARTIST) {
                view->collection.kind = LIBRARY_DB_COLLECTION_TRACKS_ARTIST;
                view->collection.context_id = view->context_id;
            } else if (view->source_type == LIB_TRACKS_GENRE) {
                view->collection.kind = LIBRARY_DB_COLLECTION_TRACKS_GENRE;
                view->collection.context_id = view->context_id;
            } else {
                view->collection.kind = LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST;
                view->collection.context_id = view->context_id;
            }
            view->item_count = library_db_count(controller->database, &view->collection);
            if (view->source_type == LIB_TRACKS_ALL) {
                snprintf(view->subtitle, sizeof(view->subtitle), "%u TRACKS \xC2\xB7 A\xE2\x80\x93Z",
                         (unsigned)view->item_count);
            } else if (view->source_type == LIB_TRACKS_ALBUM) {
                snprintf(view->subtitle, sizeof(view->subtitle), "%u TRACKS \xC2\xB7 ALBUM",
                         (unsigned)view->item_count);
            } else if (view->source_type == LIB_TRACKS_GENRE) {
                snprintf(view->subtitle, sizeof(view->subtitle), "%u TRACKS",
                         (unsigned)view->item_count);
            } else if (view->source_type == LIB_TRACKS_PLAYLIST) {
                snprintf(view->subtitle, sizeof(view->subtitle), "%u TRACKS \xC2\xB7 MIX",
                         (unsigned)view->item_count);
            }
            break;
        case LIB_VIEW_HUB:
        case LIB_VIEW_EMPTY:
        default:
            return;
    }

    uint32_t max_scroll = view_max_scroll(view);
    if (view->scroll_position > (float)max_scroll) {
        view->scroll_position = (float)max_scroll;
        view->target_index = (int32_t)max_scroll;
    }
    if (view->kind == LIB_VIEW_ALBUMS && !view->is_artist_detail && !view->is_favorites_detail) {
        snprintf(view->subtitle, sizeof(view->subtitle), "VINYL CRATE \xC2\xB7 %02u / %02u",
                 view->item_count > 0u ? (unsigned)(view->target_index + 1) : 0u,
                 (unsigned)view->item_count);
    }

    const struct library_view_frame *active_view = library_controller_current_view(controller);
    if (active_view == view) {
        load_initial_viewport_sync(controller, view);
    }
}

static bool current_view_supports_quick_seek(const struct library_view_frame *view) {
    if (view == NULL || view->item_count < 3u) {
        return false;
    }
    if (view->kind == LIB_VIEW_HUB || view->kind == LIB_VIEW_EMPTY) {
        return false;
    }
    if (view->kind == LIB_VIEW_TRACKS &&
        (view->source_type == LIB_TRACKS_ALBUM || view->source_type == LIB_TRACKS_PLAYLIST)) {
        return false;
    }
    return true;
}

static void rebuild_seek_anchors_for_current_view(struct library_controller *controller) {
    controller->seek_anchor_count = 0u;
    controller->seek_anchor_index = 0;
    const struct library_view_frame *view = library_controller_current_view(controller);
    if (!current_view_supports_quick_seek(view) || controller->database == NULL) {
        return;
    }
    enum library_sort_mode sort_mode = library_controller_effective_sort_mode(controller, view);
    controller->seek_anchor_count = library_db_build_seek_anchors(
        controller->database, &view->collection, sort_mode, controller->seek_anchors,
        LIBRARY_MAX_SEEK_ANCHORS);
}

static struct library_view_frame *push_new_view(struct library_controller *controller) {
    if (controller->stack_depth >= LIBRARY_VIEW_STACK_CAPACITY) {
        return NULL;
    }
    struct library_view_frame *frame = &controller->view_stack[controller->stack_depth++];
    memset(frame, 0, sizeof(*frame));
    return frame;
}

static void push_empty_view(struct library_controller *controller, enum library_hub_section_id icon,
                            const char *header_title, const char *header_subtitle,
                            const char *empty_title, const char *empty_message) {
    struct library_view_frame *view = push_new_view(controller);
    if (view == NULL) {
        return;
    }
    view->kind = LIB_VIEW_EMPTY;
    view->empty_icon = icon;
    snprintf(view->title, sizeof(view->title), "%s", header_title);
    snprintf(view->subtitle, sizeof(view->subtitle), "%s", header_subtitle);
    snprintf(view->empty_title, sizeof(view->empty_title), "%s", empty_title);
    snprintf(view->empty_message, sizeof(view->empty_message), "%s", empty_message);
    controller->transition_direction = 1;
    controller->transition_progress = 0.0f;
}

static void sync_cached_db_metadata(struct library_controller *controller) {
    if (controller->database == NULL) {
        memset(&controller->counts, 0, sizeof(controller->counts));
        controller->album_sort_mode = LIBRARY_SORT_ALPHABETICAL;
        controller->artist_sort_mode = LIBRARY_SORT_ALPHABETICAL;
        return;
    }
    (void)library_db_read_counts(controller->database, &controller->counts);
    controller->album_sort_mode =
        library_db_get_sort_mode(controller->database, LIBRARY_DB_SORT_SCOPE_ALBUMS);
    controller->artist_sort_mode =
        library_db_get_sort_mode(controller->database, LIBRARY_DB_SORT_SCOPE_ARTISTS);
}

void library_controller_initialize(struct library_controller *controller,
                                   struct library_db *database,
                                   struct library_cover_cache *cover_cache) {
    if (controller == NULL) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    pthread_mutex_init(&controller->prefetch_mutex, NULL);
    pthread_cond_init(&controller->prefetch_cond, NULL);
    controller->database = database;
    controller->cover_cache = cover_cache;
    controller->preferred_artist_tab = LIB_TAB_ALBUMS;
    controller->preferred_favorites_tab = LIB_TAB_ALBUMS;
    controller->transition_progress = 1.0f;
    controller->bloom_progress = 1.0f;
    controller->artist_tab_fade_progress = 1.0f;
    controller->favorites_tab_fade_progress = 1.0f;
    controller->pressed_row_index = -1;
    controller->tap_flash_index = -1;
    controller->active_rim_slice = -1;
    controller->sort_wheel_hovered_slot = -1;
    controller->playlist_sheet_pressed_button = -1;

    sync_cached_db_metadata(controller);
    library_controller_reset_to_hub(controller, true);
}

void library_controller_destroy(struct library_controller *controller) {
    if (controller == NULL) {
        return;
    }
    library_controller_stop_prefetch(controller);
    pthread_cond_destroy(&controller->prefetch_cond);
    pthread_mutex_destroy(&controller->prefetch_mutex);
    memset(controller, 0, sizeof(*controller));
}

void library_controller_reset_to_hub(struct library_controller *controller, bool reset_scroll) {
    if (controller == NULL) {
        return;
    }
    float saved_hub_scroll = 0.0f;
    int32_t saved_hub_target = 0;
    if (!reset_scroll && controller->stack_depth > 0u) {
        saved_hub_scroll = controller->view_stack[0].scroll_position;
        saved_hub_target = controller->view_stack[0].target_index;
    }
    controller->stack_depth = 1u;
    struct library_view_frame *hub = &controller->view_stack[0];
    memset(hub, 0, sizeof(*hub));
    hub->kind = LIB_VIEW_HUB;
    snprintf(hub->title, sizeof(hub->title), "Library");
    hub->scroll_position = saved_hub_scroll;
    hub->target_index = saved_hub_target;
    controller->viewport_generation++;
    controller->viewport_start = 0u;
    controller->viewport_count = 0u;
    controller->transition_progress = 1.0f;
    controller->bloom_direction = LIB_BLOOM_NONE;
    controller->bloom_progress = 1.0f;
    controller->sort_wheel_open = false;
    controller->sort_wheel_progress = 0.0f;
    controller->quick_actions_open = false;
    controller->quick_actions_progress = 0.0f;
    controller->playlist_sheet_open = false;
    controller->playlist_sheet_progress = 0.0f;
    controller->pointer_mode = LIB_PTR_NONE;
    controller->back_swipe_progress = 0.0f;
}

void library_controller_refresh_catalog_views(struct library_controller *controller) {
    if (controller == NULL) {
        return;
    }
    sync_cached_db_metadata(controller);
    for (uint32_t index = 0u; index < controller->stack_depth; index++) {
        populate_view_from_db(controller, &controller->view_stack[index]);
    }
}

void library_controller_set_active_playback(struct library_controller *controller,
                                            const char *track_path, bool is_playing) {
    if (controller == NULL) {
        return;
    }
    controller->is_playing = is_playing;
    if (track_path == NULL || track_path[0] == '\0' || controller->database == NULL) {
        controller->has_active_track = false;
        controller->active_track_id = 0;
        controller->active_album_id = 0;
        controller->active_album_uid = 0ull;
        return;
    }
    int64_t track_id = 0;
    int64_t album_id = 0;
    uint64_t album_uid = 0ull;
    if (!library_db_find_track_by_path(controller->database, track_path, &track_id, &album_id,
                                       &album_uid)) {
        controller->has_active_track = false;
        controller->active_track_id = 0;
        controller->active_album_id = 0;
        controller->active_album_uid = 0ull;
        return;
    }
    controller->has_active_track = true;
    controller->active_track_id = track_id;
    controller->active_album_id = album_id;
    controller->active_album_uid = album_uid;
}

const struct library_view_frame *library_controller_current_view(
    const struct library_controller *controller) {
    if (controller == NULL || controller->stack_depth == 0u) {
        return NULL;
    }
    return &controller->view_stack[controller->stack_depth - 1u];
}

uint32_t library_controller_current_max_scroll(const struct library_controller *controller) {
    return view_max_scroll(library_controller_current_view(controller));
}

bool library_controller_is_settling(const struct library_controller *controller) {
    if (controller == NULL) {
        return false;
    }
    if (controller->pointer_active || controller->transition_progress < 0.99f ||
        controller->bloom_progress < 0.99f) {
        return true;
    }
    const struct library_view_frame *view = library_controller_current_view(controller);
    if (view != NULL && fabsf(view->scroll_position - (float)view->target_index) > 0.02f) {
        return true;
    }
    return false;
}

static void show_sort_toast(struct library_controller *controller, enum library_sort_mode mode) {
    controller->sort_toast_active = true;
    controller->sort_toast_remaining_seconds = LIBRARY_SORT_TOAST_DURATION_SECONDS;
    snprintf(controller->sort_toast_text, sizeof(controller->sort_toast_text), "SORT \xC2\xB7 %s",
             library_sort_mode_toast_label(mode));
}

static void apply_sort_mode_for_current_view(struct library_controller *controller,
                                             enum library_sort_mode mode,
                                             struct library_controller_action *action_out) {
    struct library_view_frame *view =
        controller->stack_depth > 0u ? &controller->view_stack[controller->stack_depth - 1u] : NULL;
    if (view == NULL || controller->database == NULL) {
        return;
    }
    if (view->kind == LIB_VIEW_ALBUMS) {
        if (view->is_favorites_detail && mode == LIBRARY_SORT_LIKED_FIRST) {
            mode = LIBRARY_SORT_ALPHABETICAL;
        }
        controller->album_sort_mode = mode;
        (void)library_db_set_sort_mode(controller->database, LIBRARY_DB_SORT_SCOPE_ALBUMS, mode);
    } else if (view->kind == LIB_VIEW_ARTISTS) {
        controller->artist_sort_mode = mode;
        (void)library_db_set_sort_mode(controller->database, LIBRARY_DB_SORT_SCOPE_ARTISTS, mode);
    } else {
        return;
    }
    view->scroll_position = 0.0f;
    view->target_index = 0;
    populate_view_from_db(controller, view);
    show_sort_toast(controller, mode);
    if (action_out != NULL) {
        action_out->state_dirty = true;
    }
}

static bool step_back_internal(struct library_controller *controller,
                               struct library_controller_action *action_out) {
    if (controller->playlist_sheet_open) {
        controller->playlist_sheet_open = false;
        controller->delete_hold_progress = 0.0f;
        return true;
    }
    if (controller->quick_actions_open) {
        controller->quick_actions_open = false;
        return true;
    }
    if (controller->sort_wheel_open) {
        controller->sort_wheel_open = false;
        return true;
    }
    if (controller->stack_depth > 1u) {
        struct library_view_frame *popped = &controller->view_stack[controller->stack_depth - 1u];
        bool was_hub_albums = (popped->kind == LIB_VIEW_ALBUMS && popped->opened_from_hub_albums);
        memset(popped, 0, sizeof(*popped));
        controller->stack_depth--;
        if (controller->stack_depth == 1u) {
            if (was_hub_albums) {
                controller->bloom_direction = LIB_BLOOM_ALBUMS_TO_HUB;
                controller->bloom_progress = 0.0f;
            } else {
                controller->view_stack[0].scroll_position = 0.0f;
                controller->view_stack[0].target_index = 0;
                controller->transition_direction = -1;
                controller->transition_progress = 0.0f;
            }
        } else {
            controller->transition_direction = -1;
            controller->transition_progress = 0.0f;
        }
        populate_view_from_db(controller, &controller->view_stack[controller->stack_depth - 1u]);
        return true;
    }
    if (action_out != NULL) {
        action_out->kind = LIBRARY_ACTION_EXIT_TO_HOME;
    }
    return true;
}

static void emit_play_collection(struct library_controller *controller,
                                 struct library_db_collection collection, uint32_t start_offset,
                                 bool shuffle, struct library_controller_action *action_out) {
    if (controller == NULL || controller->database == NULL || action_out == NULL) {
        return;
    }
    struct library_db_row first_row;
    if (library_db_fetch_window(controller->database, &collection, LIBRARY_SORT_ALPHABETICAL,
                                start_offset, 1u, &first_row) == 0u) {
        return;
    }
    action_out->kind = LIBRARY_ACTION_PLAY_TRACKS;
    action_out->play_collection = collection;
    action_out->play_start_offset = start_offset;
    action_out->play_shuffle = shuffle;
    action_out->play_first_track_id = first_row.id;
    action_out->play_first_album_id = first_row.album_id;
    action_out->play_first_album_uid = first_row.uid;
    snprintf(action_out->play_first_track_title, sizeof(action_out->play_first_track_title), "%s",
             first_row.title);
    (void)library_db_get_track_path(controller->database, first_row.id,
                                    action_out->play_first_track_path,
                                    sizeof(action_out->play_first_track_path));
}

static void handle_hub_tile_tap(struct library_controller *controller, uint32_t tile_index) {
    if (controller->database == NULL) {
        return;
    }
    sync_cached_db_metadata(controller);
    switch ((enum library_hub_section_id)tile_index) {
        case LIB_HUB_FAVORITES: {
            uint32_t total_favs =
                controller->counts.favorite_tracks + controller->counts.favorite_albums;
            if (total_favs == 0u) {
                push_empty_view(controller, LIB_HUB_FAVORITES, "Favorites", "0 FAVORITES",
                                "No Favorites Yet", "HOLD A SONG OR ALBUM TO LIKE");
                return;
            }
            struct library_view_frame *view = push_new_view(controller);
            if (view == NULL) {
                return;
            }
            view->is_favorites_detail = true;
            snprintf(view->title, sizeof(view->title), "Favorites");
            enum library_tab_mode initial_tab = controller->preferred_favorites_tab;
            if (initial_tab == LIB_TAB_ALBUMS && controller->counts.favorite_albums == 0u &&
                controller->counts.favorite_tracks > 0u) {
                initial_tab = LIB_TAB_SONGS;
            } else if (initial_tab == LIB_TAB_SONGS && controller->counts.favorite_tracks == 0u &&
                       controller->counts.favorite_albums > 0u) {
                initial_tab = LIB_TAB_ALBUMS;
            }
            controller->favorites_mode_blend = (initial_tab == LIB_TAB_SONGS) ? 1.0f : 0.0f;
            if (initial_tab == LIB_TAB_ALBUMS) {
                view->kind = LIB_VIEW_ALBUMS;
            } else {
                view->kind = LIB_VIEW_TRACKS;
                view->source_type = LIB_TRACKS_FAVORITES;
            }
            populate_view_from_db(controller, view);
            controller->transition_direction = 1;
            controller->transition_progress = 0.0f;
            break;
        }
        case LIB_HUB_PLAYLISTS:
            if (controller->counts.playlists == 0u) {
                push_empty_view(controller, LIB_HUB_PLAYLISTS, "Playlists", "0 MIXES",
                                "No Playlists Yet", "ADD SONGS TO BUILD A MIX");
                return;
            }
            {
                struct library_view_frame *view = push_new_view(controller);
                if (view == NULL) {
                    return;
                }
                view->kind = LIB_VIEW_PLAYLISTS;
                snprintf(view->title, sizeof(view->title), "Playlists");
                populate_view_from_db(controller, view);
                controller->transition_direction = 1;
                controller->transition_progress = 0.0f;
            }
            break;
        case LIB_HUB_ARTISTS:
            if (controller->counts.artists == 0u) {
                push_empty_view(controller, LIB_HUB_ARTISTS, "Artists", "0 ARTISTS",
                                "No Artists Found", "SYNC MUSIC VIA USB STORAGE");
                return;
            }
            {
                struct library_view_frame *view = push_new_view(controller);
                if (view == NULL) {
                    return;
                }
                view->kind = LIB_VIEW_ARTISTS;
                snprintf(view->title, sizeof(view->title), "Artists");
                populate_view_from_db(controller, view);
                controller->transition_direction = 1;
                controller->transition_progress = 0.0f;
            }
            break;
        case LIB_HUB_ALBUMS:
            if (controller->counts.albums == 0u) {
                push_empty_view(controller, LIB_HUB_ALBUMS, "Albums", "0 ALBUMS", "No Albums Found",
                                "SYNC MUSIC VIA USB STORAGE");
                return;
            }
            {
                struct library_view_frame *view = push_new_view(controller);
                if (view == NULL) {
                    return;
                }
                view->kind = LIB_VIEW_ALBUMS;
                view->opened_from_hub_albums = true;
                snprintf(view->title, sizeof(view->title), "Albums");
                populate_view_from_db(controller, view);
                controller->bloom_direction = LIB_BLOOM_HUB_TO_ALBUMS;
                controller->bloom_progress = 0.0f;
            }
            break;
        case LIB_HUB_GENRES:
            if (controller->counts.genres == 0u) {
                push_empty_view(controller, LIB_HUB_GENRES, "Genres", "0 GENRES", "No Genres Found",
                                "RESCAN LIBRARY IN SETTINGS");
                return;
            }
            {
                struct library_view_frame *view = push_new_view(controller);
                if (view == NULL) {
                    return;
                }
                view->kind = LIB_VIEW_GENRES;
                snprintf(view->title, sizeof(view->title), "Genres");
                populate_view_from_db(controller, view);
                controller->transition_direction = 1;
                controller->transition_progress = 0.0f;
            }
            break;
        case LIB_HUB_ALL_SONGS:
        default:
            if (controller->counts.tracks == 0u) {
                push_empty_view(controller, LIB_HUB_ALL_SONGS, "All songs", "0 TRACKS",
                                "Library Is Empty", "COPY TRACKS TO MICROSD CARD");
                return;
            }
            {
                struct library_view_frame *view = push_new_view(controller);
                if (view == NULL) {
                    return;
                }
                view->kind = LIB_VIEW_TRACKS;
                view->source_type = LIB_TRACKS_ALL;
                snprintf(view->title, sizeof(view->title), "All songs");
                populate_view_from_db(controller, view);
                controller->transition_direction = 1;
                controller->transition_progress = 0.0f;
            }
            break;
    }
}

static void handle_segmented_tab_tap(struct library_controller *controller, enum library_tab_mode tab) {
    struct library_view_frame *view =
        controller->stack_depth > 0u ? &controller->view_stack[controller->stack_depth - 1u] : NULL;
    if (view == NULL || (!view->is_artist_detail && !view->is_favorites_detail)) {
        return;
    }
    bool currently_albums = (view->kind == LIB_VIEW_ALBUMS);
    if ((currently_albums && tab == LIB_TAB_ALBUMS) || (!currently_albums && tab == LIB_TAB_SONGS)) {
        return;
    }

    if (currently_albums) {
        view->albums_scroll_position = view->scroll_position;
        view->albums_target_index = view->target_index;
        view->kind = LIB_VIEW_TRACKS;
        view->source_type = view->is_artist_detail ? LIB_TRACKS_ARTIST : LIB_TRACKS_FAVORITES;
        view->show_album_subtitle = view->is_artist_detail;
        populate_view_from_db(controller, view);
        uint32_t max_s = view_max_scroll(view);
        view->scroll_position = clamp_f32(view->songs_scroll_position, 0.0f, (float)max_s);
        view->target_index = (int32_t)clamp_f32((float)view->songs_target_index, 0.0f, (float)max_s);
    } else {
        view->songs_scroll_position = view->scroll_position;
        view->songs_target_index = view->target_index;
        view->kind = LIB_VIEW_ALBUMS;
        populate_view_from_db(controller, view);
        uint32_t max_s = view_max_scroll(view);
        view->scroll_position = clamp_f32(view->albums_scroll_position, 0.0f, (float)max_s);
        view->target_index = (int32_t)clamp_f32((float)view->albums_target_index, 0.0f, (float)max_s);
    }
    load_initial_viewport_sync(controller, view);

    if (view->is_artist_detail) {
        controller->preferred_artist_tab = tab;
        controller->artist_tab_fade_progress = 0.0f;
    } else {
        controller->preferred_favorites_tab = tab;
        controller->favorites_tab_fade_progress = 0.0f;
    }
}

static bool resolve_row_for_action(struct library_controller *controller,
                                   const struct library_view_frame *view, uint32_t row_index,
                                   struct library_db_row *row_out) {
    const struct library_db_row *cached = library_controller_get_row(controller, row_index);
    if (cached != NULL) {
        *row_out = *cached;
        return true;
    }
    if (controller->database == NULL || view == NULL) {
        return false;
    }
    enum library_sort_mode sort_mode = library_controller_effective_sort_mode(controller, view);
    return library_db_fetch_window(controller->database, &view->collection, sort_mode, row_index,
                                   1u, row_out) > 0u;
}

static void handle_list_row_tap(struct library_controller *controller,
                                struct library_view_frame *view, uint32_t row_index,
                                struct library_controller_action *action_out) {
    if (row_index >= view->item_count || controller->database == NULL) {
        return;
    }
    struct library_db_row row;
    if (!resolve_row_for_action(controller, view, row_index, &row)) {
        return;
    }

    if (view->kind == LIB_VIEW_TRACKS) {
        if (controller->has_active_track && controller->active_track_id == row.id) {
            action_out->kind = LIBRARY_ACTION_TOGGLE_PLAY_PAUSE;
        } else {
            emit_play_collection(controller, view->collection, row_index, false, action_out);
        }
        return;
    }

    if (view->kind == LIB_VIEW_ARTISTS) {
        struct library_view_frame *detail = push_new_view(controller);
        if (detail == NULL) {
            return;
        }
        detail->is_artist_detail = true;
        detail->context_id = row.id;
        snprintf(detail->title, sizeof(detail->title), "%s", row.title);
        if (controller->preferred_artist_tab == LIB_TAB_ALBUMS) {
            detail->kind = LIB_VIEW_ALBUMS;
        } else {
            detail->kind = LIB_VIEW_TRACKS;
            detail->source_type = LIB_TRACKS_ARTIST;
            detail->show_album_subtitle = true;
        }
        populate_view_from_db(controller, detail);
        controller->transition_direction = 1;
        controller->transition_progress = 0.0f;
        return;
    }

    if (view->kind == LIB_VIEW_GENRES) {
        struct library_view_frame *detail = push_new_view(controller);
        if (detail == NULL) {
            return;
        }
        detail->kind = LIB_VIEW_TRACKS;
        detail->source_type = LIB_TRACKS_GENRE;
        detail->context_id = row.id;
        snprintf(detail->title, sizeof(detail->title), "%s", row.title);
        populate_view_from_db(controller, detail);
        controller->transition_direction = 1;
        controller->transition_progress = 0.0f;
        return;
    }

    if (view->kind == LIB_VIEW_PLAYLISTS) {
        struct library_view_frame *detail = push_new_view(controller);
        if (detail == NULL) {
            return;
        }
        detail->kind = LIB_VIEW_TRACKS;
        detail->source_type = LIB_TRACKS_PLAYLIST;
        detail->context_id = row.id;
        snprintf(detail->title, sizeof(detail->title), "%s", row.title);
        populate_view_from_db(controller, detail);
        controller->transition_direction = 1;
        controller->transition_progress = 0.0f;
    }
}

void library_controller_step(struct library_controller *controller, float delta_seconds) {
    if (controller == NULL) {
        return;
    }
    swap_ready_prefetch_window(controller);

    float dt = clamp_f32(delta_seconds, 0.0f, 0.05f);
    controller->elapsed_seconds += dt;

    if (controller->has_active_track && controller->is_playing) {
        controller->vinyl_rotation_radians =
            fmodf(controller->vinyl_rotation_radians + VINYL_SPIN_RADIANS_PER_SECOND * dt,
                  FULL_CIRCLE_RADIANS);
    }

    if (controller->transition_progress < 1.0f) {
        controller->transition_progress =
            clamp_f32(controller->transition_progress + dt / LIBRARY_TRANSITION_DURATION_SECONDS,
                      0.0f, 1.0f);
    }
    if (controller->bloom_direction != LIB_BLOOM_NONE && controller->bloom_progress < 1.0f) {
        controller->bloom_progress =
            clamp_f32(controller->bloom_progress + dt / LIBRARY_ALBUMS_TRANSITION_SECONDS, 0.0f,
                      1.0f);
        if (controller->bloom_progress >= 1.0f &&
            controller->bloom_direction == LIB_BLOOM_ALBUMS_TO_HUB) {
            controller->bloom_direction = LIB_BLOOM_NONE;
        }
    }

    float pill_blend = 1.0f - expf(-LIBRARY_PILL_SPRING_RATE * dt);
    const struct library_view_frame *view_const = library_controller_current_view(controller);
    if (view_const != NULL && view_const->is_artist_detail) {
        float target = (view_const->kind == LIB_VIEW_TRACKS) ? 1.0f : 0.0f;
        controller->artist_mode_blend = lerp_f32(controller->artist_mode_blend, target, pill_blend);
        if (fabsf(controller->artist_mode_blend - target) < 0.004f) {
            controller->artist_mode_blend = target;
        }
    }
    if (view_const != NULL && view_const->is_favorites_detail) {
        float target = (view_const->kind == LIB_VIEW_TRACKS) ? 1.0f : 0.0f;
        controller->favorites_mode_blend =
            lerp_f32(controller->favorites_mode_blend, target, pill_blend);
        if (fabsf(controller->favorites_mode_blend - target) < 0.004f) {
            controller->favorites_mode_blend = target;
        }
    }
    if (controller->artist_tab_fade_progress < 1.0f) {
        controller->artist_tab_fade_progress =
            clamp_f32(controller->artist_tab_fade_progress + dt / LIBRARY_ARTIST_TAB_FADE_SECONDS,
                      0.0f, 1.0f);
    }
    if (controller->favorites_tab_fade_progress < 1.0f) {
        controller->favorites_tab_fade_progress =
            clamp_f32(controller->favorites_tab_fade_progress + dt / LIBRARY_ARTIST_TAB_FADE_SECONDS,
                      0.0f, 1.0f);
    }

    /* Overlay springs */
    float overlay_blend = 1.0f - expf(-LIBRARY_OVERLAY_SPRING_RATE * dt);
    float sw_target = controller->sort_wheel_open ? 1.0f : 0.0f;
    controller->sort_wheel_progress =
        lerp_f32(controller->sort_wheel_progress, sw_target, overlay_blend);
    if (fabsf(controller->sort_wheel_progress - sw_target) < 0.004f) {
        controller->sort_wheel_progress = sw_target;
    }
    float qa_target = controller->quick_actions_open ? 1.0f : 0.0f;
    controller->quick_actions_progress =
        lerp_f32(controller->quick_actions_progress, qa_target, overlay_blend);
    if (fabsf(controller->quick_actions_progress - qa_target) < 0.004f) {
        controller->quick_actions_progress = qa_target;
    }
    float ps_target = controller->playlist_sheet_open ? 1.0f : 0.0f;
    controller->playlist_sheet_progress =
        lerp_f32(controller->playlist_sheet_progress, ps_target, overlay_blend);
    if (fabsf(controller->playlist_sheet_progress - ps_target) < 0.004f) {
        controller->playlist_sheet_progress = ps_target;
    }

    if (controller->quick_actions_open && controller->quick_action_auto_close_timer > 0.0f) {
        controller->quick_action_auto_close_timer -= dt;
        if (controller->quick_action_auto_close_timer <= 0.0f) {
            controller->quick_action_auto_close_timer = 0.0f;
            controller->quick_actions_open = false;
        }
    }

    if (controller->sort_toast_active) {
        controller->sort_toast_remaining_seconds -= dt;
        if (controller->sort_toast_remaining_seconds <= 0.0f) {
            controller->sort_toast_remaining_seconds = 0.0f;
            controller->sort_toast_active = false;
        }
    }

    if (controller->delete_completed_flash > 0.0f) {
        controller->delete_completed_flash =
            fmaxf(0.0f, controller->delete_completed_flash - 2.8f * dt);
    }

    /* Long-press timers */
    if (controller->sort_long_press_armed && controller->pointer_active) {
        controller->sort_long_press_elapsed_seconds += dt;
        if (controller->sort_long_press_elapsed_seconds >= LIBRARY_SORT_WHEEL_LONG_PRESS_SECONDS) {
            controller->sort_long_press_armed = false;
            controller->sort_wheel_open = true;
            controller->pointer_mode = LIB_PTR_SORT_WHEEL;
            controller->sort_wheel_hovered_slot = -1;
            const struct library_view_frame *v = library_controller_current_view(controller);
            controller->sort_wheel_anchor_x =
                (v != NULL && v->kind == LIB_VIEW_ARTISTS) ? LIBRARY_ARTISTS_SORT_BTN_X
                                                           : LIBRARY_CRATE_SORT_BTN_X;
            controller->sort_wheel_anchor_y = LIBRARY_CRATE_SORT_BTN_Y;
        }
    }

    if (controller->long_press_armed && controller->pointer_active) {
        controller->long_press_elapsed_seconds += dt;
        if (controller->long_press_elapsed_seconds >= LIBRARY_LONG_PRESS_SECONDS) {
            controller->long_press_armed = false;
            controller->pointer_active = false;
            controller->pointer_mode = LIB_PTR_NONE;
            const struct library_view_frame *current_view =
                library_controller_current_view(controller);
            if (current_view != NULL && controller->long_press_target_index >= 0 &&
                (uint32_t)controller->long_press_target_index < current_view->item_count) {
                struct library_db_row target_row;
                if (resolve_row_for_action(controller, current_view,
                                           (uint32_t)controller->long_press_target_index,
                                           &target_row)) {
                    if (current_view->kind == LIB_VIEW_ALBUMS) {
                        controller->quick_actions_open = true;
                        controller->quick_action_kind = LIB_QA_ALBUM;
                        controller->quick_action_row = target_row;
                        controller->quick_action_auto_close_timer = 0.0f;
                    } else if (current_view->kind == LIB_VIEW_ARTISTS) {
                        controller->quick_actions_open = true;
                        controller->quick_action_kind = LIB_QA_ARTIST;
                        controller->quick_action_row = target_row;
                        controller->quick_action_auto_close_timer = 0.0f;
                    } else if (current_view->kind == LIB_VIEW_TRACKS) {
                        controller->quick_actions_open = true;
                        controller->quick_action_kind = LIB_QA_TRACK;
                        controller->quick_action_row = target_row;
                        controller->quick_action_auto_close_timer = 0.0f;
                    } else if (current_view->kind == LIB_VIEW_PLAYLISTS) {
                        controller->playlist_sheet_open = true;
                        controller->playlist_sheet_id = target_row.id;
                        snprintf(controller->playlist_sheet_name,
                                 sizeof(controller->playlist_sheet_name), "%s", target_row.title);
                        controller->playlist_sheet_pressed_button = -1;
                        controller->delete_hold_progress = 0.0f;
                    }
                }
            }
        }
    }

    if (controller->pointer_mode == LIB_PTR_PLAYLIST_HOLD && controller->playlist_sheet_open) {
        controller->delete_hold_progress =
            clamp_f32(controller->delete_hold_progress + dt / LIBRARY_DELETE_HOLD_SECONDS, 0.0f,
                      1.0f);
        if (controller->delete_hold_progress >= 1.0f) {
            controller->playlist_sheet_open = false;
            controller->delete_hold_progress = 0.0f;
            controller->pointer_mode = LIB_PTR_NONE;
            controller->pointer_active = false;
            controller->delete_completed_flash = 1.0f;
            if (controller->database != NULL) {
                (void)library_db_delete_playlist(controller->database,
                                                 controller->playlist_sheet_id);
                sync_cached_db_metadata(controller);
            }
            struct library_view_frame *v =
                controller->stack_depth > 0u
                    ? &controller->view_stack[controller->stack_depth - 1u]
                    : NULL;
            if (v != NULL && v->kind == LIB_VIEW_PLAYLISTS) {
                populate_view_from_db(controller, v);
                if (controller->counts.playlists == 0u) {
                    v->kind = LIB_VIEW_EMPTY;
                    v->empty_icon = LIB_HUB_PLAYLISTS;
                    snprintf(v->subtitle, sizeof(v->subtitle), "0 MIXES");
                    snprintf(v->empty_title, sizeof(v->empty_title), "No Playlists Left");
                    snprintf(v->empty_message, sizeof(v->empty_message),
                             "ADD SONGS TO BUILD A MIX");
                }
            }
        }
    } else if (controller->delete_hold_progress > 0.0f) {
        controller->delete_hold_progress =
            fmaxf(0.0f, controller->delete_hold_progress - 3.8f * dt);
    }

    /* Row press & tap flash */
    if (controller->pressed_row_index >= 0 && controller->pointer_active &&
        (controller->pointer_mode == LIB_PTR_LIST || controller->long_press_armed)) {
        controller->pressed_row_intensity =
            fminf(1.0f, controller->pressed_row_intensity + 32.0f * dt);
    } else if (controller->pressed_row_intensity > 0.0f) {
        controller->pressed_row_intensity =
            fmaxf(0.0f, controller->pressed_row_intensity - 18.0f * dt);
        if (controller->pressed_row_intensity < 0.01f) {
            controller->pressed_row_intensity = 0.0f;
            controller->pressed_row_index = -1;
        }
    }
    if (controller->tap_flash_intensity > 0.0f) {
        controller->tap_flash_intensity =
            fmaxf(0.0f, controller->tap_flash_intensity - 7.5f * dt);
        if (controller->tap_flash_intensity <= 0.0f) {
            controller->tap_flash_index = -1;
        }
    }

    /* Quick-seek HUD & 17-slab pull springs */
    if (controller->pointer_mode == LIB_PTR_ARC_SCROLL ||
        controller->pointer_mode == LIB_PTR_LETTER_SEARCH) {
        controller->arc_feedback_alpha = fminf(1.0f, controller->arc_feedback_alpha + 18.0f * dt);
    } else if (controller->arc_feedback_alpha > 0.0f) {
        controller->arc_feedback_alpha = fmaxf(0.0f, controller->arc_feedback_alpha - 3.8f * dt);
    }

    if (controller->pointer_mode == LIB_PTR_LETTER_SEARCH) {
        controller->seek_hud_alpha = fminf(1.0f, controller->seek_hud_alpha + 18.0f * dt);
    } else if (controller->seek_hud_alpha > 0.0f) {
        controller->seek_hud_alpha = fmaxf(0.0f, controller->seek_hud_alpha - 11.0f * dt);
        if (controller->seek_hud_alpha < 0.01f) {
            controller->seek_hud_alpha = 0.0f;
        }
    }

    for (uint32_t slice = 0u; slice < LIBRARY_RIM_SLICE_COUNT; slice++) {
        float target_pull = 0.0f;
        if (controller->pointer_mode == LIB_PTR_LETTER_SEARCH && controller->active_rim_slice >= 0) {
            uint32_t dist =
                library_shortest_slice_distance(slice, (uint32_t)controller->active_rim_slice);
            if (dist == 0u) {
                target_pull = 1.0f;
            } else if (dist == 1u) {
                target_pull = 0.24f;
            }
        }
        float rate = target_pull > controller->rim_slice_pulls[slice] ? LIBRARY_SLAB_RISE_RATE
                                                                      : LIBRARY_SLAB_FALL_RATE;
        float blend = 1.0f - expf(-rate * dt);
        controller->rim_slice_pulls[slice] =
            lerp_f32(controller->rim_slice_pulls[slice], target_pull, blend);
        if (target_pull >= 1.0f && controller->rim_slice_pulls[slice] >= 0.94f) {
            controller->rim_slice_pulls[slice] = 1.0f;
        } else if (target_pull <= 0.0f && controller->rim_slice_pulls[slice] < 0.005f) {
            controller->rim_slice_pulls[slice] = 0.0f;
        }
    }

    /* Back-swipe retraction */
    if (controller->pointer_mode != LIB_PTR_BACK_SWIPE && controller->back_swipe_progress > 0.0f) {
        controller->back_swipe_progress =
            fmaxf(0.0f, controller->back_swipe_progress - LIBRARY_BACK_SWIPE_RETRACT_RATE * dt);
    }

    /* Scroll snap spring */
    if (controller->stack_depth > 0u && controller->pointer_mode != LIB_PTR_LIST &&
        controller->pointer_mode != LIB_PTR_CRATE && controller->pointer_mode != LIB_PTR_ARC_SCROLL) {
        struct library_view_frame *v = &controller->view_stack[controller->stack_depth - 1u];
        float target_f = (float)v->target_index;
        float snap_blend = 1.0f - expf(-LIBRARY_SNAP_SPRING_RATE * dt);
        v->scroll_position = lerp_f32(v->scroll_position, target_f, snap_blend);
        if (fabsf(v->scroll_position - target_f) < 0.001f) {
            v->scroll_position = target_f;
        }
        if (v->kind == LIB_VIEW_ALBUMS && !v->is_artist_detail && !v->is_favorites_detail) {
            int32_t focused = (int32_t)lroundf(v->scroll_position);
            if (focused < 0) {
                focused = 0;
            }
            if ((uint32_t)focused >= v->item_count && v->item_count > 0u) {
                focused = (int32_t)(v->item_count - 1u);
            }
            snprintf(v->subtitle, sizeof(v->subtitle), "VINYL CRATE \xC2\xB7 %02u / %02u",
                     v->item_count > 0u ? (unsigned)(focused + 1) : 0u, (unsigned)v->item_count);
        }
        maybe_schedule_viewport_prefetch(controller, v);
    }
}

static int32_t hit_test_hub_tile(const struct library_view_frame *view, int32_t x, int32_t y) {
    if (y < 76) {
        return -1;
    }
    for (uint32_t idx = 0u; idx < LIB_HUB_SECTION_COUNT; idx++) {
        struct library_hub_tile_rect rect;
        library_compute_hub_tile_rect(idx, view->scroll_position, &rect);
        if (rect.alpha >= 0.25f && fabsf((float)x - rect.center_x) <= rect.width * 0.5f &&
            fabsf((float)y - rect.center_y) <= rect.height * 0.5f) {
            return (int32_t)idx;
        }
    }
    return -1;
}

static int32_t hit_test_curved_row(const struct library_view_frame *view, int32_t x, int32_t y) {
    if (y < 76 || view->item_count == 0u) {
        return -1;
    }
    int32_t min_row = (int32_t)floorf(view->scroll_position) - 2;
    int32_t max_row = (int32_t)ceilf(view->scroll_position) + 4;
    if (min_row < 0) {
        min_row = 0;
    }
    if ((uint32_t)max_row >= view->item_count) {
        max_row = (int32_t)(view->item_count - 1u);
    }
    for (int32_t row = min_row; row <= max_row; row++) {
        struct library_curved_row_layout layout;
        library_compute_curved_row_layout((uint32_t)row, view->scroll_position, &layout);
        if (layout.alpha >= 0.22f && fabsf((float)x - (float)SCREEN_CENTER_X) <= layout.row_half_width &&
            fabsf((float)y - layout.row_y) <= layout.row_height * 0.5f + 3.0f) {
            return row;
        }
    }
    return -1;
}

static bool point_in_circle(int32_t x, int32_t y, float cx, float cy, float radius) {
    float dx = (float)x - cx;
    float dy = (float)y - cy;
    return (dx * dx + dy * dy) <= (radius * radius);
}

void library_controller_handle_touch(struct library_controller *controller,
                                     const struct touch_event *event,
                                     struct library_controller_action *action_out) {
    if (action_out != NULL) {
        memset(action_out, 0, sizeof(*action_out));
        action_out->kind = LIBRARY_ACTION_NONE;
    }
    if (controller == NULL || event == NULL || controller->stack_depth == 0u) {
        return;
    }
    struct library_view_frame *view = &controller->view_stack[controller->stack_depth - 1u];

    if (event->kind == TOUCH_EVENT_DOWN) {
        controller->pointer_active = true;
        controller->pointer_start_x = event->x;
        controller->pointer_start_y = event->y;
        controller->pointer_last_x = event->x;
        controller->pointer_last_y = event->y;
        controller->pointer_last_timestamp_ms = event->timestamp_ms;
        controller->pointer_start_scroll = view->scroll_position;
        controller->pointer_velocity = 0.0f;
        controller->max_pointer_drift_px = 0.0f;
        controller->long_press_armed = false;
        controller->sort_long_press_armed = false;
        controller->pointer_started_in_left_edge = false;

        /* 1. Playlist Action Sheet overlay */
        if (controller->playlist_sheet_open) {
            controller->pointer_started_in_left_edge =
                ((float)event->x <= LIBRARY_OVERLAY_BACK_SWIPE_MAX_X);
            if (event->x >= 82 && event->x <= 278 && event->y >= 146 && event->y <= 184) {
                controller->playlist_sheet_pressed_button = 0;
            } else if (event->x >= 82 && event->x <= 278 && event->y >= 188 && event->y <= 226) {
                controller->playlist_sheet_pressed_button = 1;
            } else if (event->x >= 82 && event->x <= 278 && event->y >= 232 && event->y <= 268) {
                controller->playlist_sheet_pressed_button = 2;
                controller->pointer_mode = LIB_PTR_PLAYLIST_HOLD;
                controller->delete_hold_progress = 0.0f;
            } else {
                controller->playlist_sheet_pressed_button = -1;
            }
            return;
        }

        /* 2. Quick Actions overlay */
        if (controller->quick_actions_open) {
            controller->quick_action_pressed = point_in_circle(
                event->x, event->y, (float)SCREEN_CENTER_X, LIBRARY_QUICK_ACTION_BTN_Y,
                LIBRARY_QUICK_ACTION_BTN_HIT_RADIUS_PX);
            controller->pointer_started_in_left_edge =
                !controller->quick_action_pressed &&
                ((float)event->x <= LIBRARY_OVERLAY_BACK_SWIPE_MAX_X);
            return;
        }

        /* 3. Sort buttons */
        if (view->kind == LIB_VIEW_ALBUMS && view->item_count > 0u &&
            point_in_circle(event->x, event->y, LIBRARY_CRATE_SORT_BTN_X, LIBRARY_CRATE_SORT_BTN_Y,
                            LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX)) {
            controller->sort_long_press_armed = true;
            controller->sort_long_press_elapsed_seconds = 0.0f;
            return;
        }
        if (view->kind == LIB_VIEW_ARTISTS && view->item_count > 0u &&
            point_in_circle(event->x, event->y, LIBRARY_ARTISTS_SORT_BTN_X,
                            LIBRARY_ARTISTS_SORT_BTN_Y, LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX)) {
            controller->sort_long_press_armed = true;
            controller->sort_long_press_elapsed_seconds = 0.0f;
            return;
        }

        /* 4. Check rim-pending for outer bezel touch */
        float rel_x = (float)event->x - (float)SCREEN_CENTER_X;
        float rel_y = (float)event->y - (float)SCREEN_CENTER_Y;
        float radius = hypotf(rel_x, rel_y);
        if (view->kind != LIB_VIEW_EMPTY && radius >= LIBRARY_ARC_RIM_MIN_RADIUS_PX &&
            !(view->kind == LIB_VIEW_ALBUMS &&
              point_in_circle(event->x, event->y, LIBRARY_CRATE_NOTE_BTN_X,
                              LIBRARY_CRATE_NOTE_BTN_Y, LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX))) {
            controller->pointer_mode = LIB_PTR_RIM_PENDING;
            controller->pointer_started_in_left_edge =
                ((float)event->x <= LIBRARY_RIM_BACK_SWIPE_MAX_X);
            float angle = atan2f(rel_y, rel_x);
            controller->arc_start_angle = angle;
            controller->arc_current_angle = angle;
            controller->arc_signed_sweep = 0.0f;
            controller->arc_peak_sweep = 0.0f;
            rebuild_seek_anchors_for_current_view(controller);
            return;
        }

        /* 5. Left-edge & content modes */
        float max_edge_x =
            (view->kind == LIB_VIEW_ALBUMS)
                ? LIBRARY_RIM_BACK_SWIPE_MAX_X
                : (view->kind == LIB_VIEW_EMPTY ? LIBRARY_OVERLAY_BACK_SWIPE_MAX_X
                                                : LIBRARY_LIST_BACK_SWIPE_MAX_X);
        controller->pointer_started_in_left_edge = ((float)event->x <= max_edge_x);

        if (view->kind == LIB_VIEW_HUB) {
            int32_t tile = hit_test_hub_tile(view, event->x, event->y);
            if (tile >= 0) {
                controller->pressed_row_index = tile;
                controller->pressed_row_intensity = 0.35f;
            }
            controller->pointer_mode = LIB_PTR_LIST;
        } else if (view->kind == LIB_VIEW_ALBUMS) {
            if (event->x >= 108 && event->x <= 272 && event->y >= 90 && event->y <= 216) {
                controller->long_press_armed = true;
                controller->long_press_elapsed_seconds = 0.0f;
                controller->long_press_target_index = view->target_index;
            }
            controller->pointer_mode = LIB_PTR_CRATE;
        } else if (view->kind != LIB_VIEW_EMPTY) {
            int32_t row = hit_test_curved_row(view, event->x, event->y);
            if (row >= 0) {
                controller->pressed_row_index = row;
                controller->pressed_row_intensity = 0.35f;
                if (view->kind == LIB_VIEW_ARTISTS || view->kind == LIB_VIEW_PLAYLISTS ||
                    view->kind == LIB_VIEW_TRACKS) {
                    controller->long_press_armed = true;
                    controller->long_press_elapsed_seconds = 0.0f;
                    controller->long_press_target_index = row;
                }
            }
            controller->pointer_mode = LIB_PTR_LIST;
        }
        return;
    }

    if (event->kind == TOUCH_EVENT_MOVE) {
        if (!controller->pointer_active) {
            return;
        }
        float dx_total = (float)(event->x - controller->pointer_start_x);
        float dy_total = (float)(event->y - controller->pointer_start_y);
        float drift = hypotf(dx_total, dy_total);
        if (drift > controller->max_pointer_drift_px) {
            controller->max_pointer_drift_px = drift;
        }
        if (drift > LIBRARY_TAP_DRIFT_LIMIT_PX) {
            controller->long_press_armed = false;
            controller->sort_long_press_armed = false;
        }

        float dt_sec = (event->timestamp_ms > controller->pointer_last_timestamp_ms)
                           ? ((float)(event->timestamp_ms - controller->pointer_last_timestamp_ms) *
                              0.001f)
                           : 0.026f;
        if (dt_sec < 0.005f) {
            dt_sec = 0.005f;
        }

        if (controller->pointer_mode == LIB_PTR_SORT_WHEEL) {
            controller->sort_wheel_hovered_slot = library_resolve_sort_wheel_hovered_slot_for_view(
                controller->sort_wheel_anchor_x, controller->sort_wheel_anchor_y, (float)event->x,
                (float)event->y, view->is_favorites_detail);
            return;
        }

        if (controller->pointer_mode == LIB_PTR_PLAYLIST_HOLD) {
            if (event->x < 74 || event->x > 286 || event->y < 224 || event->y > 278) {
                controller->pointer_mode = LIB_PTR_NONE;
            }
            return;
        }

        if (controller->playlist_sheet_open || controller->quick_actions_open ||
            view->kind == LIB_VIEW_EMPTY) {
            if (controller->pointer_started_in_left_edge && dx_total >= 18.0f &&
                dx_total > fabsf(dy_total) * 1.35f) {
                controller->pointer_mode = LIB_PTR_BACK_SWIPE;
                controller->back_swipe_progress =
                    clamp_f32(dx_total / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
            }
            return;
        }

        if (controller->pointer_mode == LIB_PTR_BACK_SWIPE) {
            controller->back_swipe_progress =
                clamp_f32(dx_total / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
            return;
        }

        if (controller->pointer_mode == LIB_PTR_RIM_PENDING) {
            if (controller->pointer_started_in_left_edge && dx_total >= 12.0f &&
                dx_total > fabsf(dy_total) * 1.35f) {
                controller->pointer_mode = LIB_PTR_BACK_SWIPE;
                controller->back_swipe_progress =
                    clamp_f32(dx_total / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
                return;
            }
            if (drift >= LIBRARY_TAP_DRIFT_LIMIT_PX) {
                float rel_x = (float)event->x - (float)SCREEN_CENTER_X;
                float rel_y = (float)event->y - (float)SCREEN_CENTER_Y;
                float radius = hypotf(rel_x, rel_y);
                float angle = atan2f(rel_y, rel_x);
                float angle_delta = fabsf(normalize_angle_delta(angle, controller->arc_start_angle));
                if (radius >= LIBRARY_ARC_ENGAGE_MIN_RADIUS_PX && angle_delta > 0.03f) {
                    controller->pointer_mode = LIB_PTR_ARC_SCROLL;
                    controller->arc_current_angle = angle;
                    controller->arc_signed_sweep = 0.0f;
                    controller->arc_peak_sweep = 0.0f;
                    controller->pointer_start_scroll = view->scroll_position;
                } else {
                    controller->pointer_mode =
                        (view->kind == LIB_VIEW_ALBUMS) ? LIB_PTR_CRATE : LIB_PTR_LIST;
                }
            }
            return;
        }

        if (controller->pointer_mode == LIB_PTR_ARC_SCROLL ||
            controller->pointer_mode == LIB_PTR_LETTER_SEARCH) {
            float rel_x = (float)event->x - (float)SCREEN_CENTER_X;
            float rel_y = (float)event->y - (float)SCREEN_CENTER_Y;
            float angle = atan2f(rel_y, rel_x);
            float step = normalize_angle_delta(angle, controller->arc_current_angle);
            if (fabsf(step) <= 0.0005f) {
                return;
            }
            controller->arc_current_angle = angle;
            controller->arc_signed_sweep += step;

            if (controller->pointer_mode == LIB_PTR_ARC_SCROLL) {
                if (controller->arc_signed_sweep >= 0.0f) {
                    if (controller->arc_signed_sweep > controller->arc_peak_sweep) {
                        controller->arc_peak_sweep = controller->arc_signed_sweep;
                    }
                    if (controller->arc_peak_sweep >= LIBRARY_ARC_RADIANS_PER_STEP &&
                        (controller->arc_peak_sweep - controller->arc_signed_sweep) >=
                            LIBRARY_ARC_RADIANS_PER_STEP) {
                        controller->arc_start_angle += controller->arc_peak_sweep;
                        controller->arc_signed_sweep -= controller->arc_peak_sweep;
                        controller->arc_peak_sweep = controller->arc_signed_sweep;
                        controller->pointer_start_scroll = view->scroll_position;
                    }
                } else {
                    if (controller->arc_signed_sweep < controller->arc_peak_sweep) {
                        controller->arc_peak_sweep = controller->arc_signed_sweep;
                    }
                    if (-controller->arc_peak_sweep >= LIBRARY_ARC_RADIANS_PER_STEP &&
                        (controller->arc_signed_sweep - controller->arc_peak_sweep) >=
                            LIBRARY_ARC_RADIANS_PER_STEP) {
                        controller->arc_start_angle += controller->arc_peak_sweep;
                        controller->arc_signed_sweep -= controller->arc_peak_sweep;
                        controller->arc_peak_sweep = controller->arc_signed_sweep;
                        controller->pointer_start_scroll = view->scroll_position;
                    }
                }

                uint32_t max_scroll = view_max_scroll(view);
                float next_scroll =
                    controller->pointer_start_scroll +
                    (controller->arc_signed_sweep / LIBRARY_ARC_RADIANS_PER_STEP);
                view->scroll_position = clamp_f32(next_scroll, 0.0f, (float)max_scroll);
                view->target_index = (int32_t)lroundf(view->scroll_position);

                if (fabsf(controller->arc_signed_sweep) >= LIBRARY_SEEK_THRESHOLD_RADIANS &&
                    current_view_supports_quick_seek(view) && controller->seek_anchor_count >= 2u) {
                    controller->pointer_mode = LIB_PTR_LETTER_SEARCH;
                    controller->arc_signed_sweep = 0.0f;
                    controller->active_rim_slice = library_resolve_rim_slice_index(angle);
                }
            } else {
                controller->active_rim_slice = library_resolve_rim_slice_index(angle);
                if (fabsf(controller->arc_signed_sweep) >= LIBRARY_SEEK_STEP_RADIANS &&
                    controller->seek_anchor_count > 0u) {
                    int32_t delta_steps =
                        (int32_t)truncf(controller->arc_signed_sweep / LIBRARY_SEEK_STEP_RADIANS);
                    controller->arc_signed_sweep -=
                        (float)delta_steps * LIBRARY_SEEK_STEP_RADIANS;
                    int32_t n = (int32_t)controller->seek_anchor_count;
                    controller->seek_anchor_index =
                        ((controller->seek_anchor_index + delta_steps) % n + n) % n;
                    uint32_t jump_item =
                        controller->seek_anchors[controller->seek_anchor_index].first_item_index;
                    uint32_t max_scroll = view_max_scroll(view);
                    if (jump_item > max_scroll) {
                        jump_item = max_scroll;
                    }
                    view->target_index = (int32_t)jump_item;
                    view->scroll_position = (float)jump_item;
                }
            }
            maybe_schedule_viewport_prefetch(controller, view);
            return;
        }

        /* Standard list / crate / back-swipe drag */
        if (controller->pointer_started_in_left_edge && dx_total >= 26.0f &&
            fabsf(dx_total) > fabsf(dy_total) * 1.45f) {
            controller->pointer_mode = LIB_PTR_BACK_SWIPE;
            controller->back_swipe_progress =
                clamp_f32(dx_total / BACK_SWIPE_PULL_DISTANCE_PX, 0.0f, 1.0f);
            controller->pressed_row_index = -1;
            return;
        }

        uint32_t max_scroll = view_max_scroll(view);
        if (controller->pointer_mode == LIB_PTR_CRATE) {
            float delta_slots = -dx_total / LIBRARY_DRAG_PX_PER_ALBUM;
            view->scroll_position =
                clamp_f32(controller->pointer_start_scroll + delta_slots,
                          -LIBRARY_DRAG_OVERSCROLL_SLOTS,
                          (float)max_scroll + LIBRARY_DRAG_OVERSCROLL_SLOTS);
            controller->pointer_velocity =
                ((float)(event->x - controller->pointer_last_x) / LIBRARY_DRAG_PX_PER_ALBUM) /
                dt_sec;
        } else if (controller->pointer_mode == LIB_PTR_LIST) {
            float px_per_item =
                (view->kind == LIB_VIEW_HUB) ? LIBRARY_DRAG_PX_PER_PAIR : LIBRARY_DRAG_PX_PER_ROW;
            float delta_slots = -dy_total / px_per_item;
            view->scroll_position =
                clamp_f32(controller->pointer_start_scroll + delta_slots,
                          -LIBRARY_DRAG_OVERSCROLL_SLOTS,
                          (float)max_scroll + LIBRARY_DRAG_OVERSCROLL_SLOTS);
            controller->pointer_velocity =
                ((float)(event->y - controller->pointer_last_y) / px_per_item) / dt_sec;
        }
        controller->pointer_last_x = event->x;
        controller->pointer_last_y = event->y;
        controller->pointer_last_timestamp_ms = event->timestamp_ms;
        maybe_schedule_viewport_prefetch(controller, view);
        return;
    }

    if (event->kind == TOUCH_EVENT_UP) {
        if (!controller->pointer_active) {
            return;
        }
        controller->pointer_active = false;
        controller->long_press_armed = false;
        controller->sort_long_press_armed = false;
        bool is_tap = (controller->max_pointer_drift_px <= LIBRARY_TAP_DRIFT_LIMIT_PX);

        if (controller->pointer_mode == LIB_PTR_SORT_WHEEL) {
            int32_t chosen_slot = controller->sort_wheel_hovered_slot;
            controller->sort_wheel_open = false;
            controller->pointer_mode = LIB_PTR_NONE;
            struct library_sort_wheel_slot slots[LIBRARY_SORT_MODE_COUNT];
            uint32_t slot_count = library_compute_sort_wheel_slots_for_view(
                controller->sort_wheel_anchor_x, controller->sort_wheel_anchor_y,
                view->is_favorites_detail, slots);
            if (chosen_slot >= 0 && (uint32_t)chosen_slot < slot_count) {
                apply_sort_mode_for_current_view(controller, slots[chosen_slot].mode, action_out);
            }
            return;
        }

        if (controller->pointer_mode == LIB_PTR_BACK_SWIPE) {
            bool commit = (controller->back_swipe_progress >= BACK_SWIPE_COMMIT_THRESHOLD);
            controller->pointer_mode = LIB_PTR_NONE;
            if (commit) {
                controller->back_swipe_progress = 0.0f;
                (void)step_back_internal(controller, action_out);
            }
            return;
        }

        if (controller->playlist_sheet_open) {
            int32_t pressed_btn = controller->playlist_sheet_pressed_button;
            controller->playlist_sheet_pressed_button = -1;
            controller->pointer_mode = LIB_PTR_NONE;
            if (is_tap) {
                if (event->x < 68 || event->x > 292 || event->y < 84 || event->y > 282) {
                    controller->playlist_sheet_open = false;
                } else if (pressed_btn == 0 || pressed_btn == 1) {
                    int64_t playlist_id = controller->playlist_sheet_id;
                    controller->playlist_sheet_open = false;
                    struct library_db_collection pl_collection = {
                        LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST, playlist_id};
                    emit_play_collection(controller, pl_collection, 0u, (pressed_btn == 1),
                                         action_out);
                }
            }
            return;
        }

        if (controller->quick_actions_open) {
            bool tapped_medallion = is_tap && controller->quick_action_pressed &&
                                    point_in_circle(event->x, event->y, (float)SCREEN_CENTER_X,
                                                    LIBRARY_QUICK_ACTION_BTN_Y,
                                                    LIBRARY_QUICK_ACTION_BTN_HIT_RADIUS_PX);
            controller->quick_action_pressed = false;
            controller->pointer_mode = LIB_PTR_NONE;
            if (tapped_medallion && controller->database != NULL) {
                bool next_favorite = controller->quick_action_row.is_favorite;
                if (controller->quick_action_kind == LIB_QA_ALBUM) {
                    (void)library_db_toggle_favorite(controller->database,
                                                     LIBRARY_DB_FAVORITE_ALBUM,
                                                     controller->quick_action_row.id,
                                                     &next_favorite);
                } else if (controller->quick_action_kind == LIB_QA_ARTIST) {
                    (void)library_db_toggle_favorite(controller->database,
                                                     LIBRARY_DB_FAVORITE_ARTIST,
                                                     controller->quick_action_row.id,
                                                     &next_favorite);
                } else if (controller->quick_action_kind == LIB_QA_TRACK) {
                    (void)library_db_toggle_favorite(controller->database,
                                                     LIBRARY_DB_FAVORITE_TRACK,
                                                     controller->quick_action_row.id,
                                                     &next_favorite);
                }
                controller->quick_action_row.is_favorite = next_favorite;
                sync_cached_db_metadata(controller);
                populate_view_from_db(controller, view);
                if (view->is_favorites_detail) {
                    rebuild_seek_anchors_for_current_view(controller);
                }
                controller->quick_action_auto_close_timer = LIBRARY_QUICK_ACTION_AUTO_CLOSE_SECONDS;
                if (action_out != NULL) {
                    action_out->state_dirty = true;
                }
            } else if (is_tap && !point_in_circle(event->x, event->y, (float)SCREEN_CENTER_X,
                                                  LIBRARY_QUICK_ACTION_BTN_Y,
                                                  LIBRARY_QUICK_ACTION_BTN_HIT_RADIUS_PX)) {
                controller->quick_actions_open = false;
            }
            return;
        }

        if (controller->pointer_mode == LIB_PTR_ARC_SCROLL ||
            controller->pointer_mode == LIB_PTR_LETTER_SEARCH) {
            controller->pointer_mode = LIB_PTR_NONE;
            controller->active_rim_slice = -1;
            maybe_schedule_viewport_prefetch(controller, view);
            return;
        }

        controller->pointer_mode = LIB_PTR_NONE;

        if (!is_tap) {
            uint32_t max_scroll = view_max_scroll(view);
            float weight = (view->kind == LIB_VIEW_ALBUMS) ? 0.045f : 0.04f;
            float projected = view->scroll_position - clamp_f32(controller->pointer_velocity * weight,
                                                                -0.45f, 0.45f);
            int32_t snapped = (int32_t)lroundf(projected);
            if (snapped < 0) {
                snapped = 0;
            }
            if ((uint32_t)snapped > max_scroll) {
                snapped = (int32_t)max_scroll;
            }
            view->target_index = snapped;
            maybe_schedule_viewport_prefetch(controller, view);
            return;
        }

        /* Direct tap handling */
        if (view->kind == LIB_VIEW_EMPTY) {
            if (abs(event->x - (int32_t)SCREEN_CENTER_X) <= 88 && event->y >= 236 &&
                event->y <= 284) {
                (void)step_back_internal(controller, action_out);
            }
            return;
        }

        if ((view->is_artist_detail || view->is_favorites_detail) &&
            (float)event->y >= LIBRARY_ARTIST_TOGGLE_HIT_TOP_Y &&
            (float)event->y <= LIBRARY_ARTIST_TOGGLE_HIT_BOTTOM_Y &&
            fabsf((float)event->x - (float)SCREEN_CENTER_X) <= LIBRARY_ARTIST_TOGGLE_HIT_HALF_W) {
            enum library_tab_mode tab =
                (event->x < (int32_t)SCREEN_CENTER_X) ? LIB_TAB_ALBUMS : LIB_TAB_SONGS;
            handle_segmented_tab_tap(controller, tab);
            return;
        }

        if (view->kind == LIB_VIEW_ALBUMS) {
            if (view->item_count > 0u &&
                point_in_circle(event->x, event->y, LIBRARY_CRATE_SORT_BTN_X,
                                LIBRARY_CRATE_SORT_BTN_Y, LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX) &&
                controller->database != NULL) {
                enum library_sort_mode current_mode =
                    library_controller_effective_sort_mode(controller, view);
                enum library_sort_mode next_mode =
                    (enum library_sort_mode)((current_mode + 1u) % LIBRARY_SORT_MODE_COUNT);
                if (view->is_favorites_detail && next_mode == LIBRARY_SORT_LIKED_FIRST) {
                    next_mode =
                        (enum library_sort_mode)((next_mode + 1u) % LIBRARY_SORT_MODE_COUNT);
                }
                apply_sort_mode_for_current_view(controller, next_mode, action_out);
                return;
            }
            if (point_in_circle(event->x, event->y, LIBRARY_CRATE_NOTE_BTN_X,
                                LIBRARY_CRATE_NOTE_BTN_Y, LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX) ||
                point_in_circle(event->x, event->y, (float)SCREEN_CENTER_X, LIBRARY_CRATE_NOTE_BTN_Y,
                                LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX)) {
                if (view->target_index >= 0 && (uint32_t)view->target_index < view->item_count) {
                    struct library_db_row album_row;
                    if (resolve_row_for_action(controller, view, (uint32_t)view->target_index,
                                               &album_row)) {
                        struct library_view_frame *overlay = push_new_view(controller);
                        if (overlay != NULL) {
                            overlay->kind = LIB_VIEW_TRACKS;
                            overlay->source_type = LIB_TRACKS_ALBUM;
                            overlay->context_id = album_row.id;
                            overlay->parent_album_row = album_row;
                            overlay->show_track_numbers = true;
                            snprintf(overlay->title, sizeof(overlay->title), "%s",
                                     album_row.title);
                            populate_view_from_db(controller, overlay);
                            controller->transition_direction = 1;
                            controller->transition_progress = 0.0f;
                        }
                    }
                }
                return;
            }
            if ((event->x >= 108 && event->x <= 232 && event->y >= 90 && event->y <= 216) ||
                (event->x >= 224 && event->x <= 272 && event->y >= 96 && event->y <= 212)) {
                if (view->target_index >= 0 && (uint32_t)view->target_index < view->item_count) {
                    struct library_db_row album_row;
                    if (resolve_row_for_action(controller, view, (uint32_t)view->target_index,
                                               &album_row)) {
                        if (controller->has_active_track &&
                            controller->active_album_id == album_row.id) {
                            action_out->kind = LIBRARY_ACTION_TOGGLE_PLAY_PAUSE;
                        } else {
                            struct library_db_collection album_tracks = {
                                LIBRARY_DB_COLLECTION_TRACKS_ALBUM, album_row.id};
                            emit_play_collection(controller, album_tracks, 0u, false, action_out);
                        }
                    }
                }
                return;
            }
            if (event->x < 108 && view->target_index > 0) {
                view->target_index--;
            } else if (event->x > 272 && (uint32_t)(view->target_index + 1) < view->item_count) {
                view->target_index++;
            }
            maybe_schedule_viewport_prefetch(controller, view);
            return;
        }

        if (view->kind == LIB_VIEW_ARTISTS &&
            point_in_circle(event->x, event->y, LIBRARY_ARTISTS_SORT_BTN_X,
                            LIBRARY_ARTISTS_SORT_BTN_Y, LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX) &&
            controller->database != NULL) {
            enum library_sort_mode next_mode =
                (enum library_sort_mode)((controller->artist_sort_mode + 1u) %
                                         LIBRARY_SORT_MODE_COUNT);
            apply_sort_mode_for_current_view(controller, next_mode, action_out);
            return;
        }

        if (view->kind == LIB_VIEW_HUB) {
            int32_t tile = hit_test_hub_tile(view, event->x, event->y);
            if (tile >= 0) {
                controller->tap_flash_index = tile;
                controller->tap_flash_intensity = 1.0f;
                handle_hub_tile_tap(controller, (uint32_t)tile);
            }
            return;
        }

        if (view->kind == LIB_VIEW_TRACKS && view->source_type == LIB_TRACKS_ALBUM &&
            (point_in_circle(event->x, event->y, LIBRARY_CRATE_NOTE_BTN_X, LIBRARY_CRATE_NOTE_BTN_Y,
                             LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX) ||
             point_in_circle(event->x, event->y, (float)SCREEN_CENTER_X, LIBRARY_CRATE_NOTE_BTN_Y,
                             LIBRARY_BOTTOM_BTN_HIT_RADIUS_PX))) {
            if (hit_test_curved_row(view, event->x, event->y) < 0) {
                (void)step_back_internal(controller, action_out);
                return;
            }
        }

        int32_t row = hit_test_curved_row(view, event->x, event->y);
        if (row >= 0) {
            controller->tap_flash_index = row;
            controller->tap_flash_intensity = 1.0f;
            handle_list_row_tap(controller, view, (uint32_t)row, action_out);
        }
    }
}
