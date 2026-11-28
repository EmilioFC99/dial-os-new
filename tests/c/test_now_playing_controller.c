#include "test_runner.h"

#include "now_playing_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void make_ctrl_temp_dir(char *path_out, size_t capacity) {
    snprintf(path_out, capacity, "/tmp/dialos_npc_XXXXXX");
    TEST_ASSERT_TRUE(mkdtemp(path_out) != NULL);
}

static void cleanup_ctrl_temp_dir(const char *directory) {
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/library.db", directory);
    unlink(file_path);
    snprintf(file_path, sizeof(file_path), "%s/library.db-journal", directory);
    unlink(file_path);
    rmdir(directory);
}

static int64_t seed_ctrl_track(struct library_db *database, const char *path, const char *title,
                               const char *album, uint16_t num) {
    struct library_db_track_input input = {0};
    input.path = path;
    input.audio_fingerprint = library_fnv1a_64(path);
    input.file_size_bytes = 4096u;
    input.mtime_seconds = 1700000000;
    input.format_kind = LIBRARY_FORMAT_FLAC;
    input.title = title;
    input.artist = "Artist";
    input.album_artist = "Artist";
    input.album = album;
    input.genre = "Synth";
    input.year = 2025u;
    input.disc_number = 1u;
    input.track_number = num;
    input.duration_seconds = 200u;
    input.added_at_seconds = 1700000000 + (int64_t)num;
    int64_t id = 0;
    if (!library_db_upsert_track(database, &input, 0, &id)) {
        return 0;
    }
    return id;
}

static void test_tap_seek_volume_and_6_level_back_stack(void) {
    char dir_path[128];
    make_ctrl_temp_dir(dir_path, sizeof(dir_path));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, dir_path));

    int64_t t1 = seed_ctrl_track(&database, "/m/1.flac", "Alpha", "Album A", 1u);
    (void)seed_ctrl_track(&database, "/m/2.flac", "Beta", "Album A", 2u);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&database, NULL, 0u, NULL));

    struct playback_engine engine;
    playback_engine_initialize(&engine, &database);
    struct playback_engine_effects effects;
    struct library_db_collection all_ctx = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};
    TEST_ASSERT_TRUE(
        playback_engine_launch_context(&engine, &all_ctx, "All songs", t1, false, false, &effects));

    struct now_playing_controller ctrl;
    now_playing_controller_initialize(&ctrl, &engine, &database, NULL);
    now_playing_controller_enter(&ctrl, NP_RETURN_LIBRARY, NP_OPEN_FROM_ALBUM_CRATE, false, 0.0f,
                                 60u);

    bool vol_changed = false;
    enum now_playing_action_kind action = NP_ACTION_NONE;

    /* 1. Right-bezel tap (< 8px drift) falls through to single-tap -> opens Quick Actions */
    struct touch_event down = {TOUCH_EVENT_DOWN, 335, 180, 100ull};
    struct touch_event up = {TOUCH_EVENT_UP, 336, 181, 130ull};
    now_playing_controller_handle_touch(&ctrl, &down, &effects, &vol_changed, &action);
    now_playing_controller_handle_touch(&ctrl, &up, &effects, &vol_changed, &action);
    TEST_ASSERT_FALSE(vol_changed);
    TEST_ASSERT_TRUE(ctrl.pending_single_tap);
    for (int step = 0; step < 20; step++) {
        now_playing_controller_step(&ctrl, 0.016f, &effects, &vol_changed, &action);
    }
    TEST_ASSERT_TRUE(ctrl.quick_actions_open);

    /* 2. Quick Actions -> tap LYRICS (180, 238) opens Lyrics & closes Quick Actions */
    struct touch_event lyr_down = {TOUCH_EVENT_DOWN, 180, 238, 200ull};
    struct touch_event lyr_up = {TOUCH_EVENT_UP, 180, 238, 220ull};
    now_playing_controller_handle_touch(&ctrl, &lyr_down, &effects, &vol_changed, &action);
    now_playing_controller_handle_touch(&ctrl, &lyr_up, &effects, &vol_changed, &action);
    TEST_ASSERT_TRUE(ctrl.lyrics_open);
    TEST_ASSERT_FALSE(ctrl.quick_actions_open);

    /* 3. Double-tap toggles Play/Pause */
    struct touch_event tap_d = {TOUCH_EVENT_DOWN, 180, 180, 300ull};
    struct touch_event tap_u = {TOUCH_EVENT_UP, 180, 180, 320ull};
    now_playing_controller_handle_touch(&ctrl, &tap_d, &effects, &vol_changed, &action);
    now_playing_controller_handle_touch(&ctrl, &tap_u, &effects, &vol_changed, &action);
    now_playing_controller_handle_touch(&ctrl, &tap_d, &effects, &vol_changed, &action);
    now_playing_controller_handle_touch(&ctrl, &tap_u, &effects, &vol_changed, &action);
    TEST_ASSERT_FALSE(engine.is_playing);
    TEST_ASSERT_TRUE(ctrl.play_pulse_alpha > 0.5f);

    /* 4. Center long-press (0.42s) enters Seek Mode over Lyrics */
    now_playing_controller_handle_touch(&ctrl, &tap_d, &effects, &vol_changed, &action);
    for (int step = 0; step < 30; step++) {
        now_playing_controller_step(&ctrl, 0.016f, &effects, &vol_changed, &action);
    }
    TEST_ASSERT_TRUE(ctrl.is_seeking);
    TEST_ASSERT_TRUE(ctrl.seek_over_lyrics);
    struct touch_event seek_move = {TOUCH_EVENT_MOVE, 240, 180, 400ull};
    struct touch_event seek_up = {TOUCH_EVENT_UP, 240, 180, 420ull};
    now_playing_controller_handle_touch(&ctrl, &seek_move, &effects, &vol_changed, &action);
    now_playing_controller_handle_touch(&ctrl, &seek_up, &effects, &vol_changed, &action);
    TEST_ASSERT_FALSE(ctrl.is_seeking);

    /* 5. 6-level left-edge priority stack */
    ctrl.quick_actions_open = true;
    ctrl.queue_sheet_open = true;
    ctrl.details_sheet_open = true;
    ctrl.playlist_picker_open = true;
    TEST_ASSERT_TRUE(now_playing_controller_step_back(&ctrl, &action));
    TEST_ASSERT_FALSE(ctrl.playlist_picker_open);
    TEST_ASSERT_EQ_I32(NP_ACTION_NONE, action);
    TEST_ASSERT_TRUE(now_playing_controller_step_back(&ctrl, &action));
    TEST_ASSERT_FALSE(ctrl.details_sheet_open);
    TEST_ASSERT_TRUE(now_playing_controller_step_back(&ctrl, &action));
    TEST_ASSERT_FALSE(ctrl.queue_sheet_open);
    TEST_ASSERT_TRUE(now_playing_controller_step_back(&ctrl, &action));
    TEST_ASSERT_FALSE(ctrl.quick_actions_open);
    TEST_ASSERT_TRUE(now_playing_controller_step_back(&ctrl, &action));
    TEST_ASSERT_FALSE(ctrl.lyrics_open);
    TEST_ASSERT_TRUE(now_playing_controller_step_back(&ctrl, &action));
    TEST_ASSERT_TRUE(ctrl.exit_transition_active);
    TEST_ASSERT_EQ_I32(NP_ACTION_NONE, action);
    for (int step = 0; step < 25 && action == NP_ACTION_NONE; step++) {
        now_playing_controller_step(&ctrl, 0.016f, &effects, &vol_changed, &action);
    }
    TEST_ASSERT_FALSE(ctrl.exit_transition_active);
    TEST_ASSERT_EQ_I32(NP_ACTION_EXIT, action);
    TEST_ASSERT_EQ_I32(NP_RETURN_LIBRARY, ctrl.return_target);

    library_db_close(&database);
    cleanup_ctrl_temp_dir(dir_path);
}

static void test_bezel_volume_swipe_and_audio_started_clock_sync(void) {
    char dir_path[128];
    make_ctrl_temp_dir(dir_path, sizeof(dir_path));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, dir_path));

    int64_t t1 = seed_ctrl_track(&database, "/m/1.flac", "Alpha", "Album A", 1u);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&database, NULL, 0u, NULL));

    struct playback_engine engine;
    playback_engine_initialize(&engine, &database);
    struct playback_engine_effects effects;
    struct library_db_collection all_ctx = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};
    TEST_ASSERT_TRUE(
        playback_engine_launch_context(&engine, &all_ctx, "All songs", t1, false, false, &effects));

    struct now_playing_controller ctrl;
    now_playing_controller_initialize(&ctrl, &engine, &database, NULL);
    now_playing_controller_enter(&ctrl, NP_RETURN_HOME, NP_OPEN_FROM_HOME, true, 0.0f, 60u);

    bool vol_changed = false;
    enum now_playing_action_kind action = NP_ACTION_NONE;

    /* Before audio_started, interpolated_elapsed_ms holds at engine->elapsed_ms */
    now_playing_controller_step(&ctrl, 0.05f, &effects, &vol_changed, &action);
    TEST_ASSERT_TRUE(ctrl.interpolated_elapsed_ms == 0.0f);
    engine.audio_started = true;
    now_playing_controller_step(&ctrl, 0.05f, &effects, &vol_changed, &action);
    TEST_ASSERT_TRUE(ctrl.interpolated_elapsed_ms > 40.0f);

    /* Right-bezel swipe inside physical cst816t reach (x=318, y=180) and inner edge (x=295) */
    struct touch_event bez_down = {TOUCH_EVENT_DOWN, 318, 180, 100ull};
    struct touch_event bez_move = {TOUCH_EVENT_MOVE, 310, 130, 130ull};
    struct touch_event bez_up = {TOUCH_EVENT_UP, 310, 130, 160ull};
    now_playing_controller_handle_touch(&ctrl, &bez_down, &effects, &vol_changed, &action);
    now_playing_controller_handle_touch(&ctrl, &bez_move, &effects, &vol_changed, &action);
    TEST_ASSERT_TRUE(vol_changed);
    TEST_ASSERT_TRUE(ctrl.volume_hud_active);
    TEST_ASSERT_TRUE(ctrl.volume_level >= 90u && ctrl.volume_level <= 94u);
    now_playing_controller_handle_touch(&ctrl, &bez_up, &effects, &vol_changed, &action);

    uint8_t prev_vol = ctrl.volume_level;
    struct touch_event bez2_down = {TOUCH_EVENT_DOWN, 295, 180, 200ull};
    struct touch_event bez2_move = {TOUCH_EVENT_MOVE, 292, 230, 230ull};
    struct touch_event bez2_up = {TOUCH_EVENT_UP, 292, 230, 260ull};
    now_playing_controller_handle_touch(&ctrl, &bez2_down, &effects, &vol_changed, &action);
    now_playing_controller_handle_touch(&ctrl, &bez2_move, &effects, &vol_changed, &action);
    TEST_ASSERT_TRUE(vol_changed);
    TEST_ASSERT_TRUE(ctrl.volume_level < prev_vol);
    now_playing_controller_handle_touch(&ctrl, &bez2_up, &effects, &vol_changed, &action);

    /* Anchored elapsed clock does not snap back to 0 if 1.3s passes before first a103,
     * and snaps/tracks accurately when behind by 500ms on a103 */
    engine.has_active_track = true;
    engine.is_playing = true;
    engine.audio_started = true;
    engine.elapsed_ms = 0u;
    engine.duration_ms = 180000u;
    now_playing_controller_enter(&ctrl, NP_RETURN_HOME, NP_OPEN_FROM_HOME, true, 0.0f, 50u);
    for (uint32_t tick = 0u; tick < 26u; tick++) {
        now_playing_controller_step(&ctrl, 0.05f, &effects, &vol_changed, &action);
    }
    TEST_ASSERT_TRUE(ctrl.interpolated_elapsed_ms >= 1250.0f);

    ctrl.interpolated_elapsed_ms = 500.0f;
    now_playing_controller_on_ipc_elapsed(&ctrl, 1000u);
    TEST_ASSERT_TRUE(ctrl.interpolated_elapsed_ms >= 990.0f);
    now_playing_controller_step(&ctrl, 0.05f, &effects, &vol_changed, &action);
    TEST_ASSERT_TRUE(ctrl.interpolated_elapsed_ms >= 1040.0f);

    /* Track Details press-and-hold lyrics badge banner */
    ctrl.details_sheet_open = true;
    ctrl.details_sheet_progress = 1.0f;
    struct touch_event badge_miss = {TOUCH_EVENT_DOWN, 180, 180, 300ull};
    struct touch_event badge_miss_up = {TOUCH_EVENT_UP, 180, 180, 320ull};
    now_playing_controller_handle_touch(&ctrl, &badge_miss, &effects, &vol_changed, &action);
    TEST_ASSERT_FALSE(ctrl.details_lyrics_banner_active);
    now_playing_controller_handle_touch(&ctrl, &badge_miss_up, &effects, &vol_changed, &action);

    struct touch_event badge_down = {TOUCH_EVENT_DOWN, 180, 312, 340ull};
    now_playing_controller_handle_touch(&ctrl, &badge_down, &effects, &vol_changed, &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_DETAILS_LYRICS_BADGE, (uint32_t)ctrl.pointer_mode);
    TEST_ASSERT_TRUE(ctrl.details_lyrics_banner_active);
    for (uint32_t tick = 0u; tick < 5u; tick++) {
        now_playing_controller_step(&ctrl, 0.05f, &effects, &vol_changed, &action);
    }
    TEST_ASSERT_TRUE(ctrl.details_lyrics_banner_alpha > 0.5f);

    struct touch_event badge_move_off = {TOUCH_EVENT_MOVE, 30, 100, 360ull};
    now_playing_controller_handle_touch(&ctrl, &badge_move_off, &effects, &vol_changed, &action);
    TEST_ASSERT_FALSE(ctrl.details_lyrics_banner_active);

    struct touch_event badge_move_back = {TOUCH_EVENT_MOVE, 180, 312, 380ull};
    now_playing_controller_handle_touch(&ctrl, &badge_move_back, &effects, &vol_changed, &action);
    TEST_ASSERT_TRUE(ctrl.details_lyrics_banner_active);

    struct touch_event badge_up = {TOUCH_EVENT_UP, 180, 312, 400ull};
    now_playing_controller_handle_touch(&ctrl, &badge_up, &effects, &vol_changed, &action);
    TEST_ASSERT_FALSE(ctrl.details_lyrics_banner_active);
    for (uint32_t tick = 0u; tick < 8u; tick++) {
        now_playing_controller_step(&ctrl, 0.05f, &effects, &vol_changed, &action);
    }
    TEST_ASSERT_TRUE(ctrl.details_lyrics_banner_alpha < 0.05f);

    ctrl.details_lyrics_banner_active = true;
    TEST_ASSERT_TRUE(now_playing_controller_step_back(&ctrl, &action));
    TEST_ASSERT_FALSE(ctrl.details_sheet_open);
    TEST_ASSERT_FALSE(ctrl.details_lyrics_banner_active);

    ctrl.details_lyrics_banner_active = true;
    memset(&effects, 0, sizeof(effects));
    effects.track_changed = true;
    now_playing_controller_apply_effects(&ctrl, &effects);
    TEST_ASSERT_FALSE(ctrl.details_lyrics_banner_active);

    library_db_close(&database);
    cleanup_ctrl_temp_dir(dir_path);
}

static void assert_hold_does_not_seek(struct now_playing_controller *controller, int32_t point_x,
                                      int32_t point_y, uint64_t timestamp_ms) {
    struct playback_engine_effects effects;
    bool volume_changed = false;
    enum now_playing_action_kind action = NP_ACTION_NONE;
    struct touch_event touch_down = {TOUCH_EVENT_DOWN, point_x, point_y, timestamp_ms};
    now_playing_controller_handle_touch(controller, &touch_down, &effects, &volume_changed,
                                        &action);
    for (int32_t step_index = 0; step_index < 32; step_index++) {
        now_playing_controller_step(controller, 0.016f, &effects, &volume_changed, &action);
    }
    TEST_ASSERT_FALSE(controller->is_seeking);
    struct touch_event touch_up = {TOUCH_EVENT_UP, point_x, point_y, timestamp_ms + 512ull};
    now_playing_controller_handle_touch(controller, &touch_up, &effects, &volume_changed, &action);
}

static void test_polar_touch_zones_disjoint_and_boundaries(void) {
    char temp_directory[128];
    make_ctrl_temp_dir(temp_directory, sizeof(temp_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, temp_directory));

    int64_t first_track_id = seed_ctrl_track(&database, "/m/1.flac", "Alpha", "Album A", 1u);
    (void)seed_ctrl_track(&database, "/m/2.flac", "Beta", "Album A", 2u);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&database, NULL, 0u, NULL));

    struct playback_engine engine;
    playback_engine_initialize(&engine, &database);
    struct playback_engine_effects effects;
    struct library_db_collection all_collection = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};
    TEST_ASSERT_TRUE(playback_engine_launch_context(&engine, &all_collection, "All songs",
                                                  first_track_id, false, false, &effects));

    struct now_playing_controller controller;
    now_playing_controller_initialize(&controller, &engine, &database, NULL);
    now_playing_controller_enter(&controller, NP_RETURN_HOME, NP_OPEN_FROM_HOME, true, 0.0f, 60u);

    bool volume_changed = false;
    enum now_playing_action_kind action = NP_ACTION_NONE;

    /* 1. Nominal: (270, 240) (r=108.2 <= 112) is Seek, not Volume */
    struct touch_event seek_down = {TOUCH_EVENT_DOWN, 270, 240, 100ull};
    now_playing_controller_handle_touch(&controller, &seek_down, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_CENTER_TAP_OR_HOLD, (uint32_t)controller.pointer_mode);
    for (int32_t step_index = 0; step_index < 30; step_index++) {
        now_playing_controller_step(&controller, 0.016f, &effects, &volume_changed, &action);
    }
    TEST_ASSERT_TRUE(controller.is_seeking);
    struct touch_event seek_up = {TOUCH_EVENT_UP, 270, 240, 600ull};
    now_playing_controller_handle_touch(&controller, &seek_up, &effects, &volume_changed, &action);
    TEST_ASSERT_FALSE(controller.is_seeking);

    /* 1. Nominal: (30, 180) is Back, dragging right enters NP_PTR_BACK_SWIPE */
    struct touch_event back_down = {TOUCH_EVENT_DOWN, 30, 180, 700ull};
    struct touch_event back_move = {TOUCH_EVENT_MOVE, 60, 180, 720ull};
    struct touch_event back_up = {TOUCH_EVENT_UP, 35, 180, 740ull};
    now_playing_controller_handle_touch(&controller, &back_down, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_LEFT_EDGE_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &back_move, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_BACK_SWIPE, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &back_up, &effects, &volume_changed, &action);
    controller.back_swipe_progress = 0.0f;

    /* 1. Nominal: (320, 180) is Volume */
    struct touch_event volume_down = {TOUCH_EVENT_DOWN, 320, 180, 800ull};
    struct touch_event volume_move = {TOUCH_EVENT_MOVE, 315, 140, 830ull};
    struct touch_event volume_up = {TOUCH_EVENT_UP, 315, 140, 860ull};
    now_playing_controller_handle_touch(&controller, &volume_down, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_VOLUME_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &volume_move, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_TRUE(volume_changed);
    TEST_ASSERT_TRUE(controller.volume_hud_active);
    now_playing_controller_handle_touch(&controller, &volume_up, &effects, &volume_changed,
                                        &action);

    /* 1. Nominal: (180, 320) is Queue, upward swipe opens queue_sheet_open */
    struct touch_event queue_down = {TOUCH_EVENT_DOWN, 180, 320, 900ull};
    struct touch_event queue_move = {TOUCH_EVENT_MOVE, 180, 270, 930ull};
    struct touch_event queue_up = {TOUCH_EVENT_UP, 180, 270, 960ull};
    now_playing_controller_handle_touch(&controller, &queue_down, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &queue_move, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &queue_up, &effects, &volume_changed, &action);
    TEST_ASSERT_TRUE(controller.queue_sheet_open);
    controller.queue_sheet_open = false;
    controller.queue_sheet_progress = 0.0f;

    /* 1. Nominal: (180, 30) is reserved Top Menu (does not seek on hold) */
    struct touch_event menu_down = {TOUCH_EVENT_DOWN, 180, 30, 1000ull};
    now_playing_controller_handle_touch(&controller, &menu_down, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_TOP_MENU_PENDING, (uint32_t)controller.pointer_mode);
    struct touch_event menu_up = {TOUCH_EVENT_UP, 180, 30, 1020ull};
    now_playing_controller_handle_touch(&controller, &menu_up, &effects, &volume_changed, &action);
    controller.pending_single_tap = false;

    /* 2. Boundary: Axis r=112 vs r=113 */
    struct touch_event left_inner = {TOUCH_EVENT_DOWN, 68, 180, 1100ull};
    struct touch_event release_reset = {TOUCH_EVENT_UP, 68, 180, 1110ull};
    now_playing_controller_handle_touch(&controller, &left_inner, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_CENTER_TAP_OR_HOLD, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event left_outer = {TOUCH_EVENT_DOWN, 67, 180, 1120ull};
    now_playing_controller_handle_touch(&controller, &left_outer, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_LEFT_EDGE_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event right_inner = {TOUCH_EVENT_DOWN, 292, 180, 1140ull};
    now_playing_controller_handle_touch(&controller, &right_inner, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_CENTER_TAP_OR_HOLD, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event right_outer = {TOUCH_EVENT_DOWN, 293, 180, 1160ull};
    now_playing_controller_handle_touch(&controller, &right_outer, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_VOLUME_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event bottom_inner = {TOUCH_EVENT_DOWN, 180, 292, 1180ull};
    now_playing_controller_handle_touch(&controller, &bottom_inner, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_CENTER_TAP_OR_HOLD, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event bottom_outer_down = {TOUCH_EVENT_DOWN, 180, 293, 1200ull};
    struct touch_event bottom_outer_move = {TOUCH_EVENT_MOVE, 180, 245, 1220ull};
    struct touch_event bottom_outer_up = {TOUCH_EVENT_UP, 180, 245, 1240ull};
    now_playing_controller_handle_touch(&controller, &bottom_outer_down, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_QUEUE_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &bottom_outer_move, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &bottom_outer_up, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_TRUE(controller.queue_sheet_open);
    controller.queue_sheet_open = false;
    controller.queue_sheet_progress = 0.0f;

    struct touch_event top_inner = {TOUCH_EVENT_DOWN, 180, 68, 1260ull};
    now_playing_controller_handle_touch(&controller, &top_inner, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_CENTER_TAP_OR_HOLD, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event top_outer = {TOUCH_EVENT_DOWN, 180, 67, 1280ull};
    now_playing_controller_handle_touch(&controller, &top_outer, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_TOP_MENU_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    /* 2. Boundary: Diagonal 45-degree seams at r > 112 */
    struct touch_event seam_bl_left = {TOUCH_EVENT_DOWN, 80, 280, 1300ull};
    now_playing_controller_handle_touch(&controller, &seam_bl_left, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_LEFT_EDGE_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event seam_bl_queue = {TOUCH_EVENT_DOWN, 81, 280, 1320ull};
    now_playing_controller_handle_touch(&controller, &seam_bl_queue, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_QUEUE_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event seam_br_vol = {TOUCH_EVENT_DOWN, 280, 280, 1340ull};
    now_playing_controller_handle_touch(&controller, &seam_br_vol, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_VOLUME_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event seam_br_queue = {TOUCH_EVENT_DOWN, 279, 280, 1360ull};
    now_playing_controller_handle_touch(&controller, &seam_br_queue, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_QUEUE_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event seam_tl_left = {TOUCH_EVENT_DOWN, 80, 80, 1380ull};
    now_playing_controller_handle_touch(&controller, &seam_tl_left, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_LEFT_EDGE_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event seam_tl_menu = {TOUCH_EVENT_DOWN, 81, 80, 1400ull};
    now_playing_controller_handle_touch(&controller, &seam_tl_menu, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_TOP_MENU_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event seam_tr_vol = {TOUCH_EVENT_DOWN, 280, 80, 1420ull};
    now_playing_controller_handle_touch(&controller, &seam_tr_vol, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_VOLUME_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    struct touch_event seam_tr_menu = {TOUCH_EVENT_DOWN, 279, 80, 1440ull};
    now_playing_controller_handle_touch(&controller, &seam_tr_menu, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_EQ_U32((uint32_t)NP_PTR_TOP_MENU_PENDING, (uint32_t)controller.pointer_mode);
    now_playing_controller_handle_touch(&controller, &release_reset, &effects, &volume_changed,
                                        &action);
    controller.pending_single_tap = false;

    /* 2. Boundary: Zero overlap between Seek (r<=112) and Queue */
    struct touch_event inner_swipe_down = {TOUCH_EVENT_DOWN, 180, 260, 1500ull};
    struct touch_event inner_swipe_move = {TOUCH_EVENT_MOVE, 180, 210, 1530ull};
    struct touch_event inner_swipe_up = {TOUCH_EVENT_UP, 180, 210, 1560ull};
    now_playing_controller_handle_touch(&controller, &inner_swipe_down, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &inner_swipe_move, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &inner_swipe_up, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_FALSE(controller.queue_sheet_open);

    /* 3. Fallback / Negative: Holding 0.50s in outer zones never activates Seek */
    assert_hold_does_not_seek(&controller, 30, 180, 1600ull);
    controller.pending_single_tap = false;
    assert_hold_does_not_seek(&controller, 330, 180, 2200ull);
    controller.pending_single_tap = false;
    assert_hold_does_not_seek(&controller, 180, 320, 2800ull);
    controller.pending_single_tap = false;
    assert_hold_does_not_seek(&controller, 180, 30, 3400ull);
    controller.pending_single_tap = false;

    /* 3. Fallback: Single tap in Queue (180, 320) and Top Menu (180, 30) opens Quick Actions */
    struct touch_event queue_tap_down = {TOUCH_EVENT_DOWN, 180, 320, 4000ull};
    struct touch_event queue_tap_up = {TOUCH_EVENT_UP, 180, 320, 4020ull};
    now_playing_controller_handle_touch(&controller, &queue_tap_down, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &queue_tap_up, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_TRUE(controller.pending_single_tap);
    for (int32_t step_index = 0; step_index < 20; step_index++) {
        now_playing_controller_step(&controller, 0.016f, &effects, &volume_changed, &action);
    }
    TEST_ASSERT_TRUE(controller.quick_actions_open);

    /* 3. Fallback: Over Quick Actions, swipe up from r<=112 (180,260)->(180,210) does NOT open Queue */
    struct touch_event qa_inner_down = {TOUCH_EVENT_DOWN, 180, 260, 4400ull};
    struct touch_event qa_inner_move = {TOUCH_EVENT_MOVE, 180, 210, 4430ull};
    struct touch_event qa_inner_up = {TOUCH_EVENT_UP, 180, 210, 4460ull};
    now_playing_controller_handle_touch(&controller, &qa_inner_down, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &qa_inner_move, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &qa_inner_up, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_FALSE(controller.queue_sheet_open);

    /* Over Quick Actions, swipe up from Queue arc (180,320)->(180,265) opens Queue */
    struct touch_event qa_queue_down = {TOUCH_EVENT_DOWN, 180, 320, 4500ull};
    struct touch_event qa_queue_move = {TOUCH_EVENT_MOVE, 180, 265, 4530ull};
    struct touch_event qa_queue_up = {TOUCH_EVENT_UP, 180, 265, 4560ull};
    now_playing_controller_handle_touch(&controller, &qa_queue_down, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &qa_queue_move, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &qa_queue_up, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_TRUE(controller.queue_sheet_open);
    controller.queue_sheet_open = false;
    controller.quick_actions_open = false;

    /* Single tap in Top Menu (180, 30) opens Quick Actions */
    struct touch_event menu_tap_down = {TOUCH_EVENT_DOWN, 180, 30, 4700ull};
    struct touch_event menu_tap_up = {TOUCH_EVENT_UP, 180, 30, 4720ull};
    now_playing_controller_handle_touch(&controller, &menu_tap_down, &effects, &volume_changed,
                                        &action);
    now_playing_controller_handle_touch(&controller, &menu_tap_up, &effects, &volume_changed,
                                        &action);
    TEST_ASSERT_TRUE(controller.pending_single_tap);
    for (int32_t step_index = 0; step_index < 20; step_index++) {
        now_playing_controller_step(&controller, 0.016f, &effects, &volume_changed, &action);
    }
    TEST_ASSERT_TRUE(controller.quick_actions_open);

    library_db_close(&database);
    cleanup_ctrl_temp_dir(temp_directory);
}

void run_now_playing_controller_tests(void) {
    printf("Suite: now_playing_controller\n");
    RUN_TEST(test_tap_seek_volume_and_6_level_back_stack);
    RUN_TEST(test_bezel_volume_swipe_and_audio_started_clock_sync);
    RUN_TEST(test_polar_touch_zones_disjoint_and_boundaries);
}
