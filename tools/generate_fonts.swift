#!/usr/bin/env swift

import CoreGraphics
import CoreText
import Foundation

struct FontSpec {
    let enumName: String
    let symbolPrefix: String
    let sizePt: CGFloat
    let weight: CGFloat
    let monospace: Bool
    let codepoints: [UInt32]
}

struct BakedGlyph {
    let codepoint: UInt32
    let width: UInt16
    let height: UInt16
    let offsetX: Int16
    let offsetY: Int16
    let advanceX: Int16
    let bitmapOffset: UInt32
}

struct BakedFont {
    let spec: FontSpec
    let lineHeightPx: UInt16
    let ascentPx: Int16
    let capHeightPx: Int16
    let glyphs: [BakedGlyph]
    let alphaData: [UInt8]
}

let fontPath = "/Users/emilio.flores/Library/Fonts/Inter-VariableFont_opsz,wght.ttf"
let outputHeaderPath = "src/font_atlas.h"
let outputSourcePath = "src/font_atlas.c"

let asciiAndMiddleDot: [UInt32] = Array(0x20...0x7E).map { UInt32($0) } + [0x00B7]
let timerCodepoints: [UInt32] = [0x20, 0x2D, 0x2E] + Array(0x30...0x39).map { UInt32($0) } + [0x73]

let fontSpecs: [FontSpec] = [
    FontSpec(enumName: "FONT_STYLE_MONO_10", symbolPrefix: "mono_10", sizePt: 10.0, weight: 600.0, monospace: true, codepoints: asciiAndMiddleDot),
    FontSpec(enumName: "FONT_STYLE_MONO_11", symbolPrefix: "mono_11", sizePt: 11.0, weight: 700.0, monospace: true, codepoints: asciiAndMiddleDot),
    FontSpec(enumName: "FONT_STYLE_MONO_13", symbolPrefix: "mono_13", sizePt: 13.0, weight: 800.0, monospace: true, codepoints: asciiAndMiddleDot),
    FontSpec(enumName: "FONT_STYLE_MONO_18", symbolPrefix: "mono_18", sizePt: 18.0, weight: 800.0, monospace: true, codepoints: asciiAndMiddleDot),
    FontSpec(enumName: "FONT_STYLE_DISPLAY_18", symbolPrefix: "display_18", sizePt: 18.0, weight: 800.0, monospace: false, codepoints: asciiAndMiddleDot),
    FontSpec(enumName: "FONT_STYLE_DISPLAY_32", symbolPrefix: "display_32", sizePt: 32.0, weight: 800.0, monospace: false, codepoints: asciiAndMiddleDot),
    FontSpec(enumName: "FONT_STYLE_DISPLAY_40", symbolPrefix: "display_40", sizePt: 40.0, weight: 800.0, monospace: false, codepoints: timerCodepoints),
]

guard let fontDataProvider = CGDataProvider(filename: fontPath),
      let cgFont = CGFont(fontDataProvider) else {
    fputs("Failed to load font at \(fontPath)\n", stderr)
    exit(1)
}

func makeCTFont(sizePt: CGFloat, weight: CGFloat) -> CTFont {
    let wghtAxisKey = NSNumber(value: 0x77676874 as UInt32) // 'wght'
    let opszAxisKey = NSNumber(value: 0x6F70737A as UInt32) // 'opsz'
    let variations: [NSNumber: NSNumber] = [
        wghtAxisKey: NSNumber(value: Double(weight)),
        opszAxisKey: NSNumber(value: Double(max(14.0, sizePt))),
    ]
    let attrs: [CFString: Any] = [
        kCTFontVariationAttribute: variations
    ]
    let descriptor = CTFontDescriptorCreateWithAttributes(attrs as CFDictionary)
    let baseFont = CTFontCreateWithGraphicsFont(cgFont, sizePt, nil, nil)
    return CTFontCreateCopyWithAttributes(baseFont, sizePt, nil, descriptor)
}

func bakeFont(spec: FontSpec) -> BakedFont {
    let ctFont = makeCTFont(sizePt: spec.sizePt, weight: spec.weight)
    let ascentPx = Int16(ceil(CTFontGetAscent(ctFont)))
    let descentPx = Int16(ceil(CTFontGetDescent(ctFont)))
    let capHeightPx = Int16(round(CTFontGetCapHeight(ctFont)))
    let lineHeightPx = UInt16(max(1, Int(ascentPx) + Int(descentPx)))

    let monoAdvance: CGFloat = ceil(spec.sizePt * 0.58)
    let canvasSize = Int(ceil(spec.sizePt * 2.2)) + 16
    let leftPad = 8
    let topPad = Int(ceil(spec.sizePt * 1.35)) + 4
    let cgBaselineY = CGFloat(canvasSize - topPad)

    let colorSpace = CGColorSpaceCreateDeviceGray()
    var alphaPool: [UInt8] = []
    var glyphs: [BakedGlyph] = []

    for codepoint in spec.codepoints {
        guard let scalar = UnicodeScalar(codepoint) else { continue }
        var utf16 = Array(String(scalar).utf16)
        var glyphId: CGGlyph = 0
        CTFontGetGlyphsForCharacters(ctFont, &utf16, &glyphId, utf16.count)

        var advanceSize = CGSize.zero
        CTFontGetAdvancesForGlyphs(ctFont, .horizontal, &glyphId, &advanceSize, 1)

        let targetAdvance = spec.monospace ? monoAdvance : advanceSize.width
        let advanceInt = Int16(max(1, Int(round(targetAdvance))))
        let centerShift = spec.monospace ? (targetAdvance - advanceSize.width) * 0.5 : 0.0

        var buffer = [UInt8](repeating: 0, count: canvasSize * canvasSize)
        buffer.withUnsafeMutableBytes { rawPtr in
            guard let context = CGContext(
                data: rawPtr.baseAddress,
                width: canvasSize,
                height: canvasSize,
                bitsPerComponent: 8,
                bytesPerRow: canvasSize,
                space: colorSpace,
                bitmapInfo: CGImageAlphaInfo.none.rawValue
            ) else { return }

            context.setAllowsAntialiasing(true)
            context.setShouldAntialias(true)
            context.setAllowsFontSmoothing(false)
            context.setShouldSmoothFonts(false)
            context.setAllowsFontSubpixelPositioning(false)
            context.setFillColor(gray: 1.0, alpha: 1.0)

            var position = CGPoint(x: CGFloat(leftPad) + centerShift, y: cgBaselineY)
            CTFontDrawGlyphs(ctFont, &glyphId, &position, 1, context)
        }

        var minCol = canvasSize
        var maxCol = -1
        var minRow = canvasSize
        var maxRow = -1

        for row in 0..<canvasSize {
            for col in 0..<canvasSize {
                if buffer[row * canvasSize + col] > 0 {
                    if col < minCol { minCol = col }
                    if col > maxCol { maxCol = col }
                    if row < minRow { minRow = row }
                    if row > maxRow { maxRow = row }
                }
            }
        }

        if maxCol < minCol || maxRow < minRow {
            glyphs.append(BakedGlyph(
                codepoint: codepoint,
                width: 0,
                height: 0,
                offsetX: 0,
                offsetY: 0,
                advanceX: advanceInt,
                bitmapOffset: UInt32(alphaPool.count)
            ))
        } else {
            let gw = maxCol - minCol + 1
            let gh = maxRow - minRow + 1
            let offsetX = Int16(minCol - leftPad)
            let offsetY = Int16(minRow - topPad)
            let bitmapOffset = UInt32(alphaPool.count)

            for row in minRow...maxRow {
                for col in minCol...maxCol {
                    alphaPool.append(buffer[row * canvasSize + col])
                }
            }

            glyphs.append(BakedGlyph(
                codepoint: codepoint,
                width: UInt16(gw),
                height: UInt16(gh),
                offsetX: offsetX,
                offsetY: offsetY,
                advanceX: advanceInt,
                bitmapOffset: bitmapOffset
            ))
        }
    }

    return BakedFont(
        spec: spec,
        lineHeightPx: lineHeightPx,
        ascentPx: ascentPx,
        capHeightPx: capHeightPx,
        glyphs: glyphs,
        alphaData: alphaPool
    )
}

let bakedFonts = fontSpecs.map { bakeFont(spec: $0) }

let headerContent = """
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
    FONT_STYLE_MONO_10 = 0,
    FONT_STYLE_MONO_11 = 1,
    FONT_STYLE_MONO_13 = 2,
    FONT_STYLE_MONO_18 = 3,
    FONT_STYLE_DISPLAY_18 = 4,
    FONT_STYLE_DISPLAY_32 = 5,
    FONT_STYLE_DISPLAY_40 = 6
};

const struct bitmap_font *font_atlas_get(enum font_style_id style_id);
const struct bitmap_glyph *font_atlas_lookup_glyph(const struct bitmap_font *font,
                                                   uint32_t codepoint);
uint32_t font_atlas_decode_utf8_next(const char *text, size_t *byte_offset);
int32_t font_atlas_measure_width(const struct bitmap_font *font, const char *text);

#endif
"""

var sourceLines: [String] = []
sourceLines.append("/* Auto-generated by tools/generate_fonts.swift from Inter-VariableFont_opsz,wght.ttf */")
sourceLines.append("#include \"font_atlas.h\"")
sourceLines.append("")
sourceLines.append("#include <stddef.h>")
sourceLines.append("")
sourceLines.append("#define ASCII_CODEPOINT_LIMIT 0x80u /* 128: 1-byte ASCII (0..127) -> multi-byte UTF-8 */")
sourceLines.append("#define UTF8_2BYTE_LEAD_MASK 0xE0u  /* binary 1110 0000: tests 2-byte UTF-8 lead (110x xxxx) */")
sourceLines.append("#define UTF8_2BYTE_LEAD_TAG 0xC0u   /* binary 1100 0000: 2-byte UTF-8 lead tag */")
sourceLines.append("#define UTF8_2BYTE_PAYLOAD_MASK 0x1Fu /* binary 0001 1111: extracts 5 payload bits */")
sourceLines.append("#define UTF8_CONTINUATION_MASK 0xC0u /* binary 1100 0000: tests continuation top 2 bits */")
sourceLines.append("#define UTF8_CONTINUATION_TAG 0x80u  /* binary 1000 0000: continuation byte (10xx xxxx) */")
sourceLines.append("#define UTF8_CONTINUATION_PAYLOAD 0x3Fu /* binary 0011 1111: extracts 6 payload bits */")
sourceLines.append("#define CODEPOINT_SPACE 0x20u        /* U+0020 ' ': SPACE */")
sourceLines.append("")

for baked in bakedFonts {
    let prefix = baked.spec.symbolPrefix
    sourceLines.append("static const uint8_t \(prefix)_alpha_data[] = {")
    if baked.alphaData.isEmpty {
        sourceLines.append("    0u")
    } else {
        var chunk: [String] = []
        for byteValue in baked.alphaData {
            chunk.append("\(byteValue)u")
            if chunk.count == 16 {
                sourceLines.append("    " + chunk.joined(separator: ", ") + ",")
                chunk.removeAll(keepingCapacity: true)
            }
        }
        if !chunk.isEmpty {
            sourceLines.append("    " + chunk.joined(separator: ", "))
        }
    }
    sourceLines.append("};")
    sourceLines.append("")

    sourceLines.append("static const struct bitmap_glyph \(prefix)_glyphs[] = {")
    for glyph in baked.glyphs {
        sourceLines.append("    {\(glyph.codepoint)u, \(glyph.width)u, \(glyph.height)u, \(glyph.offsetX), \(glyph.offsetY), \(glyph.advanceX), \(glyph.bitmapOffset)u},")
    }
    sourceLines.append("};")
    sourceLines.append("")

    sourceLines.append("static const struct bitmap_font \(prefix)_font = {")
    sourceLines.append("    \(baked.lineHeightPx)u, \(baked.ascentPx), \(baked.capHeightPx), \(baked.glyphs.count)u, \(prefix)_glyphs, \(prefix)_alpha_data")
    sourceLines.append("};")
    sourceLines.append("")
}

sourceLines.append("""
const struct bitmap_font *font_atlas_get(enum font_style_id style_id) {
    switch (style_id) {
        case FONT_STYLE_MONO_10:
            return &mono_10_font;
        case FONT_STYLE_MONO_11:
            return &mono_11_font;
        case FONT_STYLE_MONO_13:
            return &mono_13_font;
        case FONT_STYLE_MONO_18:
            return &mono_18_font;
        case FONT_STYLE_DISPLAY_18:
            return &display_18_font;
        case FONT_STYLE_DISPLAY_32:
            return &display_32_font;
        case FONT_STYLE_DISPLAY_40:
            return &display_40_font;
        default:
            return &mono_10_font;
    }
}

const struct bitmap_glyph *font_atlas_lookup_glyph(const struct bitmap_font *font,
                                                   uint32_t codepoint) {
    if (font == NULL || font->glyphs == NULL || font->glyph_count == 0u) {
        return NULL;
    }
    for (uint16_t index = 0u; index < font->glyph_count; index++) {
        if (font->glyphs[index].codepoint == codepoint) {
            return &font->glyphs[index];
        }
    }
    return &font->glyphs[0];
}

uint32_t font_atlas_decode_utf8_next(const char *text, size_t *byte_offset) {
    if (text == NULL || byte_offset == NULL) {
        return 0u;
    }
    size_t cursor = *byte_offset;
    uint8_t first_byte = (uint8_t)text[cursor];
    if (first_byte == 0u) {
        return 0u;
    }
    if (first_byte < ASCII_CODEPOINT_LIMIT) {
        *byte_offset = cursor + 1u;
        return (uint32_t)first_byte;
    }
    if ((first_byte & UTF8_2BYTE_LEAD_MASK) == UTF8_2BYTE_LEAD_TAG) {
        uint8_t second_byte = (uint8_t)text[cursor + 1u];
        if ((second_byte & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION_TAG) {
            *byte_offset = cursor + 2u;
            uint32_t high_bits = (uint32_t)(first_byte & UTF8_2BYTE_PAYLOAD_MASK) << 6u;
            uint32_t low_bits = (uint32_t)(second_byte & UTF8_CONTINUATION_PAYLOAD);
            return high_bits | low_bits;
        }
    }
    *byte_offset = cursor + 1u;
    return CODEPOINT_SPACE;
}

int32_t font_atlas_measure_width(const struct bitmap_font *font, const char *text) {
    if (font == NULL || text == NULL) {
        return 0;
    }
    int32_t total_width = 0;
    size_t byte_offset = 0u;
    while (text[byte_offset] != '\\0') {
        uint32_t codepoint = font_atlas_decode_utf8_next(text, &byte_offset);
        const struct bitmap_glyph *glyph = font_atlas_lookup_glyph(font, codepoint);
        if (glyph != NULL) {
            total_width += (int32_t)glyph->advance_x;
        }
    }
    return total_width;
}
""")

try headerContent.write(toFile: outputHeaderPath, atomically: true, encoding: .utf8)
try sourceLines.joined(separator: "\n").write(toFile: outputSourcePath, atomically: true, encoding: .utf8)

let totalAlphaBytes = bakedFonts.reduce(0) { $0 + $1.alphaData.count }
print("Generated \(outputHeaderPath) and \(outputSourcePath) (\(totalAlphaBytes) alpha bytes total)")
