#include "protocol.h"

#include "constants.h"

#include <stdio.h>
#include <string.h>

static bool is_ascii_whitespace(char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

static bool parse_hex_digit(char character, uint16_t *digit_out) {
    if (character >= '0' && character <= '9') {
        *digit_out = (uint16_t)(character - '0');
        return true;
    }
    if (character >= 'a' && character <= 'f') {
        *digit_out = (uint16_t)(10 + (character - 'a'));
        return true;
    }
    if (character >= 'A' && character <= 'F') {
        *digit_out = (uint16_t)(10 + (character - 'A'));
        return true;
    }
    return false;
}

static bool parse_hex_u16(const char *text, uint16_t *value_out) {
    uint16_t accumulated = 0u;
    for (size_t index = 0u; index < IPC_HEX_FIELD_BYTES; index++) {
        uint16_t digit = 0u;
        if (!parse_hex_digit(text[index], &digit)) {
            return false;
        }
        accumulated = (uint16_t)((accumulated << 4u) | digit);
    }
    *value_out = accumulated;
    return true;
}

static bool parse_hex_u32(const char *text, uint32_t *value_out) {
    uint32_t accumulated = 0u;
    for (size_t index = 0u; index < IPC_HEX_U32_BYTES; index++) {
        uint16_t digit = 0u;
        if (!parse_hex_digit(text[index], &digit)) {
            return false;
        }
        accumulated = (accumulated << 4u) | (uint32_t)digit;
    }
    *value_out = accumulated;
    return true;
}

static size_t skip_whitespace(const char *text, size_t length, size_t offset) {
    while (offset < length && is_ascii_whitespace(text[offset])) {
        offset++;
    }
    return offset;
}

static uint8_t clamp_u32_to_u8(uint32_t value, uint8_t minimum, uint8_t maximum) {
    if (value < (uint32_t)minimum) {
        return minimum;
    }
    if (value > (uint32_t)maximum) {
        return maximum;
    }
    return (uint8_t)value;
}

static void parse_aa24_json_payload(const char *payload, size_t payload_length,
                                    struct boot_config_snapshot *snapshot) {
    if (payload_length == 0u || payload[0] != '{') {
        return;
    }

    uint32_t parsed_volume = VOLUME_DEFAULT;
    uint32_t parsed_max_volume = MAX_VOLUME_DEFAULT;
    uint32_t parsed_brightness = BRIGHTNESS_BOOT_DEFAULT;
    bool has_volume = false;
    bool has_max_volume = false;
    bool has_brightness = false;
    size_t recognized_keys = 0u;

    int32_t brace_depth = 0;
    bool inside_string = false;
    bool escaped = false;
    bool expecting_key = false;

    size_t index = 0u;
    while (index < payload_length) {
        char character = payload[index];

        if (inside_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inside_string = false;
            }
            index++;
            continue;
        }

        if (character == '{') {
            brace_depth++;
            if (brace_depth == 1) {
                expecting_key = true;
            }
            index++;
            continue;
        }
        if (character == '}') {
            if (brace_depth > 0) {
                brace_depth--;
            }
            expecting_key = false;
            index++;
            continue;
        }
        if (brace_depth == 1 && character == ',') {
            expecting_key = true;
            index++;
            continue;
        }

        if (brace_depth == 1 && expecting_key && character == '"') {
            expecting_key = false;
            size_t key_start = index + 1u;
            size_t key_end = key_start;
            while (key_end < payload_length && payload[key_end] != '"') {
                if (payload[key_end] == '\\' && (key_end + 1u) < payload_length) {
                    key_end += 2u;
                } else {
                    key_end++;
                }
            }
            if (key_end >= payload_length) {
                break;
            }

            size_t key_length = key_end - key_start;
            size_t after_key = skip_whitespace(payload, payload_length, key_end + 1u);
            if (after_key >= payload_length || payload[after_key] != ':') {
                index = after_key;
                continue;
            }
            size_t value_start = skip_whitespace(payload, payload_length, after_key + 1u);
            if (value_start >= payload_length) {
                break;
            }

            bool is_volume_key = (key_length == 6u && memcmp(payload + key_start, "volume", 6u) == 0);
            bool is_max_volume_key =
                (key_length == 10u && memcmp(payload + key_start, "max_volume", 10u) == 0);
            bool is_brightness_key =
                (key_length == 10u && memcmp(payload + key_start, "brightness", 10u) == 0);

            if (is_volume_key || is_max_volume_key || is_brightness_key) {
                recognized_keys++;

                if (payload[value_start] >= '0' && payload[value_start] <= '9') {
                    uint32_t number = 0u;
                    size_t cursor = value_start;
                    while (cursor < payload_length && payload[cursor] >= '0' &&
                           payload[cursor] <= '9') {
                        uint32_t digit = (uint32_t)(payload[cursor] - '0');
                        if (number <= (UINT32_MAX - digit) / 10u) {
                            number = number * 10u + digit;
                        } else {
                            number = UINT32_MAX;
                        }
                        cursor++;
                    }
                    size_t after_number = skip_whitespace(payload, payload_length, cursor);
                    bool valid_delimiter = (after_number == payload_length ||
                                            payload[after_number] == ',' ||
                                            payload[after_number] == '}');
                    if (valid_delimiter) {
                        if (is_max_volume_key) {
                            parsed_max_volume = number;
                            has_max_volume = true;
                        } else if (is_volume_key) {
                            parsed_volume = number;
                            has_volume = true;
                        } else {
                            parsed_brightness = number;
                            has_brightness = true;
                        }
                    }
                    index = cursor;
                    continue;
                }
            }

            index = value_start;
            continue;
        }

        if (character == '"') {
            inside_string = true;
            expecting_key = false;
        } else if (!is_ascii_whitespace(character)) {
            expecting_key = false;
        }
        index++;
    }

    if (recognized_keys == 0u) {
        return;
    }

    snapshot->has_valid_payload = true;
    snapshot->max_volume = has_max_volume
                               ? clamp_u32_to_u8(parsed_max_volume, 1u, VOLUME_HARD_MAX)
                               : MAX_VOLUME_DEFAULT;
    snapshot->volume_level = has_volume
                                 ? clamp_u32_to_u8(parsed_volume, 0u, snapshot->max_volume)
                                 : clamp_u32_to_u8(VOLUME_DEFAULT, 0u, snapshot->max_volume);
    snapshot->brightness =
        has_brightness
            ? clamp_u32_to_u8(parsed_brightness, BRIGHTNESS_VISIBLE_FLOOR, BRIGHTNESS_MAX_DEFAULT)
            : BRIGHTNESS_BOOT_DEFAULT;
}

static bool parse_json_uint32_token(const char *text, size_t length, size_t value_start,
                                    uint32_t *number_out, size_t *cursor_out) {
    if (value_start >= length || text[value_start] < '0' || text[value_start] > '9') {
        return false;
    }
    uint32_t number = 0u;
    size_t cursor = value_start;
    while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
        uint32_t digit = (uint32_t)(text[cursor] - '0');
        if (number <= (UINT32_MAX - digit) / 10u) {
            number = number * 10u + digit;
        } else {
            number = UINT32_MAX;
        }
        cursor++;
    }
    size_t after_number = skip_whitespace(text, length, cursor);
    if (after_number < length && text[after_number] != ',' && text[after_number] != '}') {
        return false;
    }
    *number_out = number;
    *cursor_out = cursor;
    return true;
}

static size_t extract_unescaped_json_string(const char *payload, size_t payload_length,
                                            size_t quote_start, char *output_buffer,
                                            size_t output_capacity, size_t *next_offset_out) {
    size_t cursor = quote_start + 1u;
    size_t written = 0u;
    bool escaped = false;

    while (cursor < payload_length) {
        char character = payload[cursor];
        if (escaped) {
            char decoded = character;
            if (character == 'n') {
                decoded = '\n';
            } else if (character == 'r') {
                decoded = '\r';
            } else if (character == 't') {
                decoded = '\t';
            }
            if (output_buffer != NULL && written + 1u < output_capacity) {
                output_buffer[written++] = decoded;
            }
            escaped = false;
            cursor++;
            continue;
        }
        if (character == '\\') {
            escaped = true;
            cursor++;
            continue;
        }
        if (character == '"') {
            if (output_buffer != NULL && output_capacity > 0u) {
                output_buffer[written] = '\0';
            }
            *next_offset_out = cursor + 1u;
            return written;
        }
        if (output_buffer != NULL && written + 1u < output_capacity) {
            output_buffer[written++] = character;
        }
        cursor++;
    }

    if (output_buffer != NULL && output_capacity > 0u) {
        output_buffer[written] = '\0';
    }
    *next_offset_out = cursor;
    return written;
}

static void parse_a202_song_json(const char *song_json, size_t song_length,
                                 struct playback_status_update *status) {
    if (song_length == 0u || song_json[0] != '{') {
        return;
    }

    int32_t brace_depth = 0;
    bool inside_string = false;
    bool escaped = false;
    bool expecting_key = false;
    size_t index = 0u;

    while (index < song_length) {
        char character = song_json[index];
        if (inside_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inside_string = false;
            }
            index++;
            continue;
        }
        if (character == '{') {
            brace_depth++;
            if (brace_depth == 1) {
                expecting_key = true;
            }
            index++;
            continue;
        }
        if (character == '}') {
            if (brace_depth > 0) {
                brace_depth--;
            }
            expecting_key = false;
            index++;
            continue;
        }
        if (brace_depth == 1 && character == ',') {
            expecting_key = true;
            index++;
            continue;
        }
        if (brace_depth == 1 && expecting_key && character == '"') {
            expecting_key = false;
            size_t key_start = index + 1u;
            size_t key_end = key_start;
            while (key_end < song_length && song_json[key_end] != '"') {
                if (song_json[key_end] == '\\' && (key_end + 1u) < song_length) {
                    key_end += 2u;
                } else {
                    key_end++;
                }
            }
            if (key_end >= song_length) {
                break;
            }
            size_t key_length = key_end - key_start;
            size_t after_key = skip_whitespace(song_json, song_length, key_end + 1u);
            if (after_key >= song_length || song_json[after_key] != ':') {
                index = after_key;
                continue;
            }
            size_t value_start = skip_whitespace(song_json, song_length, after_key + 1u);
            if (value_start >= song_length) {
                break;
            }
            if (key_length == 9u && memcmp(song_json + key_start, "song_name", 9u) == 0 &&
                song_json[value_start] == '"') {
                size_t next_offset = value_start;
                extract_unescaped_json_string(song_json, song_length, value_start,
                                              status->track_title, TRACK_TITLE_CAPACITY,
                                              &next_offset);
                status->has_track_metadata = (status->track_title[0] != '\0');
                index = next_offset;
                continue;
            }
            if (key_length == 16u && memcmp(song_json + key_start, "song_artist_name", 16u) == 0 &&
                song_json[value_start] == '"') {
                size_t next_offset = value_start;
                extract_unescaped_json_string(song_json, song_length, value_start,
                                              status->artist_name, TRACK_METADATA_TEXT_CAPACITY,
                                              &next_offset);
                index = next_offset;
                continue;
            }
            if (key_length == 15u && memcmp(song_json + key_start, "song_album_name", 15u) == 0 &&
                song_json[value_start] == '"') {
                size_t next_offset = value_start;
                extract_unescaped_json_string(song_json, song_length, value_start,
                                              status->album_name, TRACK_METADATA_TEXT_CAPACITY,
                                              &next_offset);
                index = next_offset;
                continue;
            }
            if (key_length == 15u && memcmp(song_json + key_start, "song_style_name", 15u) == 0 &&
                song_json[value_start] == '"') {
                size_t next_offset = value_start;
                extract_unescaped_json_string(song_json, song_length, value_start,
                                              status->genre_name, TRACK_METADATA_TEXT_CAPACITY,
                                              &next_offset);
                index = next_offset;
                continue;
            }
            if (key_length == 14u && memcmp(song_json + key_start, "song_file_path", 14u) == 0 &&
                song_json[value_start] == '"') {
                size_t next_offset = value_start;
                extract_unescaped_json_string(song_json, song_length, value_start,
                                              status->track_path, TRACK_PATH_CAPACITY,
                                              &next_offset);
                status->has_track_path = (status->track_path[0] != '\0');
                index = next_offset;
                continue;
            }
            if (key_length == 18u &&
                memcmp(song_json + key_start, "song_duration_time", 18u) == 0) {
                uint32_t duration_ms = 0u;
                size_t next_cursor = value_start;
                if (parse_json_uint32_token(song_json, song_length, value_start, &duration_ms,
                                            &next_cursor)) {
                    status->duration_ms = duration_ms;
                    index = next_cursor;
                    continue;
                }
            }
            if (key_length == 16u &&
                memcmp(song_json + key_start, "song_sample_rate", 16u) == 0) {
                uint32_t sample_rate_hz = 0u;
                size_t next_cursor = value_start;
                if (parse_json_uint32_token(song_json, song_length, value_start, &sample_rate_hz,
                                            &next_cursor)) {
                    status->sample_rate_hz = sample_rate_hz;
                    status->has_audio_specs = true;
                    index = next_cursor;
                    continue;
                }
            }
            if (key_length == 18u &&
                memcmp(song_json + key_start, "song_encoding_rate", 18u) == 0) {
                uint32_t bit_depth = 0u;
                size_t next_cursor = value_start;
                if (parse_json_uint32_token(song_json, song_length, value_start, &bit_depth,
                                            &next_cursor)) {
                    status->bit_depth = (uint16_t)bit_depth;
                    status->has_audio_specs = true;
                    index = next_cursor;
                    continue;
                }
            }
            if (key_length == 13u &&
                memcmp(song_json + key_start, "song_bit_rate", 13u) == 0) {
                uint32_t bitrate_kbps = 0u;
                size_t next_cursor = value_start;
                if (parse_json_uint32_token(song_json, song_length, value_start, &bitrate_kbps,
                                            &next_cursor)) {
                    status->bitrate_kbps = bitrate_kbps;
                    status->has_audio_specs = true;
                    index = next_cursor;
                    continue;
                }
            }
            if (key_length == 10u && memcmp(song_json + key_start, "song_track", 10u) == 0) {
                uint32_t track_number = 0u;
                size_t next_cursor = value_start;
                if (parse_json_uint32_token(song_json, song_length, value_start, &track_number,
                                            &next_cursor)) {
                    status->track_number = (uint16_t)track_number;
                    index = next_cursor;
                    continue;
                }
            }
            if (key_length == 12u && memcmp(song_json + key_start, "song_channel", 12u) == 0) {
                uint32_t channels = 0u;
                size_t next_cursor = value_start;
                if (parse_json_uint32_token(song_json, song_length, value_start, &channels,
                                            &next_cursor)) {
                    status->channels = (uint16_t)channels;
                    index = next_cursor;
                    continue;
                }
            }
            if (key_length == 6u && memcmp(song_json + key_start, "is_dsd", 6u) == 0) {
                if (value_start + 4u <= song_length &&
                    memcmp(song_json + value_start, "true", 4u) == 0) {
                    status->is_dsd = true;
                    index = value_start + 4u;
                    continue;
                }
            }
            index = value_start;
            continue;
        }
        if (character == '"') {
            inside_string = true;
            expecting_key = false;
        } else if (!is_ascii_whitespace(character)) {
            expecting_key = false;
        }
        index++;
    }
}

static void parse_a202_json_payload(const char *payload, size_t payload_length,
                                    struct playback_status_update *status) {
    if (payload_length == 0u || payload[0] != '{') {
        return;
    }

    int32_t brace_depth = 0;
    bool inside_string = false;
    bool escaped = false;
    bool expecting_key = false;
    uint32_t raw_state = 0u;
    size_t index = 0u;

    while (index < payload_length) {
        char character = payload[index];
        if (inside_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inside_string = false;
            }
            index++;
            continue;
        }
        if (character == '{') {
            brace_depth++;
            if (brace_depth == 1) {
                expecting_key = true;
            }
            index++;
            continue;
        }
        if (character == '}') {
            if (brace_depth > 0) {
                brace_depth--;
            }
            expecting_key = false;
            index++;
            continue;
        }
        if (brace_depth == 1 && character == ',') {
            expecting_key = true;
            index++;
            continue;
        }
        if (brace_depth == 1 && expecting_key && character == '"') {
            expecting_key = false;
            size_t key_start = index + 1u;
            size_t key_end = key_start;
            while (key_end < payload_length && payload[key_end] != '"') {
                if (payload[key_end] == '\\' && (key_end + 1u) < payload_length) {
                    key_end += 2u;
                } else {
                    key_end++;
                }
            }
            if (key_end >= payload_length) {
                break;
            }
            size_t key_length = key_end - key_start;
            size_t after_key = skip_whitespace(payload, payload_length, key_end + 1u);
            if (after_key >= payload_length || payload[after_key] != ':') {
                index = after_key;
                continue;
            }
            size_t value_start = skip_whitespace(payload, payload_length, after_key + 1u);
            if (value_start >= payload_length) {
                break;
            }
            if (key_length == 5u && memcmp(payload + key_start, "state", 5u) == 0) {
                size_t next_cursor = value_start;
                if (parse_json_uint32_token(payload, payload_length, value_start, &raw_state,
                                            &next_cursor)) {
                    status->has_state = true;
                    index = next_cursor;
                    continue;
                }
            } else if (key_length == 4u && memcmp(payload + key_start, "song", 4u) == 0 &&
                       payload[value_start] == '"') {
                char unescaped_song[SONG_UNESCAPED_BUFFER_BYTES];
                size_t next_offset = value_start;
                size_t song_length = extract_unescaped_json_string(
                    payload, payload_length, value_start, unescaped_song, sizeof(unescaped_song),
                    &next_offset);
                if (song_length >= sizeof(unescaped_song)) {
                    song_length = sizeof(unescaped_song) - 1u;
                }
                parse_a202_song_json(unescaped_song, song_length, status);
                index = next_offset;
                continue;
            }
            index = value_start;
            continue;
        }
        if (character == '"') {
            inside_string = true;
            expecting_key = false;
        } else if (!is_ascii_whitespace(character)) {
            expecting_key = false;
        }
        index++;
    }

    /* Stock V2.57 emits state=2 with "song" when starting a track, and state=2 without "song" on EOF. */
    if (status->has_state) {
        if (raw_state == 0u) {
            status->state = IPC_PLAYBACK_PLAYING;
        } else if (raw_state == 1u) {
            status->state = IPC_PLAYBACK_PAUSED;
        } else if (raw_state == 2u) {
            bool has_track = (status->has_track_path || status->has_track_metadata);
            status->state = has_track ? IPC_PLAYBACK_PLAYING : IPC_PLAYBACK_STOPPED;
            status->is_loading_state = has_track;
        } else {
            status->state = IPC_PLAYBACK_IDLE;
        }
    }
}

bool protocol_parse_inbound_ipc(const char *frame_text, size_t frame_length,
                                struct inbound_ipc_message *message_out) {
    if (message_out == NULL) {
        return false;
    }

    memset(message_out, 0, sizeof(*message_out));
    message_out->kind = IPC_INBOUND_INVALID;
    message_out->boot_config.volume_level = VOLUME_DEFAULT;
    message_out->boot_config.max_volume = MAX_VOLUME_DEFAULT;
    message_out->boot_config.brightness = BRIGHTNESS_BOOT_DEFAULT;
    message_out->boot_config.has_valid_payload = false;

    if (frame_text == NULL || frame_length < IPC_COMPACT_HEADER_BYTES) {
        return false;
    }

    uint16_t declared_length = 0u;
    if (!parse_hex_u16(frame_text + IPC_OPCODE_BYTES, &declared_length)) {
        return false;
    }
    if (declared_length < IPC_COMPACT_HEADER_BYTES || (size_t)declared_length > frame_length) {
        return false;
    }

    /* Compact 8-byte header frames in stock V2.57: a202 (JSON) and a103 (8-hex ms) */
    if (memcmp(frame_text, "a202", IPC_OPCODE_BYTES) == 0) {
        message_out->kind = IPC_INBOUND_PLAYBACK_STATUS;
        size_t payload_offset = IPC_COMPACT_HEADER_BYTES;
        if ((size_t)declared_length > IPC_ENVELOPE_HEADER_BYTES &&
            frame_text[IPC_COMPACT_HEADER_BYTES] != '{' &&
            frame_text[IPC_ENVELOPE_HEADER_BYTES] == '{') {
            payload_offset = IPC_ENVELOPE_HEADER_BYTES;
        }
        parse_a202_json_payload(frame_text + payload_offset,
                                (size_t)declared_length - payload_offset,
                                &message_out->playback_status);
        return true;
    }

    if (memcmp(frame_text, "a103", IPC_OPCODE_BYTES) == 0) {
        if (declared_length < IPC_COMPACT_HEADER_BYTES + IPC_HEX_U32_BYTES) {
            return false;
        }
        uint32_t elapsed_ms = 0u;
        if (!parse_hex_u32(frame_text + IPC_COMPACT_HEADER_BYTES, &elapsed_ms)) {
            return false;
        }
        message_out->kind = IPC_INBOUND_PLAYBACK_ELAPSED;
        message_out->elapsed_ms = elapsed_ms;
        return true;
    }

    if (frame_length < IPC_ENVELOPE_HEADER_BYTES || declared_length < IPC_ENVELOPE_HEADER_BYTES) {
        return false;
    }

    uint16_t parameter = 0u;
    if (!parse_hex_u16(frame_text + IPC_OPCODE_BYTES + IPC_HEX_FIELD_BYTES, &parameter)) {
        return false;
    }

    if (memcmp(frame_text, "aa24", IPC_OPCODE_BYTES) == 0) {
        message_out->kind = IPC_INBOUND_BOOT_CONFIG;
        parse_aa24_json_payload(frame_text + IPC_ENVELOPE_HEADER_BYTES,
                                (size_t)declared_length - IPC_ENVELOPE_HEADER_BYTES,
                                &message_out->boot_config);
        return true;
    }

    if (memcmp(frame_text, "aa22", IPC_OPCODE_BYTES) == 0) {
        message_out->kind = IPC_INBOUND_SDCARD_EVENT;
        message_out->sdcard_inserted = (parameter == 1u);
        return true;
    }

    if (memcmp(frame_text, "a607", IPC_OPCODE_BYTES) == 0) {
        message_out->kind = IPC_INBOUND_INPUT_MODE_READY;
        return true;
    }

    if (memcmp(frame_text, "a102", IPC_OPCODE_BYTES) == 0) {
        message_out->kind = IPC_INBOUND_PLAYBACK_STOP;
        return true;
    }

    if (memcmp(frame_text, "a714", IPC_OPCODE_BYTES) == 0 ||
        memcmp(frame_text, "a715", IPC_OPCODE_BYTES) == 0) {
        message_out->kind = IPC_INBOUND_VOLUME_UPDATE;
        message_out->volume_level = clamp_u32_to_u8((uint32_t)parameter, 0u, VOLUME_HARD_MAX);
        return true;
    }

    if (memcmp(frame_text, "aa1f", IPC_OPCODE_BYTES) == 0) {
        message_out->kind = IPC_INBOUND_USB_CABLE_EVENT;
        message_out->usb_connected = (parameter == 1u);
        return true;
    }

    if (memcmp(frame_text, "aa1d", IPC_OPCODE_BYTES) == 0 ||
        memcmp(frame_text, "aa1e", IPC_OPCODE_BYTES) == 0) {
        message_out->kind = IPC_INBOUND_BATTERY_EVENT;
        return true;
    }

    message_out->kind = IPC_INBOUND_OTHER;
    return true;
}

size_t protocol_format_disable_idle_poweroff(char *output_buffer, size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity < IPC_COMMAND_BUFFER_BYTES) {
        return 0u;
    }
    memcpy(output_buffer, "0664000C0000", IPC_COMMAND_BUFFER_BYTES);
    return IPC_ENVELOPE_HEADER_BYTES;
}

size_t protocol_format_set_volume(uint8_t volume_level, char *output_buffer, size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity < IPC_COMMAND_BUFFER_BYTES) {
        return 0u;
    }
    uint8_t clamped_volume = volume_level > VOLUME_HARD_MAX ? VOLUME_HARD_MAX : volume_level;
    int written = snprintf(output_buffer, buffer_capacity, "0715000C%04X", (unsigned)clamped_volume);
    if (written != (int)IPC_ENVELOPE_HEADER_BYTES) {
        return 0u;
    }
    return IPC_ENVELOPE_HEADER_BYTES;
}

size_t protocol_format_play_m3u(const char *disk_m3u_path, char *output_buffer,
                                size_t buffer_capacity) {
    if (disk_m3u_path == NULL || disk_m3u_path[0] == '\0' || output_buffer == NULL) {
        return 0u;
    }
    size_t path_length = strlen(disk_m3u_path);
    size_t total_length = 16u + 1u + path_length; /* "0100LLLL00000004/" + path */
    if (total_length + 1u > buffer_capacity || total_length > (size_t)MESSAGE_QUEUE_MAX_MESSAGE_BYTES) {
        return 0u;
    }
    int written = snprintf(output_buffer, buffer_capacity, "0100%04X00000004/%s",
                           (unsigned)total_length, disk_m3u_path);
    if (written < 0 || (size_t)written != total_length) {
        return 0u;
    }
    return total_length;
}

size_t protocol_format_play_pause_toggle(char *output_buffer, size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity < IPC_COMMAND_BUFFER_BYTES) {
        return 0u;
    }
    memcpy(output_buffer, "0201000C0000", IPC_COMMAND_BUFFER_BYTES);
    return IPC_ENVELOPE_HEADER_BYTES;
}

size_t protocol_format_stop(char *output_buffer, size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity < IPC_COMMAND_BUFFER_BYTES) {
        return 0u;
    }
    memcpy(output_buffer, "0202000C0000", IPC_COMMAND_BUFFER_BYTES);
    return IPC_ENVELOPE_HEADER_BYTES;
}

size_t protocol_format_seek_ms(uint32_t target_ms, char *output_buffer, size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity < IPC_SEEK_COMMAND_BUFFER_BYTES) {
        return 0u;
    }
    int written = snprintf(output_buffer, buffer_capacity, "01030010%08X", (unsigned)target_ms);
    if (written != (int)IPC_SEEK_COMMAND_BYTES) {
        return 0u;
    }
    return IPC_SEEK_COMMAND_BYTES;
}

size_t protocol_format_set_play_mode(uint8_t play_mode, char *output_buffer, size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity < IPC_COMMAND_BUFFER_BYTES) {
        return 0u;
    }
    int written = snprintf(output_buffer, buffer_capacity, "0654000C%04X", (unsigned)play_mode);
    if (written != (int)IPC_ENVELOPE_HEADER_BYTES) {
        return 0u;
    }
    return IPC_ENVELOPE_HEADER_BYTES;
}

size_t protocol_format_set_max_volume(uint8_t max_volume, char *output_buffer,
                                    size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity < IPC_COMMAND_BUFFER_BYTES) {
        return 0u;
    }
    uint8_t clamped = clamp_u32_to_u8((uint32_t)max_volume, 1u, VOLUME_HARD_MAX);
    int written = snprintf(output_buffer, buffer_capacity, "0711000C%04X", (unsigned)clamped);
    if (written != (int)IPC_ENVELOPE_HEADER_BYTES) {
        return 0u;
    }
    return IPC_ENVELOPE_HEADER_BYTES;
}

size_t protocol_format_set_gapless(bool enabled, char *output_buffer, size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity < IPC_COMMAND_BUFFER_BYTES) {
        return 0u;
    }
    int written =
        snprintf(output_buffer, buffer_capacity, "0647000C%04X", enabled ? 1u : 0u);
    if (written != (int)IPC_ENVELOPE_HEADER_BYTES) {
        return 0u;
    }
    return IPC_ENVELOPE_HEADER_BYTES;
}

enum physical_button_action protocol_decode_input_event(const struct kernel_input_event *event) {
    if (event == NULL || event->type != INPUT_EVENT_TYPE_KEY) {
        return BUTTON_ACTION_NONE;
    }

    bool is_press = (event->value == INPUT_EVENT_VALUE_PRESS);
    bool is_repeat = (event->value == INPUT_EVENT_VALUE_REPEAT);
    if (!is_press && !is_repeat) {
        return BUTTON_ACTION_NONE;
    }

    switch (event->code) {
        case KEYCODE_VOLUME_UP_PRIMARY:
        case KEYCODE_VOLUME_UP_ALT_A:
        case KEYCODE_VOLUME_UP_ALT_B:
            return BUTTON_ACTION_VOLUME_UP;
        case KEYCODE_VOLUME_DOWN_PRIMARY:
        case KEYCODE_VOLUME_DOWN_ALT_A:
        case KEYCODE_VOLUME_DOWN_ALT_B:
            return BUTTON_ACTION_VOLUME_DOWN;
        case KEYCODE_PLAY_PAUSE:
            return is_press ? BUTTON_ACTION_PLAY_PAUSE : BUTTON_ACTION_NONE;
        case KEYCODE_POWER_SHORT_PRIMARY:
        case KEYCODE_POWER_SHORT_ALT:
            return is_press ? BUTTON_ACTION_POWER_SHORT : BUTTON_ACTION_NONE;
        case KEYCODE_POWER_LONG:
            return is_press ? BUTTON_ACTION_POWER_LONG : BUTTON_ACTION_NONE;
        default:
            return BUTTON_ACTION_NONE;
    }
}

static int32_t clamp_touch_coordinate(int32_t raw_coordinate) {
    if (raw_coordinate < 0) {
        return 0;
    }
    if (raw_coordinate > (int32_t)SCREEN_COORDINATE_MAX) {
        return (int32_t)SCREEN_COORDINATE_MAX;
    }
    return raw_coordinate;
}

void protocol_touch_decoder_initialize(struct touch_event_decoder *decoder) {
    if (decoder == NULL) {
        return;
    }
    memset(decoder, 0, sizeof(*decoder));
}

enum touch_event_kind protocol_touch_decoder_feed(struct touch_event_decoder *decoder,
                                                  const struct kernel_input_event *event,
                                                  struct touch_event *touch_out) {
    if (decoder == NULL || event == NULL || touch_out == NULL) {
        return TOUCH_EVENT_NONE;
    }

    if (event->type == INPUT_EVENT_TYPE_ABS) {
        if (event->code == ABS_CODE_MT_POSITION_X) {
            decoder->raw_x = clamp_touch_coordinate(event->value);
            decoder->position_dirty = true;
        } else if (event->code == ABS_CODE_MT_POSITION_Y) {
            decoder->raw_y = clamp_touch_coordinate(event->value);
            decoder->position_dirty = true;
        } else if (event->code == ABS_CODE_MT_TRACKING_ID) {
            if (event->value < 0) {
                if (decoder->contact_active) {
                    decoder->has_pending_release = true;
                }
            } else if (!decoder->contact_active) {
                decoder->contact_active = true;
                decoder->position_dirty = true;
            }
        }
        return TOUCH_EVENT_NONE;
    }

    if (event->type == INPUT_EVENT_TYPE_KEY && event->code == KEYCODE_BTN_TOUCH) {
        if (event->value == 0) {
            if (decoder->contact_active) {
                decoder->has_pending_release = true;
            }
        } else if (!decoder->contact_active) {
            decoder->contact_active = true;
            decoder->position_dirty = true;
        }
        return TOUCH_EVENT_NONE;
    }

    if (event->type == INPUT_EVENT_TYPE_SYN && event->code == SYN_CODE_REPORT) {
        int32_t logical_x = (int32_t)SCREEN_COORDINATE_MAX - decoder->raw_x;
        int32_t logical_y = (int32_t)SCREEN_COORDINATE_MAX - decoder->raw_y;
        uint64_t timestamp_ms = (uint64_t)event->seconds * MILLISECONDS_PER_SECOND +
                                (uint64_t)event->microseconds / MILLISECONDS_PER_SECOND;

        if (decoder->has_pending_release) {
            decoder->contact_active = false;
            decoder->down_emitted = false;
            decoder->has_pending_release = false;
            decoder->position_dirty = false;
            touch_out->kind = TOUCH_EVENT_UP;
            touch_out->x = logical_x;
            touch_out->y = logical_y;
            touch_out->timestamp_ms = timestamp_ms;
            return TOUCH_EVENT_UP;
        }

        if (decoder->contact_active && decoder->position_dirty) {
            enum touch_event_kind kind =
                decoder->down_emitted ? TOUCH_EVENT_MOVE : TOUCH_EVENT_DOWN;
            decoder->down_emitted = true;
            decoder->position_dirty = false;
            touch_out->kind = kind;
            touch_out->x = logical_x;
            touch_out->y = logical_y;
            touch_out->timestamp_ms = timestamp_ms;
            return kind;
        }
    }

    return TOUCH_EVENT_NONE;
}
