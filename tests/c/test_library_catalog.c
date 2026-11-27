#include "test_runner.h"

#include "library_catalog.h"
#include "library_scanner.h"
#include "library_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void make_temp_dir(char *path_out, size_t capacity) {
    snprintf(path_out, capacity, "/tmp/dialos_lib_test_%d", (int)getpid());
    mkdir(path_out, 0755);
}

static void remove_tree_recursive(const char *path) {
    char command[512];
    snprintf(command, sizeof(command), "rm -rf \"%s\"", path);
    (void)system(command);
}

static void write_synthetic_flac(const char *path, const char *title, const char *artist,
                                 const char *album, const char *genre, const char *track_str) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_TRUE(file != NULL);

    /* "fLaC" + STREAMINFO (type 0, 34 bytes, sample_rate=44100, total_samples=441000 -> 10s) */
    uint8_t streaminfo[4 + 4 + 34];
    memset(streaminfo, 0, sizeof(streaminfo));
    memcpy(streaminfo, "fLaC", 4u);
    streaminfo[4] = 0x00u; /* not last, type 0 */
    streaminfo[5] = 0u;
    streaminfo[6] = 0u;
    streaminfo[7] = 34u;
    /* Bytes 10..12 of STREAMINFO: 20-bit sample rate = 44100 (0x0AC44), 3-bit ch-1, 5-bit bps-1, 36-bit samples */
    uint32_t sr = 44100u;
    streaminfo[8 + 10] = (uint8_t)(sr >> 12u);
    streaminfo[8 + 11] = (uint8_t)(sr >> 4u);
    streaminfo[8 + 12] = (uint8_t)(((sr & 0x0Fu) << 4u) | (1u << 1u));
    streaminfo[8 + 13] = 0xF0u;
    /* total samples = 441000 = 0x06BAA8 */
    streaminfo[8 + 15] = 0x06u;
    streaminfo[8 + 16] = 0xBAu;
    streaminfo[8 + 17] = 0xA8u;
    fwrite(streaminfo, 1u, sizeof(streaminfo), file);

    /* VORBIS_COMMENT (type 4, last=1) */
    char c0[128], c1[128], c2[128], c3[128], c4[128];
    snprintf(c0, sizeof(c0), "TITLE=%s", title);
    snprintf(c1, sizeof(c1), "ARTIST=%s", artist);
    snprintf(c2, sizeof(c2), "ALBUM=%s", album);
    snprintf(c3, sizeof(c3), "GENRE=%s", genre);
    snprintf(c4, sizeof(c4), "TRACKNUMBER=%s", track_str);
    const char *comments[5] = {c0, c1, c2, c3, c4};

    uint8_t vc_buf[1024];
    size_t pos = 0u;
    /* vendor length 4 "TEST" */
    vc_buf[pos++] = 4u; vc_buf[pos++] = 0u; vc_buf[pos++] = 0u; vc_buf[pos++] = 0u;
    memcpy(vc_buf + pos, "TEST", 4u);
    pos += 4u;
    /* comment count = 5 */
    vc_buf[pos++] = 5u; vc_buf[pos++] = 0u; vc_buf[pos++] = 0u; vc_buf[pos++] = 0u;
    for (uint32_t idx = 0u; idx < 5u; idx++) {
        uint32_t len = (uint32_t)strlen(comments[idx]);
        vc_buf[pos++] = (uint8_t)(len & 0xFFu);
        vc_buf[pos++] = (uint8_t)((len >> 8u) & 0xFFu);
        vc_buf[pos++] = (uint8_t)((len >> 16u) & 0xFFu);
        vc_buf[pos++] = (uint8_t)((len >> 24u) & 0xFFu);
        memcpy(vc_buf + pos, comments[idx], len);
        pos += len;
    }

    uint8_t vc_hdr[4] = {0x84u, (uint8_t)((pos >> 16u) & 0xFFu),
                         (uint8_t)((pos >> 8u) & 0xFFu), (uint8_t)(pos & 0xFFu)};
    fwrite(vc_hdr, 1u, 4u, file);
    fwrite(vc_buf, 1u, pos, file);
    fclose(file);
}

static void write_synthetic_mp3_id3v23(const char *path, const char *title, const char *artist,
                                       const char *album) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_TRUE(file != NULL);

    uint8_t payload[512];
    size_t pos = 0u;

    const char *ids[3] = {"TIT2", "TPE1", "TALB"};
    const char *vals[3] = {title, artist, album};
    for (uint32_t idx = 0u; idx < 3u; idx++) {
        uint32_t len = (uint32_t)strlen(vals[idx]) + 1u; /* +1 encoding byte (3 = UTF-8) */
        memcpy(payload + pos, ids[idx], 4u);
        pos += 4u;
        payload[pos++] = (uint8_t)((len >> 24u) & 0xFFu);
        payload[pos++] = (uint8_t)((len >> 16u) & 0xFFu);
        payload[pos++] = (uint8_t)((len >> 8u) & 0xFFu);
        payload[pos++] = (uint8_t)(len & 0xFFu);
        payload[pos++] = 0u;
        payload[pos++] = 0u;
        payload[pos++] = 3u; /* UTF-8 */
        memcpy(payload + pos, vals[idx], strlen(vals[idx]));
        pos += strlen(vals[idx]);
    }

    uint8_t hdr[10] = {'I', 'D', '3', 3u, 0u, 0u,
                       (uint8_t)((pos >> 21u) & 0x7Fu), (uint8_t)((pos >> 14u) & 0x7Fu),
                       (uint8_t)((pos >> 7u) & 0x7Fu), (uint8_t)(pos & 0x7Fu)};
    fwrite(hdr, 1u, 10u, file);
    fwrite(payload, 1u, pos, file);
    /* Add a minimal MPEG1 Layer3 128kbps 44.1kHz frame header + padding */
    uint8_t mpeg_frame[417];
    memset(mpeg_frame, 0, sizeof(mpeg_frame));
    mpeg_frame[0] = 0xFFu;
    mpeg_frame[1] = 0xFBu;
    mpeg_frame[2] = 0x90u;
    mpeg_frame[3] = 0x00u;
    fwrite(mpeg_frame, 1u, sizeof(mpeg_frame), file);
    fclose(file);
}

static void write_synthetic_wav(const char *path) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_TRUE(file != NULL);
    /* 1 second of 44100 Hz 16-bit mono PCM = 88200 bytes */
    uint32_t data_bytes = 88200u;
    uint32_t riff_size = 36u + data_bytes;
    uint8_t hdr[44] = {
        'R', 'I', 'F', 'F',
        (uint8_t)(riff_size & 0xFFu), (uint8_t)((riff_size >> 8u) & 0xFFu),
        (uint8_t)((riff_size >> 16u) & 0xFFu), (uint8_t)((riff_size >> 24u) & 0xFFu),
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16u, 0u, 0u, 0u,
        1u, 0u, /* PCM */
        1u, 0u, /* 1 channel */
        0x44u, 0xACu, 0u, 0u, /* 44100 Hz */
        0x88u, 0x58u, 0x01u, 0u, /* 88200 byte/s */
        2u, 0u, 16u, 0u,
        'd', 'a', 't', 'a',
        (uint8_t)(data_bytes & 0xFFu), (uint8_t)((data_bytes >> 8u) & 0xFFu),
        (uint8_t)((data_bytes >> 16u) & 0xFFu), (uint8_t)((data_bytes >> 24u) & 0xFFu),
    };
    fwrite(hdr, 1u, 44u, file);
    uint8_t zeros[512];
    memset(zeros, 0, sizeof(zeros));
    for (uint32_t written = 0u; written < data_bytes; written += sizeof(zeros)) {
        uint32_t chunk = (data_bytes - written < sizeof(zeros)) ? (data_bytes - written) : sizeof(zeros);
        fwrite(zeros, 1u, chunk, file);
    }
    fclose(file);
}

static void test_scanner_parsers_and_cache_roundtrip(void) {
    char root_dir[256];
    make_temp_dir(root_dir, sizeof(root_dir));
    remove_tree_recursive(root_dir);
    mkdir(root_dir, 0755);

    char dialos_dir[300];
    snprintf(dialos_dir, sizeof(dialos_dir), "%s/.dial-os", root_dir);
    mkdir(dialos_dir, 0755);

    char artist_dir[300], album_dir[300], hidden_dir[300];
    snprintf(artist_dir, sizeof(artist_dir), "%s/A Hundred Drums", root_dir);
    snprintf(album_dir, sizeof(album_dir), "%s/A Hundred Drums/EP1", root_dir);
    snprintf(hidden_dir, sizeof(hidden_dir), "%s/.Trashes", root_dir);
    mkdir(artist_dir, 0755);
    mkdir(album_dir, 0755);
    mkdir(hidden_dir, 0755);

    char flac_path[360], mp3_path[360], wav_path[360], dot_path[360], m3u_path[360], cover_path[360];
    snprintf(flac_path, sizeof(flac_path), "%s/01 - Shakti Sound.flac", album_dir);
    snprintf(mp3_path, sizeof(mp3_path), "%s/02 - Boogie Man.mp3", album_dir);
    snprintf(wav_path, sizeof(wav_path), "%s/03 - Raw Signal.wav", album_dir);
    snprintf(dot_path, sizeof(dot_path), "%s/._01 - Shakti Sound.flac", album_dir);
    snprintf(cover_path, sizeof(cover_path), "%s/cover.jpg", album_dir);
    snprintf(m3u_path, sizeof(m3u_path), "%s/Night Mix.m3u", root_dir);

    write_synthetic_flac(flac_path, "Shakti Sound", "A Hundred Drums", "EP1",
                         "\xC3\x89lectronique; Bass", "1/2");
    write_synthetic_mp3_id3v23(mp3_path, "Boogie Man", "A Hundred Drums", "EP1");
    write_synthetic_wav(wav_path);

    /* Hidden AppleDouble file must be ignored */
    FILE *dot_file = fopen(dot_path, "wb");
    TEST_ASSERT_TRUE(dot_file != NULL);
    fwrite("junk", 1u, 4u, dot_file);
    fclose(dot_file);

    /* Sidecar cover placeholder */
    FILE *cov_file = fopen(cover_path, "wb");
    TEST_ASSERT_TRUE(cov_file != NULL);
    fwrite("JFIF", 1u, 4u, cov_file);
    fclose(cov_file);

    /* Read-only .m3u playlist */
    FILE *m3u_file = fopen(m3u_path, "w");
    TEST_ASSERT_TRUE(m3u_file != NULL);
    fprintf(m3u_file, "#EXTM3U\nA Hundred Drums/EP1/02 - Boogie Man.mp3\nA Hundred Drums/EP1/01 - Shakti Sound.flac\n");
    fclose(m3u_file);

    struct library_catalog catalog;
    library_catalog_initialize(&catalog);
    struct library_scanner scanner;
    library_scanner_initialize(&scanner, &catalog);

    TEST_ASSERT_TRUE(library_scanner_start(&scanner, root_dir, dialos_dir, true));
    uint64_t fake_ns = 1000000ull;
    uint32_t steps = 0u;
    while (library_scanner_is_busy(&scanner) && steps < 500u) {
        library_scanner_step(&scanner, fake_ns, LIBRARY_SCAN_DEFAULT_BUDGET_NS);
        fake_ns += 16000000ull;
        steps++;
    }
    TEST_ASSERT_TRUE(library_scanner_is_ready(&scanner));
    TEST_ASSERT_FALSE(scanner.loaded_from_cache);
    TEST_ASSERT_EQ_U32(3u, catalog.track_count);
    TEST_ASSERT_EQ_U32(1u, catalog.album_count);
    TEST_ASSERT_EQ_U32(1u, catalog.artist_count);
    TEST_ASSERT_EQ_U32(3u, catalog.genre_count); /* "Électronique", "Bass", "Unknown Genre" */
    TEST_ASSERT_EQ_U32(1u, catalog.playlist_count);
    TEST_ASSERT_EQ_STR("Night Mix", library_catalog_get_string(&catalog, catalog.playlists[0].name_offset));
    TEST_ASSERT_EQ_U32(2u, catalog.playlists[0].track_count);
    TEST_ASSERT_EQ_U32(LIBRARY_COVER_SIDECAR, catalog.albums[0].cover_source_kind);

    /* Verify FLAC duration = 10s and WAV fallback title + duration = 1s */
    int32_t flac_idx = library_catalog_find_track_by_path(&catalog, flac_path);
    int32_t wav_idx = library_catalog_find_track_by_path(&catalog, wav_path);
    TEST_ASSERT_TRUE(flac_idx >= 0 && wav_idx >= 0);
    TEST_ASSERT_EQ_U32(10u, catalog.tracks[flac_idx].duration_seconds);
    TEST_ASSERT_EQ_U32(1u, catalog.tracks[wav_idx].duration_seconds);
    TEST_ASSERT_EQ_STR("Raw Signal", library_catalog_get_string(&catalog, catalog.tracks[wav_idx].title_offset));

    /* Warm boot cache load */
    library_scanner_destroy(&scanner);
    library_catalog_destroy(&catalog);

    library_catalog_initialize(&catalog);
    library_scanner_initialize(&scanner, &catalog);
    TEST_ASSERT_TRUE(library_scanner_start(&scanner, root_dir, dialos_dir, true));
    for (uint32_t idx = 0u; idx < 20u && library_scanner_is_busy(&scanner); idx++) {
        library_scanner_step(&scanner, fake_ns, LIBRARY_SCAN_DEFAULT_BUDGET_NS);
        fake_ns += 16000000ull;
    }
    TEST_ASSERT_TRUE(library_scanner_is_ready(&scanner));
    TEST_ASSERT_TRUE(scanner.loaded_from_cache);
    TEST_ASSERT_EQ_U32(3u, catalog.track_count);

    /* Delta check detects no changes first, then detects added file */
    TEST_ASSERT_TRUE(library_scanner_start_delta_check(&scanner));
    while (library_scanner_is_busy(&scanner)) {
        library_scanner_step(&scanner, fake_ns, LIBRARY_SCAN_DELTA_BUDGET_NS);
        fake_ns += 16000000ull;
    }
    TEST_ASSERT_FALSE(scanner.delta_changed);

    char wav2_path[360];
    snprintf(wav2_path, sizeof(wav2_path), "%s/04 - Encore.wav", album_dir);
    write_synthetic_wav(wav2_path);
    TEST_ASSERT_TRUE(library_scanner_start_delta_check(&scanner));
    while (library_scanner_is_busy(&scanner)) {
        library_scanner_step(&scanner, fake_ns, LIBRARY_SCAN_DELTA_BUDGET_NS);
        fake_ns += 16000000ull;
    }
    TEST_ASSERT_TRUE(scanner.delta_changed);

    library_scanner_destroy(&scanner);
    library_catalog_destroy(&catalog);
    remove_tree_recursive(root_dir);
}

static const char *test_album_seek_getter(const struct library_catalog *catalog, uint32_t item_index,
                                          void *user_context, int64_t *added_at_out,
                                          bool *is_favorite_out) {
    const uint32_t *order = (const uint32_t *)user_context;
    const struct library_album *album = &catalog->albums[order[item_index]];
    *added_at_out = album->added_at_seconds;
    *is_favorite_out = album->is_favorite;
    return library_catalog_get_string(catalog, album->title_offset);
}

static void test_sort_modes_seek_anchors_and_state_persistence(void) {
    char root_dir[256];
    make_temp_dir(root_dir, sizeof(root_dir));
    remove_tree_recursive(root_dir);
    mkdir(root_dir, 0755);

    struct library_catalog catalog;
    library_catalog_initialize(&catalog);

    struct library_track_input tracks[3] = {
        {.file_path = "/sd/a.flac", .relative_path = "a.flac", .title = "Alpha",
         .artist = "Zed", .album = "Beta", .genre = "House",
         .track_number = 1u, .duration_seconds = 120u, .added_at_seconds = 1700000000ll},
        {.file_path = "/sd/b.flac", .relative_path = "b.flac", .title = "Gamma",
         .artist = "Amy", .album = "Alpha", .genre = "Ambient",
         .track_number = 1u, .duration_seconds = 180u, .added_at_seconds = 1710000000ll},
        {.file_path = "/sd/c.flac", .relative_path = "c.flac", .title = "Delta",
         .artist = "Bob", .album = "1999", .genre = "Dub",
         .track_number = 1u, .duration_seconds = 200u, .added_at_seconds = 1750000000ll},
    };
    for (uint32_t idx = 0u; idx < 3u; idx++) {
        TEST_ASSERT_TRUE(library_catalog_add_track(&catalog, &tracks[idx]));
    }
    TEST_ASSERT_TRUE(library_catalog_finalize(&catalog));

    struct library_state_store store;
    library_state_initialize(&store);
    uint64_t beta_id = catalog.albums[0].album_id; /* insertion order: Beta (0), Alpha (1), 1999 (2) */
    TEST_ASSERT_TRUE(library_state_toggle_album_favorite(&store, &catalog, beta_id));
    store.album_sort_mode = LIBRARY_SORT_LIKED_FIRST;

    char state_temp[300], state_final[300];
    snprintf(state_temp, sizeof(state_temp), "%s/.library_state.json.tmp", root_dir);
    snprintf(state_final, sizeof(state_final), "%s/library_state.json", root_dir);
    TEST_ASSERT_TRUE(library_state_save_json(&store, root_dir, state_temp, state_final));

    struct library_state_store reloaded;
    library_state_initialize(&reloaded);
    TEST_ASSERT_TRUE(library_state_load_json(&reloaded, state_final));
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_LIKED_FIRST, reloaded.album_sort_mode);
    TEST_ASSERT_EQ_U32(1u, reloaded.favorite_album_count);

    uint32_t order[3] = {0u, 1u, 2u};
    library_catalog_sort_album_indices(&catalog, LIBRARY_SORT_LIKED_FIRST, order, 3u);
    TEST_ASSERT_EQ_U32(0u, order[0]); /* Beta (favorited) first */

    struct library_seek_anchor anchors[16];
    uint32_t anchor_count = library_build_seek_anchors(&catalog, LIBRARY_SORT_LIKED_FIRST, true, 3u,
                                                       test_album_seek_getter, order, anchors, 16u);
    TEST_ASSERT_EQ_U32(3u, anchor_count);
    TEST_ASSERT_TRUE(anchors[0].is_favorite);
    TEST_ASSERT_EQ_STR("B", anchors[0].primary);
    TEST_ASSERT_EQ_STR("#", anchors[1].primary);

    /* Non-destructive playlist deletion records dismissed_m3u without unlinking user file */
    char m3u_file_path[300];
    snprintf(m3u_file_path, sizeof(m3u_file_path), "%s/UserMix.m3u", root_dir);
    FILE *m3u = fopen(m3u_file_path, "w");
    TEST_ASSERT_TRUE(m3u != NULL);
    fprintf(m3u, "#EXTM3U\n/sd/a.flac\n");
    fclose(m3u);
    uint32_t pl_tracks[1] = {0u};
    TEST_ASSERT_TRUE(library_catalog_add_playlist(&catalog, library_fnv1a_64("pl:usermix"),
                                                  "UserMix", m3u_file_path, pl_tracks, 1u));
    TEST_ASSERT_TRUE(library_state_delete_playlist(&reloaded, &catalog, 0u));
    TEST_ASSERT_EQ_U32(0u, catalog.playlist_count);
    TEST_ASSERT_TRUE(library_state_is_m3u_dismissed(&reloaded, m3u_file_path));
    TEST_ASSERT_EQ_I32(0, access(m3u_file_path, F_OK)); /* User .m3u file untouched! */

    library_state_destroy(&store);
    library_state_destroy(&reloaded);
    library_catalog_destroy(&catalog);
    remove_tree_recursive(root_dir);
}

static void write_be32_local(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)((value >> 24u) & 0xFFu);
    out[1] = (uint8_t)((value >> 16u) & 0xFFu);
    out[2] = (uint8_t)((value >> 8u) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

static void test_flac_embedded_picture_offset_parsing(void) {
    char root_dir[256];
    snprintf(root_dir, sizeof(root_dir), "/tmp/dialos_flac_pic_%d", (int)getpid());
    remove_tree_recursive(root_dir);
    mkdir(root_dir, 0755);

    char artist_dir[300], album_dir[340], flac_path[380];
    snprintf(artist_dir, sizeof(artist_dir), "%s/Koven", root_dir);
    snprintf(album_dir, sizeof(album_dir), "%s/Butterfly Effect", artist_dir);
    snprintf(flac_path, sizeof(flac_path), "%s/01 - Worlds Collide.flac", album_dir);
    mkdir(artist_dir, 0755);
    mkdir(album_dir, 0755);

    static const char *MIME = "image/jpeg";
    static const char *DESC = "Front";
    static const uint8_t FAKE_JPEG[6] = {0xFFu, 0xD8u, 0xFFu, 0xE0u, 0xFFu, 0xD9u};
    uint32_t mime_len = (uint32_t)strlen(MIME);
    uint32_t desc_len = (uint32_t)strlen(DESC);
    uint32_t pic_block_len = 8u + mime_len + 4u + desc_len + 16u + 4u + sizeof(FAKE_JPEG);

    FILE *file = fopen(flac_path, "wb");
    TEST_ASSERT_TRUE(file != NULL);
    fwrite("fLaC", 1u, 4u, file);

    /* STREAMINFO (type=0, 34 bytes) */
    uint8_t streaminfo_hdr[4] = {0u, 0u, 0u, 34u};
    uint8_t streaminfo[34] = {0};
    /* sample_rate = 44100 (0xAC44), total_samples = 44100 * 10 = 441000 */
    streaminfo[10] = 0x0Au;
    streaminfo[11] = 0xC4u;
    streaminfo[12] = 0x40u;
    streaminfo[15] = 0x06u;
    streaminfo[16] = 0xBAu;
    streaminfo[17] = 0xA8u;
    fwrite(streaminfo_hdr, 1u, 4u, file);
    fwrite(streaminfo, 1u, 34u, file);

    /* PICTURE (type=6, last=1) */
    uint8_t pic_hdr[4] = {
        0x86u,
        (uint8_t)((pic_block_len >> 16u) & 0xFFu),
        (uint8_t)((pic_block_len >> 8u) & 0xFFu),
        (uint8_t)(pic_block_len & 0xFFu),
    };
    fwrite(pic_hdr, 1u, 4u, file);

    uint8_t field_u32[4];
    write_be32_local(field_u32, 3u); /* Front Cover */
    fwrite(field_u32, 1u, 4u, file);
    write_be32_local(field_u32, mime_len);
    fwrite(field_u32, 1u, 4u, file);
    fwrite(MIME, 1u, mime_len, file);
    write_be32_local(field_u32, desc_len);
    fwrite(field_u32, 1u, 4u, file);
    fwrite(DESC, 1u, desc_len, file);
    uint8_t dims[16] = {0};
    write_be32_local(dims, 1000u);
    write_be32_local(dims + 4u, 1000u);
    write_be32_local(dims + 8u, 24u);
    fwrite(dims, 1u, 16u, file);
    write_be32_local(field_u32, sizeof(FAKE_JPEG));
    fwrite(field_u32, 1u, 4u, file);
    long expected_offset = ftell(file);
    fwrite(FAKE_JPEG, 1u, sizeof(FAKE_JPEG), file);
    fclose(file);

    struct library_track_input parsed;
    char scratch[2048];
    TEST_ASSERT_TRUE(
        library_parse_audio_file_metadata(flac_path, root_dir, &parsed, scratch, sizeof(scratch)));
    TEST_ASSERT_EQ_U32(LIBRARY_COVER_EMBEDDED_FLAC, parsed.cover_source_kind);
    TEST_ASSERT_EQ_U32((uint32_t)expected_offset, parsed.cover_data_offset);
    TEST_ASSERT_EQ_U32((uint32_t)sizeof(FAKE_JPEG), parsed.cover_data_length);

    remove_tree_recursive(root_dir);
}

void run_library_catalog_tests(void) {
    printf("Suite: library_catalog\n");
    RUN_TEST(test_scanner_parsers_and_cache_roundtrip);
    RUN_TEST(test_sort_modes_seek_anchors_and_state_persistence);
    RUN_TEST(test_flac_embedded_picture_offset_parsing);
}
