#include "test_runner.h"

#include "boot_sequence.h"
#include "constants.h"
#include "font_atlas.h"
#include "home_screen.h"
#include "library_controller.h"
#include "library_db.h"
#include "render.h"
#include "render_home.h"
#include "render_library.h"
#include "render_primitives.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void assert_full_canvas_and_content_present(const uint32_t *pixels, uint32_t stride_pixels,
                                                   uint32_t expected_canvas_color) {
    bool found_foreground = false;

    for (uint32_t logical_y = 0u; logical_y < SCREEN_HEIGHT; logical_y++) {
        uint32_t physical_y = render_map_coordinate_180(logical_y);
        for (uint32_t logical_x = 0u; logical_x < SCREEN_WIDTH; logical_x++) {
            uint32_t physical_x = render_map_coordinate_180(logical_x);
            uint32_t pixel = pixels[physical_y * stride_pixels + physical_x];

            if (pixel != expected_canvas_color) {
                found_foreground = true;
            }
        }
    }

    /* Corners must be opaque (no black software bezel cutout) */
    uint32_t corner_coords[4][2] = {{0u, 0u}, {359u, 0u}, {0u, 359u}, {359u, 359u}};
    for (uint32_t corner = 0u; corner < 4u; corner++) {
        uint32_t px = render_map_coordinate_180(corner_coords[corner][0]);
        uint32_t py = render_map_coordinate_180(corner_coords[corner][1]);
        TEST_ASSERT_TRUE(pixels[py * stride_pixels + px] != COLOR_OUTSIDE_BEZEL);
    }

    TEST_ASSERT_TRUE(found_foreground);
}

#define assert_bezel_clip_and_content_present assert_full_canvas_and_content_present

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

    /* Latin-1 Supplement + typographic punctuation + new Library font styles */
    const char *latin1_text = "\xC3\x89\xE2\x80\x93\xE2\x80\x94\xE2\x80\x99";
    size_t latin1_offset = 0u;
    TEST_ASSERT_EQ_U32(0x00C9u, font_atlas_decode_utf8_next(latin1_text, &latin1_offset));
    TEST_ASSERT_EQ_U32(0x2013u, font_atlas_decode_utf8_next(latin1_text, &latin1_offset));
    TEST_ASSERT_EQ_U32(0x2014u, font_atlas_decode_utf8_next(latin1_text, &latin1_offset));
    TEST_ASSERT_EQ_U32(0x2019u, font_atlas_decode_utf8_next(latin1_text, &latin1_offset));
    TEST_ASSERT_EQ_U32(0u, font_atlas_decode_utf8_next(latin1_text, &latin1_offset));

    const struct bitmap_font *mono_8 = font_atlas_get(FONT_STYLE_MONO_8);
    const struct bitmap_font *mono_9 = font_atlas_get(FONT_STYLE_MONO_9);
    const struct bitmap_font *display_11 = font_atlas_get(FONT_STYLE_DISPLAY_11);
    const struct bitmap_font *display_13 = font_atlas_get(FONT_STYLE_DISPLAY_13);
    const struct bitmap_font *display_15 = font_atlas_get(FONT_STYLE_DISPLAY_15);
    const struct bitmap_font *display_46 = font_atlas_get(FONT_STYLE_DISPLAY_46);
    TEST_ASSERT_TRUE(mono_8 != NULL && mono_9 != NULL);
    TEST_ASSERT_TRUE(display_11 != NULL && display_13 != NULL && display_15 != NULL &&
                     display_46 != NULL);
    TEST_ASSERT_EQ_U32(0x00C9u, font_atlas_lookup_glyph(display_15, 0x00C9u)->codepoint);
    TEST_ASSERT_EQ_U32(0x2013u, font_atlas_lookup_glyph(mono_9, 0x2013u)->codepoint);
    TEST_ASSERT_EQ_U32((uint32_t)'A', font_atlas_lookup_glyph(display_46, (uint32_t)'A')->codepoint);
    /* Unsupported codepoint falls back to '?' */
    TEST_ASSERT_EQ_U32((uint32_t)'?', font_atlas_lookup_glyph(display_15, 0x4E2Du)->codepoint);
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
    render_draw_line_fast(&surface, 80.0f, 80.0f, 280.0f, 280.0f, dark->hairline_strong, 0.6f);
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
        assert_full_canvas_and_content_present(pixels, stride_pixels, canvas);
        /* Top handle bar removed: pixel at (180, 25) must remain canvas */
        TEST_ASSERT_EQ_U32(
            canvas,
            pixels[render_map_coordinate_180(25u) * stride_pixels + render_map_coordinate_180(180u)]);

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

static void test_library_2d_primitives_and_blits(void) {
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

    /* 1. Clipped text and marquee must stay strictly within clip_rect */
    render_fill_canvas(&surface, dark->canvas);
    struct clip_rect text_clip = {140, 170, 220, 190};
    render_draw_text_rect_clipped_alpha(&surface, FONT_STYLE_DISPLAY_15, "CLIPPED Y-BAND",
                                        180.0f, 180.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                                        &text_clip, dark->ink_primary, 1.0f);
    render_draw_marquee_text_rect_clipped(&surface, FONT_STYLE_MONO_9,
                                          "LONG MARQUEE INSIDE CLIP RECT TEST", 180.0f, 180.0f,
                                          70.0f, 1.4f, &text_clip, dark->signal_highlight, 1.0f);
    for (uint32_t logical_y = 150u; logical_y < 170u; logical_y++) {
        uint32_t physical_y = render_map_coordinate_180(logical_y);
        for (uint32_t logical_x = 120u; logical_x < 240u; logical_x++) {
            uint32_t physical_x = render_map_coordinate_180(logical_x);
            TEST_ASSERT_EQ_U32(dark->canvas, pixels[physical_y * stride_pixels + physical_x]);
        }
    }

    /* 2. Clipped rect, chamfer plate, corner brackets */
    struct clip_rect shape_clip = {100, 100, 200, 150};
    render_draw_filled_rect_clipped_alpha(&surface, 80.0f, 80.0f, 160.0f, 120.0f, &shape_clip,
                                          dark->surface, 0.9f);
    render_draw_chamfer_plate_filled_clipped_aa(&surface, 90.0f, 90.0f, 140.0f, 100.0f, 8.0f,
                                                &shape_clip, dark->signal_highlight, 0.3f);
    render_draw_rect_corner_brackets_clipped(&surface, 90.0f, 90.0f, 140.0f, 100.0f, 10.0f, 2.0f,
                                             &shape_clip, dark->signal_highlight, 1.0f);
    TEST_ASSERT_EQ_U32(dark->canvas,
                       pixels[render_map_coordinate_180(155u) * stride_pixels +
                              render_map_coordinate_180(150u)]);

    /* 3. Annular wedge and hearts */
    render_fill_canvas(&surface, dark->canvas);
    render_draw_annular_wedge_fast(&surface, 180.0f, 180.0f, 120.0f, 140.0f, -0.5f, 0.5f,
                                   dark->signal_highlight, 0.8f);
    render_draw_heart_filled_aa(&surface, 180.0f, 180.0f, 16.0f, dark->signal_highlight, 1.0f);
    render_draw_heart_stroked_aa(&surface, 220.0f, 180.0f, 14.0f, 1.8f, dark->ink_primary, 1.0f);
    assert_bezel_clip_and_content_present(pixels, stride_pixels, dark->canvas);

    /* 4. RGB565 scaled clipped blit (180-deg inverted + clip respected) */
    uint16_t cover_pixels[4 * 4];
    for (uint32_t idx = 0u; idx < 16u; idx++) {
        cover_pixels[idx] = 0xF800u; /* pure red in RGB565 */
    }
    render_fill_canvas(&surface, dark->canvas);
    struct clip_rect blit_clip = {160, 160, 190, 190};
    render_blit_rgb565_scaled_clipped(&surface, cover_pixels, 4u, 4u, 150.0f, 150.0f, 60.0f, 60.0f,
                                      &blit_clip, 1.0f);
    uint32_t inside_px = pixels[render_map_coordinate_180(175u) * stride_pixels +
                                render_map_coordinate_180(175u)];
    uint32_t outside_px = pixels[render_map_coordinate_180(155u) * stride_pixels +
                                 render_map_coordinate_180(155u)];
    TEST_ASSERT_EQ_U32(0xFFFF0000u, inside_px);
    TEST_ASSERT_EQ_U32(dark->canvas, outside_px);

    /* 5. RGB565 rotated circular blit */
    render_fill_canvas(&surface, dark->canvas);
    render_blit_rgb565_circle_rotated(&surface, cover_pixels, 4u, 4u, 180.0f, 180.0f, 32.0f,
                                      0.75f, 1.0f);
    uint32_t center_px = pixels[render_map_coordinate_180(180u) * stride_pixels +
                                render_map_coordinate_180(180u)];
    uint32_t far_px = pixels[render_map_coordinate_180(120u) * stride_pixels +
                             render_map_coordinate_180(120u)];
    TEST_ASSERT_EQ_U32(0xFFFF0000u, center_px);
    TEST_ASSERT_EQ_U32(dark->canvas, far_px);
    TEST_ASSERT_EQ_U32(canary_value, pixels[SCREEN_WIDTH]);

    free(pixels);
}

static void test_library_screens_dark_and_light_with_custom_stride(void) {
    const uint32_t stride_pixels = 384u;
    const uint32_t canary_value = 0xDEADBEEFu;
    size_t buffer_pixels = (size_t)stride_pixels * SCREEN_HEIGHT;
    uint32_t *pixels = malloc(buffer_pixels * sizeof(uint32_t));
    TEST_ASSERT_TRUE(pixels != NULL);
    for (size_t index = 0u; index < buffer_pixels; index++) {
        pixels[index] = canary_value;
    }
    struct framebuffer_surface surface = {pixels, stride_pixels};

    char db_dir[128];
    snprintf(db_dir, sizeof(db_dir), "/tmp/dialos_render_test_%d", (int)getpid());
    char rm_cmd[160];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", db_dir);
    (void)system(rm_cmd);
    mkdir(db_dir, 0755);
    struct library_db db;
    TEST_ASSERT_TRUE(library_db_open(&db, db_dir));
    struct library_db_track_input tracks[3] = {
        {.path = "/sd/1.flac", .title = "First Song", .artist = "Artist A", .album = "Prism LP",
         .genre = "House", .track_number = 1u, .duration_seconds = 120u,
         .mtime_seconds = 1700000000ll, .audio_fingerprint = 0x101ull},
        {.path = "/sd/2.flac", .title = "Second Song", .artist = "Artist B", .album = "Grid LP",
         .genre = "Dub", .track_number = 1u, .duration_seconds = 150u,
         .mtime_seconds = 1710000000ll, .audio_fingerprint = 0x102ull},
        {.path = "/sd/3.flac", .title = "Third Song", .artist = "Artist C", .album = "Tape LP",
         .genre = "Ambient", .track_number = 1u, .duration_seconds = 180u,
         .mtime_seconds = 1720000000ll, .audio_fingerprint = 0x103ull},
    };
    int64_t first_track_id = 0;
    for (uint32_t idx = 0u; idx < 3u; idx++) {
        int64_t inserted_id = 0;
        TEST_ASSERT_TRUE(library_db_upsert_track(&db, &tracks[idx], 0, &inserted_id));
        if (idx == 0u) {
            first_track_id = inserted_id;
        }
    }
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&db, NULL, 0u, NULL));
    const char *pl_paths[1] = {"/sd/1.flac"};
    TEST_ASSERT_TRUE(library_db_upsert_m3u_playlist(&db, "Mix 1", "/sd/mix1.m3u", pl_paths, 1u));
    struct library_db_collection albums_col = {LIBRARY_DB_COLLECTION_ALBUMS, 0};
    struct library_db_row album_rows[4];
    TEST_ASSERT_EQ_U32(3u, library_db_fetch_window(&db, &albums_col, LIBRARY_SORT_ALPHABETICAL, 0u,
                                                   4u, album_rows));
    TEST_ASSERT_TRUE(
        library_db_toggle_favorite(&db, LIBRARY_DB_FAVORITE_TRACK, first_track_id, NULL));
    TEST_ASSERT_TRUE(
        library_db_toggle_favorite(&db, LIBRARY_DB_FAVORITE_ALBUM, album_rows[0].id, NULL));

    struct library_controller ctrl;
    library_controller_initialize(&ctrl, &db, NULL);
    library_controller_set_active_playback(&ctrl, "/sd/1.flac", true);

    static const enum appearance_mode MODES[2] = {APPEARANCE_MODE_DARK, APPEARANCE_MODE_LIGHT};
    for (uint32_t m_idx = 0u; m_idx < 2u; m_idx++) {
        enum appearance_mode mode = MODES[m_idx];
        uint32_t canvas = theme_get_palette(mode)->canvas;

        /* Hub + Dive transition */
        library_controller_reset_to_hub(&ctrl, true);
        render_library_hub_transition_layer(&surface, &ctrl, mode, 0.7f);
        render_draw_library_frame(&surface, &ctrl, mode, 0.35f);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        /* Albums Vinyl Crate + Sort Wheel + Sort Toast + Quick Actions */
        struct library_controller_action action;
        ctrl.view_stack[0].scroll_position = 1.0f;
        ctrl.view_stack[0].target_index = 1;
        struct touch_event d = {TOUCH_EVENT_DOWN, 244, 152, 100ull};
        struct touch_event u = {TOUCH_EVENT_UP, 244, 152, 140ull};
        library_controller_handle_touch(&ctrl, &d, &action);
        library_controller_handle_touch(&ctrl, &u, &action);
        ctrl.bloom_progress = 1.0f;
        ctrl.sort_wheel_open = true;
        ctrl.sort_wheel_progress = 1.0f;
        ctrl.sort_wheel_anchor_x = LIBRARY_CRATE_SORT_BTN_X;
        ctrl.sort_wheel_anchor_y = LIBRARY_CRATE_SORT_BTN_Y;
        ctrl.sort_wheel_hovered_slot = 2;
        ctrl.sort_toast_active = true;
        ctrl.sort_toast_remaining_seconds = 1.0f;
        snprintf(ctrl.sort_toast_text, sizeof(ctrl.sort_toast_text), "SORT \xC2\xB7 LIKED FIRST");
        render_draw_library_frame(&surface, &ctrl, mode, 0.0f);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        ctrl.sort_wheel_open = false;
        ctrl.sort_wheel_progress = 0.0f;
        ctrl.quick_actions_open = true;
        ctrl.quick_actions_progress = 1.0f;
        ctrl.quick_action_kind = LIB_QA_ALBUM;
        {
            const struct library_db_row *qa_row = library_controller_get_row(&ctrl, 0u);
            if (qa_row != NULL) {
                ctrl.quick_action_row = *qa_row;
            }
        }
        render_draw_library_frame(&surface, &ctrl, mode, 0.0f);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);
        ctrl.quick_actions_open = false;
        ctrl.quick_actions_progress = 0.0f;

        /* Album Tracklist Crystal Overlay + Track Quick Actions */
        struct touch_event nd = {TOUCH_EVENT_DOWN, 208, 325, 200ull};
        struct touch_event nu = {TOUCH_EVENT_UP, 208, 325, 240ull};
        library_controller_handle_touch(&ctrl, &nd, &action);
        library_controller_handle_touch(&ctrl, &nu, &action);
        ctrl.transition_progress = 1.0f;
        render_draw_library_frame(&surface, &ctrl, mode, 0.0f);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);

        ctrl.quick_actions_open = true;
        ctrl.quick_actions_progress = 1.0f;
        ctrl.quick_action_kind = LIB_QA_TRACK;
        {
            const struct library_db_row *qa_row = library_controller_get_row(&ctrl, 0u);
            if (qa_row != NULL) {
                ctrl.quick_action_row = *qa_row;
            }
        }
        render_draw_library_frame(&surface, &ctrl, mode, 0.0f);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);
        ctrl.quick_actions_open = false;
        ctrl.quick_actions_progress = 0.0f;

        /* Favorites -> ALBUMS with 4-slot Radial Sort Wheel */
        library_controller_reset_to_hub(&ctrl, true);
        struct touch_event fd = {TOUCH_EVENT_DOWN, 116, 152, 260ull};
        struct touch_event fu = {TOUCH_EVENT_UP, 116, 152, 290ull};
        library_controller_handle_touch(&ctrl, &fd, &action);
        library_controller_handle_touch(&ctrl, &fu, &action);
        ctrl.transition_progress = 1.0f;
        ctrl.sort_wheel_open = true;
        ctrl.sort_wheel_progress = 1.0f;
        ctrl.sort_wheel_anchor_x = LIBRARY_CRATE_SORT_BTN_X;
        ctrl.sort_wheel_anchor_y = LIBRARY_CRATE_SORT_BTN_Y;
        ctrl.sort_wheel_hovered_slot = 2;
        render_draw_library_frame(&surface, &ctrl, mode, 0.0f);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);
        ctrl.sort_wheel_open = false;
        ctrl.sort_wheel_progress = 0.0f;

        /* Playlists + Action Sheet + Hold-to-Delete arc */
        library_controller_reset_to_hub(&ctrl, true);
        struct touch_event pd = {TOUCH_EVENT_DOWN, 244, 152, 300ull};
        struct touch_event pu = {TOUCH_EVENT_UP, 244, 152, 340ull};
        library_controller_handle_touch(&ctrl, &pd, &action);
        library_controller_handle_touch(&ctrl, &pu, &action);
        ctrl.playlist_sheet_open = true;
        ctrl.playlist_sheet_progress = 1.0f;
        ctrl.delete_hold_progress = 0.55f;
        ctrl.seek_hud_alpha = 1.0f;
        ctrl.rim_slice_pulls[3] = 1.0f;
        render_draw_library_frame(&surface, &ctrl, mode, 0.0f);
        assert_bezel_clip_and_content_present(pixels, stride_pixels, canvas);
        TEST_ASSERT_EQ_U32(canary_value, pixels[SCREEN_WIDTH]);
    }

    library_controller_destroy(&ctrl);
    library_db_close(&db);
    (void)system(rm_cmd);
    free(pixels);
}

static void test_span_optimized_rect_and_circle_parity(void) {
    const uint32_t stride_pixels = 384u;
    size_t buffer_pixels = (size_t)stride_pixels * SCREEN_HEIGHT;
    uint32_t *pixels = malloc(buffer_pixels * sizeof(uint32_t));
    TEST_ASSERT_TRUE(pixels != NULL);
    struct framebuffer_surface surface = {pixels, stride_pixels};

    render_fill_canvas(&surface, 0xFF000000u);
    render_draw_filled_rect_alpha(&surface, 100.0f, 100.0f, 80.0f, 60.0f, 0xFFFFFFFFu, 0.5f);
    uint32_t center_pixel =
        pixels[render_map_coordinate_180(130u) * stride_pixels + render_map_coordinate_180(140u)];
    TEST_ASSERT_TRUE((center_pixel & 0xFFu) >= 126u && (center_pixel & 0xFFu) <= 130u);

    render_fill_canvas(&surface, 0xFF000000u);
    render_draw_filled_circle_aa(&surface, 180.0f, 180.0f, 40.0f, 0xFFFFFFFFu, 1.0f);
    uint32_t circle_center =
        pixels[render_map_coordinate_180(180u) * stride_pixels + render_map_coordinate_180(180u)];
    uint32_t circle_outside =
        pixels[render_map_coordinate_180(180u) * stride_pixels + render_map_coordinate_180(135u)];
    TEST_ASSERT_EQ_U32(0xFFFFFFFFu, circle_center);
    TEST_ASSERT_EQ_U32(0xFF000000u, circle_outside);
    free(pixels);
}

void run_render_tests(void) {
    printf("Suite: render\n");
    RUN_TEST(test_coordinate_mapping_and_circles);
    RUN_TEST(test_theme_palettes_and_font_atlas);
    RUN_TEST(test_all_screens_dark_and_light_with_custom_stride);
    RUN_TEST(test_new_primitives_and_clipping);
    RUN_TEST(test_home_screens_dark_and_light_with_custom_stride);
    RUN_TEST(test_library_2d_primitives_and_blits);
    RUN_TEST(test_library_screens_dark_and_light_with_custom_stride);
    RUN_TEST(test_span_optimized_rect_and_circle_parity);
}
