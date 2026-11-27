#ifndef DIALOS_PROTOCOL_H
#define DIALOS_PROTOCOL_H

#include "constants.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct boot_config_snapshot {
    uint8_t volume_level;
    uint8_t max_volume;
    uint8_t brightness;
    bool has_valid_payload;
};

enum ipc_playback_state {
    IPC_PLAYBACK_IDLE = 0,
    IPC_PLAYBACK_PLAYING = 1,
    IPC_PLAYBACK_PAUSED = 2
};

struct playback_status_update {
    bool has_state;
    enum ipc_playback_state state;
    bool has_track_metadata;
    char track_title[TRACK_TITLE_CAPACITY];
    bool has_track_path;
    char track_path[TRACK_PATH_CAPACITY];
    uint32_t duration_ms;
};

enum inbound_ipc_kind {
    IPC_INBOUND_INVALID = 0,
    IPC_INBOUND_BOOT_CONFIG,      /* aa24 */
    IPC_INBOUND_SDCARD_EVENT,     /* aa22 */
    IPC_INBOUND_INPUT_MODE_READY, /* a607 */
    IPC_INBOUND_PLAYBACK_STATUS,  /* a202 */
    IPC_INBOUND_PLAYBACK_ELAPSED, /* a103 */
    IPC_INBOUND_PLAYBACK_STOP,    /* a102 */
    IPC_INBOUND_VOLUME_UPDATE,    /* a714 / a715 */
    IPC_INBOUND_USB_CABLE_EVENT,  /* aa1f */
    IPC_INBOUND_BATTERY_EVENT,    /* aa1d / aa1e */
    IPC_INBOUND_OTHER
};

struct inbound_ipc_message {
    enum inbound_ipc_kind kind;
    struct boot_config_snapshot boot_config;
    bool sdcard_inserted; /* valid when kind == IPC_INBOUND_SDCARD_EVENT */
    struct playback_status_update playback_status;
    uint32_t elapsed_ms;
    uint8_t volume_level;
    bool usb_connected;
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

enum touch_event_kind {
    TOUCH_EVENT_NONE = 0,
    TOUCH_EVENT_DOWN,
    TOUCH_EVENT_MOVE,
    TOUCH_EVENT_UP
};

struct touch_event {
    enum touch_event_kind kind;
    int32_t x;
    int32_t y;
    uint64_t timestamp_ms;
};

struct touch_event_decoder {
    bool contact_active;
    bool down_emitted;
    bool has_pending_release;
    bool position_dirty;
    int32_t raw_x;
    int32_t raw_y;
};

bool protocol_parse_inbound_ipc(const char *frame_text, size_t frame_length,
                                struct inbound_ipc_message *message_out);
size_t protocol_format_disable_idle_poweroff(char *output_buffer, size_t buffer_capacity);
size_t protocol_format_set_volume(uint8_t volume_level, char *output_buffer, size_t buffer_capacity);
size_t protocol_format_play_m3u(const char *disk_m3u_path, char *output_buffer,
                                size_t buffer_capacity);
size_t protocol_format_play_pause_toggle(char *output_buffer, size_t buffer_capacity);
enum physical_button_action protocol_decode_input_event(const struct kernel_input_event *event);
void protocol_touch_decoder_initialize(struct touch_event_decoder *decoder);
enum touch_event_kind protocol_touch_decoder_feed(struct touch_event_decoder *decoder,
                                                  const struct kernel_input_event *event,
                                                  struct touch_event *touch_out);

#endif
