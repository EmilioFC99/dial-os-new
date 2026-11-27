#include "test_runner.h"

#include "library_db.h"
#include "library_scanner.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static void make_scan_work_directory(char *path_out, size_t capacity) {
    snprintf(path_out, capacity, "/tmp/dialos_scandb_XXXXXX");
    TEST_ASSERT_TRUE(mkdtemp(path_out) != NULL);
}

static void remove_scan_tree(const char *path) {
    char command[512];
    snprintf(command, sizeof(command), "rm -rf \"%s\"", path);
    (void)system(command);
}

static void make_directory_recursive(const char *path) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s", path);
    for (size_t index = 1u; buffer[index] != '\0'; index++) {
        if (buffer[index] == '/') {
            buffer[index] = '\0';
            (void)mkdir(buffer, 0755);
            buffer[index] = '/';
        }
    }
    (void)mkdir(buffer, 0755);
}

static void write_flac_with_audio_seed(const char *path, const char *title, const char *artist,
                                       const char *album, uint32_t padding_bytes,
                                       uint8_t audio_seed, size_t audio_bytes) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_TRUE(file != NULL);

    uint8_t streaminfo[4 + 4 + 34];
    memset(streaminfo, 0, sizeof(streaminfo));
    memcpy(streaminfo, "fLaC", 4u);
    streaminfo[4] = 0x00u;
    streaminfo[7] = 34u;
    uint32_t sample_rate = 44100u;
    streaminfo[8 + 10] = (uint8_t)(sample_rate >> 12u);
    streaminfo[8 + 11] = (uint8_t)(sample_rate >> 4u);
    streaminfo[8 + 12] = (uint8_t)(((sample_rate & 0x0Fu) << 4u) | (1u << 1u));
    streaminfo[8 + 13] = 0xF0u;
    streaminfo[8 + 15] = 0x06u;
    streaminfo[8 + 16] = 0xBAu;
    streaminfo[8 + 17] = 0xA8u;
    fwrite(streaminfo, 1u, sizeof(streaminfo), file);

    char c0[128], c1[128], c2[128];
    snprintf(c0, sizeof(c0), "TITLE=%s", title);
    snprintf(c1, sizeof(c1), "ARTIST=%s", artist);
    snprintf(c2, sizeof(c2), "ALBUM=%s", album);
    const char *comments[3] = {c0, c1, c2};

    uint8_t vc_buf[512];
    size_t pos = 0u;
    vc_buf[pos++] = 4u; vc_buf[pos++] = 0u; vc_buf[pos++] = 0u; vc_buf[pos++] = 0u;
    memcpy(vc_buf + pos, "TEST", 4u);
    pos += 4u;
    vc_buf[pos++] = 3u; vc_buf[pos++] = 0u; vc_buf[pos++] = 0u; vc_buf[pos++] = 0u;
    for (uint32_t index = 0u; index < 3u; index++) {
        uint32_t len = (uint32_t)strlen(comments[index]);
        vc_buf[pos++] = (uint8_t)(len & 0xFFu);
        vc_buf[pos++] = (uint8_t)((len >> 8u) & 0xFFu);
        vc_buf[pos++] = (uint8_t)((len >> 16u) & 0xFFu);
        vc_buf[pos++] = (uint8_t)((len >> 24u) & 0xFFu);
        memcpy(vc_buf + pos, comments[index], len);
        pos += len;
    }

    uint8_t vc_flag = (padding_bytes > 0u) ? 0x04u : 0x84u;
    uint8_t vc_hdr[4] = {vc_flag, (uint8_t)((pos >> 16u) & 0xFFu),
                         (uint8_t)((pos >> 8u) & 0xFFu), (uint8_t)(pos & 0xFFu)};
    fwrite(vc_hdr, 1u, 4u, file);
    fwrite(vc_buf, 1u, pos, file);

    if (padding_bytes > 0u) {
        uint8_t pad_hdr[4] = {0x81u, (uint8_t)((padding_bytes >> 16u) & 0xFFu),
                              (uint8_t)((padding_bytes >> 8u) & 0xFFu),
                              (uint8_t)(padding_bytes & 0xFFu)};
        fwrite(pad_hdr, 1u, 4u, file);
        for (uint32_t index = 0u; index < padding_bytes; index++) {
            fputc(0, file);
        }
    }

    for (size_t index = 0u; index < audio_bytes; index++) {
        fputc((int)((audio_seed + (uint8_t)(index * 13u)) & 0xFFu), file);
    }
    fclose(file);
}

static void write_mp3_with_audio_seed(const char *path, const char *title, const char *artist,
                                      const char *album, uint32_t extra_id3_padding,
                                      const char *id3v1_tag, uint8_t audio_seed,
                                      size_t audio_bytes) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_TRUE(file != NULL);

    uint8_t payload[512];
    size_t pos = 0u;
    const char *ids[3] = {"TIT2", "TPE1", "TALB"};
    const char *vals[3] = {title, artist, album};
    for (uint32_t index = 0u; index < 3u; index++) {
        uint32_t len = (uint32_t)strlen(vals[index]) + 1u;
        memcpy(payload + pos, ids[index], 4u);
        pos += 4u;
        payload[pos++] = (uint8_t)((len >> 24u) & 0xFFu);
        payload[pos++] = (uint8_t)((len >> 16u) & 0xFFu);
        payload[pos++] = (uint8_t)((len >> 8u) & 0xFFu);
        payload[pos++] = (uint8_t)(len & 0xFFu);
        payload[pos++] = 0u;
        payload[pos++] = 0u;
        payload[pos++] = 3u;
        memcpy(payload + pos, vals[index], strlen(vals[index]));
        pos += strlen(vals[index]);
    }
    uint32_t total_tag_size = (uint32_t)pos + extra_id3_padding;
    uint8_t hdr[10] = {'I', 'D', '3', 3u, 0u, 0u,
                       (uint8_t)((total_tag_size >> 21u) & 0x7Fu),
                       (uint8_t)((total_tag_size >> 14u) & 0x7Fu),
                       (uint8_t)((total_tag_size >> 7u) & 0x7Fu),
                       (uint8_t)(total_tag_size & 0x7Fu)};
    fwrite(hdr, 1u, 10u, file);
    fwrite(payload, 1u, pos, file);
    for (uint32_t index = 0u; index < extra_id3_padding; index++) {
        fputc(0, file);
    }

    uint8_t mpeg_frame[4] = {0xFFu, 0xFBu, 0x90u, 0x00u};
    fwrite(mpeg_frame, 1u, sizeof(mpeg_frame), file);
    for (size_t index = sizeof(mpeg_frame); index < audio_bytes; index++) {
        fputc((int)((audio_seed + (uint8_t)(index * 7u)) & 0xFFu), file);
    }

    if (id3v1_tag != NULL) {
        uint8_t v1[128];
        memset(v1, 0, sizeof(v1));
        memcpy(v1, "TAG", 3u);
        snprintf((char *)(v1 + 3u), 30u, "%s", id3v1_tag);
        fwrite(v1, 1u, sizeof(v1), file);
    }
    fclose(file);
}

static void write_wav_with_audio_seed(const char *path, uint8_t audio_seed, uint32_t data_bytes) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_TRUE(file != NULL);
    uint32_t riff_size = 36u + data_bytes;
    uint8_t hdr[44] = {
        'R', 'I', 'F', 'F',
        (uint8_t)(riff_size & 0xFFu), (uint8_t)((riff_size >> 8u) & 0xFFu),
        (uint8_t)((riff_size >> 16u) & 0xFFu), (uint8_t)((riff_size >> 24u) & 0xFFu),
        'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16u, 0u, 0u, 0u,
        1u, 0u, 1u, 0u, 0x44u, 0xACu, 0u, 0u, 0x88u, 0x58u, 0x01u, 0u, 2u, 0u, 16u, 0u,
        'd', 'a', 't', 'a',
        (uint8_t)(data_bytes & 0xFFu), (uint8_t)((data_bytes >> 8u) & 0xFFu),
        (uint8_t)((data_bytes >> 16u) & 0xFFu), (uint8_t)((data_bytes >> 24u) & 0xFFu),
    };
    fwrite(hdr, 1u, 44u, file);
    for (uint32_t index = 0u; index < data_bytes; index++) {
        fputc((int)((audio_seed + (uint8_t)(index * 11u)) & 0xFFu), file);
    }
    fclose(file);
}

static void run_sync_to_completion(struct library_db *database, const char *music_root,
                                   const char *covers_directory,
                                   struct library_db_scan_snapshot *snapshot_out) {
    struct library_db_scanner scanner;
    library_db_scanner_initialize(&scanner);
    TEST_ASSERT_TRUE(library_db_scanner_start(&scanner, database, music_root, covers_directory));
    TEST_ASSERT_TRUE(library_db_scanner_wait(&scanner));
    library_db_scanner_poll(&scanner, snapshot_out);
    TEST_ASSERT_TRUE(snapshot_out->ready);
    library_db_scanner_destroy(&scanner);
}

static void test_audio_payload_fingerprint_ignores_tag_and_padding_edits(void) {
    char work_dir[128];
    make_scan_work_directory(work_dir, sizeof(work_dir));

    char flac_a[256], flac_b[256];
    snprintf(flac_a, sizeof(flac_a), "%s/a.flac", work_dir);
    snprintf(flac_b, sizeof(flac_b), "%s/b.flac", work_dir);
    write_flac_with_audio_seed(flac_a, "Original Title", "Artist", "Album", 0u, 0x3Au, 16384u);
    write_flac_with_audio_seed(flac_b, "Completely Different Title", "Other", "Other", 4096u, 0x3Au,
                               16384u);

    char scratch_a[2048], scratch_b[2048];
    struct library_track_input in_a, in_b;
    TEST_ASSERT_TRUE(
        library_parse_audio_file_metadata(flac_a, work_dir, &in_a, scratch_a, sizeof(scratch_a)));
    TEST_ASSERT_TRUE(
        library_parse_audio_file_metadata(flac_b, work_dir, &in_b, scratch_b, sizeof(scratch_b)));
    uint64_t fp_a = 0u, fp_b = 0u;
    TEST_ASSERT_TRUE(library_compute_audio_fingerprint(flac_a, in_a.audio_start_offset,
                                                       in_a.audio_payload_bytes, &fp_a));
    TEST_ASSERT_TRUE(library_compute_audio_fingerprint(flac_b, in_b.audio_start_offset,
                                                       in_b.audio_payload_bytes, &fp_b));
    TEST_ASSERT_EQ_U64(fp_a, fp_b);

    char mp3_a[256], mp3_b[256];
    snprintf(mp3_a, sizeof(mp3_a), "%s/a.mp3", work_dir);
    snprintf(mp3_b, sizeof(mp3_b), "%s/b.mp3", work_dir);
    write_mp3_with_audio_seed(mp3_a, "MP3 One", "Artist", "Album", 0u, "OldV1", 0x55u, 12000u);
    write_mp3_with_audio_seed(mp3_b, "MP3 Edited", "Artist", "Album", 2048u, "NewV1", 0x55u, 12000u);
    TEST_ASSERT_TRUE(
        library_parse_audio_file_metadata(mp3_a, work_dir, &in_a, scratch_a, sizeof(scratch_a)));
    TEST_ASSERT_TRUE(
        library_parse_audio_file_metadata(mp3_b, work_dir, &in_b, scratch_b, sizeof(scratch_b)));
    TEST_ASSERT_TRUE(library_compute_audio_fingerprint(mp3_a, in_a.audio_start_offset,
                                                       in_a.audio_payload_bytes, &fp_a));
    TEST_ASSERT_TRUE(library_compute_audio_fingerprint(mp3_b, in_b.audio_start_offset,
                                                       in_b.audio_payload_bytes, &fp_b));
    TEST_ASSERT_EQ_U64(fp_a, fp_b);

    /* Tiny WAV (< 8 KB) hashes cleanly and differs when audio payload differs. */
    char wav_a[256], wav_b[256];
    snprintf(wav_a, sizeof(wav_a), "%s/tiny_a.wav", work_dir);
    snprintf(wav_b, sizeof(wav_b), "%s/tiny_b.wav", work_dir);
    write_wav_with_audio_seed(wav_a, 0x11u, 1024u);
    write_wav_with_audio_seed(wav_b, 0x22u, 1024u);
    TEST_ASSERT_TRUE(
        library_parse_audio_file_metadata(wav_a, work_dir, &in_a, scratch_a, sizeof(scratch_a)));
    TEST_ASSERT_TRUE(
        library_parse_audio_file_metadata(wav_b, work_dir, &in_b, scratch_b, sizeof(scratch_b)));
    TEST_ASSERT_TRUE(library_compute_audio_fingerprint(wav_a, in_a.audio_start_offset,
                                                       in_a.audio_payload_bytes, &fp_a));
    TEST_ASSERT_TRUE(library_compute_audio_fingerprint(wav_b, in_b.audio_start_offset,
                                                       in_b.audio_payload_bytes, &fp_b));
    TEST_ASSERT_TRUE(fp_a != fp_b);

    remove_scan_tree(work_dir);
}

static void test_incremental_scanner_cold_warm_edit_move_and_delete(void) {
    char work_dir[128];
    make_scan_work_directory(work_dir, sizeof(work_dir));
    char music_root[256], state_dir[256], covers_dir[256];
    snprintf(music_root, sizeof(music_root), "%s/music", work_dir);
    snprintf(state_dir, sizeof(state_dir), "%s/.dial-os", work_dir);
    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", state_dir);

    char album_a_dir[256], album_b_dir[256];
    snprintf(album_a_dir, sizeof(album_a_dir), "%s/ArtistA/Album1", music_root);
    snprintf(album_b_dir, sizeof(album_b_dir), "%s/ArtistB/Album2", music_root);
    make_directory_recursive(album_a_dir);
    make_directory_recursive(album_b_dir);

    char track1[320], track2[320], cover1[320], m3u_path[320];
    snprintf(track1, sizeof(track1), "%s/01.flac", album_a_dir);
    snprintf(track2, sizeof(track2), "%s/02.mp3", album_b_dir);
    snprintf(cover1, sizeof(cover1), "%s/cover.jpg", album_a_dir);
    snprintf(m3u_path, sizeof(m3u_path), "%s/mix.m3u", music_root);
    write_flac_with_audio_seed(track1, "Track One", "Artist A", "Album One", 0u, 0x12u, 12000u);
    write_mp3_with_audio_seed(track2, "Track Two", "Artist B", "Album Two", 0u, NULL, 0x77u, 10000u);
    FILE *cover_file = fopen(cover1, "wb");
    fwrite("JPG", 1u, 3u, cover_file);
    fclose(cover_file);
    FILE *m3u_file = fopen(m3u_path, "w");
    fprintf(m3u_file, "ArtistA/Album1/01.flac\nArtistB/Album2/02.mp3\n");
    fclose(m3u_file);

    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, state_dir));

    /* 1. Cold scan */
    struct library_db_scan_snapshot snapshot;
    run_sync_to_completion(&database, music_root, covers_dir, &snapshot);
    TEST_ASSERT_EQ_U32(2u, snapshot.track_count);
    TEST_ASSERT_EQ_U32(2u, snapshot.album_count);
    TEST_ASSERT_EQ_U32(2u, snapshot.telemetry.added_count);
    TEST_ASSERT_EQ_U32(2u, snapshot.telemetry.files_opened);
    TEST_ASSERT_EQ_U32(1u, snapshot.telemetry.m3u_parsed_count);
    TEST_ASSERT_EQ_U32(1u, library_db_count_pending_covers(&database));

    /* Mark Album One cached so we can verify warm boot and folder moves preserve it. */
    struct library_db_cover_job jobs[4];
    TEST_ASSERT_EQ_U32(1u, library_db_query_pending_covers(&database, jobs, 4u));
    uint64_t album1_uid = jobs[0].album_uid;
    TEST_ASSERT_TRUE(library_db_set_cover_status(&database, jobs[0].album_id,
                                                 LIBRARY_DB_COVER_CACHED));

    /* Favorite track1 and track2 */
    int64_t track1_id = 0, track2_id = 0;
    TEST_ASSERT_TRUE(library_db_find_track_by_path(&database, track1, &track1_id, NULL, NULL));
    TEST_ASSERT_TRUE(library_db_find_track_by_path(&database, track2, &track2_id, NULL, NULL));
    bool fav_state = false;
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_TRACK, track1_id,
                                                &fav_state));
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_TRACK, track2_id,
                                                &fav_state));

    /* 2. Unchanged warm boot + FAT32 +/-2s mtime shift -> 0 opens, 0 DB writes */
    struct stat st1;
    TEST_ASSERT_EQ_I32(0, stat(track1, &st1));
    struct utimbuf shifted = {st1.st_atime, st1.st_mtime + 2};
    TEST_ASSERT_EQ_I32(0, utime(track1, &shifted));
    run_sync_to_completion(&database, music_root, covers_dir, &snapshot);
    TEST_ASSERT_FALSE(snapshot.has_changes);
    TEST_ASSERT_EQ_U32(2u, snapshot.telemetry.unchanged_count);
    TEST_ASSERT_EQ_U32(0u, snapshot.telemetry.files_opened);
    TEST_ASSERT_EQ_U32(0u, snapshot.telemetry.db_writes);
    TEST_ASSERT_EQ_U32(0u, snapshot.telemetry.m3u_parsed_count);
    TEST_ASSERT_EQ_U32(0u, library_db_count_pending_covers(&database));

    /* 3. In-place tag edit on track1 (only 1 file parsed; track1_id & favorite kept) */
    write_flac_with_audio_seed(track1, "Track One Edited", "Artist A", "Album One", 32u, 0x12u,
                               12000u);
    struct utimbuf later = {st1.st_atime, st1.st_mtime + 10};
    (void)utime(track1, &later);
    run_sync_to_completion(&database, music_root, covers_dir, &snapshot);
    TEST_ASSERT_TRUE(snapshot.has_changes);
    TEST_ASSERT_EQ_U32(1u, snapshot.telemetry.modified_count);
    TEST_ASSERT_EQ_U32(1u, snapshot.telemetry.files_opened);
    int64_t check_id = 0;
    TEST_ASSERT_TRUE(library_db_find_track_by_path(&database, track1, &check_id, NULL, NULL));
    TEST_ASSERT_EQ_I32((int32_t)track1_id, (int32_t)check_id);

    /* 4. Folder move WITHOUT tag edits -> fast-path move, 0 audio opens, favorite & cover kept */
    char moved_dir[256], moved_track1[320];
    snprintf(moved_dir, sizeof(moved_dir), "%s/Reorganized/Album1_Renamed", music_root);
    make_directory_recursive(moved_dir);
    snprintf(moved_track1, sizeof(moved_track1), "%s/01.flac", moved_dir);
    TEST_ASSERT_EQ_I32(0, rename(track1, moved_track1));
    char moved_cover1[320];
    snprintf(moved_cover1, sizeof(moved_cover1), "%s/cover.jpg", moved_dir);
    TEST_ASSERT_EQ_I32(0, rename(cover1, moved_cover1));

    run_sync_to_completion(&database, music_root, covers_dir, &snapshot);
    TEST_ASSERT_EQ_U32(1u, snapshot.telemetry.moved_count);
    TEST_ASSERT_EQ_U32(0u, snapshot.telemetry.files_opened);
    TEST_ASSERT_TRUE(library_db_find_track_by_path(&database, moved_track1, &check_id, NULL, NULL));
    TEST_ASSERT_EQ_I32((int32_t)track1_id, (int32_t)check_id);

    /* 5. Folder move AND simultaneous tag edit on track2 -> audio_fingerprint matches! */
    char moved_track2[320];
    snprintf(moved_track2, sizeof(moved_track2), "%s/Moved/02_renamed.mp3", music_root);
    make_directory_recursive(moved_track2);
    rmdir(moved_track2); /* create parent only */
    (void)unlink(track2);
    write_mp3_with_audio_seed(moved_track2, "Track Two Moved+Edited", "Artist B", "Album Two",
                              512u, NULL, 0x77u, 10000u);
    run_sync_to_completion(&database, music_root, covers_dir, &snapshot);
    TEST_ASSERT_EQ_U32(1u, snapshot.telemetry.moved_edited_count);
    TEST_ASSERT_EQ_U32(2u, snapshot.track_count); /* zero duplicates */
    TEST_ASSERT_TRUE(library_db_find_track_by_path(&database, moved_track2, &check_id, NULL, NULL));
    TEST_ASSERT_EQ_I32((int32_t)track2_id, (int32_t)check_id);
    struct library_db_counts counts;
    TEST_ASSERT_TRUE(library_db_read_counts(&database, &counts));
    TEST_ASSERT_EQ_U32(2u, counts.favorite_tracks);

    /* 6. Sidecar cover replacement resets ONLY that album to PENDING */
    cover_file = fopen(moved_cover1, "wb");
    fwrite("NEW_JPG_BYTES", 1u, 13u, cover_file);
    fclose(cover_file);
    struct utimbuf cover_later = {1700005000, 1700005000};
    (void)utime(moved_cover1, &cover_later);
    run_sync_to_completion(&database, music_root, covers_dir, &snapshot);
    TEST_ASSERT_EQ_U32(0u, snapshot.telemetry.files_opened);
    TEST_ASSERT_EQ_U32(1u, library_db_count_pending_covers(&database));

    /* 7. Deleting track1 prunes Album One and unlinks sharded cover file */
    char shard_dir[320], shard_file[384];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%02x", covers_dir,
             (unsigned)((album1_uid >> 56u) & 0xFFu));
    make_directory_recursive(shard_dir);
    snprintf(shard_file, sizeof(shard_file), "%s/%016" PRIx64 ".rgb565", shard_dir, album1_uid);
    FILE *dummy_rgb = fopen(shard_file, "wb");
    fwrite("RGB", 1u, 3u, dummy_rgb);
    fclose(dummy_rgb);

    (void)unlink(moved_track1);
    run_sync_to_completion(&database, music_root, covers_dir, &snapshot);
    TEST_ASSERT_EQ_U32(1u, snapshot.telemetry.deleted_count);
    TEST_ASSERT_EQ_U32(1u, snapshot.track_count);
    TEST_ASSERT_EQ_U32(1u, snapshot.album_count);
    struct stat deleted_stat;
    TEST_ASSERT_TRUE(stat(shard_file, &deleted_stat) != 0);

    library_db_close(&database);
    remove_scan_tree(work_dir);
}

static void test_scanner_duplicates_corrupt_skip_and_cancel(void) {
    char work_dir[128];
    make_scan_work_directory(work_dir, sizeof(work_dir));
    char music_root[256], state_dir[256], covers_dir[256];
    snprintf(music_root, sizeof(music_root), "%s/music", work_dir);
    snprintf(state_dir, sizeof(state_dir), "%s/.dial-os", work_dir);
    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", state_dir);
    make_directory_recursive(music_root);

    char dup_a[320], dup_b[320], empty_bad[320];
    snprintf(dup_a, sizeof(dup_a), "%s/dup1.flac", music_root);
    snprintf(dup_b, sizeof(dup_b), "%s/dup2.flac", music_root);
    snprintf(empty_bad, sizeof(empty_bad), "%s/zero.flac", music_root);
    write_flac_with_audio_seed(dup_a, "Same", "Art", "Alb", 0u, 0x44u, 9000u);
    write_flac_with_audio_seed(dup_b, "Same", "Art", "Alb", 0u, 0x44u, 9000u);
    FILE *zero = fopen(empty_bad, "wb");
    fclose(zero);

    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, state_dir));
    struct library_db_scan_snapshot snapshot;
    run_sync_to_completion(&database, music_root, covers_dir, &snapshot);
    TEST_ASSERT_EQ_U32(2u, snapshot.track_count);

    /* Cancellation test: start and immediately cancel, ensuring clean pthread_join. */
    struct library_db_scanner scanner;
    library_db_scanner_initialize(&scanner);
    TEST_ASSERT_TRUE(library_db_scanner_start(&scanner, &database, music_root, covers_dir));
    library_db_scanner_cancel(&scanner);
    library_db_scanner_destroy(&scanner);

    library_db_close(&database);
    remove_scan_tree(work_dir);
}

static void test_normalize_fat_mtime_strips_kernel_timezone_offset(void) {
    TEST_ASSERT_TRUE(library_normalize_fat_mtime(1777026312ll, 360) == 1777004712ll);
    TEST_ASSERT_TRUE(library_normalize_fat_mtime(1777004712ll, 0) == 1777004712ll);
    TEST_ASSERT_TRUE(library_normalize_fat_mtime(1777004712ll, -540) == 1777037112ll);
}

void run_library_scanner_db_tests(void) {
    printf("Library DB scanner tests...\n");
    RUN_TEST(test_audio_payload_fingerprint_ignores_tag_and_padding_edits);
    RUN_TEST(test_incremental_scanner_cold_warm_edit_move_and_delete);
    RUN_TEST(test_scanner_duplicates_corrupt_skip_and_cancel);
    RUN_TEST(test_normalize_fat_mtime_strips_kernel_timezone_offset);
}
