#include "render_now_playing.h"

#include "home_screen.h"
#include "render_primitives.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void format_mm_ss_u32(uint32_t total_seconds, char *out, size_t cap) {
    uint32_t mins = total_seconds / 60u;
    uint32_t secs = total_seconds % 60u;
    snprintf(out, cap, "%02u:%02u", (unsigned)mins, (unsigned)secs);
}

static void draw_vinyl_or_fallback(const struct framebuffer_surface *surface,
                                   const uint16_t *cover_pixels, uint32_t cover_size,
                                   float center_x, float center_y, float radius,
                                   float angle_radians, float alpha,
                                   const struct appearance_palette *palette, bool is_dark) {
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD || radius <= 1.0f) {
        return;
    }
    if (cover_pixels != NULL && cover_size > 0u) {
        render_blit_rgb565_circle_rotated(surface, cover_pixels, cover_size, cover_size, center_x,
                                          center_y, radius, angle_radians, alpha);
        return;
    }
    uint32_t disc_base = is_dark ? 0xFF141820u : 0xFF2B3038u;
    render_draw_filled_circle_aa(surface, center_x, center_y, radius, disc_base, alpha);
    render_draw_arc_aa(surface, center_x, center_y, radius * 0.72f, 1.4f, angle_radians,
                       angle_radians + 1.8f, palette->signal_highlight, alpha * 0.8f);
    render_draw_arc_aa(surface, center_x, center_y, radius * 0.52f, 1.2f,
                       angle_radians + 3.14f, angle_radians + 4.8f, palette->ink_primary,
                       alpha * 0.45f);
    render_draw_filled_circle_aa(surface, center_x, center_y, radius * 0.16f,
                                 palette->surface, alpha);
}

static void draw_idle_screen(const struct framebuffer_surface *surface,
                             const struct now_playing_controller *controller,
                             const struct appearance_palette *palette, bool is_dark) {
    /* Perspective floor */
    float drift = fmodf(controller->elapsed_seconds * 0.25f, 1.0f);
    for (int32_t row = 0; row < 3; row++) {
        float y = 266.0f + (float)row * 26.0f;
        float half_w = 110.0f + (float)row * 24.0f;
        render_draw_line_fast(surface, 180.0f - half_w, y, 180.0f + half_w, y,
                              palette->hairline, 0.75f);
    }
    for (int32_t col = -3; col <= 3; col++) {
        float x0 = 180.0f + ((float)col + drift - 0.5f) * 28.0f;
        float x1 = 180.0f + ((float)col + drift - 0.5f) * 48.0f;
        render_draw_line_fast(surface, x0, 266.0f, x1, 318.0f, palette->hairline, 0.75f);
    }

    render_draw_arc_aa(surface, 180.0f, 180.0f, 174.0f, 1.5f, 2.3f, 3.9f, palette->ink_secondary,
                       0.30f);
    render_draw_arc_aa(surface, 180.0f, 180.0f, 174.0f, 1.5f, -0.8f, 0.8f,
                       palette->signal_highlight, 0.30f);
    render_draw_diagonal_mark(surface, 84.0f, 84.0f, 4.0f, 1.5f, palette->hairline_strong, 0.7f);
    render_draw_diagonal_mark(surface, 276.0f, 276.0f, 4.0f, 1.5f, palette->hairline_strong, 0.7f);

    float breath = 1.0f + 0.03f * sinf(controller->elapsed_seconds * 2.1f);
    float med_r = 40.0f * breath;
    render_draw_filled_circle_aa(surface, 180.0f, 114.0f, med_r, palette->surface, 0.92f);
    render_draw_arc_aa(surface, 180.0f, 114.0f, med_r, 1.6f, 0.0f, FULL_CIRCLE_RADIANS,
                       palette->hairline_strong, 0.85f);
    float spin = controller->elapsed_seconds * 0.9f;
    render_draw_arc_aa(surface, 180.0f, 114.0f, med_r + 3.0f, 2.2f, spin, spin + 1.4f,
                       palette->signal_highlight, 0.95f);
    render_draw_arc_aa(surface, 180.0f, 114.0f, 18.0f, 2.4f, 0.5f, 5.4f, palette->ink_primary,
                       0.85f);
    render_draw_filled_circle_aa(surface, 180.0f, 114.0f, 5.0f, palette->ink_primary, 0.9f);
    render_draw_line_aa(surface, 198.0f, 94.0f, 189.0f, 116.0f, 2.2f, palette->ink_primary, 0.9f);
    render_draw_filled_circle_aa(surface, 189.0f, 116.0f, 2.5f, palette->signal_highlight, 1.0f);

    render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_22, "Not Playing", 180.0f, 186.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_primary, 1.0f);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_9, "NO TRACK QUEUED ON DISC", 180.0f, 210.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_secondary, 1.0f);

    float cta_scale = controller->idle_cta_pressed ? 0.96f : 1.0f;
    float cta_w = 168.0f * cta_scale;
    float cta_h = 42.0f * cta_scale;
    float cta_x = 180.0f - cta_w * 0.5f;
    float cta_y = 267.0f - cta_h * 0.5f;
    render_draw_chamfer_plate_filled_aa(surface, cta_x, cta_y, cta_w, cta_h, 8.0f,
                                        palette->signal_highlight, 1.0f);
    render_draw_rect_corner_brackets(surface, cta_x - 3.0f, cta_y - 3.0f, cta_w + 6.0f,
                                     cta_h + 6.0f, 7.0f, 1.6f, palette->signal_highlight, 0.9f);
    uint32_t cta_ink = is_dark ? 0xFF060709u : 0xFFFFFFFFu;
    render_draw_text_alpha(surface, FONT_STYLE_MONO_11, "BROWSE LIBRARY", 180.0f, 267.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, cta_ink, 1.0f);
}

static void draw_lyrics_overlay(const struct framebuffer_surface *surface,
                                struct now_playing_controller *controller,
                                const struct appearance_palette *palette) {
    float alpha = controller->lyrics_progress;
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD || controller->engine == NULL) {
        return;
    }

    float dim = 1.0f;
    if (controller->quick_actions_progress > 0.01f || controller->queue_sheet_progress > 0.01f ||
        controller->details_sheet_progress > 0.01f || controller->playlist_picker_progress > 0.01f ||
        controller->volume_hud_alpha > 0.01f || controller->seek_expand_progress > 0.01f) {
        dim = 0.28f;
    }

    /* Fast frosted backdrop + lightweight orbiting motif glows */
    render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, 360.0f, 360.0f, 0xFF07090Du,
                                  0.86f * alpha);
    float orb_a = controller->vinyl_rotation_radians * 0.7f;
    render_draw_filled_circle_aa(surface, 180.0f + cosf(orb_a) * 68.0f,
                                 180.0f + sinf(orb_a) * 54.0f, 96.0f, palette->signal_highlight,
                                 0.07f * alpha);
    render_draw_filled_circle_aa(surface, 180.0f - cosf(orb_a) * 62.0f,
                                 180.0f - sinf(orb_a) * 48.0f, 84.0f, palette->ink_primary,
                                 0.05f * alpha);

    /* Isometric wireframe ceiling + floor */
    for (int32_t row = 0; row < 3; row++) {
        float y_floor = 278.0f + (float)row * 22.0f;
        float y_ceil = 82.0f - (float)row * 22.0f;
        float hw = 95.0f + (float)row * 22.0f;
        render_draw_line_fast(surface, 180.0f - hw, y_floor, 180.0f + hw, y_floor,
                              palette->hairline, 0.45f * alpha);
        render_draw_line_fast(surface, 180.0f - hw, y_ceil, 180.0f + hw, y_ceil,
                              palette->hairline, 0.45f * alpha);
    }

    /* Header */
    struct clip_rect hdr_clip = {87, 36, 273, 64};
    render_draw_marquee_text_rect_clipped(
        surface, FONT_STYLE_DISPLAY_11, controller->engine->active_track.title, 180.0f, 50.0f,
        186.0f, controller->elapsed_seconds, &hdr_clip, 0xFFF5F6F8u, alpha * dim);

    struct lyrics_resolved_state resolved;
    if (controller->media != NULL) {
        pthread_mutex_lock(&controller->media->mutex);
        now_playing_media_resolve_cue(&controller->media->active_lyrics,
                                      controller->lyrics_mode_pref,
                                      (uint32_t)controller->interpolated_elapsed_ms,
                                      controller->engine->duration_ms, &resolved);
        pthread_mutex_unlock(&controller->media->mutex);
    } else {
        now_playing_media_resolve_cue(NULL, LYRICS_MODE_NONE, 0u, 0u, &resolved);
    }

    char time_str[16];
    format_mm_ss_u32((uint32_t)(controller->interpolated_elapsed_ms / 1000.0f), time_str,
                     sizeof(time_str));
    char status_line[48];
    if (!controller->engine->is_playing) {
        snprintf(status_line, sizeof(status_line), "PAUSED · %s", time_str);
    } else if (resolved.effective_mode == LYRICS_MODE_TIMED) {
        snprintf(status_line, sizeof(status_line), "TIMED SYNC · %s", time_str);
    } else if (resolved.effective_mode == LYRICS_MODE_UNTIMED) {
        snprintf(status_line, sizeof(status_line), "AUTO PACE · %s", time_str);
    } else {
        snprintf(status_line, sizeof(status_line), "NO LYRICS · %s", time_str);
    }
    uint32_t dot_col = controller->engine->is_playing ? palette->signal_highlight : 0xFF8D96A6u;
    render_draw_filled_rect_alpha(surface, 118.0f, 64.0f, 4.0f, 4.0f, dot_col, alpha * dim);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_8, status_line, 180.0f, 66.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, 0xFF8D96A6u, alpha * dim);

    /* 4 peripheral registration crosshairs */
    int32_t active_mod = (!resolved.is_intro_cue && resolved.active_index >= 0)
                             ? (resolved.active_index % 4)
                             : -1;
    render_draw_crosshair_mark(surface, 76.0f, 96.0f, 4.0f, 1.4f,
                               active_mod == 0 ? palette->signal_highlight : 0xFF485060u,
                               alpha * dim);
    render_draw_diagonal_mark(surface, 284.0f, 96.0f, 4.0f, 1.4f,
                              active_mod == 1 ? palette->signal_highlight : 0xFF485060u,
                              alpha * dim);
    render_draw_diagonal_mark(surface, 76.0f, 264.0f, 4.0f, 1.4f,
                              active_mod == 2 ? palette->signal_highlight : 0xFF485060u,
                              alpha * dim);
    render_draw_crosshair_mark(surface, 284.0f, 264.0f, 4.0f, 1.4f,
                               active_mod == 3 ? palette->signal_highlight : 0xFF485060u,
                               alpha * dim);

    if (resolved.effective_mode == LYRICS_MODE_NONE) {
        render_draw_filled_circle_aa(surface, 180.0f, 146.0f, 28.0f, palette->surface,
                                     0.9f * alpha * dim);
        render_draw_arc_aa(surface, 180.0f, 146.0f, 28.0f, 1.5f, 0.0f, FULL_CIRCLE_RADIANS,
                           palette->hairline_strong, alpha * dim);
        render_draw_line_aa(surface, 166.0f, 160.0f, 194.0f, 132.0f, 2.4f,
                            palette->signal_highlight, alpha * dim);
        render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_18, "No lyrics found", 180.0f, 204.0f,
                               TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, 0xFFF5F6F8u, alpha * dim);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_9, resolved.status_note, 180.0f, 228.0f,
                               TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, 0xFF8D96A6u, alpha * dim);
        return;
    }

    float cue_ease = render_ease_out_cubic(controller->lyrics_cue_transition);
    if (resolved.is_intro_cue) {
        for (int32_t dot = -1; dot <= 1; dot++) {
            float pulse = 0.55f + 0.45f * sinf(controller->elapsed_seconds * 4.2f - (float)dot * 0.9f);
            uint32_t col = (dot == 0) ? palette->signal_highlight : 0xFFF5F6F8u;
            render_draw_filled_circle_aa(surface, 180.0f + (float)dot * 16.0f, 162.0f,
                                         4.0f + pulse * 1.5f, col, alpha * dim);
        }
    } else {
        float y_shift = render_lerp_f32(10.0f, 0.0f, cue_ease);
        struct clip_rect active_clip = {48, 108, 312, 212};
        render_draw_text_rect_clipped_alpha(
            surface, FONT_STYLE_DISPLAY_20, resolved.active_text, 180.0f, 158.0f + y_shift,
            TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, &active_clip, 0xFFF5F6F8u,
            alpha * dim * (0.35f + 0.65f * cue_ease));
    }

    if (resolved.upcoming_index >= 0) {
        render_draw_line_fast(surface, 98.0f, 206.0f, 262.0f, 206.0f, 0xFF485060u,
                              0.65f * alpha * dim);
        render_draw_diagonal_mark(surface, 95.0f, 206.0f, 2.5f, 1.2f, 0xFF8D96A6u, alpha * dim);
        render_draw_diagonal_mark(surface, 265.0f, 206.0f, 2.5f, 1.2f, 0xFF8D96A6u, alpha * dim);

        struct clip_rect next_clip = {68, 216, 292, 262};
        render_draw_text_rect_clipped_alpha(surface, FONT_STYLE_DISPLAY_15, resolved.upcoming_text,
                                            180.0f, 236.0f, TEXT_ALIGN_CENTER,
                                            TEXT_BASELINE_MIDDLE, &next_clip, 0xFF8D96A6u,
                                            0.72f * alpha * dim);
    }
}

static void draw_seek_and_edge_ring(const struct framebuffer_surface *surface,
                                    const struct now_playing_controller *controller,
                                    const struct appearance_palette *palette) {
    if (controller->engine == NULL || !controller->engine->has_active_track) {
        return;
    }
    float ring_alpha = controller->open_transition_progress;
    if (ring_alpha > ALPHA_VISIBILITY_THRESHOLD) {
        render_draw_arc_aa(surface, 180.0f, 180.0f, 178.5f, 1.6f, 0.0f, FULL_CIRCLE_RADIANS,
                           palette->ink_primary, ring_alpha * 0.20f);
        float dur = (float)controller->engine->duration_ms;
        float pos = controller->is_seeking ? (float)controller->seek_preview_ms
                                           : controller->interpolated_elapsed_ms;
        if (dur > 0.0f) {
            float ratio = render_clamp_f32(pos / dur, 0.0f, 1.0f);
            if (ratio > 0.002f) {
                render_draw_arc_aa(surface, 180.0f, 180.0f, 178.5f, 2.2f, TOP_ANGLE_RADIANS,
                                   TOP_ANGLE_RADIANS + FULL_CIRCLE_RADIANS * ratio,
                                   palette->signal_highlight, ring_alpha);
            }
        }
    }

    if (controller->seek_charge_progress > 0.02f && controller->seek_expand_progress <= 0.05f) {
        render_draw_filled_circle_aa(surface, 180.0f, 180.0f, 13.5f, 0xFF0E1117u, 0.85f);
        render_draw_arc_aa(surface, 180.0f, 180.0f, 15.5f, 2.5f, TOP_ANGLE_RADIANS,
                           TOP_ANGLE_RADIANS + FULL_CIRCLE_RADIANS * controller->seek_charge_progress,
                           palette->signal_highlight, 1.0f);
    }

    if (controller->seek_expand_progress > 0.02f) {
        float expand = render_ease_out_cubic(controller->seek_expand_progress);
        float dial_r = render_lerp_f32(13.5f, 62.0f, expand);
        render_draw_filled_circle_aa(surface, 180.0f, 180.0f, dial_r + 6.0f,
                                     palette->signal_highlight, 0.14f * expand);
        render_draw_filled_circle_aa(surface, 180.0f, 180.0f, dial_r, 0xFF0E1117u, 0.94f);
        render_draw_arc_aa(surface, 180.0f, 180.0f, dial_r, 1.6f, 0.0f, FULL_CIRCLE_RADIANS,
                           0xFF485060u, expand);

        for (uint32_t tick = 0u; tick < 24u; tick++) {
            float ang = TOP_ANGLE_RADIANS + ((float)tick / 24.0f) * FULL_CIRCLE_RADIANS;
            bool major = (tick % 6u) == 0u;
            float r0 = dial_r - (major ? 7.0f : 4.0f);
            float r1 = dial_r - 1.5f;
            float ca = cosf(ang);
            float sa = sinf(ang);
            render_draw_line_fast(surface, 180.0f + ca * r0, 180.0f + sa * r0, 180.0f + ca * r1,
                                  180.0f + sa * r1, major ? 0xFFF5F6F8u : 0xFF485060u, expand);
        }

        float dur = (float)controller->engine->duration_ms;
        float ratio = dur > 0.0f ? render_clamp_f32((float)controller->seek_preview_ms / dur, 0.0f, 1.0f)
                                 : 0.0f;
        if (ratio > 0.002f) {
            float end_ang = TOP_ANGLE_RADIANS + FULL_CIRCLE_RADIANS * ratio;
            render_draw_arc_aa(surface, 180.0f, 180.0f, dial_r - 1.0f, 3.5f, TOP_ANGLE_RADIANS,
                               end_ang, palette->signal_highlight, expand);
            render_draw_filled_rect_alpha(surface, 180.0f + cosf(end_ang) * (dial_r - 1.0f) - 3.0f,
                                          180.0f + sinf(end_ang) * (dial_r - 1.0f) - 3.0f, 6.0f,
                                          6.0f, palette->signal_highlight, expand);
        }

        if (expand > 0.45f) {
            float txt_a = render_normalize_range(expand, 0.45f, 1.0f);
            char cur_str[16];
            char dur_str[20];
            format_mm_ss_u32(controller->seek_preview_ms / 1000u, cur_str, sizeof(cur_str));
            format_mm_ss_u32(controller->engine->duration_ms / 1000u, dur_str + 2,
                             sizeof(dur_str) - 2u);
            dur_str[0] = '/';
            dur_str[1] = ' ';
            render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "SEEK", 180.0f, 159.0f,
                                   TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                                   palette->signal_highlight, txt_a);
            render_draw_text_alpha(surface, FONT_STYLE_MONO_18, cur_str, 180.0f, 181.0f,
                                   TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, 0xFFF5F6F8u, txt_a);
            render_draw_text_alpha(surface, FONT_STYLE_MONO_9, dur_str, 180.0f, 201.0f,
                                   TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, 0xFF8D96A6u, txt_a);
        }
    }
}

static void draw_quick_actions(const struct framebuffer_surface *surface,
                               const struct now_playing_controller *controller,
                               const struct appearance_palette *palette, bool is_dark) {
    float alpha = controller->quick_actions_progress;
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD || controller->engine == NULL) {
        return;
    }

    render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, 360.0f, 360.0f, 0xFF060709u, 0.74f * alpha);

    char cur_str[16];
    char dur_str[16];
    format_mm_ss_u32((uint32_t)(controller->interpolated_elapsed_ms / 1000.0f), cur_str,
                     sizeof(cur_str));
    format_mm_ss_u32(controller->engine->duration_ms / 1000u, dur_str, sizeof(dur_str));
    char hdr_status[48];
    if (controller->engine->is_playing) {
        snprintf(hdr_status, sizeof(hdr_status), "%s / %s", cur_str, dur_str);
    } else {
        snprintf(hdr_status, sizeof(hdr_status), "PAUSED · %s", cur_str);
    }
    render_draw_text_alpha(surface, FONT_STYLE_MONO_9, hdr_status, 180.0f, 54.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->signal_highlight, alpha);

    struct clip_rect title_clip = {68, 66, 292, 94};
    render_draw_marquee_text_rect_clipped(
        surface, FONT_STYLE_DISPLAY_18, controller->engine->active_track.title, 180.0f, 80.0f,
        224.0f, controller->elapsed_seconds, &title_clip, 0xFFF5F6F8u, alpha);

    char sub[256];
    snprintf(sub, sizeof(sub), "%s · %s", controller->engine->active_track.artist,
             controller->engine->active_track.album);
    struct clip_rect sub_clip = {58, 94, 302, 114};
    render_draw_marquee_text_rect_clipped(surface, FONT_STYLE_DISPLAY_11, sub, 180.0f, 102.0f,
                                          244.0f, controller->elapsed_seconds, &sub_clip,
                                          0xFF8D96A6u, alpha);
    render_draw_line_fast(surface, 74.0f, 120.0f, 286.0f, 120.0f, 0xFF232833u, alpha);

    static const float bx[6] = {104.0f, 180.0f, 256.0f, 104.0f, 180.0f, 256.0f};
    static const float by[6] = {168.0f, 168.0f, 168.0f, 238.0f, 238.0f, 238.0f};
    bool active_flags[6] = {
        controller->engine->active_track.is_favorite,
        controller->engine->shuffle_enabled,
        controller->engine->repeat_mode != PLAYBACK_REPEAT_OFF,
        controller->playlist_picker_open,
        controller->lyrics_open,
        controller->details_sheet_open,
    };
    const char *repeat_cap =
        controller->engine->repeat_mode == PLAYBACK_REPEAT_ONE
            ? "REP 1"
            : (controller->engine->repeat_mode == PLAYBACK_REPEAT_ALL ? "REP ALL" : "REPEAT");
    const char *captions[6] = {"FAV", "SHUFFLE", repeat_cap, "MIX+", "LYRICS", "INFO"};
    uint32_t inv_ink = is_dark ? 0xFF060709u : 0xFFFFFFFFu;

    for (int32_t idx = 0; idx < 6; idx++) {
        bool pressed = (controller->quick_action_pressed_index == idx);
        bool active = active_flags[idx];
        float r = pressed ? (24.0f * 0.93f) : 24.0f;
        uint32_t fill = active ? palette->signal_highlight : 0xFF141820u;
        uint32_t icon_col = active ? inv_ink : 0xFFF5F6F8u;
        render_draw_filled_circle_aa(surface, bx[idx], by[idx], r, fill,
                                     (active ? 0.98f : 0.88f) * alpha);
        render_draw_arc_aa(surface, bx[idx], by[idx], r, 1.4f, 0.0f, FULL_CIRCLE_RADIANS,
                           (pressed || active) ? palette->signal_highlight : 0xFF485060u, alpha);

        /* Vector glyphs */
        if (idx == 0) {
            if (active) {
                render_draw_heart_filled_aa(surface, bx[idx], by[idx], 8.0f, icon_col, alpha);
            } else {
                render_draw_heart_stroked_aa(surface, bx[idx], by[idx], 8.0f, 1.7f, icon_col,
                                             alpha);
            }
        } else if (idx == 1) {
            render_draw_line_aa(surface, bx[idx] - 7.0f, by[idx] + 4.0f, bx[idx] + 7.0f,
                                by[idx] - 4.0f, 1.8f, icon_col, alpha);
            render_draw_line_aa(surface, bx[idx] - 7.0f, by[idx] - 4.0f, bx[idx] + 7.0f,
                                by[idx] + 4.0f, 1.8f, icon_col, alpha);
        } else if (idx == 2) {
            render_draw_arc_aa(surface, bx[idx], by[idx], 7.5f, 1.8f, 0.2f, 5.7f, icon_col, alpha);
            if (controller->engine->repeat_mode == PLAYBACK_REPEAT_ONE) {
                render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "1", bx[idx], by[idx],
                                       TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, icon_col, alpha);
            }
        } else if (idx == 3) {
            render_draw_line_aa(surface, bx[idx] - 7.0f, by[idx] - 4.0f, bx[idx] + 3.0f,
                                by[idx] - 4.0f, 1.8f, icon_col, alpha);
            render_draw_line_aa(surface, bx[idx] - 7.0f, by[idx], bx[idx] + 1.0f, by[idx], 1.8f,
                                icon_col, alpha);
            render_draw_line_aa(surface, bx[idx] - 7.0f, by[idx] + 4.0f, bx[idx] + 1.0f,
                                by[idx] + 4.0f, 1.8f, icon_col, alpha);
            uint32_t plus_col = active ? icon_col : palette->signal_highlight;
            render_draw_line_aa(surface, bx[idx] + 5.0f, by[idx], bx[idx] + 5.0f, by[idx] + 6.0f,
                                1.8f, plus_col, alpha);
            render_draw_line_aa(surface, bx[idx] + 2.0f, by[idx] + 3.0f, bx[idx] + 8.0f,
                                by[idx] + 3.0f, 1.8f, plus_col, alpha);
        } else if (idx == 4) {
            render_draw_chamfer_plate_stroked_aa(surface, bx[idx] - 8.0f, by[idx] - 6.0f, 16.0f,
                                                 11.0f, 2.5f, 1.6f, icon_col, alpha);
            render_draw_filled_rect_alpha(surface, bx[idx] - 3.5f, by[idx] - 2.0f, 2.5f, 3.0f,
                                          active ? icon_col : palette->signal_highlight, alpha);
            render_draw_filled_rect_alpha(surface, bx[idx] + 1.0f, by[idx] - 2.0f, 2.5f, 3.0f,
                                          active ? icon_col : palette->signal_highlight, alpha);
        } else {
            render_draw_arc_aa(surface, bx[idx], by[idx], 8.0f, 1.6f, 0.0f, FULL_CIRCLE_RADIANS,
                               icon_col, alpha);
            render_draw_filled_circle_aa(surface, bx[idx], by[idx] - 3.5f, 1.3f,
                                         active ? icon_col : palette->signal_highlight, alpha);
            render_draw_line_aa(surface, bx[idx], by[idx] - 0.5f, bx[idx], by[idx] + 4.5f, 1.8f,
                                icon_col, alpha);
        }

        render_draw_text_alpha(surface, FONT_STYLE_MONO_8, captions[idx], bx[idx], by[idx] + 35.0f,
                               TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                               active ? palette->signal_highlight : 0xFF8D96A6u, alpha);
    }
}

static void draw_playlist_picker(const struct framebuffer_surface *surface,
                                 const struct now_playing_controller *controller,
                                 const struct appearance_palette *palette) {
    float alpha = controller->playlist_picker_progress;
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD || controller->engine == NULL) {
        return;
    }

    render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, 360.0f, 360.0f, 0xFF060709u, 0.72f * alpha);
    render_draw_chamfer_plate_filled_aa(surface, 66.0f, 78.0f, 228.0f, 202.0f, 10.0f,
                                        palette->surface, 0.96f * alpha);
    render_draw_chamfer_plate_stroked_aa(surface, 66.0f, 78.0f, 228.0f, 202.0f, 10.0f, 1.5f,
                                         palette->hairline_strong, alpha);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "ADD TO PLAYLIST", 180.0f, 95.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->signal_highlight, alpha);

    struct clip_rect trk_clip = {80, 102, 280, 124};
    render_draw_marquee_text_rect_clipped(
        surface, FONT_STYLE_DISPLAY_13, controller->engine->active_track.title, 180.0f, 113.0f,
        200.0f, controller->elapsed_seconds, &trk_clip, palette->ink_primary, alpha);

    uint32_t visible = controller->picker_count < NP_PICKER_VISIBLE_ROWS
                           ? controller->picker_count
                           : NP_PICKER_VISIBLE_ROWS;
    uint32_t base_offset = 0u;
    if (controller->picker_count > NP_PICKER_VISIBLE_ROWS) {
        base_offset = (uint32_t)render_clamp_f32(
            roundf(controller->picker_scroll), 0.0f,
            (float)(controller->picker_count - NP_PICKER_VISIBLE_ROWS));
    }
    for (uint32_t row = 0u; row < visible; row++) {
        uint32_t item_idx = base_offset + row;
        const struct library_db_playlist_membership *item = &controller->picker_items[item_idx];
        float ry = 132.0f + (float)row * 35.0f;
        bool pressed = (controller->picker_pressed_row == (int32_t)item_idx);
        render_draw_chamfer_plate_filled_aa(surface, 78.0f, ry, 204.0f, 32.0f, 6.0f,
                                            pressed ? palette->signal_highlight : palette->canvas,
                                            alpha * (pressed ? 0.25f : 0.85f));
        render_draw_chamfer_plate_stroked_aa(
            surface, 78.0f, ry, 204.0f, 32.0f, 6.0f, 1.2f,
            item->contains_track ? palette->signal_highlight : palette->hairline_strong, alpha);
        struct clip_rect name_clip = {88, (int32_t)ry + 4, 212, (int32_t)ry + 28};
        render_draw_text_rect_clipped_alpha(surface, FONT_STYLE_DISPLAY_11, item->name, 88.0f,
                                            ry + 16.0f, TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE,
                                            &name_clip, palette->ink_primary, alpha);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_9,
                               item->contains_track ? "ADDED" : "+ ADD", 272.0f, ry + 16.0f,
                               TEXT_ALIGN_RIGHT, TEXT_BASELINE_MIDDLE,
                               item->contains_track ? palette->signal_highlight
                                                    : palette->ink_secondary,
                               alpha);
    }

    render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "SWIPE LEFT EDGE TO CLOSE", 180.0f, 270.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_secondary, alpha);
}

static const char *format_kind_label(uint8_t format_kind) {
    switch (format_kind) {
        case LIBRARY_FORMAT_FLAC:
            return "FLAC 16-BIT";
        case LIBRARY_FORMAT_MP3:
            return "MP3 CBR/VBR";
        case LIBRARY_FORMAT_M4A:
            return "AAC / ALAC";
        case LIBRARY_FORMAT_WAV:
            return "WAV PCM";
        default:
            return "AUDIO";
    }
}

static void draw_details_sheet(const struct framebuffer_surface *surface,
                               const struct now_playing_controller *controller,
                               struct library_cover_cache *cover_cache,
                               const struct appearance_palette *palette) {
    float p = controller->details_sheet_progress;
    if (p <= ALPHA_VISIBILITY_THRESHOLD || controller->engine == NULL) {
        return;
    }
    float ease = render_ease_out_cubic(p);
    float y_off = render_lerp_f32(84.0f, 0.0f, ease);
    render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, 360.0f, 360.0f, palette->canvas, 0.92f * ease);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_9, "TRACK DETAILS", 180.0f, 54.0f + y_off,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->signal_highlight, ease);

    const struct library_db_track_details *trk = &controller->engine->active_track;
    render_draw_chamfer_plate_filled_aa(surface, 56.0f, 70.0f + y_off, 248.0f, 68.0f, 8.0f,
                                        palette->surface, ease);
    render_draw_chamfer_plate_stroked_aa(surface, 56.0f, 70.0f + y_off, 248.0f, 68.0f, 8.0f, 1.2f,
                                         palette->hairline_strong, ease);

    const uint16_t *thumb =
        (cover_cache != NULL && trk->album_uid != 0ull)
            ? library_cover_cache_lookup_or_load_disk(cover_cache, trk->album_uid)
            : NULL;
    if (thumb != NULL) {
        struct clip_rect thumb_clip = {66, (int32_t)(81.0f + y_off), 112, (int32_t)(127.0f + y_off)};
        render_blit_rgb565_scaled_clipped(surface, thumb, LIBRARY_COVER_SIZE_PX,
                                          LIBRARY_COVER_SIZE_PX, 66.0f, 81.0f + y_off, 46.0f,
                                          46.0f, &thumb_clip, ease);
    } else {
        render_draw_filled_rect_alpha(surface, 66.0f, 81.0f + y_off, 46.0f, 46.0f,
                                      palette->hairline, ease);
    }
    render_draw_stroked_rect_alpha(surface, 66.0f, 81.0f + y_off, 46.0f, 46.0f, 1.2f,
                                   palette->signal_highlight, ease);

    struct clip_rect info_clip = {120, (int32_t)(72.0f + y_off), 296, (int32_t)(136.0f + y_off)};
    render_draw_marquee_text_rect_clipped(surface, FONT_STYLE_DISPLAY_13, trk->title, 208.0f,
                                          89.0f + y_off, 174.0f, controller->elapsed_seconds,
                                          &info_clip, palette->ink_primary, ease);
    render_draw_marquee_text_rect_clipped(surface, FONT_STYLE_DISPLAY_11, trk->artist, 208.0f,
                                          106.0f + y_off, 174.0f, controller->elapsed_seconds,
                                          &info_clip, palette->ink_secondary, ease);
    char album_yr[144];
    if (trk->year > 0u) {
        snprintf(album_yr, sizeof(album_yr), "%s · %u", trk->album, (unsigned)trk->year);
    } else {
        snprintf(album_yr, sizeof(album_yr), "%s", trk->album[0] ? trk->album : "UNKNOWN ALBUM");
    }
    render_draw_marquee_text_rect_clipped(surface, FONT_STYLE_MONO_9, album_yr, 208.0f,
                                          123.0f + y_off, 174.0f, controller->elapsed_seconds,
                                          &info_clip, palette->ink_secondary, ease);

    /* 2x2 technical spec grid */
    const char *labels[4] = {"FORMAT", "SAMPLE RATE", "BITRATE", "DISC TRACK"};
    char vals[4][32];
    snprintf(vals[0], sizeof(vals[0]), "%s", format_kind_label(trk->format_kind));
    snprintf(vals[1], sizeof(vals[1]), "44.1 KHZ");
    if (trk->duration_seconds > 0u && trk->file_size_bytes > 0u) {
        uint32_t kbps = (uint32_t)((trk->file_size_bytes * 8ull) /
                                   ((uint64_t)trk->duration_seconds * 1000ull));
        snprintf(vals[2], sizeof(vals[2]), "%u KBPS", (unsigned)kbps);
    } else {
        snprintf(vals[2], sizeof(vals[2]), "1,024 KBPS");
    }
    snprintf(vals[3], sizeof(vals[3]), "%02u / %02u",
             (unsigned)(trk->track_number > 0u ? trk->track_number : 1u),
             (unsigned)(trk->album_track_count > 0u ? trk->album_track_count : 1u));

    for (uint32_t cell = 0u; cell < 4u; cell++) {
        float cx = 56.0f + (float)(cell % 2u) * 128.0f;
        float cy = 148.0f + (float)(cell / 2u) * 56.0f + y_off;
        render_draw_chamfer_plate_filled_aa(surface, cx, cy, 120.0f, 48.0f, 6.0f,
                                            palette->surface, ease);
        render_draw_chamfer_plate_stroked_aa(
            surface, cx, cy, 120.0f, 48.0f, 6.0f, 1.2f,
            cell == 0u ? palette->signal_highlight : palette->hairline_strong, ease);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_8, labels[cell], cx + 12.0f, cy + 15.0f,
                               TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE, palette->ink_secondary, ease);
        render_draw_text_alpha(surface, FONT_STYLE_MONO_11, vals[cell], cx + 12.0f, cy + 33.0f,
                               TEXT_ALIGN_LEFT, TEXT_BASELINE_MIDDLE,
                               cell == 0u ? palette->signal_highlight : palette->ink_primary, ease);
    }

    char dur_str[16];
    format_mm_ss_u32(trk->duration_seconds, dur_str, sizeof(dur_str));
    float mb = trk->file_size_bytes > 0u ? ((float)trk->file_size_bytes / (1024.0f * 1024.0f))
                                         : ((float)trk->duration_seconds * 0.13f);
    char footer[192];
    snprintf(footer, sizeof(footer), "%s · %.1f MB · %s",
             trk->genre[0] ? trk->genre : "AUDIO", (double)mb, dur_str);
    render_draw_chamfer_plate_filled_aa(surface, 62.0f, 264.0f + y_off, 236.0f, 30.0f, 6.0f,
                                        palette->surface, ease);
    struct clip_rect f_clip = {70, (int32_t)(266.0f + y_off), 290, (int32_t)(292.0f + y_off)};
    render_draw_marquee_text_rect_clipped(surface, FONT_STYLE_MONO_9, footer, 180.0f,
                                          279.0f + y_off, 220.0f, controller->elapsed_seconds,
                                          &f_clip, palette->ink_primary, ease);
}

static void draw_queue_sheet(const struct framebuffer_surface *surface,
                             const struct now_playing_controller *controller,
                             const struct appearance_palette *palette) {
    float p = controller->queue_sheet_progress;
    if (p <= ALPHA_VISIBILITY_THRESHOLD || controller->engine == NULL) {
        return;
    }
    float ease = render_ease_out_cubic(p);
    float y_off = render_lerp_f32(88.0f, 0.0f, ease);
    render_draw_filled_rect_alpha(surface, 0.0f, 0.0f, 360.0f, 360.0f, palette->canvas, 0.94f * ease);

    render_draw_filled_rect_alpha(surface, 162.0f, 24.0f + y_off, 36.0f, 3.0f,
                                  palette->hairline_strong, ease);
    char badge_hdr[32];
    snprintf(badge_hdr, sizeof(badge_hdr), "%s QUEUE",
             playback_engine_context_badge(controller->engine->context.kind));
    render_draw_text_alpha(surface, FONT_STYLE_MONO_8, badge_hdr, 180.0f, 44.0f + y_off,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->signal_highlight, ease);

    struct clip_rect ctx_clip = {71, (int32_t)(52.0f + y_off), 289, (int32_t)(82.0f + y_off)};
    render_draw_marquee_text_rect_clipped(
        surface, FONT_STYLE_DISPLAY_18, controller->engine->context_title, 180.0f, 67.0f + y_off,
        218.0f, controller->elapsed_seconds, &ctx_clip, palette->ink_primary, ease);

    char sub[64];
    snprintf(sub, sizeof(sub), "TRACK %02u OF %02u%s%s",
             (unsigned)(controller->engine->current_slot + 1u),
             (unsigned)controller->engine->queue_total,
             controller->engine->shuffle_enabled ? " · SHUFFLE" : "",
             controller->engine->repeat_mode == PLAYBACK_REPEAT_ALL
                 ? " · REP ALL"
                 : (controller->engine->repeat_mode == PLAYBACK_REPEAT_ONE ? " · REP 1" : ""));
    render_draw_text_alpha(surface, FONT_STYLE_MONO_9, sub, 180.0f, 87.0f + y_off,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, palette->ink_secondary, ease);

    struct clip_rect list_clip = {24, 102, 336, 316};
    uint32_t total = controller->engine->queue_total;
    for (uint32_t idx = 0u; idx < total; idx++) {
        float row_y = 130.0f + ((float)idx - controller->queue_scroll) * 58.0f + y_off;
        if (row_y < 76.0f || row_y > 340.0f) {
            continue;
        }
        const struct library_db_row *row = now_playing_controller_get_queue_row(controller, idx);
        if (row == NULL) {
            continue;
        }
        float dy = fabsf(row_y - 196.0f);
        float half_w = render_clamp_f32(132.0f - dy * 0.16f, 92.0f, 132.0f);
        float row_alpha = render_clamp_f32(1.0f - (dy - 70.0f) * 0.012f, 0.22f, 1.0f) * ease;
        bool is_active = (idx == controller->engine->current_slot);
        bool is_pressed = (controller->queue_pressed_row == (int32_t)idx);

        float rx = 180.0f - half_w;
        float ry = row_y - 25.0f;
        render_draw_chamfer_plate_filled_clipped_aa(
            surface, rx, ry, half_w * 2.0f, 50.0f, 6.0f, &list_clip,
            is_pressed ? palette->signal_highlight : palette->surface,
            row_alpha * (is_pressed ? 0.20f : 0.88f));
        if (is_active) {
            render_draw_chamfer_plate_stroked_aa(surface, rx, ry, half_w * 2.0f, 50.0f, 6.0f, 1.3f,
                                                 palette->signal_highlight, row_alpha);
            render_draw_filled_rect_clipped_alpha(surface, rx + 4.0f, ry + 10.0f, 3.0f, 30.0f,
                                                  &list_clip, palette->signal_highlight, row_alpha);
        }

        if (is_active && controller->engine->is_playing) {
            for (uint32_t b = 0u; b < 3u; b++) {
                float h = 4.0f + 7.0f * (0.5f + 0.5f * sinf(controller->elapsed_seconds * 6.0f +
                                                            (float)b * 1.6f));
                render_draw_filled_rect_clipped_alpha(
                    surface, rx + 13.0f + (float)b * 4.0f, row_y + 5.0f - h, 2.5f, h, &list_clip,
                    palette->signal_highlight, row_alpha);
            }
        } else {
            char idx_str[8];
            snprintf(idx_str, sizeof(idx_str), "%02u", (unsigned)(idx + 1u));
            render_draw_text_rect_clipped_alpha(
                surface, FONT_STYLE_MONO_10, idx_str, rx + 18.0f, row_y, TEXT_ALIGN_CENTER,
                TEXT_BASELINE_MIDDLE, &list_clip,
                is_active ? palette->signal_highlight : palette->ink_secondary, row_alpha);
        }

        struct clip_rect r_clip = {(int32_t)(rx + 32.0f), (int32_t)(ry + 4.0f),
                                   (int32_t)(rx + half_w * 2.0f - 44.0f), (int32_t)(ry + 46.0f)};
        render_draw_marquee_text_rect_clipped(
            surface, FONT_STYLE_DISPLAY_13, row->title, (r_clip.left_x + r_clip.right_x) * 0.5f,
            row_y - 7.0f, (float)(r_clip.right_x - r_clip.left_x), controller->elapsed_seconds,
            &r_clip, is_active ? palette->signal_highlight : palette->ink_primary, row_alpha);
        render_draw_marquee_text_rect_clipped(
            surface, FONT_STYLE_DISPLAY_11, row->subtitle, (r_clip.left_x + r_clip.right_x) * 0.5f,
            row_y + 10.0f, (float)(r_clip.right_x - r_clip.left_x), controller->elapsed_seconds,
            &r_clip, palette->ink_secondary, row_alpha);

        char dur[16];
        format_mm_ss_u32(row->duration_seconds, dur, sizeof(dur));
        render_draw_text_rect_clipped_alpha(surface, FONT_STYLE_MONO_9, dur,
                                            rx + half_w * 2.0f - 10.0f, row_y, TEXT_ALIGN_RIGHT,
                                            TEXT_BASELINE_MIDDLE, &list_clip, palette->ink_secondary,
                                            row_alpha);
    }

    render_draw_text_alpha(surface, FONT_STYLE_MONO_8, "SWIPE DOWN OR LEFT EDGE TO CLOSE", 180.0f,
                           326.0f + y_off, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           palette->ink_secondary, ease);
}

static void draw_volume_hud(const struct framebuffer_surface *surface,
                            const struct now_playing_controller *controller,
                            const struct appearance_palette *palette) {
    float alpha = controller->volume_hud_alpha;
    if (alpha <= ALPHA_VISIBILITY_THRESHOLD) {
        return;
    }
    uint8_t percent = home_normalize_volume_percent(controller->volume_level);
    bool high_output = (controller->volume_level > VOLUME_HIGH_OUTPUT_THRESHOLD);
    uint32_t hud_col = high_output ? palette->signal_alert : palette->signal_highlight;

    /* Curved right-bezel track */
    render_draw_arc_aa(surface, 180.0f, 180.0f, 166.0f, 7.0f, -0.48f, 0.48f, 0xFF060709u,
                       0.65f * alpha);
    render_draw_arc_aa(surface, 180.0f, 180.0f, 166.0f, 3.0f, -0.48f, 0.48f,
                       palette->hairline_strong, 0.8f * alpha);
    float limit_ang = 0.48f - 0.85f * 0.96f;
    render_draw_line_aa(surface, 180.0f + cosf(limit_ang) * 161.0f,
                        180.0f + sinf(limit_ang) * 161.0f, 180.0f + cosf(limit_ang) * 171.0f,
                        180.0f + sinf(limit_ang) * 171.0f, 2.0f, palette->signal_alert, alpha);
    float ratio = (float)controller->volume_level / (float)VOLUME_HARD_MAX;
    float knob_ang = 0.48f - ratio * 0.96f;
    render_draw_arc_aa(surface, 180.0f, 180.0f, 166.0f, 3.5f, knob_ang, 0.48f, hud_col, alpha);
    render_draw_filled_rect_alpha(surface, 180.0f + cosf(knob_ang) * 166.0f - 3.5f,
                                  180.0f + sinf(knob_ang) * 166.0f - 3.5f, 7.0f, 7.0f, hud_col,
                                  alpha);

    /* Centered circular medallion */
    render_draw_filled_circle_aa(surface, 180.0f, 180.0f, 58.0f, 0xFF0E1117u, 0.94f * alpha);
    render_draw_arc_aa(surface, 180.0f, 180.0f, 54.0f, 2.5f, 0.0f, FULL_CIRCLE_RADIANS,
                       0xFF232833u, alpha);
    if (ratio > 0.01f) {
        render_draw_arc_aa(surface, 180.0f, 180.0f, 54.0f, 3.2f, TOP_ANGLE_RADIANS,
                           TOP_ANGLE_RADIANS + FULL_CIRCLE_RADIANS * ratio, hud_col, alpha);
    }

    uint32_t spk_col = (controller->volume_level == 0u) ? palette->signal_alert : hud_col;
    render_draw_filled_rect_alpha(surface, 174.0f, 152.0f, 6.0f, 7.0f, spk_col, alpha);
    if (controller->volume_level > 0u) {
        render_draw_arc_aa(surface, 182.0f, 155.5f, 6.0f, 1.6f, -0.8f, 0.8f, spk_col, alpha);
    }

    char pct_str[12];
    snprintf(pct_str, sizeof(pct_str), "%u%%", (unsigned)percent);
    render_draw_text_alpha(surface, FONT_STYLE_DISPLAY_22, pct_str, 180.0f, 182.0f,
                           TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE, 0xFFF5F6F8u, alpha);
    render_draw_text_alpha(surface, FONT_STYLE_MONO_8, high_output ? "HIGH OUTPUT" : "VOLUME",
                           180.0f, 204.0f, TEXT_ALIGN_CENTER, TEXT_BASELINE_MIDDLE,
                           high_output ? palette->signal_alert : 0xFF8D96A6u, alpha);
}

void render_draw_now_playing_frame(const struct framebuffer_surface *surface,
                                   struct now_playing_controller *controller,
                                   struct library_cover_cache *cover_cache,
                                   enum appearance_mode appearance_mode) {
    if (surface == NULL || surface->pixels == NULL || controller == NULL) {
        return;
    }
    const struct appearance_palette *palette = theme_get_palette(appearance_mode);
    bool is_dark = (appearance_mode == APPEARANCE_MODE_DARK);
    render_fill_canvas(surface, palette->canvas);

    if (controller->engine == NULL || !controller->engine->has_active_track) {
        draw_idle_screen(surface, controller, palette, is_dark);
    } else {
        const struct library_db_track_details *trk = &controller->engine->active_track;
        uint32_t active_size = 0u;
        const uint16_t *active_cover = now_playing_media_get_active_cover(
            controller->media, trk->album_uid, cover_cache, &active_size);
        float base_angle = controller->vinyl_rotation_radians + controller->vinyl_nudge_radians;

        if (controller->lyrics_progress < 0.99f) {
            if (controller->open_transition_progress < 1.0f &&
                controller->open_origin == NP_OPEN_FROM_ALBUM_CRATE) {
                float p = controller->open_transition_progress;
                float slip = render_ease_out_cubic(render_normalize_range(p, 0.0f, 0.45f));
                float expand = render_ease_in_out_cubic(render_normalize_range(p, 0.25f, 1.0f));
                float vx = render_lerp_f32(218.0f, 246.0f, slip);
                vx = render_lerp_f32(vx, 180.0f, expand);
                float vy = render_lerp_f32(152.0f, 180.0f, expand);
                float vr = render_lerp_f32(52.8f, 180.0f, expand);
                draw_vinyl_or_fallback(surface, active_cover, active_size, vx, vy, vr, base_angle,
                                       1.0f, palette, is_dark);

                float sleeve_alpha =
                    1.0f - render_normalize_range(p, 0.38f, 0.72f);
                if (sleeve_alpha > ALPHA_VISIBILITY_THRESHOLD) {
                    float sx = render_lerp_f32(112.0f, -170.0f,
                                               render_ease_out_cubic(p));
                    struct clip_rect s_clip = {0, 92, 360, 212};
                    if (active_cover != NULL) {
                        render_blit_rgb565_scaled_clipped(surface, active_cover, active_size,
                                                          active_size, sx, 92.0f, 120.0f, 120.0f,
                                                          &s_clip, sleeve_alpha);
                    } else {
                        render_draw_filled_rect_alpha(surface, sx, 92.0f, 120.0f, 120.0f,
                                                      palette->surface, sleeve_alpha);
                    }
                }
            } else if (controller->open_transition_progress < 1.0f &&
                       controller->open_origin == NP_OPEN_FROM_LIBRARY) {
                float expand = render_ease_in_out_cubic(controller->open_transition_progress);
                float vr = render_lerp_f32(54.0f, 180.0f, expand);
                draw_vinyl_or_fallback(surface, active_cover, active_size, 180.0f, 180.0f, vr,
                                       base_angle, 1.0f, palette, is_dark);
            } else if (controller->skip_slide_active) {
                float slide = render_ease_out_expo(controller->skip_slide_progress);
                float dir = (float)controller->skip_slide_direction;
                float out_x = 180.0f - dir * 370.0f * slide;
                float in_x = 180.0f + dir * 370.0f * (1.0f - slide);
                uint32_t out_size = 0u;
                const uint16_t *out_cover = now_playing_media_get_peek_cover(
                    controller->media, controller->skip_outgoing_album_uid, cover_cache, &out_size);
                draw_vinyl_or_fallback(surface, out_cover, out_size, out_x, 180.0f, 180.0f,
                                       controller->skip_outgoing_angle - dir * slide * 0.4f,
                                       1.0f - slide * 0.35f, palette, is_dark);
                draw_vinyl_or_fallback(surface, active_cover, active_size, in_x, 180.0f, 180.0f,
                                       base_angle, 1.0f, palette, is_dark);
            } else if (fabsf(controller->skip_drag_offset_x) > 1.0f) {
                bool cross_album = (controller->skip_peek_valid &&
                                    controller->skip_peek_cache.action == PLAYBACK_SKIP_TRACK &&
                                    !controller->skip_peek_cache.same_album);
                if (cross_album) {
                    float dx = controller->skip_drag_offset_x;
                    draw_vinyl_or_fallback(surface, active_cover, active_size, 180.0f + dx, 180.0f,
                                           180.0f, base_angle + dx * 0.004f, 1.0f, palette,
                                           is_dark);
                    if (fabsf(dx) > 12.0f) {
                        float dir = dx < 0.0f ? 1.0f : -1.0f;
                        uint32_t pk_size = 0u;
                        const uint16_t *pk_cover = now_playing_media_get_peek_cover(
                            controller->media, controller->skip_peek_cache.target_track.album_uid,
                            cover_cache, &pk_size);
                        draw_vinyl_or_fallback(surface, pk_cover, pk_size, 180.0f + dx + dir * 370.0f,
                                               180.0f, 180.0f, base_angle * 0.6f, 0.88f, palette,
                                               is_dark);
                    }
                } else {
                    float twist_scale = (controller->skip_peek_valid &&
                                         controller->skip_peek_cache.action == PLAYBACK_SKIP_BLOCKED)
                                            ? 0.0011f
                                            : 0.0022f;
                    draw_vinyl_or_fallback(surface, active_cover, active_size, 180.0f, 180.0f,
                                           180.0f,
                                           base_angle + controller->skip_drag_offset_x * twist_scale,
                                           1.0f, palette, is_dark);
                }
            } else {
                draw_vinyl_or_fallback(surface, active_cover, active_size, 180.0f, 180.0f, 180.0f,
                                       base_angle, 1.0f, palette, is_dark);
            }
        }

        draw_lyrics_overlay(surface, controller, palette);
        draw_seek_and_edge_ring(surface, controller, palette);
        draw_quick_actions(surface, controller, palette, is_dark);
        draw_playlist_picker(surface, controller, palette);
        draw_details_sheet(surface, controller, cover_cache, palette);
        draw_queue_sheet(surface, controller, palette);
        draw_volume_hud(surface, controller, palette);
    }

    /* Transient Feedback: Play/Pause Pulse */
    if (controller->play_pulse_alpha > ALPHA_VISIBILITY_THRESHOLD) {
        float a = controller->play_pulse_alpha;
        float r = render_lerp_f32(44.0f, 36.0f, a);
        render_draw_filled_circle_aa(surface, 180.0f, 180.0f, r, 0xFF0E1117u, 0.90f * a);
        render_draw_arc_aa(surface, 180.0f, 180.0f, r, 2.0f, 0.0f, FULL_CIRCLE_RADIANS,
                           palette->signal_highlight, a);
        if (controller->play_pulse_is_playing) {
            struct point_2d tri[3] = {{174.0f, 169.0f}, {190.0f, 180.0f}, {174.0f, 191.0f}};
            render_draw_filled_convex_polygon_fast(surface, tri, 3u, palette->signal_highlight, a);
        } else {
            render_draw_filled_rect_alpha(surface, 172.0f, 170.0f, 5.0f, 20.0f,
                                          palette->signal_highlight, a);
            render_draw_filled_rect_alpha(surface, 183.0f, 170.0f, 5.0f, 20.0f,
                                          palette->signal_highlight, a);
        }
    }

    /* Same-album track skip banner */
    if (controller->banner_active) {
        float b_alpha = 1.0f;
        if (controller->banner_timer < NP_BANNER_IN_SECONDS) {
            b_alpha = controller->banner_timer / NP_BANNER_IN_SECONDS;
        } else if (controller->banner_timer > NP_BANNER_IN_SECONDS + NP_BANNER_HOLD_SECONDS) {
            b_alpha = 1.0f - (controller->banner_timer - NP_BANNER_IN_SECONDS - NP_BANNER_HOLD_SECONDS) /
                                 NP_BANNER_OUT_SECONDS;
        }
        b_alpha = render_clamp_f32(b_alpha, 0.0f, 1.0f);
        if (b_alpha > ALPHA_VISIBILITY_THRESHOLD) {
            render_draw_chamfer_plate_filled_aa(surface, 91.0f, 46.0f, 178.0f, 42.0f, 7.0f,
                                                0xFF0E1117u, 0.92f * b_alpha);
            render_draw_filled_rect_alpha(surface, 152.0f, 46.0f, 56.0f, 2.5f,
                                          palette->signal_highlight, b_alpha);
            struct clip_rect b_clip = {99, 50, 261, 86};
            render_draw_marquee_text_rect_clipped(
                surface, FONT_STYLE_DISPLAY_13, controller->banner_title, 180.0f, 61.0f, 160.0f,
                controller->elapsed_seconds, &b_clip, 0xFFF5F6F8u, b_alpha);
            render_draw_marquee_text_rect_clipped(
                surface, FONT_STYLE_MONO_8, controller->banner_subtitle, 180.0f, 77.0f, 160.0f,
                controller->elapsed_seconds, &b_clip, 0xFF8D96A6u, b_alpha);
        }
    }

    /* Toast Pill */
    if (controller->toast_active && controller->toast_alpha > ALPHA_VISIBILITY_THRESHOLD) {
        float ta = controller->toast_alpha;
        render_draw_chamfer_plate_filled_aa(surface, 96.0f, 24.0f, 168.0f, 24.0f, 6.0f,
                                            0xFF0E1117u, 0.92f * ta);
        render_draw_chamfer_plate_stroked_aa(surface, 96.0f, 24.0f, 168.0f, 24.0f, 6.0f, 1.2f,
                                             palette->signal_highlight, ta);
        struct clip_rect t_clip = {104, 25, 256, 47};
        render_draw_marquee_text_rect_clipped(surface, FONT_STYLE_MONO_9, controller->toast_text,
                                              180.0f, 36.0f, 150.0f, controller->elapsed_seconds,
                                              &t_clip, 0xFFF5F6F8u, ta);
    }

    /* Left-edge Back-Swipe Arc */
    if (controller->back_swipe_progress > 0.01f) {
        float half_span = 0.08f + controller->back_swipe_progress * 0.46f;
        render_draw_arc_aa(surface, 180.0f, 180.0f, 168.0f, 4.0f, HALF_CIRCLE_RADIANS - half_span,
                           HALF_CIRCLE_RADIANS + half_span, palette->signal_highlight, 1.0f);
    }
}
