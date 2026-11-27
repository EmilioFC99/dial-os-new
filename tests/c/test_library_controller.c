#include "test_runner.h"

#include "library_controller.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t test_db_sequence = 0u;

static void build_test_db(struct library_db *db, char *db_dir_out, size_t dir_cap) {
    test_db_sequence++;
    snprintf(db_dir_out, dir_cap, "/tmp/dialos_ctrl_test_%d_%u", (int)getpid(),
             (unsigned)test_db_sequence);
    char rm_cmd[160];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", db_dir_out);
    (void)system(rm_cmd);
    mkdir(db_dir_out, 0755);
    TEST_ASSERT_TRUE(library_db_open(db, db_dir_out));

    struct library_db_track_input tracks[4] = {
        {.path = "/sd/a/1.flac", .title = "Alpha", .artist = "Artist One", .album = "Album A",
         .genre = "House", .track_number = 1u, .duration_seconds = 120u,
         .mtime_seconds = 1700000000ll, .audio_fingerprint = 0x101ull},
        {.path = "/sd/a/2.flac", .title = "Bravo", .artist = "Artist One", .album = "Album B",
         .genre = "House", .track_number = 1u, .duration_seconds = 140u,
         .mtime_seconds = 1710000000ll, .audio_fingerprint = 0x102ull},
        {.path = "/sd/b/1.flac", .title = "Charlie", .artist = "Artist Two", .album = "Album C",
         .genre = "Ambient", .track_number = 1u, .duration_seconds = 160u,
         .mtime_seconds = 1720000000ll, .audio_fingerprint = 0x103ull},
        {.path = "/sd/c/1.flac", .title = "Delta", .artist = "Artist Three", .album = "Album D",
         .genre = "Dub", .track_number = 1u, .duration_seconds = 180u,
         .mtime_seconds = 1730000000ll, .audio_fingerprint = 0x104ull},
    };
    for (uint32_t idx = 0u; idx < 4u; idx++) {
        TEST_ASSERT_TRUE(library_db_upsert_track(db, &tracks[idx], 0, NULL));
    }
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(db, NULL, 0u, NULL));
    const char *pl_paths[2] = {"/sd/a/1.flac", "/sd/b/1.flac"};
    TEST_ASSERT_TRUE(
        library_db_upsert_m3u_playlist(db, "Late Mix", "/sd/late_mix.m3u", pl_paths, 2u));
}

static void send_tap(struct library_controller *controller, int32_t x, int32_t y, uint64_t ts_ms,
                     struct library_controller_action *action_out) {
    struct touch_event down = {TOUCH_EVENT_DOWN, x, y, ts_ms};
    struct touch_event up = {TOUCH_EVENT_UP, x, y, ts_ms + 40ull};
    library_controller_handle_touch(controller, &down, action_out);
    library_controller_handle_touch(controller, &up, action_out);
}

static void step_for_seconds(struct library_controller *controller, float total_seconds) {
    float remaining = total_seconds;
    while (remaining > 0.0001f) {
        float chunk = remaining > 0.02f ? 0.02f : remaining;
        library_controller_step(controller, chunk);
        remaining -= chunk;
    }
}

static void test_geometry_formulas(void) {
    struct library_hub_tile_rect tile0, tile2;
    library_compute_hub_tile_rect(0u, 0.0f, &tile0);
    library_compute_hub_tile_rect(2u, 0.0f, &tile2);
    TEST_ASSERT_TRUE(fabsf(tile0.width - 114.0f) < 0.1f);
    TEST_ASSERT_TRUE(fabsf(tile0.height - 112.0f) < 0.1f);
    TEST_ASSERT_TRUE(fabsf(tile0.center_x - 116.0f) < 0.1f);
    TEST_ASSERT_TRUE(fabsf(tile0.center_y - 152.0f) < 0.1f);
    TEST_ASSERT_TRUE(fabsf(tile2.width - 94.0f) < 0.1f);
    TEST_ASSERT_TRUE(fabsf(tile2.alpha - 0.38f) < 0.05f);

    struct library_curved_row_layout row0, row2;
    library_compute_curved_row_layout(0u, 0.0f, &row0);
    library_compute_curved_row_layout(2u, 0.0f, &row2);
    TEST_ASSERT_TRUE(fabsf(row0.row_y - 126.0f) < 0.1f);
    TEST_ASSERT_TRUE(fabsf(row0.row_height - 62.0f) < 0.1f);
    TEST_ASSERT_TRUE(fabsf(row2.row_height - 46.0f) < 0.1f);
    TEST_ASSERT_TRUE(fabsf(row2.alpha - 0.42f) < 0.05f);

    TEST_ASSERT_EQ_I32(0, library_resolve_rim_slice_index(-1.570796f));
    TEST_ASSERT_EQ_U32(1u, library_shortest_slice_distance(0u, 16u));

    struct library_crate_bars_window bars;
    library_compute_windowed_crate_bars(12u, 6.0f, &bars);
    TEST_ASSERT_EQ_U32(7u, bars.window_count);
    TEST_ASSERT_EQ_U32(3u, bars.start_index);

    TEST_ASSERT_EQ_I32(-1, library_resolve_sort_wheel_hovered_mode(152.0f, 325.0f, 152.0f, 315.0f));
    TEST_ASSERT_EQ_I32(-1, library_resolve_sort_wheel_hovered_mode(152.0f, 325.0f, 152.0f, 360.0f));
    TEST_ASSERT_EQ_I32(2, library_resolve_sort_wheel_hovered_mode(152.0f, 325.0f, 152.0f, 249.0f));
}

static void test_hub_bloom_scroll_preservation_and_playback(void) {
    struct library_db db;
    char db_path[128];
    build_test_db(&db, db_path, sizeof(db_path));

    struct library_controller ctrl;
    library_controller_initialize(&ctrl, &db, NULL);
    struct library_controller_action action;

    /* Scroll Hub to pair 1, tap Albums tile (index 3 -> bottom-right of pair 1) */
    ctrl.view_stack[0].scroll_position = 1.0f;
    ctrl.view_stack[0].target_index = 1;
    send_tap(&ctrl, 244, 152, 1000ull, &action);
    TEST_ASSERT_EQ_I32(LIB_VIEW_ALBUMS, library_controller_current_view(&ctrl)->kind);
    TEST_ASSERT_EQ_I32(LIB_BLOOM_HUB_TO_ALBUMS, ctrl.bloom_direction);
    library_controller_step(&ctrl, 0.6f);

    /* Tap focused sleeve -> plays album tracks from 0 */
    send_tap(&ctrl, 172, 152, 2000ull, &action);
    TEST_ASSERT_EQ_I32(LIBRARY_ACTION_PLAY_TRACKS, action.kind);
    TEST_ASSERT_EQ_I32(LIBRARY_DB_COLLECTION_TRACKS_ALBUM, action.play_collection.kind);
    TEST_ASSERT_EQ_U32(0u, action.play_start_offset);
    TEST_ASSERT_EQ_STR("/sd/a/1.flac", action.play_first_track_path);

    /* Sync active playback -> tapping same focused sleeve toggles play/pause */
    library_controller_set_active_playback(&ctrl, "/sd/a/1.flac", true);
    send_tap(&ctrl, 172, 152, 2500ull, &action);
    TEST_ASSERT_EQ_I32(LIBRARY_ACTION_TOGGLE_PLAY_PAUSE, action.kind);

    /* Open Album Tracklist Crystal Overlay via Note button (208, 325) */
    send_tap(&ctrl, 208, 325, 3000ull, &action);
    TEST_ASSERT_EQ_I32(LIB_VIEW_TRACKS, library_controller_current_view(&ctrl)->kind);
    TEST_ASSERT_EQ_I32(LIB_TRACKS_ALBUM, library_controller_current_view(&ctrl)->source_type);

    /* Left-edge swipe back from Tracklist -> Albums -> Hub preserves Hub scroll = 1.0 */
    struct touch_event sw_d = {TOUCH_EVENT_DOWN, 20, 180, 4000ull};
    struct touch_event sw_m = {TOUCH_EVENT_MOVE, 90, 180, 4040ull};
    struct touch_event sw_u = {TOUCH_EVENT_UP, 90, 180, 4060ull};
    library_controller_handle_touch(&ctrl, &sw_d, &action);
    library_controller_handle_touch(&ctrl, &sw_m, &action);
    library_controller_handle_touch(&ctrl, &sw_u, &action);
    TEST_ASSERT_EQ_I32(LIB_VIEW_ALBUMS, library_controller_current_view(&ctrl)->kind);

    library_controller_handle_touch(&ctrl, &sw_d, &action);
    library_controller_handle_touch(&ctrl, &sw_m, &action);
    library_controller_handle_touch(&ctrl, &sw_u, &action);
    TEST_ASSERT_EQ_I32(LIB_VIEW_HUB, library_controller_current_view(&ctrl)->kind);
    TEST_ASSERT_EQ_I32(LIB_BLOOM_ALBUMS_TO_HUB, ctrl.bloom_direction);
    TEST_ASSERT_TRUE(fabsf(ctrl.view_stack[0].scroll_position - 1.0f) < 0.05f);

    /* Left-edge swipe from Hub emits EXIT_TO_HOME */
    library_controller_handle_touch(&ctrl, &sw_d, &action);
    library_controller_handle_touch(&ctrl, &sw_m, &action);
    library_controller_handle_touch(&ctrl, &sw_u, &action);
    TEST_ASSERT_EQ_I32(LIBRARY_ACTION_EXIT_TO_HOME, action.kind);

    library_controller_destroy(&ctrl);
    library_db_close(&db);
    (void)unlink(db_path);
}

static void test_sort_wheel_quick_seek_and_long_press_overlays(void) {
    struct library_db db;
    char db_path[128];
    build_test_db(&db, db_path, sizeof(db_path));

    struct library_controller ctrl;
    library_controller_initialize(&ctrl, &db, NULL);
    struct library_controller_action action;

    /* Open Albums, tap Sort button -> cycles sort mode + toast */
    ctrl.view_stack[0].scroll_position = 1.0f;
    ctrl.view_stack[0].target_index = 1;
    send_tap(&ctrl, 244, 152, 1000ull, &action);
    step_for_seconds(&ctrl, 0.6f);

    send_tap(&ctrl, 152, 325, 2000ull, &action);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_REVERSE_ALPHABETICAL,
                       library_db_get_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS));
    TEST_ASSERT_TRUE(ctrl.sort_toast_active);
    TEST_ASSERT_TRUE(action.state_dirty);

    /* Hold Sort button 0.36s -> opens Radial Sort Wheel, drag to LIKED (top = 152, 249) */
    struct touch_event s_down = {TOUCH_EVENT_DOWN, 152, 325, 3000ull};
    library_controller_handle_touch(&ctrl, &s_down, &action);
    step_for_seconds(&ctrl, 0.40f);
    TEST_ASSERT_TRUE(ctrl.sort_wheel_open);
    struct touch_event s_move = {TOUCH_EVENT_MOVE, 152, 249, 3420ull};
    struct touch_event s_up = {TOUCH_EVENT_UP, 152, 249, 3460ull};
    library_controller_handle_touch(&ctrl, &s_move, &action);
    TEST_ASSERT_EQ_I32(2, ctrl.sort_wheel_hovered_slot);
    library_controller_handle_touch(&ctrl, &s_up, &action);
    TEST_ASSERT_FALSE(ctrl.sort_wheel_open);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_LIKED_FIRST,
                       library_db_get_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS));

    /* Long-press focused sleeve -> Album Quick Actions -> tap favorite -> auto-closes */
    struct touch_event qa_down = {TOUCH_EVENT_DOWN, 172, 152, 4000ull};
    library_controller_handle_touch(&ctrl, &qa_down, &action);
    step_for_seconds(&ctrl, 0.52f);
    TEST_ASSERT_TRUE(ctrl.quick_actions_open);
    struct touch_event qa_up = {TOUCH_EVENT_UP, 172, 152, 4540ull};
    library_controller_handle_touch(&ctrl, &qa_up, &action);
    send_tap(&ctrl, 180, 196, 4600ull, &action);
    TEST_ASSERT_TRUE(action.state_dirty);
    struct library_db_counts counts;
    TEST_ASSERT_TRUE(library_db_read_counts(&db, &counts));
    TEST_ASSERT_EQ_U32(1u, counts.favorite_albums);
    step_for_seconds(&ctrl, 0.30f);
    TEST_ASSERT_FALSE(ctrl.quick_actions_open);

    /* Two-stage Rim-Arc Scroll -> 17-Slab Quick-Seek (>= PI radians) */
    float radius = 150.0f;
    struct touch_event rim_d = {TOUCH_EVENT_DOWN, 180, 180 - (int32_t)radius, 5000ull};
    library_controller_handle_touch(&ctrl, &rim_d, &action);
    for (int step = 1; step <= 12; step++) {
        float angle = -1.570796f + ((float)step / 12.0f) * 3.5f;
        struct touch_event rim_m = {TOUCH_EVENT_MOVE,
                                    180 + (int32_t)lroundf(cosf(angle) * radius),
                                    180 + (int32_t)lroundf(sinf(angle) * radius),
                                    5000ull + (uint64_t)(step * 26)};
        library_controller_handle_touch(&ctrl, &rim_m, &action);
    }
    TEST_ASSERT_EQ_I32(LIB_PTR_LETTER_SEARCH, ctrl.pointer_mode);
    struct touch_event rim_u = {TOUCH_EVENT_UP, 180, 330, 5400ull};
    library_controller_handle_touch(&ctrl, &rim_u, &action);
    TEST_ASSERT_EQ_I32(LIB_PTR_NONE, ctrl.pointer_mode);

    /* Playlist Action Sheet & Hold-to-Delete (1.10s) */
    library_controller_reset_to_hub(&ctrl, true);
    send_tap(&ctrl, 244, 152, 6100ull, &action);
    TEST_ASSERT_EQ_I32(LIB_VIEW_PLAYLISTS, library_controller_current_view(&ctrl)->kind);
    struct touch_event pl_down = {TOUCH_EVENT_DOWN, 180, 126, 6200ull};
    library_controller_handle_touch(&ctrl, &pl_down, &action);
    step_for_seconds(&ctrl, 0.52f);
    TEST_ASSERT_TRUE(ctrl.playlist_sheet_open);
    struct touch_event pl_up = {TOUCH_EVENT_UP, 180, 126, 6750ull};
    library_controller_handle_touch(&ctrl, &pl_up, &action);

    /* Hold to Delete at (180, 250) for 1.15s */
    struct touch_event del_down = {TOUCH_EVENT_DOWN, 180, 250, 7000ull};
    library_controller_handle_touch(&ctrl, &del_down, &action);
    step_for_seconds(&ctrl, 1.15f);
    TEST_ASSERT_FALSE(ctrl.playlist_sheet_open);
    TEST_ASSERT_TRUE(library_db_read_counts(&db, &counts));
    TEST_ASSERT_EQ_U32(0u, counts.playlists);
    TEST_ASSERT_EQ_I32(LIB_VIEW_EMPTY, library_controller_current_view(&ctrl)->kind);

    library_controller_destroy(&ctrl);
    library_db_close(&db);
    (void)unlink(db_path);
}

static void test_track_row_long_press_quick_action(void) {
    struct library_db db;
    char db_path[128];
    build_test_db(&db, db_path, sizeof(db_path));

    struct library_controller controller;
    library_controller_initialize(&controller, &db, NULL);
    struct library_controller_action action;

    /* Open All songs (Hub pair 2, right tile = (244, 152)) */
    controller.view_stack[0].scroll_position = 2.0f;
    controller.view_stack[0].target_index = 2;
    send_tap(&controller, 244, 152, 1000ull, &action);
    step_for_seconds(&controller, 0.35f);
    const struct library_view_frame *tracks_view = library_controller_current_view(&controller);
    TEST_ASSERT_EQ_I32(LIB_VIEW_TRACKS, tracks_view->kind);
    TEST_ASSERT_EQ_U32(4u, tracks_view->item_count);

    /* Boundary: drift > LIBRARY_TAP_DRIFT_LIMIT_PX cancels long-press */
    struct touch_event drift_down = {TOUCH_EVENT_DOWN, 180, 126, 1500ull};
    struct touch_event drift_move = {TOUCH_EVENT_MOVE, 180, 142, 1540ull};
    struct touch_event drift_up = {TOUCH_EVENT_UP, 180, 142, 1580ull};
    library_controller_handle_touch(&controller, &drift_down, &action);
    library_controller_handle_touch(&controller, &drift_move, &action);
    step_for_seconds(&controller, 0.52f);
    TEST_ASSERT_FALSE(controller.quick_actions_open);
    library_controller_handle_touch(&controller, &drift_up, &action);
    controller.view_stack[controller.stack_depth - 1u].scroll_position = 0.0f;
    controller.view_stack[controller.stack_depth - 1u].target_index = 0;

    /* Long-press row 0 (Alpha) for 0.52s -> opens LIB_QA_TRACK */
    const struct library_db_row *first_row = library_controller_get_row(&controller, 0u);
    TEST_ASSERT_TRUE(first_row != NULL);
    int64_t expected_track_id = first_row->id;
    struct touch_event hold_down = {TOUCH_EVENT_DOWN, 180, 126, 2000ull};
    library_controller_handle_touch(&controller, &hold_down, &action);
    step_for_seconds(&controller, 0.52f);
    TEST_ASSERT_TRUE(controller.quick_actions_open);
    TEST_ASSERT_EQ_I32(LIB_QA_TRACK, controller.quick_action_kind);
    TEST_ASSERT_TRUE(controller.quick_action_row.id == expected_track_id);
    struct touch_event hold_up = {TOUCH_EVENT_UP, 180, 126, 2540ull};
    library_controller_handle_touch(&controller, &hold_up, &action);

    /* Negative: tap outside medallion closes without toggling favorite */
    send_tap(&controller, 180, 316, 2600ull, &action);
    TEST_ASSERT_FALSE(controller.quick_actions_open);
    TEST_ASSERT_FALSE(action.state_dirty);
    struct library_db_counts counts;
    TEST_ASSERT_TRUE(library_db_read_counts(&db, &counts));
    TEST_ASSERT_EQ_U32(0u, counts.favorite_tracks);

    /* Re-open long-press -> tap medallion -> favorites track + auto-closes */
    struct touch_event hold2_down = {TOUCH_EVENT_DOWN, 180, 126, 3000ull};
    library_controller_handle_touch(&controller, &hold2_down, &action);
    step_for_seconds(&controller, 0.52f);
    TEST_ASSERT_TRUE(controller.quick_actions_open);
    struct touch_event hold2_up = {TOUCH_EVENT_UP, 180, 126, 3540ull};
    library_controller_handle_touch(&controller, &hold2_up, &action);

    send_tap(&controller, 180, 196, 3600ull, &action);
    TEST_ASSERT_TRUE(action.state_dirty);
    TEST_ASSERT_TRUE(library_db_read_counts(&db, &counts));
    TEST_ASSERT_EQ_U32(1u, counts.favorite_tracks);
    first_row = library_controller_get_row(&controller, 0u);
    TEST_ASSERT_TRUE(first_row != NULL && first_row->is_favorite);
    step_for_seconds(&controller, 0.30f);
    TEST_ASSERT_FALSE(controller.quick_actions_open);

    /* Album Tracklist also supports track row long-press Quick Actions */
    library_controller_reset_to_hub(&controller, true);
    controller.view_stack[0].scroll_position = 1.0f;
    controller.view_stack[0].target_index = 1;
    send_tap(&controller, 244, 152, 4000ull, &action);
    step_for_seconds(&controller, 0.6f);
    send_tap(&controller, 208, 325, 4700ull, &action);
    step_for_seconds(&controller, 0.35f);
    TEST_ASSERT_EQ_I32(LIB_TRACKS_ALBUM, library_controller_current_view(&controller)->source_type);
    struct touch_event album_row_down = {TOUCH_EVENT_DOWN, 180, 126, 5200ull};
    library_controller_handle_touch(&controller, &album_row_down, &action);
    step_for_seconds(&controller, 0.52f);
    TEST_ASSERT_TRUE(controller.quick_actions_open);
    TEST_ASSERT_EQ_I32(LIB_QA_TRACK, controller.quick_action_kind);
    struct touch_event album_row_up = {TOUCH_EVENT_UP, 180, 126, 5740ull};
    library_controller_handle_touch(&controller, &album_row_up, &action);
    controller.quick_actions_open = false;

    /* Favorites -> SONGS opens automatically when only favorite tracks exist; unfavoriting refreshes list */
    library_controller_reset_to_hub(&controller, true);
    send_tap(&controller, 116, 152, 6000ull, &action);
    step_for_seconds(&controller, 0.35f);
    const struct library_view_frame *fav_view = library_controller_current_view(&controller);
    TEST_ASSERT_TRUE(fav_view->is_favorites_detail);
    TEST_ASSERT_EQ_I32(LIB_VIEW_TRACKS, fav_view->kind);
    TEST_ASSERT_EQ_I32(LIB_TRACKS_FAVORITES, fav_view->source_type);
    TEST_ASSERT_EQ_U32(1u, fav_view->item_count);

    struct touch_event fav_hold_down = {TOUCH_EVENT_DOWN, 180, 126, 6500ull};
    library_controller_handle_touch(&controller, &fav_hold_down, &action);
    step_for_seconds(&controller, 0.52f);
    TEST_ASSERT_TRUE(controller.quick_actions_open);
    TEST_ASSERT_EQ_I32(LIB_QA_TRACK, controller.quick_action_kind);
    struct touch_event fav_hold_up = {TOUCH_EVENT_UP, 180, 126, 7040ull};
    library_controller_handle_touch(&controller, &fav_hold_up, &action);
    send_tap(&controller, 180, 196, 7100ull, &action);
    TEST_ASSERT_TRUE(action.state_dirty);
    TEST_ASSERT_TRUE(library_db_read_counts(&db, &counts));
    TEST_ASSERT_EQ_U32(0u, counts.favorite_tracks);
    TEST_ASSERT_EQ_U32(0u, fav_view->item_count);

    library_controller_destroy(&controller);
    library_db_close(&db);
    (void)unlink(db_path);
}

static void test_favorites_excludes_liked_first_sort(void) {
    struct library_sort_wheel_slot slots[LIBRARY_SORT_MODE_COUNT];
    uint32_t slot_count = library_compute_sort_wheel_slots_for_view(152.0f, 325.0f, true, slots);
    TEST_ASSERT_EQ_U32(4u, slot_count);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_ALPHABETICAL, slots[0].mode);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_REVERSE_ALPHABETICAL, slots[1].mode);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_RECENT, slots[2].mode);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_OLDEST, slots[3].mode);
    for (uint32_t idx = 0u; idx < slot_count; idx++) {
        TEST_ASSERT_TRUE(slots[idx].mode != LIBRARY_SORT_LIKED_FIRST);
    }

    TEST_ASSERT_EQ_I32(
        -1, library_resolve_sort_wheel_hovered_slot_for_view(152.0f, 325.0f, 152.0f, 315.0f, true));
    TEST_ASSERT_EQ_I32(
        -1, library_resolve_sort_wheel_hovered_slot_for_view(152.0f, 325.0f, 152.0f, 360.0f, true));
    for (uint32_t idx = 0u; idx < slot_count; idx++) {
        int32_t hovered = library_resolve_sort_wheel_hovered_slot_for_view(
            152.0f, 325.0f, slots[idx].center_x, slots[idx].center_y, true);
        TEST_ASSERT_EQ_I32((int32_t)idx, hovered);
    }

    struct library_db db;
    char db_path[128];
    build_test_db(&db, db_path, sizeof(db_path));
    struct library_db_collection albums_col = {LIBRARY_DB_COLLECTION_ALBUMS, 0};
    struct library_db_row album_rows[4];
    TEST_ASSERT_EQ_U32(4u, library_db_fetch_window(&db, &albums_col, LIBRARY_SORT_ALPHABETICAL, 0u,
                                                   4u, album_rows));
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&db, LIBRARY_DB_FAVORITE_ALBUM, album_rows[0].id, NULL));
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&db, LIBRARY_DB_FAVORITE_ALBUM, album_rows[1].id, NULL));
    TEST_ASSERT_TRUE(library_db_set_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS, LIBRARY_SORT_LIKED_FIRST));

    struct library_controller controller;
    library_controller_initialize(&controller, &db, NULL);
    struct library_controller_action action;

    /* Open Favorites -> ALBUMS (preferred default when favorite albums exist) */
    send_tap(&controller, 116, 152, 1000ull, &action);
    step_for_seconds(&controller, 0.35f);
    const struct library_view_frame *fav_albums = library_controller_current_view(&controller);
    TEST_ASSERT_TRUE(fav_albums->is_favorites_detail);
    TEST_ASSERT_EQ_I32(LIB_VIEW_ALBUMS, fav_albums->kind);
    TEST_ASSERT_EQ_U32(2u, fav_albums->item_count);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_ALPHABETICAL,
                       library_controller_effective_sort_mode(&controller, fav_albums));
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_LIKED_FIRST,
                       library_db_get_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS));

    /* Tap-cycling Sort button skips LIBRARY_SORT_LIKED_FIRST */
    send_tap(&controller, 152, 325, 2000ull, &action);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_REVERSE_ALPHABETICAL,
                       library_db_get_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS));
    send_tap(&controller, 152, 325, 2200ull, &action);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_RECENT,
                       library_db_get_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS));
    send_tap(&controller, 152, 325, 2400ull, &action);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_OLDEST,
                       library_db_get_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS));
    send_tap(&controller, 152, 325, 2600ull, &action);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_ALPHABETICAL,
                       library_db_get_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS));

    /* Holding Sort button opens 4-slot wheel; slot 2 selects MOST RECENT, slot 3 selects OLDEST */
    struct touch_event wheel_down = {TOUCH_EVENT_DOWN, 152, 325, 3000ull};
    library_controller_handle_touch(&controller, &wheel_down, &action);
    step_for_seconds(&controller, 0.40f);
    TEST_ASSERT_TRUE(controller.sort_wheel_open);
    struct touch_event wheel_move = {TOUCH_EVENT_MOVE, (int32_t)lroundf(slots[2].center_x),
                                     (int32_t)lroundf(slots[2].center_y), 3420ull};
    struct touch_event wheel_up = {TOUCH_EVENT_UP, (int32_t)lroundf(slots[2].center_x),
                                   (int32_t)lroundf(slots[2].center_y), 3460ull};
    library_controller_handle_touch(&controller, &wheel_move, &action);
    TEST_ASSERT_EQ_I32(2, controller.sort_wheel_hovered_slot);
    library_controller_handle_touch(&controller, &wheel_up, &action);
    TEST_ASSERT_FALSE(controller.sort_wheel_open);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_RECENT,
                       library_db_get_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS));

    struct touch_event wheel2_down = {TOUCH_EVENT_DOWN, 152, 325, 4000ull};
    library_controller_handle_touch(&controller, &wheel2_down, &action);
    step_for_seconds(&controller, 0.40f);
    struct touch_event wheel2_move = {TOUCH_EVENT_MOVE, (int32_t)lroundf(slots[3].center_x),
                                      (int32_t)lroundf(slots[3].center_y), 4420ull};
    struct touch_event wheel2_up = {TOUCH_EVENT_UP, (int32_t)lroundf(slots[3].center_x),
                                    (int32_t)lroundf(slots[3].center_y), 4460ull};
    library_controller_handle_touch(&controller, &wheel2_move, &action);
    TEST_ASSERT_EQ_I32(3, controller.sort_wheel_hovered_slot);
    library_controller_handle_touch(&controller, &wheel2_up, &action);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_OLDEST,
                       library_db_get_sort_mode(&db, LIBRARY_DB_SORT_SCOPE_ALBUMS));

    library_controller_destroy(&controller);
    library_db_close(&db);
    (void)unlink(db_path);
}

static void test_sliding_window_prefetch_beyond_96_rows(void) {
    char db_dir[128];
    test_db_sequence++;
    snprintf(db_dir, sizeof(db_dir), "/tmp/dialos_ctrl_win_%d_%u", (int)getpid(),
             (unsigned)test_db_sequence);
    char rm_cmd[160];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", db_dir);
    (void)system(rm_cmd);
    mkdir(db_dir, 0755);
    struct library_db db;
    TEST_ASSERT_TRUE(library_db_open(&db, db_dir));
    TEST_ASSERT_TRUE(library_db_begin(&db));
    for (uint32_t idx = 0u; idx < 160u; idx++) {
        char path[64], title[64];
        snprintf(path, sizeof(path), "/sd/t_%03u.flac", (unsigned)idx);
        snprintf(title, sizeof(title), "Track %03u", (unsigned)idx);
        struct library_db_track_input input = {
            .path = path,
            .title = title,
            .artist = "Artist",
            .album = "Big Album",
            .genre = "Genre",
            .track_number = idx + 1u,
            .duration_seconds = 100u,
            .audio_fingerprint = 0x1000ull + (uint64_t)idx,
        };
        TEST_ASSERT_TRUE(library_db_upsert_track(&db, &input, 0, NULL));
    }
    TEST_ASSERT_TRUE(library_db_commit(&db));
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&db, NULL, 0u, NULL));

    struct library_controller controller;
    library_controller_initialize(&controller, &db, NULL);
    struct library_controller_action action;

    controller.view_stack[0].scroll_position = 2.0f;
    controller.view_stack[0].target_index = 2;
    send_tap(&controller, 244, 152, 1000ull, &action);
    step_for_seconds(&controller, 0.35f);
    TEST_ASSERT_EQ_U32(160u, library_controller_current_view(&controller)->item_count);

    /* Row 120 is outside initial 0..95 viewport -> scroll, step, and flush Core 1 prefetch */
    TEST_ASSERT_TRUE(library_controller_get_row(&controller, 120u) == NULL);
    controller.view_stack[controller.stack_depth - 1u].scroll_position = 120.0f;
    controller.view_stack[controller.stack_depth - 1u].target_index = 120;
    library_controller_step(&controller, 0.02f);
    library_controller_flush_prefetch(&controller);

    const struct library_db_row *row_120 = library_controller_get_row(&controller, 120u);
    TEST_ASSERT_TRUE(row_120 != NULL);
    TEST_ASSERT_EQ_STR("Track 120", row_120->title);

    library_controller_destroy(&controller);
    library_db_close(&db);
    (void)system(rm_cmd);
}

void run_library_controller_tests(void) {
    printf("Suite: library_controller\n");
    RUN_TEST(test_geometry_formulas);
    RUN_TEST(test_hub_bloom_scroll_preservation_and_playback);
    RUN_TEST(test_sort_wheel_quick_seek_and_long_press_overlays);
    RUN_TEST(test_track_row_long_press_quick_action);
    RUN_TEST(test_favorites_excludes_liked_first_sort);
    RUN_TEST(test_sliding_window_prefetch_beyond_96_rows);
}
