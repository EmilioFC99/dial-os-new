#include "now_playing_media.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LYRICS_MAX_FILE_BYTES (256u * 1024u)
#define LYRICS_TIMED_SYNTH_START_MS 6000u
#define LYRICS_TIMED_SYNTH_TAIL_MS 18000u

static void copy_text_bounded(char *destination, size_t capacity, const char *source) {
    if (destination == NULL || capacity == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    size_t length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1u;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static uint32_t read_be_u32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | (uint32_t)bytes[3];
}

static uint32_t read_le_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) | ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

static uint32_t read_syncsafe_u32(const uint8_t *bytes) {
    return (((uint32_t)bytes[0] & 0x7Fu) << 21u) | (((uint32_t)bytes[1] & 0x7Fu) << 14u) |
           (((uint32_t)bytes[2] & 0x7Fu) << 7u) | ((uint32_t)bytes[3] & 0x7Fu);
}

static bool parse_lrc_timestamp(const char *tag, size_t tag_len, uint32_t *time_ms_out) {
    /* Accepts mm:ss or mm:ss.xx or mm:ss.xxx */
    size_t colon_pos = 0u;
    bool found_colon = false;
    for (size_t index = 0u; index < tag_len; index++) {
        if (tag[index] == ':') {
            colon_pos = index;
            found_colon = true;
            break;
        }
        if (tag[index] < '0' || tag[index] > '9') {
            return false;
        }
    }
    if (!found_colon || colon_pos == 0u || colon_pos + 1u >= tag_len) {
        return false;
    }

    uint32_t minutes = 0u;
    for (size_t index = 0u; index < colon_pos; index++) {
        minutes = minutes * 10u + (uint32_t)(tag[index] - '0');
    }

    size_t cursor = colon_pos + 1u;
    uint32_t seconds = 0u;
    size_t sec_digits = 0u;
    while (cursor < tag_len && tag[cursor] >= '0' && tag[cursor] <= '9') {
        seconds = seconds * 10u + (uint32_t)(tag[cursor] - '0');
        sec_digits++;
        cursor++;
    }
    if (sec_digits == 0u) {
        return false;
    }

    uint32_t millis = 0u;
    if (cursor < tag_len) {
        if (tag[cursor] != '.' && tag[cursor] != ':') {
            return false;
        }
        cursor++;
        uint32_t factor = 100u;
        while (cursor < tag_len && tag[cursor] >= '0' && tag[cursor] <= '9') {
            if (factor > 0u) {
                millis += (uint32_t)(tag[cursor] - '0') * factor;
                factor /= 10u;
            }
            cursor++;
        }
        if (cursor < tag_len) {
            return false;
        }
    }

    *time_ms_out = (minutes * 60u + seconds) * MILLISECONDS_PER_SECOND + millis;
    return true;
}

static int compare_lyrics_cue(const void *first_ptr, const void *second_ptr) {
    const struct lyrics_cue *first = (const struct lyrics_cue *)first_ptr;
    const struct lyrics_cue *second = (const struct lyrics_cue *)second_ptr;
    if (first->time_ms < second->time_ms) {
        return -1;
    }
    if (first->time_ms > second->time_ms) {
        return 1;
    }
    return 0;
}

bool now_playing_media_parse_lrc_text(const char *text, size_t length,
                                      struct lyrics_document *document_out) {
    if (document_out == NULL) {
        return false;
    }
    memset(document_out, 0, sizeof(*document_out));
    if (text == NULL || length == 0u) {
        return false;
    }

    size_t start = 0u;
    if (length >= 3u && (uint8_t)text[0] == 0xEFu && (uint8_t)text[1] == 0xBBu &&
        (uint8_t)text[2] == 0xBFu) {
        start = 3u;
    }

    int32_t offset_ms = 0;
    size_t cursor = start;
    while (cursor < length && document_out->cue_count < LYRICS_MAX_CUES) {
        size_t line_start = cursor;
        while (cursor < length && text[cursor] != '\n' && text[cursor] != '\r') {
            cursor++;
        }
        size_t line_end = cursor;
        if (cursor < length && text[cursor] == '\r') {
            cursor++;
        }
        if (cursor < length && text[cursor] == '\n') {
            cursor++;
        }

        while (line_start < line_end &&
               (text[line_start] == ' ' || text[line_start] == '\t')) {
            line_start++;
        }
        while (line_end > line_start &&
               (text[line_end - 1u] == ' ' || text[line_end - 1u] == '\t')) {
            line_end--;
        }
        if (line_start >= line_end) {
            continue;
        }

        uint32_t timestamps[16];
        uint32_t timestamp_count = 0u;
        bool is_metadata_tag = false;
        size_t scan = line_start;

        while (scan < line_end && text[scan] == '[') {
            size_t close = scan + 1u;
            while (close < line_end && text[close] != ']') {
                close++;
            }
            if (close >= line_end) {
                break;
            }
            uint32_t parsed_ms = 0u;
            if (parse_lrc_timestamp(text + scan + 1u, close - (scan + 1u), &parsed_ms)) {
                if (timestamp_count < 16u) {
                    timestamps[timestamp_count++] = parsed_ms;
                }
                scan = close + 1u;
            } else {
                if (timestamp_count == 0u && scan == line_start) {
                    is_metadata_tag = true;
                    size_t tag_len = close - (scan + 1u);
                    const char *tag_ptr = text + scan + 1u;
                    if (tag_len > 7u && strncasecmp(tag_ptr, "offset:", 7u) == 0) {
                        char offset_buf[24];
                        size_t copy_len = tag_len - 7u;
                        if (copy_len >= sizeof(offset_buf)) {
                            copy_len = sizeof(offset_buf) - 1u;
                        }
                        memcpy(offset_buf, tag_ptr + 7u, copy_len);
                        offset_buf[copy_len] = '\0';
                        offset_ms = (int32_t)strtol(offset_buf, NULL, 10);
                    }
                }
                break;
            }
        }

        if (is_metadata_tag && scan == line_start) {
            continue;
        }

        while (scan < line_end && (text[scan] == ' ' || text[scan] == '\t')) {
            scan++;
        }
        if (scan >= line_end && timestamp_count == 0u) {
            continue;
        }

        char clean_line[LYRICS_LINE_CAPACITY];
        size_t clean_len = line_end - scan;
        if (clean_len >= sizeof(clean_line)) {
            clean_len = sizeof(clean_line) - 1u;
        }
        memcpy(clean_line, text + scan, clean_len);
        clean_line[clean_len] = '\0';

        if (timestamp_count > 0u) {
            document_out->has_timed = true;
            for (uint32_t idx = 0u;
                 idx < timestamp_count && document_out->cue_count < LYRICS_MAX_CUES; idx++) {
                struct lyrics_cue *cue = &document_out->cues[document_out->cue_count++];
                cue->time_ms = timestamps[idx];
                cue->has_timestamp = true;
                copy_text_bounded(cue->text, sizeof(cue->text), clean_line);
            }
        } else {
            struct lyrics_cue *cue = &document_out->cues[document_out->cue_count++];
            cue->time_ms = 0u;
            cue->has_timestamp = false;
            copy_text_bounded(cue->text, sizeof(cue->text), clean_line);
        }
    }

    if (document_out->has_timed && offset_ms != 0) {
        for (uint32_t idx = 0u; idx < document_out->cue_count; idx++) {
            if (!document_out->cues[idx].has_timestamp) {
                continue;
            }
            int64_t adjusted = (int64_t)document_out->cues[idx].time_ms - (int64_t)offset_ms;
            document_out->cues[idx].time_ms = adjusted < 0 ? 0u : (uint32_t)adjusted;
        }
    }

    if (document_out->has_timed && document_out->cue_count > 1u) {
        qsort(document_out->cues, document_out->cue_count, sizeof(struct lyrics_cue),
              compare_lyrics_cue);
    }
    return document_out->cue_count > 0u;
}

static bool has_extension_ci(const char *path, const char *extension) {
    size_t path_len = strlen(path);
    size_t ext_len = strlen(extension);
    if (path_len < ext_len) {
        return false;
    }
    return strcasecmp(path + path_len - ext_len, extension) == 0;
}

static bool try_load_sidecar_lrc(const char *track_path, struct lyrics_document *document_out) {
    const char *dot = strrchr(track_path, '.');
    const char *slash = strrchr(track_path, '/');
    size_t base_len = (dot != NULL && (slash == NULL || dot > slash))
                          ? (size_t)(dot - track_path)
                          : strlen(track_path);
    if (base_len + 6u >= LIBRARY_DB_PATH_BYTES) {
        return false;
    }
    static const char *const extensions[2] = {".lrc", ".LRC"};
    for (uint32_t idx = 0u; idx < 2u; idx++) {
        char lrc_path[LIBRARY_DB_PATH_BYTES];
        memcpy(lrc_path, track_path, base_len);
        memcpy(lrc_path + base_len, extensions[idx], 5u);
        FILE *file = library_fopen_readonly_noatime(lrc_path);
        if (file == NULL) {
            continue;
        }
        if (fseek(file, 0L, SEEK_END) != 0) {
            fclose(file);
            continue;
        }
        long size = ftell(file);
        if (size <= 0 || (uint64_t)size > LYRICS_MAX_FILE_BYTES || fseek(file, 0L, SEEK_SET) != 0) {
            fclose(file);
            continue;
        }
        char *text = malloc((size_t)size + 1u);
        if (text == NULL) {
            fclose(file);
            continue;
        }
        size_t read_bytes = fread(text, 1u, (size_t)size, file);
        fclose(file);
        text[read_bytes] = '\0';
        bool ok = now_playing_media_parse_lrc_text(text, read_bytes, document_out);
        free(text);
        if (ok) {
            document_out->source_kind = LYRICS_SOURCE_SIDECAR_LRC;
            copy_text_bounded(document_out->source_note, sizeof(document_out->source_note),
                              "SIDECAR .LRC");
            return true;
        }
    }
    return false;
}

static bool try_load_flac_lyrics(const char *track_path, struct lyrics_document *document_out) {
    FILE *file = library_fopen_readonly_noatime(track_path);
    if (file == NULL) {
        return false;
    }
    uint8_t header[4];
    if (fread(header, 1u, 4u, file) != 4u || memcmp(header, "fLaC", 4u) != 0) {
        fclose(file);
        return false;
    }

    bool found = false;
    bool is_last = false;
    while (!is_last) {
        uint8_t block_hdr[4];
        if (fread(block_hdr, 1u, 4u, file) != 4u) {
            break;
        }
        is_last = ((block_hdr[0] & 0x80u) != 0u);
        uint8_t block_type = (uint8_t)(block_hdr[0] & 0x7Fu);
        uint32_t block_len = ((uint32_t)block_hdr[1] << 16u) | ((uint32_t)block_hdr[2] << 8u) |
                             (uint32_t)block_hdr[3];
        if (block_type == 4u && block_len >= 8u && block_len <= LYRICS_MAX_FILE_BYTES) {
            uint8_t *payload = malloc(block_len);
            if (payload != NULL && fread(payload, 1u, block_len, file) == block_len) {
                uint32_t vendor_len = read_le_u32(payload);
                size_t offset = 4u + (size_t)vendor_len;
                if (offset + 4u <= block_len) {
                    uint32_t comment_count = read_le_u32(payload + offset);
                    offset += 4u;
                    struct lyrics_document *fallback_doc = NULL;
                    for (uint32_t idx = 0u; idx < comment_count && offset + 4u <= block_len; idx++) {
                        uint32_t entry_len = read_le_u32(payload + offset);
                        offset += 4u;
                        if (offset + entry_len > block_len) {
                            break;
                        }
                        const char *entry = (const char *)(payload + offset);
                        const char *tag_value = NULL;
                        size_t tag_value_len = 0u;
                        if (entry_len > 13u && strncasecmp(entry, "SYNCEDLYRICS=", 13u) == 0) {
                            tag_value = entry + 13u;
                            tag_value_len = entry_len - 13u;
                        } else if (entry_len > 7u && strncasecmp(entry, "LYRICS=", 7u) == 0) {
                            tag_value = entry + 7u;
                            tag_value_len = entry_len - 7u;
                        } else if (entry_len > 15u &&
                                   strncasecmp(entry, "UNSYNCEDLYRICS=", 15u) == 0) {
                            tag_value = entry + 15u;
                            tag_value_len = entry_len - 15u;
                        }
                        if (tag_value != NULL &&
                            now_playing_media_parse_lrc_text(tag_value, tag_value_len,
                                                             document_out)) {
                            if (document_out->has_timed) {
                                found = true;
                                break;
                            }
                            if (fallback_doc == NULL) {
                                fallback_doc = malloc(sizeof(*fallback_doc));
                                if (fallback_doc != NULL) {
                                    *fallback_doc = *document_out;
                                }
                            }
                        }
                        offset += entry_len;
                    }
                    if (!found && fallback_doc != NULL) {
                        *document_out = *fallback_doc;
                        found = true;
                    }
                    free(fallback_doc);
                }
            }
            free(payload);
            break;
        }
        if (fseek(file, (long)block_len, SEEK_CUR) != 0) {
            break;
        }
    }
    fclose(file);
    if (found) {
        document_out->source_kind = LYRICS_SOURCE_EMBEDDED;
        copy_text_bounded(document_out->source_note, sizeof(document_out->source_note),
                          "EMBEDDED FLAC TAG");
    }
    return found;
}

static size_t decode_id3_text_to_utf8(uint8_t encoding, const uint8_t *bytes, size_t len,
                                      char *out, size_t cap) {
    if (out == NULL || cap == 0u) {
        return 0u;
    }
    size_t written = 0u;
    if (encoding == 0u || encoding == 3u) {
        for (size_t idx = 0u; idx < len && written + 1u < cap; idx++) {
            if (bytes[idx] == '\0') {
                break;
            }
            out[written++] = (char)bytes[idx];
        }
    } else if (encoding == 1u || encoding == 2u) {
        bool little_endian = (encoding == 1u);
        size_t idx = 0u;
        if (len >= 2u && bytes[0] == 0xFFu && bytes[1] == 0xFEu) {
            little_endian = true;
            idx = 2u;
        } else if (len >= 2u && bytes[0] == 0xFEu && bytes[1] == 0xFFu) {
            little_endian = false;
            idx = 2u;
        }
        while (idx + 1u < len && written + 4u < cap) {
            uint16_t code = little_endian
                                ? (uint16_t)(bytes[idx] | ((uint16_t)bytes[idx + 1u] << 8u))
                                : (uint16_t)(((uint16_t)bytes[idx] << 8u) | bytes[idx + 1u]);
            idx += 2u;
            if (code == 0u) {
                break;
            }
            if (code < 0x80u) {
                out[written++] = (char)code;
            } else if (code < 0x800u) {
                out[written++] = (char)(0xC0u | (code >> 6u));
                out[written++] = (char)(0x80u | (code & 0x3Fu));
            } else {
                out[written++] = (char)(0xE0u | (code >> 12u));
                out[written++] = (char)(0x80u | ((code >> 6u) & 0x3Fu));
                out[written++] = (char)(0x80u | (code & 0x3Fu));
            }
        }
    }
    out[written] = '\0';
    return written;
}

static bool try_load_mp3_lyrics(const char *track_path, struct lyrics_document *document_out) {
    FILE *file = library_fopen_readonly_noatime(track_path);
    if (file == NULL) {
        return false;
    }
    uint8_t id3_hdr[10];
    if (fread(id3_hdr, 1u, 10u, file) != 10u || memcmp(id3_hdr, "ID3", 3u) != 0) {
        fclose(file);
        return false;
    }
    uint8_t major = id3_hdr[3];
    uint32_t tag_size = read_syncsafe_u32(id3_hdr + 6);
    if (major < 3u || tag_size < 10u || tag_size > LYRICS_MAX_FILE_BYTES) {
        fclose(file);
        return false;
    }

    uint8_t *tag_bytes = malloc(tag_size);
    if (tag_bytes == NULL || fread(tag_bytes, 1u, tag_size, file) != tag_size) {
        free(tag_bytes);
        fclose(file);
        return false;
    }
    fclose(file);

    bool found = false;
    size_t offset = 0u;
    if ((id3_hdr[5] & 0x40u) != 0u && tag_size >= 4u) {
        uint32_t ext_size =
            (major == 4u) ? read_syncsafe_u32(tag_bytes) : read_be_u32(tag_bytes);
        offset += (major == 4u) ? ext_size : (4u + ext_size);
    }

    while (offset + 10u <= tag_size) {
        if (tag_bytes[offset] == '\0') {
            break;
        }
        uint32_t frame_size = (major == 4u) ? read_syncsafe_u32(tag_bytes + offset + 4u)
                                            : read_be_u32(tag_bytes + offset + 4u);
        if (frame_size == 0u || offset + 10u + frame_size > tag_size) {
            break;
        }
        if (memcmp(tag_bytes + offset, "USLT", 4u) == 0 && frame_size > 4u) {
            const uint8_t *payload = tag_bytes + offset + 10u;
            uint8_t enc = payload[0];
            size_t pos = 4u; /* encoding + 3-byte language */
            if (enc == 1u || enc == 2u) {
                while (pos + 1u < frame_size && !(payload[pos] == 0u && payload[pos + 1u] == 0u)) {
                    pos += 2u;
                }
                if (pos + 1u < frame_size) {
                    pos += 2u;
                }
            } else {
                while (pos < frame_size && payload[pos] != 0u) {
                    pos++;
                }
                if (pos < frame_size) {
                    pos++;
                }
            }
            if (pos < frame_size) {
                char *utf8 = malloc(frame_size * 2u + 4u);
                if (utf8 != NULL) {
                    size_t utf8_len = decode_id3_text_to_utf8(
                        enc, payload + pos, frame_size - pos, utf8, frame_size * 2u + 4u);
                    found = now_playing_media_parse_lrc_text(utf8, utf8_len, document_out);
                    free(utf8);
                    if (found) {
                        document_out->source_kind = LYRICS_SOURCE_EMBEDDED;
                        copy_text_bounded(document_out->source_note,
                                          sizeof(document_out->source_note), "EMBEDDED USLT");
                        break;
                    }
                }
            }
        }
        offset += 10u + frame_size;
    }

    free(tag_bytes);
    return found;
}

static bool try_load_m4a_lyrics(const char *track_path, struct lyrics_document *document_out) {
    FILE *file = library_fopen_readonly_noatime(track_path);
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long file_size = ftell(file);
    if (file_size <= 16 || (uint64_t)file_size > (2u * 1024u * 1024u) ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    size_t scan_limit = (size_t)file_size;
    if (scan_limit > 262144u) {
        scan_limit = 262144u;
    }
    uint8_t *buffer = malloc(scan_limit);
    if (buffer == NULL || fread(buffer, 1u, scan_limit, file) != scan_limit) {
        free(buffer);
        fclose(file);
        return false;
    }
    fclose(file);

    bool found = false;
    const uint8_t lyr_tag[4] = {0xA9u, 'l', 'y', 'r'};
    for (size_t idx = 4u; idx + 16u < scan_limit; idx++) {
        if (memcmp(buffer + idx, lyr_tag, 4u) == 0) {
            uint32_t atom_size = read_be_u32(buffer + idx - 4u);
            if (atom_size > 24u && idx - 4u + atom_size <= scan_limit &&
                memcmp(buffer + idx + 8u, "data", 4u) == 0) {
                uint32_t data_size = read_be_u32(buffer + idx + 4u);
                if (data_size > 16u && idx + 4u + data_size <= scan_limit) {
                    const char *text = (const char *)(buffer + idx + 20u);
                    size_t text_len = data_size - 16u;
                    found = now_playing_media_parse_lrc_text(text, text_len, document_out);
                    if (found) {
                        document_out->source_kind = LYRICS_SOURCE_EMBEDDED;
                        copy_text_bounded(document_out->source_note,
                                          sizeof(document_out->source_note), "EMBEDDED M4A");
                        break;
                    }
                }
            }
        }
    }
    free(buffer);
    return found;
}

bool now_playing_media_load_lyrics_for_track(const char *track_path,
                                             struct lyrics_document *document_out) {
    if (document_out == NULL) {
        return false;
    }
    memset(document_out, 0, sizeof(*document_out));
    if (track_path == NULL || track_path[0] == '\0') {
        return false;
    }
    if (try_load_sidecar_lrc(track_path, document_out)) {
        return true;
    }
    if (has_extension_ci(track_path, ".flac")) {
        return try_load_flac_lyrics(track_path, document_out);
    }
    if (has_extension_ci(track_path, ".mp3")) {
        return try_load_mp3_lyrics(track_path, document_out);
    }
    if (has_extension_ci(track_path, ".m4a") || has_extension_ci(track_path, ".mp4")) {
        return try_load_m4a_lyrics(track_path, document_out);
    }
    return false;
}

void now_playing_media_resolve_cue(const struct lyrics_document *document,
                                   enum lyrics_mode requested_mode, uint32_t elapsed_ms,
                                   uint32_t duration_ms,
                                   struct lyrics_resolved_state *resolved_out) {
    if (resolved_out == NULL) {
        return;
    }
    memset(resolved_out, 0, sizeof(*resolved_out));
    resolved_out->active_index = -1;
    resolved_out->upcoming_index = -1;

    if (document == NULL || document->cue_count == 0u || requested_mode == LYRICS_MODE_NONE) {
        resolved_out->effective_mode = LYRICS_MODE_NONE;
        copy_text_bounded(resolved_out->status_note, sizeof(resolved_out->status_note),
                          "NO EMBEDDED OR .LRC FILE");
        return;
    }

    enum lyrics_mode mode = requested_mode;
    if (mode == LYRICS_MODE_AUTO) {
        mode = document->has_timed ? LYRICS_MODE_TIMED : LYRICS_MODE_UNTIMED;
    }
    resolved_out->effective_mode = mode;
    copy_text_bounded(resolved_out->status_note, sizeof(resolved_out->status_note),
                      document->source_note);

    uint32_t safe_duration = duration_ms > 0u ? duration_ms : 180000u;

    if (mode == LYRICS_MODE_TIMED) {
        if (document->has_timed) {
            uint32_t first_ms = document->cues[0].time_ms;
            if (elapsed_ms < first_ms) {
                resolved_out->is_intro_cue = true;
                resolved_out->active_index = -1;
                resolved_out->upcoming_index = 0;
                copy_text_bounded(resolved_out->upcoming_text, sizeof(resolved_out->upcoming_text),
                                  document->cues[0].text);
                return;
            }
            int32_t active = 0;
            for (uint32_t idx = 0u; idx < document->cue_count; idx++) {
                if (document->cues[idx].time_ms <= elapsed_ms) {
                    active = (int32_t)idx;
                } else {
                    break;
                }
            }
            resolved_out->active_index = active;
            copy_text_bounded(resolved_out->active_text, sizeof(resolved_out->active_text),
                              document->cues[active].text);
            if ((uint32_t)(active + 1) < document->cue_count) {
                resolved_out->upcoming_index = active + 1;
                copy_text_bounded(resolved_out->upcoming_text, sizeof(resolved_out->upcoming_text),
                                  document->cues[active + 1].text);
            }
            return;
        }

        /* Synthesize evenly timed cues starting at 6s with 18s tail margin */
        uint32_t start_ms = safe_duration > 30000u ? LYRICS_TIMED_SYNTH_START_MS : 1000u;
        uint32_t tail_ms = safe_duration > 30000u ? LYRICS_TIMED_SYNTH_TAIL_MS : 2000u;
        uint32_t span_ms = safe_duration > (start_ms + tail_ms)
                               ? (safe_duration - start_ms - tail_ms)
                               : safe_duration;
        if (elapsed_ms < start_ms) {
            resolved_out->is_intro_cue = true;
            resolved_out->upcoming_index = 0;
            copy_text_bounded(resolved_out->upcoming_text, sizeof(resolved_out->upcoming_text),
                              document->cues[0].text);
            return;
        }
        uint32_t progress = elapsed_ms - start_ms;
        uint32_t idx = (uint32_t)(((uint64_t)progress * document->cue_count) /
                                  (span_ms > 0u ? span_ms : 1u));
        if (idx >= document->cue_count) {
            idx = document->cue_count - 1u;
        }
        resolved_out->active_index = (int32_t)idx;
        copy_text_bounded(resolved_out->active_text, sizeof(resolved_out->active_text),
                          document->cues[idx].text);
        if (idx + 1u < document->cue_count) {
            resolved_out->upcoming_index = (int32_t)(idx + 1u);
            copy_text_bounded(resolved_out->upcoming_text, sizeof(resolved_out->upcoming_text),
                              document->cues[idx + 1u].text);
        }
        return;
    }

    /* LYRICS_MODE_UNTIMED (Auto Pace): 3%..96% of track duration, no Intro Cue */
    uint32_t start_ms = (uint32_t)(((uint64_t)safe_duration * 3u) / 100u);
    uint32_t end_ms = (uint32_t)(((uint64_t)safe_duration * 96u) / 100u);
    uint32_t span_ms = end_ms > start_ms ? (end_ms - start_ms) : safe_duration;
    uint32_t idx = 0u;
    if (elapsed_ms > start_ms) {
        idx = (uint32_t)(((uint64_t)(elapsed_ms - start_ms) * document->cue_count) /
                         (span_ms > 0u ? span_ms : 1u));
        if (idx >= document->cue_count) {
            idx = document->cue_count - 1u;
        }
    }
    resolved_out->active_index = (int32_t)idx;
    copy_text_bounded(resolved_out->active_text, sizeof(resolved_out->active_text),
                      document->cues[idx].text);
    if (idx + 1u < document->cue_count) {
        resolved_out->upcoming_index = (int32_t)(idx + 1u);
        copy_text_bounded(resolved_out->upcoming_text, sizeof(resolved_out->upcoming_text),
                          document->cues[idx + 1u].text);
    }
}

static void *media_worker_main(void *argument) {
    struct now_playing_media *media = (struct now_playing_media *)argument;
    for (;;) {
        pthread_mutex_lock(&media->mutex);
        while (!media->worker_stop && !media->has_pending_active && !media->has_pending_peek) {
            pthread_cond_wait(&media->cond, &media->mutex);
        }
        if (media->worker_stop) {
            pthread_mutex_unlock(&media->mutex);
            break;
        }
        bool do_active = media->has_pending_active;
        int64_t album_id = do_active ? media->req_active_album_id : media->req_peek_album_id;
        uint64_t album_uid = do_active ? media->req_active_album_uid : media->req_peek_album_uid;
        bool need_cover = !do_active || !media->active_cover_ready ||
                          media->active_cover_album_uid != album_uid;
        char track_path[LIBRARY_DB_PATH_BYTES];
        track_path[0] = '\0';
        if (do_active) {
            copy_text_bounded(track_path, sizeof(track_path), media->req_active_track_path);
            media->has_pending_active = false;
        } else {
            media->has_pending_peek = false;
        }
        pthread_mutex_unlock(&media->mutex);

        if (do_active && track_path[0] != '\0') {
            bool lyrics_ok =
                now_playing_media_load_lyrics_for_track(track_path, &media->staging_lyrics);
            pthread_mutex_lock(&media->mutex);
            if (strcmp(media->req_active_track_path, track_path) == 0) {
                if (lyrics_ok) {
                    media->active_lyrics = media->staging_lyrics;
                } else {
                    memset(&media->active_lyrics, 0, sizeof(media->active_lyrics));
                }
                copy_text_bounded(media->active_lyrics_path, sizeof(media->active_lyrics_path),
                                  track_path);
                media->active_lyrics_ready = true;
            }
            pthread_mutex_unlock(&media->mutex);
        }

        if (!need_cover) {
            continue;
        }

        bool cover_ok = false;
        if (album_id > 0 && media->database != NULL && media->staging_cover_pixels != NULL) {
            struct library_db_cover_job job;
            if (library_db_get_album_cover_job(media->database, album_id, &job)) {
                cover_ok = library_cover_decode_job_to_rgb565(&job, NOW_PLAYING_COVER_SIZE_PX,
                                                              media->staging_cover_pixels);
            }
        }

        pthread_mutex_lock(&media->mutex);
        if (do_active) {
            if (media->req_active_album_uid == album_uid && cover_ok &&
                media->active_cover_pixels != NULL) {
                memcpy(media->active_cover_pixels, media->staging_cover_pixels,
                       NOW_PLAYING_COVER_PIXEL_COUNT * sizeof(uint16_t));
                media->active_cover_album_uid = album_uid;
                media->active_cover_ready = true;
            }
        } else {
            if (media->req_peek_album_uid == album_uid && cover_ok &&
                media->peek_cover_pixels != NULL) {
                memcpy(media->peek_cover_pixels, media->staging_cover_pixels,
                       NOW_PLAYING_COVER_PIXEL_COUNT * sizeof(uint16_t));
                media->peek_cover_album_uid = album_uid;
                media->peek_cover_ready = true;
            }
        }
        pthread_mutex_unlock(&media->mutex);
    }
    return NULL;
}

void now_playing_media_initialize(struct now_playing_media *media, struct library_db *database) {
    if (media == NULL) {
        return;
    }
    memset(media, 0, sizeof(*media));
    media->database = database;
    media->active_cover_pixels = calloc(NOW_PLAYING_COVER_PIXEL_COUNT, sizeof(uint16_t));
    media->peek_cover_pixels = calloc(NOW_PLAYING_COVER_PIXEL_COUNT, sizeof(uint16_t));
    media->staging_cover_pixels = calloc(NOW_PLAYING_COVER_PIXEL_COUNT, sizeof(uint16_t));
    pthread_mutex_init(&media->mutex, NULL);
    pthread_cond_init(&media->cond, NULL);
    if (pthread_create(&media->worker_thread, NULL, media_worker_main, media) == 0) {
        media->worker_running = true;
    }
}

void now_playing_media_destroy(struct now_playing_media *media) {
    if (media == NULL) {
        return;
    }
    if (media->worker_running) {
        pthread_mutex_lock(&media->mutex);
        media->worker_stop = true;
        pthread_cond_broadcast(&media->cond);
        pthread_mutex_unlock(&media->mutex);
        pthread_join(media->worker_thread, NULL);
        media->worker_running = false;
    }
    pthread_cond_destroy(&media->cond);
    pthread_mutex_destroy(&media->mutex);
    free(media->active_cover_pixels);
    free(media->peek_cover_pixels);
    free(media->staging_cover_pixels);
    memset(media, 0, sizeof(*media));
}

void now_playing_media_request_active(struct now_playing_media *media, int64_t album_id,
                                      uint64_t album_uid, const char *track_path) {
    if (media == NULL) {
        return;
    }
    pthread_mutex_lock(&media->mutex);
    bool cover_same = (album_uid != 0ull && media->active_cover_ready &&
                       media->active_cover_album_uid == album_uid);
    bool lyrics_same = (track_path != NULL && media->active_lyrics_ready &&
                        strcmp(media->active_lyrics_path, track_path) == 0);
    if (!cover_same) {
        media->active_cover_ready = false;
        media->active_cover_album_uid = album_uid;
    }
    if (!lyrics_same) {
        media->active_lyrics_ready = false;
        media->active_lyrics_path[0] = '\0';
        memset(&media->active_lyrics, 0, sizeof(media->active_lyrics));
    }
    media->req_active_album_id = album_id;
    media->req_active_album_uid = album_uid;
    copy_text_bounded(media->req_active_track_path, sizeof(media->req_active_track_path),
                      track_path != NULL ? track_path : "");
    media->has_pending_active = (!cover_same || !lyrics_same);
    if (media->has_pending_active) {
        pthread_cond_signal(&media->cond);
    }
    pthread_mutex_unlock(&media->mutex);
}

void now_playing_media_request_peek(struct now_playing_media *media, int64_t album_id,
                                    uint64_t album_uid) {
    if (media == NULL || album_uid == 0ull) {
        return;
    }
    pthread_mutex_lock(&media->mutex);
    if (media->peek_cover_ready && media->peek_cover_album_uid == album_uid) {
        pthread_mutex_unlock(&media->mutex);
        return;
    }
    media->peek_cover_ready = false;
    media->peek_cover_album_uid = album_uid;
    media->req_peek_album_id = album_id;
    media->req_peek_album_uid = album_uid;
    media->has_pending_peek = true;
    pthread_cond_signal(&media->cond);
    pthread_mutex_unlock(&media->mutex);
}

void now_playing_media_clear(struct now_playing_media *media) {
    if (media == NULL) {
        return;
    }
    pthread_mutex_lock(&media->mutex);
    media->has_pending_active = false;
    media->has_pending_peek = false;
    media->active_cover_ready = false;
    media->active_cover_album_uid = 0ull;
    media->peek_cover_ready = false;
    media->peek_cover_album_uid = 0ull;
    media->active_lyrics_ready = false;
    media->active_lyrics_path[0] = '\0';
    memset(&media->active_lyrics, 0, sizeof(media->active_lyrics));
    pthread_mutex_unlock(&media->mutex);
}

const uint16_t *now_playing_media_get_active_cover(struct now_playing_media *media,
                                                   uint64_t album_uid,
                                                   struct library_cover_cache *fallback_cache,
                                                   uint32_t *size_out) {
    if (size_out != NULL) {
        *size_out = 0u;
    }
    if (album_uid == 0ull) {
        return NULL;
    }
    if (media != NULL) {
        const uint16_t *ready_pixels = NULL;
        pthread_mutex_lock(&media->mutex);
        if (media->active_cover_ready && media->active_cover_album_uid == album_uid &&
            media->active_cover_pixels != NULL) {
            ready_pixels = media->active_cover_pixels;
        }
        pthread_mutex_unlock(&media->mutex);
        if (ready_pixels != NULL) {
            if (size_out != NULL) {
                *size_out = NOW_PLAYING_COVER_SIZE_PX;
            }
            return ready_pixels;
        }
    }
    if (fallback_cache != NULL) {
        const uint16_t *thumb = library_cover_cache_lookup_or_load_disk(fallback_cache, album_uid);
        if (thumb != NULL && size_out != NULL) {
            *size_out = LIBRARY_COVER_SIZE_PX;
        }
        return thumb;
    }
    return NULL;
}

const uint16_t *now_playing_media_get_peek_cover(struct now_playing_media *media,
                                                 uint64_t album_uid,
                                                 struct library_cover_cache *fallback_cache,
                                                 uint32_t *size_out) {
    if (size_out != NULL) {
        *size_out = 0u;
    }
    if (album_uid == 0ull) {
        return NULL;
    }
    if (media != NULL) {
        const uint16_t *ready_pixels = NULL;
        pthread_mutex_lock(&media->mutex);
        if (media->peek_cover_ready && media->peek_cover_album_uid == album_uid &&
            media->peek_cover_pixels != NULL) {
            ready_pixels = media->peek_cover_pixels;
        }
        pthread_mutex_unlock(&media->mutex);
        if (ready_pixels != NULL) {
            if (size_out != NULL) {
                *size_out = NOW_PLAYING_COVER_SIZE_PX;
            }
            return ready_pixels;
        }
    }
    if (fallback_cache != NULL) {
        const uint16_t *thumb = library_cover_cache_lookup_or_load_disk(fallback_cache, album_uid);
        if (thumb != NULL && size_out != NULL) {
            *size_out = LIBRARY_COVER_SIZE_PX;
        }
        return thumb;
    }
    return NULL;
}

void now_playing_media_get_lyrics_badge(struct now_playing_media *media, const char *track_path,
                                        char *label_out, size_t label_capacity,
                                        bool *is_timed_out, bool *has_lyrics_out) {
    if (is_timed_out != NULL) {
        *is_timed_out = false;
    }
    if (has_lyrics_out != NULL) {
        *has_lyrics_out = false;
    }
    if (label_out == NULL || label_capacity == 0u) {
        return;
    }
    if (media == NULL || track_path == NULL || track_path[0] == '\0') {
        copy_text_bounded(label_out, label_capacity, "LYRICS · NONE");
        return;
    }

    pthread_mutex_lock(&media->mutex);
    bool ready = media->active_lyrics_ready &&
                 strcmp(media->active_lyrics_path, track_path) == 0;
    enum lyrics_source_kind source_kind = media->active_lyrics.source_kind;
    bool has_timed = media->active_lyrics.has_timed;
    uint32_t cue_count = media->active_lyrics.cue_count;
    pthread_mutex_unlock(&media->mutex);

    if (!ready) {
        copy_text_bounded(label_out, label_capacity, "LYRICS · LOADING");
        return;
    }
    if (cue_count == 0u || source_kind == LYRICS_SOURCE_NONE) {
        copy_text_bounded(label_out, label_capacity, "LYRICS · NONE");
        return;
    }

    if (source_kind == LYRICS_SOURCE_SIDECAR_LRC) {
        copy_text_bounded(label_out, label_capacity,
                          has_timed ? "LYRICS · TIMED (.LRC)" : "LYRICS · UNTIMED (.LRC)");
    } else {
        copy_text_bounded(label_out, label_capacity,
                          has_timed ? "LYRICS · TIMED (TAG)" : "LYRICS · UNTIMED (TAG)");
    }
    if (has_lyrics_out != NULL) {
        *has_lyrics_out = true;
    }
    if (is_timed_out != NULL) {
        *is_timed_out = has_timed;
    }
}

void now_playing_media_get_lyrics_details(struct now_playing_media *media,
                                          const char *track_path,
                                          struct lyrics_badge_details *details_out) {
    if (details_out == NULL) {
        return;
    }
    memset(details_out, 0, sizeof(*details_out));

    if (media == NULL || track_path == NULL || track_path[0] == '\0') {
        copy_text_bounded(details_out->badge_label, sizeof(details_out->badge_label), "NO LYRICS");
        copy_text_bounded(details_out->banner_title, sizeof(details_out->banner_title),
                          "NO LYRICS FOUND");
        copy_text_bounded(details_out->banner_subtitle, sizeof(details_out->banner_subtitle),
                          "NO EMBEDDED TAG OR .LRC FILE");
        details_out->is_ready = true;
        return;
    }

    char source_note[64];
    pthread_mutex_lock(&media->mutex);
    bool ready = media->active_lyrics_ready &&
                 strcmp(media->active_lyrics_path, track_path) == 0;
    enum lyrics_source_kind source_kind = media->active_lyrics.source_kind;
    bool has_timed = media->active_lyrics.has_timed;
    uint32_t cue_count = media->active_lyrics.cue_count;
    copy_text_bounded(source_note, sizeof(source_note), media->active_lyrics.source_note);
    pthread_mutex_unlock(&media->mutex);

    if (!ready) {
        copy_text_bounded(details_out->badge_label, sizeof(details_out->badge_label), "LOADING");
        copy_text_bounded(details_out->banner_title, sizeof(details_out->banner_title),
                          "LOADING LYRICS");
        copy_text_bounded(details_out->banner_subtitle, sizeof(details_out->banner_subtitle),
                          "READING TRACK METADATA");
        return;
    }

    details_out->is_ready = true;
    if (cue_count == 0u || source_kind == LYRICS_SOURCE_NONE) {
        copy_text_bounded(details_out->badge_label, sizeof(details_out->badge_label), "NO LYRICS");
        copy_text_bounded(details_out->banner_title, sizeof(details_out->banner_title),
                          "NO LYRICS FOUND");
        copy_text_bounded(details_out->banner_subtitle, sizeof(details_out->banner_subtitle),
                          "NO EMBEDDED TAG OR .LRC FILE");
        return;
    }

    details_out->has_lyrics = true;
    details_out->is_timed = has_timed;
    copy_text_bounded(details_out->badge_label, sizeof(details_out->badge_label),
                      has_timed ? "TIMED" : "UNTIMED");
    if (source_kind == LYRICS_SOURCE_SIDECAR_LRC) {
        copy_text_bounded(details_out->banner_title, sizeof(details_out->banner_title),
                          has_timed ? "TIMED LYRICS (.LRC)" : "UNTIMED LYRICS (.LRC)");
        copy_text_bounded(details_out->banner_subtitle, sizeof(details_out->banner_subtitle),
                          has_timed ? "SYNCED FROM SIDECAR .LRC FILE"
                                    : "AUTO-PACED · SIDECAR .LRC FILE");
    } else {
        copy_text_bounded(details_out->banner_title, sizeof(details_out->banner_title),
                          has_timed ? "TIMED EMBEDDED LYRICS" : "UNTIMED EMBEDDED LYRICS");
        const char *tag_source = source_note[0] != '\0' ? source_note : "EMBEDDED TAG";
        snprintf(details_out->banner_subtitle, sizeof(details_out->banner_subtitle), "%s · %s",
                 has_timed ? "SYNCED" : "AUTO-PACED", tag_source);
    }
}
