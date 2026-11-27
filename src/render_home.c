#include "render_home.h"

#include "constants.h"
#include "render_library.h"
#include "render_primitives.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LIBRARY_DISC_SEGMENTS 24u
#define CUBE_LAYER1_SKIP_HALF_SIZE 110.0f

static const float UNIT_CIRCLE_COS[LIBRARY_DISC_SEGMENTS] = {
    1.000000f,  0.965926f,  0.866025f,  0.707107f,  0.500000f,  0.258819f,
    0.000000f,  -0.258819f, -0.500000f, -0.707107f, -0.866025f, -0.965926f,
    -1.000000f, -0.965926f, -0.866025f, -0.707107f, -0.500000f, -0.258819f,
    0.000000f,  0.258819f,  0.500000f,  0.707107f,  0.866025f,  0.965926f,
};

static const float UNIT_CIRCLE_SIN[LIBRARY_DISC_SEGMENTS] = {
    0.000000f,  0.258819f,  0.500000f,  0.707107f,  0.866025f,  0.965926f,
    1.000000f,  0.965926f,  0.866025f,  0.707107f,  0.500000f,  0.258819f,
    0.000000f,  -0.258819f, -0.500000f, -0.707107f, -0.866025f, -0.965926f,
    -1.000000f, -0.965926f, -0.866025f, -0.707107f, -0.500000f, -0.258819f,
};

static void format_mm_ss(uint32_t milliseconds, char *buffer, size_t capacity) {
    uint32_t total_seconds = milliseconds / MILLISECONDS_PER_SECOND;
    uint32_t minutes = total_seconds / 60u;
    uint32_t seconds = total_seconds % 60u;
    snprintf(buffer, capacity, "%02u:%02u", (unsigned)minutes, (unsigned)seconds);
}

static void format_comma_u32(uint32_t value, char *buffer, size_t capacity) {
    char raw[16];
    snprintf(raw, sizeof(raw), "%u", (unsigned)value);
    size_t raw_len = strlen(raw);
    size_t write_index = 0u;
    for (size_t index = 0u; index < raw_len && write_index + 1u < capacity; index++) {
        if (index > 0u && ((raw_len - index) % 3u) == 0u && write_index + 2u < capacity) {
            buffer[write_index++] = ',';
        }
        buffer[write_index++] = raw[index];
    }
    buffer[write_index] = '\0';
}

static void format_uppercase_copy(const char *source, char *dest, size_t capacity) {
    if (capacity == 0u) {
        return;
    }
    size_t index = 0u;
    while (source != NULL && source[index] != '\0' && index + 1u < capacity) {
        char character = source[index];
        if (character >= 'a' && character <= 'z') {
            character = (char)(character - ('a' - 'A'));
        }
        dest[index++] = character;
    }
    dest[index] = '\0';
}

static const char *resolve_app_title(enum home_app_id app_id) {
    switch (app_id) {
        case HOME_APP_NOW_PLAYING:
            return "Now Playing";
        case HOME_APP_LIBRARY:
            return "Library";
        case HOME_APP_MODES:
            return "Modes";
        case HOME_APP_SETTINGS:
            return "Settings";
        default:
            return "Now Playing";
    }
}

static void format_app_subtitle(enum home_app_id app_id, const struct home_telemetry *telemetry,
                                enum appearance_mode appearance_mode, char *subtitle_out,
                                size_t capacity) {
    if (app_id == HOME_APP_NOW_PLAYING) {
        if (telemetry->playback_mode == HOME_PLAYBACK_STORAGE) {
            if (telemetry->sd_free_valid) {
                uint32_t gb_tenths = (uint32_t)((telemetry->sd_free_megabytes * 10ull + 512ull) / 1024ull);
                snprintf(subtitle_out, capacity, "USB STORAGE \xC2\xB7 %u.%u GB FREE",
                         (unsigned)(gb_tenths / 10u), (unsigned)(gb_tenths % 10u));
            } else {
                snprintf(subtitle_out, capacity, "USB STORAGE \xC2\xB7 MOUNTED");
            }
            return;
        }
        if (telemetry->playback_mode == HOME_PLAYBACK_PLAYING ||
            telemetry->playback_mode == HOME_PLAYBACK_PAUSED) {
            char upper_title[TRACK_TITLE_CAPACITY];
            char elapsed_text[16];
            char duration_text[16];
            format_uppercase_copy(
                telemetry->track_title[0] != '\0' ? telemetry->track_title : "UNKNOWN TRACK",
                upper_title, sizeof(upper_title));
            format_mm_ss(telemetry->elapsed_ms, elapsed_text, sizeof(elapsed_text));
            format_mm_ss(telemetry->duration_ms, duration_text, sizeof(duration_text));
            snprintf(subtitle_out, capacity, "%s \xC2\xB7 %s / %s", upper_title, elapsed_text,
                     duration_text);
            return;
        }
        snprintf(subtitle_out, capacity, "NOT PLAYING \xC2\xB7 BROWSE LIBRARY");
        return;
    }

    if (app_id == HOME_APP_LIBRARY) {
        if (telemetry->track_count == 0u) {
            snprintf(subtitle_out, capacity,
                     telemetry->sd_mounted ? "SD MOUNTED \xC2\xB7 0 TRACKS"
                                           : "EMPTY LIBRARY \xC2\xB7 0 TRACKS");
        } else {
            char tracks_text[20];
            char albums_text[20];
            format_comma_u32(telemetry->track_count, tracks_text, sizeof(tracks_text));
            format_comma_u32(telemetry->album_count, albums_text, sizeof(albums_text));
            snprintf(subtitle_out, capacity, "%s TRACKS \xC2\xB7 %s ALBUMS", tracks_text,
                     albums_text);
        }
        return;
    }

    if (app_id == HOME_APP_MODES) {
        snprintf(subtitle_out, capacity, "STORAGE \xC2\xB7 DAC \xC2\xB7 WIRELESS");
        return;
    }

    snprintf(subtitle_out, capacity, "%s \xC2\xB7 VOL LIMIT %u%%",
             appearance_mode == APPEARANCE_MODE_LIGHT ? "LIGHT MODE" : "DARK MODE",
             (unsigned)telemetry->volume_limit_percent);
}

static void draw_geometric_vinyl_disc(const struct framebuffer_surface *surface, float center_x,
                                      float center_y, float radius, float scale_x, float scale_y,
                                      float roll_radians, float spin_radians,
                                      const struct appearance_palette *palette, bool is_dark,
                                      bool focused, float alpha) {
    if (radius <= 2.0f || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    uint32_t base_color = is_dark ? 0xFF1A2029u : 0xFF282D36u;
    uint32_t mid_color = is_dark ? 0xFF262E3Bu : 0xFF3A414Du;
    uint32_t accent_color = focused ? palette->signal_highlight : palette->hairline_strong;
    uint32_t prism_color = focused ? palette->ink_primary : palette->ink_secondary;

    /* Outer disc body */
    render_draw_filled_ellipse_aa(surface, center_x, center_y, radius * scale_x, radius * scale_y,
                                  roll_radians, base_color, alpha);

    /* Concentric geometric motif ring */
    render_draw_filled_ellipse_aa(surface, center_x, center_y, radius * 0.72f * scale_x,
                                  radius * 0.72f * scale_y, roll_radians, mid_color, alpha * 0.92f);

    /* Rotating geometric prism arcs so vinyl spin is unmistakable */
    float cos_r = cosf(roll_radians);
    float sin_r = sinf(roll_radians);
    static const float SECTOR_ANGLES[3] = {0.0f, 2.094395f, 4.188790f};
    for (uint32_t sector = 0u; sector < 3u; sector++) {
        float angle = spin_radians + SECTOR_ANGLES[sector];
        float inner_r = radius * 0.34f;
        float outer_r = radius * 0.88f;
        float lx0 = cosf(angle) * inner_r * scale_x;
        float ly0 = sinf(angle) * inner_r * scale_y;
        float lx1 = cosf(angle) * outer_r * scale_x;
        float ly1 = sinf(angle) * outer_r * scale_y;

        float wx0 = center_x + lx0 * cos_r - ly0 * sin_r;
        float wy0 = center_y + lx0 * sin_r + ly0 * cos_r;
        float wx1 = center_x + lx1 * cos_r - ly1 * sin_r;
        float wy1 = center_y + lx1 * sin_r + ly1 * cos_r;
        uint32_t line_color = (sector == 0u) ? accent_color : prism_color;
        float line_w = (radius > 80.0f) ? 3.2f : 1.7f;
        render_draw_line_aa(surface, wx0, wy0, wx1, wy1, line_w, line_color,
                            alpha * (sector == 0u ? 0.95f : 0.45f));
    }

    /* Inner geometric core diamond */
    float core_r = radius * 0.28f;
    struct point_2d diamond[4];
    for (uint32_t corner = 0u; corner < 4u; corner++) {
        float angle = spin_radians + (float)corner * (HALF_CIRCLE_RADIANS * 0.5f);
        float lx = cosf(angle) * core_r * scale_x;
        float ly = sinf(angle) * core_r * scale_y;
        diamond[corner].x = center_x + lx * cos_r - ly * sin_r;
        diamond[corner].y = center_y + lx * sin_r + ly * cos_r;
    }
    render_draw_filled_convex_polygon_fast(surface, diamond, 4u, accent_color, alpha * 0.9f);
}

static void draw_turntable_sculpture(const struct framebuffer_surface *surface, float center_x,
                                     float center_y, float cube_scale, float scale_x, float scale_y,
                                     float elapsed_seconds, uint32_t primary_ink,
                                     uint32_t accent_color,
                                     const struct appearance_palette *palette, float alpha) {
    float platter_x = center_x - 3.0f * cube_scale;
    float platter_y = center_y + 1.0f * cube_scale;
    float avg_scale = (scale_x + scale_y) * 0.5f;

    /* Outer open turntable arc + inner groove + spindle */
    float idle_spin = elapsed_seconds * 0.35f;
    render_draw_arc_aa(surface, platter_x, platter_y, 21.0f * cube_scale * avg_scale,
                       2.6f * cube_scale, idle_spin, idle_spin + HALF_CIRCLE_RADIANS * 1.65f,
                       primary_ink, alpha);
    render_draw_stroked_ellipse_aa(surface, platter_x, platter_y, 12.0f * cube_scale * scale_x,
                                   12.0f * cube_scale * scale_y, 0.0f, 1.2f * cube_scale,
                                   palette->hairline_strong, alpha);
    render_draw_filled_ellipse_aa(surface, platter_x, platter_y, 4.2f * cube_scale * scale_x,
                                  4.2f * cube_scale * scale_y, 0.0f, primary_ink, alpha);

    /* Pivoting tonearm */
    float pivot_x = platter_x + 21.0f * cube_scale * scale_x;
    float pivot_y = platter_y - 16.0f * cube_scale * scale_y;
    float arm_sweep = sinf(elapsed_seconds * 1.6f) * 0.08f;
    float cos_arm = cosf(arm_sweep);
    float sin_arm = sinf(arm_sweep);

    float elbow_lx = 0.0f;
    float elbow_ly = 18.0f * cube_scale * scale_y;
    float tip_lx = -8.0f * cube_scale * scale_x;
    float tip_ly = 26.0f * cube_scale * scale_y;

    float elbow_x = pivot_x + elbow_lx * cos_arm - elbow_ly * sin_arm;
    float elbow_y = pivot_y + elbow_lx * sin_arm + elbow_ly * cos_arm;
    float tip_x = pivot_x + tip_lx * cos_arm - tip_ly * sin_arm;
    float tip_y = pivot_y + tip_lx * sin_arm + tip_ly * cos_arm;

    render_draw_filled_circle_aa(surface, pivot_x, pivot_y, 3.2f * cube_scale, primary_ink, alpha);
    render_draw_line_aa(surface, pivot_x, pivot_y, elbow_x, elbow_y, 2.3f * cube_scale,
                        primary_ink, alpha);
    render_draw_line_aa(surface, elbow_x, elbow_y, tip_x, tip_y, 2.3f * cube_scale, primary_ink,
                        alpha);

    float stylus_pulse = 0.55f + 0.45f * sinf(elapsed_seconds * 4.2f);
    float stylus_size = 6.5f * cube_scale;
    render_draw_filled_rect_alpha(surface, tip_x - stylus_size * 0.5f, tip_y - stylus_size * 0.5f,
                                  stylus_size, stylus_size, accent_color, alpha * stylus_pulse);
}

static void draw_alive_cube_icon(const struct framebuffer_surface *surface,
                                 const struct home_slot_layout *layout,
                                 const struct home_render_snapshot *snapshot,
                                 const struct library_controller *library,
                                 const struct appearance_palette *palette, bool is_dark,
                                 float icon_alpha) {
    if (icon_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float cube_scale = layout->half_size / 45.0f;
    bool highlight_active = layout->focus_weight > 0.38f;
    uint32_t primary_ink = highlight_active ? palette->ink_primary : palette->ink_secondary;
    uint32_t accent_color =
        highlight_active ? palette->signal_highlight : palette->hairline_strong;

    struct cube_face_palette muted_faces = {
        .top = palette->ink_secondary,
        .right = palette->hairline_strong,
        .left = palette->hairline,
    };
    const struct cube_face_palette *primary_faces =
        highlight_active ? &palette->cube_ink_faces : &muted_faces;
    const struct cube_face_palette *accent_faces =
        highlight_active ? &palette->cube_highlight_faces : &muted_faces;

    struct rotation_matrix_3x3 matrix =
        render_build_rotation_matrix_3d(layout->angle_x, layout->angle_y, layout->angle_z);

    if (layout->app_id == HOME_APP_NOW_PLAYING) {
        enum home_playback_mode mode = snapshot->telemetry.playback_mode;
        if (mode == HOME_PLAYBACK_STORAGE) {
            float hover_y = sinf(snapshot->elapsed_seconds * 2.4f) * 2.2f;
            render_draw_projected_3d_box(surface, &matrix, 0.0f, hover_y * cube_scale, 0.0f,
                                         15.0f * cube_scale, 18.0f * cube_scale, 7.0f * cube_scale,
                                         layout->center_x, layout->center_y, primary_faces,
                                         palette->cube_edge_stroke, icon_alpha);
            render_draw_projected_3d_box(surface, &matrix, 0.0f, (-21.0f + hover_y) * cube_scale,
                                         0.0f, 8.0f * cube_scale, 4.0f * cube_scale,
                                         4.0f * cube_scale, layout->center_x, layout->center_y,
                                         accent_faces, palette->cube_edge_stroke, icon_alpha);
            for (uint32_t bar = 0u; bar < 2u; bar++) {
                float phase = fmodf(snapshot->elapsed_seconds * 0.9f + (float)bar * 0.5f, 1.0f);
                float bar_y = render_lerp_f32(11.0f, -11.0f, phase) + hover_y;
                struct point_3d r0 = render_apply_rotation_matrix_3d(
                    &matrix, -8.0f * cube_scale, bar_y * cube_scale, 8.5f * cube_scale);
                struct point_3d r1 = render_apply_rotation_matrix_3d(
                    &matrix, 8.0f * cube_scale, bar_y * cube_scale, 8.5f * cube_scale);
                struct point_3d p0 = render_project_point_3d(r0.x, r0.y, r0.z, layout->center_x,
                                                             layout->center_y);
                struct point_3d p1 = render_project_point_3d(r1.x, r1.y, r1.z, layout->center_x,
                                                             layout->center_y);
                float stroke_w = 3.0f * cube_scale;
                if (stroke_w < 1.8f) {
                    stroke_w = 1.8f;
                }
                render_draw_line_aa(surface, p0.x, p0.y, p1.x, p1.y, stroke_w, accent_color,
                                    icon_alpha);
            }
            return;
        }

        float scale_x = render_clamp_f32(cosf(layout->angle_y) * 0.92f, 0.24f, 1.0f);
        float scale_y = render_clamp_f32(cosf(layout->angle_x) * 0.96f, 0.55f, 1.0f);

        if (mode == HOME_PLAYBACK_IDLE) {
            draw_turntable_sculpture(surface, layout->center_x, layout->center_y, cube_scale,
                                     scale_x, scale_y, snapshot->elapsed_seconds, primary_ink,
                                     accent_color, palette, icon_alpha);
            return;
        }

        /* Playing or Paused: foreshortened vinyl + 4-bar equalizer */
        bool hide_vinyl = (snapshot->launch_progress > 0.01f &&
                           snapshot->launched_app_id == HOME_APP_NOW_PLAYING);
        if (!hide_vinyl) {
            float disc_radius = layout->half_size * 0.64f;
            float disc_center_y = layout->center_y - layout->half_size * 0.08f;
            uint32_t rim_shadow = is_dark ? 0xFF2A303Cu : 0xFF1B1E24u;
            render_draw_filled_ellipse_aa(
                surface, layout->center_x + sinf(layout->angle_y) * 3.5f * cube_scale,
                disc_center_y + 2.2f * cube_scale, disc_radius * scale_x, disc_radius * scale_y,
                layout->angle_z * 0.6f, rim_shadow, icon_alpha * 0.85f);
            const uint16_t *cover_pixels =
                (library != NULL && library->has_active_track && library->active_album_uid != 0ull &&
                 library->cover_cache != NULL)
                    ? library_cover_cache_lookup_or_load_disk(library->cover_cache,
                                                              library->active_album_uid)
                    : NULL;
            if (cover_pixels != NULL && scale_x > 0.70f) {
                render_blit_rgb565_circle_rotated(
                    surface, cover_pixels, LIBRARY_COVER_SIZE_PX, LIBRARY_COVER_SIZE_PX,
                    layout->center_x, disc_center_y, disc_radius * scale_x,
                    snapshot->vinyl_rotation_radians, icon_alpha);
            } else {
                draw_geometric_vinyl_disc(surface, layout->center_x, disc_center_y, disc_radius,
                                          scale_x, scale_y, layout->angle_z * 0.6f,
                                          snapshot->vinyl_rotation_radians, palette, is_dark,
                                          highlight_active, icon_alpha);
            }
        }

        if (cube_scale > 0.45f) {
            float bar_width = 4.2f * cube_scale;
            float bar_gap = 4.5f * cube_scale;
            float total_w = 4.0f * bar_width + 3.0f * bar_gap;
            float start_x = layout->center_x - total_w * 0.5f;
            float baseline_y = layout->center_y + layout->half_size * 0.72f;
            for (uint32_t bar_index = 0u; bar_index < 4u; bar_index++) {
                float wave = 0.28f;
                if (mode == HOME_PLAYBACK_PLAYING) {
                    wave = 0.28f + 0.72f * (0.5f + 0.5f * sinf(snapshot->elapsed_seconds *
                                                                   (4.6f + (float)bar_index * 1.1f) +
                                                               (float)bar_index * 1.4f));
                }
                float bar_height = (3.5f + wave * 9.5f) * cube_scale;
                float bar_x = start_x + (float)bar_index * (bar_width + bar_gap);
                uint32_t bar_color = ((bar_index & 1u) == 0u) ? accent_color : primary_ink;
                render_draw_filled_rect_alpha(surface, bar_x, baseline_y - bar_height, bar_width,
                                              bar_height, bar_color, icon_alpha);
            }
        }
        return;
    }

    if (layout->app_id == HOME_APP_LIBRARY) {
        struct rotation_matrix_3x3 disc_matrix =
            render_build_rotation_matrix_3d(layout->angle_x + 0.42f, layout->angle_y, layout->angle_z);

        struct point_3d sp_top_r =
            render_apply_rotation_matrix_3d(&disc_matrix, 0.0f, -24.0f * cube_scale, 0.0f);
        struct point_3d sp_bot_r =
            render_apply_rotation_matrix_3d(&disc_matrix, 0.0f, 24.0f * cube_scale, 0.0f);
        struct point_3d sp_top = render_project_point_3d(sp_top_r.x, sp_top_r.y, sp_top_r.z,
                                                         layout->center_x, layout->center_y);
        struct point_3d sp_bot = render_project_point_3d(sp_bot_r.x, sp_bot_r.y, sp_bot_r.z,
                                                         layout->center_x, layout->center_y);
        float spindle_w = 2.2f * cube_scale;
        if (spindle_w < 1.2f) {
            spindle_w = 1.2f;
        }
        render_draw_line_aa(surface, sp_top.x, sp_top.y, sp_bot.x, sp_bot.y, spindle_w,
                            palette->hairline_strong, icon_alpha);

        static const struct {
            float base_y;
            float radius;
            float spin_speed;
            float phase;
            bool is_hero;
        } DISC_SPECS[3] = {
            {14.0f, 22.0f, 0.75f, 0.0f, false},
            {0.0f, 24.0f, -1.15f, 2.1f, true},
            {-14.0f, 20.0f, 0.95f, 4.2f, false},
        };

        for (uint32_t disc_index = 0u; disc_index < 3u; disc_index++) {
            float disc_y = (DISC_SPECS[disc_index].base_y +
                            sinf(snapshot->elapsed_seconds * 2.2f + DISC_SPECS[disc_index].phase) *
                                2.6f) *
                           cube_scale;
            float disc_r = DISC_SPECS[disc_index].radius * cube_scale;
            uint32_t segment_stride = (cube_scale < 0.75f) ? 2u : 1u;
            uint32_t segment_count = LIBRARY_DISC_SEGMENTS / segment_stride;
            struct point_2d ring_points[LIBRARY_DISC_SEGMENTS];

            for (uint32_t seg = 0u; seg < segment_count; seg++) {
                uint32_t table_index = seg * segment_stride;
                struct point_3d rotated = render_apply_rotation_matrix_3d(
                    &disc_matrix, UNIT_CIRCLE_COS[table_index] * disc_r, disc_y,
                    UNIT_CIRCLE_SIN[table_index] * disc_r);
                struct point_3d projected = render_project_point_3d(
                    rotated.x, rotated.y, rotated.z, layout->center_x, layout->center_y);
                ring_points[seg] = (struct point_2d){projected.x, projected.y};
            }

            uint32_t disc_fill = DISC_SPECS[disc_index].is_hero
                                     ? accent_color
                                     : (is_dark ? 0xFF252B36u : 0xFF424854u);
            uint32_t disc_stroke =
                DISC_SPECS[disc_index].is_hero ? palette->cube_edge_stroke : primary_ink;
            float stroke_w = (DISC_SPECS[disc_index].is_hero ? 1.3f : 1.6f) * cube_scale;
            if (stroke_w < 0.9f) {
                stroke_w = 0.9f;
            }

            render_draw_filled_convex_polygon_fast(surface, ring_points, segment_count,
                                                   disc_fill, icon_alpha);
            for (uint32_t seg = 0u; seg < segment_count; seg++) {
                uint32_t next_seg = (seg + 1u) % segment_count;
                if (!highlight_active) {
                    render_draw_line_fast(surface, ring_points[seg].x, ring_points[seg].y,
                                          ring_points[next_seg].x, ring_points[next_seg].y,
                                          disc_stroke, icon_alpha);
                } else {
                    render_draw_line_aa(surface, ring_points[seg].x, ring_points[seg].y,
                                        ring_points[next_seg].x, ring_points[next_seg].y, stroke_w,
                                        disc_stroke, icon_alpha);
                }
            }

            /* Rotating radial notch */
            float spin_angle =
                snapshot->elapsed_seconds * DISC_SPECS[disc_index].spin_speed +
                DISC_SPECS[disc_index].phase;
            float cos_spin = cosf(spin_angle);
            float sin_spin = sinf(spin_angle);
            struct point_3d n0_r = render_apply_rotation_matrix_3d(
                &disc_matrix, cos_spin * disc_r * 0.35f, disc_y, sin_spin * disc_r * 0.35f);
            struct point_3d n1_r = render_apply_rotation_matrix_3d(
                &disc_matrix, cos_spin * disc_r * 0.86f, disc_y, sin_spin * disc_r * 0.86f);
            struct point_3d n0 = render_project_point_3d(n0_r.x, n0_r.y, n0_r.z, layout->center_x,
                                                         layout->center_y);
            struct point_3d n1 = render_project_point_3d(n1_r.x, n1_r.y, n1_r.z, layout->center_x,
                                                         layout->center_y);
            uint32_t notch_color =
                DISC_SPECS[disc_index].is_hero ? palette->signal_highlight_ink : accent_color;
            float notch_w = 2.5f * cube_scale;
            if (notch_w < 1.4f) {
                notch_w = 1.4f;
            }
            render_draw_line_aa(surface, n0.x, n0.y, n1.x, n1.y, notch_w, notch_color, icon_alpha);

            struct point_3d hole_r = render_apply_rotation_matrix_3d(&disc_matrix, 0.0f, disc_y, 0.0f);
            struct point_3d hole_p = render_project_point_3d(
                hole_r.x, hole_r.y, hole_r.z, layout->center_x, layout->center_y);
            render_draw_filled_ellipse_aa(surface, hole_p.x, hole_p.y, 4.2f * cube_scale,
                                          2.2f * cube_scale, 0.0f, palette->canvas, icon_alpha);
        }
        return;
    }

    if (layout->app_id == HOME_APP_MODES) {
        static const float BUS_SEGMENTS[5][4] = {
            {-14.0f, -14.0f, 14.0f, -14.0f},
            {14.0f, -14.0f, 14.0f, 14.0f},
            {-14.0f, 14.0f, -14.0f, 0.0f},
            {-14.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 0.0f, -14.0f},
        };
        float bus_w = 2.2f * cube_scale;
        if (bus_w < 1.3f) {
            bus_w = 1.3f;
        }
        for (uint32_t seg = 0u; seg < 5u; seg++) {
            struct point_3d r0 = render_apply_rotation_matrix_3d(
                &matrix, BUS_SEGMENTS[seg][0] * cube_scale, BUS_SEGMENTS[seg][1] * cube_scale, 0.0f);
            struct point_3d r1 = render_apply_rotation_matrix_3d(
                &matrix, BUS_SEGMENTS[seg][2] * cube_scale, BUS_SEGMENTS[seg][3] * cube_scale, 0.0f);
            struct point_3d p0 =
                render_project_point_3d(r0.x, r0.y, r0.z, layout->center_x, layout->center_y);
            struct point_3d p1 =
                render_project_point_3d(r1.x, r1.y, r1.z, layout->center_x, layout->center_y);
            render_draw_line_aa(surface, p0.x, p0.y, p1.x, p1.y, bus_w, palette->hairline_strong,
                                icon_alpha);
        }

        static const float CORNERS[4][2] = {
            {-14.0f, -14.0f},
            {14.0f, -14.0f},
            {14.0f, 14.0f},
            {-14.0f, 14.0f},
        };
        uint32_t active_corner = ((uint32_t)floorf(snapshot->elapsed_seconds * 0.9f)) % 4u;
        for (uint32_t corner = 0u; corner < 4u; corner++) {
            bool is_active = (corner == active_corner);
            float half_xy = (is_active ? 6.4f : 5.2f) * cube_scale;
            float half_z = (is_active ? 5.0f : 3.5f) * cube_scale;
            float z_offset = is_active ? 3.0f * cube_scale : 0.0f;
            render_draw_projected_3d_box(
                surface, &matrix, CORNERS[corner][0] * cube_scale, CORNERS[corner][1] * cube_scale,
                z_offset, half_xy, half_xy, half_z, layout->center_x, layout->center_y,
                is_active ? accent_faces : primary_faces, palette->cube_edge_stroke, icon_alpha);
        }

        static const float PULSE_WAYPOINTS[6][2] = {
            {-14.0f, 14.0f}, {-14.0f, 0.0f}, {0.0f, 0.0f},
            {0.0f, -14.0f},  {14.0f, -14.0f}, {14.0f, 14.0f},
        };
        float route_phase = fmodf(snapshot->elapsed_seconds * 1.45f, 5.0f);
        uint32_t seg_index = (uint32_t)floorf(route_phase);
        if (seg_index >= 5u) {
            seg_index = 4u;
        }
        float seg_fraction = route_phase - (float)seg_index;
        float pulse_x = render_lerp_f32(PULSE_WAYPOINTS[seg_index][0],
                                        PULSE_WAYPOINTS[seg_index + 1u][0], seg_fraction);
        float pulse_y = render_lerp_f32(PULSE_WAYPOINTS[seg_index][1],
                                        PULSE_WAYPOINTS[seg_index + 1u][1], seg_fraction);
        struct point_3d pulse_r = render_apply_rotation_matrix_3d(
            &matrix, pulse_x * cube_scale, pulse_y * cube_scale, 3.0f * cube_scale);
        struct point_3d pulse_p = render_project_point_3d(pulse_r.x, pulse_r.y, pulse_r.z,
                                                          layout->center_x, layout->center_y);
        float pulse_size = 6.4f * cube_scale;
        render_draw_filled_rect_alpha(surface, pulse_p.x - pulse_size * 0.5f,
                                      pulse_p.y - pulse_size * 0.5f, pulse_size, pulse_size,
                                      accent_color, icon_alpha);
        return;
    }

    /* HOME_APP_SETTINGS: 3 horizontal mixer rails + 3 gliding 3D fader caps */
    static const float RAIL_Y[3] = {-14.0f, 0.0f, 14.0f};
    static const float CAP_BASE_X[3] = {-4.0f, 2.0f, -2.0f};
    static const float CAP_AMP_X[3] = {9.0f, 11.0f, 8.0f};
    static const float CAP_SPEED[3] = {1.35f, 1.05f, 1.55f};
    static const float CAP_PHASE[3] = {0.2f, 2.1f, 4.0f};

    float rail_w = 2.6f * cube_scale;
    if (rail_w < 1.6f) {
        rail_w = 1.6f;
    }
    for (uint32_t rail = 0u; rail < 3u; rail++) {
        struct point_3d r0 = render_apply_rotation_matrix_3d(
            &matrix, -19.0f * cube_scale, RAIL_Y[rail] * cube_scale, -2.0f * cube_scale);
        struct point_3d r1 = render_apply_rotation_matrix_3d(
            &matrix, 19.0f * cube_scale, RAIL_Y[rail] * cube_scale, -2.0f * cube_scale);
        struct point_3d p0 =
            render_project_point_3d(r0.x, r0.y, r0.z, layout->center_x, layout->center_y);
        struct point_3d p1 =
            render_project_point_3d(r1.x, r1.y, r1.z, layout->center_x, layout->center_y);
        render_draw_line_aa(surface, p0.x, p0.y, p1.x, p1.y, rail_w, palette->hairline_strong,
                            icon_alpha);

        float cap_x =
            CAP_BASE_X[rail] +
            sinf(snapshot->elapsed_seconds * CAP_SPEED[rail] + CAP_PHASE[rail]) * CAP_AMP_X[rail];
        render_draw_projected_3d_box(
            surface, &matrix, cap_x * cube_scale, RAIL_Y[rail] * cube_scale, 2.5f * cube_scale,
            5.4f * cube_scale, 4.6f * cube_scale, 4.2f * cube_scale, layout->center_x,
            layout->center_y, (rail == 1u) ? accent_faces : primary_faces,
            palette->cube_edge_stroke, icon_alpha);
    }
}

static void draw_glass_app_cube(const struct framebuffer_surface *surface,
                                const struct home_slot_layout *layout,
                                const struct home_render_snapshot *snapshot,
                                const struct library_controller *library,
                                const struct appearance_palette *palette, bool is_dark,
                                float shell_alpha, float icon_alpha) {
    if (layout->alpha <= ALPHA_VISIBILITY_THRESHOLD || layout->half_size <= 3.0f) {
        return;
    }

    static const float CUBE_CORNERS[8][3] = {
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},
    };
    static const uint8_t FACE_INDICES[6][4] = {
        {4u, 5u, 6u, 7u}, {1u, 0u, 3u, 2u}, {5u, 1u, 2u, 6u},
        {0u, 4u, 7u, 3u}, {0u, 1u, 5u, 4u}, {7u, 6u, 2u, 3u},
    };
    static const float FACE_NORMALS[6][3] = {
        {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
    };
    static const uint8_t CUBE_EDGES[12][2] = {
        {0u, 1u}, {1u, 2u}, {2u, 3u}, {3u, 0u}, {4u, 5u}, {5u, 6u},
        {6u, 7u}, {7u, 4u}, {0u, 4u}, {1u, 5u}, {2u, 6u}, {3u, 7u},
    };

    struct rotation_matrix_3x3 matrix =
        render_build_rotation_matrix_3d(layout->angle_x, layout->angle_y, layout->angle_z);

    struct point_3d outer_projected[8];
    struct point_3d inner_projected[8];
    float inner_half = layout->half_size * 0.85f;

    for (uint32_t corner = 0u; corner < 8u; corner++) {
        struct point_3d rotated_unit = render_apply_rotation_matrix_3d(
            &matrix, CUBE_CORNERS[corner][0], CUBE_CORNERS[corner][1], CUBE_CORNERS[corner][2]);
        outer_projected[corner] = render_project_point_3d(
            rotated_unit.x * layout->half_size, rotated_unit.y * layout->half_size,
            rotated_unit.z * layout->half_size, layout->center_x, layout->center_y);
        inner_projected[corner] = render_project_point_3d(
            rotated_unit.x * inner_half, rotated_unit.y * inner_half,
            rotated_unit.z * inner_half, layout->center_x, layout->center_y);
    }

    struct face_entry {
        uint8_t face_index;
        float average_depth;
        float normal_y;
        float normal_z;
    };
    struct face_entry back_faces[6];
    struct face_entry front_faces[6];
    uint32_t back_count = 0u;
    uint32_t front_count = 0u;

    for (uint32_t face = 0u; face < 6u; face++) {
        struct point_3d normal = render_apply_rotation_matrix_3d(
            &matrix, FACE_NORMALS[face][0], FACE_NORMALS[face][1], FACE_NORMALS[face][2]);
        float depth_sum = 0.0f;
        for (uint32_t corner = 0u; corner < 4u; corner++) {
            depth_sum += outer_projected[FACE_INDICES[face][corner]].z;
        }
        struct face_entry entry = {
            .face_index = (uint8_t)face,
            .average_depth = depth_sum * 0.25f,
            .normal_y = normal.y,
            .normal_z = normal.z,
        };
        if (normal.z <= 0.0f) {
            back_faces[back_count++] = entry;
        } else {
            front_faces[front_count++] = entry;
        }
    }

    for (uint32_t pass = 1u; pass < back_count; pass++) {
        for (uint32_t cursor = pass;
             cursor > 0u && back_faces[cursor - 1u].average_depth > back_faces[cursor].average_depth;
             cursor--) {
            struct face_entry swapped = back_faces[cursor - 1u];
            back_faces[cursor - 1u] = back_faces[cursor];
            back_faces[cursor] = swapped;
        }
    }
    for (uint32_t pass = 1u; pass < front_count; pass++) {
        for (uint32_t cursor = pass;
             cursor > 0u &&
             front_faces[cursor - 1u].average_depth > front_faces[cursor].average_depth;
             cursor--) {
            struct face_entry swapped = front_faces[cursor - 1u];
            front_faces[cursor - 1u] = front_faces[cursor];
            front_faces[cursor] = swapped;
        }
    }

    float effective_shell = layout->alpha * shell_alpha;
    uint32_t back_wall_color = is_dark ? 0xFFB0CEF8u : 0xFF2E343Eu;
    uint32_t back_stroke_color = is_dark ? 0xFFC3DCFFu : 0xFF2E343Eu;
    uint32_t inner_wire_color = is_dark ? 0xFFDAEAFFu : 0xFFFFFFFFu;
    uint32_t front_tint_color = is_dark ? 0xFFDCECFFu : 0xFFFFFFFFu;
    uint32_t front_border_color =
        is_dark ? (layout->focus_weight > 0.5f ? 0xFFF5FAFFu : 0xFFC8D8F0u) : 0xFF2E3238u;

    /* Layer 1: Back translucent chamber walls (culled during large Library Dive) */
    if (effective_shell > 0.01f && layout->half_size <= CUBE_LAYER1_SKIP_HALF_SIZE) {
        for (uint32_t index = 0u; index < back_count; index++) {
            uint8_t face = back_faces[index].face_index;
            struct point_2d quad[4];
            for (uint32_t corner = 0u; corner < 4u; corner++) {
                struct point_3d vertex = outer_projected[FACE_INDICES[face][corner]];
                quad[corner] = (struct point_2d){vertex.x, vertex.y};
            }
            float shade_factor = 0.55f + 0.45f * fabsf(back_faces[index].normal_y);
            float fill_alpha =
                is_dark ? (0.045f + 0.045f * shade_factor) : (0.06f + 0.055f * shade_factor);
            render_draw_filled_convex_polygon_fast(surface, quad, 4u, back_wall_color,
                                                   effective_shell * fill_alpha);
            for (uint32_t edge = 0u; edge < 4u; edge++) {
                uint32_t next_edge = (edge + 1u) & 3u;
                render_draw_line_fast(surface, quad[edge].x, quad[edge].y, quad[next_edge].x,
                                      quad[next_edge].y, back_stroke_color,
                                      effective_shell * 0.16f);
            }
        }

        /* Layer 2: Inset 0.85x inner wall wireframe */
        float inner_wire_alpha = effective_shell * (is_dark ? 0.16f : 0.48f);
        for (uint32_t edge = 0u; edge < 12u; edge++) {
            struct point_3d p0 = inner_projected[CUBE_EDGES[edge][0]];
            struct point_3d p1 = inner_projected[CUBE_EDGES[edge][1]];
            render_draw_line_fast(surface, p0.x, p0.y, p1.x, p1.y, inner_wire_color,
                                  inner_wire_alpha);
        }
    }

    /* Layer 3: Living 3D interior sculpture */
    draw_alive_cube_icon(surface, layout, snapshot, library, palette, is_dark,
                         layout->alpha * icon_alpha);

    /* Layer 4: Front crystal facets, diagonal glare streaks & L-brackets */
    if (effective_shell > 0.01f) {
        float glare_shift = sinf(layout->angle_y) * layout->half_size * 1.15f;
        float band_top_y = layout->center_y - layout->half_size * 1.4f;
        float band_bot_y = layout->center_y + layout->half_size * 1.4f;
        float slant_dx = -layout->half_size;
        float front_border_w = layout->focus_weight > 0.5f ? 1.55f : 1.05f;
        float front_border_alpha =
            is_dark ? (layout->focus_weight > 0.5f ? 0.78f : 0.34f)
                    : (layout->focus_weight > 0.5f ? 0.62f : 0.28f);

        for (uint32_t index = 0u; index < front_count; index++) {
            uint8_t face = front_faces[index].face_index;
            struct point_2d quad[4];
            for (uint32_t corner = 0u; corner < 4u; corner++) {
                struct point_3d vertex = outer_projected[FACE_INDICES[face][corner]];
                quad[corner] = (struct point_2d){vertex.x, vertex.y};
            }

            float facing = render_clamp_f32(front_faces[index].normal_z, 0.0f, 1.0f);
            float focus_scale = render_lerp_f32(0.75f, 1.0f, layout->focus_weight);
            float facet_alpha = is_dark ? render_lerp_f32(0.055f, 0.135f, 1.0f - facing)
                                        : render_lerp_f32(0.16f, 0.36f, 1.0f - facing);
            render_draw_filled_convex_polygon_fast(surface, quad, 4u, front_tint_color,
                                                   effective_shell * facet_alpha * focus_scale);

            if (facing > 0.55f && layout->half_size <= CUBE_LAYER1_SKIP_HALF_SIZE) {
                float glare_alpha = is_dark ? (0.095f * layout->focus_weight + 0.04f)
                                            : (0.42f * layout->focus_weight + 0.18f);
                float broad_x = layout->center_x - layout->half_size * 0.15f + glare_shift;
                float broad_w = layout->half_size * 0.65f;
                struct point_2d broad_band[4] = {
                    {broad_x, band_top_y},
                    {broad_x + broad_w, band_top_y},
                    {broad_x + broad_w + slant_dx, band_bot_y},
                    {broad_x + slant_dx, band_bot_y},
                };
                struct point_2d clipped_band[8];
                uint32_t clipped_count =
                    render_clip_polygon_to_convex_quad(broad_band, 4u, quad, clipped_band);
                if (clipped_count >= 3u) {
                    render_draw_filled_convex_polygon_fast(surface, clipped_band, clipped_count,
                                                           0xFFFFFFFFu,
                                                           effective_shell * glare_alpha);
                }

                float thin_x = layout->center_x + layout->half_size * 0.62f + glare_shift;
                float thin_w = layout->half_size * 0.17f;
                struct point_2d thin_band[4] = {
                    {thin_x, band_top_y},
                    {thin_x + thin_w, band_top_y},
                    {thin_x + thin_w + slant_dx, band_bot_y},
                    {thin_x + slant_dx, band_bot_y},
                };
                clipped_count =
                    render_clip_polygon_to_convex_quad(thin_band, 4u, quad, clipped_band);
                if (clipped_count >= 3u) {
                    render_draw_filled_convex_polygon_fast(surface, clipped_band, clipped_count,
                                                           0xFFFFFFFFu,
                                                           effective_shell * glare_alpha);
                }
            }

            for (uint32_t edge = 0u; edge < 4u; edge++) {
                uint32_t next_edge = (edge + 1u) & 3u;
                render_draw_line_aa(surface, quad[edge].x, quad[edge].y, quad[next_edge].x,
                                    quad[next_edge].y, front_border_w, front_border_color,
                                    effective_shell * front_border_alpha);
            }
        }

        if (layout->focus_weight > 0.45f && front_count > 0u) {
            uint8_t front_face = front_faces[front_count - 1u].face_index;
            struct point_2d front_quad[4];
            for (uint32_t corner = 0u; corner < 4u; corner++) {
                struct point_3d vertex = outer_projected[FACE_INDICES[front_face][corner]];
                front_quad[corner] = (struct point_2d){vertex.x, vertex.y};
            }
            render_draw_quad_corner_brackets(surface, front_quad, 0.24f, 2.3f,
                                             palette->signal_highlight,
                                             effective_shell * layout->focus_weight);
        }
    }
}

static void draw_home_stage(const struct framebuffer_surface *surface,
                            const struct home_render_snapshot *snapshot,
                            const struct appearance_palette *palette, bool is_dark,
                            float chrome_alpha) {
    if (chrome_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    /* Rim sheen arcs (no ticks) */
    uint32_t upper_sheen = is_dark ? 0xFFEBF4FFu : 0xFFFFFFFFu;
    float upper_alpha = is_dark ? 0.16f : 0.72f;
    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 175.0f, 2.0f,
                       -HALF_CIRCLE_RADIANS * 0.84f, -HALF_CIRCLE_RADIANS * 0.30f, upper_sheen,
                       chrome_alpha * upper_alpha);
    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 175.0f, 1.6f,
                       HALF_CIRCLE_RADIANS * 0.14f, HALF_CIRCLE_RADIANS * 0.44f,
                       palette->signal_highlight, chrome_alpha * (is_dark ? 0.22f : 0.24f));

    /* 4-row + 11-rail scrolling perspective floor */
    static const float ROW_RATIOS[4] = {0.0f, 0.36f, 0.74f, 1.0f};
    uint32_t floor_color = is_dark ? 0xFFC4DAFAu : 0xFF2E343Eu;
    float floor_base_alpha = is_dark ? 0.14f : 0.13f;

    for (uint32_t row = 0u; row < 4u; row++) {
        float ratio = ROW_RATIOS[row];
        float row_y = render_lerp_f32(202.0f, 232.0f, ratio);
        float half_span = render_lerp_f32(96.0f, 134.0f, ratio);
        float row_alpha = chrome_alpha * floor_base_alpha *
                          render_lerp_f32(0.35f, 0.85f, 1.0f - fabsf(ratio - 0.5f) * 1.4f);
        render_draw_line_fast(surface, (float)SCREEN_CENTER_X - half_span, row_y,
                              (float)SCREEN_CENTER_X + half_span, row_y, floor_color, row_alpha);
    }

    float fractional_shift = fmodf(snapshot->scroll_position, 1.0f);
    if (fractional_shift < 0.0f) {
        fractional_shift += 1.0f;
    }
    for (int32_t rail = -5; rail <= 5; rail++) {
        float base_offset = ((float)rail - fractional_shift) * 36.0f;
        float edge_fade = 1.0f - render_normalize_range(fabsf(base_offset), 86.0f, 162.0f);
        if (edge_fade <= 0.02f) {
            continue;
        }
        float top_x = (float)SCREEN_CENTER_X + base_offset * 0.56f;
        float bottom_x = (float)SCREEN_CENTER_X + base_offset * 1.18f;
        render_draw_line_fast(surface, top_x, 202.0f, bottom_x, 232.0f, floor_color,
                              chrome_alpha * floor_base_alpha * edge_fade * 0.75f);
    }

    /* Floating caustic reflection */
    int32_t center_slot = (int32_t)lroundf(snapshot->scroll_position);
    float hero_bob_y = sinf(snapshot->elapsed_seconds * 1.9f + (float)center_slot * 1.3f) * 3.2f;
    float reflection_scale = 1.0f - hero_bob_y * 0.025f;
    uint32_t outer_fill = is_dark ? palette->signal_highlight : 0xFF2E343Eu;
    render_draw_filled_ellipse_aa(surface, (float)SCREEN_CENTER_X, 216.0f,
                                  40.0f * reflection_scale, 8.5f * reflection_scale, 0.0f,
                                  outer_fill, chrome_alpha * 0.07f);
    render_draw_stroked_ellipse_aa(surface, (float)SCREEN_CENTER_X, 216.0f,
                                   40.0f * reflection_scale, 8.5f * reflection_scale, 0.0f, 1.0f,
                                   palette->signal_highlight, chrome_alpha * 0.42f);
    uint32_t inner_fill = is_dark ? 0xFFF5F6F8u : 0xFFFFFFFFu;
    render_draw_filled_ellipse_aa(surface, (float)SCREEN_CENTER_X, 216.0f,
                                  21.0f * reflection_scale, 4.2f * reflection_scale, 0.0f,
                                  inner_fill, chrome_alpha * (is_dark ? 0.14f : 0.45f));

    /* Top status beacon */
    char beacon_text[48];
    char battery_text[12];
    if (snapshot->telemetry.battery_valid) {
        snprintf(battery_text, sizeof(battery_text), "%u%%",
                 (unsigned)snapshot->telemetry.battery_percent);
    } else {
        snprintf(battery_text, sizeof(battery_text), "--%%");
    }

    enum home_playback_mode mode = snapshot->telemetry.playback_mode;
    if (mode == HOME_PLAYBACK_STORAGE) {
        snprintf(beacon_text, sizeof(beacon_text), "USB MODE \xC2\xB7 BAT %s", battery_text);
    } else {
        const char *state_label =
            (mode == HOME_PLAYBACK_PLAYING)
                ? "PLAY"
                : ((mode == HOME_PLAYBACK_PAUSED) ? "PAUSE" : "IDLE");
        snprintf(beacon_text, sizeof(beacon_text), "%s \xC2\xB7 VOL %u \xC2\xB7 %s", state_label,
                 (unsigned)snapshot->telemetry.volume_percent, battery_text);
    }

    const struct bitmap_font *beacon_font = font_atlas_get(FONT_STYLE_MONO_10);
    int32_t text_width = font_atlas_measure_width(beacon_font, beacon_text);
    float total_beacon_width = 5.0f + 6.0f + (float)text_width;
    float dot_x = (float)SCREEN_CENTER_X - total_beacon_width * 0.5f;
    uint32_t dot_color =
        (mode == HOME_PLAYBACK_IDLE) ? palette->hairline_strong : palette->signal_highlight;
    render_draw_filled_rect_alpha(surface, dot_x, 46.5f, 5.0f, 5.0f, dot_color, chrome_alpha);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, beacon_text, dot_x + 11.0f, 49.0f,
                           TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE, palette->ink_secondary,
                           chrome_alpha);
}

static void draw_home_bottom_chord(const struct framebuffer_surface *surface,
                                   const struct home_render_snapshot *snapshot,
                                   const struct appearance_palette *palette, float chord_alpha,
                                   float exit_progress) {
    if (chord_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float drift_y =
        render_lerp_f32(14.0f, 0.0f,
                        render_ease_out_expo(render_normalize_range(snapshot->entry_progress, 0.20f,
                                                                    0.92f))) +
        render_lerp_f32(0.0f, 24.0f, exit_progress);

    int32_t nearest_slot = (int32_t)lroundf(snapshot->scroll_position);
    float offset = snapshot->scroll_position - (float)nearest_slot;
    float crossfade_alpha = render_clamp_f32(1.0f - fabsf(offset) * 1.75f, 0.0f, 1.0f);
    float horizontal_shift_x = -offset * 26.0f;

    enum home_app_id active_app = (enum home_app_id)home_wrap_app_slot(nearest_slot);
    float text_alpha = chord_alpha * crossfade_alpha;
    if (text_alpha > ALPHA_VISIBILITY_THRESHOLD) {
        render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_22, resolve_app_title(active_app),
                               (float)SCREEN_CENTER_X + horizontal_shift_x, 248.0f + drift_y,
                               TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_primary,
                               text_alpha);

        char subtitle[160];
        format_app_subtitle(active_app, &snapshot->telemetry, snapshot->appearance_mode, subtitle,
                            sizeof(subtitle));
        render_draw_marquee_text(surface, FONT_STYLE_MONO_10, subtitle,
                                 (float)SCREEN_CENTER_X + horizontal_shift_x, 272.0f + drift_y,
                                 232.0f, snapshot->elapsed_seconds, palette->ink_secondary,
                                 text_alpha);
    }

    /* 4 cyclic morphing page bars */
    float pip_widths[HOME_APP_COUNT];
    float pip_focus[HOME_APP_COUNT];
    float total_width = 3.0f * 7.0f;
    for (uint32_t app_index = 0u; app_index < HOME_APP_COUNT; app_index++) {
        pip_focus[app_index] = home_cyclic_focus_weight((int32_t)app_index, snapshot->scroll_position);
        pip_widths[app_index] = render_lerp_f32(6.0f, 20.0f, render_ease_in_out_cubic(pip_focus[app_index]));
        total_width += pip_widths[app_index];
    }

    float cursor_x = (float)SCREEN_CENTER_X - total_width * 0.5f;
    for (uint32_t app_index = 0u; app_index < HOME_APP_COUNT; app_index++) {
        uint32_t pip_color =
            (pip_focus[app_index] > 0.5f) ? palette->signal_highlight : palette->hairline_strong;
        float pip_alpha = chord_alpha * render_lerp_f32(0.55f, 1.0f, pip_focus[app_index]);
        render_draw_filled_rect_alpha(surface, cursor_x, 296.0f + drift_y, pip_widths[app_index],
                                      5.0f, pip_color, pip_alpha);
        cursor_x += pip_widths[app_index] + 7.0f;
    }
}

static void draw_inline_overlay_panel(const struct framebuffer_surface *surface,
                                      const struct home_render_snapshot *snapshot,
                                      const struct appearance_palette *palette,
                                      float overlay_alpha, float slide_y) {
    if (overlay_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    if (snapshot->launched_app_id == HOME_APP_NOW_PLAYING &&
        snapshot->telemetry.playback_mode == HOME_PLAYBACK_STORAGE) {
        render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_20, "Storage Mode",
                               (float)SCREEN_CENTER_X, 168.0f + slide_y, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, palette->ink_primary, overlay_alpha);

        char storage_line[64];
        if (snapshot->telemetry.sd_free_valid) {
            uint32_t gb_tenths =
                (uint32_t)((snapshot->telemetry.sd_free_megabytes * 10ull + 512ull) / 1024ull);
            snprintf(storage_line, sizeof(storage_line), "USB CONNECTED \xC2\xB7 %u.%u GB FREE",
                     (unsigned)(gb_tenths / 10u), (unsigned)(gb_tenths % 10u));
        } else {
            snprintf(storage_line, sizeof(storage_line), "USB CONNECTED");
        }
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, storage_line, (float)SCREEN_CENTER_X,
                               192.0f + slide_y, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               palette->ink_secondary, overlay_alpha);

        /* 132 x 4 sweeping activity bar */
        float bar_x = (float)SCREEN_CENTER_X - 66.0f;
        float bar_y = 212.0f + slide_y;
        render_draw_filled_rect_alpha(surface, bar_x, bar_y, 132.0f, 4.0f, palette->hairline,
                                      overlay_alpha);
        float slug_phase = fmodf(snapshot->elapsed_seconds * 0.45f, 1.0f);
        render_draw_filled_rect_alpha(surface, bar_x + slug_phase * 96.0f, bar_y, 36.0f, 4.0f,
                                      palette->signal_highlight, overlay_alpha);

        render_draw_chamfer_plate_filled_aa(surface, 106.0f, 240.0f + slide_y, 148.0f, 34.0f, 7.0f,
                                            palette->surface, overlay_alpha);
        render_draw_chamfer_plate_stroked_aa(surface, 106.0f, 240.0f + slide_y, 148.0f, 34.0f, 7.0f,
                                             1.4f, palette->signal_highlight, overlay_alpha);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "BACK TO HOME", (float)SCREEN_CENTER_X,
                               257.0f + slide_y, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               palette->ink_primary, overlay_alpha);
        return;
    }

    render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_20,
                           resolve_app_title(snapshot->launched_app_id), (float)SCREEN_CENTER_X,
                           146.0f + slide_y, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->ink_primary, overlay_alpha);

    const char *rows[3];
    char settings_display_row[40];
    if (snapshot->launched_app_id == HOME_APP_MODES) {
        rows[0] = "USB STORAGE \xC2\xB7 READY";
        rows[1] = "USB DAC \xC2\xB7 V1.X";
        rows[2] = "WIRELESS RX \xC2\xB7 V1.X";
    } else {
        snprintf(settings_display_row, sizeof(settings_display_row), "DISPLAY \xC2\xB7 %s",
                 snapshot->appearance_mode == APPEARANCE_MODE_LIGHT ? "LIGHT MODE" : "DARK MODE");
        rows[0] = settings_display_row;
        rows[1] = "AUDIO \xC2\xB7 EQ FLAT";
        rows[2] = "SYSTEM \xC2\xB7 V1.0";
    }

    for (uint32_t row_index = 0u; row_index < 3u; row_index++) {
        float row_y = 170.0f + (float)row_index * 29.0f + slide_y;
        bool is_accented = (row_index == 0u);
        render_draw_filled_rect_alpha(surface, 91.0f, row_y, 178.0f, 24.0f, palette->surface,
                                      overlay_alpha);
        render_draw_stroked_rect_alpha(
            surface, 91.0f, row_y, 178.0f, 24.0f, is_accented ? 1.5f : 1.0f,
            is_accented ? palette->signal_highlight : palette->hairline, overlay_alpha);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, rows[row_index],
                               (float)SCREEN_CENTER_X, row_y + 12.0f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE,
                               is_accented ? palette->ink_primary : palette->ink_secondary,
                               overlay_alpha);
    }

    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "SWIPE FROM LEFT EDGE",
                           (float)SCREEN_CENTER_X, 282.0f + slide_y, TEXT_ALIGN_CENTER,
                           TEXT_BASELINE_MIDDLE, palette->ink_secondary, overlay_alpha);
}

static void draw_now_playing_shell(const struct framebuffer_surface *surface,
                                   const struct home_render_snapshot *snapshot,
                                   const struct appearance_palette *palette, bool is_dark) {
    if (snapshot->telemetry.playback_mode == HOME_PLAYBACK_IDLE) {
        /* Idle chrome: rim sheen, drifting floor, diagonal X marks, turntable medallion, CTA */
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 175.0f, 2.0f,
                           -HALF_CIRCLE_RADIANS * 0.84f, -HALF_CIRCLE_RADIANS * 0.30f,
                           is_dark ? 0xFFEBF4FFu : 0xFFFFFFFFu, is_dark ? 0.16f : 0.72f);
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 175.0f, 1.6f,
                           HALF_CIRCLE_RADIANS * 0.14f, HALF_CIRCLE_RADIANS * 0.44f,
                           palette->signal_highlight, 0.24f);

        render_draw_diagonal_mark(surface, 84.0f, 84.0f, 5.0f, 1.4f, palette->hairline_strong, 0.7f);
        render_draw_diagonal_mark(surface, 276.0f, 276.0f, 5.0f, 1.4f, palette->hairline_strong, 0.7f);

        float breath = 1.0f + 0.03f * sinf(snapshot->elapsed_seconds * 2.1f);
        render_draw_filled_circle_aa(surface, (float)SCREEN_CENTER_X, 118.0f, 34.0f * breath,
                                     palette->surface, 0.92f);
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, 118.0f, 34.0f * breath, 2.0f,
                           snapshot->elapsed_seconds * 0.8f,
                           snapshot->elapsed_seconds * 0.8f + HALF_CIRCLE_RADIANS * 0.75f,
                           palette->signal_highlight, 0.9f);
        draw_turntable_sculpture(surface, (float)SCREEN_CENTER_X, 118.0f, 0.75f, 1.0f, 1.0f,
                                 snapshot->elapsed_seconds, palette->ink_primary,
                                 palette->signal_highlight, palette, 1.0f);

        render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_22, "Not Playing",
                               (float)SCREEN_CENTER_X, 182.0f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, palette->ink_primary, 1.0f);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "NO TRACK QUEUED ON DISC",
                               (float)SCREEN_CENTER_X, 208.0f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, palette->ink_secondary, 1.0f);

        render_draw_chamfer_plate_filled_aa(surface, 101.0f, 236.0f, 158.0f, 36.0f, 8.0f,
                                            palette->signal_highlight, 1.0f);
        render_draw_rect_corner_brackets(surface, 95.0f, 230.0f, 170.0f, 48.0f, 10.0f, 2.0f,
                                         palette->signal_highlight, 0.9f);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "BROWSE LIBRARY", (float)SCREEN_CENTER_X,
                               254.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               palette->signal_highlight_ink, 1.0f);
        return;
    }

    /* Full-bleed spinning geometric vinyl + outer-edge progress ring */
    draw_geometric_vinyl_disc(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 180.0f,
                              1.0f, 1.0f, 0.0f, snapshot->vinyl_rotation_radians, palette, is_dark,
                              true, 1.0f);

    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 178.5f, 1.6f, 0.0f,
                       FULL_CIRCLE_RADIANS, palette->ink_primary, 0.20f);
    if (snapshot->telemetry.duration_ms > 0u) {
        float ratio =
            render_clamp_f32((float)snapshot->telemetry.elapsed_ms /
                                 (float)snapshot->telemetry.duration_ms,
                             0.0f, 1.0f);
        if (ratio > 0.002f) {
            render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 178.5f,
                               2.2f, TOP_ANGLE_RADIANS,
                               TOP_ANGLE_RADIANS + FULL_CIRCLE_RADIANS * ratio,
                               palette->signal_highlight, 1.0f);
        }
    }
}

static void draw_library_shell_layer(const struct framebuffer_surface *surface,
                                     const struct home_render_snapshot *snapshot,
                                     const struct appearance_palette *palette, bool is_dark,
                                     float layer_alpha, float scale_factor, float header_drift_y) {
    if (layer_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 175.0f, 2.0f,
                       -HALF_CIRCLE_RADIANS * 0.84f, -HALF_CIRCLE_RADIANS * 0.30f,
                       is_dark ? 0xFFEBF4FFu : 0xFFFFFFFFu, layer_alpha * (is_dark ? 0.16f : 0.72f));

    /* Deeper perspective floor */
    uint32_t floor_color = is_dark ? 0xFFC4DAFAu : 0xFF2E343Eu;
    float floor_alpha = layer_alpha * (is_dark ? 0.14f : 0.13f);
    for (uint32_t row = 0u; row < 3u; row++) {
        float row_y = 228.0f + (float)row * 22.0f;
        float span = 92.0f + (float)row * 24.0f;
        render_draw_line_fast(surface, (float)SCREEN_CENTER_X - span, row_y,
                              (float)SCREEN_CENTER_X + span, row_y, floor_color, floor_alpha);
    }
    for (int32_t rail = -3; rail <= 3; rail++) {
        float offset = (float)rail * 28.0f;
        render_draw_line_fast(surface, (float)SCREEN_CENTER_X + offset * 0.65f, 228.0f,
                              (float)SCREEN_CENTER_X + offset * 1.25f, 272.0f, floor_color,
                              floor_alpha * 0.75f);
    }

    render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_22, "Library", (float)SCREEN_CENTER_X,
                           54.0f + header_drift_y, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->ink_primary, layer_alpha);

    const char *status_line = snapshot->telemetry.sd_mounted ? "SD MOUNTED \xC2\xB7 READY TO INDEX"
                                                             : "EMPTY LIBRARY \xC2\xB7 0 TRACKS";
    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, status_line, (float)SCREEN_CENTER_X,
                           80.0f + header_drift_y, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->ink_secondary, layer_alpha);
    render_draw_line_aa(surface, 62.0f, 98.0f + header_drift_y, 298.0f, 98.0f + header_drift_y,
                        1.0f, palette->hairline, layer_alpha);

    /* Lightweight placeholder cards scaled from (180, 198) during dive */
    float card_w = 104.0f * scale_factor;
    float card_h = 88.0f * scale_factor;
    static const char *CARD_LABELS[2] = {"ALBUMS", "ALL SONGS"};
    for (uint32_t card = 0u; card < 2u; card++) {
        float target_cx = (card == 0u) ? 122.0f : 238.0f;
        float cx = render_lerp_f32((float)SCREEN_CENTER_X, target_cx, scale_factor);
        float cy = render_lerp_f32(198.0f, 168.0f, scale_factor);
        render_draw_chamfer_plate_filled_aa(surface, cx - card_w * 0.5f, cy - card_h * 0.5f, card_w,
                                            card_h, 10.0f * scale_factor, palette->surface,
                                            layer_alpha * 0.88f);
        render_draw_chamfer_plate_stroked_aa(surface, cx - card_w * 0.5f, cy - card_h * 0.5f,
                                             card_w, card_h, 10.0f * scale_factor, 1.2f,
                                             palette->hairline_strong, layer_alpha);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, CARD_LABELS[card], cx, cy,
                               TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_primary,
                               layer_alpha);
    }

    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "SWIPE FROM LEFT EDGE",
                           (float)SCREEN_CENTER_X, 292.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->ink_secondary, layer_alpha);
}

static void draw_back_swipe_bezel_arc(const struct framebuffer_surface *surface,
                                      float back_swipe_progress,
                                      const struct appearance_palette *palette) {
    if (back_swipe_progress <= 0.02f) {
        return;
    }
    float half_span = 0.34f * back_swipe_progress;
    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 168.0f, 4.0f,
                       HALF_CIRCLE_RADIANS - half_span, HALF_CIRCLE_RADIANS + half_span,
                       palette->signal_highlight, back_swipe_progress * 0.9f);
}

void render_draw_home_frame(const struct framebuffer_surface *surface,
                            const struct home_render_snapshot *snapshot) {
    render_draw_home_with_library_frame(surface, snapshot, NULL);
}

void render_draw_home_with_library_frame(const struct framebuffer_surface *surface,
                                         const struct home_render_snapshot *snapshot,
                                         const struct library_controller *library) {
    if (surface == NULL || surface->pixels == NULL || snapshot == NULL) {
        return;
    }

    const struct appearance_palette *palette = theme_get_palette(snapshot->appearance_mode);
    bool is_dark = (snapshot->appearance_mode == APPEARANCE_MODE_DARK);
    render_fill_canvas(surface, palette->canvas);

    if (snapshot->active_screen == ACTIVE_SCREEN_NOW_PLAYING_SHELL) {
        draw_now_playing_shell(surface, snapshot, palette, is_dark);
        draw_back_swipe_bezel_arc(surface, snapshot->back_swipe_progress, palette);
        return;
    }

    if (snapshot->active_screen == ACTIVE_SCREEN_LIBRARY_SHELL) {
        if (library != NULL) {
            render_draw_library_frame(surface, library, snapshot->appearance_mode,
                                      snapshot->back_swipe_progress);
        } else {
            draw_library_shell_layer(surface, snapshot, palette, is_dark, 1.0f, 1.0f, 0.0f);
            draw_back_swipe_bezel_arc(surface, snapshot->back_swipe_progress, palette);
        }
        return;
    }

    float entry_chrome =
        render_ease_out_cubic(render_normalize_range(snapshot->entry_progress, 0.15f, 0.85f));
    float launch_chrome = 1.0f - render_ease_in_out_cubic(
                                     render_normalize_range(snapshot->launch_progress, 0.0f, 0.45f));
    float chrome_alpha = entry_chrome * launch_chrome;

    float entry_chord =
        render_ease_out_cubic(render_normalize_range(snapshot->entry_progress, 0.25f, 0.92f));
    float exit_chord =
        render_ease_in_out_cubic(render_normalize_range(snapshot->launch_progress, 0.0f, 0.42f));
    float chord_alpha = entry_chord * (1.0f - exit_chord);

    draw_home_stage(surface, snapshot, palette, is_dark, chrome_alpha);

    /* Acrylic Cube Dive background emergence for Library */
    if (snapshot->launch_progress > 0.24f && snapshot->launched_app_id == HOME_APP_LIBRARY) {
        if (library != NULL) {
            render_library_hub_transition_layer(surface, library, snapshot->appearance_mode,
                                                snapshot->launch_progress);
        } else {
            float hub_alpha =
                render_ease_out_cubic(render_normalize_range(snapshot->launch_progress, 0.24f, 0.88f));
            float hub_scale = render_lerp_f32(
                0.80f, 1.0f,
                render_ease_out_expo(render_normalize_range(snapshot->launch_progress, 0.30f, 0.88f)));
            float header_drift = render_lerp_f32(
                -18.0f, 0.0f,
                render_ease_out_expo(render_normalize_range(snapshot->launch_progress, 0.42f, 0.96f)));
            draw_library_shell_layer(surface, snapshot, palette, is_dark, hub_alpha, hub_scale,
                                     header_drift);
        }
    }

    /* 5-slot back-to-front cube wheel */
    int32_t center_slot = (int32_t)lroundf(snapshot->scroll_position);
    int32_t focused_slot = center_slot;
    struct home_slot_layout layouts[5];
    for (int32_t offset = -2; offset <= 2; offset++) {
        home_compute_slot_layout(center_slot + offset, focused_slot, snapshot->scroll_position,
                                 snapshot->entry_progress, snapshot->launch_progress,
                                 snapshot->elapsed_seconds, &layouts[offset + 2]);
    }

    /* Insertion sort back-to-front by ascending focus_weight */
    for (uint32_t pass = 1u; pass < 5u; pass++) {
        for (uint32_t cursor = pass;
             cursor > 0u && layouts[cursor - 1u].focus_weight > layouts[cursor].focus_weight;
             cursor--) {
            struct home_slot_layout swapped = layouts[cursor - 1u];
            layouts[cursor - 1u] = layouts[cursor];
            layouts[cursor] = swapped;
        }
    }

    bool active_vinyl_zoom = (snapshot->launch_progress > 0.005f &&
                              snapshot->launched_app_id == HOME_APP_NOW_PLAYING &&
                              (snapshot->telemetry.playback_mode == HOME_PLAYBACK_PLAYING ||
                               snapshot->telemetry.playback_mode == HOME_PLAYBACK_PAUSED));

    for (uint32_t slot_index = 0u; slot_index < 5u; slot_index++) {
        struct home_slot_layout layout = layouts[slot_index];
        float shell_alpha = 1.0f;
        float icon_alpha = 1.0f;

        if (layout.is_focused && snapshot->launch_progress > 0.005f) {
            if (active_vinyl_zoom) {
                float dissolve = render_ease_out_cubic(
                    render_normalize_range(snapshot->launch_progress, 0.0f, 0.36f));
                float scale_up = render_ease_out_cubic(snapshot->launch_progress);
                layout.half_size = render_lerp_f32(layout.half_size, layout.half_size * 1.65f,
                                                   scale_up);
                layout.center_x = render_lerp_f32(layout.center_x, (float)SCREEN_CENTER_X, scale_up);
                layout.center_y = render_lerp_f32(layout.center_y, (float)SCREEN_CENTER_Y, scale_up);
                layout.angle_x *= (1.0f - dissolve);
                layout.angle_y *= (1.0f - dissolve);
                layout.angle_z *= (1.0f - dissolve);
                shell_alpha = 1.0f - dissolve;
                icon_alpha = 1.0f - dissolve;
            } else if (snapshot->launched_app_id == HOME_APP_LIBRARY) {
                float dive = render_normalize_range(snapshot->launch_progress, 0.0f, 0.56f);
                float dive_ease = render_ease_in_out_cubic(dive);
                layout.center_x = render_lerp_f32(layout.center_x, (float)SCREEN_CENTER_X, dive_ease);
                layout.center_y = render_lerp_f32(layout.center_y, (float)SCREEN_CENTER_Y, dive_ease);
                layout.half_size =
                    render_lerp_f32(layout.half_size, 178.0f, render_ease_out_cubic(dive));
                layout.angle_x *= (1.0f - dive_ease);
                layout.angle_y *= (1.0f - dive_ease);
                layout.angle_z *= (1.0f - dive_ease);
                icon_alpha = 1.0f - render_ease_out_cubic(
                                        render_normalize_range(snapshot->launch_progress, 0.08f,
                                                               0.36f));
                shell_alpha = 1.0f - render_ease_out_cubic(
                                         render_normalize_range(snapshot->launch_progress, 0.14f,
                                                                0.54f));
            } else {
                float header_ease = render_ease_in_out_cubic(snapshot->launch_progress);
                float target_half =
                    (snapshot->launched_app_id == HOME_APP_NOW_PLAYING &&
                     snapshot->telemetry.playback_mode == HOME_PLAYBACK_STORAGE)
                        ? 29.0f
                        : 25.0f;
                float target_y =
                    (snapshot->launched_app_id == HOME_APP_NOW_PLAYING &&
                     snapshot->telemetry.playback_mode == HOME_PLAYBACK_STORAGE)
                        ? 100.0f
                        : 84.0f;
                layout.center_x =
                    render_lerp_f32(layout.center_x, (float)SCREEN_CENTER_X, header_ease);
                layout.center_y = render_lerp_f32(layout.center_y, target_y, header_ease);
                layout.half_size = render_lerp_f32(layout.half_size, target_half, header_ease);
            }
        }

        draw_glass_app_cube(surface, &layout, snapshot, library, palette, is_dark, shell_alpha,
                            icon_alpha);

        /* Boot -> Home emblem cross-dissolve starting cleanly at r = 38 px */
        if (layout.is_focused && snapshot->entry_progress < 0.44f) {
            float emblem_fade = 1.0f - render_ease_out_cubic(render_normalize_range(
                                           snapshot->entry_progress, 0.0f, 0.42f));
            float emblem_radius =
                render_lerp_f32(38.0f, layout.half_size * 0.82f,
                                render_ease_out_expo(snapshot->entry_progress));
            render_draw_dialos_emblem(surface, layout.center_x, layout.center_y, emblem_radius,
                                      palette, emblem_fade, 1.0f);
        }
    }

    /* Direct vinyl zoom for active Now Playing launch */
    if (active_vinyl_zoom) {
        float zoom_ease = render_ease_in_out_cubic(snapshot->launch_progress);
        float start_radius = 45.0f * 0.64f;
        float start_y = 148.0f - 45.0f * 0.08f;
        float disc_cx = (float)SCREEN_CENTER_X;
        float disc_cy = render_lerp_f32(start_y, (float)SCREEN_CENTER_Y, zoom_ease);
        float disc_r = render_lerp_f32(start_radius, 180.0f, zoom_ease);
        const uint16_t *cover_pixels =
            (library != NULL && library->has_active_track && library->active_album_uid != 0ull &&
             library->cover_cache != NULL)
                ? library_cover_cache_lookup_or_load_disk(library->cover_cache,
                                                          library->active_album_uid)
                : NULL;
        if (cover_pixels != NULL) {
            render_blit_rgb565_circle_rotated(surface, cover_pixels, LIBRARY_COVER_SIZE_PX,
                                              LIBRARY_COVER_SIZE_PX, disc_cx, disc_cy, disc_r,
                                              snapshot->vinyl_rotation_radians, 1.0f);
        } else {
            draw_geometric_vinyl_disc(surface, disc_cx, disc_cy, disc_r, 1.0f, 1.0f, 0.0f,
                                      snapshot->vinyl_rotation_radians, palette, is_dark, true,
                                      1.0f);
        }

        float ring_alpha =
            render_ease_out_cubic(render_normalize_range(snapshot->launch_progress, 0.72f, 1.0f));
        if (ring_alpha > ALPHA_VISIBILITY_THRESHOLD) {
            render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 178.5f,
                               1.6f, 0.0f, FULL_CIRCLE_RADIANS, palette->ink_primary,
                               ring_alpha * 0.20f);
            if (snapshot->telemetry.duration_ms > 0u) {
                float ratio = render_clamp_f32(
                    (float)snapshot->telemetry.elapsed_ms / (float)snapshot->telemetry.duration_ms,
                    0.0f, 1.0f);
                if (ratio > 0.002f) {
                    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y,
                                       178.5f, 2.2f, TOP_ANGLE_RADIANS,
                                       TOP_ANGLE_RADIANS + FULL_CIRCLE_RADIANS * ratio,
                                       palette->signal_highlight, ring_alpha);
                }
            }
        }
    }

    draw_home_bottom_chord(surface, snapshot, palette, chord_alpha, exit_chord);

    /* Inline overlay content for Modes, Settings, and Storage Mode */
    if (snapshot->launch_progress > 0.36f &&
        (snapshot->launched_app_id == HOME_APP_MODES ||
         snapshot->launched_app_id == HOME_APP_SETTINGS ||
         (snapshot->launched_app_id == HOME_APP_NOW_PLAYING &&
          snapshot->telemetry.playback_mode == HOME_PLAYBACK_STORAGE))) {
        float overlay_alpha =
            render_ease_out_cubic(render_normalize_range(snapshot->launch_progress, 0.36f, 0.92f));
        float slide_y = render_lerp_f32(
            22.0f, 0.0f,
            render_ease_out_expo(render_normalize_range(snapshot->launch_progress, 0.36f, 0.95f)));
        draw_inline_overlay_panel(surface, snapshot, palette, overlay_alpha, slide_y);
    }

    draw_back_swipe_bezel_arc(surface, snapshot->back_swipe_progress, palette);
}
