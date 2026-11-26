#include "render_primitives.h"

#include "constants.h"

#include <math.h>
#include <stddef.h>

#define MAX_CONVEX_POLYGON_VERTICES 8u

float render_clamp_f32(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

float render_normalize_range(float value, float range_start, float range_end) {
    if (range_end <= range_start) {
        return value >= range_end ? 1.0f : 0.0f;
    }
    return render_clamp_f32((value - range_start) / (range_end - range_start), 0.0f, 1.0f);
}

float render_lerp_f32(float start_value, float end_value, float factor) {
    return start_value + (end_value - start_value) * factor;
}

float render_ease_out_cubic(float progress) {
    float inverted = 1.0f - render_clamp_f32(progress, 0.0f, 1.0f);
    return 1.0f - inverted * inverted * inverted;
}

float render_ease_out_expo(float progress) {
    float clamped = render_clamp_f32(progress, 0.0f, 1.0f);
    return clamped >= 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * clamped);
}

float render_ease_in_out_cubic(float progress) {
    float clamped = render_clamp_f32(progress, 0.0f, 1.0f);
    if (clamped < 0.5f) {
        return 4.0f * clamped * clamped * clamped;
    }
    float tail = -2.0f * clamped + 2.0f;
    return 1.0f - (tail * tail * tail) * 0.5f;
}

struct point_3d render_rotate_point_3d(float point_x, float point_y, float point_z, float angle_x,
                                      float angle_y, float angle_z) {
    float cos_y = cosf(angle_y);
    float sin_y = sinf(angle_y);
    float rotated_x1 = point_x * cos_y + point_z * sin_y;
    float rotated_z1 = -point_x * sin_y + point_z * cos_y;

    float cos_x = cosf(angle_x);
    float sin_x = sinf(angle_x);
    float rotated_y2 = point_y * cos_x - rotated_z1 * sin_x;
    float rotated_z2 = point_y * sin_x + rotated_z1 * cos_x;

    float cos_z = cosf(angle_z);
    float sin_z = sinf(angle_z);
    struct point_3d result = {
        .x = rotated_x1 * cos_z - rotated_y2 * sin_z,
        .y = rotated_x1 * sin_z + rotated_y2 * cos_z,
        .z = rotated_z2,
    };
    return result;
}

static uint8_t alpha_to_u8(float alpha) {
    float clamped = render_clamp_f32(alpha, 0.0f, 1.0f);
    return (uint8_t)lroundf(clamped * 255.0f);
}

static int32_t clamp_pixel_x(float coord) {
    int32_t rounded = (int32_t)floorf(coord);
    if (rounded < 0) {
        return 0;
    }
    if (rounded >= (int32_t)SCREEN_WIDTH) {
        return (int32_t)SCREEN_WIDTH - 1;
    }
    return rounded;
}

static int32_t clamp_pixel_y(float coord) {
    int32_t rounded = (int32_t)floorf(coord);
    if (rounded < 0) {
        return 0;
    }
    if (rounded >= (int32_t)SCREEN_HEIGHT) {
        return (int32_t)SCREEN_HEIGHT - 1;
    }
    return rounded;
}

void render_fill_canvas(const struct framebuffer_surface *surface, uint32_t canvas_color) {
    if (surface == NULL || surface->pixels == NULL) {
        return;
    }
    for (uint32_t logical_y = 0u; logical_y < SCREEN_HEIGHT; logical_y++) {
        uint32_t physical_y = render_map_coordinate_180(logical_y);
        uint32_t *row_pixels = surface->pixels + physical_y * surface->stride_pixels;
        for (uint32_t logical_x = 0u; logical_x < SCREEN_WIDTH; logical_x++) {
            uint32_t physical_x = render_map_coordinate_180(logical_x);
            bool inside = render_is_inside_bezel_circle((int32_t)logical_x, (int32_t)logical_y);
            row_pixels[physical_x] = inside ? canvas_color : COLOR_OUTSIDE_BEZEL;
        }
    }
}

void render_blend_pixel(const struct framebuffer_surface *surface, int32_t logical_x,
                        int32_t logical_y, uint32_t color, uint8_t alpha) {
    if (alpha == 0u || logical_x < 0 || logical_x >= (int32_t)SCREEN_WIDTH || logical_y < 0 ||
        logical_y >= (int32_t)SCREEN_HEIGHT) {
        return;
    }
    if (!render_is_inside_bezel_circle(logical_x, logical_y)) {
        return;
    }

    uint32_t physical_x = render_map_coordinate_180((uint32_t)logical_x);
    uint32_t physical_y = render_map_coordinate_180((uint32_t)logical_y);
    uint32_t *target = &surface->pixels[physical_y * surface->stride_pixels + physical_x];

    if (alpha == 255u) {
        *target = 0xFF000000u | (color & 0x00FFFFFFu);
        return;
    }

    uint32_t destination = *target;
    uint32_t inv_alpha = 255u - (uint32_t)alpha;
    uint32_t src_r = (color >> 16u) & 0xFFu;
    uint32_t src_g = (color >> 8u) & 0xFFu;
    uint32_t src_b = color & 0xFFu;

    uint32_t dst_r = (destination >> 16u) & 0xFFu;
    uint32_t dst_g = (destination >> 8u) & 0xFFu;
    uint32_t dst_b = destination & 0xFFu;

    uint32_t out_r = (src_r * (uint32_t)alpha + dst_r * inv_alpha + 127u) / 255u;
    uint32_t out_g = (src_g * (uint32_t)alpha + dst_g * inv_alpha + 127u) / 255u;
    uint32_t out_b = (src_b * (uint32_t)alpha + dst_b * inv_alpha + 127u) / 255u;

    *target = 0xFF000000u | (out_r << 16u) | (out_g << 8u) | out_b;
}

void render_draw_line_aa(const struct framebuffer_surface *surface, float start_x, float start_y,
                         float end_x, float end_y, float line_width, uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD ||
        line_width <= 0.0f) {
        return;
    }

    float half_width = line_width * 0.5f;
    float margin = half_width + 1.5f;
    float min_fx = start_x < end_x ? start_x : end_x;
    float max_fx = start_x > end_x ? start_x : end_x;
    float min_fy = start_y < end_y ? start_y : end_y;
    float max_fy = start_y > end_y ? start_y : end_y;

    int32_t min_x = clamp_pixel_x(min_fx - margin);
    int32_t max_x = clamp_pixel_x(max_fx + margin);
    int32_t min_y = clamp_pixel_y(min_fy - margin);
    int32_t max_y = clamp_pixel_y(max_fy + margin);

    float delta_x = end_x - start_x;
    float delta_y = end_y - start_y;
    float length_sq = delta_x * delta_x + delta_y * delta_y;

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float proj_t = 0.0f;
            if (length_sq > 1e-6f) {
                proj_t = ((float)pixel_x - start_x) * delta_x + ((float)pixel_y - start_y) * delta_y;
                proj_t = render_clamp_f32(proj_t / length_sq, 0.0f, 1.0f);
            }
            float closest_x = start_x + proj_t * delta_x;
            float closest_y = start_y + proj_t * delta_y;
            float dist_x = (float)pixel_x - closest_x;
            float dist_y = (float)pixel_y - closest_y;
            float distance = sqrtf(dist_x * dist_x + dist_y * dist_y);
            float coverage = render_clamp_f32(half_width + 0.5f - distance, 0.0f, 1.0f);
            if (coverage > 0.0f) {
                render_blend_pixel(surface, pixel_x, pixel_y, color, alpha_to_u8(alpha * coverage));
            }
        }
    }
}

void render_draw_arc_aa(const struct framebuffer_surface *surface, float center_x, float center_y,
                        float radius, float line_width, float start_angle, float end_angle,
                        uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD ||
        line_width <= 0.0f || radius <= 0.0f) {
        return;
    }

    float sweep = end_angle - start_angle;
    if (sweep <= 0.0f) {
        return;
    }
    bool is_full_circle = sweep >= (FULL_CIRCLE_RADIANS - 1e-4f);

    float half_width = line_width * 0.5f;
    float outer_margin = radius + half_width + 1.5f;
    int32_t min_x = clamp_pixel_x(center_x - outer_margin);
    int32_t max_x = clamp_pixel_x(center_x + outer_margin);
    int32_t min_y = clamp_pixel_y(center_y - outer_margin);
    int32_t max_y = clamp_pixel_y(center_y + outer_margin);

    float inner_cutoff = radius - half_width - 1.5f;
    float inner_cutoff_sq = inner_cutoff > 0.0f ? inner_cutoff * inner_cutoff : 0.0f;
    float outer_cutoff_sq = outer_margin * outer_margin;

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float offset_y = (float)pixel_y - center_y;
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float offset_x = (float)pixel_x - center_x;
            float dist_sq = offset_x * offset_x + offset_y * offset_y;
            if (dist_sq < inner_cutoff_sq || dist_sq > outer_cutoff_sq) {
                continue;
            }

            float dist = sqrtf(dist_sq);
            float radial_cov = render_clamp_f32(half_width + 0.5f - fabsf(dist - radius), 0.0f, 1.0f);
            if (radial_cov <= 0.0f) {
                continue;
            }

            float coverage = radial_cov;
            if (!is_full_circle) {
                float angle = atan2f(offset_y, offset_x);
                float relative = fmodf(angle - start_angle, FULL_CIRCLE_RADIANS);
                if (relative < 0.0f) {
                    relative += FULL_CIRCLE_RADIANS;
                }
                float angular_dist;
                if (relative <= sweep) {
                    float dist_start = relative * radius;
                    float dist_end = (sweep - relative) * radius;
                    angular_dist = dist_start < dist_end ? dist_start : dist_end;
                } else {
                    float dist_past_end = (relative - sweep) * radius;
                    float dist_before_start = (FULL_CIRCLE_RADIANS - relative) * radius;
                    angular_dist = -(dist_past_end < dist_before_start ? dist_past_end
                                                                       : dist_before_start);
                }
                float angular_cov = render_clamp_f32(angular_dist + 0.5f, 0.0f, 1.0f);
                coverage *= angular_cov;
            }

            if (coverage > 0.0f) {
                render_blend_pixel(surface, pixel_x, pixel_y, color, alpha_to_u8(alpha * coverage));
            }
        }
    }
}

void render_draw_filled_circle_aa(const struct framebuffer_surface *surface, float center_x,
                                  float center_y, float radius, uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD ||
        radius <= 0.0f) {
        return;
    }

    float margin = radius + 1.5f;
    int32_t min_x = clamp_pixel_x(center_x - margin);
    int32_t max_x = clamp_pixel_x(center_x + margin);
    int32_t min_y = clamp_pixel_y(center_y - margin);
    int32_t max_y = clamp_pixel_y(center_y + margin);

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float offset_y = (float)pixel_y - center_y;
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float offset_x = (float)pixel_x - center_x;
            float dist = sqrtf(offset_x * offset_x + offset_y * offset_y);
            float coverage = render_clamp_f32(radius + 0.5f - dist, 0.0f, 1.0f);
            if (coverage > 0.0f) {
                render_blend_pixel(surface, pixel_x, pixel_y, color, alpha_to_u8(alpha * coverage));
            }
        }
    }
}

void render_draw_filled_rect_alpha(const struct framebuffer_surface *surface, float left_x,
                                   float top_y, float width, float height, uint32_t color,
                                   float alpha) {
    if (surface == NULL || surface->pixels == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD ||
        width <= 0.0f || height <= 0.0f) {
        return;
    }

    float right_x = left_x + width;
    float bottom_y = top_y + height;
    int32_t min_x = clamp_pixel_x(left_x - 1.0f);
    int32_t max_x = clamp_pixel_x(right_x + 1.0f);
    int32_t min_y = clamp_pixel_y(top_y - 1.0f);
    int32_t max_y = clamp_pixel_y(bottom_y + 1.0f);

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float cov_y = render_clamp_f32((float)pixel_y - top_y + 0.5f, 0.0f, 1.0f) *
                      render_clamp_f32(bottom_y - (float)pixel_y + 0.5f, 0.0f, 1.0f);
        if (cov_y <= 0.0f) {
            continue;
        }
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float cov_x = render_clamp_f32((float)pixel_x - left_x + 0.5f, 0.0f, 1.0f) *
                          render_clamp_f32(right_x - (float)pixel_x + 0.5f, 0.0f, 1.0f);
            float coverage = cov_x * cov_y;
            if (coverage > 0.0f) {
                render_blend_pixel(surface, pixel_x, pixel_y, color, alpha_to_u8(alpha * coverage));
            }
        }
    }
}

void render_draw_stroked_rect_alpha(const struct framebuffer_surface *surface, float left_x,
                                    float top_y, float width, float height, float line_width,
                                    uint32_t color, float alpha) {
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    float right_x = left_x + width;
    float bottom_y = top_y + height;
    render_draw_line_aa(surface, left_x, top_y, right_x, top_y, line_width, color, alpha);
    render_draw_line_aa(surface, right_x, top_y, right_x, bottom_y, line_width, color, alpha);
    render_draw_line_aa(surface, right_x, bottom_y, left_x, bottom_y, line_width, color, alpha);
    render_draw_line_aa(surface, left_x, bottom_y, left_x, top_y, line_width, color, alpha);
}

void render_draw_filled_convex_polygon_aa(const struct framebuffer_surface *surface,
                                          const struct point_2d *vertices, uint32_t vertex_count,
                                          uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || vertices == NULL || vertex_count < 3u ||
        vertex_count > MAX_CONVEX_POLYGON_VERTICES || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float signed_area = 0.0f;
    float min_fx = vertices[0].x;
    float max_fx = vertices[0].x;
    float min_fy = vertices[0].y;
    float max_fy = vertices[0].y;

    for (uint32_t index = 0u; index < vertex_count; index++) {
        uint32_t next_index = (index + 1u) % vertex_count;
        signed_area += vertices[index].x * vertices[next_index].y -
                       vertices[next_index].x * vertices[index].y;
        if (vertices[index].x < min_fx) {
            min_fx = vertices[index].x;
        }
        if (vertices[index].x > max_fx) {
            max_fx = vertices[index].x;
        }
        if (vertices[index].y < min_fy) {
            min_fy = vertices[index].y;
        }
        if (vertices[index].y > max_fy) {
            max_fy = vertices[index].y;
        }
    }

    if (fabsf(signed_area) < 1e-4f) {
        return;
    }

    /* Ensure outward normal points away from polygon interior regardless of winding */
    float winding_sign = signed_area > 0.0f ? 1.0f : -1.0f;
    float normal_x[MAX_CONVEX_POLYGON_VERTICES];
    float normal_y[MAX_CONVEX_POLYGON_VERTICES];

    for (uint32_t index = 0u; index < vertex_count; index++) {
        uint32_t next_index = (index + 1u) % vertex_count;
        float edge_x = vertices[next_index].x - vertices[index].x;
        float edge_y = vertices[next_index].y - vertices[index].y;
        float edge_len = sqrtf(edge_x * edge_x + edge_y * edge_y);
        if (edge_len < 1e-6f) {
            normal_x[index] = 0.0f;
            normal_y[index] = 0.0f;
        } else {
            normal_x[index] = (edge_y / edge_len) * winding_sign;
            normal_y[index] = (-edge_x / edge_len) * winding_sign;
        }
    }

    int32_t min_x = clamp_pixel_x(min_fx - 1.5f);
    int32_t max_x = clamp_pixel_x(max_fx + 1.5f);
    int32_t min_y = clamp_pixel_y(min_fy - 1.5f);
    int32_t max_y = clamp_pixel_y(max_fy + 1.5f);

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float max_signed_dist = -1e6f;
            for (uint32_t index = 0u; index < vertex_count; index++) {
                float dist = ((float)pixel_x - vertices[index].x) * normal_x[index] +
                             ((float)pixel_y - vertices[index].y) * normal_y[index];
                if (dist > max_signed_dist) {
                    max_signed_dist = dist;
                }
            }
            float coverage = render_clamp_f32(0.5f - max_signed_dist, 0.0f, 1.0f);
            if (coverage > 0.0f) {
                render_blend_pixel(surface, pixel_x, pixel_y, color, alpha_to_u8(alpha * coverage));
            }
        }
    }
}

static void build_chamfer_vertices(float left_x, float top_y, float width, float height,
                                   float chamfer, struct point_2d *vertices_out) {
    float cut = chamfer;
    if (cut > width * 0.4f) {
        cut = width * 0.4f;
    }
    if (cut > height * 0.4f) {
        cut = height * 0.4f;
    }
    float right_x = left_x + width;
    float bottom_y = top_y + height;

    vertices_out[0] = (struct point_2d){left_x + cut, top_y};
    vertices_out[1] = (struct point_2d){right_x - cut, top_y};
    vertices_out[2] = (struct point_2d){right_x, top_y + cut};
    vertices_out[3] = (struct point_2d){right_x, bottom_y - cut};
    vertices_out[4] = (struct point_2d){right_x - cut, bottom_y};
    vertices_out[5] = (struct point_2d){left_x + cut, bottom_y};
    vertices_out[6] = (struct point_2d){left_x, bottom_y - cut};
    vertices_out[7] = (struct point_2d){left_x, top_y + cut};
}

void render_draw_chamfer_plate_filled_aa(const struct framebuffer_surface *surface, float left_x,
                                         float top_y, float width, float height, float chamfer,
                                         uint32_t color, float alpha) {
    struct point_2d vertices[8];
    build_chamfer_vertices(left_x, top_y, width, height, chamfer, vertices);
    render_draw_filled_convex_polygon_aa(surface, vertices, 8u, color, alpha);
}

void render_draw_chamfer_plate_stroked_aa(const struct framebuffer_surface *surface, float left_x,
                                          float top_y, float width, float height, float chamfer,
                                          float line_width, uint32_t color, float alpha) {
    struct point_2d vertices[8];
    build_chamfer_vertices(left_x, top_y, width, height, chamfer, vertices);
    for (uint32_t index = 0u; index < 8u; index++) {
        uint32_t next_index = (index + 1u) % 8u;
        render_draw_line_aa(surface, vertices[index].x, vertices[index].y, vertices[next_index].x,
                            vertices[next_index].y, line_width, color, alpha);
    }
}

void render_draw_text_alpha(const struct framebuffer_surface *surface, enum font_style_id style_id,
                            const char *text, float anchor_x, float anchor_y,
                            enum text_align_mode align, enum text_baseline_mode baseline,
                            uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || text == NULL ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    const struct bitmap_font *font = font_atlas_get(style_id);
    if (font == NULL) {
        return;
    }

    int32_t total_width = font_atlas_measure_width(font, text);
    float cursor_x = anchor_x;
    if (align == TEXT_ALIGN_CENTER) {
        cursor_x = anchor_x - (float)total_width * 0.5f;
    } else if (align == TEXT_ALIGN_RIGHT) {
        cursor_x = anchor_x - (float)total_width;
    }

    int32_t pen_x = (int32_t)lroundf(cursor_x);
    int32_t baseline_y = (baseline == TEXT_BASELINE_MIDDLE)
                             ? (int32_t)lroundf(anchor_y + (float)font->cap_height_px * 0.5f)
                             : (int32_t)lroundf(anchor_y + (float)font->ascent_px);

    size_t byte_offset = 0u;
    while (text[byte_offset] != '\0') {
        uint32_t codepoint = font_atlas_decode_utf8_next(text, &byte_offset);
        const struct bitmap_glyph *glyph = font_atlas_lookup_glyph(font, codepoint);
        if (glyph == NULL) {
            continue;
        }

        int32_t draw_x = pen_x + (int32_t)glyph->offset_x;
        int32_t draw_y = baseline_y + (int32_t)glyph->offset_y;

        for (uint16_t row_index = 0u; row_index < glyph->height; row_index++) {
            for (uint16_t col_index = 0u; col_index < glyph->width; col_index++) {
                uint8_t coverage_u8 =
                    font->alpha_data[glyph->bitmap_offset + (uint32_t)row_index * glyph->width +
                                     col_index];
                if (coverage_u8 == 0u) {
                    continue;
                }
                uint8_t blended_alpha = (uint8_t)lroundf(((float)coverage_u8 / 255.0f) * alpha * 255.0f);
                render_blend_pixel(surface, draw_x + (int32_t)col_index,
                                   draw_y + (int32_t)row_index, color, blended_alpha);
            }
        }
        pen_x += (int32_t)glyph->advance_x;
    }
}

void render_draw_crosshair_mark(const struct framebuffer_surface *surface, float center_x,
                                float center_y, float half_length, float line_width,
                                uint32_t color, float alpha) {
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    render_draw_line_aa(surface, center_x - half_length, center_y, center_x + half_length, center_y,
                        line_width, color, alpha);
    render_draw_line_aa(surface, center_x, center_y - half_length, center_x, center_y + half_length,
                        line_width, color, alpha);
}

void render_draw_diagonal_mark(const struct framebuffer_surface *surface, float center_x,
                               float center_y, float half_span, float line_width, uint32_t color,
                               float alpha) {
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    render_draw_line_aa(surface, center_x - half_span, center_y - half_span, center_x + half_span,
                        center_y + half_span, line_width, color, alpha);
    render_draw_line_aa(surface, center_x + half_span, center_y - half_span, center_x - half_span,
                        center_y + half_span, line_width, color, alpha);
}

void render_draw_perimeter_ticks(const struct framebuffer_surface *surface, uint32_t tick_count,
                                 float inner_radius, float outer_radius, float visible_ratio,
                                 const struct appearance_palette *palette, float base_alpha) {
    if (palette == NULL || tick_count == 0u || base_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    uint32_t visible_ticks =
        (uint32_t)floorf((float)tick_count * render_clamp_f32(visible_ratio, 0.0f, 1.0f));
    if (visible_ticks == 0u) {
        return;
    }

    for (uint32_t tick_index = 0u; tick_index < visible_ticks; tick_index++) {
        float angle_radians =
            TOP_ANGLE_RADIANS + ((float)tick_index / (float)tick_count) * FULL_CIRCLE_RADIANS;
        bool is_major_tick = (tick_index % 12u) == 0u;
        bool is_mid_tick = (tick_index % 6u) == 0u && !is_major_tick;
        float tick_start =
            is_major_tick ? (inner_radius - 4.0f) : (is_mid_tick ? (inner_radius - 1.5f) : (inner_radius + 1.0f));
        float cos_a = cosf(angle_radians);
        float sin_a = sinf(angle_radians);

        render_draw_line_aa(surface, (float)SCREEN_CENTER_X + cos_a * tick_start,
                            (float)SCREEN_CENTER_Y + sin_a * tick_start,
                            (float)SCREEN_CENTER_X + cos_a * outer_radius,
                            (float)SCREEN_CENTER_Y + sin_a * outer_radius,
                            is_major_tick ? 1.5f : 1.0f,
                            is_major_tick ? palette->ink_secondary : palette->hairline_strong,
                            base_alpha * (is_major_tick ? 0.9f : 0.45f));
    }
}

void render_draw_dialos_emblem(const struct framebuffer_surface *surface, float center_x,
                               float center_y, float radius,
                               const struct appearance_palette *palette, float alpha,
                               float detent_pop) {
    if (palette == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float ring_thickness = radius * 0.25f;
    if (ring_thickness < 5.0f) {
        ring_thickness = 5.0f;
    }
    float gap_start_angle = -HALF_CIRCLE_RADIANS * 0.36f;
    float gap_end_angle = -HALF_CIRCLE_RADIANS * 0.06f;

    render_draw_arc_aa(surface, center_x, center_y, radius, ring_thickness, gap_end_angle,
                       gap_start_angle + FULL_CIRCLE_RADIANS, palette->ink_primary, alpha);

    render_draw_arc_aa(surface, center_x, center_y, radius * 0.56f, 1.25f, 0.0f,
                       FULL_CIRCLE_RADIANS, palette->hairline_strong, alpha);

    render_draw_filled_circle_aa(surface, center_x, center_y, radius * 0.22f, palette->ink_primary,
                                 alpha);

    float detent_angle = (gap_start_angle + gap_end_angle) * 0.5f;
    float detent_scale = render_lerp_f32(0.4f, 1.0f, render_ease_out_expo(detent_pop));
    float detent_half = (ring_thickness * 0.96f * detent_scale) * 0.5f;
    float detent_cx = center_x + cosf(detent_angle) * radius;
    float detent_cy = center_y + sinf(detent_angle) * radius;
    float rotation = detent_angle + HALF_CIRCLE_RADIANS * 0.25f;
    float cos_r = cosf(rotation);
    float sin_r = sinf(rotation);

    static const float SQUARE_SIGNS[4][2] = {
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, 1.0f},
    };
    struct point_2d detent_vertices[4];
    for (uint32_t corner = 0u; corner < 4u; corner++) {
        float local_x = SQUARE_SIGNS[corner][0] * detent_half;
        float local_y = SQUARE_SIGNS[corner][1] * detent_half;
        detent_vertices[corner].x = detent_cx + local_x * cos_r - local_y * sin_r;
        detent_vertices[corner].y = detent_cy + local_x * sin_r + local_y * cos_r;
    }
    render_draw_filled_convex_polygon_aa(surface, detent_vertices, 4u, palette->signal_highlight,
                                         alpha);
}

void render_draw_hazard_stripe_band(const struct framebuffer_surface *surface, float origin_x,
                                    float origin_y, float band_width, float band_height,
                                    float phase_offset, uint32_t fill_color) {
    if (surface == NULL || surface->pixels == NULL || band_width <= 0.0f || band_height <= 0.0f) {
        return;
    }

    const float stripe_pitch = 16.0f;
    const float stripe_width = 7.0f;
    float shifted = fmodf(phase_offset, stripe_pitch);
    if (shifted < 0.0f) {
        shifted += stripe_pitch;
    }

    int32_t min_x = clamp_pixel_x(origin_x);
    int32_t max_x = clamp_pixel_x(origin_x + band_width - 1.0f);
    int32_t min_y = clamp_pixel_y(origin_y);
    int32_t max_y = clamp_pixel_y(origin_y + band_height - 1.0f);

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float rel_y = (origin_y + band_height) - (float)pixel_y;
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float projected = (float)pixel_x - origin_x - shifted - rel_y;
            float phase = fmodf(projected, stripe_pitch);
            if (phase < 0.0f) {
                phase += stripe_pitch;
            }
            if (phase < stripe_width) {
                render_blend_pixel(surface, pixel_x, pixel_y, fill_color, 255u);
            }
        }
    }
}
