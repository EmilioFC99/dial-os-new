#ifndef DIALOS_RENDER_PRIMITIVES_H
#define DIALOS_RENDER_PRIMITIVES_H

#include <stdbool.h>
#include <stdint.h>

#include "font_atlas.h"
#include "render.h"
#include "theme.h"

#define FULL_CIRCLE_RADIANS 6.28318530718f
#define HALF_CIRCLE_RADIANS 3.14159265359f
#define TOP_ANGLE_RADIANS (-1.57079632679f)
#define ALPHA_VISIBILITY_THRESHOLD 0.005f
#define PERSPECTIVE_FOCAL_PX 410.0f
#define MAX_FAST_POLYGON_VERTICES 24u

enum text_align_mode {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER = 1,
    TEXT_ALIGN_RIGHT = 2
};

enum text_baseline_mode {
    TEXT_BASELINE_MIDDLE = 0,
    TEXT_BASELINE_TOP = 1
};

struct point_2d {
    float x;
    float y;
};

struct point_3d {
    float x;
    float y;
    float z;
};

struct rotation_matrix_3x3 {
    float m00;
    float m01;
    float m02;
    float m10;
    float m11;
    float m12;
    float m20;
    float m21;
    float m22;
};

struct segmented_ring_params {
    float center_x;
    float center_y;
    float radius;
    float start_angle_offset;
    float duty_ratio;
    uint32_t segment_count;
    uint32_t filled_count;
    uint32_t major_stride;
    bool alternating_colors;
    float filled_width;
    float leading_width;
    float unfilled_width;
    uint32_t highlight_color;
    uint32_t primary_color;
    uint32_t unfilled_color;
    float filled_alpha;
    float unfilled_alpha;
};

float render_clamp_f32(float value, float minimum, float maximum);
float render_normalize_range(float value, float range_start, float range_end);
float render_lerp_f32(float start_value, float end_value, float factor);
float render_ease_out_cubic(float progress);
float render_ease_out_expo(float progress);
float render_ease_in_out_cubic(float progress);
struct rotation_matrix_3x3 render_build_rotation_matrix_3d(float angle_x, float angle_y,
                                                           float angle_z);
struct point_3d render_apply_rotation_matrix_3d(const struct rotation_matrix_3x3 *matrix,
                                                float point_x, float point_y, float point_z);
struct point_3d render_project_point_3d(float point_x, float point_y, float point_z,
                                        float center_x, float center_y);

void render_fill_canvas(const struct framebuffer_surface *surface, uint32_t canvas_color);
void render_blend_pixel(const struct framebuffer_surface *surface, int32_t logical_x,
                        int32_t logical_y, uint32_t color, uint8_t alpha);

void render_draw_line_aa(const struct framebuffer_surface *surface, float start_x, float start_y,
                         float end_x, float end_y, float line_width, uint32_t color, float alpha);
void render_draw_line_fast(const struct framebuffer_surface *surface, float start_x, float start_y,
                           float end_x, float end_y, uint32_t color, float alpha);
void render_draw_arc_aa(const struct framebuffer_surface *surface, float center_x, float center_y,
                        float radius, float line_width, float start_angle, float end_angle,
                        uint32_t color, float alpha);
void render_draw_segmented_ring_aa(const struct framebuffer_surface *surface,
                                   const struct segmented_ring_params *params);
void render_draw_filled_circle_aa(const struct framebuffer_surface *surface, float center_x,
                                  float center_y, float radius, uint32_t color, float alpha);
void render_draw_filled_ellipse_aa(const struct framebuffer_surface *surface, float center_x,
                                   float center_y, float radius_x, float radius_y,
                                   float angle_radians, uint32_t color, float alpha);
void render_draw_stroked_ellipse_aa(const struct framebuffer_surface *surface, float center_x,
                                    float center_y, float radius_x, float radius_y,
                                    float angle_radians, float line_width, uint32_t color,
                                    float alpha);
void render_draw_filled_rect_alpha(const struct framebuffer_surface *surface, float left_x,
                                   float top_y, float width, float height, uint32_t color,
                                   float alpha);
void render_draw_stroked_rect_alpha(const struct framebuffer_surface *surface, float left_x,
                                    float top_y, float width, float height, float line_width,
                                    uint32_t color, float alpha);
void render_draw_filled_convex_polygon_aa(const struct framebuffer_surface *surface,
                                          const struct point_2d *vertices, uint32_t vertex_count,
                                          uint32_t color, float alpha);
void render_draw_filled_convex_polygon_fast(const struct framebuffer_surface *surface,
                                            const struct point_2d *vertices, uint32_t vertex_count,
                                            uint32_t color, float alpha);
uint32_t render_clip_polygon_to_convex_quad(const struct point_2d *subject_vertices,
                                            uint32_t subject_count,
                                            const struct point_2d *clip_quad_vertices,
                                            struct point_2d *clipped_out);
void render_draw_filled_convex_polygon_edged_aa(const struct framebuffer_surface *surface,
                                                const struct point_2d *vertices,
                                                uint32_t vertex_count, uint32_t fill_color,
                                                uint32_t edge_color, float edge_width,
                                                float alpha);
void render_draw_projected_3d_box(const struct framebuffer_surface *surface,
                                  const struct rotation_matrix_3x3 *matrix, float offset_x,
                                  float offset_y, float offset_z, float half_x, float half_y,
                                  float half_z, float screen_center_x, float screen_center_y,
                                  const struct cube_face_palette *face_palette, uint32_t edge_color,
                                  float alpha);
void render_draw_chamfer_plate_filled_aa(const struct framebuffer_surface *surface, float left_x,
                                         float top_y, float width, float height, float chamfer,
                                         uint32_t color, float alpha);
void render_draw_chamfer_plate_stroked_aa(const struct framebuffer_surface *surface, float left_x,
                                          float top_y, float width, float height, float chamfer,
                                          float line_width, uint32_t color, float alpha);
void render_draw_quad_corner_brackets(const struct framebuffer_surface *surface,
                                      const struct point_2d *quad_vertices, float arm_ratio,
                                      float line_width, uint32_t color, float alpha);
void render_draw_rect_corner_brackets(const struct framebuffer_surface *surface, float left_x,
                                      float top_y, float width, float height, float arm_length,
                                      float line_width, uint32_t color, float alpha);

void render_draw_text_alpha(const struct framebuffer_surface *surface, enum font_style_id style_id,
                            const char *text, float anchor_x, float anchor_y,
                            enum text_align_mode align, enum text_baseline_mode baseline,
                            uint32_t color, float alpha);
void render_draw_text_clipped_alpha(const struct framebuffer_surface *surface,
                                    enum font_style_id style_id, const char *text, float anchor_x,
                                    float anchor_y, enum text_align_mode align,
                                    enum text_baseline_mode baseline, int32_t clip_left_x,
                                    int32_t clip_right_x, uint32_t color, float alpha);
void render_draw_marquee_text(const struct framebuffer_surface *surface, enum font_style_id style_id,
                              const char *text, float center_x, float center_y, float max_width,
                              float elapsed_seconds, uint32_t color, float alpha);

void render_draw_crosshair_mark(const struct framebuffer_surface *surface, float center_x,
                                float center_y, float half_length, float line_width,
                                uint32_t color, float alpha);
void render_draw_diagonal_mark(const struct framebuffer_surface *surface, float center_x,
                               float center_y, float half_span, float line_width, uint32_t color,
                               float alpha);
void render_draw_perimeter_ticks(const struct framebuffer_surface *surface, uint32_t tick_count,
                                 float inner_radius, float outer_radius, float visible_ratio,
                                 const struct appearance_palette *palette, float base_alpha);
void render_draw_dialos_emblem(const struct framebuffer_surface *surface, float center_x,
                               float center_y, float radius,
                               const struct appearance_palette *palette, float alpha,
                               float detent_pop);
void render_draw_hazard_stripe_band(const struct framebuffer_surface *surface, float origin_x,
                                    float origin_y, float band_width, float band_height,
                                    float phase_offset, uint32_t fill_color);

#endif
