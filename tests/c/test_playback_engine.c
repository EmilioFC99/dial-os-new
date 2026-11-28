#include "test_runner.h"

#include "playback_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void make_temp_dir(char *path_out, size_t capacity) {
    snprintf(path_out, capacity, "/tmp/dialos_pe_XXXXXX");
    TEST_ASSERT_TRUE(mkdtemp(path_out) != NULL);
}

static void cleanup_temp_dir(const char *directory_path) {
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/library.db", directory_path);
    unlink(file_path);
    snprintf(file_path, sizeof(file_path), "%s/library.db-journal", directory_path);
    unlink(file_path);
    rmdir(directory_path);
}

static int64_t add_test_track(struct library_db *database, const char *path, const char *title,
                              const char *artist, const char *album, uint16_t track_num,
                              uint32_t duration_s) {
    struct library_db_track_input input = {0};
    input.path = path;
    input.audio_fingerprint = library_fnv1a_64(path);
    input.file_size_bytes = 1024u;
    input.mtime_seconds = 1700000000;
    input.format_kind = LIBRARY_FORMAT_FLAC;
    input.title = title;
    input.artist = artist;
    input.album_artist = artist;
    input.album = album;
    input.genre = "Electronic";
    input.year = 2024u;
    input.disc_number = 1u;
    input.track_number = track_num;
    input.duration_seconds = duration_s;
    input.added_at_seconds = 1700000000 + (int64_t)track_num;
    int64_t track_id = 0;
    if (!library_db_upsert_track(database, &input, 0, &track_id)) {
        return 0;
    }
    return track_id;
}

static void test_launch_3s_rule_and_repeat_matrix(void) {
    char dir_path[128];
    make_temp_dir(dir_path, sizeof(dir_path));
    struct library_db db;
    TEST_ASSERT_TRUE(library_db_open(&db, dir_path));

    int64_t t1 = add_test_track(&db, "/m/a/1.flac", "Alpha", "Artist A", "Album A", 1u, 120u);
    int64_t t2 = add_test_track(&db, "/m/a/2.flac", "Beta", "Artist A", "Album A", 2u, 150u);
    int64_t t3 = add_test_track(&db, "/m/b/3.flac", "Gamma", "Artist B", "Album B", 1u, 180u);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&db, NULL, 0u, NULL));

    struct playback_engine engine;
    playback_engine_initialize(&engine, &db);
    struct playback_engine_effects effects;
    struct library_db_collection all_ctx = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};

    TEST_ASSERT_TRUE(
        playback_engine_launch_context(&engine, &all_ctx, "All songs", t1, false, false, &effects));
    TEST_ASSERT_TRUE(effects.launch_slice);
    TEST_ASSERT_TRUE(effects.vinyl_nudge_radians == 0.0f);
    TEST_ASSERT_EQ_U32(0u, effects.slice_start_slot);
    TEST_ASSERT_EQ_I32(t1, engine.active_track.track_id);

    /* Confirm track loaded via a202 */
    struct playback_status_update st = {0};
    st.has_state = true;
    st.state = IPC_PLAYBACK_PLAYING;
    st.has_track_path = true;
    snprintf(st.track_path, sizeof(st.track_path), "%s", "/m/a/1.flac");
    playback_engine_handle_ipc_status(&engine, &st, &effects);
    TEST_ASSERT_FALSE(engine.awaiting_slice_load);

    /* 3-second Previous rule: >3000ms restarts at 0 without changing track */
    playback_engine_handle_ipc_elapsed(&engine, 3500u);
    TEST_ASSERT_TRUE(playback_engine_skip(&engine, -1, &effects));
    TEST_ASSERT_TRUE(effects.send_seek);
    TEST_ASSERT_EQ_U32(0u, effects.seek_ms);
    TEST_ASSERT_TRUE(effects.vinyl_nudge_radians == 0.0f);
    TEST_ASSERT_EQ_I32(t1, engine.active_track.track_id);

    /* <=3000ms at slot 0 under Repeat OFF bounces with START OF ALL SONGS */
    playback_engine_handle_ipc_elapsed(&engine, 2500u);
    TEST_ASSERT_TRUE(playback_engine_skip(&engine, -1, &effects));
    TEST_ASSERT_TRUE(effects.boundary_bounce);
    TEST_ASSERT_TRUE(effects.vinyl_nudge_radians == 0.0f);
    TEST_ASSERT_EQ_STR("START OF ALL SONGS", effects.toast_text);

    /* Re-selecting already loaded track preserves elapsed time */
    playback_engine_handle_ipc_elapsed(&engine, 42000u);
    TEST_ASSERT_TRUE(
        playback_engine_launch_context(&engine, &all_ctx, "All songs", t1, false, false, &effects));
    TEST_ASSERT_FALSE(effects.launch_slice);
    TEST_ASSERT_EQ_U32(42000u, engine.elapsed_ms);

    /* Next to t2 (same album -> banner, no twist) then t3 (different album -> cross-album slide) */
    TEST_ASSERT_TRUE(playback_engine_skip(&engine, 1, &effects));
    TEST_ASSERT_TRUE(effects.show_same_album_banner);
    TEST_ASSERT_TRUE(effects.vinyl_nudge_radians == 0.0f);
    TEST_ASSERT_EQ_I32(t2, engine.active_track.track_id);

    struct playback_skip_preview preview;
    playback_engine_peek_skip(&engine, 1, &preview);
    TEST_ASSERT_EQ_I32(PLAYBACK_SKIP_TRACK, preview.action);
    TEST_ASSERT_FALSE(preview.same_album);
    TEST_ASSERT_EQ_I32(t3, preview.target_track.track_id);

    TEST_ASSERT_TRUE(playback_engine_skip(&engine, 1, &effects));
    TEST_ASSERT_TRUE(effects.start_cross_album_slide);
    TEST_ASSERT_TRUE(effects.vinyl_nudge_radians != 0.0f);
    TEST_ASSERT_EQ_I32(t3, engine.active_track.track_id);

    /* Repeat ONE allows manual skip (bounces at end) while looping on natural EOF */
    TEST_ASSERT_TRUE(playback_engine_cycle_repeat(&engine, &effects)); /* ALL */
    TEST_ASSERT_EQ_STR("REPEAT // ALL", effects.toast_text);
    TEST_ASSERT_TRUE(playback_engine_cycle_repeat(&engine, &effects)); /* ONE */
    TEST_ASSERT_EQ_STR("REPEAT // ONE", effects.toast_text);
    TEST_ASSERT_FALSE(effects.send_play_mode);
    TEST_ASSERT_TRUE(playback_engine_skip(&engine, 1, &effects));
    TEST_ASSERT_TRUE(effects.boundary_bounce);
    TEST_ASSERT_TRUE(effects.vinyl_nudge_radians == 0.0f);
    TEST_ASSERT_EQ_STR("END OF ALL SONGS", effects.toast_text);

    /* Repeat OFF natural EOF at last track resets to slot 0 paused */
    TEST_ASSERT_TRUE(playback_engine_cycle_repeat(&engine, &effects)); /* OFF */
    playback_engine_handle_ipc_status(&engine, &(struct playback_status_update){
        .has_state = true, .state = IPC_PLAYBACK_PLAYING,
        .has_track_path = true, .track_path = "/m/b/3.flac"
    }, &effects);
    playback_engine_handle_ipc_status(&engine, &(struct playback_status_update){
        .has_state = true, .state = IPC_PLAYBACK_STOPPED
    }, &effects);
    TEST_ASSERT_FALSE(engine.is_playing);
    TEST_ASSERT_EQ_U32(0u, engine.slice_count);
    TEST_ASSERT_EQ_U32(0u, engine.current_slot);
    TEST_ASSERT_EQ_STR("END OF ALL SONGS", effects.toast_text);

    /* Stale playing IPC after end-of-queue stop must not restart vinyl */
    playback_engine_handle_ipc_status(&engine, &(struct playback_status_update){
        .has_state = true, .state = IPC_PLAYBACK_PLAYING
    }, &effects);
    TEST_ASSERT_FALSE(engine.is_playing);

    /* Stale a103 elapsed (>1.5s) is ignored while !audio_started, <=1.5s accepted */
    engine.audio_started = false;
    engine.has_pending_resume_seek = false;
    engine.elapsed_ms = 0u;
    TEST_ASSERT_FALSE(playback_engine_handle_ipc_elapsed(&engine, 3000u));
    TEST_ASSERT_EQ_U32(0u, engine.elapsed_ms);
    TEST_ASSERT_TRUE(playback_engine_handle_ipc_elapsed(&engine, 1000u));
    TEST_ASSERT_EQ_U32(1000u, engine.elapsed_ms);

    /* Gapless auto-advance with is_loading_state=true resets audio_started=false */
    TEST_ASSERT_TRUE(playback_engine_toggle_play_pause(&engine, &effects));
    playback_engine_handle_ipc_status(&engine, &(struct playback_status_update){
        .has_state = true, .state = IPC_PLAYBACK_PLAYING,
        .is_loading_state = false,
        .has_track_path = true, .track_path = "/m/a/1.flac"
    }, &effects);
    TEST_ASSERT_TRUE(engine.audio_started);
    playback_engine_handle_ipc_status(&engine, &(struct playback_status_update){
        .has_state = true, .state = IPC_PLAYBACK_PLAYING,
        .is_loading_state = true,
        .has_track_path = true, .track_path = "/m/a/2.flac"
    }, &effects);
    TEST_ASSERT_FALSE(engine.audio_started);

    library_db_close(&db);
    cleanup_temp_dir(dir_path);
}

static void test_gapless_slice_advance_ephemeral_and_reboot_resume(void) {
    char dir_path[128];
    make_temp_dir(dir_path, sizeof(dir_path));
    struct library_db db;
    TEST_ASSERT_TRUE(library_db_open(&db, dir_path));

    int64_t t1 = add_test_track(&db, "/m/a/1.flac", "One", "Artist A", "Album A", 1u, 120u);
    int64_t t2 = add_test_track(&db, "/m/a/2.flac", "Two", "Artist A", "Album A", 2u, 150u);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&db, NULL, 0u, NULL));

    /* Favorite both and launch FAVORITES */
    bool fav = false;
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&db, LIBRARY_DB_FAVORITE_TRACK, t1, &fav));
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&db, LIBRARY_DB_FAVORITE_TRACK, t2, &fav));

    struct playback_engine engine;
    playback_engine_initialize(&engine, &db);
    struct playback_engine_effects effects;
    struct library_db_collection fav_ctx = {LIBRARY_DB_COLLECTION_TRACKS_FAVORITES, 0};
    TEST_ASSERT_TRUE(
        playback_engine_launch_context(&engine, &fav_ctx, "Favorites", t1, false, false, &effects));

    /* Unfavoriting active track marks it ephemeral; finishing track drops it and advances to t2 */
    TEST_ASSERT_TRUE(playback_engine_toggle_active_favorite(&engine, &fav, &effects));
    TEST_ASSERT_FALSE(fav);
    TEST_ASSERT_TRUE(engine.ephemeral_active);
    TEST_ASSERT_EQ_STR("REMOVED FROM FAVORITES", effects.toast_text);

    playback_engine_handle_ipc_status(&engine, &(struct playback_status_update){
        .has_state = true, .state = IPC_PLAYBACK_PLAYING,
        .has_track_path = true, .track_path = "/m/a/1.flac"
    }, &effects);
    playback_engine_handle_ipc_status(&engine, &(struct playback_status_update){
        .has_state = true, .state = IPC_PLAYBACK_STOPPED
    }, &effects);
    TEST_ASSERT_FALSE(engine.ephemeral_active);
    TEST_ASSERT_EQ_I32(t2, engine.active_track.track_id);
    TEST_ASSERT_EQ_U32(1u, engine.queue_total);

    /* Confirm t2 loaded, then save & restore across reboot */
    playback_engine_handle_ipc_status(&engine, &(struct playback_status_update){
        .has_state = true, .state = IPC_PLAYBACK_PLAYING,
        .has_track_path = true, .track_path = "/m/a/2.flac"
    }, &effects);
    playback_engine_handle_ipc_elapsed(&engine, 55000u);
    playback_engine_save_to_db(&engine, 88u);

    struct playback_engine restored;
    playback_engine_initialize(&restored, &db);
    uint8_t vol = 0u;
    bool has_vol = false;
    TEST_ASSERT_TRUE(playback_engine_restore_from_db(&restored, &vol, &has_vol));
    TEST_ASSERT_TRUE(has_vol);
    TEST_ASSERT_EQ_U32(88u, vol);
    TEST_ASSERT_TRUE(restored.has_active_track);
    TEST_ASSERT_FALSE(restored.is_playing);
    TEST_ASSERT_EQ_U32(55000u, restored.elapsed_ms);

    TEST_ASSERT_TRUE(playback_engine_toggle_play_pause(&restored, &effects));
    TEST_ASSERT_TRUE(effects.launch_slice);
    TEST_ASSERT_TRUE(effects.vinyl_nudge_radians == 0.0f);
    playback_engine_handle_ipc_status(&restored, &(struct playback_status_update){
        .has_state = true, .state = IPC_PLAYBACK_PLAYING,
        .has_track_path = true, .track_path = "/m/a/2.flac"
    }, &effects);
    TEST_ASSERT_TRUE(effects.send_seek);
    TEST_ASSERT_EQ_U32(55000u, effects.seek_ms);

    library_db_close(&db);
    cleanup_temp_dir(dir_path);
}

void run_playback_engine_tests(void) {
    printf("Suite: playback_engine\n");
    RUN_TEST(test_launch_3s_rule_and_repeat_matrix);
    RUN_TEST(test_gapless_slice_advance_ephemeral_and_reboot_resume);
}
