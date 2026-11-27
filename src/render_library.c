#include "render_library.h"

#include "constants.h"
#include "render_primitives.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void format_comma_u32(uint32_t value, char *output, size_t capacity) {
    char raw[16];
    snprintf(raw, sizeof(raw), "%u", (unsigned)value);
    size_t len = strlen(raw);
    size_t out_idx = 0u;
    for (size_t idx = 0u; idx < len && out_idx + 1u < capacity; idx++) {
        if (idx > 0u && ((len - idx) % 3u) == 0u && out_idx + 1u < capacity) {
            output[out_idx++] = ',';
        }
        output[out_idx++] = raw[idx];
    }
    output[out_idx] = '\0';
}

static void format_duration_mmss(uint32_t seconds, char *output, size_t capacity) {
    uint32_t mins = seconds / 60u;
    uint32_t secs = seconds % 60u;
    snprintf(output, capacity, "%02u:%02u", (unsigned)mins, (unsigned)secs);
}

static void draw_peripheral_rim_sheen(const struct framebuffer_surface *surface,
                                      const struct appearance_palette *palette, bool is_dark,
                                      float alpha) {
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 175.0f, 2.0f,
                       -HALF_CIRCLE_RADIANS * 0.84f, -HALF_CIRCLE_RADIANS * 0.30f,
                       is_dark ? 0xFFEBF4FFu : 0xFFFFFFFFu, alpha * (is_dark ? 0.14f : 0.72f));
    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 175.0f, 1.5f,
                       HALF_CIRCLE_RADIANS * 0.14f, HALF_CIRCLE_RADIANS * 0.44f,
                       palette->signal_highlight, alpha * (is_dark ? 0.18f : 0.22f));
}

static void draw_library_perspective_floor(const struct framebuffer_surface *surface,
                                           const struct appearance_palette *palette, bool is_dark,
                                           float scroll_position, float alpha) {
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    uint32_t floor_color = is_dark ? 0xFFC4DAFAu : 0xFF2E343Eu;
    float line_alpha = alpha * 0.10f;
    for (uint32_t row = 0u; row < 3u; row++) {
        float ratio = (float)row * 0.5f;
        float row_y = 266.0f + ratio * 60.0f;
        float half_span = render_lerp_f32(112.0f, 74.0f, ratio);
        render_draw_line_fast(surface, (float)SCREEN_CENTER_X - half_span, row_y,
                              (float)SCREEN_CENTER_X + half_span, row_y, floor_color, line_alpha);
    }
    float frac = fmodf(scroll_position, 1.0f);
    if (frac < 0.0f) {
        frac += 1.0f;
    }
    for (int32_t column = -3; column <= 3; column++) {
        float shifted = (float)column - frac * 0.5f;
        render_draw_line_fast(surface, (float)SCREEN_CENTER_X + shifted * 34.0f, 266.0f,
                              (float)SCREEN_CENTER_X + shifted * 52.0f, 326.0f, floor_color,
                              line_alpha);
    }
    render_draw_diagonal_mark(surface, 78.0f, 74.0f, 3.5f, 1.2f, palette->hairline_strong,
                              alpha * 0.55f);
    render_draw_diagonal_mark(surface, 282.0f, 294.0f, 3.5f, 1.2f, palette->hairline_strong,
                              alpha * 0.55f);
}

static void draw_living_icon(const struct framebuffer_surface *surface,
                             enum library_hub_section_id icon_id, float center_x, float center_y,
                             float radius, float elapsed_seconds, bool emphasized,
                             const struct appearance_palette *palette, bool is_dark, float alpha) {
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD || radius <= 1.0f) {
        return;
    }
    uint32_t ink = emphasized ? palette->signal_highlight : palette->ink_primary;
    uint32_t accent = palette->signal_highlight;
    uint32_t muted = palette->hairline_strong;

    switch (icon_id) {
        case LIB_HUB_FAVORITES: {
            float scale_x = 0.68f + 0.32f * fabsf(cosf(elapsed_seconds * 1.4f));
            float rx = radius * scale_x;
            struct point_2d left_lobe[5] = {
                {center_x, center_y + 0.78f * radius},
                {center_x - 0.86f * rx, center_y - 0.08f * radius},
                {center_x - 0.82f * rx, center_y - 0.58f * radius},
                {center_x - 0.34f * rx, center_y - 0.80f * radius},
                {center_x, center_y - 0.40f * radius},
            };
            struct point_2d right_lobe[5] = {
                {center_x, center_y + 0.78f * radius},
                {center_x, center_y - 0.40f * radius},
                {center_x + 0.34f * rx, center_y - 0.80f * radius},
                {center_x + 0.82f * rx, center_y - 0.58f * radius},
                {center_x + 0.86f * rx, center_y - 0.08f * radius},
            };
            uint32_t fill_col = emphasized ? accent : (is_dark ? 0xFFF5F6F8u : 0xFF2E3238u);
            float fill_alpha = emphasized ? alpha : alpha * (is_dark ? 0.16f : 0.14f);
            render_draw_filled_convex_polygon_fast(surface, left_lobe, 5u, fill_col, fill_alpha);
            render_draw_filled_convex_polygon_fast(surface, right_lobe, 5u, fill_col, fill_alpha);
            struct point_2d pts[8] = {
                {center_x, center_y + 0.78f * radius},
                {center_x - 0.86f * rx, center_y - 0.08f * radius},
                {center_x - 0.82f * rx, center_y - 0.58f * radius},
                {center_x - 0.34f * rx, center_y - 0.80f * radius},
                {center_x, center_y - 0.40f * radius},
                {center_x + 0.34f * rx, center_y - 0.80f * radius},
                {center_x + 0.82f * rx, center_y - 0.58f * radius},
                {center_x + 0.86f * rx, center_y - 0.08f * radius},
            };
            for (uint32_t idx = 0u; idx < 8u; idx++) {
                uint32_t next_idx = (idx + 1u) & 7u;
                render_draw_line_aa(surface, pts[idx].x, pts[idx].y, pts[next_idx].x,
                                    pts[next_idx].y, 1.5f, ink, alpha);
            }
            render_draw_line_aa(surface, center_x, center_y - 0.40f * radius, center_x,
                                center_y + 0.72f * radius, 1.3f,
                                emphasized ? palette->signal_highlight_ink : accent, alpha);
            break;
        }
        case LIB_HUB_PLAYLISTS: {
            static const float BASE_Y[3] = {0.46f, 0.0f, -0.46f};
            static const float RAD_X[3] = {0.72f, 0.84f, 0.68f};
            static const float PHASES[3] = {0.0f, 2.0f, 4.0f};
            for (uint32_t idx = 0u; idx < 3u; idx++) {
                float drift_y = center_y + BASE_Y[idx] * radius +
                                sinf(elapsed_seconds * 2.2f + PHASES[idx]) * 1.6f;
                float stroke_w = (idx == 1u) ? 1.6f : 1.15f;
                uint32_t col = (idx == 1u) ? (emphasized ? accent : ink) : muted;
                render_draw_stroked_ellipse_aa(surface, center_x, drift_y, RAD_X[idx] * radius,
                                               0.24f * radius, 0.0f, stroke_w, col, alpha);
            }
            break;
        }
        case LIB_HUB_ARTISTS: {
            float sway_x = sinf(elapsed_seconds * 1.5f) * 1.4f;
            render_draw_stroked_ellipse_aa(surface, center_x + sway_x, center_y - 0.34f * radius,
                                           0.33f * radius, 0.33f * radius, 0.0f, 1.5f, ink, alpha);
            render_draw_line_aa(surface, center_x - 0.74f * radius, center_y + 0.72f * radius,
                                center_x - 0.56f * radius, center_y + 0.20f * radius, 1.65f, ink,
                                alpha);
            render_draw_line_aa(surface, center_x - 0.56f * radius, center_y + 0.20f * radius,
                                center_x + 0.56f * radius, center_y + 0.20f * radius, 1.65f, ink,
                                alpha);
            render_draw_line_aa(surface, center_x + 0.56f * radius, center_y + 0.20f * radius,
                                center_x + 0.74f * radius, center_y + 0.72f * radius, 1.65f, ink,
                                alpha);
            break;
        }
        case LIB_HUB_ALBUMS: {
            float sleeve_half = 0.70f * radius;
            float sleeve_cx = center_x - 0.24f * radius;
            float disc_cx = sleeve_cx + sleeve_half * 0.88f + sinf(elapsed_seconds * 1.8f) * 0.05f * radius;
            float disc_r = sleeve_half * 0.88f;
            render_draw_filled_circle_aa(surface, disc_cx, center_y, disc_r,
                                         is_dark ? 0xFF13171Fu : 0xFF242830u, alpha);
            render_draw_arc_aa(surface, disc_cx, center_y, 0.66f * disc_r, 1.0f,
                               elapsed_seconds * 1.25f,
                               elapsed_seconds * 1.25f + HALF_CIRCLE_RADIANS * 0.75f,
                               palette->ink_secondary, alpha * 0.75f);
            render_draw_filled_circle_aa(surface, disc_cx, center_y, 0.30f * disc_r, accent, alpha);
            render_draw_filled_circle_aa(surface, disc_cx, center_y, 0.11f * disc_r, 0xFF060709u,
                                         alpha);

            render_draw_filled_rect_alpha(surface, sleeve_cx - sleeve_half, center_y - sleeve_half,
                                          sleeve_half * 2.0f, sleeve_half * 2.0f,
                                          is_dark ? 0xFF141923u : 0xFF272C35u, alpha);
            render_draw_line_aa(surface, sleeve_cx - 0.3f * sleeve_half, center_y + sleeve_half,
                                sleeve_cx + 0.45f * sleeve_half, center_y - sleeve_half, 2.2f,
                                accent, alpha);
            render_draw_stroked_rect_alpha(surface, sleeve_cx - sleeve_half, center_y - sleeve_half,
                                           sleeve_half * 2.0f, sleeve_half * 2.0f, 1.1f, ink, alpha);
            float arm = fmaxf(4.0f, 0.36f * sleeve_half);
            render_draw_line_aa(surface, sleeve_cx - sleeve_half, center_y - sleeve_half + arm,
                                sleeve_cx - sleeve_half, center_y - sleeve_half, 1.7f, accent,
                                alpha);
            render_draw_line_aa(surface, sleeve_cx - sleeve_half, center_y - sleeve_half,
                                sleeve_cx - sleeve_half + arm, center_y - sleeve_half, 1.7f, accent,
                                alpha);
            break;
        }
        case LIB_HUB_GENRES: {
            float bar_w = 0.26f * radius;
            float gap = 0.16f * radius;
            float total_w = 4.0f * bar_w + 3.0f * gap;
            float start_x = center_x - total_w * 0.5f;
            float bottom_y = center_y + 0.68f * radius;
            for (uint32_t idx = 0u; idx < 4u; idx++) {
                float level = 0.28f + 0.72f * (0.5f + 0.5f * sinf(elapsed_seconds * (3.2f + (float)idx * 0.9f) +
                                                                  (float)idx * 1.4f));
                float bar_h = radius * (0.45f + level * 0.95f);
                uint32_t col = (idx == 1u && emphasized) ? accent : ((idx % 2u == 0u) ? ink : muted);
                render_draw_filled_rect_alpha(surface, start_x + (float)idx * (bar_w + gap),
                                              bottom_y - bar_h, bar_w, bar_h, col, alpha);
            }
            break;
        }
        case LIB_HUB_ALL_SONGS:
        default: {
            float orbit = elapsed_seconds * 1.2f;
            render_draw_arc_aa(surface, center_x, center_y, 0.78f * radius, 1.7f, orbit,
                               orbit + 1.45f * HALF_CIRCLE_RADIANS, ink, alpha);
            render_draw_arc_aa(surface, center_x, center_y, 0.44f * radius, 1.4f, -orbit * 1.3f,
                               -orbit * 1.3f + 1.1f * HALF_CIRCLE_RADIANS, muted, alpha);
            float dot_angle = orbit + 1.65f * HALF_CIRCLE_RADIANS;
            float dot_x = center_x + cosf(dot_angle) * 0.78f * radius - 2.2f;
            float dot_y = center_y + sinf(dot_angle) * 0.78f * radius - 2.2f;
            render_draw_filled_rect_alpha(surface, dot_x, dot_y, 4.4f, 4.4f, accent, alpha);
            break;
        }
    }
}

static void draw_track_ring_icon(const struct framebuffer_surface *surface, float center_x,
                                 float center_y, float radius, float elapsed_seconds,
                                 bool active, const struct appearance_palette *palette,
                                 float alpha) {
    uint32_t ink = active ? palette->signal_highlight : palette->ink_primary;
    render_draw_arc_aa(surface, center_x, center_y, 0.78f * radius, 1.4f, 0.0f,
                       FULL_CIRCLE_RADIANS, ink, alpha);
    float spin = elapsed_seconds * (active ? 2.4f : 0.9f);
    render_draw_arc_aa(surface, center_x, center_y, 0.44f * radius, 1.8f, spin,
                       spin + 0.65f * HALF_CIRCLE_RADIANS,
                       active ? palette->signal_highlight : palette->hairline_strong, alpha);
    render_draw_filled_circle_aa(surface, center_x, center_y, 0.16f * radius, ink, alpha);
}

static void draw_geometric_motif(const struct framebuffer_surface *surface, uint8_t motif_id,
                                 float center_x, float center_y, float half_size,
                                 const struct clip_rect *clip,
                                 const struct appearance_palette *palette, bool is_dark,
                                 float alpha) {
    uint32_t base_ink = is_dark ? 0xFF11151Cu : 0xFF22262Eu;
    uint32_t slate_surface = is_dark ? 0xFF1D2330u : 0xFF343A46u;
    uint32_t stark_light = 0xFFF5F6F8u;
    uint32_t accent = palette->signal_highlight;
    float h = half_size;
    float x = center_x;
    float y = center_y;

    switch (motif_id) {
        case 0u: /* prism */
            render_draw_filled_rect_clipped_alpha(surface, x - h, y - h, 2.0f * h, 2.0f * h, clip,
                                                  base_ink, alpha);
            {
                struct point_2d poly[6] = {{x - h, y - 0.18f * h},   {x + 0.35f * h, y - h},
                                          {x + h, y - h},           {x + h, y + 0.25f * h},
                                          {x - 0.42f * h, y + h},   {x - h, y + h}};
                render_draw_filled_convex_polygon_fast(surface, poly, 6u, slate_surface, alpha);
                struct point_2d slash[4] = {{x - 0.14f * h, y - h},
                                            {x + 0.56f * h, y - h},
                                            {x - 0.24f * h, y + h},
                                            {x - 0.68f * h, y + h}};
                render_draw_filled_convex_polygon_fast(surface, slash, 4u, accent, alpha);
            }
            render_draw_arc_aa(surface, x, y, 0.62f * h, fmaxf(3.0f, 0.14f * h),
                               -0.22f * HALF_CIRCLE_RADIANS, 0.48f * HALF_CIRCLE_RADIANS,
                               stark_light, alpha);
            for (uint32_t idx = 0u; idx < 4u; idx++) {
                float step = fmaxf(4.0f, 0.105f * h);
                render_draw_filled_rect_clipped_alpha(
                    surface, x - 0.78f * h, y - 0.58f * h + (float)idx * step,
                    (0.48f - 0.08f * (float)idx) * h, fmaxf(2.0f, 0.05f * h), clip, stark_light,
                    alpha);
            }
            break;
        case 1u: /* grid */
            render_draw_filled_rect_clipped_alpha(surface, x - h, y - h, 2.0f * h, 2.0f * h, clip,
                                                  0xFF0F1D2Bu, alpha);
            {
                float step = 0.34f * h;
                for (float g = -1.2f * h; g <= 1.2f * h; g += step) {
                    render_draw_line_fast(surface, x + g, y - h, x + g + 0.4f * h, y + h,
                                          0xFF5EDCE6u, alpha * 0.36f);
                    render_draw_line_fast(surface, x - h, y + g, x + h, y + g - 0.35f * h,
                                          0xFF5EDCE6u, alpha * 0.36f);
                }
                struct point_2d diamond[4] = {{x - 0.48f * h, y - 0.12f * h},
                                              {x + 0.16f * h, y - 0.52f * h},
                                              {x + 0.58f * h, y - 0.14f * h},
                                              {x - 0.06f * h, y + 0.26f * h}};
                render_draw_filled_convex_polygon_fast(surface, diamond, 4u, 0xFF5EDCE6u, alpha);
            }
            render_draw_filled_rect_clipped_alpha(surface, x - 0.56f * h, y + 0.34f * h, 1.12f * h,
                                                  0.14f * h, clip, stark_light, alpha);
            render_draw_crosshair_mark(surface, x - 0.52f * h, y - 0.54f * h, 0.12f * h, 1.4f,
                                       stark_light, alpha * 0.9f);
            render_draw_crosshair_mark(surface, x + 0.52f * h, y + 0.64f * h, 0.12f * h, 1.4f,
                                       accent, alpha * 0.95f);
            break;
        case 2u: /* monolith */
            render_draw_filled_rect_clipped_alpha(surface, x - h, y - h, 2.0f * h, 1.12f * h, clip,
                                                  stark_light, alpha);
            render_draw_filled_rect_clipped_alpha(surface, x - h, y + 0.12f * h, 2.0f * h, 0.88f * h,
                                                  clip, base_ink, alpha);
            for (uint32_t idx = 0u; idx < 2u; idx++) {
                float ox = x - 0.36f * h + (float)idx * 0.36f * h;
                float lw = fmaxf(3.0f, 0.14f * h);
                render_draw_line_aa(surface, ox - 0.18f * h, y - 0.58f * h, ox + 0.18f * h,
                                    y - 0.26f * h, lw, base_ink, alpha);
                render_draw_line_aa(surface, ox + 0.18f * h, y - 0.26f * h, ox - 0.18f * h,
                                    y + 0.04f * h, lw, base_ink, alpha);
            }
            render_draw_filled_rect_clipped_alpha(surface, x + 0.28f * h, y + 0.32f * h, 0.42f * h,
                                                  0.42f * h, clip, accent, alpha);
            render_draw_filled_rect_clipped_alpha(surface, x - 0.72f * h, y + 0.48f * h, 0.74f * h,
                                                  0.08f * h, clip, stark_light, alpha);
            break;
        case 3u: /* tape */
            render_draw_filled_rect_clipped_alpha(surface, x - h, y - h, 2.0f * h, 2.0f * h, clip,
                                                  0xFF191B20u, alpha);
            {
                struct point_2d ribbon[4] = {{x - h, y + 0.18f * h},
                                             {x + h, y - 0.54f * h},
                                             {x + h, y - 0.14f * h},
                                             {x - h, y + 0.58f * h}};
                render_draw_filled_convex_polygon_fast(surface, ribbon, 4u, 0xFFF5B841u, alpha);
            }
            for (int32_t side = -1; side <= 1; side += 2) {
                float rx = x + (float)side * 0.36f * h;
                render_draw_filled_circle_aa(surface, rx, y, 0.28f * h, base_ink, alpha);
                render_draw_arc_aa(surface, rx, y, 0.28f * h, fmaxf(1.5f, 0.045f * h), 0.0f,
                                   FULL_CIRCLE_RADIANS, stark_light, alpha);
                render_draw_filled_circle_aa(surface, rx, y, 0.09f * h, stark_light, alpha);
            }
            render_draw_filled_rect_clipped_alpha(surface, x - 0.64f * h, y + 0.62f * h, 1.28f * h,
                                                  0.07f * h, clip, stark_light, alpha);
            break;
        case 4u: /* optical */
            render_draw_filled_rect_clipped_alpha(surface, x - h, y - h, 2.0f * h, 2.0f * h, clip,
                                                  0xFF161224u, alpha);
            render_draw_arc_aa(surface, x, y, 0.68f * h, fmaxf(2.5f, 0.11f * h),
                               -0.85f * HALF_CIRCLE_RADIANS, 0.15f * HALF_CIRCLE_RADIANS,
                               0xFFB296F5u, alpha);
            render_draw_arc_aa(surface, x, y, 0.42f * h, fmaxf(1.5f, 0.045f * h),
                               0.10f * HALF_CIRCLE_RADIANS, 1.25f * HALF_CIRCLE_RADIANS,
                               stark_light, alpha);
            render_draw_filled_rect_clipped_alpha(surface, x + 0.24f * h, y - 0.68f * h, 0.42f * h,
                                                  0.42f * h, clip, accent, alpha);
            render_draw_diagonal_mark(surface, x + 0.45f * h, y - 0.47f * h, 0.11f * h, 1.5f,
                                      palette->signal_highlight_ink, alpha);
            render_draw_diagonal_mark(surface, x - 0.52f * h, y + 0.54f * h, 0.11f * h, 1.4f,
                                      stark_light, alpha * 0.85f);
            break;
        default: /* concrete */
            render_draw_filled_rect_clipped_alpha(surface, x - h, y - h, 2.0f * h, 2.0f * h, clip,
                                                  0xFFD4D7DCu, alpha);
            render_draw_filled_rect_clipped_alpha(surface, x - 0.78f * h, y - 0.72f * h, 0.68f * h,
                                                  1.44f * h, clip, 0xFF262A31u, alpha);
            render_draw_filled_rect_clipped_alpha(surface, x + 0.08f * h, y - 0.28f * h, 0.70f * h,
                                                  1.00f * h, clip, 0xFF262A31u, alpha);
            render_draw_hazard_stripe_band(surface, x + 0.08f * h, y - 0.72f * h, 0.70f * h,
                                           0.30f * h, 0.0f, palette->signal_alert);
            render_draw_filled_circle_aa(surface, x - 0.44f * h, y - 0.34f * h, 0.16f * h, accent,
                                         alpha);
            break;
    }
}

static void draw_crate_album_sleeve_and_disc(
    const struct framebuffer_surface *surface, struct library_controller *controller,
    uint32_t album_index, float center_x, float center_y, float sleeve_size, float delta,
    float focus_weight, float base_alpha, bool draw_peeking_disc,
    const struct appearance_palette *palette, bool is_dark) {
    if (controller == NULL || controller->catalog == NULL ||
        album_index >= controller->catalog->album_count || base_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    const struct library_album *album = &controller->catalog->albums[album_index];
    float half_s = sleeve_size * 0.5f;

    const uint16_t *cover_pixels = NULL;
    if (controller->cover_cache != NULL && album->cover_source_kind != LIBRARY_COVER_NONE) {
        cover_pixels =
            library_cover_cache_lookup_or_load_disk(controller->cover_cache, album->album_id);
        if (cover_pixels == NULL) {
            const char *cov_path =
                library_catalog_get_string(controller->catalog, album->cover_path_offset);
            (void)library_cover_cache_request(controller->cover_cache, album->album_id,
                                              album->cover_source_kind, cov_path,
                                              album->cover_data_offset, album->cover_data_length);
        }
    }

    /* Peeking spinning vinyl disc behind sleeve when active album */
    if (draw_peeking_disc && controller->has_active_track &&
        controller->active_album_id == album->album_id && focus_weight > 0.42f) {
        float peek_weight = render_ease_out_cubic(render_normalize_range(focus_weight, 0.42f, 1.0f));
        float disc_x = center_x + render_lerp_f32(half_s * 0.12f, half_s * 0.42f + 22.0f, peek_weight);
        float disc_r = half_s * 0.88f;
        float disc_alpha = base_alpha * peek_weight * 0.96f;
        if (cover_pixels != NULL) {
            render_blit_rgb565_circle_rotated(surface, cover_pixels, LIBRARY_COVER_SIZE_PX,
                                              LIBRARY_COVER_SIZE_PX, disc_x, center_y, disc_r,
                                              controller->vinyl_rotation_radians, disc_alpha);
        } else {
            render_draw_filled_circle_aa(surface, disc_x, center_y, disc_r,
                                         is_dark ? 0xFF13171Fu : 0xFF242830u, disc_alpha);
        }
        static const float GROOVE_RATIOS[4] = {0.88f, 0.78f, 0.68f, 0.48f};
        float groove_lw = fmaxf(0.8f, disc_r * 0.008f);
        for (uint32_t g_idx = 0u; g_idx < 4u; g_idx++) {
            render_draw_arc_aa(surface, disc_x, center_y, disc_r * GROOVE_RATIOS[g_idx], groove_lw,
                               0.0f, FULL_CIRCLE_RADIANS, 0xFFF5F6F8u,
                               disc_alpha * (is_dark ? 0.10f : 0.13f));
        }
    }

    struct clip_rect sleeve_clip = {
        (int32_t)floorf(center_x - half_s),
        (int32_t)floorf(center_y - half_s),
        (int32_t)ceilf(center_x + half_s),
        (int32_t)ceilf(center_y + half_s),
    };

    if (cover_pixels != NULL) {
        render_blit_rgb565_scaled_clipped(surface, cover_pixels, LIBRARY_COVER_SIZE_PX,
                                          LIBRARY_COVER_SIZE_PX, center_x - half_s,
                                          center_y - half_s, sleeve_size, sleeve_size, &sleeve_clip,
                                          base_alpha);
    } else {
        draw_geometric_motif(surface, album->motif_id, center_x, center_y, half_s, &sleeve_clip,
                             palette, is_dark, base_alpha);
    }

    /* Scroll-parallax diagonal glare */
    float glare_shift = delta * 26.0f;
    struct point_2d glare_quad[4] = {
        {center_x - 0.95f * half_s + glare_shift, center_y - half_s},
        {center_x - 0.38f * half_s + glare_shift, center_y - half_s},
        {center_x - 1.12f * half_s + glare_shift, center_y + half_s},
        {center_x - 1.68f * half_s + glare_shift, center_y + half_s},
    };
    struct point_2d sleeve_quad[4] = {
        {center_x - half_s, center_y - half_s},
        {center_x + half_s, center_y - half_s},
        {center_x + half_s, center_y + half_s},
        {center_x - half_s, center_y + half_s},
    };
    struct point_2d clipped_glare[8];
    uint32_t glare_verts =
        render_clip_polygon_to_convex_quad(glare_quad, 4u, sleeve_quad, clipped_glare);
    if (glare_verts >= 3u) {
        render_draw_filled_convex_polygon_fast(surface, clipped_glare, glare_verts, 0xFFFFFFFFu,
                                               base_alpha * (is_dark ? 0.11f : 0.30f));
    }

    render_draw_stroked_rect_alpha(surface, center_x - half_s, center_y - half_s, sleeve_size,
                                   sleeve_size, focus_weight > 0.5f ? 1.6f : 1.15f,
                                   palette->hairline_strong, base_alpha);

    if (focus_weight > 0.5f) {
        render_draw_rect_corner_brackets(surface, center_x - half_s, center_y - half_s,
                                         sleeve_size, sleeve_size, 12.0f, 2.3f,
                                         palette->signal_highlight, base_alpha * focus_weight);
    }

    if (album->is_favorite) {
        float badge_size = roundf(sleeve_size * 0.21f);
        float bx = center_x + half_s - badge_size - 5.0f;
        float by = center_y - half_s + 5.0f;
        render_draw_chamfer_plate_filled_aa(surface, bx, by, badge_size, badge_size, 4.0f,
                                            is_dark ? 0xFF060709u : 0xFFF5F6F8u,
                                            base_alpha * (is_dark ? 0.82f : 0.90f));
        render_draw_chamfer_plate_stroked_aa(surface, bx, by, badge_size, badge_size, 4.0f, 1.25f,
                                             palette->signal_highlight, base_alpha);
        render_draw_heart_filled_aa(surface, bx + badge_size * 0.5f, by + badge_size * 0.5f + 0.5f,
                                    badge_size * 0.29f, palette->signal_highlight, base_alpha);
    }
}

static void draw_standard_header(const struct framebuffer_surface *surface, const char *title,
                                 const char *subtitle, float drift_y,
                                 const struct appearance_palette *palette, float alpha) {
    render_draw_filled_rect_alpha(surface, 166.0f, 20.0f + drift_y, 28.0f, 3.0f,
                                  palette->hairline_strong, alpha * 0.65f);
    render_draw_marquee_text(surface, FONT_STYLE_DISPLAY_15, title, (float)SCREEN_CENTER_X,
                             44.0f + drift_y, 184.0f, 0.0f, palette->ink_primary, alpha);
    render_draw_marquee_text(surface, FONT_STYLE_MONO_9, subtitle, (float)SCREEN_CENTER_X,
                             60.0f + drift_y, 196.0f, 0.0f, palette->ink_secondary, alpha);
    render_draw_line_aa(surface, 128.0f, 72.0f + drift_y, 232.0f, 72.0f + drift_y, 1.0f,
                        palette->hairline_strong, alpha * 0.7f);
}

static void draw_segmented_header(const struct framebuffer_surface *surface, const char *title,
                                  float blend, const struct appearance_palette *palette,
                                  bool is_dark, float alpha) {
    render_draw_filled_rect_alpha(surface, 166.0f, 20.0f, 28.0f, 3.0f, palette->hairline_strong,
                                  alpha * 0.65f);
    render_draw_marquee_text(surface, FONT_STYLE_DISPLAY_15, title, (float)SCREEN_CENTER_X, 38.0f,
                             184.0f, 0.0f, palette->ink_primary, alpha);

    render_draw_chamfer_plate_filled_aa(surface, 106.0f, 53.0f, 148.0f, 20.0f, 5.0f,
                                        is_dark ? 0xFFD2E6FFu : 0xFFFFFFFFu,
                                        alpha * (is_dark ? 0.08f : 0.56f));
    render_draw_chamfer_plate_stroked_aa(surface, 106.0f, 53.0f, 148.0f, 20.0f, 5.0f, 1.0f,
                                         is_dark ? 0xFFEBF5FFu : 0xFF2E3238u,
                                         alpha * (is_dark ? 0.24f : 0.22f));

    struct clip_rect pill_clip = {106, 53, 254, 73};
    float active_x = render_lerp_f32(106.0f, 180.0f, render_ease_in_out_cubic(blend));
    render_draw_chamfer_plate_filled_clipped_aa(surface, active_x, 53.0f, 74.0f, 20.0f, 5.0f,
                                                &pill_clip, palette->signal_highlight, alpha);

    render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "ALBUMS", 143.0f, 63.5f, TEXT_ALIGN_CENTER,
                           TEXT_BASELINE_MIDDLE,
                           blend < 0.5f ? palette->signal_highlight_ink : palette->ink_secondary,
                           alpha);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "SONGS", 217.0f, 63.5f, TEXT_ALIGN_CENTER,
                           TEXT_BASELINE_MIDDLE,
                           blend >= 0.5f ? palette->signal_highlight_ink : palette->ink_secondary,
                           alpha);
    render_draw_line_aa(surface, 128.0f, 79.0f, 232.0f, 79.0f, 1.0f, palette->hairline_strong,
                        alpha * 0.7f);
}

static void draw_right_scroll_arc(const struct framebuffer_surface *surface, float scroll_pos,
                                  uint32_t max_scroll, uint32_t total_rows,
                                  const struct appearance_palette *palette, float alpha) {
    if (total_rows <= 2u || max_scroll == 0u || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 166.0f, 2.5f,
                       -0.44f, 0.44f, palette->hairline, alpha * 0.85f);
    float visible_ratio = render_clamp_f32(2.0f / (float)total_rows, 0.22f, 0.72f);
    float span = 0.88f * visible_ratio;
    float progress = render_clamp_f32(scroll_pos / (float)max_scroll, 0.0f, 1.0f);
    float start_a = -0.44f + (0.88f - span) * progress;
    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 166.0f, 3.2f,
                       start_a, start_a + span, palette->signal_highlight, alpha * 0.85f);
}

static void draw_hub_tiles(const struct framebuffer_surface *surface,
                           const struct library_controller *controller, float scroll_pair,
                           float elapsed_seconds, float tile_alpha, float scale_factor,
                           bool part_non_albums, float part_factor,
                           const struct appearance_palette *palette, bool is_dark) {
    static const char *TILE_NAMES[6] = {"Favorites", "Playlists", "Artists",
                                        "Albums",    "Genres",    "All songs"};
    const struct library_catalog *catalog = controller->catalog;
    uint32_t counts[6] = {0u, 0u, 0u, 0u, 0u, 0u};
    if (catalog != NULL) {
        counts[0] = catalog->favorite_track_count + catalog->favorite_album_count;
        counts[1] = catalog->playlist_count;
        counts[2] = catalog->artist_count;
        counts[3] = catalog->album_count;
        counts[4] = catalog->genre_count;
        counts[5] = catalog->track_count;
    }

    struct clip_rect tile_clip = {0, 76, (int32_t)SCREEN_COORDINATE_MAX,
                                  (int32_t)SCREEN_COORDINATE_MAX};

    for (uint32_t idx = 0u; idx < LIB_HUB_SECTION_COUNT; idx++) {
        struct library_hub_tile_rect rect;
        library_compute_hub_tile_rect(idx, scroll_pair, &rect);

        float cx = render_lerp_f32((float)SCREEN_CENTER_X, rect.center_x, scale_factor);
        float cy = render_lerp_f32(198.0f, rect.center_y, scale_factor);
        float alpha = rect.alpha * tile_alpha;
        bool hide_albums_shell = (part_non_albums && idx == LIB_HUB_ALBUMS);

        if (part_non_albums) {
            if (idx != LIB_HUB_ALBUMS) {
                cx += ((idx % 2u == 0u) ? -95.0f : 95.0f) * part_factor;
                cy += ((rect.center_y < 278.0f) ? -24.0f : 24.0f) * part_factor;
                alpha *= (1.0f - part_factor);
            } else {
                alpha *= (1.0f - render_normalize_range(part_factor, 0.0f, 0.30f));
            }
        }
        if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
            continue;
        }

        float press = (controller->pressed_row_index == (int32_t)idx)
                          ? controller->pressed_row_intensity
                          : 0.0f;
        float press_scale = render_lerp_f32(1.0f, 0.96f, press);
        float w = rect.width * scale_factor * press_scale;
        float h = rect.height * scale_factor * press_scale;
        float left = cx - w * 0.5f;
        float top = cy - h * 0.5f;
        float chamfer_scale = render_clamp_f32(w / 114.0f, 0.5f, 1.0f);
        float outer_chamfer = 12.0f * chamfer_scale;
        bool is_pressed = (press > 0.10f);

        render_draw_chamfer_plate_filled_clipped_aa(
            surface, left, top, w, h, outer_chamfer, &tile_clip,
            is_pressed ? palette->signal_highlight : (is_dark ? 0xFFD2E6FFu : 0xFFFFFFFFu),
            alpha * (is_pressed ? 0.16f : (is_dark ? 0.065f : 0.54f)));
        render_draw_chamfer_plate_stroked_aa(
            surface, left, top, w, h, outer_chamfer, is_pressed ? 1.6f : 1.1f,
            is_pressed ? palette->signal_highlight : palette->hairline_strong, alpha);
        if (press > 0.04f) {
            render_draw_rect_corner_brackets_clipped(surface, left, top, w, h,
                                                     12.0f * chamfer_scale, 2.3f, &tile_clip,
                                                     palette->signal_highlight, alpha * press);
        }

        float size_scale = w / (rect.is_peeking ? 94.0f : 114.0f);
        float icon_r = (rect.is_peeking ? 15.0f : 19.0f) * size_scale;
        if (!hide_albums_shell) {
            draw_living_icon(surface, (enum library_hub_section_id)idx, cx, cy - 14.0f * size_scale,
                             icon_r, elapsed_seconds, is_pressed, palette, is_dark, alpha);
        }

        enum font_style_id title_font = rect.is_peeking ? FONT_STYLE_DISPLAY_11 : FONT_STYLE_DISPLAY_13;
        render_draw_marquee_text_rect_clipped(surface, title_font, TILE_NAMES[idx], cx,
                                              cy + 25.0f * size_scale, fmaxf(40.0f, w - 14.0f),
                                              elapsed_seconds, &tile_clip, palette->ink_primary,
                                              alpha);
        char count_str[24];
        format_comma_u32(counts[idx], count_str, sizeof(count_str));
        render_draw_text_rect_clipped_alpha(surface, FONT_STYLE_MONO_9, count_str, cx,
                                            cy + 41.0f * size_scale, TEXT_ALIGN_CENTER,
                                            TEXT_BASELINE_MIDDLE, &tile_clip,
                                            palette->ink_secondary, alpha);
    }
}

static void draw_mini_equalizer(const struct framebuffer_surface *surface, float right_x,
                                float center_y, float elapsed_seconds,
                                const struct appearance_palette *palette, float alpha) {
    float bar_w = 2.6f;
    float gap = 2.2f;
    float total_w = 3.0f * bar_w + 2.0f * gap;
    float start_x = right_x - total_w;
    for (uint32_t idx = 0u; idx < 3u; idx++) {
        float osc = 0.5f + 0.5f * sinf(elapsed_seconds * (5.2f + (float)idx * 1.4f) + (float)idx * 1.7f);
        float bar_h = 3.5f + (0.25f + 0.75f * osc) * 9.5f;
        render_draw_filled_rect_alpha(surface, start_x + (float)idx * (bar_w + gap),
                                      center_y + 6.0f - bar_h, bar_w, bar_h,
                                      palette->signal_highlight, alpha);
    }
}

static void draw_curved_list_body(const struct framebuffer_surface *surface,
                                  const struct library_controller *controller,
                                  const struct library_view_frame *view, float slide_x,
                                  float slide_y, float body_alpha,
                                  const struct appearance_palette *palette, bool is_dark) {
    if (view == NULL || view->item_count == 0u || body_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    const struct library_catalog *catalog = controller->catalog;
    if (catalog == NULL) {
        return;
    }

    struct clip_rect list_clip = {0, 76, (int32_t)SCREEN_COORDINATE_MAX,
                                  (int32_t)SCREEN_COORDINATE_MAX};

    int32_t min_row = (int32_t)floorf(view->scroll_position) - 1;
    int32_t max_row = (int32_t)ceilf(view->scroll_position) + 3;
    if (min_row < 0) {
        min_row = 0;
    }
    if ((uint32_t)max_row >= view->item_count) {
        max_row = (int32_t)(view->item_count - 1u);
    }

    for (int32_t row = min_row; row <= max_row; row++) {
        struct library_curved_row_layout layout;
        library_compute_curved_row_layout((uint32_t)row, view->scroll_position, &layout);
        float alpha = layout.alpha * body_alpha;
        if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
            continue;
        }
        float left = (float)SCREEN_CENTER_X - layout.row_half_width + slide_x;
        float right = (float)SCREEN_CENTER_X + layout.row_half_width + slide_x;
        float row_y = layout.row_y + slide_y;
        float right_pad = right - 10.0f;

        float press = (controller->pressed_row_index == row) ? controller->pressed_row_intensity : 0.0f;
        if (controller->tap_flash_index == row && controller->tap_flash_intensity > press) {
            press = controller->tap_flash_intensity;
        }
        if (press > 0.01f) {
            render_draw_chamfer_plate_filled_clipped_aa(
                surface, left, row_y - layout.row_height * 0.5f, layout.row_half_width * 2.0f,
                layout.row_height, 8.0f, &list_clip, palette->signal_highlight,
                alpha * press * (is_dark ? 0.16f : 0.20f));
            render_draw_rect_corner_brackets_clipped(
                surface, left, row_y - layout.row_height * 0.5f, layout.row_half_width * 2.0f,
                layout.row_height, 10.0f, 2.1f, &list_clip, palette->signal_highlight,
                alpha * press);
        } else {
            render_draw_line_fast(surface, left + 12.0f, row_y + layout.row_height * 0.5f,
                                  right - 12.0f, row_y + layout.row_height * 0.5f,
                                  palette->hairline, alpha);
        }

        uint32_t mapped_idx = view->item_indices[row];
        if (view->kind == LIB_VIEW_TRACKS) {
            const struct library_track *track = &catalog->tracks[mapped_idx];
            bool is_active =
                controller->has_active_track && controller->active_track_id == track->track_id;
            float text_origin_x = left + 40.0f;
            if (view->show_track_numbers) {
                char num_str[8];
                snprintf(num_str, sizeof(num_str), "%02u", (unsigned)(row + 1));
                render_draw_text_rect_clipped_alpha(
                    surface, FONT_STYLE_MONO_10, num_str, left + 12.0f, row_y + 0.5f,
                    TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE, &list_clip,
                    is_active ? palette->signal_highlight : palette->ink_secondary, alpha);
                text_origin_x = left + 36.0f;
            } else {
                draw_track_ring_icon(surface, left + 20.0f, row_y, layout.is_preview ? 8.0f : 10.0f,
                                     controller->elapsed_seconds, is_active, palette, alpha);
            }

            float right_reserved = 28.0f;
            bool row_in_heart_clip =
                (row_y >= (float)list_clip.top_y + 5.0f && row_y <= (float)list_clip.bottom_y - 5.0f);
            if (is_active) {
                draw_mini_equalizer(surface, right_pad, row_y, controller->elapsed_seconds, palette,
                                    alpha);
                if (track->is_favorite && row_in_heart_clip) {
                    render_draw_heart_filled_aa(surface, right_pad - 24.0f, row_y + 0.5f, 4.5f,
                                                palette->signal_highlight, alpha);
                    right_reserved = 38.0f;
                }
            } else {
                char duration_text[16];
                format_duration_mmss(track->duration_seconds, duration_text, sizeof(duration_text));
                int32_t duration_width =
                    font_atlas_measure_width(font_atlas_get(FONT_STYLE_MONO_10), duration_text);
                if (track->is_favorite && row_in_heart_clip) {
                    render_draw_heart_filled_aa(surface,
                                                right_pad - (float)duration_width - 10.0f,
                                                row_y + 0.5f, 4.5f, palette->signal_highlight,
                                                alpha);
                    right_reserved = (float)duration_width + 24.0f;
                } else {
                    right_reserved = (float)duration_width + 10.0f;
                }
                render_draw_text_rect_clipped_alpha(surface, FONT_STYLE_MONO_10, duration_text,
                                                    right_pad, row_y + 0.5f, TEXT_ALIGN_RIGHT,
                                                    TEXT_BASELINE_MIDDLE, &list_clip,
                                                    palette->ink_secondary, alpha);
            }

            float max_text_w = fmaxf(40.0f, (right_pad - right_reserved) - text_origin_x);
            const char *title = library_catalog_get_string(catalog, track->title_offset);
            uint32_t title_col = is_active ? palette->signal_highlight : palette->ink_primary;
            if (layout.is_preview) {
                render_draw_text_rect_clipped_alpha(surface, FONT_STYLE_DISPLAY_13, title,
                                                    text_origin_x, row_y + 0.5f, TEXT_ALIGN_LEFT,
                                                    TEXT_BASELINE_MIDDLE, &list_clip, title_col,
                                                    alpha);
            } else if (view->show_track_numbers) {
                render_draw_marquee_text_rect_clipped(
                    surface, FONT_STYLE_DISPLAY_15, title, text_origin_x + max_text_w * 0.5f,
                    row_y + 0.5f, max_text_w, controller->elapsed_seconds, &list_clip, title_col,
                    alpha);
            } else {
                render_draw_marquee_text_rect_clipped(
                    surface, FONT_STYLE_DISPLAY_15, title, text_origin_x + max_text_w * 0.5f,
                    row_y - 8.0f, max_text_w, controller->elapsed_seconds, &list_clip, title_col,
                    alpha);
                const char *sub = library_catalog_get_string(
                    catalog, view->show_album_subtitle ? track->album_offset : track->artist_offset);
                render_draw_marquee_text_rect_clipped(
                    surface, FONT_STYLE_MONO_9, sub, text_origin_x + max_text_w * 0.5f,
                    row_y + 9.0f, max_text_w, controller->elapsed_seconds, &list_clip,
                    palette->ink_secondary, alpha);
            }
        } else {
            enum library_hub_section_id icon_kind = LIB_HUB_ARTISTS;
            const char *label = "";
            uint32_t count_val = 0u;
            bool is_fav = false;
            if (view->kind == LIB_VIEW_ARTISTS) {
                icon_kind = LIB_HUB_ARTISTS;
                label = library_catalog_get_string(catalog, catalog->artists[mapped_idx].name_offset);
                count_val = catalog->artists[mapped_idx].album_count;
                is_fav = catalog->artists[mapped_idx].is_favorite;
            } else if (view->kind == LIB_VIEW_GENRES) {
                icon_kind = LIB_HUB_GENRES;
                label = library_catalog_get_string(catalog, catalog->genres[mapped_idx].name_offset);
                count_val = catalog->genres[mapped_idx].track_count;
            } else if (view->kind == LIB_VIEW_PLAYLISTS) {
                icon_kind = LIB_HUB_PLAYLISTS;
                label =
                    library_catalog_get_string(catalog, catalog->playlists[mapped_idx].name_offset);
                count_val = catalog->playlists[mapped_idx].track_count;
            }

            draw_living_icon(surface, icon_kind, left + 22.0f, row_y,
                             layout.is_preview ? 9.0f : 11.0f, controller->elapsed_seconds, false,
                             palette, is_dark, alpha);

            char count_str[24];
            format_comma_u32(count_val, count_str, sizeof(count_str));
            int32_t count_w = font_atlas_measure_width(font_atlas_get(FONT_STYLE_MONO_10), count_str);
            render_draw_text_rect_clipped_alpha(surface, FONT_STYLE_MONO_10, count_str, right_pad,
                                                row_y + 0.5f, TEXT_ALIGN_RIGHT,
                                                TEXT_BASELINE_MIDDLE, &list_clip,
                                                palette->ink_secondary, alpha);
            if (is_fav) {
                render_draw_heart_filled_aa(surface, right_pad - (float)count_w - 10.0f,
                                            row_y + 0.5f, 4.5f, palette->signal_highlight, alpha);
            }
            float right_reserved = (float)count_w + (is_fav ? 24.0f : 10.0f);
            float max_w = fmaxf(40.0f, (right_pad - right_reserved) - (left + 44.0f));
            render_draw_marquee_text_rect_clipped(
                surface, layout.is_preview ? FONT_STYLE_DISPLAY_13 : FONT_STYLE_DISPLAY_15, label,
                left + 44.0f + max_w * 0.5f, row_y + 0.5f, max_w, controller->elapsed_seconds,
                &list_clip, palette->ink_primary, alpha);
        }
    }
}

static void draw_sort_button_glyph(const struct framebuffer_surface *surface, float center_x,
                                   float center_y, enum library_sort_mode mode, bool active_tint,
                                   const struct appearance_palette *palette, float alpha) {
    uint32_t col = active_tint ? palette->signal_highlight : palette->ink_primary;
    render_draw_line_aa(surface, center_x - 8.0f, center_y - 4.5f, center_x + 1.0f,
                        center_y - 4.5f, 1.5f, col, alpha);
    render_draw_line_aa(surface, center_x - 8.0f, center_y, center_x - 1.5f, center_y, 1.5f, col,
                        alpha);
    render_draw_line_aa(surface, center_x - 8.0f, center_y + 4.5f, center_x - 4.0f,
                        center_y + 4.5f, 1.5f, col, alpha);

    if (mode == LIBRARY_SORT_LIKED_FIRST) {
        render_draw_heart_filled_aa(surface, center_x + 5.5f, center_y + 2.5f, 4.2f, col, alpha);
    } else if (mode == LIBRARY_SORT_RECENT) {
        render_draw_line_aa(surface, center_x + 5.5f, center_y - 5.5f, center_x + 5.5f,
                            center_y + 5.5f, 1.5f, col, alpha);
        render_draw_line_aa(surface, center_x + 2.5f, center_y + 2.5f, center_x + 5.5f,
                            center_y + 5.5f, 1.5f, col, alpha);
        render_draw_line_aa(surface, center_x + 8.5f, center_y + 2.5f, center_x + 5.5f,
                            center_y + 5.5f, 1.5f, col, alpha);
    } else if (mode == LIBRARY_SORT_OLDEST) {
        render_draw_line_aa(surface, center_x + 5.5f, center_y - 5.5f, center_x + 5.5f,
                            center_y + 5.5f, 1.5f, col, alpha);
        render_draw_line_aa(surface, center_x + 2.5f, center_y - 2.5f, center_x + 5.5f,
                            center_y - 5.5f, 1.5f, col, alpha);
        render_draw_line_aa(surface, center_x + 8.5f, center_y - 2.5f, center_x + 5.5f,
                            center_y - 5.5f, 1.5f, col, alpha);
    } else {
        const char *tag = (mode == LIBRARY_SORT_REVERSE_ALPHABETICAL) ? "ZA" : "AZ";
        render_draw_text_alpha(surface, FONT_STYLE_MONO_8, tag, center_x + 5.0f, center_y + 3.0f,
                               TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, col, alpha);
    }
}

static void draw_floating_circle_button(const struct framebuffer_surface *surface, float center_x,
                                        float center_y, bool highlighted_border,
                                        const struct appearance_palette *palette, bool is_dark,
                                        float alpha) {
    render_draw_filled_circle_aa(surface, center_x, center_y, LIBRARY_BOTTOM_BTN_RADIUS_PX,
                                 is_dark ? 0xFF12161Eu : 0xFFFFFFFFu,
                                 alpha * (is_dark ? 0.88f : 0.84f));
    render_draw_arc_aa(surface, center_x, center_y, LIBRARY_BOTTOM_BTN_RADIUS_PX, 1.4f, 0.0f,
                       FULL_CIRCLE_RADIANS,
                       highlighted_border ? palette->signal_highlight : palette->hairline_strong,
                       alpha);
}

static void draw_note_button_glyph(const struct framebuffer_surface *surface, float center_x,
                                   float center_y, uint32_t color, float alpha) {
    render_draw_filled_rect_alpha(surface, center_x - 5.5f, center_y + 2.5f, 4.5f, 3.6f, color,
                                  alpha);
    render_draw_filled_rect_alpha(surface, center_x + 1.5f, center_y + 1.0f, 4.5f, 3.6f, color,
                                  alpha);
    render_draw_line_aa(surface, center_x - 1.2f, center_y + 4.0f, center_x - 1.2f, center_y - 5.0f,
                        1.65f, color, alpha);
    render_draw_line_aa(surface, center_x - 1.2f, center_y - 5.0f, center_x + 5.8f, center_y - 6.5f,
                        1.65f, color, alpha);
    render_draw_line_aa(surface, center_x + 5.8f, center_y - 6.5f, center_x + 5.8f, center_y + 2.5f,
                        1.65f, color, alpha);
}

static void draw_empty_state(const struct framebuffer_surface *surface,
                             enum library_hub_section_id icon, const char *title,
                             const char *message, float center_y, bool show_cta,
                             float elapsed_seconds, const struct appearance_palette *palette,
                             bool is_dark, float alpha) {
    float breath = 1.0f + sinf(elapsed_seconds * 2.1f) * 0.045f;
    render_draw_filled_circle_aa(surface, (float)SCREEN_CENTER_X, center_y,
                                 (show_cta ? 34.0f : 32.0f) * breath, palette->surface,
                                 alpha * 0.90f);
    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, center_y,
                       (show_cta ? 42.0f : 40.0f) * breath, 1.6f, -0.68f * HALF_CIRCLE_RADIANS,
                       0.12f * HALF_CIRCLE_RADIANS, palette->signal_highlight, alpha);
    draw_living_icon(surface, icon, (float)SCREEN_CENTER_X, center_y, show_cta ? 17.0f : 16.0f,
                     elapsed_seconds, false, palette, is_dark, alpha);

    render_draw_text_alpha(surface, show_cta ? FONT_STYLE_DISPLAY_20 : FONT_STYLE_DISPLAY_18, title,
                           (float)SCREEN_CENTER_X, center_y + 64.0f, TEXT_ALIGN_CENTER,
                           TEXT_BASELINE_MIDDLE, palette->ink_primary, alpha);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_9, message, (float)SCREEN_CENTER_X,
                           center_y + 86.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->ink_secondary, alpha);

    if (show_cta) {
        render_draw_chamfer_plate_filled_aa(surface, 97.0f, 242.0f, 166.0f, 36.0f, 8.0f,
                                            palette->signal_highlight, alpha);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "BACK TO LIBRARY",
                               (float)SCREEN_CENTER_X, 260.5f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, palette->signal_highlight_ink, alpha);
    }
}

static void draw_albums_crate_body(const struct framebuffer_surface *surface,
                                   struct library_controller *controller,
                                   const struct library_view_frame *view, float body_alpha,
                                   float crate_factor,
                                   const struct appearance_palette *palette, bool is_dark) {
    if (view == NULL || view->item_count == 0u || body_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        if (view != NULL && view->is_favorites_detail && view->item_count == 0u) {
            draw_empty_state(surface, LIB_HUB_ALBUMS, "No Favorite Albums",
                             "HOLD AN ALBUM SLEEVE TO LIKE", 152.0f, false,
                             controller->elapsed_seconds, palette, is_dark, body_alpha);
        }
        return;
    }

    float shelf_half = render_lerp_f32(24.0f, 118.0f, render_normalize_range(crate_factor, 0.36f, 0.85f));
    render_draw_line_aa(surface, (float)SCREEN_CENTER_X - shelf_half, 224.0f,
                        (float)SCREEN_CENTER_X + shelf_half, 224.0f, 1.0f, palette->hairline_strong,
                        0.45f * body_alpha * render_normalize_range(crate_factor, 0.36f, 0.85f));

    int32_t min_idx = (int32_t)floorf(view->scroll_position) - 3;
    int32_t max_idx = (int32_t)ceilf(view->scroll_position) + 3;
    if (min_idx < 0) {
        min_idx = 0;
    }
    if ((uint32_t)max_idx >= view->item_count) {
        max_idx = (int32_t)(view->item_count - 1u);
    }

    int32_t draw_order[8];
    uint32_t draw_count = 0u;
    for (int32_t idx = min_idx; idx <= max_idx && draw_count < 8u; idx++) {
        if (fabsf((float)idx - view->scroll_position) <= 1.85f) {
            draw_order[draw_count++] = idx;
        }
    }
    for (uint32_t pass = 1u; pass < draw_count; pass++) {
        for (uint32_t cur = pass; cur > 0u; cur--) {
            float d_prev = fabsf((float)draw_order[cur - 1u] - view->scroll_position);
            float d_cur = fabsf((float)draw_order[cur] - view->scroll_position);
            if (d_prev < d_cur) {
                int32_t tmp = draw_order[cur - 1u];
                draw_order[cur - 1u] = draw_order[cur];
                draw_order[cur] = tmp;
            }
        }
    }

    float neighbor_ease = render_ease_out_expo(render_normalize_range(crate_factor, 0.34f, 0.96f));
    float neighbor_alpha = render_normalize_range(crate_factor, 0.36f, 0.92f);

    for (uint32_t slot = 0u; slot < draw_count; slot++) {
        int32_t idx = draw_order[slot];
        float delta = (float)idx - view->scroll_position;
        float abs_delta = fabsf(delta);
        float focus_weight = render_clamp_f32(1.0f - abs_delta, 0.0f, 1.0f);
        bool is_hero = (abs_delta < 0.5f);

        float sleeve_size =
            render_lerp_f32(86.0f, 120.0f, render_ease_in_out_cubic(focus_weight));
        float cx = (float)SCREEN_CENTER_X - focus_weight * 8.0f + delta * LIBRARY_CRATE_SPACING_PX;
        float cy = LIBRARY_CRATE_CENTER_Y + fminf(abs_delta, 1.5f) * 6.0f;
        float alpha = (1.0f - render_normalize_range(abs_delta, 1.15f, 1.85f)) *
                      render_lerp_f32(0.32f, 1.0f, render_ease_in_out_cubic(focus_weight)) *
                      body_alpha;

        if (crate_factor < 0.999f) {
            if (is_hero) {
                struct library_hub_tile_rect tile_rect;
                library_compute_hub_tile_rect(LIB_HUB_ALBUMS, controller->view_stack[0].scroll_position,
                                              &tile_rect);
                float start_cx = tile_rect.center_x - 19.0f * tile_rect.size_scale * 0.24f;
                float start_cy = tile_rect.center_y - 14.0f * tile_rect.size_scale;
                float start_size = 2.0f * 19.0f * tile_rect.size_scale * 0.70f;
                cx = render_lerp_f32(start_cx, cx, render_ease_in_out_cubic(crate_factor));
                cy = render_lerp_f32(start_cy, cy, render_ease_in_out_cubic(crate_factor));
                sleeve_size = render_lerp_f32(start_size, sleeve_size,
                                              render_ease_out_cubic(crate_factor));
            } else {
                sleeve_size *= render_lerp_f32(0.86f, 1.0f, neighbor_ease);
                cx += (delta < 0.0f ? -120.0f : 120.0f) * (1.0f - neighbor_ease);
                alpha *= neighbor_alpha;
            }
        }

        uint32_t album_idx = view->item_indices[idx];
        const struct library_album *album = &controller->catalog->albums[album_idx];
        if (controller->has_active_track && controller->is_playing &&
            controller->active_album_id == album->album_id) {
            cy += sinf(controller->elapsed_seconds * 1.7f + (float)idx * 1.4f) *
                  render_lerp_f32(0.8f, 2.2f, focus_weight) * crate_factor;
        }

        draw_crate_album_sleeve_and_disc(surface, controller, album_idx, cx, cy, sleeve_size,
                                         delta, focus_weight, alpha, true, palette, is_dark);
    }

    float bottom_ease = render_ease_out_expo(render_normalize_range(crate_factor, 0.42f, 0.96f));
    float bottom_alpha = body_alpha * bottom_ease;
    float bottom_dy = (1.0f - bottom_ease) * 22.0f;

    int32_t focused = (int32_t)lroundf(view->scroll_position);
    if (focused < 0) {
        focused = 0;
    }
    if ((uint32_t)focused >= view->item_count) {
        focused = (int32_t)(view->item_count - 1u);
    }
    float offset = view->scroll_position - (float)focused;
    float text_alpha = bottom_alpha * render_clamp_f32(1.0f - fabsf(offset) * 1.8f, 0.0f, 1.0f);
    float text_dx = -offset * 24.0f;

    const struct library_album *focused_album = &controller->catalog->albums[view->item_indices[focused]];
    render_draw_marquee_text(surface, FONT_STYLE_DISPLAY_18,
                             library_catalog_get_string(controller->catalog,
                                                        focused_album->title_offset),
                             (float)SCREEN_CENTER_X + text_dx, 246.0f + bottom_dy, 224.0f,
                             controller->elapsed_seconds, palette->ink_primary, text_alpha);
    char meta_line[160];
    snprintf(meta_line, sizeof(meta_line), "%s \xC2\xB7 %u TRACKS",
             library_catalog_get_string(controller->catalog, focused_album->artist_offset),
             (unsigned)focused_album->track_count);
    render_draw_marquee_text(surface, FONT_STYLE_MONO_9, meta_line, (float)SCREEN_CENTER_X + text_dx,
                             268.0f + bottom_dy, 214.0f, controller->elapsed_seconds,
                             palette->ink_secondary, text_alpha);

    /* Windowed 7-bar indicator strip */
    struct library_crate_bars_window bars;
    library_compute_windowed_crate_bars(view->item_count, view->scroll_position, &bars);
    float total_bar_w = 0.0f;
    float bar_widths[7];
    for (uint32_t slot = 0u; slot < bars.window_count; slot++) {
        uint32_t idx = bars.start_index + slot;
        float focus = render_clamp_f32(1.0f - fabsf(view->scroll_position - (float)idx), 0.0f, 1.0f);
        bar_widths[slot] = render_lerp_f32(6.0f, 18.0f, render_ease_in_out_cubic(focus));
        total_bar_w += bar_widths[slot] + (slot > 0u ? 6.0f : 0.0f);
    }
    float bx = (float)SCREEN_CENTER_X - total_bar_w * 0.5f;
    for (uint32_t slot = 0u; slot < bars.window_count; slot++) {
        uint32_t idx = bars.start_index + slot;
        float focus = render_clamp_f32(1.0f - fabsf(view->scroll_position - (float)idx), 0.0f, 1.0f);
        render_draw_filled_rect_alpha(
            surface, bx, 292.0f + bottom_dy, bar_widths[slot], 5.0f,
            focus > 0.5f ? palette->signal_highlight : palette->hairline_strong,
            bottom_alpha * render_lerp_f32(0.55f, 1.0f, focus));
        bx += bar_widths[slot] + 6.0f;
    }

    enum library_sort_mode album_sort = library_controller_effective_sort_mode(controller, view);
    float sort_bob = sinf(controller->elapsed_seconds * 2.4f + 1.2f) * 1.1f;
    float note_bob = sinf(controller->elapsed_seconds * 2.4f) * 1.1f;
    draw_floating_circle_button(surface, LIBRARY_CRATE_SORT_BTN_X,
                                LIBRARY_CRATE_SORT_BTN_Y + sort_bob + bottom_dy,
                                album_sort != LIBRARY_SORT_ALPHABETICAL, palette, is_dark,
                                bottom_alpha);
    draw_sort_button_glyph(surface, LIBRARY_CRATE_SORT_BTN_X,
                           LIBRARY_CRATE_SORT_BTN_Y + sort_bob + bottom_dy, album_sort,
                           album_sort != LIBRARY_SORT_ALPHABETICAL, palette, bottom_alpha);

    draw_floating_circle_button(surface, LIBRARY_CRATE_NOTE_BTN_X,
                                LIBRARY_CRATE_NOTE_BTN_Y + note_bob + bottom_dy, true, palette,
                                is_dark, bottom_alpha);
    draw_note_button_glyph(surface, LIBRARY_CRATE_NOTE_BTN_X,
                           LIBRARY_CRATE_NOTE_BTN_Y + note_bob + bottom_dy,
                           palette->signal_highlight, bottom_alpha);
}

static void draw_overlays(const struct framebuffer_surface *surface,
                          const struct library_controller *controller,
                          const struct appearance_palette *palette, bool is_dark) {
    /* 1. Radial Half-Wheel Sort Selector */
    if (controller->sort_wheel_progress > 0.01f) {
        float progress = controller->sort_wheel_progress;
        float ease = render_ease_out_expo(progress);
        render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, (float)SCREEN_WIDTH,
                                      (float)SCREEN_HEIGHT, is_dark ? 0xFF060709u : 0xFF0E1116u,
                                      progress * (is_dark ? 0.58f : 0.52f));
        render_draw_annular_wedge_fast(surface, controller->sort_wheel_anchor_x,
                                       controller->sort_wheel_anchor_y, 52.0f * ease, 98.0f * ease,
                                       -0.96f * HALF_CIRCLE_RADIANS, -0.04f * HALF_CIRCLE_RADIANS,
                                       is_dark ? 0xFF12161Eu : 0xFFF6F7F9u, progress * 0.90f);

        const struct library_view_frame *current_view = library_controller_current_view(controller);
        bool exclude_liked_first = (current_view != NULL && current_view->is_favorites_detail);
        struct library_sort_wheel_slot slots[LIBRARY_SORT_MODE_COUNT];
        uint32_t slot_count = library_compute_sort_wheel_slots_for_view(
            controller->sort_wheel_anchor_x, controller->sort_wheel_anchor_y, exclude_liked_first,
            slots);
        enum library_sort_mode current_mode =
            library_controller_effective_sort_mode(controller, current_view);

        int32_t hovered_slot = controller->sort_wheel_hovered_slot;
        bool has_hovered = (hovered_slot >= 0 && (uint32_t)hovered_slot < slot_count);
        if (has_hovered) {
            const struct library_sort_wheel_slot *hovered_info = &slots[hovered_slot];
            render_draw_annular_wedge_fast(
                surface, controller->sort_wheel_anchor_x, controller->sort_wheel_anchor_y,
                52.0f * ease, 98.0f * ease, hovered_info->angle_radians - 0.25f,
                hovered_info->angle_radians + 0.25f, palette->signal_highlight,
                progress * (is_dark ? 0.16f : 0.20f));
            render_draw_line_aa(
                surface, controller->sort_wheel_anchor_x, controller->sort_wheel_anchor_y,
                render_lerp_f32(controller->sort_wheel_anchor_x, hovered_info->center_x, ease),
                render_lerp_f32(controller->sort_wheel_anchor_y, hovered_info->center_y, ease),
                1.5f, palette->signal_highlight, progress);
        }

        for (uint32_t slot_index = 0u; slot_index < slot_count; slot_index++) {
            float center_x =
                render_lerp_f32(controller->sort_wheel_anchor_x, slots[slot_index].center_x, ease);
            float center_y =
                render_lerp_f32(controller->sort_wheel_anchor_y, slots[slot_index].center_y, ease);
            bool hovered = (hovered_slot == (int32_t)slot_index);
            bool is_current = (current_mode == slots[slot_index].mode);
            float slot_radius = LIBRARY_SORT_WHEEL_SLOT_RADIUS_PX * (hovered ? 1.16f : 1.0f);
            render_draw_filled_circle_aa(surface, center_x, center_y, slot_radius,
                                         hovered ? palette->signal_highlight : palette->surface,
                                         progress * 0.94f);
            render_draw_arc_aa(surface, center_x, center_y, slot_radius,
                               (hovered || is_current) ? 1.6f : 1.0f, 0.0f, FULL_CIRCLE_RADIANS,
                               (hovered || is_current) ? palette->signal_highlight
                                                       : palette->hairline_strong,
                               progress);
            draw_sort_button_glyph(surface, center_x, center_y, slots[slot_index].mode,
                                   is_current && !hovered, palette, progress);
        }

        const char *pill_label =
            has_hovered ? library_sort_mode_toast_label(slots[hovered_slot].mode)
                        : "DRAG TO SORT";
        render_draw_chamfer_plate_filled_aa(surface, controller->sort_wheel_anchor_x - 68.0f, 200.0f,
                                            136.0f, 24.0f, 5.0f, palette->surface,
                                            progress * 0.94f);
        render_draw_chamfer_plate_stroked_aa(surface, controller->sort_wheel_anchor_x - 68.0f,
                                             200.0f, 136.0f, 24.0f, 5.0f, 1.2f,
                                             palette->signal_highlight, progress);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_9, pill_label,
                               controller->sort_wheel_anchor_x, 212.5f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE,
                               has_hovered ? palette->signal_highlight : palette->ink_primary,
                               progress);

        draw_floating_circle_button(surface, controller->sort_wheel_anchor_x,
                                    controller->sort_wheel_anchor_y, true, palette, is_dark,
                                    progress);
        draw_sort_button_glyph(surface, controller->sort_wheel_anchor_x,
                               controller->sort_wheel_anchor_y, current_mode, true, palette,
                               progress);
    }

    /* 2. Sort Toast */
    if (controller->sort_toast_active) {
        float toast_alpha = render_clamp_f32(
            controller->sort_toast_remaining_seconds / LIBRARY_SORT_TOAST_FADE_SECONDS, 0.0f, 1.0f);
        render_draw_chamfer_plate_filled_aa(surface, 96.0f, 139.0f, 168.0f, 26.0f, 6.0f,
                                            is_dark ? 0xFF060709u : 0xFF2E3238u,
                                            toast_alpha * 0.92f);
        render_draw_chamfer_plate_stroked_aa(surface, 96.0f, 139.0f, 168.0f, 26.0f, 6.0f, 1.25f,
                                             palette->signal_highlight, toast_alpha);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_9, controller->sort_toast_text,
                               (float)SCREEN_CENTER_X, 152.5f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, 0xFFF5F6F8u, toast_alpha);
    }

    /* 3. Pre-Quick-Seek Rim-Arc & 17-Slab Crystal Quick-Seek */
    if (controller->arc_feedback_alpha > 0.01f && controller->seek_hud_alpha <= 0.01f) {
        float a = controller->arc_feedback_alpha;
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 166.0f, 2.0f,
                           0.0f, FULL_CIRCLE_RADIANS, palette->hairline, a * 0.65f);
        float s0 = controller->arc_start_angle;
        float s1 = s0 + render_clamp_f32(controller->arc_signed_sweep, -HALF_CIRCLE_RADIANS,
                                         HALF_CIRCLE_RADIANS);
        if (s1 < s0) {
            float tmp = s0;
            s0 = s1;
            s1 = tmp;
        }
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 166.0f, 3.5f,
                           s0, s1, palette->signal_highlight, a);
        float bead_x = (float)SCREEN_CENTER_X + cosf(controller->arc_current_angle) * 166.0f;
        float bead_y = (float)SCREEN_CENTER_Y + sinf(controller->arc_current_angle) * 166.0f;
        render_draw_filled_circle_aa(surface, bead_x, bead_y, 4.5f, palette->signal_highlight, a);
    }

    if (controller->seek_hud_alpha > 0.01f) {
        float a = controller->seek_hud_alpha;
        float ease = render_ease_out_expo(a);
        render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, (float)SCREEN_WIDTH,
                                      (float)SCREEN_HEIGHT, is_dark ? 0xFF060709u : 0xFFE6E8EBu,
                                      a * (is_dark ? 0.64f : 0.70f));

        float slice_pitch = FULL_CIRCLE_RADIANS / (float)LIBRARY_RIM_SLICE_COUNT;
        for (uint32_t slice = 0u; slice < LIBRARY_RIM_SLICE_COUNT; slice++) {
            float pull = render_ease_out_cubic(controller->rim_slice_pulls[slice]);
            float center_a = TOP_ANGLE_RADIANS + ((float)slice + 0.5f) * slice_pitch;
            float span = slice_pitch * render_lerp_f32(0.56f, 0.72f, pull);
            if (pull < 0.04f) {
                render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 171.5f,
                                   1.25f, center_a - span * 0.5f, center_a + span * 0.5f,
                                   palette->hairline_strong, a * 0.42f);
            } else {
                float outer_r = render_lerp_f32(172.0f, 173.0f, pull);
                float inner_r = render_lerp_f32(170.5f, 161.0f, pull);
                render_draw_annular_wedge_fast(
                    surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, inner_r, outer_r,
                    center_a - span * 0.5f, center_a + span * 0.5f,
                    is_dark ? 0xFFD2E6FFu : 0xFFFFFFFFu,
                    a * render_lerp_f32(0.10f, is_dark ? 0.22f : 0.72f, pull));
                if (pull > 0.50f) {
                    render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y,
                                       inner_r + 0.6f, 1.6f, center_a - span * 0.5f + 0.02f,
                                       center_a + span * 0.5f - 0.02f, palette->signal_highlight,
                                       a);
                }
            }
        }

        float lens_r = render_lerp_f32(50.0f, 62.0f, ease);
        render_draw_filled_circle_aa(surface, (float)SCREEN_CENTER_X, 174.0f, lens_r,
                                     is_dark ? 0xFF12151Bu : 0xFFF8F9FBu, a * 0.93f);
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, 174.0f, lens_r, 1.6f, 0.0f,
                           FULL_CIRCLE_RADIANS, palette->signal_highlight, a);

        if (controller->seek_anchor_count > 0u) {
            int32_t n = (int32_t)controller->seek_anchor_count;
            int32_t cur_idx = controller->seek_anchor_index % n;
            const struct library_seek_anchor *cur = &controller->seek_anchors[cur_idx];
            const struct library_seek_anchor *prev =
                &controller->seek_anchors[(cur_idx - 1 + n) % n];
            const struct library_seek_anchor *next = &controller->seek_anchors[(cur_idx + 1) % n];

            if (cur->is_date_mode) {
                render_draw_text_alpha(surface, FONT_STYLE_MONO_11, prev->short_label, 106.0f, 174.0f,
                                       TEXT_ALIGN_RIGHT, TEXT_BASELINE_MIDDLE,
                                       palette->ink_secondary, a * 0.78f);
                render_draw_text_alpha(surface, FONT_STYLE_MONO_11, next->short_label, 254.0f, 174.0f,
                                       TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE,
                                       palette->ink_secondary, a * 0.78f);
                render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_32, cur->primary,
                                       (float)SCREEN_CENTER_X,
                                       cur->sub_label[0] != '\0' ? 172.0f : 178.0f,
                                       TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                                       palette->signal_highlight, a);
                if (cur->sub_label[0] != '\0') {
                    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, cur->sub_label,
                                           (float)SCREEN_CENTER_X, 198.0f, TEXT_ALIGN_CENTER,
                                           TEXT_BASELINE_MIDDLE, palette->ink_secondary, a * 0.85f);
                }
            } else {
                render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_15, prev->primary, 140.0f, 174.0f,
                                       TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                                       palette->ink_secondary, a * 0.65f);
                render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_15, next->primary, 220.0f, 174.0f,
                                       TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                                       palette->ink_secondary, a * 0.65f);
                render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_46, cur->primary,
                                       (float)SCREEN_CENTER_X, 176.0f, TEXT_ALIGN_CENTER,
                                       TEXT_BASELINE_MIDDLE, palette->signal_highlight, a);
                if (cur->is_favorite) {
                    render_draw_heart_filled_aa(surface, (float)SCREEN_CENTER_X, 212.0f, 6.0f,
                                                palette->signal_highlight, a);
                }
            }
        }
        render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "6% ARC STEP \xC2\xB7 CW NEXT \xC2\xB7 CCW PREV",
                               (float)SCREEN_CENTER_X, 286.0f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, palette->ink_secondary, a * 0.8f);
    }

    /* 4. Album / Artist / Track Quick Actions Overlay */
    if (controller->quick_actions_progress > 0.01f && controller->catalog != NULL) {
        float progress = controller->quick_actions_progress;
        float ease = render_ease_out_expo(progress);
        render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, (float)SCREEN_WIDTH,
                                      (float)SCREEN_HEIGHT, is_dark ? 0xFF060709u : 0xFF0E1116u,
                                      progress * 0.78f);

        bool is_favorite = false;
        const char *header_label = "ALBUM ACTIONS";
        const char *action_label_active = "FAVORITED";
        const char *action_label_idle = "FAVORITE";
        const char *title = "";
        char subtitle[128] = {0};

        if (controller->quick_action_kind == LIB_QA_ALBUM &&
            controller->quick_action_target_index < controller->catalog->album_count) {
            const struct library_album *album =
                &controller->catalog->albums[controller->quick_action_target_index];
            is_favorite = album->is_favorite;
            title = library_catalog_get_string(controller->catalog, album->title_offset);
            snprintf(subtitle, sizeof(subtitle), "%s \xC2\xB7 %u TRACKS",
                     library_catalog_get_string(controller->catalog, album->artist_offset),
                     (unsigned)album->track_count);
        } else if (controller->quick_action_kind == LIB_QA_ARTIST &&
                   controller->quick_action_target_index < controller->catalog->artist_count) {
            const struct library_artist *artist =
                &controller->catalog->artists[controller->quick_action_target_index];
            is_favorite = artist->is_favorite;
            header_label = "ARTIST ACTIONS";
            action_label_active = "LIKED";
            action_label_idle = "LIKE";
            title = library_catalog_get_string(controller->catalog, artist->name_offset);
            snprintf(subtitle, sizeof(subtitle), "%u ALBUMS \xC2\xB7 %u TRACKS",
                     (unsigned)artist->album_count, (unsigned)artist->track_count);
        } else if (controller->quick_action_kind == LIB_QA_TRACK &&
                   controller->quick_action_target_index < controller->catalog->track_count) {
            const struct library_track *track =
                &controller->catalog->tracks[controller->quick_action_target_index];
            is_favorite = track->is_favorite;
            header_label = "SONG ACTIONS";
            title = library_catalog_get_string(controller->catalog, track->title_offset);
            snprintf(subtitle, sizeof(subtitle), "%s \xC2\xB7 %s",
                     library_catalog_get_string(controller->catalog, track->artist_offset),
                     library_catalog_get_string(controller->catalog, track->album_offset));
        }

        float header_offset_y = render_lerp_f32(-12.0f, 0.0f, ease);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_9, header_label, (float)SCREEN_CENTER_X,
                               68.0f + header_offset_y, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               palette->signal_highlight, progress);
        render_draw_marquee_text(surface, FONT_STYLE_DISPLAY_18, title, (float)SCREEN_CENTER_X,
                                 94.0f + header_offset_y, 224.0f, controller->elapsed_seconds,
                                 0xFFF5F6F8u, progress);
        render_draw_marquee_text(surface, FONT_STYLE_MONO_10, subtitle, (float)SCREEN_CENTER_X,
                                 114.0f + header_offset_y, 244.0f, controller->elapsed_seconds,
                                 0xFFF5F6F8u, progress * 0.72f);
        render_draw_line_aa(surface, 116.0f, 132.0f + header_offset_y, 244.0f,
                            132.0f + header_offset_y, 1.0f, 0xFFF5FAFFu, progress * 0.16f);

        float medallion_offset_y = render_lerp_f32(14.0f, 0.0f, ease);
        float medallion_radius = LIBRARY_QUICK_ACTION_BTN_RADIUS_PX *
                                 (controller->quick_action_pressed ? 0.93f : 1.0f);
        render_draw_filled_circle_aa(
            surface, (float)SCREEN_CENTER_X, LIBRARY_QUICK_ACTION_BTN_Y + medallion_offset_y,
            medallion_radius, is_favorite ? palette->signal_highlight : 0xFF161A22u,
            progress * 0.92f);
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X,
                           LIBRARY_QUICK_ACTION_BTN_Y + medallion_offset_y, medallion_radius,
                           (is_favorite || controller->quick_action_pressed) ? 2.0f : 1.5f, 0.0f,
                           FULL_CIRCLE_RADIANS,
                           (is_favorite || controller->quick_action_pressed)
                               ? palette->signal_highlight
                               : 0xFFF5FAFFu,
                           progress * ((is_favorite || controller->quick_action_pressed) ? 1.0f
                                                                                         : 0.34f));
        if (is_favorite) {
            render_draw_heart_filled_aa(surface, (float)SCREEN_CENTER_X,
                                        LIBRARY_QUICK_ACTION_BTN_Y + medallion_offset_y, 14.0f,
                                        palette->signal_highlight_ink, progress);
        } else {
            render_draw_heart_stroked_aa(surface, (float)SCREEN_CENTER_X,
                                         LIBRARY_QUICK_ACTION_BTN_Y + medallion_offset_y, 14.0f,
                                         1.8f, 0xFFF5F6F8u, progress);
        }
        render_draw_text_alpha(surface, FONT_STYLE_MONO_9,
                               is_favorite ? action_label_active : action_label_idle,
                               (float)SCREEN_CENTER_X, 246.0f + medallion_offset_y,
                               TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               is_favorite ? palette->signal_highlight : 0xFFF5F6F8u, progress);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "TAP OUTSIDE TO CLOSE",
                               (float)SCREEN_CENTER_X, 316.0f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, 0xFFF5F6F8u, progress * 0.48f);
    }

    /* 5. Playlist Action Sheet + 360-deg Delete Countdown */
    if (controller->playlist_sheet_progress > 0.01f && controller->catalog != NULL) {
        float p = controller->playlist_sheet_progress;
        float ease = render_ease_out_expo(p);
        render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, (float)SCREEN_WIDTH,
                                      (float)SCREEN_HEIGHT, palette->canvas, p * 0.80f);
        float card_y = render_lerp_f32(112.0f, 86.0f, ease);
        render_draw_filled_rect_alpha(surface, 68.0f, card_y, 224.0f, 194.0f, palette->surface,
                                      p * 0.96f);
        render_draw_stroked_rect_alpha(surface, 68.0f, card_y, 224.0f, 194.0f, 1.25f,
                                       palette->hairline_strong, p);

        const char *pl_name =
            (controller->playlist_sheet_index < controller->catalog->playlist_count)
                ? library_catalog_get_string(
                      controller->catalog,
                      controller->catalog->playlists[controller->playlist_sheet_index].name_offset)
                : "";
        render_draw_text_alpha(surface, FONT_STYLE_MONO_9, "PLAYLIST ACTIONS",
                               (float)SCREEN_CENTER_X, card_y + 18.0f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, palette->signal_highlight, p);
        render_draw_marquee_text(surface, FONT_STYLE_DISPLAY_15, pl_name, (float)SCREEN_CENTER_X,
                                 card_y + 40.0f, 196.0f, controller->elapsed_seconds,
                                 palette->ink_primary, p);

        render_draw_filled_rect_alpha(surface, 82.0f, card_y + 62.0f, 196.0f, 34.0f,
                                      palette->signal_highlight, p);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "PLAY MIX", (float)SCREEN_CENTER_X,
                               card_y + 79.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               palette->signal_highlight_ink, p);

        render_draw_filled_rect_alpha(surface, 82.0f, card_y + 104.0f, 196.0f, 34.0f,
                                      palette->surface, p);
        render_draw_stroked_rect_alpha(surface, 82.0f, card_y + 104.0f, 196.0f, 34.0f, 1.1f,
                                       palette->hairline_strong, p);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "SHUFFLE MIX", (float)SCREEN_CENTER_X,
                               card_y + 121.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               palette->ink_primary, p);

        float hold = controller->delete_hold_progress;
        render_draw_filled_rect_alpha(surface, 82.0f, card_y + 146.0f, 196.0f, 34.0f,
                                      palette->signal_alert, p * (is_dark ? 0.22f : 0.16f));
        if (hold > 0.01f) {
            render_draw_filled_rect_alpha(surface, 82.0f, card_y + 146.0f, 196.0f * hold, 34.0f,
                                          palette->signal_alert, p);
            render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 168.0f,
                               4.0f, TOP_ANGLE_RADIANS,
                               TOP_ANGLE_RADIANS + hold * FULL_CIRCLE_RADIANS,
                               palette->signal_alert, p);
        }
        render_draw_stroked_rect_alpha(surface, 82.0f, card_y + 146.0f, 196.0f, 34.0f, 1.3f,
                                       palette->signal_alert, p);
        char del_label[32];
        if (hold > 0.04f) {
            snprintf(del_label, sizeof(del_label), "HOLDING \xC2\xB7 %u%%",
                     (unsigned)lroundf(hold * 100.0f));
        } else {
            snprintf(del_label, sizeof(del_label), "HOLD TO DELETE");
        }
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, del_label, (float)SCREEN_CENTER_X,
                               card_y + 163.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               hold > 0.48f ? 0xFFFFFFFFu : palette->signal_alert, p);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "SWIPE LEFT EDGE TO CANCEL",
                               (float)SCREEN_CENTER_X, card_y + 214.0f, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, palette->ink_secondary, p);
    }

    if (controller->delete_completed_flash > 0.01f) {
        render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, (float)SCREEN_WIDTH,
                                      (float)SCREEN_HEIGHT, palette->signal_alert,
                                      controller->delete_completed_flash * 0.28f);
    }
}

void render_library_hub_transition_layer(const struct framebuffer_surface *surface,
                                         const struct library_controller *controller,
                                         enum appearance_mode appearance_mode,
                                         float reveal_progress) {
    if (surface == NULL || surface->pixels == NULL || reveal_progress <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    const struct appearance_palette *palette = theme_get_palette(appearance_mode);
    bool is_dark = (appearance_mode == APPEARANCE_MODE_DARK);

    float floor_alpha = render_ease_out_cubic(render_normalize_range(reveal_progress, 0.24f, 0.68f));
    float header_ease = render_ease_out_expo(render_normalize_range(reveal_progress, 0.42f, 0.96f));
    float header_drift = render_lerp_f32(-18.0f, 0.0f, header_ease);
    float tile_alpha = render_ease_out_cubic(render_normalize_range(reveal_progress, 0.30f, 0.88f));
    float tile_scale = render_lerp_f32(0.80f, 1.0f, tile_alpha);

    draw_peripheral_rim_sheen(surface, palette, is_dark, floor_alpha);
    float scroll_pos = (controller != NULL && controller->stack_depth > 0u)
                           ? controller->view_stack[0].scroll_position
                           : 0.0f;
    draw_library_perspective_floor(surface, palette, is_dark, scroll_pos, floor_alpha);

    char sub[64];
    uint32_t track_count = (controller != NULL && controller->catalog != NULL)
                               ? controller->catalog->track_count
                               : 0u;
    if (track_count > 0u) {
        char num[16];
        format_comma_u32(track_count, num, sizeof(num));
        snprintf(sub, sizeof(sub), "2-UP CRYSTAL HUB \xC2\xB7 %s TRACKS", num);
    } else {
        snprintf(sub, sizeof(sub), "0 TRACKS \xC2\xB7 EMPTY COLLECTION");
    }
    draw_standard_header(surface, "Library", sub, header_drift, palette, header_ease);

    if (controller != NULL) {
        draw_hub_tiles(surface, controller, scroll_pos, controller->elapsed_seconds, tile_alpha,
                       tile_scale, false, 0.0f, palette, is_dark);
    }
}

void render_draw_library_frame(const struct framebuffer_surface *surface,
                               const struct library_controller *controller,
                               enum appearance_mode appearance_mode, float back_swipe_progress) {
    if (surface == NULL || surface->pixels == NULL || controller == NULL) {
        return;
    }
    const struct appearance_palette *palette = theme_get_palette(appearance_mode);
    bool is_dark = (appearance_mode == APPEARANCE_MODE_DARK);
    render_fill_canvas(surface, palette->canvas);

    const struct library_view_frame *view = library_controller_current_view(controller);
    if (view == NULL) {
        return;
    }

    float floor_scroll = view->scroll_position;
    if (controller->bloom_direction != LIB_BLOOM_NONE && controller->bloom_progress < 1.0f) {
        float f = (controller->bloom_direction == LIB_BLOOM_HUB_TO_ALBUMS)
                      ? controller->bloom_progress
                      : (1.0f - controller->bloom_progress);
        floor_scroll = render_lerp_f32(controller->view_stack[0].scroll_position,
                                       view->scroll_position, render_ease_in_out_cubic(f));
    }

    draw_peripheral_rim_sheen(surface, palette, is_dark, 1.0f);
    draw_library_perspective_floor(surface, palette, is_dark, floor_scroll, 1.0f);

    /* Tracklist Crystal Overlay draws focused album sleeve behind translucent glass sheet */
    if (view->kind == LIB_VIEW_TRACKS && view->source_type == LIB_TRACKS_ALBUM) {
        draw_crate_album_sleeve_and_disc(surface, (struct library_controller *)controller,
                                         view->context_index, 172.0f, 152.0f, 120.0f, 0.0f, 1.0f,
                                         0.95f, true, palette, is_dark);
        float sheet_p = render_ease_out_expo(controller->transition_progress);
        float sheet_top_y = render_lerp_f32(316.0f, 74.0f, sheet_p);
        render_draw_filled_rect_alpha(surface, 0.0f, sheet_top_y, (float)SCREEN_WIDTH,
                                      (float)SCREEN_HEIGHT - sheet_top_y,
                                      is_dark ? 0xFF0A0D13u : 0xFFEFF3F8u,
                                      is_dark ? 0.80f : 0.84f);
        struct point_2d diag[4] = {{42.0f, sheet_top_y},
                                   {136.0f, sheet_top_y},
                                   {54.0f, 360.0f},
                                   {-40.0f, 360.0f}};
        render_draw_filled_convex_polygon_fast(surface, diag, 4u,
                                               is_dark ? 0xFFE1EEFFu : 0xFFFFFFFFu,
                                               is_dark ? 0.035f : 0.24f);
        render_draw_line_aa(surface, 48.0f, sheet_top_y, 312.0f, sheet_top_y, 1.25f,
                            palette->hairline_strong, 0.9f);
        render_draw_filled_rect_alpha(surface, 166.0f, sheet_top_y + 6.0f, 28.0f, 3.0f,
                                      palette->hairline_strong, 0.65f);
        float slide_y = (1.0f - sheet_p) * 42.0f;
        draw_standard_header(surface, view->title, view->subtitle, 0.0f, palette,
                             render_ease_out_cubic(controller->transition_progress));
        draw_curved_list_body(surface, controller, view, 0.0f, slide_y, 1.0f, palette, is_dark);
    } else if (controller->bloom_direction != LIB_BLOOM_NONE && controller->bloom_progress < 1.0f) {
        float f = (controller->bloom_direction == LIB_BLOOM_HUB_TO_ALBUMS)
                      ? controller->bloom_progress
                      : (1.0f - controller->bloom_progress);
        float hub_hdr_alpha = 1.0f - render_ease_out_cubic(render_normalize_range(f, 0.0f, 0.42f));
        float crate_hdr_alpha = render_ease_out_cubic(render_normalize_range(f, 0.44f, 0.92f));
        float part_factor = render_ease_in_out_cubic(render_normalize_range(f, 0.0f, 0.44f));

        char sub[64];
        uint32_t track_count = controller->catalog != NULL ? controller->catalog->track_count : 0u;
        snprintf(sub, sizeof(sub), "2-UP CRYSTAL HUB \xC2\xB7 %u TRACKS", (unsigned)track_count);
        draw_standard_header(surface, "Library", sub, -10.0f * f, palette, hub_hdr_alpha);
        draw_standard_header(surface, view->title, view->subtitle,
                             (1.0f - render_ease_out_expo(render_normalize_range(f, 0.44f, 0.92f))) *
                                 10.0f,
                             palette, crate_hdr_alpha);
        draw_hub_tiles(surface, controller, controller->view_stack[0].scroll_position,
                       controller->elapsed_seconds, 1.0f, 1.0f, true, part_factor, palette,
                       is_dark);
        if (view->kind == LIB_VIEW_ALBUMS) {
            draw_albums_crate_body(surface, (struct library_controller *)controller, view, 1.0f, f,
                                   palette, is_dark);
        }
    } else {
        float trans_ease = render_ease_out_expo(controller->transition_progress);
        float slide_x = (1.0f - trans_ease) * 28.0f * (float)controller->transition_direction;
        float body_alpha = render_ease_out_cubic(controller->transition_progress);

        if (view->is_artist_detail || view->is_favorites_detail) {
            float blend = view->is_artist_detail ? controller->artist_mode_blend
                                                 : controller->favorites_mode_blend;
            float tab_fade = view->is_artist_detail ? controller->artist_tab_fade_progress
                                                    : controller->favorites_tab_fade_progress;
            draw_segmented_header(surface, view->title, blend, palette, is_dark, 1.0f);
            body_alpha *= render_ease_out_cubic(tab_fade);
        } else if (view->kind == LIB_VIEW_HUB) {
            char sub[64];
            uint32_t track_count = controller->catalog != NULL ? controller->catalog->track_count : 0u;
            if (track_count > 0u) {
                char num[16];
                format_comma_u32(track_count, num, sizeof(num));
                snprintf(sub, sizeof(sub), "2-UP CRYSTAL HUB \xC2\xB7 %s TRACKS", num);
            } else {
                snprintf(sub, sizeof(sub), "0 TRACKS \xC2\xB7 EMPTY COLLECTION");
            }
            draw_standard_header(surface, "Library", sub, 0.0f, palette, 1.0f);
        } else {
            draw_standard_header(surface, view->title, view->subtitle, 0.0f, palette, 1.0f);
        }

        if (view->kind == LIB_VIEW_HUB) {
            draw_hub_tiles(surface, controller, view->scroll_position, controller->elapsed_seconds,
                           body_alpha, 1.0f, false, 0.0f, palette, is_dark);
            if (controller->arc_feedback_alpha <= 0.05f && controller->seek_hud_alpha <= 0.05f) {
                draw_right_scroll_arc(surface, view->scroll_position, 2u, 4u, palette, body_alpha);
            }
        } else if (view->kind == LIB_VIEW_EMPTY) {
            draw_empty_state(surface, view->empty_icon, view->empty_title, view->empty_message,
                             128.0f, true, controller->elapsed_seconds, palette, is_dark,
                             body_alpha);
        } else if (view->kind == LIB_VIEW_ALBUMS) {
            draw_albums_crate_body(surface, (struct library_controller *)controller, view,
                                   body_alpha, 1.0f, palette, is_dark);
        } else {
            if (view->is_favorites_detail && view->item_count == 0u) {
                draw_empty_state(surface, LIB_HUB_FAVORITES, "No Favorite Songs",
                                 "HOLD A SONG ROW TO FAVORITE", 152.0f, false,
                                 controller->elapsed_seconds, palette, is_dark, body_alpha);
            } else {
                draw_curved_list_body(surface, controller, view, slide_x, 0.0f, body_alpha, palette,
                                      is_dark);
            }
            if (controller->arc_feedback_alpha <= 0.05f && controller->seek_hud_alpha <= 0.05f) {
                draw_right_scroll_arc(surface, view->scroll_position,
                                      library_controller_current_max_scroll(controller),
                                      view->item_count, palette, body_alpha);
            }
            if (view->kind == LIB_VIEW_ARTISTS) {
                enum library_sort_mode art_sort = controller->state_store != NULL
                                                      ? controller->state_store->artist_sort_mode
                                                      : LIBRARY_SORT_ALPHABETICAL;
                float bob = sinf(controller->elapsed_seconds * 2.4f + 1.2f) * 1.1f;
                draw_floating_circle_button(surface, LIBRARY_ARTISTS_SORT_BTN_X,
                                            LIBRARY_ARTISTS_SORT_BTN_Y + bob,
                                            art_sort != LIBRARY_SORT_ALPHABETICAL, palette, is_dark,
                                            body_alpha);
                draw_sort_button_glyph(surface, LIBRARY_ARTISTS_SORT_BTN_X,
                                       LIBRARY_ARTISTS_SORT_BTN_Y + bob, art_sort,
                                       art_sort != LIBRARY_SORT_ALPHABETICAL, palette, body_alpha);
            }
        }
    }

    draw_overlays(surface, controller, palette, is_dark);

    float swipe_p = fmaxf(controller->back_swipe_progress, back_swipe_progress);
    if (swipe_p > 0.02f) {
        float half_span = 0.34f * swipe_p;
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, 168.0f, 4.0f,
                           HALF_CIRCLE_RADIANS - half_span, HALF_CIRCLE_RADIANS + half_span,
                           palette->signal_highlight, swipe_p * 0.9f);
    }
}
