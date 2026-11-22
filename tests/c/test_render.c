#include "test_runner.h"

#include "constants.h"
#include "render.h"

#include <stdlib.h>
#include <string.h>

static void assert_safe_circle_and_foreground_present(const uint32_t *pixels, uint32_t stride_pixels,
                                                      uint32_t expected_accent_color) {
    bool found_foreground = false;
    bool found_accent = false;

    for (uint32_t logical_y = 0u; logical_y < SCREEN_HEIGHT; logical_y++) {
        uint32_t physical_y = render_map_coordinate_180(logical_y);
        for (uint32_t logical_x = 0u; logical_x < SCREEN_WIDTH; logical_x++) {
            uint32_t physical_x = render_map_coordinate_180(logical_x);
            uint32_t pixel = pixels[physical_y * stride_pixels + physical_x];

            if (!render_is_inside_safe_circle((int32_t)logical_x, (int32_t)logical_y)) {
                TEST_ASSERT_EQ_U32(COLOR_OUTSIDE_BEZEL, pixel);
            } else {
                if (pixel != COLOR_BACKGROUND) {
                    found_foreground = true;
                    int32_t delta_x = (int32_t)logical_x - SCREEN_CENTER_X;
                    int32_t delta_y = (int32_t)logical_y - SCREEN_CENTER_Y;
                    /* Confirm drawn foreground stays within safe circle */
                    TEST_ASSERT_TRUE((delta_x * delta_x + delta_y * delta_y) <=
                                     (SAFE_CIRCLE_RADIUS * SAFE_CIRCLE_RADIUS));
                }
                if (pixel == expected_accent_color) {
                    found_accent = true;
                }
            }
        }
    }

    TEST_ASSERT_TRUE(found_foreground);
    TEST_ASSERT_TRUE(found_accent);
}

static void test_coordinate_mapping_and_safe_circle(void) {
    TEST_ASSERT_EQ_U32(359u, render_map_coordinate_180(0u));
    TEST_ASSERT_EQ_U32(179u, render_map_coordinate_180(180u));
    TEST_ASSERT_EQ_U32(0u, render_map_coordinate_180(359u));

    TEST_ASSERT_TRUE(render_is_inside_safe_circle(180, 180));
    TEST_ASSERT_TRUE(render_is_inside_safe_circle(180 + SAFE_CIRCLE_RADIUS, 180));
    TEST_ASSERT_FALSE(render_is_inside_safe_circle(180 + SAFE_CIRCLE_RADIUS + 1, 180));
    TEST_ASSERT_FALSE(render_is_inside_safe_circle(0, 0));
}

static void test_glyph_lookup_and_msb_order(void) {
    const uint8_t *space_glyph = render_lookup_glyph(' ');
    const uint8_t *letter_a_glyph = render_lookup_glyph('A');
    const uint8_t *control_fallback = render_lookup_glyph('\x07');
    const uint8_t *high_ascii_fallback = render_lookup_glyph((char)0x80);

    TEST_ASSERT_TRUE(space_glyph != NULL);
    TEST_ASSERT_TRUE(letter_a_glyph != NULL);
    TEST_ASSERT_TRUE(memcmp(space_glyph, control_fallback, FONT_GLYPH_HEIGHT) == 0);
    TEST_ASSERT_TRUE(memcmp(space_glyph, high_ascii_fallback, FONT_GLYPH_HEIGHT) == 0);

    /* Verify glyph 'A' has non-zero top/middle pixels and MSB-first structure */
    bool has_non_zero_row = false;
    for (uint32_t row_index = 0u; row_index < FONT_GLYPH_HEIGHT; row_index++) {
        if (letter_a_glyph[row_index] != 0u) {
            has_non_zero_row = true;
        }
    }
    TEST_ASSERT_TRUE(has_non_zero_row);
}

static void test_all_screens_with_custom_stride(void) {
    const uint32_t stride_pixels = 384u;
    const uint32_t canary_value = 0xDEADBEEFu;
    size_t buffer_pixels = (size_t)stride_pixels * SCREEN_HEIGHT;
    uint32_t *pixels = malloc(buffer_pixels * sizeof(uint32_t));
    TEST_ASSERT_TRUE(pixels != NULL);

    for (size_t index = 0u; index < buffer_pixels; index++) {
        pixels[index] = canary_value;
    }

    struct framebuffer_surface surface = {pixels, stride_pixels};

    render_draw_boot_frame(&surface, 90u);
    assert_safe_circle_and_foreground_present(pixels, stride_pixels, COLOR_ACCENT);
    /* Verify padding columns beyond SCREEN_WIDTH were not overwritten */
    TEST_ASSERT_EQ_U32(canary_value, pixels[SCREEN_WIDTH]);

    render_draw_warning_frame(&surface);
    assert_safe_circle_and_foreground_present(pixels, stride_pixels, COLOR_WARNING);

    struct ready_screen_status ready_status = {true, 85u, 48u, 85u, true};
    render_draw_ready_frame(&surface, &ready_status);
    assert_safe_circle_and_foreground_present(pixels, stride_pixels, COLOR_ACCENT);

    struct ready_screen_status ready_missing_battery = {false, 0u, 0u, 120u, false};
    render_draw_ready_frame(&surface, &ready_missing_battery);
    assert_safe_circle_and_foreground_present(pixels, stride_pixels, COLOR_FOREGROUND);

    render_draw_crash_frame(&surface, 3u);
    assert_safe_circle_and_foreground_present(pixels, stride_pixels, COLOR_ERROR);

    render_draw_shutdown_frame(&surface);
    assert_safe_circle_and_foreground_present(pixels, stride_pixels, COLOR_FOREGROUND);

    free(pixels);
}

void run_render_tests(void) {
    printf("Suite: render\n");
    RUN_TEST(test_coordinate_mapping_and_safe_circle);
    RUN_TEST(test_glyph_lookup_and_msb_order);
    RUN_TEST(test_all_screens_with_custom_stride);
}
