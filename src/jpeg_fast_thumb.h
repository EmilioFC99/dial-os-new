#ifndef DIALOS_JPEG_FAST_THUMB_H
#define DIALOS_JPEG_FAST_THUMB_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Decode a large baseline JPEG (>= target_size * 8 px per side) at 1/8 scale
 * using only DC coefficients and a ~230 KB accumulator, writing RGB565 pixels.
 * Returns false for progressive JPEGs, small images, or unsupported layouts so
 * the caller can fall back to full decoding.
 */
bool jpeg_fast_thumb_decode_file(const char *file_path, uint32_t data_offset,
                                 uint32_t data_length, uint16_t *rgb565_out,
                                 uint32_t target_size);

#endif
