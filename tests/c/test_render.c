#include "test_runner.h"

#include "boot_sequence.h"
#include "constants.h"
#include "font_atlas.h"
#include "render.h"
#include "theme.h"

#include <stdlib.h>
#include <string.h>

static void assert_bezel_clip_and_content_present(const uint32_t *pixels, uint32_t stride_pixels,
                                                  uint32_t expected_canvas_color) {
    bool found_foreground = false;

    for (uint32_t logical_y = 0u; logical_y < SCREEN_HEIGHT; logical_y++) {
        uint32_t physical_y = render_map_coordinate_180(logical_y);
        for (uint32_t logical_x = 0u; logical_x < SCREEN_WIDTH; logical_x++) {
            uint32_t physical_x = render_map_coordinate_180(logical_x);
            uint32_t pixel = pixels[physical_y * stride_pixels + physical_x];

            if (!render_is_inside_bezel_circle((int32_t)logical_x, (int32_t)logical_y)) {
                TEST_ASSERT_EQ_U32(COLOR_OUTSIDE_BEZEL, pixel);
            } else if (pixel != expected_canvas_color) {
                found_foreground = true;
            }
        }
    }

    TEST_ASSERT_TRUE(found_foreground);
}

static void test_coordinate_mapping_and_circles(void) {
    TEST_ASSERT_EQ_U32(359u, render_map_coordinate_180(0u));
    TEST_ASSERT_EQ_U32(179u, render_map_coordinate_180(180u));
    TEST_ASSERT_EQ_U32(0u, render_map_coordinate_180(359u));

    TEST_ASSERT_TRUE(render_is_inside_bezel_circle(180, 180));
    TEST_ASSERT_TRUE(render_is_inside_bezel_circle(180 + DISPLAY_BEZEL_RADIUS, 180));
    TEST_ASSERT_FALSE(render_is_inside_bezel_circle(0, 0));

    TEST_ASSERT_TRUE(render_is_inside_safe_circle(180, 180));
    TEST_ASSERT_TRUE(render_is_inside_safe_circle(180 + SAFE_CIRCLE_RADIUS, 180));
    TEST_ASSERT_FALSE(render_is_inside_safe_circle(180 + SAFE_CIRCLE_RADIUS + 1, 180));
    TEST_ASSERT_FALSE(render_is_inside_safe_circle(0, 0));
}

static void test_theme_palettes_and_font_atlas(void) {
    const struct appearance_palette *dark = theme_get_palette(APPEARANCE_MODE_DARK);
    const struct appearance_palette *light = theme_get_palette(APPEARANCE_MODE_LIGHT);
    TEST_ASSERT_TRUE(dark != NULL && light != NULL);
    TEST_ASSERT_EQ_U32(0xFF060709u, dark->canvas);
    TEST_ASSERT_EQ_U32(0xFFC0FE05u, dark->signal_highlight);
    TEST_ASSERT_EQ_U32(0xFFE6E8EBu, light->canvas);
    TEST_ASSERT_EQ_U32(0xFFE85A24u, light->signal_highlight);

    const struct bitmap_font *mono_10 = font_atlas_get(FONT_STYLE_MONO_10);
    const struct bitmap_font *display_32 = font_atlas_get(FONT_STYLE_DISPLAY_32);
    TEST_ASSERT_TRUE(mono_10 != NULL && display_32 != NULL);
    TEST_ASSERT_TRUE(font_atlas_measure_width(display_32, "DIAL") > 0);
    TEST_ASSERT_TRUE(font_atlas_measure_width(mono_10, "V1.0 \xC2\xB7 AUDIO OK") > 0);

    size_t offset = 0u;
    TEST_ASSERT_EQ_U32((uint32_t)'A', font_atlas_decode_utf8_next("A\xC2\xB7", &offset));
    TEST_ASSERT_EQ_U32(CODEPOINT_MIDDLE_DOT, font_atlas_decode_utf8_next("A\xC2\xB7", &offset));
    TEST_ASSERT_EQ_U32(0u, font_atlas_decode_utf8_next("A\xC2\xB7", &offset));
}

static void test_all_screens_dark_and_light_with_custom_stride(void) {
    const uint32_t stride_pixels = 384u;
    const uint32_t canary_value = 0xDEADBEEFu;
    size_t buffer_pixels = (size_t)stride_pixels * SCREEN_HEIGHT;
    uint32_t *pixels = malloc(buffer_pixels * sizeof(uint32_t));
    TEST_ASSERT_TRUE(pixels != NULL);

    for (size_t index = 0u; index < buffer_pixels; index++) {
        pixels[index] = canary_value;
    }

    struct framebuffer_surface surface = {pixels, stride_pixels};

    static const enum appearance_mode MODES[2] = {APPEARANCE_MODE_DARK, APPEARANCE_MODE_LIGHT};
    for (uint32_t mode_index = 0u; mode_index < 2u; mode_index++) {
        enum appearance_mode mode = MODES[mode_index];
        uint32_t canvas = theme_get_palette(mode)->canvas;

        struct boot_render_snapshot wake_snapshot = {
            .phase = BOOT_PHASE_WAKE,
            .act_progress = 0.45f,
            .elapsed_seconds = 0.43f,
            .status_label = "MEM // CHECK",
        };
        render_draw_boot_frame(&surface, &wake_snapshot, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);
        TEST_ASSERT_EQ_U32(canary_value, pixels[SCREEN_WIDTH]);

        struct boot_render_snapshot buffer_snapshot = {
            .phase = BOOT_PHASE_BUFFER,
            .act_progress = 0.55f,
            .elapsed_seconds = 2.02f,
            .sweep_progress = 0.55f,
            .status_label = "WAIT // AUDIO ENGINE",
        };
        render_draw_boot_frame(&surface, &buffer_snapshot, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        struct boot_render_snapshot lock_snapshot = {
            .phase = BOOT_PHASE_SIGNAL_LOCK,
            .act_progress = 0.65f,
            .elapsed_seconds = 3.58f,
            .sweep_progress = 1.0f,
            .status_label = "LOCK // SYSTEM READY",
            .summary_line = "V1.0 \xC2\xB7 AUDIO OK \xC2\xB7 SD MOUNTED",
        };
        render_draw_boot_frame(&surface, &lock_snapshot, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        struct boot_render_snapshot handoff_snapshot = {
            .phase = BOOT_PHASE_HANDOFF,
            .act_progress = 1.0f,
            .handoff_progress = 0.6f,
            .elapsed_seconds = 4.28f,
            .sweep_progress = 1.0f,
            .status_label = "LOCK // SYSTEM READY",
            .summary_line = "V1.0 \xC2\xB7 AUDIO OK \xC2\xB7 NO SD",
        };
        render_draw_boot_frame(&surface, &handoff_snapshot, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        struct boot_render_snapshot warning_snapshot = {
            .phase = BOOT_PHASE_WARNING,
            .act_progress = 1.0f,
            .elapsed_seconds = 14.2f,
            .warning_elapsed_seconds = 2.2f,
            .sweep_progress = 0.92f,
            .status_label = "POLLING /UI BUS",
        };
        render_draw_boot_frame(&surface, &warning_snapshot, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        struct ready_screen_status ready_status = {true, 85u, 48u, 85u, true};
        render_draw_ready_frame(&surface, &ready_status, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        struct ready_screen_status ready_missing_battery = {false, 0u, 0u, 120u, false};
        render_draw_ready_frame(&surface, &ready_missing_battery, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        render_draw_crash_frame(&surface, 3u, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        render_draw_shutdown_frame(&surface, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);
    }

    free(pixels);
}

void run_render_tests(void) {
    printf("Suite: render\n");
    RUN_TEST(test_coordinate_mapping_and_circles);
    RUN_TEST(test_theme_palettes_and_font_atlas);
    RUN_TEST(test_all_screens_dark_and_light_with_custom_stride);
}
