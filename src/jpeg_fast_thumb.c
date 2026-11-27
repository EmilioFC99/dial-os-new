#include "jpeg_fast_thumb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JPEG_STREAM_BUFFER_BYTES 8192u
#define JPEG_MAX_COMPONENTS 3u
#define JPEG_MAX_HUFFMAN_TABLES 4u
#define JPEG_MAX_QUANT_TABLES 4u
#define JPEG_MAX_SAMPLING_FACTOR 4u

struct jpeg_huffman_table {
    bool valid;
    uint16_t fast_lut[256];
    int32_t min_code[17];
    int32_t max_code[17];
    int32_t val_offset[17];
    uint8_t symbols[256];
};

struct jpeg_component_info {
    uint8_t id;
    uint8_t h_sample;
    uint8_t v_sample;
    uint8_t quant_id;
    uint8_t dc_table_id;
    uint8_t ac_table_id;
};

struct jpeg_stream_reader {
    FILE *file;
    bool has_limit;
    uint32_t remaining_bytes;
    uint8_t buffer[JPEG_STREAM_BUFFER_BYTES];
    size_t buffer_pos;
    size_t buffer_len;
    uint32_t bit_buffer;
    uint32_t bit_count;
    bool hit_marker;
    uint8_t pending_marker;
};

struct thumb_accum_cell {
    uint32_t red_sum;
    uint32_t green_sum;
    uint32_t blue_sum;
    uint32_t sample_count;
};

static bool stream_refill(struct jpeg_stream_reader *reader) {
    if (reader->has_limit && reader->remaining_bytes == 0u) {
        return false;
    }
    size_t to_read = JPEG_STREAM_BUFFER_BYTES;
    if (reader->has_limit && reader->remaining_bytes < to_read) {
        to_read = reader->remaining_bytes;
    }
    size_t bytes_read = fread(reader->buffer, 1u, to_read, reader->file);
    if (bytes_read == 0u) {
        return false;
    }
    if (reader->has_limit) {
        reader->remaining_bytes -= (uint32_t)bytes_read;
    }
    reader->buffer_pos = 0u;
    reader->buffer_len = bytes_read;
    return true;
}

static bool stream_read_u8(struct jpeg_stream_reader *reader, uint8_t *byte_out) {
    if (reader->buffer_pos >= reader->buffer_len) {
        if (!stream_refill(reader)) {
            return false;
        }
    }
    *byte_out = reader->buffer[reader->buffer_pos++];
    return true;
}

static bool stream_read_u16_be(struct jpeg_stream_reader *reader, uint16_t *value_out) {
    uint8_t high_byte = 0u;
    uint8_t low_byte = 0u;
    if (!stream_read_u8(reader, &high_byte) || !stream_read_u8(reader, &low_byte)) {
        return false;
    }
    *value_out = (uint16_t)(((uint16_t)high_byte << 8u) | (uint16_t)low_byte);
    return true;
}

static bool stream_skip_bytes(struct jpeg_stream_reader *reader, uint32_t count) {
    while (count > 0u) {
        if (reader->buffer_pos < reader->buffer_len) {
            size_t avail = reader->buffer_len - reader->buffer_pos;
            size_t take = (count < avail) ? (size_t)count : avail;
            reader->buffer_pos += take;
            count -= (uint32_t)take;
        } else if (!stream_refill(reader)) {
            return false;
        }
    }
    return true;
}

static bool ensure_bits(struct jpeg_stream_reader *reader, uint32_t needed_bits) {
    while (reader->bit_count < needed_bits) {
        uint8_t next_byte = 0u;
        if (reader->hit_marker) {
            reader->bit_buffer <<= 8u;
            reader->bit_count += 8u;
            continue;
        }
        if (!stream_read_u8(reader, &next_byte)) {
            return false;
        }
        if (next_byte == 0xFFu) {
            uint8_t after_ff = 0u;
            do {
                if (!stream_read_u8(reader, &after_ff)) {
                    return false;
                }
            } while (after_ff == 0xFFu);
            if (after_ff != 0x00u) {
                reader->hit_marker = true;
                reader->pending_marker = after_ff;
                reader->bit_buffer <<= 8u;
                reader->bit_count += 8u;
                continue;
            }
        }
        reader->bit_buffer = (reader->bit_buffer << 8u) | (uint32_t)next_byte;
        reader->bit_count += 8u;
    }
    return true;
}

static uint32_t peek_bits(struct jpeg_stream_reader *reader, uint32_t bit_length) {
    return (reader->bit_buffer >> (reader->bit_count - bit_length)) & ((1u << bit_length) - 1u);
}

static void drop_bits(struct jpeg_stream_reader *reader, uint32_t bit_length) {
    reader->bit_count -= bit_length;
    reader->bit_buffer &= (reader->bit_count > 0u) ? ((1u << reader->bit_count) - 1u) : 0u;
}

static bool read_bits(struct jpeg_stream_reader *reader, uint32_t bit_length, uint32_t *bits_out) {
    if (bit_length == 0u) {
        *bits_out = 0u;
        return true;
    }
    if (!ensure_bits(reader, bit_length)) {
        return false;
    }
    *bits_out = peek_bits(reader, bit_length);
    drop_bits(reader, bit_length);
    return true;
}

static int32_t receive_extend(uint32_t raw_bits, uint32_t bit_length) {
    if (bit_length == 0u) {
        return 0;
    }
    uint32_t threshold = 1u << (bit_length - 1u);
    if (raw_bits < threshold) {
        return (int32_t)raw_bits - (int32_t)((1u << bit_length) - 1u);
    }
    return (int32_t)raw_bits;
}

static bool build_huffman_table(struct jpeg_huffman_table *table, const uint8_t counts[16],
                                const uint8_t *symbols, uint32_t total_symbols) {
    memset(table, 0, sizeof(*table));
    if (total_symbols > 256u) {
        return false;
    }
    if (total_symbols > 0u) {
        memcpy(table->symbols, symbols, total_symbols);
    }

    int32_t code = 0;
    int32_t symbol_cursor = 0;
    for (uint32_t bit_len = 1u; bit_len <= 16u; bit_len++) {
        uint8_t count_at_len = counts[bit_len - 1u];
        if (count_at_len == 0u) {
            table->max_code[bit_len] = -1;
        } else {
            table->val_offset[bit_len] = symbol_cursor - code;
            table->min_code[bit_len] = code;
            code += (int32_t)count_at_len;
            table->max_code[bit_len] = code - 1;
            symbol_cursor += (int32_t)count_at_len;
        }
        code <<= 1;
    }

    /* Populate 8-bit fast lookup table for <= 8-bit codes */
    for (uint32_t bit_len = 1u; bit_len <= 8u; bit_len++) {
        if (table->max_code[bit_len] < 0) {
            continue;
        }
        uint32_t shift = 8u - bit_len;
        for (int32_t huff_code = table->min_code[bit_len]; huff_code <= table->max_code[bit_len];
             huff_code++) {
            uint8_t sym = table->symbols[table->val_offset[bit_len] + huff_code];
            uint16_t entry = (uint16_t)(((uint16_t)sym << 5u) | (uint16_t)bit_len);
            uint32_t base_index = (uint32_t)huff_code << shift;
            uint32_t span = 1u << shift;
            for (uint32_t offset = 0u; offset < span; offset++) {
                table->fast_lut[base_index + offset] = entry;
            }
        }
    }

    table->valid = true;
    return true;
}

static bool decode_huffman_symbol(struct jpeg_stream_reader *reader,
                                  const struct jpeg_huffman_table *table, uint8_t *symbol_out) {
    if (!table->valid) {
        return false;
    }
    if (reader->bit_count < 8u && !ensure_bits(reader, 8u)) {
        return false;
    }
    uint32_t peek8 = peek_bits(reader, 8u);
    uint16_t fast_entry = table->fast_lut[peek8];
    if (fast_entry != 0u) {
        uint32_t code_len = fast_entry & 0x1Fu;
        drop_bits(reader, code_len);
        *symbol_out = (uint8_t)(fast_entry >> 5u);
        return true;
    }

    int32_t code = (int32_t)peek8;
    drop_bits(reader, 8u);
    for (uint32_t bit_len = 9u; bit_len <= 16u; bit_len++) {
        uint32_t next_bit = 0u;
        if (!read_bits(reader, 1u, &next_bit)) {
            return false;
        }
        code = (code << 1) | (int32_t)next_bit;
        if (table->max_code[bit_len] >= 0 && code <= table->max_code[bit_len]) {
            int32_t sym_index = table->val_offset[bit_len] + code;
            if (sym_index < 0 || sym_index >= 256) {
                return false;
            }
            *symbol_out = table->symbols[sym_index];
            return true;
        }
    }
    return false;
}

static bool skip_ac_coefficients(struct jpeg_stream_reader *reader,
                                 const struct jpeg_huffman_table *ac_table) {
    uint32_t coeff_index = 1u;
    while (coeff_index < 64u) {
        uint8_t run_size = 0u;
        if (!decode_huffman_symbol(reader, ac_table, &run_size)) {
            return false;
        }
        if (run_size == 0x00u) {
            return true;
        }
        if (run_size == 0xF0u) {
            coeff_index += 16u;
            continue;
        }
        uint32_t zero_run = (uint32_t)(run_size >> 4u);
        uint32_t extra_bits = (uint32_t)(run_size & 0x0Fu);
        coeff_index += zero_run + 1u;
        if (extra_bits > 0u) {
            if (!ensure_bits(reader, extra_bits)) {
                return false;
            }
            drop_bits(reader, extra_bits);
        }
    }
    return true;
}

static bool consume_restart_marker(struct jpeg_stream_reader *reader) {
    reader->bit_buffer = 0u;
    reader->bit_count = 0u;
    if (reader->hit_marker) {
        uint8_t marker = reader->pending_marker;
        reader->hit_marker = false;
        reader->pending_marker = 0u;
        return marker >= 0xD0u && marker <= 0xD7u;
    }
    uint8_t byte_value = 0u;
    while (stream_read_u8(reader, &byte_value)) {
        if (byte_value == 0xFFu) {
            uint8_t marker = 0u;
            do {
                if (!stream_read_u8(reader, &marker)) {
                    return false;
                }
            } while (marker == 0xFFu);
            if (marker >= 0xD0u && marker <= 0xD7u) {
                return true;
            }
            if (marker != 0x00u) {
                return false;
            }
        }
    }
    return false;
}

static uint8_t clamp_u8_i32(int32_t value) {
    if (value < 0) {
        return 0u;
    }
    if (value > 255) {
        return 255u;
    }
    return (uint8_t)value;
}

bool jpeg_fast_thumb_decode_file(const char *file_path, uint32_t data_offset,
                                 uint32_t data_length, uint16_t *rgb565_out,
                                 uint32_t target_size) {
    if (file_path == NULL || rgb565_out == NULL || target_size == 0u) {
        return false;
    }

    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        return false;
    }
    if (data_length > 0u && fseek(file, (long)data_offset, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    struct jpeg_stream_reader reader;
    memset(&reader, 0, sizeof(reader));
    reader.file = file;
    reader.has_limit = (data_length > 0u);
    reader.remaining_bytes = data_length;

    uint16_t soi_marker = 0u;
    if (!stream_read_u16_be(&reader, &soi_marker) || soi_marker != 0xFFD8u) {
        fclose(file);
        return false;
    }

    uint16_t q_dc[JPEG_MAX_QUANT_TABLES] = {0};
    struct jpeg_huffman_table dc_tables[JPEG_MAX_HUFFMAN_TABLES];
    struct jpeg_huffman_table ac_tables[JPEG_MAX_HUFFMAN_TABLES];
    memset(dc_tables, 0, sizeof(dc_tables));
    memset(ac_tables, 0, sizeof(ac_tables));

    struct jpeg_component_info components[JPEG_MAX_COMPONENTS];
    memset(components, 0, sizeof(components));
    uint32_t component_count = 0u;
    uint32_t image_width = 0u;
    uint32_t image_height = 0u;
    uint32_t max_h_sample = 1u;
    uint32_t max_v_sample = 1u;
    uint16_t restart_interval = 0u;
    bool seen_sof = false;
    bool seen_sos = false;

    while (!seen_sos) {
        uint8_t prefix = 0u;
        if (!stream_read_u8(&reader, &prefix)) {
            fclose(file);
            return false;
        }
        if (prefix != 0xFFu) {
            continue;
        }
        uint8_t marker = 0u;
        do {
            if (!stream_read_u8(&reader, &marker)) {
                fclose(file);
                return false;
            }
        } while (marker == 0xFFu);

        if (marker == 0x00u || marker == 0xD8u || marker == 0x01u ||
            (marker >= 0xD0u && marker <= 0xD7u)) {
            continue;
        }
        if (marker == 0xD9u) {
            fclose(file);
            return false;
        }

        uint16_t segment_length = 0u;
        if (!stream_read_u16_be(&reader, &segment_length) || segment_length < 2u) {
            fclose(file);
            return false;
        }
        uint32_t payload_bytes = (uint32_t)(segment_length - 2u);

        if (marker == 0xDBu) { /* DQT */
            while (payload_bytes > 0u) {
                uint8_t precision_id = 0u;
                if (!stream_read_u8(&reader, &precision_id)) {
                    fclose(file);
                    return false;
                }
                payload_bytes--;
                uint8_t precision = (uint8_t)(precision_id >> 4u);
                uint8_t table_id = (uint8_t)(precision_id & 0x0Fu);
                uint32_t entry_bytes = (precision == 0u) ? 1u : 2u;
                if (payload_bytes < 64u * entry_bytes || table_id >= JPEG_MAX_QUANT_TABLES) {
                    fclose(file);
                    return false;
                }
                if (precision == 0u) {
                    uint8_t dc_val = 0u;
                    if (!stream_read_u8(&reader, &dc_val)) {
                        fclose(file);
                        return false;
                    }
                    q_dc[table_id] = dc_val;
                } else {
                    if (!stream_read_u16_be(&reader, &q_dc[table_id])) {
                        fclose(file);
                        return false;
                    }
                }
                if (!stream_skip_bytes(&reader, 63u * entry_bytes)) {
                    fclose(file);
                    return false;
                }
                payload_bytes -= 64u * entry_bytes;
            }
        } else if (marker == 0xC4u) { /* DHT */
            while (payload_bytes > 0u) {
                uint8_t class_id = 0u;
                if (!stream_read_u8(&reader, &class_id) || payload_bytes < 17u) {
                    fclose(file);
                    return false;
                }
                payload_bytes--;
                uint8_t table_class = (uint8_t)((class_id >> 4u) & 1u);
                uint8_t table_id = (uint8_t)(class_id & 0x0Fu);
                uint8_t counts[16];
                uint32_t symbol_total = 0u;
                for (uint32_t idx = 0u; idx < 16u; idx++) {
                    if (!stream_read_u8(&reader, &counts[idx])) {
                        fclose(file);
                        return false;
                    }
                    symbol_total += counts[idx];
                }
                payload_bytes -= 16u;
                if (symbol_total > 256u || payload_bytes < symbol_total ||
                    table_id >= JPEG_MAX_HUFFMAN_TABLES) {
                    fclose(file);
                    return false;
                }
                uint8_t symbols[256];
                for (uint32_t idx = 0u; idx < symbol_total; idx++) {
                    if (!stream_read_u8(&reader, &symbols[idx])) {
                        fclose(file);
                        return false;
                    }
                }
                payload_bytes -= symbol_total;
                struct jpeg_huffman_table *target_table =
                    (table_class == 0u) ? &dc_tables[table_id] : &ac_tables[table_id];
                if (!build_huffman_table(target_table, counts, symbols, symbol_total)) {
                    fclose(file);
                    return false;
                }
            }
        } else if (marker == 0xDDu) { /* DRI */
            if (payload_bytes != 2u || !stream_read_u16_be(&reader, &restart_interval)) {
                fclose(file);
                return false;
            }
        } else if (marker == 0xC0u || marker == 0xC1u) { /* SOF0 / SOF1 baseline */
            if (payload_bytes < 6u) {
                fclose(file);
                return false;
            }
            uint8_t precision = 0u;
            uint16_t raw_height = 0u;
            uint16_t raw_width = 0u;
            uint8_t raw_comps = 0u;
            if (!stream_read_u8(&reader, &precision) ||
                !stream_read_u16_be(&reader, &raw_height) ||
                !stream_read_u16_be(&reader, &raw_width) || !stream_read_u8(&reader, &raw_comps)) {
                fclose(file);
                return false;
            }
            if (precision != 8u || (raw_comps != 1u && raw_comps != 3u) ||
                raw_width < target_size * 8u || raw_height < target_size * 8u ||
                payload_bytes != (uint32_t)(6u + raw_comps * 3u)) {
                fclose(file);
                return false;
            }
            image_width = raw_width;
            image_height = raw_height;
            component_count = raw_comps;
            max_h_sample = 1u;
            max_v_sample = 1u;
            for (uint32_t comp_idx = 0u; comp_idx < component_count; comp_idx++) {
                uint8_t comp_id = 0u;
                uint8_t sampling = 0u;
                uint8_t quant_id = 0u;
                if (!stream_read_u8(&reader, &comp_id) || !stream_read_u8(&reader, &sampling) ||
                    !stream_read_u8(&reader, &quant_id)) {
                    fclose(file);
                    return false;
                }
                uint8_t h_sample = (uint8_t)(sampling >> 4u);
                uint8_t v_sample = (uint8_t)(sampling & 0x0Fu);
                if (h_sample == 0u || h_sample > JPEG_MAX_SAMPLING_FACTOR || v_sample == 0u ||
                    v_sample > JPEG_MAX_SAMPLING_FACTOR || quant_id >= JPEG_MAX_QUANT_TABLES) {
                    fclose(file);
                    return false;
                }
                components[comp_idx].id = comp_id;
                components[comp_idx].h_sample = h_sample;
                components[comp_idx].v_sample = v_sample;
                components[comp_idx].quant_id = quant_id;
                if (h_sample > max_h_sample) {
                    max_h_sample = h_sample;
                }
                if (v_sample > max_v_sample) {
                    max_v_sample = v_sample;
                }
            }
            seen_sof = true;
        } else if (marker == 0xDAu) { /* SOS */
            if (!seen_sof || payload_bytes < 4u) {
                fclose(file);
                return false;
            }
            uint8_t scan_comps = 0u;
            if (!stream_read_u8(&reader, &scan_comps) || scan_comps != component_count ||
                payload_bytes != (uint32_t)(4u + scan_comps * 2u)) {
                fclose(file);
                return false;
            }
            for (uint32_t scan_idx = 0u; scan_idx < scan_comps; scan_idx++) {
                uint8_t comp_id = 0u;
                uint8_t table_sel = 0u;
                if (!stream_read_u8(&reader, &comp_id) || !stream_read_u8(&reader, &table_sel)) {
                    fclose(file);
                    return false;
                }
                bool matched = false;
                for (uint32_t comp_idx = 0u; comp_idx < component_count; comp_idx++) {
                    if (components[comp_idx].id == comp_id) {
                        components[comp_idx].dc_table_id = (uint8_t)(table_sel >> 4u);
                        components[comp_idx].ac_table_id = (uint8_t)(table_sel & 0x0Fu);
                        if (components[comp_idx].dc_table_id >= JPEG_MAX_HUFFMAN_TABLES ||
                            components[comp_idx].ac_table_id >= JPEG_MAX_HUFFMAN_TABLES) {
                            fclose(file);
                            return false;
                        }
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    fclose(file);
                    return false;
                }
            }
            if (!stream_skip_bytes(&reader, 3u)) {
                fclose(file);
                return false;
            }
            seen_sos = true;
        } else if (marker >= 0xC2u && marker <= 0xCFu && marker != 0xC4u && marker != 0xCCu) {
            /* Progressive / lossless / arithmetic SOF markers are unsupported here */
            fclose(file);
            return false;
        } else {
            if (!stream_skip_bytes(&reader, payload_bytes)) {
                fclose(file);
                return false;
            }
        }
    }

    size_t accum_count = (size_t)target_size * (size_t)target_size;
    struct thumb_accum_cell *accum = calloc(accum_count, sizeof(struct thumb_accum_cell));
    if (accum == NULL) {
        fclose(file);
        return false;
    }

    uint32_t mcu_width_px = max_h_sample * 8u;
    uint32_t mcu_height_px = max_v_sample * 8u;
    uint32_t mcu_cols = (image_width + mcu_width_px - 1u) / mcu_width_px;
    uint32_t mcu_rows = (image_height + mcu_height_px - 1u) / mcu_height_px;
    uint32_t total_mcus = mcu_cols * mcu_rows;

    int32_t dc_pred[JPEG_MAX_COMPONENTS] = {0, 0, 0};
    uint8_t block_dc[JPEG_MAX_COMPONENTS][JPEG_MAX_SAMPLING_FACTOR * JPEG_MAX_SAMPLING_FACTOR];

    for (uint32_t mcu_index = 0u; mcu_index < total_mcus; mcu_index++) {
        if (restart_interval > 0u && mcu_index > 0u && (mcu_index % restart_interval) == 0u) {
            if (!consume_restart_marker(&reader)) {
                free(accum);
                fclose(file);
                return false;
            }
            dc_pred[0] = 0;
            dc_pred[1] = 0;
            dc_pred[2] = 0;
        }

        for (uint32_t comp_idx = 0u; comp_idx < component_count; comp_idx++) {
            const struct jpeg_component_info *comp = &components[comp_idx];
            const struct jpeg_huffman_table *dc_table = &dc_tables[comp->dc_table_id];
            const struct jpeg_huffman_table *ac_table = &ac_tables[comp->ac_table_id];
            int32_t quant_dc = (int32_t)q_dc[comp->quant_id];
            uint32_t block_count = (uint32_t)comp->h_sample * (uint32_t)comp->v_sample;

            for (uint32_t block_idx = 0u; block_idx < block_count; block_idx++) {
                uint8_t category = 0u;
                if (!decode_huffman_symbol(&reader, dc_table, &category) || category > 15u) {
                    free(accum);
                    fclose(file);
                    return false;
                }
                uint32_t extra_bits = 0u;
                if (!read_bits(&reader, category, &extra_bits)) {
                    free(accum);
                    fclose(file);
                    return false;
                }
                dc_pred[comp_idx] += receive_extend(extra_bits, category);
                int32_t level = ((dc_pred[comp_idx] * quant_dc) >> 3) + 128;
                block_dc[comp_idx][block_idx] = clamp_u8_i32(level);

                if (!skip_ac_coefficients(&reader, ac_table)) {
                    free(accum);
                    fclose(file);
                    return false;
                }
            }
        }

        uint32_t mcu_x = mcu_index % mcu_cols;
        uint32_t mcu_y = mcu_index / mcu_cols;
        for (uint32_t sub_y = 0u; sub_y < max_v_sample; sub_y++) {
            uint32_t pixel_y = mcu_y * mcu_height_px + sub_y * 8u + 4u;
            if (pixel_y >= image_height) {
                pixel_y = image_height - 1u;
            }
            uint32_t dest_y = (pixel_y * target_size) / image_height;
            if (dest_y >= target_size) {
                dest_y = target_size - 1u;
            }
            for (uint32_t sub_x = 0u; sub_x < max_h_sample; sub_x++) {
                uint32_t pixel_x = mcu_x * mcu_width_px + sub_x * 8u + 4u;
                if (pixel_x >= image_width) {
                    pixel_x = image_width - 1u;
                }
                uint32_t dest_x = (pixel_x * target_size) / image_width;
                if (dest_x >= target_size) {
                    dest_x = target_size - 1u;
                }

                uint8_t red8 = 0u;
                uint8_t green8 = 0u;
                uint8_t blue8 = 0u;
                if (component_count == 1u) {
                    uint32_t b_x = (sub_x * components[0].h_sample) / max_h_sample;
                    uint32_t b_y = (sub_y * components[0].v_sample) / max_v_sample;
                    uint8_t luma = block_dc[0][b_y * components[0].h_sample + b_x];
                    red8 = luma;
                    green8 = luma;
                    blue8 = luma;
                } else {
                    uint32_t y_bx = (sub_x * components[0].h_sample) / max_h_sample;
                    uint32_t y_by = (sub_y * components[0].v_sample) / max_v_sample;
                    uint32_t cb_bx = (sub_x * components[1].h_sample) / max_h_sample;
                    uint32_t cb_by = (sub_y * components[1].v_sample) / max_v_sample;
                    uint32_t cr_bx = (sub_x * components[2].h_sample) / max_h_sample;
                    uint32_t cr_by = (sub_y * components[2].v_sample) / max_v_sample;

                    int32_t luma = (int32_t)block_dc[0][y_by * components[0].h_sample + y_bx];
                    int32_t chroma_b =
                        (int32_t)block_dc[1][cb_by * components[1].h_sample + cb_bx] - 128;
                    int32_t chroma_r =
                        (int32_t)block_dc[2][cr_by * components[2].h_sample + cr_bx] - 128;

                    red8 = clamp_u8_i32(luma + ((91881 * chroma_r + 32768) >> 16));
                    green8 = clamp_u8_i32(luma - ((22554 * chroma_b + 46802 * chroma_r + 32768) >> 16));
                    blue8 = clamp_u8_i32(luma + ((116130 * chroma_b + 32768) >> 16));
                }

                struct thumb_accum_cell *cell = &accum[(size_t)dest_y * target_size + dest_x];
                cell->red_sum += red8;
                cell->green_sum += green8;
                cell->blue_sum += blue8;
                cell->sample_count++;
            }
        }
    }

    fclose(file);

    uint16_t last_valid_rgb565 = 0u;
    for (size_t idx = 0u; idx < accum_count; idx++) {
        if (accum[idx].sample_count > 0u) {
            uint32_t red8 = accum[idx].red_sum / accum[idx].sample_count;
            uint32_t green8 = accum[idx].green_sum / accum[idx].sample_count;
            uint32_t blue8 = accum[idx].blue_sum / accum[idx].sample_count;
            last_valid_rgb565 =
                (uint16_t)(((red8 >> 3u) << 11u) | ((green8 >> 2u) << 5u) | (blue8 >> 3u));
            rgb565_out[idx] = last_valid_rgb565;
        } else {
            rgb565_out[idx] = last_valid_rgb565;
        }
    }

    free(accum);
    return true;
}
