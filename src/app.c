#include "app.h"

#include "constants.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static size_t append_literal(char *output_buffer, size_t buffer_capacity, size_t offset,
                             const char *literal) {
    size_t index = 0u;
    while (literal[index] != '\0' && (offset + 1u) < buffer_capacity) {
        output_buffer[offset++] = literal[index++];
    }
    if (buffer_capacity > 0u) {
        output_buffer[offset < buffer_capacity ? offset : buffer_capacity - 1u] = '\0';
    }
    return offset;
}

static size_t append_decimal_u64(char *output_buffer, size_t buffer_capacity, size_t offset,
                                 uint64_t value) {
    char digits[24];
    size_t count = 0u;
    if (value == 0ull) {
        digits[count++] = '0';
    } else {
        while (value > 0ull && count < sizeof(digits)) {
            digits[count++] = (char)('0' + (value % 10ull));
            value /= 10ull;
        }
    }
    while (count > 0u && (offset + 1u) < buffer_capacity) {
        output_buffer[offset++] = digits[--count];
    }
    if (buffer_capacity > 0u) {
        output_buffer[offset < buffer_capacity ? offset : buffer_capacity - 1u] = '\0';
    }
    return offset;
}

static size_t append_decimal_i32(char *output_buffer, size_t buffer_capacity, size_t offset,
                                 int32_t value) {
    if (value < 0) {
        offset = append_literal(output_buffer, buffer_capacity, offset, "-");
        uint64_t magnitude = (uint64_t)(-(int64_t)value);
        return append_decimal_u64(output_buffer, buffer_capacity, offset, magnitude);
    }
    return append_decimal_u64(output_buffer, buffer_capacity, offset, (uint64_t)value);
}

static size_t append_hex_u64(char *output_buffer, size_t buffer_capacity, size_t offset,
                             uint64_t value) {
    static const char HEX_DIGITS[] = "0123456789abcdef";
    char hex_chars[16];
    size_t min_digits = 8u;
    size_t count = 0u;

    while (value > 0ull || count < min_digits) {
        hex_chars[count++] = HEX_DIGITS[value & 0xFull];
        value >>= 4u;
    }

    offset = append_literal(output_buffer, buffer_capacity, offset, "0x");
    while (count > 0u && (offset + 1u) < buffer_capacity) {
        output_buffer[offset++] = hex_chars[--count];
    }
    if (buffer_capacity > 0u) {
        output_buffer[offset < buffer_capacity ? offset : buffer_capacity - 1u] = '\0';
    }
    return offset;
}

static bool write_all_posix(int file_descriptor, const char *bytes, size_t length) {
    size_t total_written = 0u;
    while (total_written < length) {
        ssize_t written = write(file_descriptor, bytes + total_written, length - total_written);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_written += (size_t)written;
    }
    return true;
}

size_t crash_format_record(const struct crash_record *record,
                           char *output_buffer, size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity == 0u) {
        return 0u;
    }
    output_buffer[0] = '\0';
    if (record == NULL) {
        return 0u;
    }

    size_t offset = 0u;
    offset = append_literal(output_buffer, buffer_capacity, offset, "signal=");
    offset = append_decimal_i32(output_buffer, buffer_capacity, offset, record->signal_number);
    offset = append_literal(output_buffer, buffer_capacity, offset, "\nsi_addr=");
    offset = append_hex_u64(output_buffer, buffer_capacity, offset, record->fault_address);
    offset = append_literal(output_buffer, buffer_capacity, offset, "\npc=");
    offset = append_hex_u64(output_buffer, buffer_capacity, offset, record->program_counter);
    offset = append_literal(output_buffer, buffer_capacity, offset, "\nra=");
    offset = append_hex_u64(output_buffer, buffer_capacity, offset, record->return_address);
    offset = append_literal(output_buffer, buffer_capacity, offset, "\nuptime_ms=");
    offset = append_decimal_u64(output_buffer, buffer_capacity, offset, record->uptime_ms);
    offset = append_literal(output_buffer, buffer_capacity, offset, "\nui_state=");
    offset = append_decimal_i32(output_buffer, buffer_capacity, offset, record->ui_state);
    offset = append_literal(output_buffer, buffer_capacity, offset, "\ncrash_count=");
    offset = append_decimal_u64(output_buffer, buffer_capacity, offset, record->crash_count);
    offset = append_literal(output_buffer, buffer_capacity, offset, "\n");
    return offset;
}

uint32_t crash_parse_count(const char *count_text, size_t text_length) {
    if (count_text == NULL || text_length == 0u) {
        return 0u;
    }

    size_t index = 0u;
    if (count_text[0] < '0' || count_text[0] > '9') {
        return 0u;
    }

    uint32_t parsed = 0u;
    while (index < text_length && count_text[index] >= '0' && count_text[index] <= '9') {
        uint32_t digit = (uint32_t)(count_text[index] - '0');
        if (parsed <= (UINT32_MAX - digit) / 10u) {
            parsed = parsed * 10u + digit;
        } else {
            parsed = UINT32_MAX;
        }
        index++;
    }

    while (index < text_length) {
        if (count_text[index] != '\n' && count_text[index] != '\r') {
            return 0u;
        }
        index++;
    }
    return parsed;
}

size_t crash_format_count(uint32_t crash_count,
                          char *output_buffer, size_t buffer_capacity) {
    if (output_buffer == NULL || buffer_capacity == 0u) {
        return 0u;
    }
    output_buffer[0] = '\0';
    size_t offset = append_decimal_u64(output_buffer, buffer_capacity, 0u, crash_count);
    return append_literal(output_buffer, buffer_capacity, offset, "\n");
}

bool crash_should_enter_screen(uint32_t crash_count) {
    return crash_count >= CRASH_LOOP_THRESHOLD;
}

uint32_t crash_increment_count_file(const char *count_path, const char *temp_path) {
    if (count_path == NULL || temp_path == NULL) {
        return 0u;
    }

    uint32_t current_count = 0u;
    int read_fd = open(count_path, O_RDONLY);
    if (read_fd >= 0) {
        char read_buffer[CRASH_COUNT_BUFFER_BYTES];
        ssize_t bytes_read = read(read_fd, read_buffer, sizeof(read_buffer));
        close(read_fd);
        if (bytes_read > 0) {
            current_count = crash_parse_count(read_buffer, (size_t)bytes_read);
        }
    }

    uint32_t next_count = current_count < UINT32_MAX ? (current_count + 1u) : UINT32_MAX;
    char count_buffer[CRASH_COUNT_BUFFER_BYTES];
    size_t count_length = crash_format_count(next_count, count_buffer, sizeof(count_buffer));

    int write_fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (write_fd < 0) {
        return next_count;
    }

    bool wrote_ok = write_all_posix(write_fd, count_buffer, count_length);
    if (close(write_fd) != 0 || !wrote_ok || rename(temp_path, count_path) != 0) {
        unlink(temp_path);
    }
    return next_count;
}

bool crash_write_log_atomic(const struct crash_record *record, bool sd_mounted,
                            const char *directory_path, const char *temp_path,
                            const char *final_path) {
    if (record == NULL || !sd_mounted || directory_path == NULL ||
        temp_path == NULL || final_path == NULL) {
        return false;
    }

    if (mkdir(directory_path, 0755) != 0 && errno != EEXIST) {
        return false;
    }

    char log_buffer[CRASH_LOG_BUFFER_BYTES];
    size_t log_length = crash_format_record(record, log_buffer, sizeof(log_buffer));
    if (log_length == 0u) {
        return false;
    }

    int write_fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (write_fd < 0) {
        return false;
    }

    bool ok = write_all_posix(write_fd, log_buffer, log_length);
    if (ok && fsync(write_fd) != 0) {
        ok = false;
    }
    if (close(write_fd) != 0) {
        ok = false;
    }
    if (ok && rename(temp_path, final_path) != 0) {
        ok = false;
    }
    if (ok) {
        int directory_fd = open(directory_path, O_RDONLY);
        if (directory_fd >= 0) {
            (void)fsync(directory_fd);
            close(directory_fd);
        }
    }
    if (!ok) {
        unlink(temp_path);
    }
    return ok;
}

static uint64_t elapsed_ms_since(uint64_t later_ns, uint64_t earlier_ns) {
    if (later_ns <= earlier_ns) {
        return 0ull;
    }
    return (later_ns - earlier_ns) / NANOSECONDS_PER_MILLISECOND;
}

static void set_ui_state(struct app_runtime *runtime, enum ui_screen_state next_state) {
    runtime->state = next_state;
    if (runtime->platform->update_crash_state_mirror != NULL) {
        runtime->platform->update_crash_state_mirror(runtime->platform->context, (int32_t)next_state);
    }
}

static void render_ready_screen(struct app_runtime *runtime) {
    struct framebuffer_surface surface;
    if (!runtime->platform->display_acquire_surface(runtime->platform->context, &surface)) {
        return;
    }
    struct ready_screen_status status = {
        .battery_valid = runtime->hardware.battery_valid,
        .battery_percent = runtime->hardware.battery_percent,
        .volume_level = runtime->volume_level,
        .max_volume = runtime->max_volume,
        .sd_mounted = runtime->hardware.sd_mounted,
    };
    render_draw_ready_frame(&surface, &status);
    runtime->platform->display_present_surface(runtime->platform->context);
}

static void build_telemetry_snapshot(const struct app_runtime *runtime, bool is_shutdown_update,
                                     uint64_t now_ns, struct telemetry_snapshot *snapshot_out) {
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->takeover_ms = runtime->takeover_ms;
    snapshot_out->animation_frames = runtime->animation_frames;
    snapshot_out->animation_fps_x10 = runtime->animation_fps_x10;
    snapshot_out->max_frame_ms = runtime->max_frame_ms;
    snapshot_out->aa24_ms = runtime->aa24_ms;
    snapshot_out->idle_poweroff_disabled = runtime->idle_poweroff_disabled;
    snapshot_out->sd_mounted = runtime->hardware.sd_mounted;
    snapshot_out->is_shutdown_update = is_shutdown_update;
    if (is_shutdown_update) {
        snapshot_out->uptime_ms = elapsed_ms_since(now_ns, runtime->startup_ns);
    }
}

static void freeze_animation_metrics(struct app_runtime *runtime, uint64_t now_ns) {
    if (runtime->animation_fps_x10 > 0u) {
        return;
    }
    uint64_t elapsed_ms = elapsed_ms_since(now_ns, runtime->startup_ns);
    if (elapsed_ms == 0ull) {
        elapsed_ms = 1ull;
    }
    runtime->animation_fps_x10 = (uint32_t)(((uint64_t)runtime->animation_frames * 10000ull) / elapsed_ms);
}

static void attempt_send_disable_idle_poweroff(struct app_runtime *runtime, uint64_t now_ns) {
    char command[IPC_COMMAND_BUFFER_BYTES];
    size_t length = protocol_format_disable_idle_poweroff(command, sizeof(command));
    if (length == 0u) {
        runtime->pending_disable_idle_poweroff = false;
        return;
    }

    runtime->disable_idle_poweroff_attempts++;
    if (runtime->platform->ipc_send_player(runtime->platform->context, command, length)) {
        runtime->idle_poweroff_disabled = true;
        runtime->pending_disable_idle_poweroff = false;
        return;
    }

    if (runtime->disable_idle_poweroff_attempts >= IPC_DISABLE_IDLE_MAX_RETRIES) {
        runtime->pending_disable_idle_poweroff = false;
        return;
    }

    runtime->next_ipc_retry_ns = now_ns + (uint64_t)IPC_RETRY_INTERVAL_MS * NANOSECONDS_PER_MILLISECOND;
}

static void enter_ready_from_aa24(struct app_runtime *runtime,
                                  const struct boot_config_snapshot *boot_config,
                                  uint64_t now_ns) {
    freeze_animation_metrics(runtime, now_ns);
    runtime->aa24_received = true;
    runtime->aa24_ms = elapsed_ms_since(now_ns, runtime->startup_ns);

    runtime->volume_level = boot_config->volume_level;
    runtime->max_volume = boot_config->max_volume;
    runtime->saved_brightness = boot_config->brightness;
    if (runtime->volume_level > runtime->max_volume) {
        runtime->volume_level = runtime->max_volume;
    }

    runtime->platform->display_set_brightness(runtime->platform->context, runtime->saved_brightness);

    runtime->pending_disable_idle_poweroff = true;
    runtime->disable_idle_poweroff_attempts = 0u;
    attempt_send_disable_idle_poweroff(runtime, now_ns);

    runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
    set_ui_state(runtime, UI_STATE_READY);
    runtime->ready_entered_ns = now_ns;
    runtime->last_battery_poll_ns = now_ns;

    render_ready_screen(runtime);

    if (runtime->platform->telemetry_write != NULL) {
        struct telemetry_snapshot snapshot;
        build_telemetry_snapshot(runtime, false, now_ns, &snapshot);
        runtime->platform->telemetry_write(runtime->platform->context, &snapshot);
    }
}

void app_initialize(struct app_runtime *runtime, const struct platform_operations *platform) {
    if (runtime == NULL || platform == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->platform = platform;
    runtime->volume_level = VOLUME_DEFAULT;
    runtime->max_volume = MAX_VOLUME_DEFAULT;
    runtime->saved_brightness = BRIGHTNESS_BOOT_DEFAULT;
    runtime->startup_ns = platform->monotonic_ns(platform->context);

    runtime->initial_crash_count =
        platform->crash_count_read != NULL ? platform->crash_count_read(platform->context) : 0u;

    if (crash_should_enter_screen(runtime->initial_crash_count)) {
        set_ui_state(runtime, UI_STATE_CRASH_SCREEN);
        struct framebuffer_surface surface;
        if (platform->display_acquire_surface(platform->context, &surface)) {
            render_draw_crash_frame(&surface, runtime->initial_crash_count);
            platform->display_present_surface(platform->context);
        }
        platform->display_set_brightness(platform->context, BRIGHTNESS_BOOT_DEFAULT);

        char command[IPC_COMMAND_BUFFER_BYTES];
        size_t length = protocol_format_disable_idle_poweroff(command, sizeof(command));
        if (length > 0u) {
            (void)platform->ipc_send_player(platform->context, command, length);
        }
        return;
    }

    set_ui_state(runtime, UI_STATE_BOOT_ANIMATION);
    uint64_t frame_start_ns = platform->monotonic_ns(platform->context);
    struct framebuffer_surface surface;
    if (platform->display_acquire_surface(platform->context, &surface)) {
        render_draw_boot_frame(&surface, 0u);
        platform->display_present_surface(platform->context);
    }
    platform->display_set_brightness(platform->context, BRIGHTNESS_BOOT_DEFAULT);

    uint64_t frame_end_ns = platform->monotonic_ns(platform->context);
    runtime->takeover_ms = elapsed_ms_since(frame_end_ns, runtime->startup_ns);
    runtime->max_frame_ms = (uint32_t)elapsed_ms_since(frame_end_ns, frame_start_ns);
    runtime->animation_frames = 1u;
    runtime->animation_angle_degrees = BOOT_DIAL_STEP_DEGREES;
    runtime->next_frame_ns = runtime->startup_ns + BOOT_FRAME_PERIOD_NS;
}

void app_handle_ipc_message(struct app_runtime *runtime, const struct inbound_ipc_message *message) {
    if (runtime == NULL || message == NULL || runtime->platform == NULL) {
        return;
    }
    if (runtime->state == UI_STATE_CRASH_SCREEN || runtime->state == UI_STATE_SHUTTING_DOWN) {
        return;
    }

    uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);

    switch (message->kind) {
        case IPC_INBOUND_BOOT_CONFIG:
            if (runtime->state == UI_STATE_BOOT_ANIMATION || runtime->state == UI_STATE_WARNING) {
                enter_ready_from_aa24(runtime, &message->boot_config, now_ns);
            } else if (runtime->state == UI_STATE_READY || runtime->state == UI_STATE_LOCKED) {
                runtime->saved_brightness = message->boot_config.brightness;
                if (runtime->state == UI_STATE_READY) {
                    runtime->platform->display_set_brightness(runtime->platform->context,
                                                              runtime->saved_brightness);
                }
            }
            break;

        case IPC_INBOUND_INPUT_MODE_READY:
            if ((runtime->state == UI_STATE_READY || runtime->state == UI_STATE_LOCKED) &&
                !runtime->a607_resent_disable) {
                runtime->a607_resent_disable = true;
                runtime->pending_disable_idle_poweroff = true;
                runtime->disable_idle_poweroff_attempts = 0u;
                attempt_send_disable_idle_poweroff(runtime, now_ns);
            }
            break;

        case IPC_INBOUND_SDCARD_EVENT:
            if (runtime->state == UI_STATE_READY) {
                bool previous_sd_mounted = runtime->hardware.sd_mounted;
                runtime->platform->probe_hardware_status(runtime->platform->context,
                                                         &runtime->hardware);
                if (runtime->hardware.sd_mounted != previous_sd_mounted) {
                    render_ready_screen(runtime);
                }
                runtime->sd_followup_first_pending = true;
                runtime->sd_followup_first_ns =
                    now_ns + (uint64_t)SD_MOUNT_FOLLOWUP_FIRST_MS * NANOSECONDS_PER_MILLISECOND;
                runtime->sd_followup_second_pending = true;
                runtime->sd_followup_second_ns =
                    now_ns + (uint64_t)SD_MOUNT_FOLLOWUP_SECOND_MS * NANOSECONDS_PER_MILLISECOND;
            }
            break;

        default:
            break;
    }
}

void app_handle_button_action(struct app_runtime *runtime, enum physical_button_action action) {
    if (runtime == NULL || runtime->platform == NULL || runtime->shutting_down) {
        return;
    }

    if (action == BUTTON_ACTION_POWER_LONG) {
        enum ui_screen_state previous_state = runtime->state;
        bool screen_was_locked = (previous_state == UI_STATE_LOCKED);
        uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);

        runtime->shutting_down = true;
        set_ui_state(runtime, UI_STATE_SHUTTING_DOWN);

        if (!screen_was_locked) {
            struct framebuffer_surface surface;
            if (runtime->platform->display_acquire_surface(runtime->platform->context, &surface)) {
                render_draw_shutdown_frame(&surface);
                runtime->platform->display_present_surface(runtime->platform->context);
            }
        }

        char mute_command[IPC_COMMAND_BUFFER_BYTES];
        size_t length = protocol_format_set_volume(0u, mute_command, sizeof(mute_command));
        if (length > 0u) {
            (void)runtime->platform->ipc_send_player(runtime->platform->context, mute_command, length);
        }

        struct telemetry_snapshot final_snapshot;
        build_telemetry_snapshot(runtime, true, now_ns, &final_snapshot);
        runtime->platform->execute_shutdown(runtime->platform->context, screen_was_locked,
                                            &final_snapshot);
        return;
    }

    if (runtime->state == UI_STATE_READY) {
        if (action == BUTTON_ACTION_POWER_SHORT) {
            set_ui_state(runtime, UI_STATE_LOCKED);
            runtime->platform->display_enter_sleep(runtime->platform->context);
            return;
        }

        if (action == BUTTON_ACTION_VOLUME_UP || action == BUTTON_ACTION_VOLUME_DOWN) {
            uint8_t next_volume = runtime->volume_level;
            if (action == BUTTON_ACTION_VOLUME_UP && next_volume < runtime->max_volume) {
                next_volume++;
            } else if (action == BUTTON_ACTION_VOLUME_DOWN && next_volume > 0u) {
                next_volume--;
            }

            if (next_volume != runtime->volume_level) {
                runtime->volume_level = next_volume;
                char command[IPC_COMMAND_BUFFER_BYTES];
                size_t length = protocol_format_set_volume(next_volume, command, sizeof(command));
                if (length > 0u) {
                    (void)runtime->platform->ipc_send_player(runtime->platform->context, command,
                                                             length);
                }
                render_ready_screen(runtime);
            }
        }
    } else if (runtime->state == UI_STATE_LOCKED && action == BUTTON_ACTION_POWER_SHORT) {
        uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);
        runtime->platform->display_begin_wake(runtime->platform->context);
        runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
        runtime->last_battery_poll_ns = now_ns;
        set_ui_state(runtime, UI_STATE_READY);
        render_ready_screen(runtime);
        runtime->platform->display_finish_wake(runtime->platform->context, runtime->saved_brightness);
    }
}

void app_handle_tick(struct app_runtime *runtime, uint64_t now_ns) {
    if (runtime == NULL || runtime->platform == NULL || runtime->shutting_down) {
        return;
    }

    if (runtime->state == UI_STATE_BOOT_ANIMATION) {
        if (elapsed_ms_since(now_ns, runtime->startup_ns) >= AA24_WARNING_TIMEOUT_MS) {
            freeze_animation_metrics(runtime, now_ns);
            set_ui_state(runtime, UI_STATE_WARNING);
            struct framebuffer_surface surface;
            if (runtime->platform->display_acquire_surface(runtime->platform->context, &surface)) {
                render_draw_warning_frame(&surface);
                runtime->platform->display_present_surface(runtime->platform->context);
            }
            return;
        }

        if (now_ns >= runtime->next_frame_ns) {
            uint64_t frame_start_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            struct framebuffer_surface surface;
            if (runtime->platform->display_acquire_surface(runtime->platform->context, &surface)) {
                render_draw_boot_frame(&surface, runtime->animation_angle_degrees);
                runtime->platform->display_present_surface(runtime->platform->context);
            }
            uint64_t frame_end_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            uint32_t frame_ms = (uint32_t)elapsed_ms_since(frame_end_ns, frame_start_ns);
            if (frame_ms > runtime->max_frame_ms) {
                runtime->max_frame_ms = frame_ms;
            }
            runtime->animation_frames++;
            runtime->animation_angle_degrees =
                (uint16_t)((runtime->animation_angle_degrees + BOOT_DIAL_STEP_DEGREES) %
                           FULL_CIRCLE_DEGREES);
            runtime->next_frame_ns += BOOT_FRAME_PERIOD_NS;
            if (runtime->next_frame_ns <= now_ns) {
                runtime->next_frame_ns = now_ns + BOOT_FRAME_PERIOD_NS;
            }
        }
        return;
    }

    if (runtime->state != UI_STATE_READY && runtime->state != UI_STATE_LOCKED) {
        return;
    }

    if (runtime->pending_disable_idle_poweroff && now_ns >= runtime->next_ipc_retry_ns) {
        attempt_send_disable_idle_poweroff(runtime, now_ns);
    }

    if (!runtime->crash_count_reset_done &&
        elapsed_ms_since(now_ns, runtime->ready_entered_ns) >= READY_STABILITY_WINDOW_MS) {
        runtime->crash_count_reset_done = true;
        if (runtime->initial_crash_count > 0u && runtime->platform->crash_count_write != NULL) {
            runtime->platform->crash_count_write(runtime->platform->context, 0u);
        }
    }

    if (runtime->state == UI_STATE_READY) {
        bool needs_redraw = false;

        if (runtime->sd_followup_first_pending && now_ns >= runtime->sd_followup_first_ns) {
            runtime->sd_followup_first_pending = false;
            bool previous_sd = runtime->hardware.sd_mounted;
            runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
            if (runtime->hardware.sd_mounted != previous_sd) {
                needs_redraw = true;
            }
        }

        if (runtime->sd_followup_second_pending && now_ns >= runtime->sd_followup_second_ns) {
            runtime->sd_followup_second_pending = false;
            bool previous_sd = runtime->hardware.sd_mounted;
            runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
            if (runtime->hardware.sd_mounted != previous_sd) {
                needs_redraw = true;
            }
        }

        if (elapsed_ms_since(now_ns, runtime->last_battery_poll_ns) >= BATTERY_POLL_INTERVAL_MS) {
            struct hardware_status previous = runtime->hardware;
            runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
            runtime->last_battery_poll_ns = now_ns;
            if (runtime->hardware.battery_valid != previous.battery_valid ||
                runtime->hardware.battery_percent != previous.battery_percent ||
                runtime->hardware.sd_mounted != previous.sd_mounted) {
                needs_redraw = true;
            }
        }

        if (needs_redraw) {
            render_ready_screen(runtime);
        }
    }
}

static uint64_t min_deadline_ns(uint64_t current_min, uint64_t candidate) {
    if (current_min == 0ull || candidate < current_min) {
        return candidate;
    }
    return current_min;
}

int32_t app_compute_poll_timeout_ms(const struct app_runtime *runtime, uint64_t now_ns) {
    if (runtime == NULL || runtime->shutting_down) {
        return 0;
    }

    if (runtime->state == UI_STATE_WARNING || runtime->state == UI_STATE_CRASH_SCREEN ||
        runtime->state == UI_STATE_SHUTTING_DOWN) {
        return -1;
    }

    uint64_t next_deadline = 0ull;

    if (runtime->state == UI_STATE_BOOT_ANIMATION) {
        uint64_t warning_deadline =
            runtime->startup_ns + (uint64_t)AA24_WARNING_TIMEOUT_MS * NANOSECONDS_PER_MILLISECOND;
        next_deadline = min_deadline_ns(runtime->next_frame_ns, warning_deadline);
    } else if (runtime->state == UI_STATE_READY || runtime->state == UI_STATE_LOCKED) {
        if (runtime->pending_disable_idle_poweroff) {
            next_deadline = min_deadline_ns(next_deadline, runtime->next_ipc_retry_ns);
        }
        if (!runtime->crash_count_reset_done && runtime->initial_crash_count > 0u) {
            uint64_t reset_deadline =
                runtime->ready_entered_ns +
                (uint64_t)READY_STABILITY_WINDOW_MS * NANOSECONDS_PER_MILLISECOND;
            next_deadline = min_deadline_ns(next_deadline, reset_deadline);
        }
        if (runtime->state == UI_STATE_READY) {
            uint64_t battery_deadline =
                runtime->last_battery_poll_ns +
                (uint64_t)BATTERY_POLL_INTERVAL_MS * NANOSECONDS_PER_MILLISECOND;
            next_deadline = min_deadline_ns(next_deadline, battery_deadline);
            if (runtime->sd_followup_first_pending) {
                next_deadline = min_deadline_ns(next_deadline, runtime->sd_followup_first_ns);
            }
            if (runtime->sd_followup_second_pending) {
                next_deadline = min_deadline_ns(next_deadline, runtime->sd_followup_second_ns);
            }
        }
    }

    if (next_deadline == 0ull) {
        return -1;
    }
    if (next_deadline <= now_ns) {
        return 0;
    }
    uint64_t delta_ns = next_deadline - now_ns;
    uint64_t delta_ms = (delta_ns + NANOSECONDS_PER_MILLISECOND - 1ull) / NANOSECONDS_PER_MILLISECOND;
    if (delta_ms > (uint64_t)INT32_MAX) {
        return INT32_MAX;
    }
    return (int32_t)delta_ms;
}

bool app_is_shutting_down(const struct app_runtime *runtime) {
    return runtime != NULL && runtime->shutting_down;
}
