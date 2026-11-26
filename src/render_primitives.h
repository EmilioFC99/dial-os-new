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

float render_clamp_f32(float value, float minimum, float maximum);
float render_normalize_range(float value, float range_start, float range_end);
float render_lerp_f32(float start_value, float end_value, float factor);
float render_ease_out_cubic(float progress);
float render_ease_out_expo(float progress);
float render_ease_in_out_cubic(float progress);
struct point_3d render_rotate_point_3d(float point_x, float point_y, float point_z, float angle_x,
                                      float angle_y, float angle_z);

void render_fill_canvas(const struct framebuffer_surface *surface, uint32_t canvas_color);
void render_blend_pixel(const struct framebuffer_surface *surface, int32_t logical_x,
                        int32_t logical_y, uint32_t color, uint8_t alpha);

void render_draw_line_aa(const struct framebuffer_surface *surface, float start_x, float start_y,
                         float end_x, float end_y, float line_width, uint32_t color, float alpha);
void render_draw_arc_aa(const struct framebuffer_surface *surface, float center_x, float center_y,
                        float radius, float line_width, float start_angle, float end_angle,
                        uint32_t color, float alpha);
void render_draw_filled_circle_aa(const struct framebuffer_surface *surface, float center_x,
                                  float center_y, float radius, uint32_t color, float alpha);
void render_draw_filled_rect_alpha(const struct framebuffer_surface *surface, float left_x,
                                   float top_y, float width, float height, uint32_t color,
                                   float alpha);
void render_draw_stroked_rect_alpha(const struct framebuffer_surface *surface, float left_x,
                                    float top_y, float width, float height, float line_width,
                                    uint32_t color, float alpha);
void render_draw_filled_convex_polygon_aa(const struct framebuffer_surface *surface,
                                          const struct point_2d *vertices, uint32_t vertex_count,
                                          uint32_t color, float alpha);
void render_draw_chamfer_plate_filled_aa(const struct framebuffer_surface *surface, float left_x,
                                         float top_y, float width, float height, float chamfer,
                                         uint32_t color, float alpha);
void render_draw_chamfer_plate_stroked_aa(const struct framebuffer_surface *surface, float left_x,
                                          float top_y, float width, float height, float chamfer,
                                          float line_width, uint32_t color, float alpha);

void render_draw_text_alpha(const struct framebuffer_surface *surface, enum font_style_id style_id,
                            const char *text, float anchor_x, float anchor_y,
                            enum text_align_mode align, enum text_baseline_mode baseline,
                            uint32_t color, float alpha);

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
