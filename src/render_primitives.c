#include "render_primitives.h"

#include "constants.h"

#include <math.h>
#include <stddef.h>

#define MAX_CONVEX_POLYGON_VERTICES 8u

static int16_t bezel_half_width_table[SCREEN_HEIGHT];
static bool bezel_table_initialized = false;

static void ensure_bezel_table_initialized(void) {
    if (bezel_table_initialized) {
        return;
    }
    int32_t radius_sq = DISPLAY_BEZEL_RADIUS * DISPLAY_BEZEL_RADIUS;
    for (uint32_t logical_y = 0u; logical_y < SCREEN_HEIGHT; logical_y++) {
        int32_t delta_y = (int32_t)logical_y - SCREEN_CENTER_Y;
        int32_t rem = radius_sq - delta_y * delta_y;
        if (rem < 0) {
            bezel_half_width_table[logical_y] = -1;
        } else {
            int32_t half_w = (int32_t)sqrtf((float)rem);
            while ((half_w + 1) * (half_w + 1) <= rem) {
                half_w++;
            }
            while (half_w * half_w > rem) {
                half_w--;
            }
            bezel_half_width_table[logical_y] = (int16_t)half_w;
        }
    }
    bezel_table_initialized = true;
}

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

struct rotation_matrix_3x3 render_build_rotation_matrix_3d(float angle_x, float angle_y,
                                                           float angle_z) {
    float cos_y = cosf(angle_y);
    float sin_y = sinf(angle_y);
    float cos_x = cosf(angle_x);
    float sin_x = sinf(angle_x);
    float cos_z = cosf(angle_z);
    float sin_z = sinf(angle_z);

    /* Combined Rz * Rx * Ry matrix */
    struct rotation_matrix_3x3 matrix = {
        .m00 = cos_z * cos_y - sin_z * sin_x * sin_y,
        .m01 = -sin_z * cos_x,
        .m02 = cos_z * sin_y + sin_z * sin_x * cos_y,
        .m10 = sin_z * cos_y + cos_z * sin_x * sin_y,
        .m11 = cos_z * cos_x,
        .m12 = sin_z * sin_y - cos_z * sin_x * cos_y,
        .m20 = -cos_x * sin_y,
        .m21 = sin_x,
        .m22 = cos_x * cos_y,
    };
    return matrix;
}

struct point_3d render_apply_rotation_matrix_3d(const struct rotation_matrix_3x3 *matrix,
                                                float point_x, float point_y, float point_z) {
    struct point_3d result = {
        .x = matrix->m00 * point_x + matrix->m01 * point_y + matrix->m02 * point_z,
        .y = matrix->m10 * point_x + matrix->m11 * point_y + matrix->m12 * point_z,
        .z = matrix->m20 * point_x + matrix->m21 * point_y + matrix->m22 * point_z,
    };
    return result;
}

static inline uint8_t alpha_to_u8(float alpha) {
    float clamped = render_clamp_f32(alpha, 0.0f, 1.0f);
    return (uint8_t)lroundf(clamped * 255.0f);
}

static inline uint32_t div_255_fast(uint32_t value) {
    uint32_t shifted = value + 128u;
    return (shifted + (shifted >> 8u)) >> 8u;
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
    ensure_bezel_table_initialized();

    for (uint32_t logical_y = 0u; logical_y < SCREEN_HEIGHT; logical_y++) {
        uint32_t physical_y = render_map_coordinate_180(logical_y);
        uint32_t *row_pixels = surface->pixels + physical_y * surface->stride_pixels;
        int16_t half_w = bezel_half_width_table[logical_y];

        if (half_w < 0) {
            for (uint32_t col = 0u; col < SCREEN_WIDTH; col++) {
                row_pixels[col] = COLOR_OUTSIDE_BEZEL;
            }
            continue;
        }

        int32_t logical_min_x = SCREEN_CENTER_X - (int32_t)half_w;
        int32_t logical_max_x = SCREEN_CENTER_X + (int32_t)half_w;
        if (logical_min_x < 0) {
            logical_min_x = 0;
        }
        if (logical_max_x > (int32_t)SCREEN_COORDINATE_MAX) {
            logical_max_x = (int32_t)SCREEN_COORDINATE_MAX;
        }

        /* 180-degree X flip reverses [logical_min_x .. logical_max_x] to [phys_min .. phys_max] */
        uint32_t phys_min_x = render_map_coordinate_180((uint32_t)logical_max_x);
        uint32_t phys_max_x = render_map_coordinate_180((uint32_t)logical_min_x);

        for (uint32_t col = 0u; col < phys_min_x; col++) {
            row_pixels[col] = COLOR_OUTSIDE_BEZEL;
        }
        for (uint32_t col = phys_min_x; col <= phys_max_x; col++) {
            row_pixels[col] = canvas_color;
        }
        for (uint32_t col = phys_max_x + 1u; col < SCREEN_WIDTH; col++) {
            row_pixels[col] = COLOR_OUTSIDE_BEZEL;
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

    uint32_t out_r = div_255_fast(src_r * (uint32_t)alpha + dst_r * inv_alpha);
    uint32_t out_g = div_255_fast(src_g * (uint32_t)alpha + dst_g * inv_alpha);
    uint32_t out_b = div_255_fast(src_b * (uint32_t)alpha + dst_b * inv_alpha);

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
    float max_dist = half_width + 0.5f;
    float max_dist_sq = max_dist * max_dist;
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
    float inv_length_sq = length_sq > 1e-6f ? (1.0f / length_sq) : 0.0f;

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float rel_y = (float)pixel_y - start_y;
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float rel_x = (float)pixel_x - start_x;
            float proj_t = 0.0f;
            if (inv_length_sq > 0.0f) {
                proj_t = (rel_x * delta_x + rel_y * delta_y) * inv_length_sq;
                if (proj_t < 0.0f) {
                    proj_t = 0.0f;
                } else if (proj_t > 1.0f) {
                    proj_t = 1.0f;
                }
            }
            float dist_x = rel_x - proj_t * delta_x;
            float dist_y = rel_y - proj_t * delta_y;
            float dist_sq = dist_x * dist_x + dist_y * dist_y;
            if (dist_sq >= max_dist_sq) {
                continue;
            }
            float coverage = max_dist - sqrtf(dist_sq);
            if (coverage > 1.0f) {
                coverage = 1.0f;
            }
            render_blend_pixel(surface, pixel_x, pixel_y, color, alpha_to_u8(alpha * coverage));
        }
    }
}

static void compute_annular_row_spans(float center_x, float offset_y, float inner_r, float outer_r,
                                      int32_t *left_min_out, int32_t *left_max_out,
                                      int32_t *right_min_out, int32_t *right_max_out) {
    float abs_y = fabsf(offset_y);
    float outer_dx = sqrtf(fmaxf(0.0f, outer_r * outer_r - abs_y * abs_y));
    if (inner_r > 0.0f && abs_y < inner_r) {
        float inner_dx = sqrtf(inner_r * inner_r - abs_y * abs_y);
        *left_min_out = clamp_pixel_x(center_x - outer_dx);
        *left_max_out = clamp_pixel_x(center_x - inner_dx);
        *right_min_out = clamp_pixel_x(center_x + inner_dx);
        *right_max_out = clamp_pixel_x(center_x + outer_dx);
        if (*left_max_out >= *right_min_out) {
            *left_max_out = *right_max_out;
            *right_min_out = 1;
            *right_max_out = 0;
        }
    } else {
        *left_min_out = clamp_pixel_x(center_x - outer_dx);
        *left_max_out = clamp_pixel_x(center_x + outer_dx);
        *right_min_out = 1;
        *right_max_out = 0;
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
    float inner_cutoff = radius - half_width - 1.5f;
    float inner_cutoff_sq = inner_cutoff > 0.0f ? inner_cutoff * inner_cutoff : 0.0f;
    float outer_cutoff_sq = outer_margin * outer_margin;

    int32_t min_y = clamp_pixel_y(center_y - outer_margin);
    int32_t max_y = clamp_pixel_y(center_y + outer_margin);

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float offset_y = (float)pixel_y - center_y;
        int32_t spans[2][2];
        compute_annular_row_spans(center_x, offset_y, inner_cutoff, outer_margin, &spans[0][0],
                                  &spans[0][1], &spans[1][0], &spans[1][1]);

        for (int32_t span_index = 0; span_index < 2; span_index++) {
            for (int32_t pixel_x = spans[span_index][0]; pixel_x <= spans[span_index][1];
                 pixel_x++) {
                float offset_x = (float)pixel_x - center_x;
                float dist_sq = offset_x * offset_x + offset_y * offset_y;
                if (dist_sq < inner_cutoff_sq || dist_sq > outer_cutoff_sq) {
                    continue;
                }

                float dist = sqrtf(dist_sq);
                float radial_cov =
                    render_clamp_f32(half_width + 0.5f - fabsf(dist - radius), 0.0f, 1.0f);
                if (radial_cov <= 0.0f) {
                    continue;
                }

                float coverage = radial_cov;
                if (!is_full_circle) {
                    float angle = atan2f(offset_y, offset_x);
                    float relative = angle - start_angle;
                    while (relative < 0.0f) {
                        relative += FULL_CIRCLE_RADIANS;
                    }
                    while (relative >= FULL_CIRCLE_RADIANS) {
                        relative -= FULL_CIRCLE_RADIANS;
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
                    render_blend_pixel(surface, pixel_x, pixel_y, color,
                                       alpha_to_u8(alpha * coverage));
                }
            }
        }
    }
}

void render_draw_segmented_ring_aa(const struct framebuffer_surface *surface,
                                   const struct segmented_ring_params *params) {
    if (surface == NULL || surface->pixels == NULL || params == NULL ||
        params->segment_count == 0u || params->radius <= 0.0f) {
        return;
    }

    float max_width = params->filled_width;
    if (params->leading_width > max_width) {
        max_width = params->leading_width;
    }
    if (params->unfilled_width > max_width) {
        max_width = params->unfilled_width;
    }
    if (max_width <= 0.0f) {
        return;
    }

    float max_half_w = max_width * 0.5f;
    float outer_margin = params->radius + max_half_w + 1.5f;
    float inner_cutoff = params->radius - max_half_w - 1.5f;
    float inner_cutoff_sq = inner_cutoff > 0.0f ? inner_cutoff * inner_cutoff : 0.0f;
    float outer_cutoff_sq = outer_margin * outer_margin;

    float pitch = FULL_CIRCLE_RADIANS / (float)params->segment_count;
    float inv_pitch = (float)params->segment_count / FULL_CIRCLE_RADIANS;
    float arc_span = pitch * params->duty_ratio;

    int32_t min_y = clamp_pixel_y(params->center_y - outer_margin);
    int32_t max_y = clamp_pixel_y(params->center_y + outer_margin);

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float offset_y = (float)pixel_y - params->center_y;
        int32_t spans[2][2];
        compute_annular_row_spans(params->center_x, offset_y, inner_cutoff, outer_margin,
                                  &spans[0][0], &spans[0][1], &spans[1][0], &spans[1][1]);

        for (int32_t span_index = 0; span_index < 2; span_index++) {
            for (int32_t pixel_x = spans[span_index][0]; pixel_x <= spans[span_index][1];
                 pixel_x++) {
                float offset_x = (float)pixel_x - params->center_x;
                float dist_sq = offset_x * offset_x + offset_y * offset_y;
                if (dist_sq < inner_cutoff_sq || dist_sq > outer_cutoff_sq) {
                    continue;
                }

                float dist = sqrtf(dist_sq);
                float angle = atan2f(offset_y, offset_x);
                float relative = angle - params->start_angle_offset;
                while (relative < 0.0f) {
                    relative += FULL_CIRCLE_RADIANS;
                }
                while (relative >= FULL_CIRCLE_RADIANS) {
                    relative -= FULL_CIRCLE_RADIANS;
                }

                float segment_pos = relative * inv_pitch;
                uint32_t segment_index = (uint32_t)segment_pos;
                if (segment_index >= params->segment_count) {
                    segment_index = params->segment_count - 1u;
                }
                float in_seg_angle = relative - (float)segment_index * pitch;

                float seg_width;
                uint32_t seg_color;
                float seg_alpha;

                if (params->major_stride > 0u) {
                    bool is_major = (segment_index % params->major_stride) == 0u;
                    seg_width = is_major ? params->filled_width : params->unfilled_width;
                    seg_color = is_major ? params->highlight_color : params->unfilled_color;
                    seg_alpha = is_major ? params->filled_alpha : params->unfilled_alpha;
                } else {
                    bool is_filled = segment_index < params->filled_count;
                    if (is_filled) {
                        bool is_leading =
                            (params->filled_count > 0u && segment_index == params->filled_count - 1u);
                        seg_width = is_leading ? params->leading_width : params->filled_width;
                        if (params->alternating_colors) {
                            seg_color = ((segment_index % 2u) == 0u) ? params->highlight_color
                                                                     : params->primary_color;
                        } else {
                            seg_color = params->highlight_color;
                        }
                        seg_alpha = params->filled_alpha;
                    } else {
                        seg_width = params->unfilled_width;
                        seg_color = params->unfilled_color;
                        seg_alpha = params->unfilled_alpha;
                    }
                }

                if (seg_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
                    continue;
                }

                float radial_cov =
                    render_clamp_f32(seg_width * 0.5f + 0.5f - fabsf(dist - params->radius), 0.0f,
                                     1.0f);
                if (radial_cov <= 0.0f) {
                    continue;
                }

                float angular_dist;
                if (in_seg_angle <= arc_span) {
                    float dist_start = in_seg_angle * params->radius;
                    float dist_end = (arc_span - in_seg_angle) * params->radius;
                    angular_dist = dist_start < dist_end ? dist_start : dist_end;
                } else {
                    float dist_past_end = (in_seg_angle - arc_span) * params->radius;
                    float dist_before_start = (pitch - in_seg_angle) * params->radius;
                    angular_dist = -(dist_past_end < dist_before_start ? dist_past_end
                                                                       : dist_before_start);
                }
                float angular_cov = render_clamp_f32(angular_dist + 0.5f, 0.0f, 1.0f);
                float coverage = radial_cov * angular_cov;
                if (coverage > 0.0f) {
                    render_blend_pixel(surface, pixel_x, pixel_y, seg_color,
                                       alpha_to_u8(seg_alpha * coverage));
                }
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

static bool prepare_convex_polygon(const struct point_2d *vertices, uint32_t vertex_count,
                                   float *normal_x, float *normal_y, int32_t *min_x_out,
                                   int32_t *max_x_out, int32_t *min_y_out, int32_t *max_y_out) {
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
        return false;
    }

    float winding_sign = signed_area > 0.0f ? 1.0f : -1.0f;
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

    *min_x_out = clamp_pixel_x(min_fx - 1.5f);
    *max_x_out = clamp_pixel_x(max_fx + 1.5f);
    *min_y_out = clamp_pixel_y(min_fy - 1.5f);
    *max_y_out = clamp_pixel_y(max_fy + 1.5f);
    return true;
}

void render_draw_filled_convex_polygon_aa(const struct framebuffer_surface *surface,
                                          const struct point_2d *vertices, uint32_t vertex_count,
                                          uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || vertices == NULL || vertex_count < 3u ||
        vertex_count > MAX_CONVEX_POLYGON_VERTICES || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float normal_x[MAX_CONVEX_POLYGON_VERTICES];
    float normal_y[MAX_CONVEX_POLYGON_VERTICES];
    int32_t min_x = 0;
    int32_t max_x = 0;
    int32_t min_y = 0;
    int32_t max_y = 0;

    if (!prepare_convex_polygon(vertices, vertex_count, normal_x, normal_y, &min_x, &max_x, &min_y,
                                &max_y)) {
        return;
    }

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

void render_draw_filled_convex_polygon_edged_aa(const struct framebuffer_surface *surface,
                                                const struct point_2d *vertices,
                                                uint32_t vertex_count, uint32_t fill_color,
                                                uint32_t edge_color, float edge_width,
                                                float alpha) {
    if (surface == NULL || surface->pixels == NULL || vertices == NULL || vertex_count < 3u ||
        vertex_count > MAX_CONVEX_POLYGON_VERTICES || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float normal_x[MAX_CONVEX_POLYGON_VERTICES];
    float normal_y[MAX_CONVEX_POLYGON_VERTICES];
    int32_t min_x = 0;
    int32_t max_x = 0;
    int32_t min_y = 0;
    int32_t max_y = 0;

    if (!prepare_convex_polygon(vertices, vertex_count, normal_x, normal_y, &min_x, &max_x, &min_y,
                                &max_y)) {
        return;
    }

    uint32_t fill_r = (fill_color >> 16u) & 0xFFu;
    uint32_t fill_g = (fill_color >> 8u) & 0xFFu;
    uint32_t fill_b = fill_color & 0xFFu;
    uint32_t edge_r = (edge_color >> 16u) & 0xFFu;
    uint32_t edge_g = (edge_color >> 8u) & 0xFFu;
    uint32_t edge_b = edge_color & 0xFFu;

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

            float outer_cov = render_clamp_f32(0.5f - max_signed_dist, 0.0f, 1.0f);
            if (outer_cov <= 0.0f) {
                continue;
            }
            float inner_cov = render_clamp_f32(0.5f - edge_width - max_signed_dist, 0.0f, 1.0f);
            if (inner_cov >= 1.0f) {
                render_blend_pixel(surface, pixel_x, pixel_y, fill_color, alpha_to_u8(alpha));
            } else if (inner_cov <= 0.0f) {
                render_blend_pixel(surface, pixel_x, pixel_y, edge_color,
                                   alpha_to_u8(alpha * outer_cov));
            } else {
                float edge_ratio = (outer_cov - inner_cov) / outer_cov;
                uint32_t edge_u8 = (uint32_t)lroundf(edge_ratio * 255.0f);
                uint32_t fill_u8 = 255u - edge_u8;
                uint32_t mix_r = div_255_fast(fill_r * fill_u8 + edge_r * edge_u8);
                uint32_t mix_g = div_255_fast(fill_g * fill_u8 + edge_g * edge_u8);
                uint32_t mix_b = div_255_fast(fill_b * fill_u8 + edge_b * edge_u8);
                uint32_t mixed_color = (mix_r << 16u) | (mix_g << 8u) | mix_b;
                render_blend_pixel(surface, pixel_x, pixel_y, mixed_color,
                                   alpha_to_u8(alpha * outer_cov));
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
                uint8_t blended_alpha =
                    (uint8_t)lroundf(((float)coverage_u8 / 255.0f) * alpha * 255.0f);
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
