#ifndef DIALOS_FONT_ATLAS_H
#define DIALOS_FONT_ATLAS_H

#include <stddef.h>
#include <stdint.h>

struct bitmap_glyph {
    uint32_t codepoint;
    uint16_t width;
    uint16_t height;
    int16_t offset_x;
    int16_t offset_y;
    int16_t advance_x;
    uint32_t bitmap_offset;
};

struct bitmap_font {
    uint16_t line_height_px;
    int16_t ascent_px;
    int16_t cap_height_px;
    uint16_t glyph_count;
    const struct bitmap_glyph *glyphs;
    const uint8_t *alpha_data;
};

enum font_style_id {
    FONT_STYLE_MONO_8 = 0,
    FONT_STYLE_MONO_9 = 1,
    FONT_STYLE_MONO_10 = 2,
    FONT_STYLE_MONO_11 = 3,
    FONT_STYLE_MONO_13 = 4,
    FONT_STYLE_MONO_18 = 5,
    FONT_STYLE_DISPLAY_11 = 6,
    FONT_STYLE_DISPLAY_13 = 7,
    FONT_STYLE_DISPLAY_15 = 8,
    FONT_STYLE_DISPLAY_18 = 9,
    FONT_STYLE_DISPLAY_20 = 10,
    FONT_STYLE_DISPLAY_22 = 11,
    FONT_STYLE_DISPLAY_32 = 12,
    FONT_STYLE_DISPLAY_40 = 13,
    FONT_STYLE_DISPLAY_46 = 14
};

const struct bitmap_font *font_atlas_get(enum font_style_id style_id);
const struct bitmap_glyph *font_atlas_lookup_glyph(const struct bitmap_font *font,
                                                   uint32_t codepoint);
uint32_t font_atlas_decode_utf8_next(const char *text, size_t *byte_offset);
int32_t font_atlas_measure_width(const struct bitmap_font *font, const char *text);

#endif