#ifndef DIALOS_PROTOCOL_H
#define DIALOS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct boot_config_snapshot {
    uint8_t volume_level;
    uint8_t max_volume;
    uint8_t brightness;
    bool has_valid_payload;
};

enum inbound_ipc_kind {
    IPC_INBOUND_INVALID = 0,
    IPC_INBOUND_BOOT_CONFIG,      /* aa24 */
    IPC_INBOUND_SDCARD_EVENT,     /* aa22 */
    IPC_INBOUND_INPUT_MODE_READY, /* a607 */
    IPC_INBOUND_OTHER
};

struct inbound_ipc_message {
    enum inbound_ipc_kind kind;
    struct boot_config_snapshot boot_config;
    bool sdcard_inserted; /* valid when kind == IPC_INBOUND_SDCARD_EVENT */
};

struct kernel_input_event {
    uint32_t seconds;
    uint32_t microseconds;
    uint16_t type;
    uint16_t code;
    int32_t value;
};
_Static_assert(sizeof(struct kernel_input_event) == 16u, "MIPS32 input_event must be 16 bytes");

enum physical_button_action {
    BUTTON_ACTION_NONE = 0,
    BUTTON_ACTION_POWER_SHORT,
    BUTTON_ACTION_POWER_LONG,
    BUTTON_ACTION_VOLUME_UP,
    BUTTON_ACTION_VOLUME_DOWN
};

bool protocol_parse_inbound_ipc(const char *frame_text, size_t frame_length,
                                struct inbound_ipc_message *message_out);
size_t protocol_format_disable_idle_poweroff(char *output_buffer, size_t buffer_capacity);
size_t protocol_format_set_volume(uint8_t volume_level, char *output_buffer, size_t buffer_capacity);
enum physical_button_action protocol_decode_input_event(const struct kernel_input_event *event);

#endif
