#include "render_primitives.h"

#include "constants.h"

#include <math.h>
#include <stddef.h>

#define MAX_CONVEX_POLYGON_VERTICES 8u

static void blend_horizontal_span(const struct framebuffer_surface *surface, int32_t logical_y,
                                  int32_t start_x, int32_t end_x, uint32_t color,
                                  uint8_t alpha_u8);

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

struct point_3d render_project_point_3d(float point_x, float point_y, float point_z,
                                        float center_x, float center_y) {
    float denominator = PERSPECTIVE_FOCAL_PX - point_z;
    float scale = fabsf(denominator) > 0.001f ? (PERSPECTIVE_FOCAL_PX / denominator) : 1.0f;
    struct point_3d projected = {
        .x = center_x + point_x * scale,
        .y = center_y + point_y * scale,
        .z = point_z,
    };
    return projected;
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
    uint32_t opaque_canvas = 0xFF000000u | (canvas_color & 0x00FFFFFFu);
    for (uint32_t physical_y = 0u; physical_y < SCREEN_HEIGHT; physical_y++) {
        uint32_t *row_pixels = surface->pixels + physical_y * surface->stride_pixels;
        for (uint32_t physical_x = 0u; physical_x < SCREEN_WIDTH; physical_x++) {
            row_pixels[physical_x] = opaque_canvas;
        }
    }
}

void render_blend_pixel(const struct framebuffer_surface *surface, int32_t logical_x,
                        int32_t logical_y, uint32_t color, uint8_t alpha) {
    if (alpha == 0u || logical_x < 0 || logical_x >= (int32_t)SCREEN_WIDTH || logical_y < 0 ||
        logical_y >= (int32_t)SCREEN_HEIGHT) {
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

    float outer_reach = radius + 0.5f;
    float outer_sq = outer_reach * outer_reach;
    float inner_reach = radius - 0.5f;
    float inner_sq = inner_reach > 0.0f ? (inner_reach * inner_reach) : 0.0f;
    uint8_t alpha_u8 = alpha_to_u8(alpha);

    int32_t min_y = clamp_pixel_y(center_y - outer_reach - 1.0f);
    int32_t max_y = clamp_pixel_y(center_y + outer_reach + 1.0f);

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float offset_y = (float)pixel_y - center_y;
        float rem_outer = outer_sq - offset_y * offset_y;
        if (rem_outer <= 0.0f) {
            continue;
        }
        float outer_dx = sqrtf(rem_outer);
        int32_t row_min_x = clamp_pixel_x(center_x - outer_dx - 0.5f);
        int32_t row_max_x = clamp_pixel_x(center_x + outer_dx + 0.5f);

        int32_t solid_min_x = row_max_x + 1;
        int32_t solid_max_x = row_min_x - 1;
        float rem_inner = inner_sq - offset_y * offset_y;
        if (inner_reach > 0.0f && rem_inner > 0.0f) {
            float inner_dx = sqrtf(rem_inner);
            solid_min_x = (int32_t)ceilf(center_x - inner_dx);
            solid_max_x = (int32_t)floorf(center_x + inner_dx);
            if (solid_min_x < row_min_x) {
                solid_min_x = row_min_x;
            }
            if (solid_max_x > row_max_x) {
                solid_max_x = row_max_x;
            }
        }

        if (solid_min_x <= solid_max_x) {
            for (int32_t pixel_x = row_min_x; pixel_x < solid_min_x; pixel_x++) {
                float offset_x = (float)pixel_x - center_x;
                float dist = sqrtf(offset_x * offset_x + offset_y * offset_y);
                float coverage = render_clamp_f32(outer_reach - dist, 0.0f, 1.0f);
                if (coverage > 0.0f) {
                    render_blend_pixel(surface, pixel_x, pixel_y, color,
                                       alpha_to_u8(alpha * coverage));
                }
            }
            blend_horizontal_span(surface, pixel_y, solid_min_x, solid_max_x, color, alpha_u8);
            for (int32_t pixel_x = solid_max_x + 1; pixel_x <= row_max_x; pixel_x++) {
                float offset_x = (float)pixel_x - center_x;
                float dist = sqrtf(offset_x * offset_x + offset_y * offset_y);
                float coverage = render_clamp_f32(outer_reach - dist, 0.0f, 1.0f);
                if (coverage > 0.0f) {
                    render_blend_pixel(surface, pixel_x, pixel_y, color,
                                       alpha_to_u8(alpha * coverage));
                }
            }
        } else {
            for (int32_t pixel_x = row_min_x; pixel_x <= row_max_x; pixel_x++) {
                float offset_x = (float)pixel_x - center_x;
                float dist = sqrtf(offset_x * offset_x + offset_y * offset_y);
                float coverage = render_clamp_f32(outer_reach - dist, 0.0f, 1.0f);
                if (coverage > 0.0f) {
                    render_blend_pixel(surface, pixel_x, pixel_y, color,
                                       alpha_to_u8(alpha * coverage));
                }
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

    int32_t full_min_x = (int32_t)ceilf(left_x + 0.5f);
    int32_t full_max_x = (int32_t)floorf(right_x - 0.5f);
    if (full_min_x < min_x) {
        full_min_x = min_x;
    }
    if (full_max_x > max_x) {
        full_max_x = max_x;
    }

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float cov_y = render_clamp_f32((float)pixel_y - top_y + 0.5f, 0.0f, 1.0f) *
                      render_clamp_f32(bottom_y - (float)pixel_y + 0.5f, 0.0f, 1.0f);
        if (cov_y <= 0.0f) {
            continue;
        }
        uint8_t row_alpha_u8 = alpha_to_u8(alpha * cov_y);
        if (row_alpha_u8 == 0u) {
            continue;
        }
        if (full_min_x <= full_max_x) {
            for (int32_t pixel_x = min_x; pixel_x < full_min_x; pixel_x++) {
                float cov_x = render_clamp_f32((float)pixel_x - left_x + 0.5f, 0.0f, 1.0f) *
                              render_clamp_f32(right_x - (float)pixel_x + 0.5f, 0.0f, 1.0f);
                if (cov_x > 0.0f) {
                    render_blend_pixel(surface, pixel_x, pixel_y, color,
                                       alpha_to_u8(alpha * cov_y * cov_x));
                }
            }
            blend_horizontal_span(surface, pixel_y, full_min_x, full_max_x, color, row_alpha_u8);
            for (int32_t pixel_x = full_max_x + 1; pixel_x <= max_x; pixel_x++) {
                float cov_x = render_clamp_f32((float)pixel_x - left_x + 0.5f, 0.0f, 1.0f) *
                              render_clamp_f32(right_x - (float)pixel_x + 0.5f, 0.0f, 1.0f);
                if (cov_x > 0.0f) {
                    render_blend_pixel(surface, pixel_x, pixel_y, color,
                                       alpha_to_u8(alpha * cov_y * cov_x));
                }
            }
        } else {
            for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
                float cov_x = render_clamp_f32((float)pixel_x - left_x + 0.5f, 0.0f, 1.0f) *
                              render_clamp_f32(right_x - (float)pixel_x + 0.5f, 0.0f, 1.0f);
                float coverage = cov_x * cov_y;
                if (coverage > 0.0f) {
                    render_blend_pixel(surface, pixel_x, pixel_y, color,
                                       alpha_to_u8(alpha * coverage));
                }
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
    render_draw_filled_convex_polygon_fast(surface, vertices, 8u, color, alpha);
}

void render_draw_line_fast(const struct framebuffer_surface *surface, float start_x, float start_y,
                           float end_x, float end_y, uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    uint8_t alpha_u8 = alpha_to_u8(alpha);
    if (alpha_u8 == 0u) {
        return;
    }

    int32_t x0 = (int32_t)lroundf(start_x);
    int32_t y0 = (int32_t)lroundf(start_y);
    int32_t x1 = (int32_t)lroundf(end_x);
    int32_t y1 = (int32_t)lroundf(end_y);

    int32_t delta_x = x1 >= x0 ? (x1 - x0) : (x0 - x1);
    int32_t delta_y = y1 >= y0 ? (y1 - y0) : (y0 - y1);
    int32_t step_x = (x0 < x1) ? 1 : -1;
    int32_t step_y = (y0 < y1) ? 1 : -1;
    int32_t error_term = delta_x - delta_y;

    for (;;) {
        render_blend_pixel(surface, x0, y0, color, alpha_u8);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int32_t doubled_error = error_term * 2;
        if (doubled_error > -delta_y) {
            error_term -= delta_y;
            x0 += step_x;
        }
        if (doubled_error < delta_x) {
            error_term += delta_x;
            y0 += step_y;
        }
    }
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

static void blend_horizontal_span(const struct framebuffer_surface *surface, int32_t logical_y,
                                  int32_t start_x, int32_t end_x, uint32_t color,
                                  uint8_t alpha_u8) {
    if (logical_y < 0 || logical_y >= (int32_t)SCREEN_HEIGHT || alpha_u8 == 0u) {
        return;
    }
    int32_t span_min_x = start_x > 0 ? start_x : 0;
    int32_t span_max_x =
        end_x < (int32_t)SCREEN_COORDINATE_MAX ? end_x : (int32_t)SCREEN_COORDINATE_MAX;
    if (span_min_x > span_max_x) {
        return;
    }

    uint32_t physical_y = render_map_coordinate_180((uint32_t)logical_y);
    uint32_t *row_pixels = surface->pixels + physical_y * surface->stride_pixels;
    uint32_t physical_start_x = render_map_coordinate_180((uint32_t)span_max_x);
    uint32_t physical_end_x = render_map_coordinate_180((uint32_t)span_min_x);
    uint32_t opaque_color = 0xFF000000u | (color & 0x00FFFFFFu);

    if (alpha_u8 == 255u) {
        for (uint32_t pixel_x = physical_start_x; pixel_x <= physical_end_x; pixel_x++) {
            row_pixels[pixel_x] = opaque_color;
        }
        return;
    }

    uint32_t src_r = (color >> 16u) & 0xFFu;
    uint32_t src_g = (color >> 8u) & 0xFFu;
    uint32_t src_b = color & 0xFFu;
    uint32_t inv_alpha = 255u - (uint32_t)alpha_u8;

    for (uint32_t pixel_x = physical_start_x; pixel_x <= physical_end_x; pixel_x++) {
        uint32_t dst = row_pixels[pixel_x];
        uint32_t dst_r = (dst >> 16u) & 0xFFu;
        uint32_t dst_g = (dst >> 8u) & 0xFFu;
        uint32_t dst_b = dst & 0xFFu;
        uint32_t out_r = div_255_fast(src_r * alpha_u8 + dst_r * inv_alpha);
        uint32_t out_g = div_255_fast(src_g * alpha_u8 + dst_g * inv_alpha);
        uint32_t out_b = div_255_fast(src_b * alpha_u8 + dst_b * inv_alpha);
        row_pixels[pixel_x] = 0xFF000000u | (out_r << 16u) | (out_g << 8u) | out_b;
    }
}

void render_draw_filled_convex_polygon_fast(const struct framebuffer_surface *surface,
                                            const struct point_2d *vertices, uint32_t vertex_count,
                                            uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || vertices == NULL || vertex_count < 3u ||
        vertex_count > MAX_FAST_POLYGON_VERTICES || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float min_y_f = vertices[0].y;
    float max_y_f = vertices[0].y;
    for (uint32_t index = 1u; index < vertex_count; index++) {
        if (vertices[index].y < min_y_f) {
            min_y_f = vertices[index].y;
        }
        if (vertices[index].y > max_y_f) {
            max_y_f = vertices[index].y;
        }
    }

    int32_t min_y = (int32_t)ceilf(min_y_f - 0.5f);
    int32_t max_y = (int32_t)floorf(max_y_f - 0.5f);
    if (min_y < 0) {
        min_y = 0;
    }
    if (max_y > (int32_t)SCREEN_COORDINATE_MAX) {
        max_y = (int32_t)SCREEN_COORDINATE_MAX;
    }

    uint8_t alpha_u8 = alpha_to_u8(alpha);
    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float scan_y = (float)pixel_y + 0.5f;
        float span_min_x = 1e9f;
        float span_max_x = -1e9f;
        bool has_intersection = false;

        for (uint32_t edge_index = 0u; edge_index < vertex_count; edge_index++) {
            uint32_t next_index = (edge_index + 1u) % vertex_count;
            float y0 = vertices[edge_index].y;
            float y1 = vertices[next_index].y;
            if ((y0 <= scan_y && y1 > scan_y) || (y1 <= scan_y && y0 > scan_y)) {
                float factor = (scan_y - y0) / (y1 - y0);
                float x_intersect =
                    vertices[edge_index].x + (vertices[next_index].x - vertices[edge_index].x) * factor;
                if (!has_intersection) {
                    span_min_x = x_intersect;
                    span_max_x = x_intersect;
                    has_intersection = true;
                } else {
                    if (x_intersect < span_min_x) {
                        span_min_x = x_intersect;
                    }
                    if (x_intersect > span_max_x) {
                        span_max_x = x_intersect;
                    }
                }
            }
        }

        if (has_intersection) {
            int32_t start_x = (int32_t)ceilf(span_min_x - 0.5f);
            int32_t end_x = (int32_t)floorf(span_max_x - 0.5f);
            if (start_x <= end_x) {
                blend_horizontal_span(surface, pixel_y, start_x, end_x, color, alpha_u8);
            }
        }
    }
}

uint32_t render_clip_polygon_to_convex_quad(const struct point_2d *subject_vertices,
                                            uint32_t subject_count,
                                            const struct point_2d *clip_quad_vertices,
                                            struct point_2d *clipped_out) {
    if (subject_vertices == NULL || clip_quad_vertices == NULL || clipped_out == NULL ||
        subject_count < 3u || subject_count > 8u) {
        return 0u;
    }

    float signed_area = 0.0f;
    for (uint32_t index = 0u; index < 4u; index++) {
        uint32_t next_index = (index + 1u) & 3u;
        signed_area += clip_quad_vertices[index].x * clip_quad_vertices[next_index].y -
                       clip_quad_vertices[next_index].x * clip_quad_vertices[index].y;
    }
    float orientation_sign = (signed_area >= 0.0f) ? 1.0f : -1.0f;

    struct point_2d buffer_a[12];
    struct point_2d buffer_b[12];
    for (uint32_t index = 0u; index < subject_count; index++) {
        buffer_a[index] = subject_vertices[index];
    }
    uint32_t current_count = subject_count;

    for (uint32_t edge_index = 0u; edge_index < 4u; edge_index++) {
        if (current_count == 0u) {
            return 0u;
        }
        struct point_2d edge_start = clip_quad_vertices[edge_index];
        struct point_2d edge_end = clip_quad_vertices[(edge_index + 1u) & 3u];
        float edge_dx = edge_end.x - edge_start.x;
        float edge_dy = edge_end.y - edge_start.y;

        uint32_t output_count = 0u;
        struct point_2d prev_point = buffer_a[current_count - 1u];
        float prev_side =
            (edge_dx * (prev_point.y - edge_start.y) - edge_dy * (prev_point.x - edge_start.x)) *
            orientation_sign;

        for (uint32_t point_index = 0u; point_index < current_count; point_index++) {
            struct point_2d curr_point = buffer_a[point_index];
            float curr_side =
                (edge_dx * (curr_point.y - edge_start.y) - edge_dy * (curr_point.x - edge_start.x)) *
                orientation_sign;

            if (curr_side >= 0.0f) {
                if (prev_side < 0.0f && output_count < 12u) {
                    float denom = prev_side - curr_side;
                    float factor = fabsf(denom) > 1e-6f ? (prev_side / denom) : 0.0f;
                    buffer_b[output_count++] = (struct point_2d){
                        prev_point.x + (curr_point.x - prev_point.x) * factor,
                        prev_point.y + (curr_point.y - prev_point.y) * factor,
                    };
                }
                if (output_count < 12u) {
                    buffer_b[output_count++] = curr_point;
                }
            } else if (prev_side >= 0.0f && output_count < 12u) {
                float denom = prev_side - curr_side;
                float factor = fabsf(denom) > 1e-6f ? (prev_side / denom) : 0.0f;
                buffer_b[output_count++] = (struct point_2d){
                    prev_point.x + (curr_point.x - prev_point.x) * factor,
                    prev_point.y + (curr_point.y - prev_point.y) * factor,
                };
            }
            prev_point = curr_point;
            prev_side = curr_side;
        }

        for (uint32_t copy_index = 0u; copy_index < output_count; copy_index++) {
            buffer_a[copy_index] = buffer_b[copy_index];
        }
        current_count = output_count;
    }

    uint32_t final_count = current_count > 8u ? 8u : current_count;
    for (uint32_t index = 0u; index < final_count; index++) {
        clipped_out[index] = buffer_a[index];
    }
    return final_count;
}

static void compute_tight_ellipse_bounds(float center_x, float center_y, float radius_x,
                                         float radius_y, float angle_radians, float margin,
                                         int32_t *min_x_out, int32_t *max_x_out,
                                         int32_t *min_y_out, int32_t *max_y_out) {
    float bound_x;
    float bound_y;
    if (angle_radians == 0.0f) {
        bound_x = radius_x + margin;
        bound_y = radius_y + margin;
    } else {
        float cos_a = cosf(angle_radians);
        float sin_a = sinf(angle_radians);
        bound_x = sqrtf((radius_x * cos_a) * (radius_x * cos_a) +
                        (radius_y * sin_a) * (radius_y * sin_a)) +
                  margin;
        bound_y = sqrtf((radius_x * sin_a) * (radius_x * sin_a) +
                        (radius_y * cos_a) * (radius_y * cos_a)) +
                  margin;
    }
    int32_t min_x = (int32_t)floorf(center_x - bound_x);
    int32_t max_x = (int32_t)ceilf(center_x + bound_x);
    int32_t min_y = (int32_t)floorf(center_y - bound_y);
    int32_t max_y = (int32_t)ceilf(center_y + bound_y);
    *min_x_out = min_x < 0 ? 0 : min_x;
    *max_x_out = max_x > (int32_t)SCREEN_COORDINATE_MAX ? (int32_t)SCREEN_COORDINATE_MAX : max_x;
    *min_y_out = min_y < 0 ? 0 : min_y;
    *max_y_out = max_y > (int32_t)SCREEN_COORDINATE_MAX ? (int32_t)SCREEN_COORDINATE_MAX : max_y;
}

void render_draw_filled_ellipse_aa(const struct framebuffer_surface *surface, float center_x,
                                   float center_y, float radius_x, float radius_y,
                                   float angle_radians, uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || radius_x <= 0.1f || radius_y <= 0.1f ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    int32_t min_x, max_x, min_y, max_y;
    compute_tight_ellipse_bounds(center_x, center_y, radius_x, radius_y, angle_radians, 1.5f,
                                 &min_x, &max_x, &min_y, &max_y);

    bool axis_aligned = (angle_radians == 0.0f);
    float cos_a = axis_aligned ? 1.0f : cosf(angle_radians);
    float sin_a = axis_aligned ? 0.0f : sinf(angle_radians);
    float inv_rx = 1.0f / radius_x;
    float inv_ry = 1.0f / radius_y;
    float min_r = radius_x < radius_y ? radius_x : radius_y;
    float max_norm = 1.0f + 0.5f / min_r;
    float max_norm_sq = max_norm * max_norm;

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float offset_y = (float)pixel_y + 0.5f - center_y;
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float offset_x = (float)pixel_x + 0.5f - center_x;
            float local_x = axis_aligned ? offset_x : (offset_x * cos_a + offset_y * sin_a);
            float local_y = axis_aligned ? offset_y : (-offset_x * sin_a + offset_y * cos_a);
            float norm_sq =
                (local_x * inv_rx) * (local_x * inv_rx) + (local_y * inv_ry) * (local_y * inv_ry);
            if (norm_sq >= max_norm_sq) {
                continue;
            }
            float signed_px = (sqrtf(norm_sq) - 1.0f) * min_r;
            if (signed_px < 0.5f) {
                float coverage = render_clamp_f32(0.5f - signed_px, 0.0f, 1.0f);
                render_blend_pixel(surface, pixel_x, pixel_y, color,
                                   alpha_to_u8(alpha * coverage));
            }
        }
    }
}

void render_draw_stroked_ellipse_aa(const struct framebuffer_surface *surface, float center_x,
                                    float center_y, float radius_x, float radius_y,
                                    float angle_radians, float line_width, uint32_t color,
                                    float alpha) {
    if (surface == NULL || surface->pixels == NULL || radius_x <= 0.1f || radius_y <= 0.1f ||
        line_width <= 0.0f || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float half_w = line_width * 0.5f;
    int32_t min_x, max_x, min_y, max_y;
    compute_tight_ellipse_bounds(center_x, center_y, radius_x, radius_y, angle_radians,
                                 half_w + 1.5f, &min_x, &max_x, &min_y, &max_y);

    bool axis_aligned = (angle_radians == 0.0f);
    float cos_a = axis_aligned ? 1.0f : cosf(angle_radians);
    float sin_a = axis_aligned ? 0.0f : sinf(angle_radians);
    float inv_rx = 1.0f / radius_x;
    float inv_ry = 1.0f / radius_y;
    float min_r = radius_x < radius_y ? radius_x : radius_y;
    float band_norm = (half_w + 0.5f) / min_r;
    float inner_norm = 1.0f - band_norm;
    float outer_norm = 1.0f + band_norm;
    float inner_norm_sq = inner_norm > 0.0f ? (inner_norm * inner_norm) : 0.0f;
    float outer_norm_sq = outer_norm * outer_norm;

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float offset_y = (float)pixel_y + 0.5f - center_y;
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float offset_x = (float)pixel_x + 0.5f - center_x;
            float local_x = axis_aligned ? offset_x : (offset_x * cos_a + offset_y * sin_a);
            float local_y = axis_aligned ? offset_y : (-offset_x * sin_a + offset_y * cos_a);
            float norm_sq =
                (local_x * inv_rx) * (local_x * inv_rx) + (local_y * inv_ry) * (local_y * inv_ry);
            if (norm_sq <= inner_norm_sq || norm_sq >= outer_norm_sq) {
                continue;
            }
            float dist_px = fabsf((sqrtf(norm_sq) - 1.0f) * min_r);
            if (dist_px < half_w + 0.5f) {
                float coverage = render_clamp_f32(half_w + 0.5f - dist_px, 0.0f, 1.0f);
                render_blend_pixel(surface, pixel_x, pixel_y, color,
                                   alpha_to_u8(alpha * coverage));
            }
        }
    }
}

void render_draw_projected_3d_box(const struct framebuffer_surface *surface,
                                  const struct rotation_matrix_3x3 *matrix, float offset_x,
                                  float offset_y, float offset_z, float half_x, float half_y,
                                  float half_z, float screen_center_x, float screen_center_y,
                                  const struct cube_face_palette *face_palette, uint32_t edge_color,
                                  float alpha) {
    if (surface == NULL || matrix == NULL || face_palette == NULL ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    static const float CORNER_SIGNS[8][3] = {
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

    struct point_3d projected_corners[8];
    for (uint32_t corner = 0u; corner < 8u; corner++) {
        struct point_3d rotated = render_apply_rotation_matrix_3d(
            matrix, offset_x + CORNER_SIGNS[corner][0] * half_x,
            offset_y + CORNER_SIGNS[corner][1] * half_y,
            offset_z + CORNER_SIGNS[corner][2] * half_z);
        projected_corners[corner] =
            render_project_point_3d(rotated.x, rotated.y, rotated.z, screen_center_x,
                                    screen_center_y);
    }

    struct {
        uint8_t face_index;
        float average_depth;
        uint32_t fill_color;
    } visible_faces[6];
    uint32_t visible_count = 0u;

    for (uint32_t face = 0u; face < 6u; face++) {
        struct point_3d normal = render_apply_rotation_matrix_3d(
            matrix, FACE_NORMALS[face][0], FACE_NORMALS[face][1], FACE_NORMALS[face][2]);
        if (normal.z <= 0.01f) {
            continue;
        }
        uint32_t fill_color = face_palette->left;
        if (normal.y < -0.35f) {
            fill_color = face_palette->top;
        } else if (normal.x >= 0.0f) {
            fill_color = face_palette->right;
        }
        float depth_sum = 0.0f;
        for (uint32_t corner = 0u; corner < 4u; corner++) {
            depth_sum += projected_corners[FACE_INDICES[face][corner]].z;
        }
        visible_faces[visible_count].face_index = (uint8_t)face;
        visible_faces[visible_count].average_depth = depth_sum * 0.25f;
        visible_faces[visible_count].fill_color = fill_color;
        visible_count++;
    }

    for (uint32_t pass = 1u; pass < visible_count; pass++) {
        for (uint32_t cursor = pass;
             cursor > 0u &&
             visible_faces[cursor - 1u].average_depth > visible_faces[cursor].average_depth;
             cursor--) {
            uint8_t temp_face = visible_faces[cursor - 1u].face_index;
            float temp_depth = visible_faces[cursor - 1u].average_depth;
            uint32_t temp_color = visible_faces[cursor - 1u].fill_color;
            visible_faces[cursor - 1u] = visible_faces[cursor];
            visible_faces[cursor].face_index = temp_face;
            visible_faces[cursor].average_depth = temp_depth;
            visible_faces[cursor].fill_color = temp_color;
        }
    }

    for (uint32_t index = 0u; index < visible_count; index++) {
        uint8_t face = visible_faces[index].face_index;
        struct point_2d quad[4];
        for (uint32_t corner = 0u; corner < 4u; corner++) {
            struct point_3d vertex = projected_corners[FACE_INDICES[face][corner]];
            quad[corner] = (struct point_2d){vertex.x, vertex.y};
        }
        render_draw_filled_convex_polygon_edged_aa(surface, quad, 4u,
                                                   visible_faces[index].fill_color, edge_color,
                                                   0.85f, alpha);
    }
}

void render_draw_quad_corner_brackets(const struct framebuffer_surface *surface,
                                      const struct point_2d *quad_vertices, float arm_ratio,
                                      float line_width, uint32_t color, float alpha) {
    if (surface == NULL || quad_vertices == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    static const uint32_t CORNER_INDICES[2] = {0u, 2u};
    for (uint32_t slot = 0u; slot < 2u; slot++) {
        uint32_t corner = CORNER_INDICES[slot];
        uint32_t prev = (corner + 3u) & 3u;
        uint32_t next = (corner + 1u) & 3u;
        struct point_2d origin = quad_vertices[corner];
        struct point_2d arm_prev = {
            render_lerp_f32(origin.x, quad_vertices[prev].x, arm_ratio),
            render_lerp_f32(origin.y, quad_vertices[prev].y, arm_ratio),
        };
        struct point_2d arm_next = {
            render_lerp_f32(origin.x, quad_vertices[next].x, arm_ratio),
            render_lerp_f32(origin.y, quad_vertices[next].y, arm_ratio),
        };
        render_draw_line_aa(surface, arm_prev.x, arm_prev.y, origin.x, origin.y, line_width, color,
                            alpha);
        render_draw_line_aa(surface, origin.x, origin.y, arm_next.x, arm_next.y, line_width, color,
                            alpha);
    }
}

void render_draw_rect_corner_brackets(const struct framebuffer_surface *surface, float left_x,
                                      float top_y, float width, float height, float arm_length,
                                      float line_width, uint32_t color, float alpha) {
    if (surface == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    float right_x = left_x + width;
    float bottom_y = top_y + height;

    render_draw_line_aa(surface, left_x, top_y + arm_length, left_x, top_y, line_width, color,
                        alpha);
    render_draw_line_aa(surface, left_x, top_y, left_x + arm_length, top_y, line_width, color,
                        alpha);

    render_draw_line_aa(surface, right_x - arm_length, bottom_y, right_x, bottom_y, line_width,
                        color, alpha);
    render_draw_line_aa(surface, right_x, bottom_y, right_x, bottom_y - arm_length, line_width,
                        color, alpha);
}

void render_draw_text_alpha(const struct framebuffer_surface *surface, enum font_style_id style_id,
                            const char *text, float anchor_x, float anchor_y,
                            enum text_align_mode align, enum text_baseline_mode baseline,
                            uint32_t color, float alpha) {
    render_draw_text_clipped_alpha(surface, style_id, text, anchor_x, anchor_y, align, baseline,
                                   0, (int32_t)SCREEN_COORDINATE_MAX, color, alpha);
}

void render_draw_text_clipped_alpha(const struct framebuffer_surface *surface,
                                    enum font_style_id style_id, const char *text, float anchor_x,
                                    float anchor_y, enum text_align_mode align,
                                    enum text_baseline_mode baseline, int32_t clip_left_x,
                                    int32_t clip_right_x, uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || text == NULL ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD || clip_left_x > clip_right_x) {
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
    uint8_t alpha_u8 = alpha_to_u8(alpha);

    size_t byte_offset = 0u;
    while (text[byte_offset] != '\0') {
        uint32_t codepoint = font_atlas_decode_utf8_next(text, &byte_offset);
        const struct bitmap_glyph *glyph = font_atlas_lookup_glyph(font, codepoint);
        if (glyph == NULL) {
            continue;
        }

        int32_t draw_x = pen_x + (int32_t)glyph->offset_x;
        int32_t draw_y = baseline_y + (int32_t)glyph->offset_y;

        if (draw_x + (int32_t)glyph->width - 1 >= clip_left_x && draw_x <= clip_right_x) {
            for (uint16_t row_index = 0u; row_index < glyph->height; row_index++) {
                for (uint16_t col_index = 0u; col_index < glyph->width; col_index++) {
                    int32_t pixel_x = draw_x + (int32_t)col_index;
                    if (pixel_x < clip_left_x || pixel_x > clip_right_x) {
                        continue;
                    }
                    uint8_t coverage_u8 =
                        font->alpha_data[glyph->bitmap_offset +
                                         (uint32_t)row_index * glyph->width + col_index];
                    if (coverage_u8 == 0u) {
                        continue;
                    }
                    uint8_t blended_alpha =
                        (alpha_u8 == 255u)
                            ? coverage_u8
                            : (uint8_t)div_255_fast((uint32_t)coverage_u8 * (uint32_t)alpha_u8);
                    render_blend_pixel(surface, pixel_x, draw_y + (int32_t)row_index, color,
                                       blended_alpha);
                }
            }
        }
        pen_x += (int32_t)glyph->advance_x;
    }
}

void render_draw_marquee_text(const struct framebuffer_surface *surface, enum font_style_id style_id,
                              const char *text, float center_x, float center_y, float max_width,
                              float elapsed_seconds, uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || text == NULL ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD || max_width <= 0.0f) {
        return;
    }

    const struct bitmap_font *font = font_atlas_get(style_id);
    if (font == NULL) {
        return;
    }

    int32_t text_width = font_atlas_measure_width(font, text);
    if ((float)text_width <= max_width) {
        render_draw_text_alpha(surface, style_id, text, center_x, center_y, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, color, alpha);
        return;
    }

    const float start_pause_seconds = 0.9f;
    const float scroll_speed_px_per_sec = 28.0f;
    const float end_pause_seconds = 0.8f;
    const float gap_px = 36.0f;
    float cycle_distance = (float)text_width + gap_px;
    float scroll_duration = cycle_distance / scroll_speed_px_per_sec;
    float total_cycle = start_pause_seconds + scroll_duration + end_pause_seconds;
    float phase_seconds = fmodf(elapsed_seconds, total_cycle);
    if (phase_seconds < 0.0f) {
        phase_seconds += total_cycle;
    }

    float scroll_offset = 0.0f;
    if (phase_seconds > start_pause_seconds) {
        float active_scroll = phase_seconds - start_pause_seconds;
        if (active_scroll < scroll_duration) {
            scroll_offset = active_scroll * scroll_speed_px_per_sec;
        } else {
            scroll_offset = cycle_distance;
        }
    }

    float left_edge = center_x - max_width * 0.5f;
    int32_t clip_left_x = (int32_t)floorf(left_edge);
    int32_t clip_right_x = (int32_t)ceilf(center_x + max_width * 0.5f);
    float primary_x = left_edge - scroll_offset;

    render_draw_text_clipped_alpha(surface, style_id, text, primary_x, center_y, TEXT_ALIGN_LEFT,
                                   TEXT_BASELINE_MIDDLE, clip_left_x, clip_right_x, color, alpha);
    render_draw_text_clipped_alpha(surface, style_id, text, primary_x + cycle_distance, center_y,
                                   TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE, clip_left_x,
                                   clip_right_x, color, alpha);
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

static struct clip_rect normalize_clip_rect(const struct clip_rect *clip) {
    struct clip_rect normalized = {0, 0, (int32_t)SCREEN_COORDINATE_MAX,
                                   (int32_t)SCREEN_COORDINATE_MAX};
    if (clip == NULL) {
        return normalized;
    }
    if (clip->left_x > normalized.left_x) {
        normalized.left_x = clip->left_x;
    }
    if (clip->top_y > normalized.top_y) {
        normalized.top_y = clip->top_y;
    }
    if (clip->right_x < normalized.right_x) {
        normalized.right_x = clip->right_x;
    }
    if (clip->bottom_y < normalized.bottom_y) {
        normalized.bottom_y = clip->bottom_y;
    }
    return normalized;
}

static void draw_filled_convex_polygon_clipped_fast(const struct framebuffer_surface *surface,
                                                    const struct point_2d *vertices,
                                                    uint32_t vertex_count,
                                                    const struct clip_rect *clip, uint32_t color,
                                                    float alpha) {
    if (surface == NULL || surface->pixels == NULL || vertices == NULL || vertex_count < 3u ||
        vertex_count > MAX_FAST_POLYGON_VERTICES || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    struct clip_rect bounds = normalize_clip_rect(clip);
    if (bounds.left_x > bounds.right_x || bounds.top_y > bounds.bottom_y) {
        return;
    }

    float min_y_f = vertices[0].y;
    float max_y_f = vertices[0].y;
    for (uint32_t index = 1u; index < vertex_count; index++) {
        if (vertices[index].y < min_y_f) {
            min_y_f = vertices[index].y;
        }
        if (vertices[index].y > max_y_f) {
            max_y_f = vertices[index].y;
        }
    }

    int32_t min_y = (int32_t)ceilf(min_y_f - 0.5f);
    int32_t max_y = (int32_t)floorf(max_y_f - 0.5f);
    if (min_y < bounds.top_y) {
        min_y = bounds.top_y;
    }
    if (max_y > bounds.bottom_y) {
        max_y = bounds.bottom_y;
    }

    uint8_t alpha_u8 = alpha_to_u8(alpha);
    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float scan_y = (float)pixel_y + 0.5f;
        float span_min_x = 1e9f;
        float span_max_x = -1e9f;
        bool has_intersection = false;

        for (uint32_t edge_index = 0u; edge_index < vertex_count; edge_index++) {
            uint32_t next_index = (edge_index + 1u) % vertex_count;
            float y0 = vertices[edge_index].y;
            float y1 = vertices[next_index].y;
            if ((y0 <= scan_y && y1 > scan_y) || (y1 <= scan_y && y0 > scan_y)) {
                float factor = (scan_y - y0) / (y1 - y0);
                float x_intersect =
                    vertices[edge_index].x + (vertices[next_index].x - vertices[edge_index].x) * factor;
                if (!has_intersection) {
                    span_min_x = x_intersect;
                    span_max_x = x_intersect;
                    has_intersection = true;
                } else {
                    if (x_intersect < span_min_x) {
                        span_min_x = x_intersect;
                    }
                    if (x_intersect > span_max_x) {
                        span_max_x = x_intersect;
                    }
                }
            }
        }

        if (has_intersection) {
            int32_t start_x = (int32_t)ceilf(span_min_x - 0.5f);
            int32_t end_x = (int32_t)floorf(span_max_x - 0.5f);
            if (start_x < bounds.left_x) {
                start_x = bounds.left_x;
            }
            if (end_x > bounds.right_x) {
                end_x = bounds.right_x;
            }
            if (start_x <= end_x) {
                blend_horizontal_span(surface, pixel_y, start_x, end_x, color, alpha_u8);
            }
        }
    }
}

void render_draw_filled_rect_clipped_alpha(const struct framebuffer_surface *surface, float left_x,
                                           float top_y, float width, float height,
                                           const struct clip_rect *clip, uint32_t color,
                                           float alpha) {
    if (surface == NULL || surface->pixels == NULL || width <= 0.0f || height <= 0.0f ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    struct clip_rect bounds = normalize_clip_rect(clip);
    int32_t min_x = (int32_t)ceilf(left_x - 0.5f);
    int32_t max_x = (int32_t)floorf(left_x + width - 0.5f);
    int32_t min_y = (int32_t)ceilf(top_y - 0.5f);
    int32_t max_y = (int32_t)floorf(top_y + height - 0.5f);
    if (min_x < bounds.left_x) {
        min_x = bounds.left_x;
    }
    if (max_x > bounds.right_x) {
        max_x = bounds.right_x;
    }
    if (min_y < bounds.top_y) {
        min_y = bounds.top_y;
    }
    if (max_y > bounds.bottom_y) {
        max_y = bounds.bottom_y;
    }
    if (min_x > max_x || min_y > max_y) {
        return;
    }
    uint8_t alpha_u8 = alpha_to_u8(alpha);
    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        blend_horizontal_span(surface, pixel_y, min_x, max_x, color, alpha_u8);
    }
}

void render_draw_chamfer_plate_filled_clipped_aa(const struct framebuffer_surface *surface,
                                                 float left_x, float top_y, float width,
                                                 float height, float chamfer,
                                                 const struct clip_rect *clip, uint32_t color,
                                                 float alpha) {
    struct point_2d vertices[8];
    build_chamfer_vertices(left_x, top_y, width, height, chamfer, vertices);
    draw_filled_convex_polygon_clipped_fast(surface, vertices, 8u, clip, color, alpha);
}

static void draw_line_clipped_aa(const struct framebuffer_surface *surface, float start_x,
                                 float start_y, float end_x, float end_y, float line_width,
                                 const struct clip_rect *bounds, uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD ||
        line_width <= 0.0f) {
        return;
    }

    float delta_x = end_x - start_x;
    float delta_y = end_y - start_y;
    float length_sq = delta_x * delta_x + delta_y * delta_y;
    float half_width = line_width * 0.5f;
    float reach = half_width + 1.0f;

    int32_t min_x = (int32_t)floorf((start_x < end_x ? start_x : end_x) - reach);
    int32_t max_x = (int32_t)ceilf((start_x > end_x ? start_x : end_x) + reach);
    int32_t min_y = (int32_t)floorf((start_y < end_y ? start_y : end_y) - reach);
    int32_t max_y = (int32_t)ceilf((start_y > end_y ? start_y : end_y) + reach);
    if (min_x < bounds->left_x) {
        min_x = bounds->left_x;
    }
    if (max_x > bounds->right_x) {
        max_x = bounds->right_x;
    }
    if (min_y < bounds->top_y) {
        min_y = bounds->top_y;
    }
    if (max_y > bounds->bottom_y) {
        max_y = bounds->bottom_y;
    }

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float sample_y = (float)pixel_y + 0.5f;
        for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            float sample_x = (float)pixel_x + 0.5f;
            float projection = 0.0f;
            if (length_sq > 0.0001f) {
                projection = ((sample_x - start_x) * delta_x + (sample_y - start_y) * delta_y) /
                             length_sq;
                projection = render_clamp_f32(projection, 0.0f, 1.0f);
            }
            float closest_x = start_x + delta_x * projection;
            float closest_y = start_y + delta_y * projection;
            float dx = sample_x - closest_x;
            float dy = sample_y - closest_y;
            float distance = sqrtf(dx * dx + dy * dy);
            float coverage = render_clamp_f32(half_width + 0.5f - distance, 0.0f, 1.0f);
            if (coverage > 0.0f) {
                render_blend_pixel(surface, pixel_x, pixel_y, color,
                                   alpha_to_u8(alpha * coverage));
            }
        }
    }
}

void render_draw_rect_corner_brackets_clipped(const struct framebuffer_surface *surface, float left_x,
                                              float top_y, float width, float height,
                                              float arm_length, float line_width,
                                              const struct clip_rect *clip, uint32_t color,
                                              float alpha) {
    if (surface == NULL || alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    struct clip_rect bounds = normalize_clip_rect(clip);
    if (bounds.left_x > bounds.right_x || bounds.top_y > bounds.bottom_y) {
        return;
    }
    float right_x = left_x + width;
    float bottom_y = top_y + height;

    draw_line_clipped_aa(surface, left_x, top_y + arm_length, left_x, top_y, line_width, &bounds,
                         color, alpha);
    draw_line_clipped_aa(surface, left_x, top_y, left_x + arm_length, top_y, line_width, &bounds,
                         color, alpha);
    draw_line_clipped_aa(surface, right_x - arm_length, bottom_y, right_x, bottom_y, line_width,
                         &bounds, color, alpha);
    draw_line_clipped_aa(surface, right_x, bottom_y, right_x, bottom_y - arm_length, line_width,
                         &bounds, color, alpha);
}

void render_draw_text_rect_clipped_alpha(const struct framebuffer_surface *surface,
                                         enum font_style_id style_id, const char *text,
                                         float anchor_x, float anchor_y,
                                         enum text_align_mode align,
                                         enum text_baseline_mode baseline,
                                         const struct clip_rect *clip, uint32_t color,
                                         float alpha) {
    if (surface == NULL || surface->pixels == NULL || text == NULL ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    struct clip_rect bounds = normalize_clip_rect(clip);
    if (bounds.left_x > bounds.right_x || bounds.top_y > bounds.bottom_y) {
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
    uint8_t alpha_u8 = alpha_to_u8(alpha);

    size_t byte_offset = 0u;
    while (text[byte_offset] != '\0') {
        uint32_t codepoint = font_atlas_decode_utf8_next(text, &byte_offset);
        const struct bitmap_glyph *glyph = font_atlas_lookup_glyph(font, codepoint);
        if (glyph == NULL) {
            continue;
        }

        int32_t draw_x = pen_x + (int32_t)glyph->offset_x;
        int32_t draw_y = baseline_y + (int32_t)glyph->offset_y;

        if (draw_x + (int32_t)glyph->width - 1 >= bounds.left_x && draw_x <= bounds.right_x &&
            draw_y + (int32_t)glyph->height - 1 >= bounds.top_y && draw_y <= bounds.bottom_y) {
            for (uint16_t row_index = 0u; row_index < glyph->height; row_index++) {
                int32_t pixel_y = draw_y + (int32_t)row_index;
                if (pixel_y < bounds.top_y || pixel_y > bounds.bottom_y) {
                    continue;
                }
                for (uint16_t col_index = 0u; col_index < glyph->width; col_index++) {
                    int32_t pixel_x = draw_x + (int32_t)col_index;
                    if (pixel_x < bounds.left_x || pixel_x > bounds.right_x) {
                        continue;
                    }
                    uint8_t coverage_u8 =
                        font->alpha_data[glyph->bitmap_offset +
                                         (uint32_t)row_index * glyph->width + col_index];
                    if (coverage_u8 == 0u) {
                        continue;
                    }
                    uint8_t blended_alpha =
                        (alpha_u8 == 255u)
                            ? coverage_u8
                            : (uint8_t)div_255_fast((uint32_t)coverage_u8 * (uint32_t)alpha_u8);
                    render_blend_pixel(surface, pixel_x, pixel_y, color, blended_alpha);
                }
            }
        }
        pen_x += (int32_t)glyph->advance_x;
    }
}

void render_draw_marquee_text_rect_clipped(const struct framebuffer_surface *surface,
                                           enum font_style_id style_id, const char *text,
                                           float center_x, float center_y, float max_width,
                                           float elapsed_seconds, const struct clip_rect *clip,
                                           uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || text == NULL ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD || max_width <= 0.0f) {
        return;
    }

    const struct bitmap_font *font = font_atlas_get(style_id);
    if (font == NULL) {
        return;
    }

    struct clip_rect bounds = normalize_clip_rect(clip);
    int32_t text_width = font_atlas_measure_width(font, text);
    if ((float)text_width <= max_width) {
        render_draw_text_rect_clipped_alpha(surface, style_id, text, center_x, center_y,
                                            TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, &bounds, color,
                                            alpha);
        return;
    }

    const float start_pause_seconds = 0.9f;
    const float scroll_speed_px_per_sec = 28.0f;
    const float end_pause_seconds = 0.8f;
    const float gap_px = 36.0f;
    float cycle_distance = (float)text_width + gap_px;
    float scroll_duration = cycle_distance / scroll_speed_px_per_sec;
    float total_cycle = start_pause_seconds + scroll_duration + end_pause_seconds;
    float phase_seconds = fmodf(elapsed_seconds, total_cycle);
    if (phase_seconds < 0.0f) {
        phase_seconds += total_cycle;
    }

    float scroll_offset = 0.0f;
    if (phase_seconds > start_pause_seconds) {
        float active_scroll = phase_seconds - start_pause_seconds;
        if (active_scroll < scroll_duration) {
            scroll_offset = active_scroll * scroll_speed_px_per_sec;
        } else {
            scroll_offset = cycle_distance;
        }
    }

    float left_edge = center_x - max_width * 0.5f;
    int32_t slot_left_x = (int32_t)floorf(left_edge);
    int32_t slot_right_x = (int32_t)ceilf(center_x + max_width * 0.5f);
    if (slot_left_x > bounds.left_x) {
        bounds.left_x = slot_left_x;
    }
    if (slot_right_x < bounds.right_x) {
        bounds.right_x = slot_right_x;
    }
    float primary_x = left_edge - scroll_offset;

    render_draw_text_rect_clipped_alpha(surface, style_id, text, primary_x, center_y,
                                        TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE, &bounds, color,
                                        alpha);
    render_draw_text_rect_clipped_alpha(surface, style_id, text, primary_x + cycle_distance,
                                        center_y, TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE, &bounds,
                                        color, alpha);
}

void render_draw_annular_wedge_fast(const struct framebuffer_surface *surface, float center_x,
                                    float center_y, float inner_radius, float outer_radius,
                                    float start_angle, float end_angle, uint32_t color,
                                    float alpha) {
    if (surface == NULL || surface->pixels == NULL || outer_radius <= inner_radius ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    float span = end_angle - start_angle;
    if (span <= 0.001f) {
        return;
    }
    uint32_t segments = (uint32_t)ceilf(span / 0.22f);
    if (segments < 1u) {
        segments = 1u;
    }
    if (segments > 32u) {
        segments = 32u;
    }
    float prev_cos = cosf(start_angle);
    float prev_sin = sinf(start_angle);
    for (uint32_t step = 1u; step <= segments; step++) {
        float angle = start_angle + span * ((float)step / (float)segments);
        float next_cos = cosf(angle);
        float next_sin = sinf(angle);
        struct point_2d quad[4] = {
            {center_x + prev_cos * inner_radius, center_y + prev_sin * inner_radius},
            {center_x + prev_cos * outer_radius, center_y + prev_sin * outer_radius},
            {center_x + next_cos * outer_radius, center_y + next_sin * outer_radius},
            {center_x + next_cos * inner_radius, center_y + next_sin * inner_radius},
        };
        render_draw_filled_convex_polygon_fast(surface, quad, 4u, color, alpha);
        prev_cos = next_cos;
        prev_sin = next_sin;
    }
}

void render_draw_heart_filled_aa(const struct framebuffer_surface *surface, float center_x,
                                 float center_y, float radius, uint32_t color, float alpha) {
    if (surface == NULL || surface->pixels == NULL || radius <= 0.5f ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    struct point_2d left_lobe[5] = {
        {center_x, center_y + 0.78f * radius},
        {center_x - 0.86f * radius, center_y - 0.08f * radius},
        {center_x - 0.82f * radius, center_y - 0.58f * radius},
        {center_x - 0.34f * radius, center_y - 0.80f * radius},
        {center_x, center_y - 0.40f * radius},
    };
    struct point_2d right_lobe[5] = {
        {center_x, center_y + 0.78f * radius},
        {center_x, center_y - 0.40f * radius},
        {center_x + 0.34f * radius, center_y - 0.80f * radius},
        {center_x + 0.82f * radius, center_y - 0.58f * radius},
        {center_x + 0.86f * radius, center_y - 0.08f * radius},
    };
    render_draw_filled_convex_polygon_fast(surface, left_lobe, 5u, color, alpha);
    render_draw_filled_convex_polygon_fast(surface, right_lobe, 5u, color, alpha);
}

void render_draw_heart_stroked_aa(const struct framebuffer_surface *surface, float center_x,
                                  float center_y, float radius, float line_width, uint32_t color,
                                  float alpha) {
    if (surface == NULL || surface->pixels == NULL || radius <= 0.5f ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    struct point_2d outline[8] = {
        {center_x, center_y + 0.78f * radius},
        {center_x - 0.86f * radius, center_y - 0.08f * radius},
        {center_x - 0.82f * radius, center_y - 0.58f * radius},
        {center_x - 0.34f * radius, center_y - 0.80f * radius},
        {center_x, center_y - 0.40f * radius},
        {center_x + 0.34f * radius, center_y - 0.80f * radius},
        {center_x + 0.82f * radius, center_y - 0.58f * radius},
        {center_x + 0.86f * radius, center_y - 0.08f * radius},
    };
    for (uint32_t index = 0u; index < 8u; index++) {
        uint32_t next_index = (index + 1u) & 7u;
        render_draw_line_aa(surface, outline[index].x, outline[index].y, outline[next_index].x,
                            outline[next_index].y, line_width, color, alpha);
    }
}

static inline uint32_t rgb565_to_xrgb8888(uint16_t pixel565) {
    uint32_t red5 = (uint32_t)((pixel565 >> 11u) & 0x1Fu);
    uint32_t green6 = (uint32_t)((pixel565 >> 5u) & 0x3Fu);
    uint32_t blue5 = (uint32_t)(pixel565 & 0x1Fu);
    uint32_t red8 = (red5 << 3u) | (red5 >> 2u);
    uint32_t green8 = (green6 << 2u) | (green6 >> 4u);
    uint32_t blue8 = (blue5 << 3u) | (blue5 >> 2u);
    return 0xFF000000u | (red8 << 16u) | (green8 << 8u) | blue8;
}

void render_blit_rgb565_scaled_clipped(const struct framebuffer_surface *surface,
                                       const uint16_t *source_pixels, uint32_t source_width,
                                       uint32_t source_height, float dest_x, float dest_y,
                                       float dest_width, float dest_height,
                                       const struct clip_rect *clip, float alpha) {
    if (surface == NULL || surface->pixels == NULL || source_pixels == NULL ||
        source_width == 0u || source_height == 0u || dest_width <= 0.0f || dest_height <= 0.0f ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    struct clip_rect bounds = normalize_clip_rect(clip);
    int32_t min_x = (int32_t)ceilf(dest_x - 0.5f);
    int32_t max_x = (int32_t)floorf(dest_x + dest_width - 0.5f);
    int32_t min_y = (int32_t)ceilf(dest_y - 0.5f);
    int32_t max_y = (int32_t)floorf(dest_y + dest_height - 0.5f);
    if (min_x < bounds.left_x) {
        min_x = bounds.left_x;
    }
    if (max_x > bounds.right_x) {
        max_x = bounds.right_x;
    }
    if (min_y < bounds.top_y) {
        min_y = bounds.top_y;
    }
    if (max_y > bounds.bottom_y) {
        max_y = bounds.bottom_y;
    }
    if (min_x > max_x || min_y > max_y) {
        return;
    }

    uint8_t alpha_u8 = alpha_to_u8(alpha);
    int32_t step_u_fp = (int32_t)(((double)source_width * 65536.0) / (double)dest_width);
    int32_t step_v_fp = (int32_t)(((double)source_height * 65536.0) / (double)dest_height);
    int32_t start_u_fp =
        (int32_t)((((double)min_x + 0.5 - (double)dest_x) * (double)source_width * 65536.0) /
                  (double)dest_width);
    int32_t v_fp =
        (int32_t)((((double)min_y + 0.5 - (double)dest_y) * (double)source_height * 65536.0) /
                  (double)dest_height);
    uint32_t physical_start_x = render_map_coordinate_180((uint32_t)min_x);

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        uint32_t src_y = (v_fp < 0) ? 0u : (uint32_t)(v_fp >> 16);
        if (src_y >= source_height) {
            src_y = source_height - 1u;
        }
        const uint16_t *src_row = source_pixels + (size_t)src_y * source_width;
        uint32_t physical_y = render_map_coordinate_180((uint32_t)pixel_y);
        uint32_t *dst_ptr = surface->pixels + physical_y * surface->stride_pixels + physical_start_x;
        int32_t u_fp = start_u_fp;

        if (alpha_u8 == 255u) {
            for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
                uint32_t src_x = (u_fp < 0) ? 0u : (uint32_t)(u_fp >> 16);
                if (src_x >= source_width) {
                    src_x = source_width - 1u;
                }
                *dst_ptr-- = rgb565_to_xrgb8888(src_row[src_x]);
                u_fp += step_u_fp;
            }
        } else {
            uint32_t inv_alpha = 255u - (uint32_t)alpha_u8;
            for (int32_t pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
                uint32_t src_x = (u_fp < 0) ? 0u : (uint32_t)(u_fp >> 16);
                if (src_x >= source_width) {
                    src_x = source_width - 1u;
                }
                uint32_t src_color = rgb565_to_xrgb8888(src_row[src_x]);
                uint32_t dst_color = *dst_ptr;
                uint32_t out_r = div_255_fast(((src_color >> 16u) & 0xFFu) * alpha_u8 +
                                              ((dst_color >> 16u) & 0xFFu) * inv_alpha);
                uint32_t out_g = div_255_fast(((src_color >> 8u) & 0xFFu) * alpha_u8 +
                                              ((dst_color >> 8u) & 0xFFu) * inv_alpha);
                uint32_t out_b =
                    div_255_fast((src_color & 0xFFu) * alpha_u8 + (dst_color & 0xFFu) * inv_alpha);
                *dst_ptr-- = 0xFF000000u | (out_r << 16u) | (out_g << 8u) | out_b;
                u_fp += step_u_fp;
            }
        }
        v_fp += step_v_fp;
    }
}

void render_blit_rgb565_circle_rotated_shaded(const struct framebuffer_surface *surface,
                                              const uint16_t *source_pixels,
                                              uint32_t source_width, uint32_t source_height,
                                              float center_x, float center_y, float radius,
                                              float angle_radians, float alpha,
                                              uint32_t shade_256, uint32_t tint_xrgb) {
    if (surface == NULL || surface->pixels == NULL || source_pixels == NULL ||
        source_width == 0u || source_height == 0u || radius <= 0.5f ||
        alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float outer_reach = radius + 0.5f;
    float outer_sq = outer_reach * outer_reach;
    float inner_reach = radius - 0.5f;
    float inner_sq = inner_reach > 0.0f ? (inner_reach * inner_reach) : 0.0f;

    int32_t min_y = clamp_pixel_y(center_y - outer_reach);
    int32_t max_y = clamp_pixel_y(center_y + outer_reach);
    if (min_y > max_y) {
        return;
    }

    float inv_diameter = 1.0f / (2.0f * radius);
    float cos_a = cosf(angle_radians);
    float sin_a = sinf(angle_radians);
    float scale_x = inv_diameter * (float)source_width * 65536.0f;
    float scale_y = inv_diameter * (float)source_height * 65536.0f;
    int32_t du_dx_fp = (int32_t)(cos_a * scale_x);
    int32_t dv_dx_fp = (int32_t)(-sin_a * scale_y);
    float half_w_fp = (float)source_width * 0.5f * 65536.0f;
    float half_h_fp = (float)source_height * 0.5f * 65536.0f;
    uint32_t alpha_256 = (uint32_t)(render_clamp_f32(alpha, 0.0f, 1.0f) * 256.0f + 0.5f);
    bool opaque = (alpha_256 >= 255u);
    if (shade_256 > 256u) {
        shade_256 = 256u;
    }
    bool has_shade = (shade_256 < 256u);
    uint32_t inv_shade = 256u - shade_256;
    uint32_t tint_rb = (tint_xrgb & 0x00FF00FFu) * inv_shade;
    uint32_t tint_g = (tint_xrgb & 0x0000FF00u) * inv_shade;
    int32_t max_sx = (int32_t)source_width - 1;
    int32_t max_sy = (int32_t)source_height - 1;

    for (int32_t pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        float offset_y = (float)pixel_y + 0.5f - center_y;
        float offset_y_sq = offset_y * offset_y;
        if (offset_y_sq >= outer_sq) {
            continue;
        }
        float half_outer = sqrtf(outer_sq - offset_y_sq);
        int32_t row_start_x = clamp_pixel_x(center_x - half_outer);
        int32_t row_end_x = clamp_pixel_x(center_x + half_outer);
        if (row_start_x > row_end_x) {
            continue;
        }

        int32_t inner_start_x = row_end_x + 1;
        int32_t inner_end_x = row_start_x - 1;
        if (offset_y_sq < inner_sq) {
            float half_inner = sqrtf(inner_sq - offset_y_sq);
            inner_start_x = (int32_t)ceilf(center_x - half_inner - 0.5f);
            inner_end_x = (int32_t)floorf(center_x + half_inner - 0.5f);
            if (inner_start_x < row_start_x) {
                inner_start_x = row_start_x;
            }
            if (inner_end_x > row_end_x) {
                inner_end_x = row_end_x;
            }
        }

        float dx_start = (float)row_start_x + 0.5f - center_x;
        int32_t u_fp = (int32_t)((dx_start * cos_a + offset_y * sin_a) * scale_x + half_w_fp);
        int32_t v_fp = (int32_t)((-dx_start * sin_a + offset_y * cos_a) * scale_y + half_h_fp);

        uint32_t mapped_y = SCREEN_COORDINATE_MAX - (uint32_t)pixel_y;
        uint32_t *dest_ptr = surface->pixels + mapped_y * surface->stride_pixels +
                             (SCREEN_COORDINATE_MAX - (uint32_t)row_start_x);

        for (int32_t pixel_x = row_start_x; pixel_x <= row_end_x; pixel_x++) {
            int32_t sx = u_fp >> 16;
            int32_t sy = v_fp >> 16;
            if (sx < 0) {
                sx = 0;
            } else if (sx > max_sx) {
                sx = max_sx;
            }
            if (sy < 0) {
                sy = 0;
            } else if (sy > max_sy) {
                sy = max_sy;
            }
            uint16_t raw565 = source_pixels[(uint32_t)sy * source_width + (uint32_t)sx];
            uint32_t src_color = rgb565_to_xrgb8888(raw565);
            if (has_shade) {
                uint32_t rb = (((src_color & 0x00FF00FFu) * shade_256 + tint_rb) >> 8u) &
                              0x00FF00FFu;
                uint32_t g = (((src_color & 0x0000FF00u) * shade_256 + tint_g) >> 8u) &
                             0x0000FF00u;
                src_color = 0xFF000000u | rb | g;
            }

            if (pixel_x >= inner_start_x && pixel_x <= inner_end_x) {
                if (opaque) {
                    *dest_ptr = src_color;
                } else {
                    uint32_t dst_color = *dest_ptr;
                    uint32_t inv_alpha = 256u - alpha_256;
                    uint32_t rb = (((src_color & 0x00FF00FFu) * alpha_256 +
                                    (dst_color & 0x00FF00FFu) * inv_alpha) >>
                                   8u) &
                                  0x00FF00FFu;
                    uint32_t g = (((src_color & 0x0000FF00u) * alpha_256 +
                                   (dst_color & 0x0000FF00u) * inv_alpha) >>
                                  8u) &
                                 0x0000FF00u;
                    *dest_ptr = 0xFF000000u | rb | g;
                }
            } else {
                float offset_x = (float)pixel_x + 0.5f - center_x;
                float dist_sq = offset_x * offset_x + offset_y_sq;
                if (dist_sq < outer_sq) {
                    float coverage = render_clamp_f32(outer_reach - sqrtf(dist_sq), 0.0f, 1.0f);
                    uint32_t cov_256 = (uint32_t)(alpha * coverage * 256.0f + 0.5f);
                    if (cov_256 > 0u) {
                        uint32_t dst_color = *dest_ptr;
                        uint32_t inv_alpha = 256u - cov_256;
                        uint32_t rb = (((src_color & 0x00FF00FFu) * cov_256 +
                                        (dst_color & 0x00FF00FFu) * inv_alpha) >>
                                       8u) &
                                      0x00FF00FFu;
                        uint32_t g = (((src_color & 0x0000FF00u) * cov_256 +
                                       (dst_color & 0x0000FF00u) * inv_alpha) >>
                                      8u) &
                                     0x0000FF00u;
                        *dest_ptr = 0xFF000000u | rb | g;
                    }
                }
            }
            dest_ptr--;
            u_fp += du_dx_fp;
            v_fp += dv_dx_fp;
        }
    }
}

void render_blit_rgb565_circle_rotated(const struct framebuffer_surface *surface,
                                       const uint16_t *source_pixels, uint32_t source_width,
                                       uint32_t source_height, float center_x, float center_y,
                                       float radius, float angle_radians, float alpha) {
    render_blit_rgb565_circle_rotated_shaded(surface, source_pixels, source_width, source_height,
                                             center_x, center_y, radius, angle_radians, alpha,
                                             256u, 0u);
}
