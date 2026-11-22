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

    if (frame_text == NULL || frame_length < IPC_ENVELOPE_HEADER_BYTES) {
        return false;
    }

    uint16_t declared_length = 0u;
    uint16_t parameter = 0u;
    if (!parse_hex_u16(frame_text + IPC_OPCODE_BYTES, &declared_length) ||
        !parse_hex_u16(frame_text + IPC_OPCODE_BYTES + IPC_HEX_FIELD_BYTES, &parameter)) {
        return false;
    }

    if (declared_length < IPC_ENVELOPE_HEADER_BYTES || (size_t)declared_length > frame_length) {
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

enum physical_button_action protocol_decode_input_event(const struct kernel_input_event *event) {
    if (event == NULL || event->type != INPUT_EVENT_TYPE_KEY ||
        event->value != INPUT_EVENT_VALUE_PRESS) {
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
        case KEYCODE_POWER_SHORT_PRIMARY:
        case KEYCODE_POWER_SHORT_ALT:
            return BUTTON_ACTION_POWER_SHORT;
        case KEYCODE_POWER_LONG:
            return BUTTON_ACTION_POWER_LONG;
        default:
            return BUTTON_ACTION_NONE;
    }
}
