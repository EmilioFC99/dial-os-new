#include "test_runner.h"

#include "boot_sequence.h"
#include "constants.h"
#include "font_atlas.h"
#include "home_screen.h"
#include "render.h"
#include "render_home.h"
#include "render_primitives.h"
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
            .buffer_exit_progress = 0.0f,
            .elapsed_seconds = 4.72f,
            .sweep_progress = 0.55f,
            .status_label = "WAIT // AUDIO ENGINE",
        };
        render_draw_boot_frame(&surface, &buffer_snapshot, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        struct boot_render_snapshot buffer_full_snapshot = {
            .phase = BOOT_PHASE_BUFFER,
            .act_progress = 1.0f,
            .buffer_exit_progress = 0.0f,
            .elapsed_seconds = 7.80f,
            .sweep_progress = 1.0f,
            .status_label = "LOCK // SYSTEM READY",
        };
        render_draw_boot_frame(&surface, &buffer_full_snapshot, mode);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        struct boot_render_snapshot buffer_exit_snapshot = {
            .phase = BOOT_PHASE_BUFFER,
            .act_progress = 1.0f,
            .buffer_exit_progress = 0.7f,
            .elapsed_seconds = 8.05f,
            .sweep_progress = 1.0f,
            .status_label = "LOCK // SYSTEM READY",
        };
        render_draw_boot_frame(&surface, &buffer_exit_snapshot, mode);
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

static void test_new_primitives_and_clipping(void) {
    const uint32_t stride_pixels = 384u;
    const uint32_t canary_value = 0xDEADBEEFu;
    size_t buffer_pixels = (size_t)stride_pixels * SCREEN_HEIGHT;
    uint32_t *pixels = malloc(buffer_pixels * sizeof(uint32_t));
    TEST_ASSERT_TRUE(pixels != NULL);

    for (size_t index = 0u; index < buffer_pixels; index++) {
        pixels[index] = canary_value;
    }

    struct framebuffer_surface surface = {pixels, stride_pixels};
    const struct appearance_palette *dark = theme_get_palette(APPEARANCE_MODE_DARK);

    /* Clipped text must not write outside [clip_left_x, clip_right_x] */
    render_fill_canvas(&surface, dark->canvas);
    render_draw_text_clipped_alpha(&surface, FONT_STYLE_MONO_11, "CLIPPED TEXT TEST", 180.0f,
                                   180.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, 160, 200,
                                   dark->ink_primary, 1.0f);
    for (uint32_t logical_y = 160u; logical_y < 200u; logical_y++) {
        uint32_t physical_y = render_map_coordinate_180(logical_y);
        for (uint32_t logical_x = 120u; logical_x < 160u; logical_x++) {
            uint32_t physical_x = render_map_coordinate_180(logical_x);
            TEST_ASSERT_EQ_U32(dark->canvas, pixels[physical_y * stride_pixels + physical_x]);
        }
        for (uint32_t logical_x = 201u; logical_x < 240u; logical_x++) {
            uint32_t physical_x = render_map_coordinate_180(logical_x);
            TEST_ASSERT_EQ_U32(dark->canvas, pixels[physical_y * stride_pixels + physical_x]);
        }
    }

    /* Marquee text (both static and scrolling) + fast polygon + ellipse + 3D box */
    render_draw_marquee_text(&surface, FONT_STYLE_MONO_10, "SHORT", 180.0f, 120.0f, 232.0f, 0.5f,
                             dark->ink_secondary, 1.0f);
    render_draw_marquee_text(&surface, FONT_STYLE_MONO_10,
                             "VERY LONG MARQUEE SUBTITLE THAT EXCEEDS TWO HUNDRED THIRTY TWO PIXELS",
                             180.0f, 220.0f, 232.0f, 1.8f, dark->ink_secondary, 1.0f);

    struct point_2d subject[4] = {{140.0f, 140.0f}, {220.0f, 140.0f}, {220.0f, 220.0f}, {140.0f, 220.0f}};
    struct point_2d clip_quad[4] = {{160.0f, 160.0f}, {200.0f, 160.0f}, {200.0f, 200.0f}, {160.0f, 200.0f}};
    struct point_2d clipped[8];
    uint32_t clipped_count = render_clip_polygon_to_convex_quad(subject, 4u, clip_quad, clipped);
    TEST_ASSERT_EQ_U32(4u, clipped_count);

    render_draw_filled_convex_polygon_fast(&surface, clipped, clipped_count, dark->signal_highlight,
                                           0.45f);
    render_draw_filled_ellipse_aa(&surface, 180.0f, 180.0f, 30.0f, 14.0f, 0.25f, dark->surface,
                                  0.8f);
    render_draw_stroked_ellipse_aa(&surface, 180.0f, 180.0f, 30.0f, 14.0f, 0.25f, 1.5f,
                                   dark->signal_highlight, 0.9f);

    struct rotation_matrix_3x3 matrix = render_build_rotation_matrix_3d(0.2f, 0.4f, 0.05f);
    render_draw_projected_3d_box(&surface, &matrix, 0.0f, 0.0f, 0.0f, 14.0f, 14.0f, 8.0f,
                                 180.0f, 180.0f, &dark->cube_highlight_faces,
                                 dark->cube_edge_stroke, 1.0f);
    assert_bezel_clip_and_content_present(pixels, stride_pixels, dark->canvas);
    TEST_ASSERT_EQ_U32(canary_value, pixels[SCREEN_WIDTH]);

    free(pixels);
}

static void test_home_screens_dark_and_light_with_custom_stride(void) {
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

        struct home_render_snapshot snap;
        memset(&snap, 0, sizeof(snap));
        snap.appearance_mode = mode;
        snap.active_screen = ACTIVE_SCREEN_HOME;
        snap.entry_progress = 1.0f;
        snap.elapsed_seconds = 2.4f;
        snap.telemetry.volume_percent = 52u;
        snap.telemetry.volume_limit_percent = 85u;
        snap.telemetry.battery_valid = true;
        snap.telemetry.battery_percent = 94u;

        /* Boot -> Home cross-dissolve */
        snap.entry_progress = 0.25f;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        /* All 4 wheel slots (including Playing, Paused, Idle, Storage) */
        snap.entry_progress = 1.0f;
        snap.telemetry.playback_mode = HOME_PLAYBACK_PLAYING;
        snprintf(snap.telemetry.track_title, sizeof(snap.telemetry.track_title), "Shakti Sound");
        snap.telemetry.elapsed_ms = 34000u;
        snap.telemetry.duration_ms = 154000u;
        snap.scroll_position = 0.0f;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        snap.telemetry.playback_mode = HOME_PLAYBACK_PAUSED;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        snap.telemetry.playback_mode = HOME_PLAYBACK_STORAGE;
        snap.telemetry.sd_free_valid = true;
        snap.telemetry.sd_free_megabytes = 43622ull;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        for (uint32_t slot = 1u; slot <= 3u; slot++) {
            snap.scroll_position = (float)slot;
            render_draw_home_frame(&surface, &snap);
            assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);
        }

        /* Mid-drag state */
        snap.scroll_position = 0.45f;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        /* Modes & Settings inline previews + left-edge back swipe arc */
        snap.scroll_position = 2.0f;
        snap.launched_app_id = HOME_APP_MODES;
        snap.launch_progress = 1.0f;
        snap.launch_target = 1.0f;
        snap.back_swipe_progress = 0.5f;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        snap.scroll_position = 3.0f;
        snap.launched_app_id = HOME_APP_SETTINGS;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        /* Library Acrylic Cube Dive mid-transition + shell */
        snap.scroll_position = 1.0f;
        snap.launched_app_id = HOME_APP_LIBRARY;
        snap.launch_progress = 0.4f;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        snap.active_screen = ACTIVE_SCREEN_LIBRARY_SHELL;
        snap.launch_progress = 1.0f;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        /* Now Playing Direct Vinyl Zoom + Active Shell + Idle Shell */
        snap.active_screen = ACTIVE_SCREEN_HOME;
        snap.scroll_position = 0.0f;
        snap.launched_app_id = HOME_APP_NOW_PLAYING;
        snap.telemetry.playback_mode = HOME_PLAYBACK_PLAYING;
        snap.launch_progress = 0.55f;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        snap.active_screen = ACTIVE_SCREEN_NOW_PLAYING_SHELL;
        snap.launch_progress = 1.0f;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        snap.telemetry.playback_mode = HOME_PLAYBACK_IDLE;
        render_draw_home_frame(&surface, &snap);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);
        TEST_ASSERT_EQ_U32(canary_value, pixels[SCREEN_WIDTH]);
    }

    free(pixels);
}

void run_render_tests(void) {
    printf("Suite: render\n");
    RUN_TEST(test_coordinate_mapping_and_circles);
    RUN_TEST(test_theme_palettes_and_font_atlas);
    RUN_TEST(test_all_screens_dark_and_light_with_custom_stride);
    RUN_TEST(test_new_primitives_and_clipping);
    RUN_TEST(test_home_screens_dark_and_light_with_custom_stride);
}
