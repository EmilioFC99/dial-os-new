#include "test_runner.h"

#include "now_playing_media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_be32(uint8_t *dest, uint32_t val) {
    dest[0] = (uint8_t)(val >> 24u);
    dest[1] = (uint8_t)(val >> 16u);
    dest[2] = (uint8_t)(val >> 8u);
    dest[3] = (uint8_t)val;
}

static void write_le32(uint8_t *dest, uint32_t val) {
    dest[0] = (uint8_t)val;
    dest[1] = (uint8_t)(val >> 8u);
    dest[2] = (uint8_t)(val >> 16u);
    dest[3] = (uint8_t)(val >> 24u);
}

static void test_lrc_parsing_and_cue_resolution(void) {
    const char *lrc_sample =
        "\xEF\xBB\xBF[ar:Artist]\n"
        "[ti:Title]\n"
        "[00:15.00]Second line\r\n"
        "[00:05.50][00:25.250]Chorus\n"
        "[00:35]Final line\n";
    struct lyrics_document doc;
    TEST_ASSERT_TRUE(now_playing_media_parse_lrc_text(lrc_sample, strlen(lrc_sample), &doc));
    TEST_ASSERT_TRUE(doc.has_timed);
    TEST_ASSERT_EQ_U32(4u, doc.cue_count);
    TEST_ASSERT_EQ_U32(5500u, doc.cues[0].time_ms);
    TEST_ASSERT_EQ_STR("Chorus", doc.cues[0].text);
    TEST_ASSERT_EQ_U32(15000u, doc.cues[1].time_ms);
    TEST_ASSERT_EQ_STR("Second line", doc.cues[1].text);
    TEST_ASSERT_EQ_U32(25250u, doc.cues[2].time_ms);
    TEST_ASSERT_EQ_U32(35000u, doc.cues[3].time_ms);

    /* Intro Cue before 5500ms */
    struct lyrics_resolved_state resolved;
    now_playing_media_resolve_cue(&doc, LYRICS_MODE_AUTO, 2000u, 60000u, &resolved);
    TEST_ASSERT_EQ_I32(LYRICS_MODE_TIMED, resolved.effective_mode);
    TEST_ASSERT_TRUE(resolved.is_intro_cue);
    TEST_ASSERT_EQ_I32(-1, resolved.active_index);
    TEST_ASSERT_EQ_I32(0, resolved.upcoming_index);
    TEST_ASSERT_EQ_STR("Chorus", resolved.upcoming_text);

    /* Active cue at 16000ms */
    now_playing_media_resolve_cue(&doc, LYRICS_MODE_AUTO, 16000u, 60000u, &resolved);
    TEST_ASSERT_FALSE(resolved.is_intro_cue);
    TEST_ASSERT_EQ_I32(1, resolved.active_index);
    TEST_ASSERT_EQ_STR("Second line", resolved.active_text);
    TEST_ASSERT_EQ_I32(2, resolved.upcoming_index);

    /* Untimed plain text + Auto Pace */
    const char *plain = "Line A\nLine B\nLine C\n";
    TEST_ASSERT_TRUE(now_playing_media_parse_lrc_text(plain, strlen(plain), &doc));
    TEST_ASSERT_FALSE(doc.has_timed);
    TEST_ASSERT_EQ_U32(3u, doc.cue_count);
    now_playing_media_resolve_cue(&doc, LYRICS_MODE_AUTO, 50000u, 100000u, &resolved);
    TEST_ASSERT_EQ_I32(LYRICS_MODE_UNTIMED, resolved.effective_mode);
    TEST_ASSERT_FALSE(resolved.is_intro_cue);
    TEST_ASSERT_EQ_I32(1, resolved.active_index);
    TEST_ASSERT_EQ_STR("Line B", resolved.active_text);

    /* No Lyrics */
    now_playing_media_resolve_cue(NULL, LYRICS_MODE_AUTO, 0u, 100000u, &resolved);
    TEST_ASSERT_EQ_I32(LYRICS_MODE_NONE, resolved.effective_mode);
    TEST_ASSERT_EQ_STR("NO EMBEDDED OR .LRC FILE", resolved.status_note);

    /* [offset:+500] and blank instrumental break timestamps */
    const char *offset_lrc = "[offset:+500]\n[00:05.00]Start\n[00:10.00] \n[00:15.00]End\n";
    TEST_ASSERT_TRUE(now_playing_media_parse_lrc_text(offset_lrc, strlen(offset_lrc), &doc));
    TEST_ASSERT_EQ_U32(3u, doc.cue_count);
    TEST_ASSERT_EQ_U32(4500u, doc.cues[0].time_ms);
    TEST_ASSERT_EQ_U32(9500u, doc.cues[1].time_ms);
    TEST_ASSERT_EQ_STR("", doc.cues[1].text);
    now_playing_media_resolve_cue(&doc, LYRICS_MODE_AUTO, 10000u, 30000u, &resolved);
    TEST_ASSERT_EQ_STR("", resolved.active_text);
    TEST_ASSERT_EQ_STR("End", resolved.upcoming_text);
}

static void test_sidecar_and_embedded_lyrics_files(void) {
    char temp_dir[] = "/tmp/dialos_media_XXXXXX";
    TEST_ASSERT_TRUE(mkdtemp(temp_dir) != NULL);

    /* 1. Sidecar .lrc */
    char flac_path[256];
    char lrc_path[256];
    snprintf(flac_path, sizeof(flac_path), "%s/track.flac", temp_dir);
    snprintf(lrc_path, sizeof(lrc_path), "%s/track.lrc", temp_dir);
    FILE *lrc_file = fopen(lrc_path, "wb");
    TEST_ASSERT_TRUE(lrc_file != NULL);
    fputs("[00:01.00]Hello Sidecar\n", lrc_file);
    fclose(lrc_file);

    struct lyrics_document doc;
    TEST_ASSERT_TRUE(now_playing_media_load_lyrics_for_track(flac_path, &doc));
    TEST_ASSERT_TRUE(doc.has_timed);
    TEST_ASSERT_EQ_STR("Hello Sidecar", doc.cues[0].text);
    unlink(lrc_path);

    /* 2. Embedded FLAC Vorbis LYRICS= */
    const char *comment = "LYRICS=Line One\nLine Two";
    uint32_t comment_len = (uint32_t)strlen(comment);
    uint32_t block_len = 4u + 4u + 4u + comment_len;
    uint8_t flac_bytes[128];
    memcpy(flac_bytes, "fLaC", 4u);
    flac_bytes[4] = 0x84u; /* last + VORBIS_COMMENT (4) */
    flac_bytes[5] = (uint8_t)(block_len >> 16u);
    flac_bytes[6] = (uint8_t)(block_len >> 8u);
    flac_bytes[7] = (uint8_t)block_len;
    write_le32(flac_bytes + 8, 0u);
    write_le32(flac_bytes + 12, 1u);
    write_le32(flac_bytes + 16, comment_len);
    memcpy(flac_bytes + 20, comment, comment_len);
    FILE *flac_file = fopen(flac_path, "wb");
    TEST_ASSERT_TRUE(flac_file != NULL);
    fwrite(flac_bytes, 1u, 20u + comment_len, flac_file);
    fclose(flac_file);

    TEST_ASSERT_TRUE(now_playing_media_load_lyrics_for_track(flac_path, &doc));
    TEST_ASSERT_FALSE(doc.has_timed);
    TEST_ASSERT_EQ_U32(2u, doc.cue_count);
    TEST_ASSERT_EQ_STR("Line One", doc.cues[0].text);
    unlink(flac_path);

    /* 3. Embedded MP3 ID3v2.3 USLT */
    char mp3_path[256];
    snprintf(mp3_path, sizeof(mp3_path), "%s/song.mp3", temp_dir);
    const char *uslt_text = "[00:02.00]ID3 Lyric";
    uint32_t uslt_frame_len = 1u + 3u + 1u + (uint32_t)strlen(uslt_text);
    uint32_t id3_body_len = 10u + uslt_frame_len;
    uint8_t mp3_bytes[128];
    memset(mp3_bytes, 0, sizeof(mp3_bytes));
    memcpy(mp3_bytes, "ID3\x03\x00\x00", 6u);
    mp3_bytes[9] = (uint8_t)id3_body_len; /* syncsafe for < 128 */
    memcpy(mp3_bytes + 10, "USLT", 4u);
    write_be32(mp3_bytes + 14, uslt_frame_len);
    mp3_bytes[20] = 3u; /* UTF-8 */
    memcpy(mp3_bytes + 21, "eng", 3u);
    mp3_bytes[24] = 0u; /* empty descriptor */
    memcpy(mp3_bytes + 25, uslt_text, strlen(uslt_text));
    FILE *mp3_file = fopen(mp3_path, "wb");
    TEST_ASSERT_TRUE(mp3_file != NULL);
    fwrite(mp3_bytes, 1u, 10u + id3_body_len, mp3_file);
    fclose(mp3_file);

    TEST_ASSERT_TRUE(now_playing_media_load_lyrics_for_track(mp3_path, &doc));
    TEST_ASSERT_TRUE(doc.has_timed);
    TEST_ASSERT_EQ_STR("ID3 Lyric", doc.cues[0].text);
    unlink(mp3_path);

    /* 4. FLAC with UNSYNCEDLYRICS= before timed LYRICS= prefers timed LYRICS= */
    const char *c1 = "UNSYNCEDLYRICS=Plain line";
    const char *c2 = "LYRICS=[00:03.00]Synced line";
    uint32_t l1 = (uint32_t)strlen(c1);
    uint32_t l2 = (uint32_t)strlen(c2);
    uint32_t blk_len = 4u + 4u + 4u + l1 + 4u + l2;
    uint8_t flac2[256];
    memcpy(flac2, "fLaC", 4u);
    flac2[4] = 0x84u;
    flac2[5] = (uint8_t)(blk_len >> 16u);
    flac2[6] = (uint8_t)(blk_len >> 8u);
    flac2[7] = (uint8_t)blk_len;
    write_le32(flac2 + 8, 0u);
    write_le32(flac2 + 12, 2u);
    write_le32(flac2 + 16, l1);
    memcpy(flac2 + 20, c1, l1);
    write_le32(flac2 + 20 + l1, l2);
    memcpy(flac2 + 24 + l1, c2, l2);
    FILE *ff2 = fopen(flac_path, "wb");
    TEST_ASSERT_TRUE(ff2 != NULL);
    fwrite(flac2, 1u, 8u + blk_len, ff2);
    fclose(ff2);
    TEST_ASSERT_TRUE(now_playing_media_load_lyrics_for_track(flac_path, &doc));
    TEST_ASSERT_TRUE(doc.has_timed);
    TEST_ASSERT_EQ_STR("Synced line", doc.cues[0].text);
    unlink(flac_path);

    /* 5. Requesting a new track immediately clears previous active_lyrics_path & ready flag */
    struct now_playing_media media;
    now_playing_media_initialize(&media, NULL);
    pthread_mutex_lock(&media.mutex);
    media.active_lyrics_ready = true;
    snprintf(media.active_lyrics_path, sizeof(media.active_lyrics_path), "/m/old.flac");
    pthread_mutex_unlock(&media.mutex);
    now_playing_media_request_active(&media, 0, 0ull, "/m/new.flac");
    pthread_mutex_lock(&media.mutex);
    TEST_ASSERT_FALSE(media.active_lyrics_ready);
    TEST_ASSERT_EQ_STR("", media.active_lyrics_path);
    pthread_mutex_unlock(&media.mutex);
    now_playing_media_destroy(&media);

    rmdir(temp_dir);
}

void run_now_playing_media_tests(void) {
    printf("Suite: now_playing_media\n");
    RUN_TEST(test_lrc_parsing_and_cue_resolution);
    RUN_TEST(test_sidecar_and_embedded_lyrics_files);
}
