#include "render.h"

#include "constants.h"
#include "font_atlas.h"
#include "render_primitives.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define RIM_SEGMENT_COUNT 48u
#define MATRIX_GRID_SIZE 5
#define MATRIX_CELL_SIZE_PX 18.0f
#define MATRIX_CELL_GAP_PX 5.0f
#define BUFFER_EXIT_START_PROGRESS 0.92f
#define BUFFER_EXIT_END_PROGRESS 0.995f
#define SUBCUBE_TOTAL_COUNT 27u
#define CUBE_FACE_COUNT 6u

struct subcube_spec {
    int8_t grid_x;
    int8_t grid_y;
    int8_t grid_z;
    int8_t diagonal_layer;
    uint8_t fill_order;
};

enum subcube_visual_state {
    SUBCUBE_WIRE = 0,
    SUBCUBE_HIGHLIGHT = 1,
    SUBCUBE_INK = 2
};

struct projected_subcube {
    struct point_3d rotated_center;
    enum subcube_visual_state visual_state;
    float pop_scale;
};

static const float CUBE_LOCAL_VERTICES[8][3] = {
    {-1.0f, -1.0f, -1.0f},
    {1.0f, -1.0f, -1.0f},
    {1.0f, 1.0f, -1.0f},
    {-1.0f, 1.0f, -1.0f},
    {-1.0f, -1.0f, 1.0f},
    {1.0f, -1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f},
    {-1.0f, 1.0f, 1.0f},
};

static const uint8_t CUBE_FACE_INDICES[CUBE_FACE_COUNT][4] = {
    {4u, 5u, 6u, 7u},
    {1u, 0u, 3u, 2u},
    {5u, 1u, 2u, 6u},
    {0u, 4u, 7u, 3u},
    {0u, 1u, 5u, 4u},
    {7u, 6u, 2u, 3u},
};

static const float CUBE_FACE_NORMALS[CUBE_FACE_COUNT][3] = {
    {0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -1.0f},
    {1.0f, 0.0f, 0.0f},
    {-1.0f, 0.0f, 0.0f},
    {0.0f, -1.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
};

/* 5x5 Wake Matrix pattern: 1 = signal highlight square, 2 = structural disc cell */
static const uint8_t WAKE_MATRIX_PATTERN[MATRIX_GRID_SIZE][MATRIX_GRID_SIZE] = {
    {0u, 2u, 2u, 2u, 0u},
    {2u, 0u, 0u, 1u, 2u},
    {2u, 0u, 1u, 0u, 2u},
    {2u, 0u, 0u, 0u, 2u},
    {0u, 2u, 2u, 2u, 0u},
};

static struct subcube_spec LATTICE_SUBCUBE_OFFSETS[SUBCUBE_TOTAL_COUNT];
static bool lattice_offsets_initialized = false;

static void ensure_lattice_offsets_initialized(void) {
    if (lattice_offsets_initialized) {
        return;
    }

    uint32_t count = 0u;
    for (int8_t grid_z = -1; grid_z <= 1; grid_z++) {
        for (int8_t grid_y = -1; grid_y <= 1; grid_y++) {
            for (int8_t grid_x = -1; grid_x <= 1; grid_x++) {
                int8_t diagonal_layer = (int8_t)((grid_x + 1) + (1 - grid_y) + (grid_z + 1));
                LATTICE_SUBCUBE_OFFSETS[count++] = (struct subcube_spec){
                    .grid_x = grid_x,
                    .grid_y = grid_y,
                    .grid_z = grid_z,
                    .diagonal_layer = diagonal_layer,
                    .fill_order = 0u,
                };
            }
        }
    }

    for (uint32_t left = 0u; left < SUBCUBE_TOTAL_COUNT; left++) {
        for (uint32_t right = left + 1u; right < SUBCUBE_TOTAL_COUNT; right++) {
            const struct subcube_spec first = LATTICE_SUBCUBE_OFFSETS[left];
            const struct subcube_spec second = LATTICE_SUBCUBE_OFFSETS[right];
            bool should_swap = false;
            if (first.diagonal_layer != second.diagonal_layer) {
                should_swap = second.diagonal_layer < first.diagonal_layer;
            } else if (first.grid_y != second.grid_y) {
                should_swap = second.grid_y > first.grid_y;
            } else if (first.grid_x != second.grid_x) {
                should_swap = second.grid_x < first.grid_x;
            } else {
                should_swap = second.grid_z < first.grid_z;
            }
            if (should_swap) {
                LATTICE_SUBCUBE_OFFSETS[left] = second;
                LATTICE_SUBCUBE_OFFSETS[right] = first;
            }
        }
    }

    for (uint32_t order_index = 0u; order_index < SUBCUBE_TOTAL_COUNT; order_index++) {
        LATTICE_SUBCUBE_OFFSETS[order_index].fill_order = (uint8_t)order_index;
    }
    lattice_offsets_initialized = true;
}

static bool surface_is_valid(const struct framebuffer_surface *surface) {
    return surface != NULL && surface->pixels != NULL && surface->stride_pixels >= SCREEN_WIDTH;
}

uint32_t render_map_coordinate_180(uint32_t logical_coordinate) {
    if (logical_coordinate > SCREEN_COORDINATE_MAX) {
        return 0u;
    }
    return SCREEN_COORDINATE_MAX - logical_coordinate;
}

bool render_is_inside_bezel_circle(int32_t logical_x, int32_t logical_y) {
    int32_t delta_x = logical_x - SCREEN_CENTER_X;
    int32_t delta_y = logical_y - SCREEN_CENTER_Y;
    return (delta_x * delta_x + delta_y * delta_y) <=
           (DISPLAY_BEZEL_RADIUS * DISPLAY_BEZEL_RADIUS);
}

bool render_is_inside_safe_circle(int32_t logical_x, int32_t logical_y) {
    int32_t delta_x = logical_x - SCREEN_CENTER_X;
    int32_t delta_y = logical_y - SCREEN_CENTER_Y;
    return (delta_x * delta_x + delta_y * delta_y) <= (SAFE_CIRCLE_RADIUS * SAFE_CIRCLE_RADIUS);
}

static uint32_t resolve_face_tone(struct point_3d rotated_normal,
                                  const struct cube_face_palette *face_palette) {
    if (rotated_normal.y < -0.35f) {
        return face_palette->top;
    }
    if (rotated_normal.x >= 0.0f) {
        return face_palette->right;
    }
    return face_palette->left;
}

static void draw_rotating_matrix_cube(const struct framebuffer_surface *surface, float center_x,
                                      float center_y, float cube_scale, float fill_progress,
                                      float elapsed_seconds,
                                      const struct appearance_palette *palette, float alpha) {
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD || cube_scale <= 0.01f) {
        return;
    }

    ensure_lattice_offsets_initialized();

    float angle_y = elapsed_seconds * 1.15f + 0.65f;
    float angle_x = 0.56f + sinf(elapsed_seconds * 0.85f) * 0.16f;
    float angle_z = 0.10f;
    float cell_pitch = 28.0f * cube_scale;
    float half_cube_size = 10.2f * cube_scale;
    float clamped_fill = render_clamp_f32(fill_progress, 0.0f, 1.0f);
    float fill_cursor = clamped_fill * (float)SUBCUBE_TOTAL_COUNT;
    int32_t active_layer_threshold = (int32_t)floorf(clamped_fill * 7.0f);
    if (active_layer_threshold > 6) {
        active_layer_threshold = 6;
    }

    struct projected_subcube sorted_subcubes[SUBCUBE_TOTAL_COUNT];
    for (uint32_t cube_index = 0u; cube_index < SUBCUBE_TOTAL_COUNT; cube_index++) {
        const struct subcube_spec spec = LATTICE_SUBCUBE_OFFSETS[cube_index];
        struct point_3d rotated_center = render_rotate_point_3d(
            (float)spec.grid_x * cell_pitch, (float)spec.grid_y * cell_pitch,
            (float)spec.grid_z * cell_pitch, angle_x, angle_y, angle_z);

        float activation = render_clamp_f32(fill_cursor - (float)spec.fill_order, 0.0f, 1.0f);
        bool is_filled = activation > 0.0f;
        bool is_leading_wave =
            is_filled && ((int32_t)spec.diagonal_layer == active_layer_threshold ||
                          (float)spec.fill_order >= (fill_cursor - 3.0f));
        enum subcube_visual_state visual_state =
            !is_filled ? SUBCUBE_WIRE : (is_leading_wave ? SUBCUBE_HIGHLIGHT : SUBCUBE_INK);
        float pop_scale =
            is_filled ? render_lerp_f32(0.45f, 1.0f, render_ease_out_expo(activation)) : 1.0f;

        sorted_subcubes[cube_index] = (struct projected_subcube){
            .rotated_center = rotated_center,
            .visual_state = visual_state,
            .pop_scale = pop_scale,
        };
    }

    for (uint32_t left = 0u; left < SUBCUBE_TOTAL_COUNT; left++) {
        for (uint32_t right = left + 1u; right < SUBCUBE_TOTAL_COUNT; right++) {
            if (sorted_subcubes[right].rotated_center.z < sorted_subcubes[left].rotated_center.z) {
                struct projected_subcube swap_cube = sorted_subcubes[left];
                sorted_subcubes[left] = sorted_subcubes[right];
                sorted_subcubes[right] = swap_cube;
            }
        }
    }

    struct point_3d rotated_normals[CUBE_FACE_COUNT];
    for (uint32_t face_index = 0u; face_index < CUBE_FACE_COUNT; face_index++) {
        rotated_normals[face_index] = render_rotate_point_3d(
            CUBE_FACE_NORMALS[face_index][0], CUBE_FACE_NORMALS[face_index][1],
            CUBE_FACE_NORMALS[face_index][2], angle_x, angle_y, angle_z);
    }

    struct point_3d rotated_corners[8];
    for (uint32_t vertex_index = 0u; vertex_index < 8u; vertex_index++) {
        rotated_corners[vertex_index] = render_rotate_point_3d(
            CUBE_LOCAL_VERTICES[vertex_index][0] * half_cube_size,
            CUBE_LOCAL_VERTICES[vertex_index][1] * half_cube_size,
            CUBE_LOCAL_VERTICES[vertex_index][2] * half_cube_size, angle_x, angle_y, angle_z);
    }

    float wire_alpha = ((float)palette->cube_wireframe_alpha / 255.0f) * alpha;

    for (uint32_t sorted_index = 0u; sorted_index < SUBCUBE_TOTAL_COUNT; sorted_index++) {
        const struct projected_subcube subcube = sorted_subcubes[sorted_index];
        float origin_x = center_x + subcube.rotated_center.x;
        float origin_y = center_y + subcube.rotated_center.y;
        bool is_lit = (subcube.visual_state != SUBCUBE_WIRE);
        float scale_factor = subcube.pop_scale;
        const struct cube_face_palette *face_palette =
            (subcube.visual_state == SUBCUBE_HIGHLIGHT) ? &palette->cube_highlight_faces
                                                        : &palette->cube_ink_faces;

        for (uint32_t face_index = 0u; face_index < CUBE_FACE_COUNT; face_index++) {
            struct point_3d rotated_normal = rotated_normals[face_index];
            if (rotated_normal.z <= 0.01f) {
                continue;
            }

            struct point_2d face_vertices[4];
            for (uint32_t corner_index = 0u; corner_index < 4u; corner_index++) {
                struct point_3d corner_offset =
                    rotated_corners[CUBE_FACE_INDICES[face_index][corner_index]];
                face_vertices[corner_index].x = origin_x + corner_offset.x * scale_factor;
                face_vertices[corner_index].y = origin_y + corner_offset.y * scale_factor;
            }

            if (is_lit) {
                uint32_t face_color = resolve_face_tone(rotated_normal, face_palette);
                render_draw_filled_convex_polygon_aa(surface, face_vertices, 4u, face_color, alpha);
                for (uint32_t edge_index = 0u; edge_index < 4u; edge_index++) {
                    uint32_t next_index = (edge_index + 1u) % 4u;
                    render_draw_line_aa(surface, face_vertices[edge_index].x,
                                        face_vertices[edge_index].y, face_vertices[next_index].x,
                                        face_vertices[next_index].y, 1.0f,
                                        palette->cube_edge_stroke, alpha);
                }
            } else {
                for (uint32_t edge_index = 0u; edge_index < 4u; edge_index++) {
                    uint32_t next_index = (edge_index + 1u) % 4u;
                    render_draw_line_aa(surface, face_vertices[edge_index].x,
                                        face_vertices[edge_index].y, face_vertices[next_index].x,
                                        face_vertices[next_index].y, 0.85f, palette->cube_wireframe,
                                        wire_alpha);
                }
            }
        }
    }
}

static void render_act_wake_matrix(const struct framebuffer_surface *surface, float act_progress,
                                   const struct appearance_palette *palette) {
    float wake_slit_progress = render_normalize_range(act_progress, 0.0f, 0.16f);
    if (wake_slit_progress < 1.0f) {
        float slit_half_width =
            render_lerp_f32(8.0f, 146.0f, render_ease_out_expo(wake_slit_progress));
        float slit_alpha = 1.0f - render_normalize_range(act_progress, 0.10f, 0.18f);
        render_draw_line_aa(surface, (float)SCREEN_CENTER_X - slit_half_width,
                            (float)SCREEN_CENTER_Y, (float)SCREEN_CENTER_X + slit_half_width,
                            (float)SCREEN_CENTER_Y, 2.0f, palette->signal_highlight, slit_alpha);
    }

    float tick_reveal_progress =
        render_ease_out_cubic(render_normalize_range(act_progress, 0.08f, 0.72f));
    render_draw_perimeter_ticks(surface, RIM_SEGMENT_COUNT, (float)OUTER_TICK_RADIUS - 6.0f,
                                (float)OUTER_TICK_RADIUS, tick_reveal_progress, palette, 0.95f);

    static const float CORNER_ANGLES[4] = {
        -HALF_CIRCLE_RADIANS * 0.75f,
        -HALF_CIRCLE_RADIANS * 0.25f,
        HALF_CIRCLE_RADIANS * 0.25f,
        HALF_CIRCLE_RADIANS * 0.75f,
    };
    for (uint32_t corner_index = 0u; corner_index < 4u; corner_index++) {
        float mark_progress =
            render_normalize_range(act_progress, 0.10f + (float)corner_index * 0.06f,
                                   0.22f + (float)corner_index * 0.06f);
        float angle_radians = CORNER_ANGLES[corner_index];
        float mark_x = (float)SCREEN_CENTER_X + cosf(angle_radians) * (float)REGISTRATION_MARK_RADIUS;
        float mark_y = (float)SCREEN_CENTER_Y + sinf(angle_radians) * (float)REGISTRATION_MARK_RADIUS;
        render_draw_diagonal_mark(
            surface, mark_x, mark_y, 4.5f, 1.5f,
            mark_progress < 0.75f ? palette->signal_highlight : palette->ink_secondary,
            render_ease_out_cubic(mark_progress));
    }

    float collapse_progress =
        render_ease_in_out_cubic(render_normalize_range(act_progress, 0.80f, 1.0f));
    float matrix_scale = render_lerp_f32(1.0f, 0.26f, collapse_progress);
    float matrix_alpha = 1.0f - render_normalize_range(act_progress, 0.88f, 1.0f);

    float total_grid_span = (float)MATRIX_GRID_SIZE * MATRIX_CELL_SIZE_PX +
                            (float)(MATRIX_GRID_SIZE - 1) * MATRIX_CELL_GAP_PX;
    float matrix_top_offset = -total_grid_span * 0.5f;
    float matrix_center_x = (float)SCREEN_CENTER_X;
    float matrix_center_y = (float)SCREEN_CENTER_Y - 6.0f;

    float bracket_half_span = (total_grid_span * 0.5f + 12.0f) * matrix_scale;
    float bracket_arm = 10.0f * matrix_scale;
    float bracket_alpha =
        matrix_alpha * render_ease_out_cubic(render_normalize_range(act_progress, 0.12f, 0.30f));
    static const float BRACKET_SIGNS[4][2] = {
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, 1.0f},
    };
    for (uint32_t corner_index = 0u; corner_index < 4u; corner_index++) {
        float sign_x = BRACKET_SIGNS[corner_index][0];
        float sign_y = BRACKET_SIGNS[corner_index][1];
        float corner_x = matrix_center_x + sign_x * bracket_half_span;
        float corner_y = matrix_center_y + sign_y * bracket_half_span;
        render_draw_line_aa(surface, corner_x - sign_x * bracket_arm, corner_y, corner_x, corner_y,
                            1.5f, palette->hairline_strong, bracket_alpha);
        render_draw_line_aa(surface, corner_x, corner_y, corner_x, corner_y - sign_y * bracket_arm,
                            1.5f, palette->hairline_strong, bracket_alpha);
    }

    float outline_alpha =
        matrix_alpha * render_ease_out_cubic(render_normalize_range(act_progress, 0.12f, 0.28f));
    float scaled_cell = MATRIX_CELL_SIZE_PX * matrix_scale;

    for (int32_t row_index = 0; row_index < MATRIX_GRID_SIZE; row_index++) {
        for (int32_t column_index = 0; column_index < MATRIX_GRID_SIZE; column_index++) {
            float cell_x =
                matrix_center_x +
                (matrix_top_offset + (float)column_index * (MATRIX_CELL_SIZE_PX + MATRIX_CELL_GAP_PX)) *
                    matrix_scale;
            float cell_y =
                matrix_center_y +
                (matrix_top_offset + (float)row_index * (MATRIX_CELL_SIZE_PX + MATRIX_CELL_GAP_PX)) *
                    matrix_scale;

            render_draw_stroked_rect_alpha(surface, cell_x + 0.5f, cell_y + 0.5f,
                                           scaled_cell - 1.0f, scaled_cell - 1.0f, 1.0f,
                                           palette->hairline_strong, outline_alpha);

            float diagonal_order = ((float)row_index + (float)column_index * 1.15f) / 9.0f;
            float cell_activate_time = 0.16f + diagonal_order * 0.44f;
            float cell_flash_delta = act_progress - cell_activate_time;
            if (cell_flash_delta >= 0.0f) {
                uint8_t cell_role = WAKE_MATRIX_PATTERN[row_index][column_index];
                bool is_fresh_flash = cell_flash_delta < 0.08f;
                if (is_fresh_flash || cell_role == 1u) {
                    render_draw_filled_rect_alpha(surface, cell_x, cell_y, scaled_cell, scaled_cell,
                                                  palette->signal_highlight, matrix_alpha);
                } else if (cell_role == 2u) {
                    float inset = 2.0f * matrix_scale;
                    render_draw_filled_rect_alpha(surface, cell_x + inset, cell_y + inset,
                                                  scaled_cell - inset * 2.0f,
                                                  scaled_cell - inset * 2.0f, palette->ink_primary,
                                                  matrix_alpha * 0.92f);
                } else {
                    float inset = 6.0f * matrix_scale;
                    render_draw_filled_rect_alpha(surface, cell_x + inset, cell_y + inset,
                                                  scaled_cell - inset * 2.0f,
                                                  scaled_cell - inset * 2.0f,
                                                  palette->hairline_strong, matrix_alpha * 0.22f);
                }
            }
        }
    }

    float caption_alpha =
        render_ease_out_cubic(render_normalize_range(act_progress, 0.18f, 0.34f)) *
        (1.0f - render_normalize_range(act_progress, 0.84f, 0.98f));
    if (caption_alpha > 0.01f) {
        const struct bitmap_font *mono_font = font_atlas_get(FONT_STYLE_MONO_10);
        const char *caption_text = "MEM // CHECK";
        float caption_width = (float)font_atlas_measure_width(mono_font, caption_text);
        float cursor_width = 6.0f;
        float cursor_gap = 6.0f;
        float caption_origin_x =
            (float)SCREEN_CENTER_X - (caption_width + cursor_gap + cursor_width) * 0.5f;

        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, caption_text, caption_origin_x, 286.0f,
                               TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE, palette->ink_secondary,
                               caption_alpha);

        bool cursor_visible = (((int32_t)floorf(act_progress * 18.0f)) % 2) == 0;
        if (cursor_visible) {
            render_draw_filled_rect_alpha(surface, caption_origin_x + caption_width + cursor_gap,
                                          281.0f, cursor_width, 10.0f, palette->signal_highlight,
                                          caption_alpha);
        }
    }
}

static void render_act_buffer_sweep(const struct framebuffer_surface *surface, float act_progress,
                                    float elapsed_seconds, float sweep_progress,
                                    const char *status_label,
                                    const struct appearance_palette *palette) {
    float clamped_sweep = render_clamp_f32(sweep_progress, 0.0f, 1.0f);
    float entry_scale =
        render_lerp_f32(0.58f, 1.0f, render_ease_out_expo(render_normalize_range(act_progress, 0.0f, 0.14f)));
    float exit_progress = act_progress < clamped_sweep ? act_progress : clamped_sweep;
    float exit_fade = render_ease_in_out_cubic(
        render_normalize_range(exit_progress, BUFFER_EXIT_START_PROGRESS, BUFFER_EXIT_END_PROGRESS));
    float exit_alpha = 1.0f - exit_fade;
    if (exit_alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }

    float cube_scale = entry_scale * render_lerp_f32(1.0f, 0.86f, exit_fade);
    float cube_alpha =
        render_ease_out_cubic(render_normalize_range(act_progress, 0.0f, 0.12f)) * exit_alpha;

    uint32_t filled_segment_count = (uint32_t)lroundf(clamped_sweep * (float)RIM_SEGMENT_COUNT);
    float segment_pitch_angle = FULL_CIRCLE_RADIANS / (float)RIM_SEGMENT_COUNT;
    float segment_arc_span = segment_pitch_angle * 0.68f;

    for (uint32_t segment_index = 0u; segment_index < RIM_SEGMENT_COUNT; segment_index++) {
        float start_angle = TOP_ANGLE_RADIANS + (float)segment_index * segment_pitch_angle;
        float end_angle = start_angle + segment_arc_span;
        bool is_filled = segment_index < filled_segment_count;
        bool is_leading = (filled_segment_count > 0u && segment_index == filled_segment_count - 1u);

        if (is_filled) {
            bool is_highlight_band = (segment_index % 2u) == 0u;
            render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y,
                               (float)OUTER_TICK_RADIUS - 3.0f, is_leading ? 9.0f : 6.5f,
                               start_angle, end_angle,
                               is_highlight_band ? palette->signal_highlight : palette->ink_primary,
                               exit_alpha);
        } else {
            render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y,
                               (float)OUTER_TICK_RADIUS - 3.0f, 2.5f, start_angle, end_angle,
                               palette->hairline, 0.85f * exit_alpha);
        }
    }

    static const float CORNER_ANGLES[4] = {
        -HALF_CIRCLE_RADIANS * 0.75f,
        -HALF_CIRCLE_RADIANS * 0.25f,
        HALF_CIRCLE_RADIANS * 0.25f,
        HALF_CIRCLE_RADIANS * 0.75f,
    };
    static const float CORNER_THRESHOLDS[4] = {0.85f, 0.15f, 0.40f, 0.65f};

    for (uint32_t corner_index = 0u; corner_index < 4u; corner_index++) {
        float angle_radians = CORNER_ANGLES[corner_index];
        float mark_x = (float)SCREEN_CENTER_X + cosf(angle_radians) * (float)REGISTRATION_MARK_RADIUS;
        float mark_y = (float)SCREEN_CENTER_Y + sinf(angle_radians) * (float)REGISTRATION_MARK_RADIUS;
        bool is_reached = clamped_sweep >= CORNER_THRESHOLDS[corner_index];
        render_draw_diagonal_mark(surface, mark_x, mark_y, 4.5f, is_reached ? 1.8f : 1.3f,
                                  is_reached ? palette->signal_highlight : palette->hairline_strong,
                                  (is_reached ? 1.0f : 0.7f) * exit_alpha);
    }

    draw_rotating_matrix_cube(surface, (float)SCREEN_CENTER_X, 164.0f, cube_scale, clamped_sweep,
                              elapsed_seconds, palette, cube_alpha);

    const char *readout_text =
        (status_label != NULL && status_label[0] != '\0') ? status_label : "INIT // SYSTEM";
    float status_alpha =
        render_ease_out_cubic(render_normalize_range(act_progress, 0.05f, 0.16f)) * exit_alpha;
    if (status_alpha > 0.01f) {
        const struct bitmap_font *mono_font = font_atlas_get(FONT_STYLE_MONO_10);
        float status_width = (float)font_atlas_measure_width(mono_font, readout_text);
        float cursor_width = 6.0f;
        float cursor_gap = 6.0f;
        float status_origin_x =
            (float)SCREEN_CENTER_X - (status_width + cursor_gap + cursor_width) * 0.5f;

        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, readout_text, status_origin_x, 286.0f,
                               TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE, palette->ink_primary,
                               status_alpha);

        bool cursor_visible = (((int32_t)floorf(elapsed_seconds * 6.0f)) % 2) == 0;
        if (cursor_visible) {
            render_draw_filled_rect_alpha(surface, status_origin_x + status_width + cursor_gap,
                                          281.0f, cursor_width, 10.0f, palette->signal_highlight,
                                          status_alpha);
        }
    }
}

static void render_act_signal_lock(const struct framebuffer_surface *surface, float act_progress,
                                   float handoff_progress, const char *summary_line,
                                   const struct appearance_palette *palette) {
    float lock_settle = render_ease_out_expo(render_normalize_range(act_progress, 0.08f, 0.55f));
    float handoff_ease = render_ease_in_out_cubic(handoff_progress);

    float rim_radius = render_lerp_f32((float)OUTER_TICK_RADIUS, 198.0f, handoff_ease);
    float rim_alpha =
        (1.0f - handoff_ease) * render_ease_out_cubic(render_normalize_range(act_progress, 0.0f, 0.25f));

    if (rim_alpha > 0.01f) {
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, rim_radius, 1.0f,
                           0.0f, FULL_CIRCLE_RADIANS, palette->hairline, rim_alpha);

        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, rim_radius, 4.0f,
                           TOP_ANGLE_RADIANS - HALF_CIRCLE_RADIANS * 0.42f,
                           TOP_ANGLE_RADIANS + HALF_CIRCLE_RADIANS * 0.14f, palette->ink_primary,
                           rim_alpha);

        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, rim_radius, 4.0f,
                           HALF_CIRCLE_RADIANS * 0.16f, HALF_CIRCLE_RADIANS * 0.44f,
                           palette->signal_highlight, rim_alpha);

        static const float MARK_ANGLES[2] = {
            -HALF_CIRCLE_RADIANS * 0.22f,
            HALF_CIRCLE_RADIANS * 0.78f,
        };
        for (uint32_t mark_index = 0u; mark_index < 2u; mark_index++) {
            float angle_radians = MARK_ANGLES[mark_index];
            render_draw_diagonal_mark(
                surface, (float)SCREEN_CENTER_X + cosf(angle_radians) * (rim_radius - 14.0f),
                (float)SCREEN_CENTER_Y + sinf(angle_radians) * (rim_radius - 14.0f), 4.5f, 1.6f,
                palette->ink_secondary, rim_alpha);
        }
    }

    float emblem_y = render_lerp_f32(124.0f, (float)SCREEN_CENTER_Y, handoff_ease);
    float emblem_radius = render_lerp_f32(31.0f, 38.0f, handoff_ease);
    float emblem_alpha = render_ease_out_cubic(render_normalize_range(act_progress, 0.10f, 0.28f));
    float detent_pop = render_normalize_range(act_progress, 0.24f, 0.56f);

    render_draw_dialos_emblem(surface, (float)SCREEN_CENTER_X, emblem_y, emblem_radius, palette,
                              emblem_alpha, detent_pop);

    if (handoff_progress > 0.15f) {
        float halo_progress = render_normalize_range(handoff_progress, 0.15f, 1.0f);
        float halo_radius = render_lerp_f32(42.0f, 62.0f, render_ease_out_cubic(halo_progress));
        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y, halo_radius,
                           2.0f, 0.0f, FULL_CIRCLE_RADIANS, palette->signal_highlight,
                           (1.0f - halo_progress) * 0.65f);
    }

    float content_exit_alpha = 1.0f - render_normalize_range(handoff_progress, 0.0f, 0.55f);
    float content_drift_y = render_lerp_f32(0.0f, 18.0f, handoff_ease);
    float wordmark_reveal = render_normalize_range(act_progress, 0.12f, 0.30f);
    float content_alpha = render_ease_out_cubic(wordmark_reveal) * content_exit_alpha;

    if (content_exit_alpha > 0.01f && wordmark_reveal > 0.01f) {
        float wordmark_y = 196.0f + content_drift_y;
        const struct bitmap_font *display_font = font_atlas_get(FONT_STYLE_DISPLAY_32);
        float dial_word_width = (float)font_atlas_measure_width(display_font, "DIAL");
        float badge_gap = 8.0f;
        float badge_width = 42.0f;
        float badge_height = 29.0f;
        float total_lockup_width = dial_word_width + badge_gap + badge_width;
        float lockup_origin_x = (float)SCREEN_CENTER_X - total_lockup_width * 0.5f;

        render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_32, "DIAL", lockup_origin_x, wordmark_y,
                               TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE, palette->ink_primary,
                               content_alpha);

        float badge_x = lockup_origin_x + dial_word_width + badge_gap;
        float badge_y = wordmark_y - 15.0f;
        render_draw_filled_rect_alpha(surface, badge_x, badge_y, badge_width, badge_height,
                                      palette->signal_highlight, content_alpha);

        render_draw_text_alpha(surface, FONT_STYLE_MONO_18, "OS", badge_x + badge_width * 0.5f,
                               wordmark_y + 0.5f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               palette->signal_highlight_ink, content_alpha);

        float divider_half_width = render_lerp_f32(0.0f, 74.0f, lock_settle);
        float divider_y = 228.0f + content_drift_y;
        render_draw_line_aa(surface, (float)SCREEN_CENTER_X - divider_half_width, divider_y,
                            (float)SCREEN_CENTER_X + divider_half_width, divider_y, 1.0f,
                            palette->hairline_strong, content_alpha);

        if (lock_settle > 0.4f) {
            render_draw_crosshair_mark(surface, (float)SCREEN_CENTER_X - divider_half_width - 6.0f,
                                       divider_y, 3.0f, 1.0f, palette->ink_secondary,
                                       content_exit_alpha);
            render_draw_crosshair_mark(surface, (float)SCREEN_CENTER_X + divider_half_width + 6.0f,
                                       divider_y, 3.0f, 1.0f, palette->ink_secondary,
                                       content_exit_alpha);
        }

        float status_alpha =
            render_ease_out_cubic(render_normalize_range(act_progress, 0.30f, 0.56f)) *
            content_exit_alpha;
        const struct bitmap_font *welcome_font = font_atlas_get(FONT_STYLE_MONO_11);
        const char *welcome_label = "WELCOME";
        float welcome_width = (float)font_atlas_measure_width(welcome_font, welcome_label);
        float indicator_size = 7.0f;
        float indicator_gap = 7.0f;
        float status_origin_x =
            (float)SCREEN_CENTER_X - (indicator_size + indicator_gap + welcome_width) * 0.5f;

        render_draw_filled_rect_alpha(surface, status_origin_x, 248.5f + content_drift_y,
                                      indicator_size, indicator_size, palette->signal_highlight,
                                      status_alpha);

        render_draw_text_alpha(surface, FONT_STYLE_MONO_11, welcome_label,
                               status_origin_x + indicator_size + indicator_gap,
                               252.0f + content_drift_y, TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE,
                               palette->ink_primary, status_alpha);

        const char *resolved_summary =
            (summary_line != NULL && summary_line[0] != '\0')
                ? summary_line
                : "V1.0 \xC2\xB7 AUDIO OK \xC2\xB7 NO SD";
        render_draw_text_alpha(surface, FONT_STYLE_MONO_10, resolved_summary,
                               (float)SCREEN_CENTER_X, 276.0f + content_drift_y, TEXT_ALIGN_CENTER,
                               TEXT_BASELINE_MIDDLE, palette->ink_secondary, status_alpha);
    }

    if (act_progress < 0.32f) {
        float shutter_enter = render_normalize_range(act_progress, 0.0f, 0.14f);
        float shutter_exit = render_normalize_range(act_progress, 0.14f, 0.32f);
        float full_band_span = 280.0f;
        float left_edge_x = (float)SCREEN_CENTER_X - full_band_span * 0.5f +
                            render_ease_in_out_cubic(shutter_exit) * full_band_span;
        float right_edge_x = (float)SCREEN_CENTER_X - full_band_span * 0.5f +
                             render_ease_out_expo(shutter_enter) * full_band_span;
        float active_width = right_edge_x - left_edge_x;
        if (active_width > 1.0f) {
            render_draw_filled_rect_alpha(surface, left_edge_x, 166.0f, active_width, 56.0f,
                                          palette->signal_highlight, 1.0f);
        }
    }
}

static void render_warning_scenario(const struct framebuffer_surface *surface,
                                    float elapsed_seconds, float warning_elapsed_seconds,
                                    const struct appearance_palette *palette) {
    const uint32_t segment_count = 24u;
    float rotation_radians = elapsed_seconds * 0.45f;

    for (uint32_t segment_index = 0u; segment_index < segment_count; segment_index++) {
        float start_angle =
            rotation_radians + ((float)segment_index / (float)segment_count) * FULL_CIRCLE_RADIANS;
        float sweep_angle = (FULL_CIRCLE_RADIANS / (float)segment_count) * 0.56f;
        bool is_hot_segment = (segment_index % 6u) == 0u;

        render_draw_arc_aa(surface, (float)SCREEN_CENTER_X, (float)SCREEN_CENTER_Y,
                           (float)ORBITAL_ARC_RADIUS, is_hot_segment ? 5.0f : 2.5f, start_angle,
                           start_angle + sweep_angle,
                           is_hot_segment ? palette->signal_alert : palette->hairline_strong, 1.0f);
    }

    render_draw_hazard_stripe_band(surface, (float)SCREEN_CENTER_X - 68.0f, 72.0f, 136.0f, 12.0f,
                                   elapsed_seconds * 28.0f, palette->signal_alert);

    float plate_width = 196.0f;
    float plate_height = 32.0f;
    render_draw_filled_rect_alpha(surface, (float)SCREEN_CENTER_X - plate_width * 0.5f, 118.0f,
                                  plate_width, plate_height, palette->signal_alert, 1.0f);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_13, "AUDIO ENGINE SLOW", (float)SCREEN_CENTER_X,
                           134.5f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->signal_alert_ink, 1.0f);

    float safe_warning_seconds =
        (isfinite(warning_elapsed_seconds) && warning_elapsed_seconds > 0.0f)
            ? warning_elapsed_seconds
            : 0.0f;
    char timer_text[24];
    snprintf(timer_text, sizeof(timer_text), "%.1fs", (double)safe_warning_seconds);
    render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_40, timer_text, (float)SCREEN_CENTER_X,
                           192.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_primary,
                           1.0f);

    const char *bus_label = "POLLING /UI BUS";
    const struct bitmap_font *mono_10 = font_atlas_get(FONT_STYLE_MONO_10);
    float bus_label_width = (float)font_atlas_measure_width(mono_10, bus_label);
    float pulse_size = 7.0f;
    float pulse_gap = 7.0f;
    float bus_origin_x = (float)SCREEN_CENTER_X - (pulse_size + pulse_gap + bus_label_width) * 0.5f;
    float pulse_alpha = 0.35f + 0.65f * (0.5f + 0.5f * sinf(elapsed_seconds * 5.2f));

    render_draw_filled_rect_alpha(surface, bus_origin_x, 229.5f, pulse_size, pulse_size,
                                  palette->signal_highlight, pulse_alpha);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, bus_label,
                           bus_origin_x + pulse_size + pulse_gap, 233.0f, TEXT_ALIGN_LEFT,
                           TEXT_BASELINE_MIDDLE, palette->ink_secondary, 1.0f);

    float action_plate_width = 204.0f;
    float action_plate_height = 32.0f;
    float action_left_x = (float)SCREEN_CENTER_X - action_plate_width * 0.5f;
    float action_top_y = 262.0f;
    float action_center_y = action_top_y + action_plate_height * 0.5f;

    render_draw_chamfer_plate_filled_aa(surface, action_left_x, action_top_y, action_plate_width,
                                        action_plate_height, 7.0f, palette->surface, 1.0f);
    render_draw_chamfer_plate_stroked_aa(surface, action_left_x, action_top_y, action_plate_width,
                                         action_plate_height, 7.0f, 1.0f, palette->hairline_strong,
                                         1.0f);

    const char *action_label = "HOLD POWER TO SHUT DOWN";
    float action_label_width = (float)font_atlas_measure_width(mono_10, action_label);
    float action_square_size = 5.0f;
    float action_gap = 7.0f;
    float action_origin_x =
        (float)SCREEN_CENTER_X - (action_square_size + action_gap + action_label_width) * 0.5f;

    render_draw_filled_rect_alpha(surface, action_origin_x,
                                  action_center_y - action_square_size * 0.5f, action_square_size,
                                  action_square_size, palette->signal_highlight, 1.0f);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, action_label,
                           action_origin_x + action_square_size + action_gap,
                           action_center_y + 0.5f, TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE,
                           palette->ink_primary, 1.0f);
}

void render_draw_boot_frame(const struct framebuffer_surface *surface,
                            const struct boot_render_snapshot *snapshot,
                            enum appearance_mode mode) {
    if (!surface_is_valid(surface) || snapshot == NULL) {
        return;
    }

    const struct appearance_palette *palette = theme_get_palette(mode);
    render_fill_canvas(surface, palette->canvas);

    if (snapshot->phase == BOOT_PHASE_WARNING) {
        render_warning_scenario(surface, snapshot->elapsed_seconds,
                                snapshot->warning_elapsed_seconds, palette);
        return;
    }

    if (snapshot->phase == BOOT_PHASE_WAKE) {
        render_act_wake_matrix(surface, snapshot->act_progress, palette);
        return;
    }

    if (snapshot->phase == BOOT_PHASE_BUFFER) {
        render_act_buffer_sweep(surface, snapshot->act_progress, snapshot->elapsed_seconds,
                                snapshot->sweep_progress, snapshot->status_label, palette);
        return;
    }

    render_act_signal_lock(surface, snapshot->act_progress, snapshot->handoff_progress,
                           snapshot->summary_line, palette);
}

void render_draw_ready_frame(const struct framebuffer_surface *surface,
                             const struct ready_screen_status *status,
                             enum appearance_mode mode) {
    if (!surface_is_valid(surface) || status == NULL) {
        return;
    }

    const struct appearance_palette *palette = theme_get_palette(mode);
    render_fill_canvas(surface, palette->canvas);

    render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_18, "DialOS Ready", (float)SCREEN_CENTER_X,
                           112.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->signal_highlight, 1.0f);

    char line_buffer[40];
    if (status->battery_valid) {
        snprintf(line_buffer, sizeof(line_buffer), "BATTERY: %u%%",
                 (unsigned)status->battery_percent);
    } else {
        snprintf(line_buffer, sizeof(line_buffer), "BATTERY: --");
    }
    render_draw_text_alpha(surface, FONT_STYLE_MONO_11, line_buffer, (float)SCREEN_CENTER_X, 166.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_primary, 1.0f);

    snprintf(line_buffer, sizeof(line_buffer), "VOLUME: %u / %u", (unsigned)status->volume_level,
             (unsigned)status->max_volume);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_11, line_buffer, (float)SCREEN_CENTER_X, 194.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_primary, 1.0f);

    render_draw_text_alpha(surface, FONT_STYLE_MONO_11,
                           status->sd_mounted ? "SD: MOUNTED" : "SD: NONE", (float)SCREEN_CENTER_X,
                           222.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_secondary,
                           1.0f);
}

void render_draw_crash_frame(const struct framebuffer_surface *surface, uint32_t crash_count,
                             enum appearance_mode mode) {
    if (!surface_is_valid(surface)) {
        return;
    }

    const struct appearance_palette *palette = theme_get_palette(mode);
    render_fill_canvas(surface, palette->canvas);

    render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_18, "CRASH SCREEN", (float)SCREEN_CENTER_X,
                           116.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->signal_alert,
                           1.0f);

    char line_buffer[40];
    snprintf(line_buffer, sizeof(line_buffer), "%u CONSECUTIVE CRASHES", (unsigned)crash_count);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_11, line_buffer, (float)SCREEN_CENTER_X, 162.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_primary, 1.0f);

    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "CHECK .DIAL-OS/CRASH.LOG",
                           (float)SCREEN_CENTER_X, 192.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->ink_secondary, 1.0f);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "HOLD POWER TO SHUT DOWN",
                           (float)SCREEN_CENTER_X, 220.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->ink_secondary, 1.0f);
}

void render_draw_shutdown_frame(const struct framebuffer_surface *surface,
                                enum appearance_mode mode) {
    if (!surface_is_valid(surface)) {
        return;
    }

    const struct appearance_palette *palette = theme_get_palette(mode);
    render_fill_canvas(surface, palette->canvas);

    render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_18, "SHUTTING DOWN", (float)SCREEN_CENTER_X,
                           168.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_primary,
                           1.0f);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_10, "SYNCING STORAGE...",
                           (float)SCREEN_CENTER_X, 206.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->ink_secondary, 1.0f);
}
