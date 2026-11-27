#include "test_runner.h"

#include "jpeg_fast_thumb.h"
#include "library_cover_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t compute_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t index = 0u; index < length; index++) {
        crc ^= data[index];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            crc = (crc & 1u) != 0u ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }
    return ~crc;
}

static uint32_t compute_adler32(const uint8_t *data, size_t length) {
    uint32_t s1 = 1u;
    uint32_t s2 = 0u;
    for (size_t index = 0u; index < length; index++) {
        s1 = (s1 + data[index]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16u) | s1;
}

static void write_be32_buf(uint8_t *out, uint32_t val) {
    out[0] = (uint8_t)((val >> 24u) & 0xFFu);
    out[1] = (uint8_t)((val >> 16u) & 0xFFu);
    out[2] = (uint8_t)((val >> 8u) & 0xFFu);
    out[3] = (uint8_t)(val & 0xFFu);
}

static size_t append_png_chunk(uint8_t *out, const char *type, const uint8_t *payload,
                               uint32_t payload_len) {
    write_be32_buf(out, payload_len);
    memcpy(out + 4u, type, 4u);
    if (payload_len > 0u) {
        memcpy(out + 8u, payload, payload_len);
    }
    uint32_t crc = compute_crc32(out + 4u, 4u + payload_len);
    write_be32_buf(out + 8u + payload_len, crc);
    return 12u + payload_len;
}

static size_t build_synthetic_2x2_red_png(uint8_t *out, size_t capacity) {
    if (capacity < 160u) {
        return 0u;
    }
    size_t pos = 0u;
    static const uint8_t PNG_SIG[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};
    memcpy(out, PNG_SIG, 8u);
    pos += 8u;

    uint8_t ihdr[13] = {0u, 0u, 0u, 2u, 0u, 0u, 0u, 2u, 8u, 2u, 0u, 0u, 0u};
    pos += append_png_chunk(out + pos, "IHDR", ihdr, 13u);

    /* 2 rows of (filter=0, R=255, G=0, B=0, R=255, G=0, B=0) = 14 bytes */
    uint8_t raw_scanlines[14] = {
        0u, 255u, 0u, 0u, 255u, 0u, 0u,
        0u, 255u, 0u, 0u, 255u, 0u, 0u,
    };
    uint8_t zlib_block[2 + 5 + 14 + 4];
    zlib_block[0] = 0x78u;
    zlib_block[1] = 0x01u;
    zlib_block[2] = 0x01u; /* BFINAL=1, BTYPE=00 (stored) */
    zlib_block[3] = 14u;
    zlib_block[4] = 0u;
    zlib_block[5] = (uint8_t)(~14u & 0xFFu);
    zlib_block[6] = 0xFFu;
    memcpy(zlib_block + 7u, raw_scanlines, 14u);
    write_be32_buf(zlib_block + 21u, compute_adler32(raw_scanlines, 14u));
    pos += append_png_chunk(out + pos, "IDAT", zlib_block, sizeof(zlib_block));
    pos += append_png_chunk(out + pos, "IEND", NULL, 0u);
    return pos;
}

static void test_cover_decode_disk_cache_and_lru(void) {
    char root_dir[256];
    snprintf(root_dir, sizeof(root_dir), "/tmp/dialos_cov_test_%d", (int)getpid());
    char rm_cmd[320];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", root_dir);
    (void)system(rm_cmd);
    mkdir(root_dir, 0755);

    char covers_dir[300];
    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", root_dir);

    uint8_t png_bytes[256];
    size_t png_len = build_synthetic_2x2_red_png(png_bytes, sizeof(png_bytes));
    TEST_ASSERT_TRUE(png_len > 0u);

    /* Sidecar PNG decode + disk cache write */
    char sidecar_path[300];
    snprintf(sidecar_path, sizeof(sidecar_path), "%s/cover.png", root_dir);
    FILE *sidecar = fopen(sidecar_path, "wb");
    TEST_ASSERT_TRUE(sidecar != NULL);
    fwrite(png_bytes, 1u, png_len, sidecar);
    fclose(sidecar);

    struct library_cover_cache cache;
    library_cover_cache_initialize(&cache, covers_dir);

    TEST_ASSERT_TRUE(library_cover_cache_request(&cache, 0x1111ull, LIBRARY_COVER_SIDECAR,
                                                 sidecar_path, 0u, 0u));
    /* Heavy decode suppressed during drag/scroll */
    TEST_ASSERT_FALSE(library_cover_cache_step(&cache, false));
    /* Heavy decode allowed when settled */
    TEST_ASSERT_TRUE(library_cover_cache_step(&cache, true));

    const uint16_t *pixels = library_cover_cache_lookup(&cache, 0x1111ull);
    TEST_ASSERT_TRUE(pixels != NULL);
    TEST_ASSERT_EQ_U32(0xF800u, pixels[0]);
    TEST_ASSERT_EQ_U32(0xF800u, pixels[LIBRARY_COVER_PIXEL_COUNT - 1u]);

    /* Delete original sidecar and reset RAM LRU; lookup_or_load_disk hits disk immediately */
    unlink(sidecar_path);
    library_cover_cache_clear(&cache);
    TEST_ASSERT_TRUE(library_cover_cache_lookup(&cache, 0x1111ull) == NULL);
    TEST_ASSERT_TRUE(library_cover_cache_lookup_or_load_disk(&cache, 0x1111ull) != NULL);
    uint32_t ram_hits = 0u;
    uint32_t disk_hits = 0u;
    uint32_t decodes = 0u;
    uint32_t max_op_ms = 0u;
    library_cover_cache_take_window_metrics(&cache, &ram_hits, &disk_hits, &decodes, &max_op_ms);
    TEST_ASSERT_EQ_U32(0u, ram_hits);
    TEST_ASSERT_EQ_U32(1u, disk_hits);

    /* Second lookup hits RAM LRU */
    TEST_ASSERT_TRUE(library_cover_cache_lookup_or_load_disk(&cache, 0x1111ull) != NULL);
    library_cover_cache_take_window_metrics(&cache, &ram_hits, &disk_hits, &decodes, &max_op_ms);
    TEST_ASSERT_EQ_U32(1u, ram_hits);
    TEST_ASSERT_EQ_U32(0u, disk_hits);

    /* Embedded offset decode */
    char embedded_path[300];
    snprintf(embedded_path, sizeof(embedded_path), "%s/track.flac", root_dir);
    FILE *emb = fopen(embedded_path, "wb");
    TEST_ASSERT_TRUE(emb != NULL);
    uint8_t header_pad[64];
    memset(header_pad, 0xAA, sizeof(header_pad));
    fwrite(header_pad, 1u, sizeof(header_pad), emb);
    fwrite(png_bytes, 1u, png_len, emb);
    fclose(emb);

    TEST_ASSERT_TRUE(library_cover_cache_request(&cache, 0x2222ull, LIBRARY_COVER_EMBEDDED_FLAC,
                                                 embedded_path, 64u, (uint32_t)png_len));
    TEST_ASSERT_TRUE(library_cover_cache_step(&cache, true));
    TEST_ASSERT_TRUE(library_cover_cache_lookup(&cache, 0x2222ull) != NULL);

    /* Corrupt payload rejection marks failed and never retries */
    char bad_path[300];
    snprintf(bad_path, sizeof(bad_path), "%s/bad.jpg", root_dir);
    FILE *bad = fopen(bad_path, "wb");
    TEST_ASSERT_TRUE(bad != NULL);
    fwrite("NOT_A_JPEG", 1u, 10u, bad);
    fclose(bad);
    TEST_ASSERT_TRUE(library_cover_cache_request(&cache, 0x3333ull, LIBRARY_COVER_SIDECAR,
                                                 bad_path, 0u, 0u));
    TEST_ASSERT_TRUE(library_cover_cache_step(&cache, true));
    TEST_ASSERT_TRUE(library_cover_cache_lookup(&cache, 0x3333ull) == NULL);
    TEST_ASSERT_FALSE(library_cover_cache_request(&cache, 0x3333ull, LIBRARY_COVER_SIDECAR,
                                                  bad_path, 0u, 0u));

    /* LRU eviction past 128 slots */
    library_cover_cache_clear(&cache);
    for (uint64_t idx = 1ull; idx <= (uint64_t)LIBRARY_COVER_LRU_SLOTS + 2ull; idx++) {
        char path_i[300];
        snprintf(path_i, sizeof(path_i), "%s/c_%llu.png", root_dir, (unsigned long long)idx);
        FILE *fp = fopen(path_i, "wb");
        TEST_ASSERT_TRUE(fp != NULL);
        fwrite(png_bytes, 1u, png_len, fp);
        fclose(fp);
        TEST_ASSERT_TRUE(library_cover_cache_request(&cache, 100ull + idx, LIBRARY_COVER_SIDECAR,
                                                     path_i, 0u, 0u));
        TEST_ASSERT_TRUE(library_cover_cache_step(&cache, true));
    }
    TEST_ASSERT_TRUE(library_cover_cache_lookup(&cache, 101ull) == NULL);
    TEST_ASSERT_TRUE(library_cover_cache_lookup(&cache, 100ull + LIBRARY_COVER_LRU_SLOTS + 2ull) !=
                     NULL);

    library_cover_cache_destroy(&cache);
    (void)system(rm_cmd);
}

static uint64_t fake_precache_clock_ns(void *context) {
    uint64_t *tick = (uint64_t *)context;
    *tick += 1000000ull;
    return *tick;
}

static void test_boot_cover_precache_warm_and_cold(void) {
    char root_dir[256];
    snprintf(root_dir, sizeof(root_dir), "/tmp/dialos_cov_pre_%d", (int)getpid());
    char rm_cmd[320];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", root_dir);
    (void)system(rm_cmd);
    mkdir(root_dir, 0755);

    char covers_dir[300];
    snprintf(covers_dir, sizeof(covers_dir), "%s/covers", root_dir);
    uint8_t png_bytes[256];
    size_t png_len = build_synthetic_2x2_red_png(png_bytes, sizeof(png_bytes));
    TEST_ASSERT_TRUE(png_len > 0u);

    char cover_a[300], cover_b[300];
    snprintf(cover_a, sizeof(cover_a), "%s/a.png", root_dir);
    snprintf(cover_b, sizeof(cover_b), "%s/b.png", root_dir);
    FILE *file_a = fopen(cover_a, "wb");
    FILE *file_b = fopen(cover_b, "wb");
    TEST_ASSERT_TRUE(file_a != NULL && file_b != NULL);
    fwrite(png_bytes, 1u, png_len, file_a);
    fwrite(png_bytes, 1u, png_len, file_b);
    fclose(file_a);
    fclose(file_b);

    struct library_catalog catalog;
    library_catalog_initialize(&catalog);
    struct library_track_input input_a = {
        .file_path = "/music/a/1.mp3",
        .title = "Song A",
        .album = "Album A",
        .artist = "Artist A",
        .cover_source_kind = LIBRARY_COVER_SIDECAR,
        .cover_path = cover_a,
    };
    struct library_track_input input_b = {
        .file_path = "/music/b/1.mp3",
        .title = "Song B",
        .album = "Album B",
        .artist = "Artist B",
        .cover_source_kind = LIBRARY_COVER_SIDECAR,
        .cover_path = cover_b,
    };
    struct library_track_input input_c = {
        .file_path = "/music/c/1.mp3",
        .title = "Song C",
        .album = "Album C",
        .artist = "Artist C",
        .cover_source_kind = LIBRARY_COVER_NONE,
    };
    TEST_ASSERT_TRUE(library_catalog_add_track(&catalog, &input_a));
    TEST_ASSERT_TRUE(library_catalog_add_track(&catalog, &input_b));
    TEST_ASSERT_TRUE(library_catalog_add_track(&catalog, &input_c));

    struct library_cover_cache cache;
    library_cover_cache_initialize(&cache, covers_dir);

    /* Cold precache on background pthread: poll until finished */
    uint64_t clock_ns = 0ull;
    library_cover_cache_begin_boot_precache(&cache, &catalog);
    TEST_ASSERT_TRUE(library_cover_cache_is_boot_precaching(&cache));
    TEST_ASSERT_EQ_U32(2u, cache.precache_total);

    for (uint32_t spin = 0u; spin < 500u && library_cover_cache_is_boot_precaching(&cache);
         spin++) {
        (void)library_cover_cache_step_boot_precache(&cache, &catalog, fake_precache_clock_ns,
                                                   &clock_ns, LIBRARY_COVER_PRECACHE_STAT_BUDGET_NS);
        usleep(1000u);
    }
    TEST_ASSERT_FALSE(library_cover_cache_is_boot_precaching(&cache));
    TEST_ASSERT_EQ_U32(2u, cache.precache_done);
    TEST_ASSERT_EQ_U32(2u, cache.precache_decoded);
    TEST_ASSERT_EQ_U32(0u, cache.precache_disk_hits);

    /* Warm precache: skips JPEG/PNG decode, hits disk and preloads RAM */
    library_cover_cache_clear(&cache);
    library_cover_cache_begin_boot_precache(&cache, &catalog);
    for (uint32_t spin = 0u; spin < 500u && library_cover_cache_is_boot_precaching(&cache);
         spin++) {
        (void)library_cover_cache_step_boot_precache(&cache, &catalog, fake_precache_clock_ns,
                                                   &clock_ns, LIBRARY_COVER_PRECACHE_STAT_BUDGET_NS);
        usleep(1000u);
    }
    TEST_ASSERT_FALSE(library_cover_cache_is_boot_precaching(&cache));
    TEST_ASSERT_EQ_U32(0u, cache.precache_decoded);
    TEST_ASSERT_EQ_U32(2u, cache.precache_disk_hits);
    TEST_ASSERT_EQ_U32(2u, cache.precache_ram_preloaded);

    library_cover_cache_destroy(&cache);
    library_catalog_destroy(&catalog);
    (void)system(rm_cmd);
}

static void test_jpeg_fast_thumb_large_baseline_decode(void) {
    char jpg_path[256];
    snprintf(jpg_path, sizeof(jpg_path), "/tmp/dialos_fast_jpg_%d.jpg", (int)getpid());
    FILE *file = fopen(jpg_path, "wb");
    TEST_ASSERT_TRUE(file != NULL);

    /* Minimal valid 960x960 grayscale baseline SOF0 JPEG (14,400 blocks * 2 zero bits) */
    static const uint8_t JPEG_HEADER[] = {
        0xFFu, 0xD8u,                                           /* SOI */
        0xFFu, 0xDBu, 0x00u, 0x43u, 0x00u,                      /* DQT id=0, 8-bit */
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, /* 64 quant entries */
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
        0xFFu, 0xC4u, 0x00u, 0x14u, 0x00u,                      /* DHT DC id=0: 1 code '0' -> 0 */
        0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0xFFu, 0xC4u, 0x00u, 0x14u, 0x10u,                      /* DHT AC id=0: 1 code '0' -> EOB */
        0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0xFFu, 0xC0u, 0x00u, 0x0Bu, 0x08u, 0x03u, 0xC0u, 0x03u, 0xC0u, /* SOF0 960x960 */
        0x01u, 0x01u, 0x11u, 0x00u,
        0xFFu, 0xDAu, 0x00u, 0x08u, 0x01u, 0x01u, 0x00u, 0x00u, 0x3Fu, 0x00u, /* SOS */
    };
    fwrite(JPEG_HEADER, 1u, sizeof(JPEG_HEADER), file);
    uint8_t zero_scan[3600];
    memset(zero_scan, 0, sizeof(zero_scan));
    fwrite(zero_scan, 1u, sizeof(zero_scan), file);
    static const uint8_t EOI[2] = {0xFFu, 0xD9u};
    fwrite(EOI, 1u, 2u, file);
    fclose(file);

    uint16_t rgb565[LIBRARY_COVER_PIXEL_COUNT];
    memset(rgb565, 0, sizeof(rgb565));
    TEST_ASSERT_TRUE(jpeg_fast_thumb_decode_file(jpg_path, 0u, 0u, rgb565, LIBRARY_COVER_SIZE_PX));
    /* luma=128 -> R=16, G=32, B=16 -> 0x8410 */
    TEST_ASSERT_EQ_U32(0x8410u, rgb565[0]);
    TEST_ASSERT_EQ_U32(0x8410u, rgb565[LIBRARY_COVER_PIXEL_COUNT - 1u]);
    unlink(jpg_path);
}

void run_library_cover_cache_tests(void) {
    printf("Suite: library_cover_cache\n");
    RUN_TEST(test_cover_decode_disk_cache_and_lru);
    RUN_TEST(test_boot_cover_precache_warm_and_cold);
    RUN_TEST(test_jpeg_fast_thumb_large_baseline_decode);
}
