#include "library_scanner.h"

#include "library_db.h"

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#include <time.h>
#include <unistd.h>

struct linux_kernel_timezone {
    int32_t minutes_west;
    int32_t dst_time;
};

static int32_t read_kernel_tz_minutes_west(void) {
#if defined(__linux__) && defined(SYS_gettimeofday)
    struct timeval current_time;
    struct linux_kernel_timezone kernel_timezone;
    memset(&kernel_timezone, 0, sizeof(kernel_timezone));
    if (syscall(SYS_gettimeofday, &current_time, &kernel_timezone) == 0) {
        return kernel_timezone.minutes_west;
    }
#endif
    return 0;
}

int64_t library_normalize_fat_mtime(int64_t raw_mtime_seconds, int32_t kernel_tz_minutes_west) {
    return raw_mtime_seconds - (int64_t)kernel_tz_minutes_west * 60ll;
}

static int64_t normalize_stat_mtime(time_t raw_mtime) {
    return library_normalize_fat_mtime((int64_t)raw_mtime, read_kernel_tz_minutes_west());
}

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
    uint64_t audio_start_offset;
    uint64_t audio_payload_bytes;
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

static void parse_flac_file(FILE *file, uint64_t file_size_bytes, struct parsed_tag_fields *fields) {
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
                    if (fseek(file, block_start + 8L + (long)mime_len, SEEK_SET) == 0) {
                        uint8_t desc_len_buf[4];
                        if (fread(desc_len_buf, 1u, 4u, file) == 4u) {
                            uint32_t desc_len = read_be32(desc_len_buf);
                            long image_meta_pos =
                                block_start + 8L + (long)mime_len + 4L + (long)desc_len + 16L;
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

        if (is_last) {
            uint64_t after_block = (uint64_t)block_start + (uint64_t)block_size;
            if (after_block <= file_size_bytes) {
                fields->audio_start_offset = after_block;
                fields->audio_payload_bytes = file_size_bytes - after_block;
            }
            break;
        }
        if (fseek(file, block_start + (long)block_size, SEEK_SET) != 0) {
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

    if ((uint64_t)audio_start <= file_size_bytes) {
        uint64_t payload_end = file_size_bytes;
        if (file_size_bytes >= (uint64_t)audio_start + 128ull) {
            uint8_t id3v1_marker[3];
            if (fseek(file, (long)(file_size_bytes - 128ull), SEEK_SET) == 0 &&
                fread(id3v1_marker, 1u, 3u, file) == 3u &&
                memcmp(id3v1_marker, "TAG", 3u) == 0) {
                payload_end -= 128ull;
            }
        }
        fields->audio_start_offset = (uint64_t)audio_start;
        fields->audio_payload_bytes = payload_end - (uint64_t)audio_start;
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
        } else if (strcmp(atom_name, "mdat") == 0) {
            long payload_start = atom_start + header_bytes;
            if (atom_end >= payload_start) {
                fields->audio_start_offset = (uint64_t)payload_start;
                fields->audio_payload_bytes = (uint64_t)(atom_end - payload_start);
            }
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
            long data_pos = ftell(file);
            if (data_pos >= 0) {
                fields->audio_start_offset = (uint64_t)data_pos;
                fields->audio_payload_bytes = ch_size;
            }
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
            parse_flac_file(file, (uint64_t)file_stat.st_size, &fields);
        } else if (format_kind == LIBRARY_FORMAT_MP3) {
            parse_mp3_file(file, (uint64_t)file_stat.st_size, &fields);
        } else if (format_kind == LIBRARY_FORMAT_M4A) {
            parse_m4a_atoms(file, (long)file_stat.st_size, &fields, 0u);
        } else if (format_kind == LIBRARY_FORMAT_WAV) {
            parse_wav_file(file, &fields);
        }
        fclose(file);
    }
    if (fields.audio_payload_bytes == 0u &&
        (uint64_t)file_stat.st_size > fields.audio_start_offset) {
        fields.audio_payload_bytes = (uint64_t)file_stat.st_size - fields.audio_start_offset;
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
    input_out->added_at_seconds = normalize_stat_mtime(file_stat.st_mtime);
    input_out->file_size_bytes = (uint64_t)file_stat.st_size;
    input_out->format_kind = format_kind;
    input_out->cover_source_kind = fields.cover_kind;
    input_out->cover_path = s_cover;
    input_out->cover_data_offset = fields.cover_offset;
    input_out->cover_data_length = fields.cover_length;
    input_out->audio_start_offset = fields.audio_start_offset;
    input_out->audio_payload_bytes = fields.audio_payload_bytes;
    return true;
}

#define AUDIO_FINGERPRINT_WINDOW_BYTES 4096u
#define AUDIO_FINGERPRINT_DIRECT_LIMIT_BYTES (AUDIO_FINGERPRINT_WINDOW_BYTES * 2u)
#define FNV1A_64_OFFSET_BASIS 14695981039346656037ull
#define FNV1A_64_PRIME 1099511628211ull
#define FAT32_MTIME_TOLERANCE_SECONDS 2ll
#define DELETED_ALBUM_UNLINK_BATCH 128u

static uint64_t fnv1a_extend(uint64_t hash, const uint8_t *bytes, size_t length) {
    for (size_t index = 0u; index < length; index++) {
        hash ^= bytes[index];
        hash *= FNV1A_64_PRIME;
    }
    return hash;
}

bool library_compute_audio_fingerprint(const char *file_path, uint64_t audio_start_offset,
                                       uint64_t audio_payload_bytes, uint64_t *fingerprint_out) {
    if (file_path == NULL || fingerprint_out == NULL) {
        return false;
    }
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        return false;
    }

    uint8_t window_buffer[AUDIO_FINGERPRINT_WINDOW_BYTES];
    uint64_t hash = FNV1A_64_OFFSET_BASIS;
    hash = fnv1a_extend(hash, (const uint8_t *)&audio_payload_bytes, sizeof(audio_payload_bytes));

    if (audio_payload_bytes <= AUDIO_FINGERPRINT_DIRECT_LIMIT_BYTES) {
        if (fseek(file, (long)audio_start_offset, SEEK_SET) == 0) {
            uint64_t remaining = audio_payload_bytes;
            while (remaining > 0u) {
                size_t chunk = (remaining > AUDIO_FINGERPRINT_WINDOW_BYTES)
                                   ? AUDIO_FINGERPRINT_WINDOW_BYTES
                                   : (size_t)remaining;
                size_t bytes_read = fread(window_buffer, 1u, chunk, file);
                if (bytes_read == 0u) {
                    break;
                }
                hash = fnv1a_extend(hash, window_buffer, bytes_read);
                remaining -= bytes_read;
            }
        }
    } else {
        if (fseek(file, (long)audio_start_offset, SEEK_SET) == 0) {
            size_t head_read = fread(window_buffer, 1u, AUDIO_FINGERPRINT_WINDOW_BYTES, file);
            hash = fnv1a_extend(hash, window_buffer, head_read);
        }
        uint64_t mid_offset = audio_start_offset + (audio_payload_bytes / 2ull);
        if (fseek(file, (long)mid_offset, SEEK_SET) == 0) {
            size_t mid_read = fread(window_buffer, 1u, AUDIO_FINGERPRINT_WINDOW_BYTES, file);
            hash = fnv1a_extend(hash, window_buffer, mid_read);
        }
    }

    fclose(file);
    *fingerprint_out = hash;
    return true;
}

struct cached_track_stamp {
    int64_t track_id;
    char *path;
    uint64_t path_hash;
    uint64_t file_size_bytes;
    int64_t mtime_seconds;
    uint64_t audio_fingerprint;
    bool matched;
};

struct cached_m3u_stamp {
    char *path;
    uint64_t file_size_bytes;
    int64_t mtime_seconds;
    bool matched;
};

struct discovered_audio_file {
    char *path;
    uint64_t file_size_bytes;
    int64_t mtime_seconds;
    uint8_t format_kind;
    int64_t matched_track_id;
    bool needs_parse;
};

struct discovered_sidecar_cover {
    char *directory_path;
    char *cover_path;
    uint64_t file_size_bytes;
    int64_t mtime_seconds;
};

struct discovered_m3u_file {
    char *path;
    uint64_t file_size_bytes;
    int64_t mtime_seconds;
    bool needs_parse;
};

struct scanner_work_state {
    struct library_db_scanner *owner;
    struct cached_track_stamp *cached_tracks;
    uint32_t cached_count;
    uint32_t cached_capacity;
    uint32_t *hash_slots;
    uint32_t hash_slot_count;

    struct cached_m3u_stamp *cached_m3u;
    uint32_t cached_m3u_count;
    uint32_t cached_m3u_capacity;

    struct discovered_audio_file *discovered_audio;
    uint32_t discovered_audio_count;
    uint32_t discovered_audio_capacity;

    struct discovered_sidecar_cover *discovered_sidecars;
    uint32_t discovered_sidecar_count;
    uint32_t discovered_sidecar_capacity;

    struct discovered_m3u_file *discovered_m3u;
    uint32_t discovered_m3u_count;
    uint32_t discovered_m3u_capacity;
};

static char *duplicate_c_string(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length + 1u);
    }
    return copy;
}

static bool timestamps_match_fat32(int64_t first_seconds, int64_t second_seconds) {
    int64_t difference = first_seconds - second_seconds;
    if (difference < 0) {
        difference = -difference;
    }
    return difference <= FAT32_MTIME_TOLERANCE_SECONDS;
}

static void collect_cached_track_stamp(void *callback_context, int64_t track_id, const char *path,
                                       uint64_t file_size_bytes, int64_t mtime_seconds,
                                       uint64_t audio_fingerprint) {
    struct scanner_work_state *state = (struct scanner_work_state *)callback_context;
    if (state->cached_count >= state->cached_capacity) {
        uint32_t next_capacity =
            (state->cached_capacity == 0u) ? 256u : state->cached_capacity * 2u;
        struct cached_track_stamp *grown = realloc(
            state->cached_tracks, (size_t)next_capacity * sizeof(struct cached_track_stamp));
        if (grown == NULL) {
            return;
        }
        state->cached_tracks = grown;
        state->cached_capacity = next_capacity;
    }
    char *path_copy = duplicate_c_string(path);
    if (path_copy == NULL) {
        return;
    }
    state->cached_tracks[state->cached_count++] = (struct cached_track_stamp){
        .track_id = track_id,
        .path = path_copy,
        .path_hash = library_fnv1a_64(path),
        .file_size_bytes = file_size_bytes,
        .mtime_seconds = mtime_seconds,
        .audio_fingerprint = audio_fingerprint,
        .matched = false,
    };
}

static void collect_cached_m3u_stamp(void *callback_context, const char *path,
                                     uint64_t file_size_bytes, int64_t mtime_seconds) {
    struct scanner_work_state *state = (struct scanner_work_state *)callback_context;
    if (state->cached_m3u_count >= state->cached_m3u_capacity) {
        uint32_t next_capacity =
            (state->cached_m3u_capacity == 0u) ? 16u : state->cached_m3u_capacity * 2u;
        struct cached_m3u_stamp *grown =
            realloc(state->cached_m3u, (size_t)next_capacity * sizeof(struct cached_m3u_stamp));
        if (grown == NULL) {
            return;
        }
        state->cached_m3u = grown;
        state->cached_m3u_capacity = next_capacity;
    }
    char *path_copy = duplicate_c_string(path);
    if (path_copy == NULL) {
        return;
    }
    state->cached_m3u[state->cached_m3u_count++] = (struct cached_m3u_stamp){
        .path = path_copy,
        .file_size_bytes = file_size_bytes,
        .mtime_seconds = mtime_seconds,
        .matched = false,
    };
}

static void build_cached_path_hash_table(struct scanner_work_state *state) {
    if (state->cached_count == 0u) {
        return;
    }
    uint32_t slot_count = 64u;
    while (slot_count < state->cached_count * 2u) {
        slot_count *= 2u;
    }
    state->hash_slots = malloc((size_t)slot_count * sizeof(uint32_t));
    if (state->hash_slots == NULL) {
        return;
    }
    for (uint32_t index = 0u; index < slot_count; index++) {
        state->hash_slots[index] = UINT32_MAX;
    }
    state->hash_slot_count = slot_count;
    uint32_t mask = slot_count - 1u;
    for (uint32_t index = 0u; index < state->cached_count; index++) {
        uint32_t slot = (uint32_t)(state->cached_tracks[index].path_hash & mask);
        while (state->hash_slots[slot] != UINT32_MAX) {
            slot = (slot + 1u) & mask;
        }
        state->hash_slots[slot] = index;
    }
}

static struct cached_track_stamp *lookup_cached_track_by_path(const struct scanner_work_state *state,
                                                              const char *path) {
    if (state->hash_slots == NULL || state->hash_slot_count == 0u) {
        return NULL;
    }
    uint64_t path_hash = library_fnv1a_64(path);
    uint32_t mask = state->hash_slot_count - 1u;
    uint32_t slot = (uint32_t)(path_hash & mask);
    while (state->hash_slots[slot] != UINT32_MAX) {
        struct cached_track_stamp *candidate = &state->cached_tracks[state->hash_slots[slot]];
        if (candidate->path_hash == path_hash && strcmp(candidate->path, path) == 0) {
            return candidate;
        }
        slot = (slot + 1u) & mask;
    }
    return NULL;
}

static int classify_sidecar_cover_rank(const char *filename) {
    static const char *const CANDIDATE_NAMES[6] = {"cover", "folder", "front",
                                                   "album", "artwork", "disc"};
    static const char *const CANDIDATE_EXTS[3] = {".jpg", ".jpeg", ".png"};
    for (int name_index = 0; name_index < 6; name_index++) {
        for (int ext_index = 0; ext_index < 3; ext_index++) {
            char target[64];
            snprintf(target, sizeof(target), "%s%s", CANDIDATE_NAMES[name_index],
                     CANDIDATE_EXTS[ext_index]);
            if (library_compare_ascii_casefold(filename, target) == 0) {
                return name_index * 4 + ext_index;
            }
        }
    }
    return -1;
}

static void record_discovered_audio(struct scanner_work_state *state, const char *path,
                                    uint64_t file_size_bytes, int64_t mtime_seconds,
                                    uint8_t format_kind) {
    if (state->discovered_audio_count >= state->discovered_audio_capacity) {
        uint32_t next_capacity =
            (state->discovered_audio_capacity == 0u) ? 256u : state->discovered_audio_capacity * 2u;
        struct discovered_audio_file *grown = realloc(
            state->discovered_audio, (size_t)next_capacity * sizeof(struct discovered_audio_file));
        if (grown == NULL) {
            return;
        }
        state->discovered_audio = grown;
        state->discovered_audio_capacity = next_capacity;
    }
    char *path_copy = duplicate_c_string(path);
    if (path_copy == NULL) {
        return;
    }
    state->discovered_audio[state->discovered_audio_count++] = (struct discovered_audio_file){
        .path = path_copy,
        .file_size_bytes = file_size_bytes,
        .mtime_seconds = mtime_seconds,
        .format_kind = format_kind,
        .matched_track_id = 0,
        .needs_parse = true,
    };
}

static void record_discovered_sidecar(struct scanner_work_state *state, const char *directory_path,
                                      const char *cover_path, uint64_t file_size_bytes,
                                      int64_t mtime_seconds) {
    if (state->discovered_sidecar_count >= state->discovered_sidecar_capacity) {
        uint32_t next_capacity = (state->discovered_sidecar_capacity == 0u)
                                     ? 64u
                                     : state->discovered_sidecar_capacity * 2u;
        struct discovered_sidecar_cover *grown =
            realloc(state->discovered_sidecars,
                    (size_t)next_capacity * sizeof(struct discovered_sidecar_cover));
        if (grown == NULL) {
            return;
        }
        state->discovered_sidecars = grown;
        state->discovered_sidecar_capacity = next_capacity;
    }
    char *directory_copy = duplicate_c_string(directory_path);
    char *cover_copy = (cover_path != NULL) ? duplicate_c_string(cover_path) : NULL;
    if (directory_copy == NULL || (cover_path != NULL && cover_copy == NULL)) {
        free(directory_copy);
        free(cover_copy);
        return;
    }
    state->discovered_sidecars[state->discovered_sidecar_count++] = (struct discovered_sidecar_cover){
        .directory_path = directory_copy,
        .cover_path = cover_copy,
        .file_size_bytes = file_size_bytes,
        .mtime_seconds = mtime_seconds,
    };
}

static void record_discovered_m3u(struct scanner_work_state *state, const char *path,
                                  uint64_t file_size_bytes, int64_t mtime_seconds) {
    if (state->discovered_m3u_count >= state->discovered_m3u_capacity) {
        uint32_t next_capacity =
            (state->discovered_m3u_capacity == 0u) ? 16u : state->discovered_m3u_capacity * 2u;
        struct discovered_m3u_file *grown = realloc(
            state->discovered_m3u, (size_t)next_capacity * sizeof(struct discovered_m3u_file));
        if (grown == NULL) {
            return;
        }
        state->discovered_m3u = grown;
        state->discovered_m3u_capacity = next_capacity;
    }
    char *path_copy = duplicate_c_string(path);
    if (path_copy == NULL) {
        return;
    }
    state->discovered_m3u[state->discovered_m3u_count++] = (struct discovered_m3u_file){
        .path = path_copy,
        .file_size_bytes = file_size_bytes,
        .mtime_seconds = mtime_seconds,
        .needs_parse = true,
    };
}

static void walk_music_directory(struct scanner_work_state *state, const char *directory_path,
                                 uint32_t depth) {
    if (depth >= LIBRARY_SCAN_MAX_DEPTH || atomic_load(&state->owner->cancel_requested)) {
        return;
    }
    DIR *directory_handle = opendir(directory_path);
    if (directory_handle == NULL) {
        return;
    }

    int best_cover_rank = 999;
    char best_cover_path[512];
    uint64_t best_cover_size = 0u;
    int64_t best_cover_mtime = 0;
    bool directory_has_audio = false;
    best_cover_path[0] = '\0';

    struct dirent *entry = NULL;
    while ((entry = readdir(directory_handle)) != NULL) {
        if (atomic_load(&state->owner->cancel_requested)) {
            break;
        }
        if (should_skip_directory_name(entry->d_name)) {
            continue;
        }
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory_path, entry->d_name);
        struct stat entry_stat;
        if (stat(full_path, &entry_stat) != 0) {
            continue;
        }
        if (S_ISDIR(entry_stat.st_mode)) {
            walk_music_directory(state, full_path, depth + 1u);
        } else if (S_ISREG(entry_stat.st_mode)) {
            uint8_t format_kind = classify_audio_extension(entry->d_name);
            if (format_kind != LIBRARY_FORMAT_UNKNOWN) {
                directory_has_audio = true;
                record_discovered_audio(state, full_path, (uint64_t)entry_stat.st_size,
                                        normalize_stat_mtime(entry_stat.st_mtime), format_kind);
                atomic_store(&state->owner->atomic_discovered_files,
                             state->discovered_audio_count);
            } else if (is_m3u_filename(entry->d_name)) {
                record_discovered_m3u(state, full_path, (uint64_t)entry_stat.st_size,
                                      normalize_stat_mtime(entry_stat.st_mtime));
            } else {
                int cover_rank = classify_sidecar_cover_rank(entry->d_name);
                if (cover_rank >= 0 && cover_rank < best_cover_rank) {
                    best_cover_rank = cover_rank;
                    copy_string_bounded(best_cover_path, sizeof(best_cover_path), full_path,
                                        sizeof(best_cover_path) - 1u);
                    best_cover_size = (uint64_t)entry_stat.st_size;
                    best_cover_mtime = normalize_stat_mtime(entry_stat.st_mtime);
                }
            }
        }
    }
    closedir(directory_handle);

    if (directory_has_audio) {
        record_discovered_sidecar(state, directory_path,
                                  (best_cover_path[0] != '\0') ? best_cover_path : NULL,
                                  best_cover_size, best_cover_mtime);
    }
}

static void parse_m3u_into_db(struct library_db *database, const char *m3u_path) {
    FILE *file = fopen(m3u_path, "r");
    if (file == NULL) {
        return;
    }

    const char *slash = strrchr(m3u_path, '/');
    const char *base_name = (slash != NULL) ? (slash + 1) : m3u_path;
    const char *dot = strrchr(base_name, '.');
    size_t name_length = (dot != NULL) ? (size_t)(dot - base_name) : strlen(base_name);
    char playlist_name[128];
    copy_string_bounded(playlist_name, sizeof(playlist_name), base_name, name_length);

    char m3u_directory[320];
    if (slash != NULL) {
        copy_string_bounded(m3u_directory, sizeof(m3u_directory), m3u_path,
                            (size_t)(slash - m3u_path));
    } else {
        m3u_directory[0] = '\0';
    }

    char **track_paths = malloc(512u * sizeof(char *));
    if (track_paths == NULL) {
        fclose(file);
        return;
    }
    uint32_t track_count = 0u;
    char line[512];
    while (fgets(line, sizeof(line), file) != NULL && track_count < 512u) {
        size_t length = strlen(line);
        while (length > 0u && (line[length - 1u] == '\n' || line[length - 1u] == '\r')) {
            line[--length] = '\0';
        }
        if (length == 0u || line[0] == '#') {
            continue;
        }
        char resolved[512];
        if (line[0] == '/') {
            copy_string_bounded(resolved, sizeof(resolved), line, sizeof(resolved) - 1u);
        } else if (m3u_directory[0] != '\0') {
            snprintf(resolved, sizeof(resolved), "%s/%s", m3u_directory, line);
        } else {
            copy_string_bounded(resolved, sizeof(resolved), line, sizeof(resolved) - 1u);
        }
        char *copy = duplicate_c_string(resolved);
        if (copy != NULL) {
            track_paths[track_count++] = copy;
        }
    }
    fclose(file);

    if (track_count > 0u) {
        (void)library_db_upsert_m3u_playlist(database, playlist_name, m3u_path,
                                             (const char *const *)track_paths, track_count);
    }
    for (uint32_t index = 0u; index < track_count; index++) {
        free(track_paths[index]);
    }
    free(track_paths);
}

static void unlink_sharded_cover_file(const char *covers_directory, uint64_t album_uid) {
    if (covers_directory == NULL || covers_directory[0] == '\0' || album_uid == 0ull) {
        return;
    }
    char shard_path[512];
    snprintf(shard_path, sizeof(shard_path), "%s/%02x/%016" PRIx64 ".rgb565", covers_directory,
             (unsigned)((album_uid >> 56u) & 0xFFu), album_uid);
    (void)unlink(shard_path);
}

static int64_t find_unique_missing_by_stat(struct scanner_work_state *state,
                                           uint64_t file_size_bytes, int64_t mtime_seconds) {
    uint32_t match_index = UINT32_MAX;
    for (uint32_t index = 0u; index < state->cached_count; index++) {
        struct cached_track_stamp *cached = &state->cached_tracks[index];
        if (cached->matched) {
            continue;
        }
        if (cached->file_size_bytes == file_size_bytes &&
            timestamps_match_fat32(cached->mtime_seconds, mtime_seconds)) {
            if (match_index != UINT32_MAX) {
                return 0; /* Ambiguous: fall back to audio_fingerprint matching. */
            }
            match_index = index;
        }
    }
    if (match_index != UINT32_MAX) {
        state->cached_tracks[match_index].matched = true;
        return state->cached_tracks[match_index].track_id;
    }
    return 0;
}

static int64_t find_missing_by_fingerprint(struct scanner_work_state *state,
                                           uint64_t audio_fingerprint) {
    for (uint32_t index = 0u; index < state->cached_count; index++) {
        struct cached_track_stamp *cached = &state->cached_tracks[index];
        if (!cached->matched && cached->audio_fingerprint == audio_fingerprint) {
            cached->matched = true;
            return cached->track_id;
        }
    }
    return 0;
}

static void free_scanner_work_state(struct scanner_work_state *state) {
    for (uint32_t index = 0u; index < state->cached_count; index++) {
        free(state->cached_tracks[index].path);
    }
    free(state->cached_tracks);
    free(state->hash_slots);

    for (uint32_t index = 0u; index < state->cached_m3u_count; index++) {
        free(state->cached_m3u[index].path);
    }
    free(state->cached_m3u);

    for (uint32_t index = 0u; index < state->discovered_audio_count; index++) {
        free(state->discovered_audio[index].path);
    }
    free(state->discovered_audio);

    for (uint32_t index = 0u; index < state->discovered_sidecar_count; index++) {
        free(state->discovered_sidecars[index].directory_path);
        free(state->discovered_sidecars[index].cover_path);
    }
    free(state->discovered_sidecars);

    for (uint32_t index = 0u; index < state->discovered_m3u_count; index++) {
        free(state->discovered_m3u[index].path);
    }
    free(state->discovered_m3u);
}

static void *db_scanner_worker_main(void *argument) {
    struct library_db_scanner *scanner = (struct library_db_scanner *)argument;
    struct library_db_scan_telemetry telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    telemetry.kernel_tz_minutes_west = read_kernel_tz_minutes_west();

    struct stat root_stat;
    bool root_exists =
        (stat(scanner->root_path, &root_stat) == 0 && S_ISDIR(root_stat.st_mode));
    atomic_store(&scanner->atomic_music_root_exists, root_exists);
    if (!root_exists) {
        atomic_store(&scanner->atomic_phase, LIB_DB_SCAN_READY);
        atomic_store(&scanner->atomic_ready, true);
        return NULL;
    }

    struct scanner_work_state state;
    memset(&state, 0, sizeof(state));
    state.owner = scanner;

    library_db_for_each_track_stamp(scanner->database, collect_cached_track_stamp, &state);
    library_db_for_each_m3u_file(scanner->database, collect_cached_m3u_stamp, &state);
    build_cached_path_hash_table(&state);

    walk_music_directory(&state, scanner->root_path, 0u);
    if (atomic_load(&scanner->cancel_requested)) {
        free_scanner_work_state(&state);
        return NULL;
    }

    /* Pass 1: exact path matches first so moved files only claim truly missing entries. */
    uint32_t parse_needed_count = 0u;
    for (uint32_t index = 0u; index < state.discovered_audio_count; index++) {
        struct discovered_audio_file *discovered = &state.discovered_audio[index];
        struct cached_track_stamp *cached = lookup_cached_track_by_path(&state, discovered->path);
        if (cached != NULL) {
            cached->matched = true;
            discovered->matched_track_id = cached->track_id;
            if (cached->file_size_bytes == discovered->file_size_bytes &&
                timestamps_match_fat32(cached->mtime_seconds, discovered->mtime_seconds)) {
                discovered->needs_parse = false;
                telemetry.unchanged_count++;
            } else {
                discovered->needs_parse = true;
                parse_needed_count++;
            }
        }
    }

    /* Pass 2: fast-path unique (size, mtime) move match, then queue remaining for tag parse. */
    for (uint32_t index = 0u; index < state.discovered_audio_count; index++) {
        struct discovered_audio_file *discovered = &state.discovered_audio[index];
        if (discovered->matched_track_id > 0) {
            continue;
        }
        int64_t fast_moved_id = find_unique_missing_by_stat(&state, discovered->file_size_bytes,
                                                            discovered->mtime_seconds);
        if (fast_moved_id > 0) {
            discovered->matched_track_id = -fast_moved_id; /* Negative marks stat-only move. */
            discovered->needs_parse = false;
        } else {
            discovered->needs_parse = true;
            parse_needed_count++;
        }
    }

    bool any_m3u_dirty = false;
    for (uint32_t index = 0u; index < state.discovered_m3u_count; index++) {
        struct discovered_m3u_file *m3u = &state.discovered_m3u[index];
        m3u->needs_parse = true;
        for (uint32_t cached_idx = 0u; cached_idx < state.cached_m3u_count; cached_idx++) {
            struct cached_m3u_stamp *cached = &state.cached_m3u[cached_idx];
            if (strcmp(cached->path, m3u->path) == 0) {
                cached->matched = true;
                if (cached->file_size_bytes == m3u->file_size_bytes &&
                    timestamps_match_fat32(cached->mtime_seconds, m3u->mtime_seconds)) {
                    m3u->needs_parse = false;
                }
                break;
            }
        }
        if (m3u->needs_parse) {
            any_m3u_dirty = true;
        }
    }
    for (uint32_t cached_idx = 0u; cached_idx < state.cached_m3u_count; cached_idx++) {
        if (!state.cached_m3u[cached_idx].matched) {
            any_m3u_dirty = true;
            break;
        }
    }

    atomic_store(&scanner->atomic_delta_files, parse_needed_count);
    atomic_store(&scanner->atomic_phase, LIB_DB_SCAN_PARSING);

    bool has_track_or_cover_delta = false;
    for (uint32_t index = 0u; index < state.discovered_audio_count; index++) {
        if (state.discovered_audio[index].needs_parse ||
            state.discovered_audio[index].matched_track_id < 0) {
            has_track_or_cover_delta = true;
            break;
        }
    }
    if (!has_track_or_cover_delta) {
        for (uint32_t index = 0u; index < state.cached_count; index++) {
            if (!state.cached_tracks[index].matched) {
                has_track_or_cover_delta = true;
                break;
            }
        }
    }

    /* Even if 0 tracks changed, a sidecar cover.jpg might have been added, replaced, or deleted. */
    uint32_t pending_before_sidecars = library_db_count_pending_covers(scanner->database);
    if (!has_track_or_cover_delta && !any_m3u_dirty && state.discovered_sidecar_count > 0u) {
        if (library_db_begin(scanner->database)) {
            for (uint32_t index = 0u; index < state.discovered_sidecar_count; index++) {
                struct discovered_sidecar_cover *sidecar = &state.discovered_sidecars[index];
                (void)library_db_apply_sidecar_cover(scanner->database, sidecar->directory_path,
                                                     sidecar->cover_path, sidecar->mtime_seconds,
                                                     sidecar->file_size_bytes);
            }
            uint32_t pending_after_sidecars = library_db_count_pending_covers(scanner->database);
            if (pending_after_sidecars != pending_before_sidecars) {
                (void)library_db_commit(scanner->database);
                telemetry.db_writes = 1u;
                telemetry.sidecars_updated_count = 1u;
                has_track_or_cover_delta = true;
            } else {
                library_db_rollback(scanner->database);
            }
        }
    }

    if (!has_track_or_cover_delta && !any_m3u_dirty) {
        struct library_db_counts counts;
        if (library_db_read_counts(scanner->database, &counts)) {
            atomic_store(&scanner->atomic_album_count, counts.albums);
            atomic_store(&scanner->atomic_track_count, counts.tracks);
        }
        pthread_mutex_lock(&scanner->snapshot_mutex);
        scanner->telemetry = telemetry;
        pthread_mutex_unlock(&scanner->snapshot_mutex);
        atomic_store(&scanner->atomic_has_changes, false);
        atomic_store(&scanner->atomic_phase, LIB_DB_SCAN_READY);
        atomic_store(&scanner->atomic_ready, true);
        free_scanner_work_state(&state);
        return NULL;
    }

    if (library_db_begin(scanner->database)) {
        char scratch[2048];
        uint32_t parsed_progress = 0u;

        for (uint32_t index = 0u; index < state.discovered_audio_count; index++) {
            if (atomic_load(&scanner->cancel_requested)) {
                library_db_rollback(scanner->database);
                free_scanner_work_state(&state);
                return NULL;
            }
            struct discovered_audio_file *discovered = &state.discovered_audio[index];
            if (!discovered->needs_parse) {
                if (discovered->matched_track_id < 0) {
                    int64_t moved_id = -discovered->matched_track_id;
                    if (library_db_update_track_location(scanner->database, moved_id,
                                                         discovered->path,
                                                         discovered->mtime_seconds)) {
                        telemetry.moved_count++;
                    }
                }
                continue;
            }

            struct library_track_input legacy_input;
            telemetry.files_opened++;
            if (library_parse_audio_file_metadata(discovered->path, scanner->root_path,
                                                  &legacy_input, scratch, sizeof(scratch))) {
                uint64_t audio_fp = 0u;
                (void)library_compute_audio_fingerprint(discovered->path,
                                                        legacy_input.audio_start_offset,
                                                        legacy_input.audio_payload_bytes,
                                                        &audio_fp);

                int64_t target_id = discovered->matched_track_id;
                bool was_modified_in_place = (target_id > 0);
                bool was_moved_and_edited = false;
                if (target_id == 0) {
                    target_id = find_missing_by_fingerprint(&state, audio_fp);
                    was_moved_and_edited = (target_id > 0);
                }

                struct library_db_track_input db_input;
                memset(&db_input, 0, sizeof(db_input));
                db_input.path = discovered->path;
                db_input.audio_fingerprint = audio_fp;
                db_input.file_size_bytes = discovered->file_size_bytes;
                db_input.mtime_seconds = discovered->mtime_seconds;
                db_input.format_kind = legacy_input.format_kind;
                db_input.title = legacy_input.title;
                db_input.artist = legacy_input.artist;
                db_input.album_artist = legacy_input.album_artist;
                db_input.album = legacy_input.album;
                db_input.genre = legacy_input.genre;
                db_input.year = legacy_input.year;
                db_input.disc_number = legacy_input.disc_number;
                db_input.track_number = legacy_input.track_number;
                db_input.duration_seconds = legacy_input.duration_seconds;
                db_input.added_at_seconds = legacy_input.added_at_seconds;
                db_input.cover_source_kind = legacy_input.cover_source_kind;
                db_input.cover_path = legacy_input.cover_path;
                db_input.cover_data_offset = legacy_input.cover_data_offset;
                db_input.cover_data_length = legacy_input.cover_data_length;
                if (legacy_input.cover_source_kind == LIBRARY_COVER_SIDECAR &&
                    legacy_input.cover_path != NULL && legacy_input.cover_path[0] != '\0') {
                    struct stat cover_stat;
                    if (stat(legacy_input.cover_path, &cover_stat) == 0) {
                        db_input.cover_source_mtime = normalize_stat_mtime(cover_stat.st_mtime);
                        db_input.cover_source_size = (uint64_t)cover_stat.st_size;
                    }
                }

                if (library_db_upsert_track(scanner->database, &db_input, target_id, NULL)) {
                    if (was_modified_in_place) {
                        telemetry.modified_count++;
                    } else if (was_moved_and_edited) {
                        telemetry.moved_edited_count++;
                    } else {
                        telemetry.added_count++;
                    }
                }
            }
            parsed_progress++;
            atomic_store(&scanner->atomic_scanned_files, parsed_progress);
        }

        atomic_store(&scanner->atomic_phase, LIB_DB_SCAN_COMMITTING);

        for (uint32_t index = 0u; index < state.cached_count; index++) {
            if (!state.cached_tracks[index].matched) {
                if (library_db_delete_track(scanner->database, state.cached_tracks[index].track_id)) {
                    telemetry.deleted_count++;
                }
            }
        }

        for (uint32_t index = 0u; index < state.discovered_sidecar_count; index++) {
            struct discovered_sidecar_cover *sidecar = &state.discovered_sidecars[index];
            (void)library_db_apply_sidecar_cover(scanner->database, sidecar->directory_path,
                                                 sidecar->cover_path, sidecar->mtime_seconds,
                                                 sidecar->file_size_bytes);
        }

        for (uint32_t index = 0u; index < state.discovered_m3u_count; index++) {
            struct discovered_m3u_file *m3u = &state.discovered_m3u[index];
            if (m3u->needs_parse) {
                parse_m3u_into_db(scanner->database, m3u->path);
                (void)library_db_put_m3u_file(scanner->database, m3u->path, m3u->file_size_bytes,
                                              m3u->mtime_seconds);
                telemetry.m3u_parsed_count++;
            }
        }
        for (uint32_t index = 0u; index < state.cached_m3u_count; index++) {
            if (!state.cached_m3u[index].matched) {
                (void)library_db_remove_m3u_file(scanner->database, state.cached_m3u[index].path);
            }
        }

        uint64_t deleted_albums[DELETED_ALBUM_UNLINK_BATCH];
        uint32_t deleted_album_count = 0u;
        (void)library_db_recompute_aggregates(scanner->database, deleted_albums,
                                              DELETED_ALBUM_UNLINK_BATCH, &deleted_album_count);

        if (library_db_commit(scanner->database)) {
            telemetry.db_writes = 1u;
            for (uint32_t index = 0u; index < deleted_album_count; index++) {
                unlink_sharded_cover_file(scanner->covers_directory, deleted_albums[index]);
            }
        } else {
            library_db_rollback(scanner->database);
        }
    }

    struct library_db_counts counts;
    if (library_db_read_counts(scanner->database, &counts)) {
        atomic_store(&scanner->atomic_album_count, counts.albums);
        atomic_store(&scanner->atomic_track_count, counts.tracks);
    }

    pthread_mutex_lock(&scanner->snapshot_mutex);
    scanner->telemetry = telemetry;
    pthread_mutex_unlock(&scanner->snapshot_mutex);

    atomic_store(&scanner->atomic_has_changes, true);
    atomic_store(&scanner->atomic_phase, LIB_DB_SCAN_READY);
    atomic_store(&scanner->atomic_ready, true);
    free_scanner_work_state(&state);
    return NULL;
}

void library_db_scanner_initialize(struct library_db_scanner *scanner) {
    if (scanner == NULL) {
        return;
    }
    memset(scanner, 0, sizeof(*scanner));
    pthread_mutex_init(&scanner->snapshot_mutex, NULL);
    atomic_store(&scanner->atomic_phase, LIB_DB_SCAN_IDLE);
}

void library_db_scanner_cancel(struct library_db_scanner *scanner) {
    if (scanner == NULL) {
        return;
    }
    if (scanner->worker_running) {
        atomic_store(&scanner->cancel_requested, true);
        (void)pthread_join(scanner->worker_thread, NULL);
        scanner->worker_running = false;
    }
}

void library_db_scanner_destroy(struct library_db_scanner *scanner) {
    if (scanner == NULL) {
        return;
    }
    library_db_scanner_cancel(scanner);
    pthread_mutex_destroy(&scanner->snapshot_mutex);
    memset(scanner, 0, sizeof(*scanner));
}

bool library_db_scanner_start(struct library_db_scanner *scanner, struct library_db *database,
                              const char *music_root, const char *covers_directory) {
    if (scanner == NULL || database == NULL || music_root == NULL) {
        return false;
    }
    library_db_scanner_cancel(scanner);
    scanner->database = database;
    copy_string_bounded(scanner->root_path, sizeof(scanner->root_path), music_root,
                        sizeof(scanner->root_path) - 1u);
    copy_string_bounded(scanner->covers_directory, sizeof(scanner->covers_directory),
                        (covers_directory != NULL) ? covers_directory : "",
                        sizeof(scanner->covers_directory) - 1u);

    pthread_mutex_lock(&scanner->snapshot_mutex);
    memset(&scanner->telemetry, 0, sizeof(scanner->telemetry));
    pthread_mutex_unlock(&scanner->snapshot_mutex);

    atomic_store(&scanner->atomic_phase, LIB_DB_SCAN_DISCOVERING);
    atomic_store(&scanner->atomic_discovered_files, 0u);
    atomic_store(&scanner->atomic_delta_files, 0u);
    atomic_store(&scanner->atomic_scanned_files, 0u);
    atomic_store(&scanner->atomic_album_count, 0u);
    atomic_store(&scanner->atomic_track_count, 0u);
    atomic_store(&scanner->atomic_music_root_exists, true);
    atomic_store(&scanner->atomic_ready, false);
    atomic_store(&scanner->atomic_has_changes, false);
    atomic_store(&scanner->cancel_requested, false);

    if (pthread_create(&scanner->worker_thread, NULL, db_scanner_worker_main, scanner) == 0) {
        scanner->worker_running = true;
        return true;
    }
    atomic_store(&scanner->atomic_phase, LIB_DB_SCAN_IDLE);
    return false;
}

bool library_db_scanner_wait(struct library_db_scanner *scanner) {
    if (scanner == NULL) {
        return false;
    }
    if (scanner->worker_running) {
        (void)pthread_join(scanner->worker_thread, NULL);
        scanner->worker_running = false;
    }
    return atomic_load(&scanner->atomic_ready);
}

void library_db_scanner_poll(struct library_db_scanner *scanner,
                             struct library_db_scan_snapshot *snapshot_out) {
    if (scanner == NULL || snapshot_out == NULL) {
        return;
    }
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->phase = (enum library_db_scan_phase)atomic_load(&scanner->atomic_phase);
    snapshot_out->discovered_files = atomic_load(&scanner->atomic_discovered_files);
    snapshot_out->delta_files = atomic_load(&scanner->atomic_delta_files);
    snapshot_out->scanned_files = atomic_load(&scanner->atomic_scanned_files);
    snapshot_out->album_count = atomic_load(&scanner->atomic_album_count);
    snapshot_out->track_count = atomic_load(&scanner->atomic_track_count);
    snapshot_out->music_root_exists = atomic_load(&scanner->atomic_music_root_exists);
    snapshot_out->ready = atomic_load(&scanner->atomic_ready);
    snapshot_out->has_changes = atomic_load(&scanner->atomic_has_changes);
    pthread_mutex_lock(&scanner->snapshot_mutex);
    snapshot_out->telemetry = scanner->telemetry;
    pthread_mutex_unlock(&scanner->snapshot_mutex);
}
