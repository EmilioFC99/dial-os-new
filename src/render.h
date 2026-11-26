#ifndef DIALOS_RENDER_H
#define DIALOS_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_sequence.h"
#include "theme.h"

struct framebuffer_surface {
    uint32_t *pixels;
    uint32_t stride_pixels;
};

struct ready_screen_status {
    bool battery_valid;
    uint8_t battery_percent;
    uint8_t volume_level;
    uint8_t max_volume;
    bool sd_mounted;
};

uint32_t render_map_coordinate_180(uint32_t logical_coordinate);
bool render_is_inside_bezel_circle(int32_t logical_x, int32_t logical_y);
bool render_is_inside_safe_circle(int32_t logical_x, int32_t logical_y);

void render_draw_boot_frame(const struct framebuffer_surface *surface,
                            const struct boot_render_snapshot *snapshot,
                            enum appearance_mode mode);
void render_draw_ready_frame(const struct framebuffer_surface *surface,
                             const struct ready_screen_status *status,
                             enum appearance_mode mode);
void render_draw_crash_frame(const struct framebuffer_surface *surface, uint32_t crash_count,
                             enum appearance_mode mode);
void render_draw_shutdown_frame(const struct framebuffer_surface *surface,
                                enum appearance_mode mode);

#endif
