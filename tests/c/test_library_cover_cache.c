#include "test_runner.h"

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

    /* Delete original sidecar and reset RAM LRU; disk .rgb565 loads even with heavy=false */
    unlink(sidecar_path);
    library_cover_cache_clear(&cache);
    TEST_ASSERT_TRUE(library_cover_cache_lookup(&cache, 0x1111ull) == NULL);
    TEST_ASSERT_TRUE(library_cover_cache_request(&cache, 0x1111ull, LIBRARY_COVER_SIDECAR,
                                                 sidecar_path, 0u, 0u));
    TEST_ASSERT_TRUE(library_cover_cache_step(&cache, false));
    TEST_ASSERT_TRUE(library_cover_cache_lookup(&cache, 0x1111ull) != NULL);

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

    /* LRU eviction past 24 slots */
    library_cover_cache_clear(&cache);
    for (uint64_t idx = 1ull; idx <= 26ull; idx++) {
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
    TEST_ASSERT_TRUE(library_cover_cache_lookup(&cache, 126ull) != NULL);

    library_cover_cache_destroy(&cache);
    (void)system(rm_cmd);
}

void run_library_cover_cache_tests(void) {
    printf("Suite: library_cover_cache\n");
    RUN_TEST(test_cover_decode_disk_cache_and_lru);
}
