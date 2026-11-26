#ifndef DIALOS_APP_H
#define DIALOS_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_sequence.h"
#include "platform.h"
#include "protocol.h"
#include "theme.h"

struct crash_record {
    int32_t signal_number;
    uint64_t fault_address;
    uint64_t program_counter;
    uint64_t return_address;
    uint64_t uptime_ms;
    int32_t ui_state;
    uint32_t crash_count;
};

enum ui_screen_state {
    UI_STATE_BOOT_ANIMATION = 0,
    UI_STATE_WARNING = 1,
    UI_STATE_READY = 2,
    UI_STATE_LOCKED = 3,
    UI_STATE_CRASH_SCREEN = 4,
    UI_STATE_SHUTTING_DOWN = 5
};

struct app_runtime {
    const struct platform_operations *platform;
    enum ui_screen_state state;
    uint32_t initial_crash_count;
    bool crash_count_reset_done;
    uint64_t startup_ns;
    uint64_t next_frame_ns;
    uint64_t ready_entered_ns;
    uint64_t last_battery_poll_ns;
    uint64_t next_ipc_retry_ns;
    uint64_t sd_followup_first_ns;
    uint64_t sd_followup_second_ns;
    bool sd_followup_first_pending;
    bool sd_followup_second_pending;
    struct boot_controller boot;
    uint64_t last_boot_tick_ns;
    enum appearance_mode appearance_mode;
    uint32_t animation_frames;
    uint32_t animation_fps_x10;
    uint32_t max_frame_ms;
    uint64_t takeover_ms;
    uint64_t aa24_ms;
    bool aa24_received;
    bool pending_disable_idle_poweroff;
    uint32_t disable_idle_poweroff_attempts;
    bool idle_poweroff_disabled;
    bool a607_resent_disable;
    uint8_t volume_level;
    uint8_t max_volume;
    uint8_t saved_brightness;
    struct hardware_status hardware;
    bool shutting_down;
};

/* Async-signal-safe portable crash formatting and atomic POSIX storage helpers */
size_t crash_format_record(const struct crash_record *record,
                           char *output_buffer, size_t buffer_capacity);
uint32_t crash_parse_count(const char *count_text, size_t text_length);
size_t crash_format_count(uint32_t crash_count,
                          char *output_buffer, size_t buffer_capacity);
bool crash_should_enter_screen(uint32_t crash_count);
uint32_t crash_increment_count_file(const char *count_path, const char *temp_path);
bool crash_write_log_atomic(const struct crash_record *record, bool sd_mounted,
                            const char *directory_path, const char *temp_path,
                            const char *final_path);

/* Phase 1 runtime state machine */
void app_initialize(struct app_runtime *runtime, const struct platform_operations *platform);
void app_handle_ipc_message(struct app_runtime *runtime, const struct inbound_ipc_message *message);
void app_handle_button_action(struct app_runtime *runtime, enum physical_button_action action);
void app_handle_tick(struct app_runtime *runtime, uint64_t now_ns);
int32_t app_compute_poll_timeout_ms(const struct app_runtime *runtime, uint64_t now_ns);
bool app_is_shutting_down(const struct app_runtime *runtime);

#endif
