#include "library_scanner.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define FLAC_METADATA_LAST_MASK 0x80u
#define FLAC_BLOCK_TYPE_MASK 0x7Fu
#define FLAC_BLOCK_STREAMINFO 0u
#define FLAC_BLOCK_VORBIS_COMMENT 4u
#define FLAC_BLOCK_PICTURE 6u
#define FLAC_PICTURE_FRONT_COVER 3u

#define ID3_EXTENDED_HEADER_MASK 0x40u
#define ID3_SYNCHSAFE_MASK 0x7Fu
#define ID3_ENCODING_ISO8859 0u
#define ID3_ENCODING_UTF16_BOM 1u
#define ID3_ENCODING_UTF16_BE 2u
#define ID3_ENCODING_UTF8 3u

#define MP3_SYNC_SCAN_LIMIT 65536u
#define MAX_METADATA_READ_BYTES (256u * 1024u)

static uint64_t monotonic_clock_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ull;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static bool ends_with_ci(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (text_len < suffix_len) {
        return false;
    }
    return library_compare_ascii_casefold(text + text_len - suffix_len, suffix) == 0;
}

static uint8_t classify_audio_extension(const char *filename) {
    if (ends_with_ci(filename, ".flac")) {
        return LIBRARY_FORMAT_FLAC;
    }
    if (ends_with_ci(filename, ".mp3")) {
        return LIBRARY_FORMAT_MP3;
    }
    if (ends_with_ci(filename, ".m4a")) {
        return LIBRARY_FORMAT_M4A;
    }
    if (ends_with_ci(filename, ".wav")) {
        return LIBRARY_FORMAT_WAV;
    }
    if (ends_with_ci(filename, ".ogg") || ends_with_ci(filename, ".opus") ||
        ends_with_ci(filename, ".aiff") || ends_with_ci(filename, ".aif") ||
        ends_with_ci(filename, ".ape") || ends_with_ci(filename, ".dsf") ||
        ends_with_ci(filename, ".dff")) {
        return LIBRARY_FORMAT_OTHER;
    }
    return LIBRARY_FORMAT_UNKNOWN;
}

static bool is_m3u_filename(const char *filename) {
    return ends_with_ci(filename, ".m3u") || ends_with_ci(filename, ".m3u8");
}

static bool should_skip_directory_name(const char *name) {
    if (name == NULL || name[0] == '\0' || name[0] == '.') {
        return true;
    }
    if (library_compare_ascii_casefold(name, "System Volume Information") == 0 ||
        library_compare_ascii_casefold(name, "LOST.DIR") == 0 ||
        library_compare_ascii_casefold(name, "recovery") == 0) {
        return true;
    }
    return false;
}

static void copy_string_bounded(char *dest, size_t capacity, const char *source, size_t max_len) {
    if (capacity == 0u) {
        return;
    }
    size_t count = 0u;
    if (source != NULL) {
        while (count < max_len && count + 1u < capacity && source[count] != '\0') {
            dest[count] = source[count];
            count++;
        }
    }
    dest[count] = '\0';
}

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | (uint16_t)bytes[1]);
}

static uint32_t read_be24(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 16u) | ((uint32_t)bytes[1] << 8u) | (uint32_t)bytes[2];
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | (uint32_t)bytes[3];
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) | ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

static uint16_t parse_leading_u16(const char *text) {
    if (text == NULL) {
        return 0u;
    }
    while (*text != '\0' && !isdigit((unsigned char)*text)) {
        text++;
    }
    uint32_t value = 0u;
    while (isdigit((unsigned char)*text)) {
        value = value * 10u + (uint32_t)(*text - '0');
        if (value > 65535u) {
            return 0u;
        }
        text++;
    }
    return (uint16_t)value;
}

static uint16_t parse_year_u16(const char *text) {
    if (text == NULL) {
        return 0u;
    }
    for (size_t idx = 0u; text[idx] != '\0'; idx++) {
        if (isdigit((unsigned char)text[idx]) && isdigit((unsigned char)text[idx + 1u]) &&
            isdigit((unsigned char)text[idx + 2u]) && isdigit((unsigned char)text[idx + 3u])) {
            uint32_t year = (uint32_t)(text[idx] - '0') * 1000u +
                            (uint32_t)(text[idx + 1u] - '0') * 100u +
                            (uint32_t)(text[idx + 2u] - '0') * 10u +
                            (uint32_t)(text[idx + 3u] - '0');
            if (year >= 1900u && year <= 2100u) {
                return (uint16_t)year;
            }
        }
    }
    return 0u;
}

static void decode_latin1_to_utf8(const uint8_t *source, size_t length, char *output,
                                  size_t capacity) {
    size_t out_idx = 0u;
    for (size_t idx = 0u; idx < length && source[idx] != 0u; idx++) {
        uint8_t byte_val = source[idx];
        if (byte_val < 0x80u) {
            if (out_idx + 1u >= capacity) {
                break;
            }
            output[out_idx++] = (char)byte_val;
        } else {
            if (out_idx + 2u >= capacity) {
                break;
            }
            output[out_idx++] = (char)(0xC0u | (byte_val >> 6u));
            output[out_idx++] = (char)(0x80u | (byte_val & 0x3Fu));
        }
    }
    output[out_idx] = '\0';
}

static void decode_utf16_to_utf8(const uint8_t *source, size_t length, bool big_endian,
                                 char *output, size_t capacity) {
    size_t pos = 0u;
    if (length >= 2u && source[0] == 0xFEu && source[1] == 0xFFu) {
        big_endian = true;
        pos = 2u;
    } else if (length >= 2u && source[0] == 0xFFu && source[1] == 0xFEu) {
        big_endian = false;
        pos = 2u;
    }
    size_t out_idx = 0u;
    while (pos + 1u < length) {
        uint16_t code_unit = big_endian
                                 ? (uint16_t)(((uint16_t)source[pos] << 8u) | source[pos + 1u])
                                 : (uint16_t)((uint16_t)source[pos] | ((uint16_t)source[pos + 1u] << 8u));
        pos += 2u;
        if (code_unit == 0u) {
            break;
        }
        uint32_t codepoint = code_unit;
        if (code_unit >= 0xD800u && code_unit <= 0xDBFFu && pos + 1u < length) {
            uint16_t low_unit =
                big_endian
                    ? (uint16_t)(((uint16_t)source[pos] << 8u) | source[pos + 1u])
                    : (uint16_t)((uint16_t)source[pos] | ((uint16_t)source[pos + 1u] << 8u));
            if (low_unit >= 0xDC00u && low_unit <= 0xDFFFu) {
                codepoint = 0x10000u + (((uint32_t)(code_unit - 0xD800u) << 10u) |
                                        (uint32_t)(low_unit - 0xDC00u));
                pos += 2u;
            }
        }
        if (codepoint < 0x80u) {
            if (out_idx + 1u >= capacity) {
                break;
            }
            output[out_idx++] = (char)codepoint;
        } else if (codepoint < 0x800u) {
            if (out_idx + 2u >= capacity) {
                break;
            }
            output[out_idx++] = (char)(0xC0u | (codepoint >> 6u));
            output[out_idx++] = (char)(0x80u | (codepoint & 0x3Fu));
        } else if (codepoint < 0x10000u) {
            if (out_idx + 3u >= capacity) {
                break;
            }
            output[out_idx++] = (char)(0xE0u | (codepoint >> 12u));
            output[out_idx++] = (char)(0x80u | ((codepoint >> 6u) & 0x3Fu));
            output[out_idx++] = (char)(0x80u | (codepoint & 0x3Fu));
        } else {
            if (out_idx + 4u >= capacity) {
                break;
            }
            output[out_idx++] = (char)(0xF0u | (codepoint >> 18u));
            output[out_idx++] = (char)(0x80u | ((codepoint >> 12u) & 0x3Fu));
            output[out_idx++] = (char)(0x80u | ((codepoint >> 6u) & 0x3Fu));
            output[out_idx++] = (char)(0x80u | (codepoint & 0x3Fu));
        }
    }
    output[out_idx] = '\0';
}

struct parsed_tag_fields {
    char title[160];
    char artist[160];
    char album_artist[160];
    char album[160];
    char genre[160];
    uint16_t year;
    uint16_t disc_number;
    uint16_t track_number;
    uint32_t duration_seconds;
    uint8_t cover_kind;
    uint32_t cover_offset;
    uint32_t cover_length;
};

static void apply_tag_pair(struct parsed_tag_fields *fields, const char *key, const char *value) {
    if (value == NULL || value[0] == '\0') {
        return;
    }
    if (library_compare_ascii_casefold(key, "TITLE") == 0) {
        copy_string_bounded(fields->title, sizeof(fields->title), value, sizeof(fields->title) - 1u);
    } else if (library_compare_ascii_casefold(key, "ARTIST") == 0) {
        copy_string_bounded(fields->artist, sizeof(fields->artist), value,
                            sizeof(fields->artist) - 1u);
    } else if (library_compare_ascii_casefold(key, "ALBUMARTIST") == 0 ||
               library_compare_ascii_casefold(key, "ALBUM ARTIST") == 0) {
        copy_string_bounded(fields->album_artist, sizeof(fields->album_artist), value,
                            sizeof(fields->album_artist) - 1u);
    } else if (library_compare_ascii_casefold(key, "ALBUM") == 0) {
        copy_string_bounded(fields->album, sizeof(fields->album), value, sizeof(fields->album) - 1u);
    } else if (library_compare_ascii_casefold(key, "GENRE") == 0) {
        copy_string_bounded(fields->genre, sizeof(fields->genre), value, sizeof(fields->genre) - 1u);
    } else if (library_compare_ascii_casefold(key, "TRACKNUMBER") == 0) {
        fields->track_number = parse_leading_u16(value);
    } else if (library_compare_ascii_casefold(key, "DISCNUMBER") == 0) {
        fields->disc_number = parse_leading_u16(value);
    } else if (library_compare_ascii_casefold(key, "DATE") == 0 ||
               library_compare_ascii_casefold(key, "YEAR") == 0) {
        uint16_t parsed = parse_year_u16(value);
        if (parsed != 0u) {
            fields->year = parsed;
        }
    }
}

static void parse_flac_file(FILE *file, struct parsed_tag_fields *fields) {
    uint8_t magic[4];
    if (fread(magic, 1u, 4u, file) != 4u || memcmp(magic, "fLaC", 4u) != 0) {
        return;
    }
    for (uint32_t block_iter = 0u; block_iter < 64u; block_iter++) {
        uint8_t block_hdr[4];
        if (fread(block_hdr, 1u, 4u, file) != 4u) {
            break;
        }
        bool is_last = (block_hdr[0] & FLAC_METADATA_LAST_MASK) != 0u;
        uint8_t block_type = block_hdr[0] & FLAC_BLOCK_TYPE_MASK;
        uint32_t block_size = read_be24(block_hdr + 1u);
        long block_start = ftell(file);
        if (block_start < 0) {
            break;
        }

        if (block_type == FLAC_BLOCK_STREAMINFO && block_size >= 18u) {
            uint8_t info[18];
            if (fread(info, 1u, 18u, file) == 18u) {
                uint32_t sample_rate = ((uint32_t)info[10] << 12u) | ((uint32_t)info[11] << 4u) |
                                       ((uint32_t)info[12] >> 4u);
                uint64_t total_samples = ((uint64_t)(info[13] & 0x0Fu) << 32u) |
                                         ((uint64_t)info[14] << 24u) | ((uint64_t)info[15] << 16u) |
                                         ((uint64_t)info[16] << 8u) | (uint64_t)info[17];
                if (sample_rate > 0u && total_samples > 0u) {
                    fields->duration_seconds = (uint32_t)((total_samples + sample_rate / 2u) / sample_rate);
                }
            }
        } else if (block_type == FLAC_BLOCK_VORBIS_COMMENT && block_size >= 8u &&
                   block_size <= MAX_METADATA_READ_BYTES) {
            uint8_t *vc_data = malloc(block_size);
            if (vc_data != NULL) {
                if (fread(vc_data, 1u, block_size, file) == block_size) {
                    size_t pos = 0u;
                    uint32_t vendor_len = read_le32(vc_data);
                    pos += 4u;
                    if (pos + vendor_len + 4u <= block_size) {
                        pos += vendor_len;
                        uint32_t comment_count = read_le32(vc_data + pos);
                        pos += 4u;
                        for (uint32_t idx = 0u; idx < comment_count && pos + 4u <= block_size; idx++) {
                            uint32_t entry_len = read_le32(vc_data + pos);
                            pos += 4u;
                            if (entry_len > block_size - pos) {
                                break;
                            }
                            const char *entry = (const char *)(vc_data + pos);
                            const void *eq = memchr(entry, '=', entry_len);
                            if (eq != NULL) {
                                size_t key_len = (size_t)((const char *)eq - entry);
                                size_t val_len = entry_len - key_len - 1u;
                                char key_buf[64];
                                char val_buf[160];
                                copy_string_bounded(key_buf, sizeof(key_buf), entry, key_len);
                                copy_string_bounded(val_buf, sizeof(val_buf), (const char *)eq + 1u,
                                                    val_len);
                                apply_tag_pair(fields, key_buf, val_buf);
                            }
                            pos += entry_len;
                        }
                    }
                }
                free(vc_data);
            }
        } else if (block_type == FLAC_BLOCK_PICTURE && block_size >= 32u) {
            uint8_t pic_hdr[32];
            if (fread(pic_hdr, 1u, 32u, file) == 32u) {
                uint32_t pic_type = read_be32(pic_hdr);
                uint32_t mime_len = read_be32(pic_hdr + 4u);
                if (mime_len <= 128u && 8u + mime_len + 20u <= block_size) {
                    if (fseek(file, block_start + 8L + (long)mime_len + 4L, SEEK_SET) == 0) {
                        uint8_t desc_len_buf[4];
                        if (fread(desc_len_buf, 1u, 4u, file) == 4u) {
                            uint32_t desc_len = read_be32(desc_len_buf);
                            long image_meta_pos = block_start + 8L + (long)mime_len + 8L + (long)desc_len + 16L;
                            if (image_meta_pos + 4L <= block_start + (long)block_size &&
                                fseek(file, image_meta_pos, SEEK_SET) == 0) {
                                uint8_t img_len_buf[4];
                                if (fread(img_len_buf, 1u, 4u, file) == 4u) {
                                    uint32_t img_len = read_be32(img_len_buf);
                                    uint32_t img_offset = (uint32_t)(image_meta_pos + 4L);
                                    if (img_len > 0u &&
                                        img_offset + img_len <= (uint32_t)(block_start + (long)block_size)) {
                                        if (fields->cover_kind == LIBRARY_COVER_NONE ||
                                            pic_type == FLAC_PICTURE_FRONT_COVER) {
                                            fields->cover_kind = LIBRARY_COVER_EMBEDDED_FLAC;
                                            fields->cover_offset = img_offset;
                                            fields->cover_length = img_len;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (is_last || fseek(file, block_start + (long)block_size, SEEK_SET) != 0) {
            break;
        }
    }
}

static void decode_id3_text(const uint8_t *payload, size_t length, char *output, size_t capacity) {
    if (length == 0u || capacity == 0u) {
        if (capacity > 0u) {
            output[0] = '\0';
        }
        return;
    }
    uint8_t encoding = payload[0];
    const uint8_t *text_bytes = payload + 1u;
    size_t text_len = length - 1u;
    if (encoding == ID3_ENCODING_UTF8) {
        copy_string_bounded(output, capacity, (const char *)text_bytes, text_len);
    } else if (encoding == ID3_ENCODING_UTF16_BOM) {
        decode_utf16_to_utf8(text_bytes, text_len, false, output, capacity);
    } else if (encoding == ID3_ENCODING_UTF16_BE) {
        decode_utf16_to_utf8(text_bytes, text_len, true, output, capacity);
    } else {
        decode_latin1_to_utf8(text_bytes, text_len, output, capacity);
    }
}

static void parse_mp3_file(FILE *file, uint64_t file_size_bytes, struct parsed_tag_fields *fields) {
    uint8_t id3_hdr[10];
    long audio_start = 0L;
    if (fread(id3_hdr, 1u, 10u, file) == 10u && memcmp(id3_hdr, "ID3", 3u) == 0) {
        uint8_t version = id3_hdr[3];
        uint8_t flags = id3_hdr[5];
        uint32_t tag_size = ((uint32_t)(id3_hdr[6] & ID3_SYNCHSAFE_MASK) << 21u) |
                            ((uint32_t)(id3_hdr[7] & ID3_SYNCHSAFE_MASK) << 14u) |
                            ((uint32_t)(id3_hdr[8] & ID3_SYNCHSAFE_MASK) << 7u) |
                            (uint32_t)(id3_hdr[9] & ID3_SYNCHSAFE_MASK);
        audio_start = 10L + (long)tag_size;

        size_t pos = 0u;
        if ((flags & ID3_EXTENDED_HEADER_MASK) != 0u && tag_size >= 4u) {
            uint8_t ext[4];
            if (fread(ext, 1u, 4u, file) == 4u) {
                uint32_t ext_size = (version == 4u)
                                        ? (((uint32_t)(ext[0] & 0x7Fu) << 21u) |
                                           ((uint32_t)(ext[1] & 0x7Fu) << 14u) |
                                           ((uint32_t)(ext[2] & 0x7Fu) << 7u) |
                                           (uint32_t)(ext[3] & 0x7Fu))
                                        : read_be32(ext);
                pos = (size_t)(ext_size + (version == 3u ? 4u : 0u));
                (void)fseek(file, 10L + (long)pos, SEEK_SET);
            }
        }

        size_t hdr_len = (version == 2u) ? 6u : 10u;
        while (pos + hdr_len <= tag_size) {
            uint8_t frame_hdr[10];
            if (fread(frame_hdr, 1u, hdr_len, file) != hdr_len || frame_hdr[0] == 0u) {
                break;
            }
            char frame_id[5] = {0};
            uint32_t frame_size = 0u;
            if (version == 2u) {
                memcpy(frame_id, frame_hdr, 3u);
                frame_size = read_be24(frame_hdr + 3u);
            } else {
                memcpy(frame_id, frame_hdr, 4u);
                if (version == 4u) {
                    frame_size = ((uint32_t)(frame_hdr[4] & 0x7Fu) << 21u) |
                                 ((uint32_t)(frame_hdr[5] & 0x7Fu) << 14u) |
                                 ((uint32_t)(frame_hdr[6] & 0x7Fu) << 7u) |
                                 (uint32_t)(frame_hdr[7] & 0x7Fu);
                } else {
                    frame_size = read_be32(frame_hdr + 4u);
                }
            }
            long frame_data_pos = ftell(file);
            if (frame_data_pos < 0 || frame_size == 0u || pos + hdr_len + frame_size > tag_size) {
                break;
            }

            if (strcmp(frame_id, "APIC") == 0 && frame_size > 8u) {
                uint8_t preview[256];
                size_t to_read = frame_size < sizeof(preview) ? frame_size : sizeof(preview);
                if (fread(preview, 1u, to_read, file) == to_read) {
                    uint8_t enc = preview[0];
                    size_t cursor = 1u;
                    while (cursor < to_read && preview[cursor] != 0u) {
                        cursor++;
                    }
                    cursor++; /* NUL after MIME */
                    if (cursor < to_read) {
                        uint8_t pic_type = preview[cursor++];
                        if (enc == ID3_ENCODING_UTF16_BOM || enc == ID3_ENCODING_UTF16_BE) {
                            while (cursor + 1u < to_read &&
                                   !(preview[cursor] == 0u && preview[cursor + 1u] == 0u)) {
                                cursor += 2u;
                            }
                            cursor += 2u;
                        } else {
                            while (cursor < to_read && preview[cursor] != 0u) {
                                cursor++;
                            }
                            cursor++;
                        }
                        if (cursor < frame_size &&
                            (fields->cover_kind == LIBRARY_COVER_NONE || pic_type == 3u)) {
                            fields->cover_kind = LIBRARY_COVER_EMBEDDED_MP3;
                            fields->cover_offset = (uint32_t)(frame_data_pos + (long)cursor);
                            fields->cover_length = frame_size - (uint32_t)cursor;
                        }
                    }
                }
            } else if (frame_size <= 1024u) {
                uint8_t frame_payload[1024];
                if (fread(frame_payload, 1u, frame_size, file) == frame_size) {
                    char text[160];
                    decode_id3_text(frame_payload, frame_size, text, sizeof(text));
                    if (strcmp(frame_id, "TIT2") == 0 || strcmp(frame_id, "TT2") == 0) {
                        apply_tag_pair(fields, "TITLE", text);
                    } else if (strcmp(frame_id, "TPE1") == 0 || strcmp(frame_id, "TP1") == 0) {
                        apply_tag_pair(fields, "ARTIST", text);
                    } else if (strcmp(frame_id, "TPE2") == 0 || strcmp(frame_id, "TP2") == 0) {
                        apply_tag_pair(fields, "ALBUMARTIST", text);
                    } else if (strcmp(frame_id, "TALB") == 0 || strcmp(frame_id, "TAL") == 0) {
                        apply_tag_pair(fields, "ALBUM", text);
                    } else if (strcmp(frame_id, "TCON") == 0 || strcmp(frame_id, "TCO") == 0) {
                        apply_tag_pair(fields, "GENRE", text);
                    } else if (strcmp(frame_id, "TRCK") == 0 || strcmp(frame_id, "TRK") == 0) {
                        apply_tag_pair(fields, "TRACKNUMBER", text);
                    } else if (strcmp(frame_id, "TPOS") == 0 || strcmp(frame_id, "TPA") == 0) {
                        apply_tag_pair(fields, "DISCNUMBER", text);
                    } else if (strcmp(frame_id, "TDRC") == 0 || strcmp(frame_id, "TYER") == 0 ||
                               strcmp(frame_id, "TYE") == 0) {
                        apply_tag_pair(fields, "DATE", text);
                    }
                }
            }

            pos += hdr_len + frame_size;
            if (fseek(file, frame_data_pos + (long)frame_size, SEEK_SET) != 0) {
                break;
            }
        }
    }

    /* MPEG duration estimate from first frame header */
    if (fseek(file, audio_start, SEEK_SET) == 0) {
        uint8_t scan_buf[4096];
        size_t bytes_read = fread(scan_buf, 1u, sizeof(scan_buf), file);
        static const uint16_t MPEG1_L3_BITRATES[16] = {0u, 32u, 40u, 48u, 56u, 64u, 80u, 96u,
                                                       112u, 128u, 160u, 192u, 224u, 256u, 320u, 0u};
        static const uint16_t MPEG2_L3_BITRATES[16] = {0u, 8u, 16u, 24u, 32u, 40u, 48u, 56u,
                                                       64u, 80u, 96u, 112u, 128u, 144u, 160u, 0u};
        static const uint32_t MPEG1_SR[4] = {44100u, 48000u, 32000u, 0u};
        for (size_t idx = 0u; idx + 4u <= bytes_read; idx++) {
            if (scan_buf[idx] == 0xFFu && (scan_buf[idx + 1u] & 0xE0u) == 0xE0u) {
                uint8_t ver_id = (scan_buf[idx + 1u] >> 3u) & 0x03u;
                uint8_t layer = (scan_buf[idx + 1u] >> 1u) & 0x03u;
                uint8_t br_idx = (scan_buf[idx + 2u] >> 4u) & 0x0Fu;
                uint8_t sr_idx = (scan_buf[idx + 2u] >> 2u) & 0x03u;
                if (ver_id != 1u && layer == 1u && br_idx > 0u && br_idx < 15u && sr_idx < 3u) {
                    uint32_t bitrate_kbps = (ver_id == 3u) ? MPEG1_L3_BITRATES[br_idx]
                                                           : MPEG2_L3_BITRATES[br_idx];
                    uint32_t sr = MPEG1_SR[sr_idx] >> (ver_id == 3u ? 0u : (ver_id == 2u ? 1u : 2u));
                    if (bitrate_kbps > 0u && sr > 0u && file_size_bytes > (uint64_t)audio_start) {
                        uint64_t audio_bytes = file_size_bytes - (uint64_t)audio_start;
                        fields->duration_seconds =
                            (uint32_t)((audio_bytes * 8ull) / ((uint64_t)bitrate_kbps * 1000ull));
                        break;
                    }
                }
            }
        }
    }
}

static void parse_m4a_atoms(FILE *file, long end_offset, struct parsed_tag_fields *fields,
                            uint32_t depth) {
    if (depth > 8u) {
        return;
    }
    while (ftell(file) + 8L <= end_offset) {
        long atom_start = ftell(file);
        uint8_t hdr[8];
        if (fread(hdr, 1u, 8u, file) != 8u) {
            break;
        }
        uint64_t atom_size = read_be32(hdr);
        long header_bytes = 8L;
        if (atom_size == 1u) {
            uint8_t ext[8];
            if (fread(ext, 1u, 8u, file) != 8u) {
                break;
            }
            atom_size = ((uint64_t)read_be32(ext) << 32u) | (uint64_t)read_be32(ext + 4u);
            header_bytes = 16L;
        } else if (atom_size == 0u) {
            atom_size = (uint64_t)(end_offset - atom_start);
        }
        if (atom_size < (uint64_t)header_bytes) {
            break;
        }
        long atom_end = atom_start + (long)atom_size;
        if (atom_end > end_offset) {
            atom_end = end_offset;
        }
        char atom_name[5] = {(char)hdr[4], (char)hdr[5], (char)hdr[6], (char)hdr[7], '\0'};

        if (strcmp(atom_name, "moov") == 0 || strcmp(atom_name, "udta") == 0 ||
            strcmp(atom_name, "ilst") == 0) {
            parse_m4a_atoms(file, atom_end, fields, depth + 1u);
        } else if (strcmp(atom_name, "meta") == 0) {
            if (fseek(file, 4L, SEEK_CUR) == 0) {
                parse_m4a_atoms(file, atom_end, fields, depth + 1u);
            }
        } else if (strcmp(atom_name, "mvhd") == 0 && atom_end - ftell(file) >= 20L) {
            uint8_t mvhd[20];
            if (fread(mvhd, 1u, 20u, file) == 20u && mvhd[0] == 0u) {
                uint32_t timescale = read_be32(mvhd + 12u);
                uint32_t duration = read_be32(mvhd + 16u);
                if (timescale > 0u) {
                    fields->duration_seconds = duration / timescale;
                }
            }
        } else if (atom_end - ftell(file) >= 16L) {
            /* Check for 'data' child inside ilst item */
            long data_hdr_pos = ftell(file);
            uint8_t data_hdr[16];
            if (fread(data_hdr, 1u, 16u, file) == 16u && memcmp(data_hdr + 4u, "data", 4u) == 0) {
                uint32_t data_atom_size = read_be32(data_hdr);
                long payload_len = (long)data_atom_size - 16L;
                if (payload_len > 0L && ftell(file) + payload_len <= atom_end) {
                    if (strcmp(atom_name, "covr") == 0) {
                        fields->cover_kind = LIBRARY_COVER_EMBEDDED_M4A;
                        fields->cover_offset = (uint32_t)ftell(file);
                        fields->cover_length = (uint32_t)payload_len;
                    } else if (strcmp(atom_name, "trkn") == 0 && payload_len >= 4L) {
                        uint8_t trkn[4];
                        if (fread(trkn, 1u, 4u, file) == 4u) {
                            fields->track_number = read_be16(trkn + 2u);
                        }
                    } else if (strcmp(atom_name, "disk") == 0 && payload_len >= 4L) {
                        uint8_t disk[4];
                        if (fread(disk, 1u, 4u, file) == 4u) {
                            fields->disc_number = read_be16(disk + 2u);
                        }
                    } else if (payload_len <= 256L) {
                        char text[160];
                        size_t to_read = (size_t)payload_len < sizeof(text) - 1u
                                             ? (size_t)payload_len
                                             : sizeof(text) - 1u;
                        if (fread(text, 1u, to_read, file) == to_read) {
                            text[to_read] = '\0';
                            if (memcmp(atom_name, "\xA9nam", 4u) == 0) {
                                apply_tag_pair(fields, "TITLE", text);
                            } else if (memcmp(atom_name, "\xA9" "ART", 4u) == 0) {
                                apply_tag_pair(fields, "ARTIST", text);
                            } else if (strcmp(atom_name, "aART") == 0) {
                                apply_tag_pair(fields, "ALBUMARTIST", text);
                            } else if (memcmp(atom_name, "\xA9" "alb", 4u) == 0) {
                                apply_tag_pair(fields, "ALBUM", text);
                            } else if (memcmp(atom_name, "\xA9" "gen", 4u) == 0) {
                                apply_tag_pair(fields, "GENRE", text);
                            } else if (memcmp(atom_name, "\xA9" "day", 4u) == 0) {
                                apply_tag_pair(fields, "DATE", text);
                            }
                        }
                    }
                }
            }
            (void)data_hdr_pos;
        }
        if (fseek(file, atom_end, SEEK_SET) != 0) {
            break;
        }
    }
}

static void parse_wav_file(FILE *file, struct parsed_tag_fields *fields) {
    uint8_t riff[12];
    if (fread(riff, 1u, 12u, file) != 12u || memcmp(riff, "RIFF", 4u) != 0 ||
        memcmp(riff + 8u, "WAVE", 4u) != 0) {
        return;
    }
    uint32_t byte_rate = 0u;
    uint32_t data_size = 0u;
    for (uint32_t chunk_idx = 0u; chunk_idx < 32u; chunk_idx++) {
        uint8_t ch[8];
        if (fread(ch, 1u, 8u, file) != 8u) {
            break;
        }
        uint32_t ch_size = read_le32(ch + 4u);
        if (memcmp(ch, "fmt ", 4u) == 0 && ch_size >= 16u) {
            uint8_t fmt[16];
            if (fread(fmt, 1u, 16u, file) == 16u) {
                byte_rate = read_le32(fmt + 8u);
            }
            if (ch_size > 16u && fseek(file, (long)(ch_size - 16u), SEEK_CUR) != 0) {
                break;
            }
        } else if (memcmp(ch, "data", 4u) == 0) {
            data_size = ch_size;
            break;
        } else {
            if (fseek(file, (long)(ch_size + (ch_size & 1u)), SEEK_CUR) != 0) {
                break;
            }
        }
    }
    if (byte_rate > 0u && data_size > 0u) {
        fields->duration_seconds = data_size / byte_rate;
    }
}

static void clean_filename_for_title(const char *filename, char *title_out, size_t capacity,
                                     uint16_t *track_num_out) {
    const char *dot = strrchr(filename, '.');
    size_t stem_len = (dot != NULL) ? (size_t)(dot - filename) : strlen(filename);
    char stem[160];
    copy_string_bounded(stem, sizeof(stem), filename, stem_len);

    size_t cursor = 0u;
    if (isdigit((unsigned char)stem[0])) {
        uint32_t num = 0u;
        size_t num_end = 0u;
        while (isdigit((unsigned char)stem[num_end])) {
            num = num * 10u + (uint32_t)(stem[num_end] - '0');
            num_end++;
        }
        while (stem[num_end] == ' ' || stem[num_end] == '-' || stem[num_end] == '_' ||
               stem[num_end] == '.') {
            num_end++;
        }
        if (stem[num_end] != '\0') {
            if (*track_num_out == 0u && num > 0u && num < 1000u) {
                *track_num_out = (uint16_t)num;
            }
            cursor = num_end;
        }
    }
    copy_string_bounded(title_out, capacity, stem + cursor, capacity - 1u);
}

static void infer_folder_hierarchy(const char *relative_path, char *artist_out, size_t artist_cap,
                                   char *album_out, size_t album_cap, const char **filename_out) {
    const char *last_slash = strrchr(relative_path, '/');
    *filename_out = (last_slash != NULL) ? (last_slash + 1) : relative_path;
    copy_string_bounded(artist_out, artist_cap, "Unknown Artist", artist_cap - 1u);
    copy_string_bounded(album_out, album_cap, "Unknown Album", album_cap - 1u);

    if (last_slash == NULL) {
        return;
    }
    size_t album_end = (size_t)(last_slash - relative_path);
    size_t album_start = album_end;
    while (album_start > 0u && relative_path[album_start - 1u] != '/') {
        album_start--;
    }
    if (album_end > album_start) {
        copy_string_bounded(album_out, album_cap, relative_path + album_start,
                            album_end - album_start);
    }
    if (album_start > 1u) {
        size_t artist_end = album_start - 1u;
        size_t artist_start = artist_end;
        while (artist_start > 0u && relative_path[artist_start - 1u] != '/') {
            artist_start--;
        }
        if (artist_end > artist_start) {
            copy_string_bounded(artist_out, artist_cap, relative_path + artist_start,
                                artist_end - artist_start);
        }
    }
}

static void find_sidecar_cover(const char *file_path, char *cover_path_out, size_t capacity) {
    cover_path_out[0] = '\0';
    const char *slash = strrchr(file_path, '/');
    if (slash == NULL) {
        return;
    }
    size_t dir_len = (size_t)(slash - file_path);
    char dir_path[320];
    copy_string_bounded(dir_path, sizeof(dir_path), file_path, dir_len);

    static const char *CANDIDATE_NAMES[6] = {"cover", "folder", "front",
                                             "album", "artwork", "disc"};
    static const char *CANDIDATE_EXTS[3] = {".jpg", ".jpeg", ".png"};

    DIR *dir_handle = opendir(dir_path);
    if (dir_handle == NULL) {
        return;
    }
    struct dirent *entry = NULL;
    int best_rank = 999;
    while ((entry = readdir(dir_handle)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        for (int name_idx = 0; name_idx < 6; name_idx++) {
            for (int ext_idx = 0; ext_idx < 3; ext_idx++) {
                char target[64];
                snprintf(target, sizeof(target), "%s%s", CANDIDATE_NAMES[name_idx],
                         CANDIDATE_EXTS[ext_idx]);
                if (library_compare_ascii_casefold(entry->d_name, target) == 0) {
                    int rank = name_idx * 4 + ext_idx;
                    if (rank < best_rank) {
                        best_rank = rank;
                        snprintf(cover_path_out, capacity, "%s/%s", dir_path, entry->d_name);
                    }
                }
            }
        }
    }
    closedir(dir_handle);
}

bool library_parse_audio_file_metadata(const char *file_path, const char *root_path,
                                       struct library_track_input *input_out,
                                       char *string_scratch, size_t scratch_capacity) {
    if (file_path == NULL || input_out == NULL || string_scratch == NULL ||
        scratch_capacity < 1536u) {
        return false;
    }
    memset(input_out, 0, sizeof(*input_out));

    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0 || file_stat.st_size <= 0) {
        return false;
    }

    const char *rel = file_path;
    if (root_path != NULL) {
        size_t root_len = strlen(root_path);
        if (strncmp(file_path, root_path, root_len) == 0 && file_path[root_len] == '/') {
            rel = file_path + root_len + 1u;
        }
    }

    struct parsed_tag_fields fields;
    memset(&fields, 0, sizeof(fields));
    uint8_t format_kind = classify_audio_extension(file_path);

    FILE *file = fopen(file_path, "rb");
    if (file != NULL) {
        if (format_kind == LIBRARY_FORMAT_FLAC) {
            parse_flac_file(file, &fields);
        } else if (format_kind == LIBRARY_FORMAT_MP3) {
            parse_mp3_file(file, (uint64_t)file_stat.st_size, &fields);
        } else if (format_kind == LIBRARY_FORMAT_M4A) {
            parse_m4a_atoms(file, (long)file_stat.st_size, &fields, 0u);
        } else if (format_kind == LIBRARY_FORMAT_WAV) {
            parse_wav_file(file, &fields);
        }
        fclose(file);
    }

    char fallback_artist[160];
    char fallback_album[160];
    const char *filename = NULL;
    infer_folder_hierarchy(rel, fallback_artist, sizeof(fallback_artist), fallback_album,
                           sizeof(fallback_album), &filename);
    if (fields.title[0] == '\0') {
        clean_filename_for_title(filename, fields.title, sizeof(fields.title),
                                 &fields.track_number);
    }
    if (fields.artist[0] == '\0') {
        copy_string_bounded(fields.artist, sizeof(fields.artist), fallback_artist,
                            sizeof(fields.artist) - 1u);
    }
    if (fields.album[0] == '\0') {
        copy_string_bounded(fields.album, sizeof(fields.album), fallback_album,
                            sizeof(fields.album) - 1u);
    }

    /* Pack strings into caller scratch buffer */
    char *s_rel = string_scratch;
    char *s_title = string_scratch + 256u;
    char *s_artist = string_scratch + 416u;
    char *s_aartist = string_scratch + 576u;
    char *s_album = string_scratch + 736u;
    char *s_genre = string_scratch + 896u;
    char *s_cover = string_scratch + 1056u;

    copy_string_bounded(s_rel, 256u, rel, 255u);
    copy_string_bounded(s_title, 160u, fields.title, 159u);
    copy_string_bounded(s_artist, 160u, fields.artist, 159u);
    copy_string_bounded(s_aartist, 160u, fields.album_artist, 159u);
    copy_string_bounded(s_album, 160u, fields.album, 159u);
    copy_string_bounded(s_genre, 160u, fields.genre, 159u);

    find_sidecar_cover(file_path, s_cover, 480u);
    if (s_cover[0] != '\0') {
        fields.cover_kind = LIBRARY_COVER_SIDECAR;
        fields.cover_offset = 0u;
        fields.cover_length = 0u;
    } else if (fields.cover_kind != LIBRARY_COVER_NONE) {
        copy_string_bounded(s_cover, 480u, file_path, 479u);
    }

    input_out->file_path = file_path;
    input_out->relative_path = s_rel;
    input_out->title = s_title;
    input_out->artist = s_artist;
    input_out->album_artist = s_aartist;
    input_out->album = s_album;
    input_out->genre = s_genre;
    input_out->year = fields.year;
    input_out->disc_number = fields.disc_number;
    input_out->track_number = fields.track_number;
    input_out->duration_seconds = fields.duration_seconds;
    input_out->added_at_seconds = (int64_t)file_stat.st_mtime;
    input_out->file_size_bytes = (uint64_t)file_stat.st_size;
    input_out->format_kind = format_kind;
    input_out->cover_source_kind = fields.cover_kind;
    input_out->cover_path = s_cover;
    input_out->cover_data_offset = fields.cover_offset;
    input_out->cover_data_length = fields.cover_length;
    return true;
}

static void close_dir_stack(struct library_scanner *scanner) {
    for (uint32_t idx = 0u; idx < scanner->dir_depth; idx++) {
        if (scanner->dir_stack[idx].dir_handle != NULL) {
            closedir((DIR *)scanner->dir_stack[idx].dir_handle);
        }
        free(scanner->dir_stack[idx].dir_path);
        scanner->dir_stack[idx].dir_handle = NULL;
        scanner->dir_stack[idx].dir_path = NULL;
    }
    scanner->dir_depth = 0u;
}

static void free_discovered_entries(struct library_scanner *scanner) {
    if (scanner->audio_entries != NULL) {
        for (uint32_t idx = 0u; idx < scanner->discovered_files; idx++) {
            free(scanner->audio_entries[idx].path);
        }
        free(scanner->audio_entries);
    }
    scanner->audio_entries = NULL;
    scanner->discovered_files = 0u;
    scanner->audio_capacity = 0u;
    scanner->scanned_files = 0u;

    if (scanner->m3u_paths != NULL) {
        for (uint32_t idx = 0u; idx < scanner->m3u_count; idx++) {
            free(scanner->m3u_paths[idx]);
        }
        free(scanner->m3u_paths);
    }
    scanner->m3u_paths = NULL;
    scanner->m3u_count = 0u;
    scanner->m3u_capacity = 0u;
    scanner->m3u_cursor = 0u;
}

void library_scanner_initialize(struct library_scanner *scanner, struct library_catalog *catalog) {
    if (scanner == NULL) {
        return;
    }
    memset(scanner, 0, sizeof(*scanner));
    scanner->catalog = catalog;
    scanner->phase = LIB_SCAN_IDLE;
}

void library_scanner_reset(struct library_scanner *scanner) {
    if (scanner == NULL) {
        return;
    }
    close_dir_stack(scanner);
    free_discovered_entries(scanner);
    scanner->phase = LIB_SCAN_IDLE;
    scanner->loaded_from_cache = false;
    scanner->delta_changed = false;
    scanner->delta_fingerprint = 0ull;
}

void library_scanner_destroy(struct library_scanner *scanner) {
    if (scanner == NULL) {
        return;
    }
    library_scanner_reset(scanner);
}

static bool push_dir_frame(struct library_scanner *scanner, const char *dir_path) {
    if (scanner->dir_depth >= LIBRARY_SCAN_MAX_DEPTH) {
        return false;
    }
    DIR *dir_handle = opendir(dir_path);
    if (dir_handle == NULL) {
        return false;
    }
    size_t len = strlen(dir_path);
    char *copy = malloc(len + 1u);
    if (copy == NULL) {
        closedir(dir_handle);
        return false;
    }
    memcpy(copy, dir_path, len + 1u);
    scanner->dir_stack[scanner->dir_depth].dir_handle = dir_handle;
    scanner->dir_stack[scanner->dir_depth].dir_path = copy;
    scanner->dir_depth++;
    return true;
}

bool library_scanner_start(struct library_scanner *scanner, const char *root_path,
                           const char *index_dir_path, bool try_cache_first) {
    if (scanner == NULL || scanner->catalog == NULL || root_path == NULL) {
        return false;
    }
    library_scanner_reset(scanner);
    copy_string_bounded(scanner->root_path, sizeof(scanner->root_path), root_path,
                        sizeof(scanner->root_path) - 1u);
    if (index_dir_path != NULL && index_dir_path[0] != '\0') {
        copy_string_bounded(scanner->index_dir_path, sizeof(scanner->index_dir_path),
                            index_dir_path, sizeof(scanner->index_dir_path) - 1u);
        snprintf(scanner->index_temp_path, sizeof(scanner->index_temp_path), "%s/.library.idx.tmp",
                 scanner->index_dir_path);
        snprintf(scanner->index_final_path, sizeof(scanner->index_final_path), "%s/library.idx",
                 scanner->index_dir_path);
    }

    struct stat root_stat;
    scanner->music_root_exists = (stat(scanner->root_path, &root_stat) == 0 && S_ISDIR(root_stat.st_mode));
    if (!scanner->music_root_exists) {
        library_catalog_clear(scanner->catalog);
        scanner->phase = LIB_SCAN_READY;
        return true;
    }

    if (try_cache_first && scanner->index_final_path[0] != '\0') {
        scanner->phase = LIB_SCAN_TRY_CACHE;
        return true;
    }

    library_catalog_clear(scanner->catalog);
    (void)push_dir_frame(scanner, scanner->root_path);
    scanner->phase = LIB_SCAN_DISCOVERING;
    return true;
}

bool library_scanner_start_delta_check(struct library_scanner *scanner) {
    if (scanner == NULL || scanner->catalog == NULL || !scanner->music_root_exists ||
        library_scanner_is_busy(scanner)) {
        return false;
    }
    close_dir_stack(scanner);
    free_discovered_entries(scanner);
    scanner->delta_changed = false;
    scanner->delta_fingerprint = 0ull;
    (void)push_dir_frame(scanner, scanner->root_path);
    scanner->phase = LIB_SCAN_DELTA_DISCOVERING;
    return true;
}

static bool append_audio_entry(struct library_scanner *scanner, const char *path, int64_t mtime_sec,
                               uint64_t size_bytes, uint8_t format_kind) {
    if (scanner->discovered_files >= LIBRARY_MAX_TRACKS) {
        return false;
    }
    if (scanner->discovered_files >= scanner->audio_capacity) {
        uint32_t new_cap = scanner->audio_capacity == 0u ? 128u : scanner->audio_capacity * 2u;
        if (new_cap > LIBRARY_MAX_TRACKS) {
            new_cap = LIBRARY_MAX_TRACKS;
        }
        struct library_discovered_entry *grown =
            realloc(scanner->audio_entries, (size_t)new_cap * sizeof(struct library_discovered_entry));
        if (grown == NULL) {
            return false;
        }
        scanner->audio_entries = grown;
        scanner->audio_capacity = new_cap;
    }
    size_t len = strlen(path);
    char *path_copy = malloc(len + 1u);
    if (path_copy == NULL) {
        return false;
    }
    memcpy(path_copy, path, len + 1u);
    scanner->audio_entries[scanner->discovered_files++] = (struct library_discovered_entry){
        .path = path_copy,
        .mtime_seconds = mtime_sec,
        .file_size_bytes = size_bytes,
        .format_kind = format_kind,
    };
    return true;
}

static void append_m3u_path(struct library_scanner *scanner, const char *path) {
    if (scanner->m3u_count >= LIBRARY_MAX_PLAYLISTS) {
        return;
    }
    if (scanner->m3u_count >= scanner->m3u_capacity) {
        uint32_t new_cap = scanner->m3u_capacity == 0u ? 16u : scanner->m3u_capacity * 2u;
        char **grown = realloc(scanner->m3u_paths, (size_t)new_cap * sizeof(char *));
        if (grown == NULL) {
            return;
        }
        scanner->m3u_paths = grown;
        scanner->m3u_capacity = new_cap;
    }
    size_t len = strlen(path);
    char *copy = malloc(len + 1u);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, path, len + 1u);
    scanner->m3u_paths[scanner->m3u_count++] = copy;
}

static uint64_t fold_entry_fingerprint(const char *path, int64_t mtime_sec, uint64_t size_bytes) {
    uint64_t hash = library_fnv1a_64(path);
    hash ^= (uint64_t)mtime_sec * 0x9E3779B97F4A7C15ull;
    hash ^= size_bytes * 0xBF58476D1CE4E5B9ull;
    return hash;
}

static void step_discovery_batch(struct library_scanner *scanner, bool collect_files,
                                 uint64_t start_ns, uint64_t budget_ns) {
    while (scanner->dir_depth > 0u) {
        struct library_dir_frame *frame = &scanner->dir_stack[scanner->dir_depth - 1u];
        struct dirent *entry = readdir((DIR *)frame->dir_handle);
        if (entry == NULL) {
            closedir((DIR *)frame->dir_handle);
            free(frame->dir_path);
            frame->dir_handle = NULL;
            frame->dir_path = NULL;
            scanner->dir_depth--;
            if (budget_ns > 0u && (monotonic_clock_ns() - start_ns) >= budget_ns) {
                break;
            }
            continue;
        }
        if (should_skip_directory_name(entry->d_name)) {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", frame->dir_path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            (void)push_dir_frame(scanner, full_path);
        } else if (S_ISREG(st.st_mode)) {
            uint8_t fmt = classify_audio_extension(entry->d_name);
            if (fmt != LIBRARY_FORMAT_UNKNOWN) {
                scanner->delta_fingerprint ^=
                    fold_entry_fingerprint(full_path, (int64_t)st.st_mtime, (uint64_t)st.st_size);
                if (collect_files) {
                    (void)append_audio_entry(scanner, full_path, (int64_t)st.st_mtime,
                                             (uint64_t)st.st_size, fmt);
                } else {
                    scanner->discovered_files++;
                }
            } else if (collect_files && is_m3u_filename(entry->d_name)) {
                append_m3u_path(scanner, full_path);
            }
        }
        if (budget_ns > 0u && (monotonic_clock_ns() - start_ns) >= budget_ns) {
            break;
        }
    }
}

static void parse_single_m3u_file(struct library_catalog *catalog, const char *m3u_path) {
    FILE *file = fopen(m3u_path, "r");
    if (file == NULL) {
        return;
    }

    const char *slash = strrchr(m3u_path, '/');
    const char *base_name = (slash != NULL) ? (slash + 1) : m3u_path;
    const char *dot = strrchr(base_name, '.');
    size_t name_len = (dot != NULL) ? (size_t)(dot - base_name) : strlen(base_name);
    char playlist_name[128];
    copy_string_bounded(playlist_name, sizeof(playlist_name), base_name, name_len);

    char m3u_dir[320];
    if (slash != NULL) {
        copy_string_bounded(m3u_dir, sizeof(m3u_dir), m3u_path, (size_t)(slash - m3u_path));
    } else {
        m3u_dir[0] = '\0';
    }

    uint32_t track_indices[512];
    uint32_t track_count = 0u;
    char line[512];
    while (fgets(line, sizeof(line), file) != NULL && track_count < 512u) {
        size_t len = strlen(line);
        while (len > 0u && (line[len - 1u] == '\n' || line[len - 1u] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0u || line[0] == '#') {
            continue;
        }
        char resolved[512];
        if (line[0] == '/') {
            copy_string_bounded(resolved, sizeof(resolved), line, sizeof(resolved) - 1u);
        } else if (m3u_dir[0] != '\0') {
            snprintf(resolved, sizeof(resolved), "%s/%s", m3u_dir, line);
        } else {
            copy_string_bounded(resolved, sizeof(resolved), line, sizeof(resolved) - 1u);
        }
        int32_t found = library_catalog_find_track_by_path(catalog, resolved);
        if (found >= 0) {
            track_indices[track_count++] = (uint32_t)found;
        }
    }
    fclose(file);

    if (track_count > 0u) {
        char id_seed[384];
        snprintf(id_seed, sizeof(id_seed), "playlist:m3u:%s", m3u_path);
        (void)library_catalog_add_playlist(catalog, library_fnv1a_64(id_seed), playlist_name,
                                           m3u_path, track_indices, track_count);
    }
}

bool library_scanner_step(struct library_scanner *scanner, uint64_t now_ns, uint64_t budget_ns) {
    (void)now_ns;
    if (scanner == NULL || scanner->catalog == NULL) {
        return false;
    }
    uint64_t start_ns = monotonic_clock_ns();

    switch (scanner->phase) {
        case LIB_SCAN_TRY_CACHE:
            if (library_catalog_load_index(scanner->catalog, scanner->index_final_path)) {
                scanner->loaded_from_cache = true;
                scanner->discovered_files = scanner->catalog->track_count;
                scanner->scanned_files = scanner->catalog->track_count;
                scanner->phase = LIB_SCAN_READY;
                return true;
            }
            library_catalog_clear(scanner->catalog);
            (void)push_dir_frame(scanner, scanner->root_path);
            scanner->phase = LIB_SCAN_DISCOVERING;
            return true;

        case LIB_SCAN_DISCOVERING:
            step_discovery_batch(scanner, true, start_ns, budget_ns);
            if (scanner->dir_depth == 0u) {
                scanner->catalog->fingerprint = scanner->delta_fingerprint;
                scanner->phase = LIB_SCAN_SCANNING;
            }
            return true;

        case LIB_SCAN_SCANNING: {
            char scratch[2048];
            while (scanner->scanned_files < scanner->discovered_files) {
                const struct library_discovered_entry *entry =
                    &scanner->audio_entries[scanner->scanned_files];
                struct library_track_input input;
                if (library_parse_audio_file_metadata(entry->path, scanner->root_path, &input,
                                                      scratch, sizeof(scratch))) {
                    (void)library_catalog_add_track(scanner->catalog, &input);
                }
                scanner->scanned_files++;
                if (budget_ns > 0u && (monotonic_clock_ns() - start_ns) >= budget_ns) {
                    break;
                }
            }
            if (scanner->scanned_files >= scanner->discovered_files) {
                scanner->phase = LIB_SCAN_PARSING_M3U;
            }
            return true;
        }

        case LIB_SCAN_PARSING_M3U:
            while (scanner->m3u_cursor < scanner->m3u_count) {
                parse_single_m3u_file(scanner->catalog, scanner->m3u_paths[scanner->m3u_cursor]);
                scanner->m3u_cursor++;
                if (budget_ns > 0u && (monotonic_clock_ns() - start_ns) >= budget_ns) {
                    break;
                }
            }
            if (scanner->m3u_cursor >= scanner->m3u_count) {
                scanner->phase = LIB_SCAN_FINALIZING;
            }
            return true;

        case LIB_SCAN_FINALIZING:
            (void)library_catalog_finalize(scanner->catalog);
            if (scanner->index_dir_path[0] != '\0') {
                (void)library_catalog_save_index(scanner->catalog, scanner->index_dir_path,
                                                 scanner->index_temp_path,
                                                 scanner->index_final_path);
            }
            free_discovered_entries(scanner);
            scanner->phase = LIB_SCAN_READY;
            return true;

        case LIB_SCAN_DELTA_DISCOVERING:
            step_discovery_batch(scanner, false, start_ns, budget_ns);
            if (scanner->dir_depth == 0u) {
                scanner->phase = LIB_SCAN_DELTA_CHECKING;
            }
            return true;

        case LIB_SCAN_DELTA_CHECKING:
            scanner->delta_changed =
                (scanner->discovered_files != scanner->catalog->track_count ||
                 scanner->delta_fingerprint != scanner->catalog->fingerprint);
            scanner->phase = LIB_SCAN_READY;
            return true;

        case LIB_SCAN_IDLE:
        case LIB_SCAN_READY:
        default:
            return false;
    }
}

bool library_scanner_is_busy(const struct library_scanner *scanner) {
    if (scanner == NULL) {
        return false;
    }
    return scanner->phase != LIB_SCAN_IDLE && scanner->phase != LIB_SCAN_READY;
}

bool library_scanner_is_ready(const struct library_scanner *scanner) {
    return scanner != NULL && scanner->phase == LIB_SCAN_READY;
}
