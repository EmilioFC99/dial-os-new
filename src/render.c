#include "render.h"

#include "constants.h"

#include <math.h>
#include <stddef.h>

#define DEGREES_TO_RADIANS 0.0174532925f
#define ARC_SUBSTEP_DEGREES 0.5f

/* Classic 8x8 printable ASCII bitmap font (MSB-first: bit 7 is leftmost pixel). */
static const uint8_t FONT_GLYPHS_8X8[FONT_GLYPH_COUNT][FONT_GLYPH_HEIGHT] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' ' */
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, /* '!' */
    {0x6C, 0x6C, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '"' */
    {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00}, /* '#' */
    {0x30, 0x7C, 0xC0, 0x78, 0x0C, 0xF8, 0x30, 0x00}, /* '$' */
    {0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00}, /* '%' */
    {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00}, /* '&' */
    {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '\'' */
    {0x18, 0x30, 0x60, 0x60, 0x60, 0x30, 0x18, 0x00}, /* '(' */
    {0x60, 0x30, 0x18, 0x18, 0x18, 0x30, 0x60, 0x00}, /* ')' */
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, /* '*' */
    {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00}, /* '+' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30}, /* ',' */
    {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00}, /* '-' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00}, /* '.' */
    {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00}, /* '/' */
    {0x7C, 0xC6, 0xCE, 0xDE, 0xF6, 0xE6, 0x7C, 0x00}, /* '0' */
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, /* '1' */
    {0x7C, 0xC6, 0x06, 0x3C, 0x60, 0xC0, 0xFE, 0x00}, /* '2' */
    {0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00}, /* '3' */
    {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00}, /* '4' */
    {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00}, /* '5' */
    {0x38, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00}, /* '6' */
    {0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00}, /* '7' */
    {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00}, /* '8' */
    {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00}, /* '9' */
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00}, /* ':' */
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30}, /* ';' */
    {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00}, /* '<' */
    {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00}, /* '=' */
    {0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00}, /* '>' */
    {0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00}, /* '?' */
    {0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x78, 0x00}, /* '@' */
    {0x38, 0x6C, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00}, /* 'A' */
    {0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00}, /* 'B' */
    {0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00}, /* 'C' */
    {0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00}, /* 'D' */
    {0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00}, /* 'E' */
    {0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00}, /* 'F' */
    {0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3E, 0x00}, /* 'G' */
    {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00}, /* 'H' */
    {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, /* 'I' */
    {0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00}, /* 'J' */
    {0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00}, /* 'K' */
    {0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00}, /* 'L' */
    {0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00}, /* 'M' */
    {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00}, /* 'N' */
    {0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, /* 'O' */
    {0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00}, /* 'P' */
    {0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xCC, 0x76, 0x00}, /* 'Q' */
    {0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00}, /* 'R' */
    {0x7C, 0xC6, 0x60, 0x38, 0x0C, 0xC6, 0x7C, 0x00}, /* 'S' */
    {0x7E, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, /* 'T' */
    {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, /* 'U' */
    {0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00}, /* 'V' */
    {0xC6, 0xC6, 0xD6, 0xFE, 0xFE, 0xEE, 0xC6, 0x00}, /* 'W' */
    {0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00}, /* 'X' */
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00}, /* 'Y' */
    {0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00}, /* 'Z' */
    {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00}, /* '[' */
    {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00}, /* '\\' */
    {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00}, /* ']' */
    {0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00}, /* '^' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, /* '_' */
    {0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '`' */
    {0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0x00}, /* 'a' */
    {0xE0, 0x60, 0x7C, 0x66, 0x66, 0x66, 0xDC, 0x00}, /* 'b' */
    {0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00}, /* 'c' */
    {0x1C, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x76, 0x00}, /* 'd' */
    {0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00}, /* 'e' */
    {0x3C, 0x66, 0x60, 0xF8, 0x60, 0x60, 0xF0, 0x00}, /* 'f' */
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0xF8}, /* 'g' */
    {0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0x00}, /* 'h' */
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00}, /* 'i' */
    {0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78}, /* 'j' */
    {0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0x00}, /* 'k' */
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, /* 'l' */
    {0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xC6, 0x00}, /* 'm' */
    {0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x00}, /* 'n' */
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, /* 'o' */
    {0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0}, /* 'p' */
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x1E}, /* 'q' */
    {0x00, 0x00, 0xDC, 0x76, 0x60, 0x60, 0xF0, 0x00}, /* 'r' */
    {0x00, 0x00, 0x7C, 0xC0, 0x7C, 0x06, 0xFC, 0x00}, /* 's' */
    {0x30, 0x30, 0xFC, 0x30, 0x30, 0x36, 0x1C, 0x00}, /* 't' */
    {0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00}, /* 'u' */
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00}, /* 'v' */
    {0x00, 0x00, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00}, /* 'w' */
    {0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00}, /* 'x' */
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0xFC}, /* 'y' */
    {0x00, 0x00, 0xFE, 0x8C, 0x18, 0x32, 0xFE, 0x00}, /* 'z' */
    {0x1C, 0x30, 0x30, 0xE0, 0x30, 0x30, 0x1C, 0x00}, /* '{' */
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, /* '|' */
    {0xE0, 0x30, 0x30, 0x1C, 0x30, 0x30, 0xE0, 0x00}, /* '}' */
    {0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '~' */
};

static bool surface_is_valid(const struct framebuffer_surface *surface) {
    return surface != NULL && surface->pixels != NULL && surface->stride_pixels >= SCREEN_WIDTH;
}

uint32_t render_map_coordinate_180(uint32_t logical_coordinate) {
    if (logical_coordinate > SCREEN_COORDINATE_MAX) {
        return 0u;
    }
    return SCREEN_COORDINATE_MAX - logical_coordinate;
}

bool render_is_inside_safe_circle(int32_t logical_x, int32_t logical_y) {
    int32_t delta_x = logical_x - SCREEN_CENTER_X;
    int32_t delta_y = logical_y - SCREEN_CENTER_Y;
    return (delta_x * delta_x + delta_y * delta_y) <= (SAFE_CIRCLE_RADIUS * SAFE_CIRCLE_RADIUS);
}

const uint8_t *render_lookup_glyph(char character) {
    unsigned char code = (unsigned char)character;
    if (code < (unsigned char)FONT_ASCII_FIRST || code > (unsigned char)FONT_ASCII_LAST) {
        return FONT_GLYPHS_8X8[0];
    }
    return FONT_GLYPHS_8X8[code - (unsigned char)FONT_ASCII_FIRST];
}

static void write_logical_pixel(const struct framebuffer_surface *surface, int32_t logical_x,
                                int32_t logical_y, uint32_t color) {
    if (logical_x < 0 || logical_x >= (int32_t)SCREEN_WIDTH ||
        logical_y < 0 || logical_y >= (int32_t)SCREEN_HEIGHT) {
        return;
    }
    if (!render_is_inside_safe_circle(logical_x, logical_y)) {
        return;
    }
    uint32_t physical_x = render_map_coordinate_180((uint32_t)logical_x);
    uint32_t physical_y = render_map_coordinate_180((uint32_t)logical_y);
    surface->pixels[physical_y * surface->stride_pixels + physical_x] = color;
}

static void fill_background(const struct framebuffer_surface *surface) {
    for (uint32_t logical_y = 0u; logical_y < SCREEN_HEIGHT; logical_y++) {
        uint32_t physical_y = render_map_coordinate_180(logical_y);
        uint32_t *row_pixels = surface->pixels + physical_y * surface->stride_pixels;
        for (uint32_t logical_x = 0u; logical_x < SCREEN_WIDTH; logical_x++) {
            uint32_t physical_x = render_map_coordinate_180(logical_x);
            bool inside = render_is_inside_safe_circle((int32_t)logical_x, (int32_t)logical_y);
            row_pixels[physical_x] = inside ? COLOR_BACKGROUND : COLOR_OUTSIDE_BEZEL;
        }
    }
}

static void draw_text_centered(const struct framebuffer_surface *surface, int32_t top_y,
                               const char *text, uint32_t scale, uint32_t color) {
    if (text == NULL || scale == 0u) {
        return;
    }
    size_t length = 0u;
    while (text[length] != '\0') {
        length++;
    }
    int32_t total_width = (int32_t)(length * FONT_GLYPH_WIDTH * scale);
    int32_t start_x = SCREEN_CENTER_X - (total_width / 2);

    for (size_t character_index = 0u; character_index < length; character_index++) {
        const uint8_t *glyph = render_lookup_glyph(text[character_index]);
        int32_t glyph_x = start_x + (int32_t)(character_index * FONT_GLYPH_WIDTH * scale);
        for (uint32_t row_index = 0u; row_index < FONT_GLYPH_HEIGHT; row_index++) {
            uint8_t row_bits = glyph[row_index];
            for (uint32_t column_index = 0u; column_index < FONT_GLYPH_WIDTH; column_index++) {
                if ((row_bits & (uint8_t)(FONT_ROW_MSB_MASK >> column_index)) == 0u) {
                    continue;
                }
                for (uint32_t scale_y = 0u; scale_y < scale; scale_y++) {
                    for (uint32_t scale_x = 0u; scale_x < scale; scale_x++) {
                        int32_t pixel_x = glyph_x + (int32_t)(column_index * scale + scale_x);
                        int32_t pixel_y = top_y + (int32_t)(row_index * scale + scale_y);
                        write_logical_pixel(surface, pixel_x, pixel_y, color);
                    }
                }
            }
        }
    }
}

static size_t append_text(char *buffer, size_t capacity, size_t offset, const char *text) {
    size_t index = 0u;
    while (text[index] != '\0' && (offset + 1u) < capacity) {
        buffer[offset++] = text[index++];
    }
    if (capacity > 0u) {
        buffer[offset < capacity ? offset : capacity - 1u] = '\0';
    }
    return offset;
}

static size_t append_u32(char *buffer, size_t capacity, size_t offset, uint32_t value) {
    char digits[16];
    size_t digit_count = 0u;
    if (value == 0u) {
        digits[digit_count++] = '0';
    } else {
        while (value > 0u && digit_count < sizeof(digits)) {
            digits[digit_count++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    while (digit_count > 0u && (offset + 1u) < capacity) {
        buffer[offset++] = digits[--digit_count];
    }
    if (capacity > 0u) {
        buffer[offset < capacity ? offset : capacity - 1u] = '\0';
    }
    return offset;
}

void render_draw_boot_frame(const struct framebuffer_surface *surface, uint16_t angle_degrees) {
    if (!surface_is_valid(surface)) {
        return;
    }

    fill_background(surface);

    const int32_t inner_sq = BOOT_DIAL_INNER_RADIUS * BOOT_DIAL_INNER_RADIUS;
    const int32_t outer_sq = BOOT_DIAL_OUTER_RADIUS * BOOT_DIAL_OUTER_RADIUS;
    for (int32_t delta_y = -BOOT_DIAL_OUTER_RADIUS; delta_y <= BOOT_DIAL_OUTER_RADIUS; delta_y++) {
        for (int32_t delta_x = -BOOT_DIAL_OUTER_RADIUS; delta_x <= BOOT_DIAL_OUTER_RADIUS; delta_x++) {
            int32_t distance_sq = delta_x * delta_x + delta_y * delta_y;
            if (distance_sq >= inner_sq && distance_sq <= outer_sq) {
                write_logical_pixel(surface, BOOT_DIAL_CENTER_X + delta_x,
                                    BOOT_DIAL_CENTER_Y + delta_y, COLOR_MUTED);
            }
        }
    }

    float start_degrees = (float)(angle_degrees % FULL_CIRCLE_DEGREES);
    float end_degrees = start_degrees + (float)BOOT_DIAL_ARC_DEGREES;
    for (float step_deg = start_degrees; step_deg <= end_degrees; step_deg += ARC_SUBSTEP_DEGREES) {
        float radians = step_deg * DEGREES_TO_RADIANS;
        float cos_angle = cosf(radians);
        float sin_angle = sinf(radians);
        for (int32_t radius = BOOT_DIAL_INNER_RADIUS; radius <= BOOT_DIAL_OUTER_RADIUS; radius++) {
            int32_t pixel_x = BOOT_DIAL_CENTER_X + (int32_t)lroundf((float)radius * cos_angle);
            int32_t pixel_y = BOOT_DIAL_CENTER_Y + (int32_t)lroundf((float)radius * sin_angle);
            write_logical_pixel(surface, pixel_x, pixel_y, COLOR_ACCENT);
        }
    }

    draw_text_centered(surface, 240, "DialOS", 2u, COLOR_FOREGROUND);
}

void render_draw_warning_frame(const struct framebuffer_surface *surface) {
    if (!surface_is_valid(surface)) {
        return;
    }

    fill_background(surface);
    draw_text_centered(surface, 128, "AUDIO SLOW", 2u, COLOR_WARNING);
    draw_text_centered(surface, 172, "Waiting for mq_player", 1u, COLOR_FOREGROUND);
    draw_text_centered(surface, 204, "Hold Power to shut down", 1u, COLOR_MUTED);
}

void render_draw_ready_frame(const struct framebuffer_surface *surface,
                             const struct ready_screen_status *status) {
    if (!surface_is_valid(surface) || status == NULL) {
        return;
    }

    fill_background(surface);
    draw_text_centered(surface, 104, "DialOS Ready", 2u, COLOR_ACCENT);

    char line_buffer[40];
    size_t offset = append_text(line_buffer, sizeof(line_buffer), 0u, "Battery: ");
    if (status->battery_valid) {
        offset = append_u32(line_buffer, sizeof(line_buffer), offset, status->battery_percent);
        append_text(line_buffer, sizeof(line_buffer), offset, "%");
    } else {
        append_text(line_buffer, sizeof(line_buffer), offset, "--");
    }
    draw_text_centered(surface, 160, line_buffer, 1u, COLOR_FOREGROUND);

    offset = append_text(line_buffer, sizeof(line_buffer), 0u, "Volume: ");
    offset = append_u32(line_buffer, sizeof(line_buffer), offset, status->volume_level);
    offset = append_text(line_buffer, sizeof(line_buffer), offset, " / ");
    append_u32(line_buffer, sizeof(line_buffer), offset, status->max_volume);
    draw_text_centered(surface, 188, line_buffer, 1u, COLOR_FOREGROUND);

    draw_text_centered(surface, 216, status->sd_mounted ? "SD: Mounted" : "SD: None", 1u,
                       COLOR_FOREGROUND);
}

void render_draw_crash_frame(const struct framebuffer_surface *surface, uint32_t crash_count) {
    if (!surface_is_valid(surface)) {
        return;
    }

    fill_background(surface);
    draw_text_centered(surface, 108, "CRASH SCREEN", 2u, COLOR_ERROR);

    char line_buffer[40];
    size_t offset = append_u32(line_buffer, sizeof(line_buffer), 0u, crash_count);
    append_text(line_buffer, sizeof(line_buffer), offset, " consecutive crashes");
    draw_text_centered(surface, 156, line_buffer, 1u, COLOR_FOREGROUND);

    draw_text_centered(surface, 184, "Check .dial-os/crash.log", 1u, COLOR_MUTED);
    draw_text_centered(surface, 212, "Hold Power to shut down", 1u, COLOR_MUTED);
}

void render_draw_shutdown_frame(const struct framebuffer_surface *surface) {
    if (!surface_is_valid(surface)) {
        return;
    }

    fill_background(surface);
    draw_text_centered(surface, 164, "SHUTTING DOWN", 2u, COLOR_FOREGROUND);
    draw_text_centered(surface, 204, "Syncing storage...", 1u, COLOR_MUTED);
}
