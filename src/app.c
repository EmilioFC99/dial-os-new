#include "app.h"

#include "constants.h"
#include "render_home.h"
#include "render_now_playing.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
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

static void render_boot_snapshot_frame(struct app_runtime *runtime) {
    struct framebuffer_surface surface;
    if (!runtime->platform->display_acquire_surface(runtime->platform->context, &surface)) {
        return;
    }
    struct boot_render_snapshot snapshot;
    boot_controller_get_snapshot(&runtime->boot, &snapshot);
    render_draw_boot_frame(&surface, &snapshot, runtime->appearance_mode);
    runtime->platform->display_present_surface(runtime->platform->context);
}

static void flush_runtime_logs(struct app_runtime *runtime, bool sync_to_disk) {
    if (runtime->platform->log_append != NULL) {
        (void)logger_flush(&runtime->logger, runtime->platform->log_append,
                           runtime->platform->context, sync_to_disk);
    }
}

static uint64_t runtime_uptime_ms(const struct app_runtime *runtime, uint64_t now_ns) {
    return elapsed_ms_since(now_ns, runtime->startup_ns);
}

static void sync_home_telemetry(struct app_runtime *runtime) {
    if (runtime->hardware.sd_free_valid) {
        runtime->cached_sd_free_valid = true;
        runtime->cached_sd_free_megabytes = runtime->hardware.sd_free_megabytes;
    }

    struct home_telemetry *telemetry = &runtime->home.telemetry;
    telemetry->volume_percent = home_normalize_volume_percent(runtime->volume_level);
    telemetry->volume_limit_percent = home_normalize_volume_percent(runtime->max_volume);
    telemetry->battery_valid = runtime->hardware.battery_valid;
    telemetry->battery_percent = runtime->hardware.battery_percent;
    telemetry->sd_mounted = runtime->hardware.sd_mounted;
    telemetry->sd_free_valid = runtime->cached_sd_free_valid;
    telemetry->sd_free_megabytes = runtime->cached_sd_free_megabytes;

    if (runtime->hardware.usb_storage_active) {
        telemetry->playback_mode = HOME_PLAYBACK_STORAGE;
    } else if (telemetry->playback_mode == HOME_PLAYBACK_STORAGE) {
        telemetry->playback_mode = runtime->non_storage_playback_mode;
    }
}

static void render_home_screen(struct app_runtime *runtime) {
    uint64_t frame_start_ns = runtime->platform->monotonic_ns(runtime->platform->context);
    struct framebuffer_surface surface;
    if (!runtime->platform->display_acquire_surface(runtime->platform->context, &surface)) {
        return;
    }
    if (runtime->home.active_screen == ACTIVE_SCREEN_NOW_PLAYING_SHELL) {
        render_draw_now_playing_frame(&surface, &runtime->now_playing, &runtime->library_covers,
                                      runtime->appearance_mode);
    } else {
        struct home_render_snapshot snapshot;
        home_controller_get_snapshot(&runtime->home, runtime->appearance_mode, &snapshot);
        render_draw_home_with_library_frame(&surface, &snapshot, &runtime->library);
    }
    runtime->platform->display_present_surface(runtime->platform->context);

    uint64_t frame_end_ns = runtime->platform->monotonic_ns(runtime->platform->context);
    uint32_t frame_ms = (uint32_t)elapsed_ms_since(frame_end_ns, frame_start_ns);
    if (frame_ms > runtime->home_max_frame_ms) {
        runtime->home_max_frame_ms = frame_ms;
    }
    runtime->home_frames++;
    runtime->perf_window_frames++;
    runtime->perf_window_total_frame_ms += frame_ms;
    if (frame_ms > runtime->perf_window_max_frame_ms) {
        runtime->perf_window_max_frame_ms = frame_ms;
    }
    if (frame_ms >= FRAME_SLOW_THRESHOLD_MS) {
        runtime->perf_window_slow_frames++;
    }
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
    snapshot_out->home_frames = runtime->home_frames;
    snapshot_out->home_max_frame_ms = runtime->home_max_frame_ms;
    if (is_shutdown_update) {
        snapshot_out->uptime_ms = elapsed_ms_since(now_ns, runtime->startup_ns);
        if (runtime->home_frames > 0u && runtime->ready_entered_ns > 0ull) {
            uint64_t home_elapsed_ms = elapsed_ms_since(now_ns, runtime->ready_entered_ns);
            if (home_elapsed_ms == 0ull) {
                home_elapsed_ms = 1ull;
            }
            snapshot_out->home_fps_x10 =
                (uint32_t)(((uint64_t)runtime->home_frames * 10000ull) / home_elapsed_ms);
        }
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

static const char *resolve_temp_directory(const struct app_runtime *runtime);
static void begin_library_mount_sync(struct app_runtime *runtime);
static void handle_sd_mount_transition(struct app_runtime *runtime);

static void flush_playback_persistence(struct app_runtime *runtime) {
    if (runtime == NULL || !runtime->library_db_open_ok) {
        return;
    }
    if (!runtime->persistence_dirty && !runtime->playback.persistence_dirty) {
        return;
    }
    playback_engine_save_to_db(&runtime->playback, runtime->volume_level);
    runtime->playback.persistence_dirty = false;
    runtime->persistence_dirty = false;
    runtime->persistence_dirty_since_ns = 0ull;
}

static void mark_playback_persistence_dirty(struct app_runtime *runtime, uint64_t now_ns) {
    runtime->persistence_dirty = true;
    if (runtime->persistence_dirty_since_ns == 0ull) {
        runtime->persistence_dirty_since_ns = now_ns;
    }
}

static void sync_playback_to_ui(struct app_runtime *runtime) {
    enum home_playback_mode mode = HOME_PLAYBACK_IDLE;
    if (runtime->playback.has_active_track) {
        mode = runtime->playback.is_playing ? HOME_PLAYBACK_PLAYING : HOME_PLAYBACK_PAUSED;
    }
    runtime->non_storage_playback_mode = mode;
    if (!runtime->hardware.usb_storage_active) {
        runtime->home.telemetry.playback_mode = mode;
    }
    if (runtime->playback.has_active_track) {
        snprintf(runtime->home.telemetry.track_title, sizeof(runtime->home.telemetry.track_title),
                 "%s", runtime->playback.active_track.title);
        runtime->home.telemetry.duration_ms = runtime->playback.duration_ms;
        runtime->home.telemetry.elapsed_ms = runtime->playback.elapsed_ms;
    } else {
        runtime->home.telemetry.track_title[0] = '\0';
        runtime->home.telemetry.duration_ms = 0u;
        runtime->home.telemetry.elapsed_ms = 0u;
    }
    runtime->home.elapsed_fractional_ms = 0.0f;

    const char *active_path =
        runtime->playback.has_active_track ? runtime->playback.active_track.path : "";
    if (strcmp(runtime->last_synced_track_path, active_path) != 0) {
        snprintf(runtime->last_synced_track_path, sizeof(runtime->last_synced_track_path), "%s",
                 active_path);
        library_controller_set_active_playback(
            &runtime->library, runtime->playback.has_active_track ? active_path : NULL,
            runtime->playback.is_playing);
    } else {
        runtime->library.is_playing =
            runtime->playback.has_active_track && runtime->playback.is_playing;
    }
    runtime->now_playing.volume_level = runtime->volume_level;
}

static void write_and_send_queue_slice(struct app_runtime *runtime, uint32_t start_slot,
                                       uint64_t now_ns) {
    if (!runtime->library_db_open_ok) {
        return;
    }
    const char *temp_directory = resolve_temp_directory(runtime);
    uint32_t previous_sequence = runtime->m3u_sequence;
    runtime->m3u_sequence = (runtime->m3u_sequence + 1u) & 0xFFu;
    char previous_path[320];
    char disk_path[320];
    snprintf(previous_path, sizeof(previous_path), "%s/dialos_play_%u.m3u", temp_directory,
             (unsigned)previous_sequence);
    (void)unlink(previous_path);
    snprintf(disk_path, sizeof(disk_path), "%s/dialos_play_%u.m3u", temp_directory,
             (unsigned)runtime->m3u_sequence);

    FILE *m3u = fopen(disk_path, "w");
    if (m3u == NULL) {
        return;
    }
    fprintf(m3u, "#EXTM3U\n");
    uint32_t written_tracks = 0u;
    (void)library_db_write_queue_slice_m3u(&runtime->library_db, start_slot,
                                          PLAYBACK_M3U_SLICE_TRACKS, m3u, &written_tracks);
    fclose(m3u);
    if (written_tracks == 0u) {
        (void)unlink(disk_path);
        return;
    }

    runtime->playback.slice_start_slot = start_slot;
    runtime->playback.slice_count = written_tracks;
    runtime->playback.awaiting_slice_load = true;

    char ipc_frame[512];
    size_t frame_len = protocol_format_play_m3u(disk_path, ipc_frame, sizeof(ipc_frame));
    if (frame_len > 0u) {
        (void)runtime->platform->ipc_send_player(runtime->platform->context, ipc_frame, frame_len);
    }
    logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "PLAY",
                    "slice m3u=%s start=%u tracks=%u", disk_path, (unsigned)start_slot,
                    (unsigned)written_tracks);
}

static void execute_playback_effects(struct app_runtime *runtime,
                                     const struct playback_engine_effects *effects,
                                     uint64_t now_ns) {
    if (runtime == NULL || effects == NULL) {
        return;
    }
    char command[IPC_SEEK_COMMAND_BUFFER_BYTES];
    if (effects->launch_slice) {
        write_and_send_queue_slice(runtime, effects->slice_start_slot, now_ns);
    }
    if (effects->send_play_pause) {
        size_t length = protocol_format_play_pause_toggle(command, sizeof(command));
        if (length > 0u) {
            (void)runtime->platform->ipc_send_player(runtime->platform->context, command, length);
        }
    }
    if (effects->send_stop) {
        size_t length = protocol_format_stop(command, sizeof(command));
        if (length > 0u) {
            (void)runtime->platform->ipc_send_player(runtime->platform->context, command, length);
        }
    }
    if (effects->send_seek) {
        size_t length = protocol_format_seek_ms(effects->seek_ms, command, sizeof(command));
        if (length > 0u) {
            (void)runtime->platform->ipc_send_player(runtime->platform->context, command, length);
        }
    }

    sync_playback_to_ui(runtime);

    if (effects->track_changed) {
        if (runtime->playback.has_active_track) {
            now_playing_media_request_active(&runtime->now_playing_media,
                                             runtime->playback.active_track.album_id,
                                             runtime->playback.active_track.album_uid,
                                             runtime->playback.active_track.path);
        } else {
            now_playing_media_clear(&runtime->now_playing_media);
        }
    }

    now_playing_controller_apply_effects(&runtime->now_playing, effects);

    if (runtime->playback.persistence_dirty) {
        mark_playback_persistence_dirty(runtime, now_ns);
    }
}

static void maybe_restore_playback_state(struct app_runtime *runtime, uint64_t now_ns) {
    if (!runtime->library_db_open_ok || !runtime->aa24_received ||
        !runtime->boot_player_configured || runtime->playback_state_restored) {
        return;
    }
    runtime->playback_state_restored = true;

    uint8_t restored_volume = runtime->volume_level;
    bool has_restored_volume = false;
    (void)playback_engine_restore_from_db(&runtime->playback, &restored_volume,
                                          &has_restored_volume);

    char command[IPC_COMMAND_BUFFER_BYTES];
    if (has_restored_volume) {
        if (restored_volume > runtime->max_volume) {
            restored_volume = runtime->max_volume;
        }
        runtime->volume_level = restored_volume;
        size_t length = protocol_format_set_volume(restored_volume, command, sizeof(command));
        if (length > 0u) {
            (void)runtime->platform->ipc_send_player(runtime->platform->context, command, length);
        }
    }

    sync_home_telemetry(runtime);
    sync_playback_to_ui(runtime);
    if (runtime->playback.has_active_track) {
        (void)library_cover_cache_lookup_or_load_disk(
            &runtime->library_covers, runtime->playback.active_track.album_uid);
        now_playing_media_request_active(&runtime->now_playing_media,
                                         runtime->playback.active_track.album_id,
                                         runtime->playback.active_track.album_uid,
                                         runtime->playback.active_track.path);
    }
    logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "BOOT",
                    "restore vol=%u shuf=%u rep=%u track=%u album=%lld elapsed=%u",
                    (unsigned)runtime->volume_level, runtime->playback.shuffle_enabled ? 1u : 0u,
                    (unsigned)runtime->playback.repeat_mode,
                    runtime->playback.has_active_track ? 1u : 0u,
                    (long long)runtime->playback.active_track.album_id,
                    (unsigned)runtime->playback.elapsed_ms);
}

static void handle_now_playing_action(struct app_runtime *runtime,
                                      enum now_playing_action_kind np_action) {
    if (np_action == NP_ACTION_EXIT) {
        runtime->home.vinyl_rotation_radians = runtime->now_playing.vinyl_rotation_radians;
        runtime->library.vinyl_rotation_radians = runtime->now_playing.vinyl_rotation_radians;
        runtime->now_playing_entered_from_home = false;
        if (runtime->now_playing.return_target == NP_RETURN_LIBRARY) {
            runtime->home.active_screen = ACTIVE_SCREEN_LIBRARY_SHELL;
            runtime->home.launched_app_id = HOME_APP_LIBRARY;
            runtime->home.launch_progress = 1.0f;
            runtime->home.launch_target = 1.0f;
            library_controller_refresh_catalog_views(&runtime->library);
            library_controller_focus_album_if_crate(&runtime->library,
                                                    runtime->playback.active_track.album_id);
        } else {
            home_controller_return_from_shell(&runtime->home, HOME_APP_NOW_PLAYING);
            if (!runtime->playback.has_active_track) {
                runtime->home.launch_progress = 0.0f;
            }
        }
    } else if (np_action == NP_ACTION_BROWSE_LIBRARY) {
        runtime->now_playing_entered_from_home = false;
        library_controller_reset_to_hub(&runtime->library, true);
        runtime->home.active_screen = ACTIVE_SCREEN_LIBRARY_SHELL;
        runtime->home.launched_app_id = HOME_APP_LIBRARY;
        runtime->home.launch_progress = 1.0f;
        runtime->home.launch_target = 1.0f;
    }
}

static void attempt_send_disable_idle_poweroff(struct app_runtime *runtime, uint64_t now_ns) {
    char command[IPC_COMMAND_BUFFER_BYTES];
    size_t length = protocol_format_disable_idle_poweroff(command, sizeof(command));
    if (length == 0u) {
        runtime->pending_disable_idle_poweroff = false;
        return;
    }

    runtime->disable_idle_poweroff_attempts++;
    bool sent_ok = runtime->platform->ipc_send_player(runtime->platform->context, command, length);
    logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "IPC",
                    "tx 0664 ok=%u attempt=%u", sent_ok ? 1u : 0u,
                    (unsigned)runtime->disable_idle_poweroff_attempts);
    if (sent_ok) {
        runtime->idle_poweroff_disabled = true;
        runtime->pending_disable_idle_poweroff = false;
        if (!runtime->boot_player_configured) {
            runtime->boot_player_configured = true;
            size_t max_vol_len =
                protocol_format_set_max_volume(MAX_VOLUME_DEFAULT, command, sizeof(command));
            if (max_vol_len > 0u) {
                (void)runtime->platform->ipc_send_player(runtime->platform->context, command,
                                                         max_vol_len);
            }
            maybe_restore_playback_state(runtime, now_ns);
        }
        return;
    }

    if (runtime->disable_idle_poweroff_attempts >= IPC_DISABLE_IDLE_MAX_RETRIES) {
        runtime->pending_disable_idle_poweroff = false;
        return;
    }

    runtime->next_ipc_retry_ns = now_ns + (uint64_t)IPC_RETRY_INTERVAL_MS * NANOSECONDS_PER_MILLISECOND;
}

static void enter_home_from_boot_complete(struct app_runtime *runtime, uint64_t now_ns) {
    freeze_animation_metrics(runtime, now_ns);
    runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
    sync_home_telemetry(runtime);
    home_controller_enter_from_boot(&runtime->home);
    set_ui_state(runtime, UI_STATE_HOME);
    runtime->ready_entered_ns = now_ns;
    runtime->last_battery_poll_ns = now_ns;
    runtime->last_home_tick_ns = now_ns;
    runtime->perf_window_start_ns = now_ns;
    runtime->next_frame_ns = now_ns + BOOT_FRAME_PERIOD_NS;

    logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "BOOT",
                    "complete -> home takeover_ms=%llu aa24_ms=%llu frames=%u fps_x10=%u "
                    "max_ms=%u sd=%u",
                    (unsigned long long)runtime->takeover_ms,
                    (unsigned long long)runtime->aa24_ms, (unsigned)runtime->animation_frames,
                    (unsigned)runtime->animation_fps_x10, (unsigned)runtime->max_frame_ms,
                    runtime->hardware.sd_mounted ? 1u : 0u);

    render_home_screen(runtime);

    if (runtime->platform->telemetry_write != NULL) {
        struct telemetry_snapshot snapshot;
        build_telemetry_snapshot(runtime, false, now_ns, &snapshot);
        runtime->platform->telemetry_write(runtime->platform->context, &snapshot);
    }
}

static void begin_library_mount_sync(struct app_runtime *runtime);
static void handle_sd_mount_transition(struct app_runtime *runtime);

static void handle_aa24_during_boot(struct app_runtime *runtime,
                                    const struct boot_config_snapshot *boot_config,
                                    uint64_t now_ns) {
    runtime->aa24_received = true;
    runtime->aa24_ms = elapsed_ms_since(now_ns, runtime->startup_ns);

    runtime->volume_level = boot_config->volume_level;
    runtime->max_volume = MAX_VOLUME_DEFAULT;
    runtime->saved_brightness = boot_config->brightness;
    if (runtime->volume_level > runtime->max_volume) {
        runtime->volume_level = runtime->max_volume;
    }
    runtime->now_playing.volume_level = runtime->volume_level;

    runtime->platform->display_set_brightness(runtime->platform->context, runtime->saved_brightness);

    runtime->pending_disable_idle_poweroff = true;
    runtime->disable_idle_poweroff_attempts = 0u;
    attempt_send_disable_idle_poweroff(runtime, now_ns);

    runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
    logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "BOOT", "aa24 sd=%u",
                    runtime->hardware.sd_mounted ? 1u : 0u);
    if (runtime->hardware.sd_mounted) {
        runtime->boot_sd_missing_since_ns = 0ull;
        if (!runtime->library_scan_started) {
            runtime->library_prev_sd_mounted = true;
            begin_library_mount_sync(runtime);
        } else if (!runtime->library_prev_sd_mounted) {
            handle_sd_mount_transition(runtime);
        }
    } else {
        runtime->boot_sd_missing_since_ns = now_ns;
        runtime->boot_last_sd_probe_ns = now_ns;
    }
    boot_controller_mark_audio_ready(&runtime->boot, runtime->hardware.sd_mounted);

    if (runtime->state == UI_STATE_WARNING) {
        set_ui_state(runtime, UI_STATE_BOOT_ANIMATION);
        runtime->last_boot_tick_ns = now_ns;
        runtime->next_frame_ns = now_ns + BOOT_FRAME_PERIOD_NS;
        render_boot_snapshot_frame(runtime);
    }
}

static const char *resolve_music_root(const struct app_runtime *runtime) {
    if (runtime->platform != NULL && runtime->platform->library_music_root != NULL) {
        return runtime->platform->library_music_root;
    }
    return PATH_SDCARD_MOUNT;
}

static const char *resolve_dialos_directory(const struct app_runtime *runtime) {
    if (runtime->platform != NULL && runtime->platform->library_dialos_directory != NULL) {
        return runtime->platform->library_dialos_directory;
    }
    return PATH_SDCARD_DIALOS_DIRECTORY;
}

static const char *resolve_temp_directory(const struct app_runtime *runtime) {
    if (runtime->platform != NULL && runtime->platform->library_temp_directory != NULL) {
        return runtime->platform->library_temp_directory;
    }
    return PATH_TMP_ROOT;
}

static void sync_library_counts_to_home(struct app_runtime *runtime) {
    struct library_db_counts counts;
    memset(&counts, 0, sizeof(counts));
    if (runtime->library_db_open_ok) {
        (void)library_db_read_counts(&runtime->library_db, &counts);
    }
    runtime->home.telemetry.track_count = counts.tracks;
    runtime->home.telemetry.album_count = counts.albums;
}

static void begin_library_mount_sync(struct app_runtime *runtime) {
    const char *dialos_directory = resolve_dialos_directory(runtime);
    char covers_directory[384];
    snprintf(covers_directory, sizeof(covers_directory), "%s/covers", dialos_directory);

    library_controller_stop_prefetch(&runtime->library);
    now_playing_media_clear(&runtime->now_playing_media);
    library_db_scanner_cancel(&runtime->library_scanner);
    library_cover_cache_cancel_boot_precache(&runtime->library_covers);
    if (runtime->library_db_open_ok) {
        flush_playback_persistence(runtime);
        library_db_close(&runtime->library_db);
        runtime->library_db_open_ok = false;
    }

    library_cover_cache_set_directory(&runtime->library_covers, covers_directory);
    runtime->library_db_open_ok = library_db_open(&runtime->library_db, dialos_directory);
    if (runtime->library_db_open_ok) {
        uint64_t now_ns = runtime->platform != NULL
                              ? runtime->platform->monotonic_ns(runtime->platform->context)
                              : 0ull;
        library_controller_refresh_catalog_views(&runtime->library);
        sync_library_counts_to_home(runtime);
        maybe_restore_playback_state(runtime, now_ns);
    }
    runtime->library_scan_finished_handled = false;
    runtime->boot_cover_precache_started = false;
    runtime->library_scan_start_ns =
        runtime->platform != NULL ? runtime->platform->monotonic_ns(runtime->platform->context)
                                  : 0ull;
    runtime->library_scan_started =
        runtime->library_db_open_ok &&
        library_db_scanner_start(&runtime->library_scanner, &runtime->library_db,
                                 resolve_music_root(runtime), covers_directory);
    if (!runtime->library_scan_started) {
        runtime->library_scan_finished_handled = true;
    }
    if (runtime->state == UI_STATE_BOOT_ANIMATION || runtime->state == UI_STATE_WARNING) {
        struct library_db_scan_snapshot scan_snap;
        library_db_scanner_poll(&runtime->library_scanner, &scan_snap);
        struct library_db_counts counts;
        memset(&counts, 0, sizeof(counts));
        if (runtime->library_db_open_ok) {
            (void)library_db_read_counts(&runtime->library_db, &counts);
        }
        bool scan_ready = !runtime->library_scan_started || scan_snap.ready;
        boot_controller_update_library_scan(
            &runtime->boot, (uint32_t)scan_snap.phase, scan_snap.discovered_files,
            scan_snap.scanned_files, counts.albums, counts.tracks,
            scan_snap.music_root_exists, scan_ready);
    }
}

static void finish_library_scan_step(struct app_runtime *runtime) {
    library_db_scanner_wait(&runtime->library_scanner);
    runtime->library_scan_finished_handled = true;
    if (runtime->library_db_open_ok && runtime->playback_state_restored &&
        runtime->playback.has_active_track && !runtime->playback.ephemeral_active &&
        library_db_playback_queue_count(&runtime->library_db) > 0u) {
        bool became_ephemeral = false;
        (void)library_db_reconcile_playback_queue(
            &runtime->library_db, &runtime->playback.context,
            runtime->playback.active_track.track_id, runtime->playback.shuffle_enabled,
            &runtime->playback.current_slot, &runtime->playback.queue_total, &became_ephemeral);
        if (became_ephemeral) {
            runtime->playback.ephemeral_active = true;
        }
        struct library_db_track_details refreshed_track;
        if (library_db_get_queue_track(&runtime->library_db, runtime->playback.current_slot,
                                       &refreshed_track)) {
            runtime->playback.active_track = refreshed_track;
        }
        sync_playback_to_ui(runtime);
        (void)library_cover_cache_lookup_or_load_disk(
            &runtime->library_covers, runtime->playback.active_track.album_uid);
        now_playing_media_request_active(&runtime->now_playing_media,
                                         runtime->playback.active_track.album_id,
                                         runtime->playback.active_track.album_uid,
                                         runtime->playback.active_track.path);
    }
    library_controller_refresh_catalog_views(&runtime->library);
    sync_library_counts_to_home(runtime);
    if (runtime->platform != NULL) {
        maybe_restore_playback_state(
            runtime, runtime->platform->monotonic_ns(runtime->platform->context));
    }
}

static void handle_sd_mount_transition(struct app_runtime *runtime) {
    bool now_mounted = runtime->hardware.sd_mounted;
    if (now_mounted == runtime->library_prev_sd_mounted) {
        return;
    }
    runtime->library_prev_sd_mounted = now_mounted;
    if (now_mounted) {
        begin_library_mount_sync(runtime);
    } else {
        library_controller_stop_prefetch(&runtime->library);
        now_playing_media_clear(&runtime->now_playing_media);
        library_db_scanner_cancel(&runtime->library_scanner);
        library_cover_cache_cancel_boot_precache(&runtime->library_covers);
        library_cover_cache_clear(&runtime->library_covers);
        if (runtime->library_db_open_ok) {
            flush_playback_persistence(runtime);
            library_db_close(&runtime->library_db);
            runtime->library_db_open_ok = false;
        }
        runtime->library_scan_started = false;
        runtime->library_scan_finished_handled = false;
        boot_controller_update_cover_precache(&runtime->boot, false, 0u, 0u);
        library_controller_reset_to_hub(&runtime->library, true);
        sync_library_counts_to_home(runtime);
    }
}

void app_destroy(struct app_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }
    library_controller_stop_prefetch(&runtime->library);
    library_db_scanner_cancel(&runtime->library_scanner);
    library_cover_cache_cancel_boot_precache(&runtime->library_covers);
    if (runtime->library_db_open_ok) {
        flush_playback_persistence(runtime);
    }
    now_playing_media_destroy(&runtime->now_playing_media);
    library_controller_destroy(&runtime->library);
    library_cover_cache_destroy(&runtime->library_covers);
    library_db_scanner_destroy(&runtime->library_scanner);
    if (runtime->library_db_open_ok) {
        library_db_close(&runtime->library_db);
        runtime->library_db_open_ok = false;
    }
}

void app_initialize(struct app_runtime *runtime, const struct platform_operations *platform) {
    if (runtime == NULL || platform == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->platform = platform;
    runtime->appearance_mode = APPEARANCE_MODE_DARK;
    if (platform->appearance_mode_read != NULL) {
        enum appearance_mode saved_mode = APPEARANCE_MODE_DARK;
        if (platform->appearance_mode_read(platform->context, &saved_mode)) {
            runtime->appearance_mode = saved_mode;
        }
    }
    runtime->volume_level = VOLUME_DEFAULT;
    runtime->max_volume = MAX_VOLUME_DEFAULT;
    runtime->saved_brightness = BRIGHTNESS_BOOT_DEFAULT;
    runtime->startup_ns = platform->monotonic_ns(platform->context);
    runtime->last_boot_tick_ns = runtime->startup_ns;
    logger_initialize(&runtime->logger);
    boot_controller_initialize(&runtime->boot);
    runtime->last_logged_boot_phase = runtime->boot.phase;
    home_controller_initialize(&runtime->home);
    library_db_scanner_initialize(&runtime->library_scanner);
    library_cover_cache_initialize(&runtime->library_covers, "");
    library_controller_initialize(&runtime->library, &runtime->library_db,
                                  &runtime->library_covers);
    playback_engine_initialize(&runtime->playback, &runtime->library_db);
    now_playing_media_initialize(&runtime->now_playing_media, &runtime->library_db);
    now_playing_controller_initialize(&runtime->now_playing, &runtime->playback,
                                      &runtime->library_db, &runtime->now_playing_media);
    if (platform->probe_hardware_status != NULL) {
        platform->probe_hardware_status(platform->context, &runtime->hardware);
    }
    runtime->library_prev_sd_mounted = false;
    sync_home_telemetry(runtime);

    runtime->initial_crash_count =
        platform->crash_count_read != NULL ? platform->crash_count_read(platform->context) : 0u;

    logger_log_line(&runtime->logger, 0ull, "BOOT", "start appearance=%s crash_count=%u",
                    runtime->appearance_mode == APPEARANCE_MODE_LIGHT ? "light" : "dark",
                    (unsigned)runtime->initial_crash_count);

    if (crash_should_enter_screen(runtime->initial_crash_count)) {
        set_ui_state(runtime, UI_STATE_CRASH_SCREEN);
        flush_runtime_logs(runtime, true);
        struct framebuffer_surface surface;
        if (platform->display_acquire_surface(platform->context, &surface)) {
            render_draw_crash_frame(&surface, runtime->initial_crash_count,
                                    runtime->appearance_mode);
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
    render_boot_snapshot_frame(runtime);
    platform->display_set_brightness(platform->context, BRIGHTNESS_BOOT_DEFAULT);

    uint64_t frame_end_ns = platform->monotonic_ns(platform->context);
    runtime->takeover_ms = elapsed_ms_since(frame_end_ns, runtime->startup_ns);
    runtime->max_frame_ms = (uint32_t)elapsed_ms_since(frame_end_ns, frame_start_ns);
    runtime->animation_frames = 1u;
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
            logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "IPC",
                            "rx aa24 vol=%u max_vol=%u bright=%u",
                            (unsigned)message->boot_config.volume_level,
                            (unsigned)message->boot_config.max_volume,
                            (unsigned)message->boot_config.brightness);
            if (runtime->state == UI_STATE_BOOT_ANIMATION || runtime->state == UI_STATE_WARNING) {
                handle_aa24_during_boot(runtime, &message->boot_config, now_ns);
                sync_home_telemetry(runtime);
            } else if (runtime->state == UI_STATE_HOME || runtime->state == UI_STATE_LOCKED) {
                runtime->saved_brightness = message->boot_config.brightness;
                if (runtime->state == UI_STATE_HOME) {
                    runtime->platform->display_set_brightness(runtime->platform->context,
                                                              runtime->saved_brightness);
                }
            }
            break;

        case IPC_INBOUND_INPUT_MODE_READY:
            logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "IPC", "rx a607");
            if (runtime->aa24_received && !runtime->a607_resent_disable) {
                runtime->a607_resent_disable = true;
                runtime->pending_disable_idle_poweroff = true;
                runtime->disable_idle_poweroff_attempts = 0u;
                attempt_send_disable_idle_poweroff(runtime, now_ns);
            }
            break;

        case IPC_INBOUND_SDCARD_EVENT:
            logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "IPC",
                            "rx aa22 inserted=%u", message->sdcard_inserted ? 1u : 0u);
            if (runtime->state == UI_STATE_BOOT_ANIMATION || runtime->state == UI_STATE_WARNING) {
                runtime->platform->probe_hardware_status(runtime->platform->context,
                                                         &runtime->hardware);
                boot_controller_update_sd_mounted(&runtime->boot, runtime->hardware.sd_mounted);
                if (runtime->aa24_received) {
                    handle_sd_mount_transition(runtime);
                }
                sync_home_telemetry(runtime);
            } else if (runtime->state == UI_STATE_HOME) {
                runtime->platform->probe_hardware_status(runtime->platform->context,
                                                         &runtime->hardware);
                handle_sd_mount_transition(runtime);
                sync_home_telemetry(runtime);
                runtime->sd_followup_first_pending = true;
                runtime->sd_followup_first_ns =
                    now_ns + (uint64_t)SD_MOUNT_FOLLOWUP_FIRST_MS * NANOSECONDS_PER_MILLISECOND;
                runtime->sd_followup_second_pending = true;
                runtime->sd_followup_second_ns =
                    now_ns + (uint64_t)SD_MOUNT_FOLLOWUP_SECOND_MS * NANOSECONDS_PER_MILLISECOND;
            }
            break;

        case IPC_INBOUND_PLAYBACK_STATUS:
            if (message->playback_status.has_state) {
                logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "IPC",
                                "rx a202 state=%d", (int)message->playback_status.state);
            }
            if (runtime->library_db_open_ok) {
                struct playback_engine_effects effects;
                playback_engine_handle_ipc_status(&runtime->playback, &message->playback_status,
                                                  &effects);
                execute_playback_effects(runtime, &effects, now_ns);
            } else {
                if (message->playback_status.has_track_metadata) {
                    snprintf(runtime->home.telemetry.track_title,
                             sizeof(runtime->home.telemetry.track_title), "%s",
                             message->playback_status.track_title);
                    runtime->home.telemetry.duration_ms = message->playback_status.duration_ms;
                    runtime->home.telemetry.elapsed_ms = 0u;
                    runtime->home.elapsed_fractional_ms = 0.0f;
                }
                if (message->playback_status.has_state) {
                    enum home_playback_mode mode = HOME_PLAYBACK_IDLE;
                    if (message->playback_status.state == IPC_PLAYBACK_PLAYING) {
                        mode = HOME_PLAYBACK_PLAYING;
                    } else if (message->playback_status.state == IPC_PLAYBACK_PAUSED) {
                        mode = HOME_PLAYBACK_PAUSED;
                    }
                    runtime->non_storage_playback_mode = mode;
                    if (!runtime->hardware.usb_storage_active) {
                        runtime->home.telemetry.playback_mode = mode;
                    }
                }
                if (message->playback_status.has_track_path) {
                    library_controller_set_active_playback(
                        &runtime->library, message->playback_status.track_path,
                        runtime->non_storage_playback_mode == HOME_PLAYBACK_PLAYING);
                } else if (message->playback_status.has_state) {
                    runtime->library.is_playing =
                        (runtime->non_storage_playback_mode == HOME_PLAYBACK_PLAYING);
                }
            }
            break;

        case IPC_INBOUND_PLAYBACK_ELAPSED:
            if (playback_engine_handle_ipc_elapsed(&runtime->playback, message->elapsed_ms)) {
                now_playing_controller_on_ipc_elapsed(&runtime->now_playing,
                                                      runtime->playback.elapsed_ms);
            }
            runtime->home.telemetry.elapsed_ms = message->elapsed_ms;
            runtime->home.elapsed_fractional_ms = 0.0f;
            break;

        case IPC_INBOUND_PLAYBACK_STOP:
            logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "IPC", "rx a102");
            if (runtime->library_db_open_ok) {
                struct playback_engine_effects effects;
                playback_engine_handle_ipc_stop(&runtime->playback, &effects);
                execute_playback_effects(runtime, &effects, now_ns);
            } else {
                runtime->non_storage_playback_mode = HOME_PLAYBACK_IDLE;
                runtime->home.telemetry.elapsed_ms = 0u;
                runtime->home.telemetry.duration_ms = 0u;
                runtime->home.telemetry.track_title[0] = '\0';
                library_controller_set_active_playback(&runtime->library, NULL, false);
                if (!runtime->hardware.usb_storage_active) {
                    runtime->home.telemetry.playback_mode = HOME_PLAYBACK_IDLE;
                }
            }
            break;

        case IPC_INBOUND_VOLUME_UPDATE: {
            uint8_t next_volume = message->volume_level;
            if (next_volume > runtime->max_volume) {
                next_volume = runtime->max_volume;
            }
            runtime->volume_level = next_volume;
            runtime->now_playing.volume_level = next_volume;
            sync_home_telemetry(runtime);
            mark_playback_persistence_dirty(runtime, now_ns);
            break;
        }

        case IPC_INBOUND_USB_CABLE_EVENT:
        case IPC_INBOUND_BATTERY_EVENT:
            if (runtime->state == UI_STATE_HOME) {
                runtime->platform->probe_hardware_status(runtime->platform->context,
                                                         &runtime->hardware);
                sync_home_telemetry(runtime);
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
                render_draw_shutdown_frame(&surface, runtime->appearance_mode);
                runtime->platform->display_present_surface(runtime->platform->context);
            }
        }

        if (runtime->library_db_open_ok) {
            playback_engine_save_to_db(&runtime->playback, runtime->volume_level);
            runtime->playback.persistence_dirty = false;
            runtime->persistence_dirty = false;
        }

        char mute_command[IPC_COMMAND_BUFFER_BYTES];
        size_t length = protocol_format_set_volume(0u, mute_command, sizeof(mute_command));
        if (length > 0u) {
            (void)runtime->platform->ipc_send_player(runtime->platform->context, mute_command, length);
        }

        struct telemetry_snapshot final_snapshot;
        build_telemetry_snapshot(runtime, true, now_ns, &final_snapshot);
        logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "POWER",
                        "shutdown uptime_ms=%llu home_frames=%u home_fps_x10=%u home_max_ms=%u",
                        (unsigned long long)final_snapshot.uptime_ms,
                        (unsigned)final_snapshot.home_frames,
                        (unsigned)final_snapshot.home_fps_x10,
                        (unsigned)final_snapshot.home_max_frame_ms);
        flush_runtime_logs(runtime, true);
        runtime->platform->execute_shutdown(runtime->platform->context, screen_was_locked,
                                            &final_snapshot);
        return;
    }

    if (runtime->state == UI_STATE_HOME || runtime->state == UI_STATE_LOCKED) {
        if (action == BUTTON_ACTION_POWER_SHORT) {
            uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            if (runtime->state == UI_STATE_HOME) {
                logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "POWER",
                                "lock");
                flush_runtime_logs(runtime, true);
                set_ui_state(runtime, UI_STATE_LOCKED);
                runtime->platform->display_enter_sleep(runtime->platform->context);
            } else {
                logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "POWER",
                                "wake");
                runtime->platform->display_begin_wake(runtime->platform->context);
                runtime->platform->probe_hardware_status(runtime->platform->context,
                                                         &runtime->hardware);
                sync_home_telemetry(runtime);
                runtime->last_battery_poll_ns = now_ns;
                runtime->last_home_tick_ns = now_ns;
                runtime->perf_window_start_ns = now_ns;
                runtime->next_frame_ns = now_ns + BOOT_FRAME_PERIOD_NS;
                set_ui_state(runtime, UI_STATE_HOME);
                render_home_screen(runtime);
                runtime->platform->display_finish_wake(runtime->platform->context,
                                                       runtime->saved_brightness);
            }
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
                uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);
                runtime->volume_level = next_volume;
                runtime->now_playing.volume_level = next_volume;
                sync_home_telemetry(runtime);
                mark_playback_persistence_dirty(runtime, now_ns);
                logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "INPUT",
                                "button=%s vol=%u",
                                action == BUTTON_ACTION_VOLUME_UP ? "vol_up" : "vol_down",
                                (unsigned)next_volume);
                char command[IPC_COMMAND_BUFFER_BYTES];
                size_t length = protocol_format_set_volume(next_volume, command, sizeof(command));
                if (length > 0u) {
                    (void)runtime->platform->ipc_send_player(runtime->platform->context, command,
                                                             length);
                }
                if (runtime->state == UI_STATE_HOME) {
                    if (runtime->home.active_screen == ACTIVE_SCREEN_NOW_PLAYING_SHELL) {
                        now_playing_controller_show_volume_hud(&runtime->now_playing, next_volume);
                    }
                    render_home_screen(runtime);
                }
            }
            return;
        }

        if (action == BUTTON_ACTION_PLAY_PAUSE) {
            uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            struct playback_engine_effects effects;
            if (playback_engine_toggle_play_pause(&runtime->playback, &effects)) {
                execute_playback_effects(runtime, &effects, now_ns);
                if (runtime->state == UI_STATE_HOME &&
                    runtime->home.active_screen == ACTIVE_SCREEN_NOW_PLAYING_SHELL) {
                    now_playing_controller_trigger_play_pulse(&runtime->now_playing,
                                                              runtime->playback.is_playing);
                }
            }
            return;
        }

        if (action == BUTTON_ACTION_NEXT_TRACK || action == BUTTON_ACTION_PREV_TRACK) {
            uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            int32_t direction = (action == BUTTON_ACTION_NEXT_TRACK) ? 1 : -1;
            struct playback_engine_effects effects;
            if (playback_engine_skip(&runtime->playback, direction, &effects)) {
                execute_playback_effects(runtime, &effects, now_ns);
            }
            return;
        }
    }
}

static bool is_inside_warning_retry_button(int32_t x, int32_t y) {
    return abs(x - SCREEN_CENTER_X) <= 92 && y >= 238 && y <= 290;
}

void app_handle_touch_event(struct app_runtime *runtime, const struct touch_event *event) {
    if (runtime == NULL || event == NULL || runtime->platform == NULL || runtime->shutting_down ||
        event->kind == TOUCH_EVENT_NONE) {
        return;
    }

    if (runtime->state == UI_STATE_CRASH_SCREEN || runtime->state == UI_STATE_SHUTTING_DOWN ||
        runtime->state == UI_STATE_LOCKED) {
        return;
    }

    if (runtime->state == UI_STATE_WARNING) {
        if (event->kind == TOUCH_EVENT_DOWN) {
            runtime->warning_retry_pressed = is_inside_warning_retry_button(event->x, event->y);
            runtime->warning_retry_start_x = event->x;
            runtime->warning_retry_start_y = event->y;
            return;
        }
        if (event->kind == TOUCH_EVENT_UP && runtime->warning_retry_pressed) {
            runtime->warning_retry_pressed = false;
            int32_t delta_x = event->x - runtime->warning_retry_start_x;
            int32_t delta_y = event->y - runtime->warning_retry_start_y;
            float drift_sq = (float)(delta_x * delta_x + delta_y * delta_y);
            if (drift_sq <= BOOT_RETRY_TAP_DRIFT_LIMIT_PX * BOOT_RETRY_TAP_DRIFT_LIMIT_PX &&
                is_inside_warning_retry_button(event->x, event->y)) {
                uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);
                boot_controller_retry_from_warning(&runtime->boot);
                set_ui_state(runtime, UI_STATE_BOOT_ANIMATION);
                runtime->last_boot_tick_ns = now_ns;
                runtime->next_frame_ns = now_ns + BOOT_FRAME_PERIOD_NS;
                render_boot_snapshot_frame(runtime);
            }
        }
        return;
    }

    if (runtime->state == UI_STATE_HOME) {
        if (runtime->home.active_screen == ACTIVE_SCREEN_NOW_PLAYING_SHELL) {
            uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            struct playback_engine_effects effects;
            bool volume_changed = false;
            enum now_playing_action_kind np_action = NP_ACTION_NONE;
            now_playing_controller_handle_touch(&runtime->now_playing, event, &effects,
                                                &volume_changed, &np_action);
            if (volume_changed) {
                if (runtime->now_playing.volume_level > runtime->max_volume) {
                    runtime->now_playing.volume_level = runtime->max_volume;
                }
                runtime->volume_level = runtime->now_playing.volume_level;
                sync_home_telemetry(runtime);
                mark_playback_persistence_dirty(runtime, now_ns);
                char command[IPC_COMMAND_BUFFER_BYTES];
                size_t length =
                    protocol_format_set_volume(runtime->volume_level, command, sizeof(command));
                if (length > 0u) {
                    (void)runtime->platform->ipc_send_player(runtime->platform->context, command,
                                                             length);
                }
            }
            execute_playback_effects(runtime, &effects, now_ns);
            if (np_action != NP_ACTION_NONE) {
                handle_now_playing_action(runtime, np_action);
            }
            return;
        }

        if (runtime->home.active_screen == ACTIVE_SCREEN_LIBRARY_SHELL) {
            uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            struct library_controller_action action;
            library_controller_handle_touch(&runtime->library, event, &action);
            if (action.kind == LIBRARY_ACTION_PLAY_TRACKS) {
                struct playback_engine_effects effects;
                if (playback_engine_launch_context(
                        &runtime->playback, &action.play_collection, action.play_context_title,
                        action.play_first_track_id, action.play_has_shuffle_override,
                        action.play_shuffle, &effects)) {
                    execute_playback_effects(runtime, &effects, now_ns);
                    runtime->home.active_screen = ACTIVE_SCREEN_NOW_PLAYING_SHELL;
                    runtime->now_playing_entered_from_home = true;
                    now_playing_controller_enter(
                        &runtime->now_playing, NP_RETURN_LIBRARY,
                        action.play_from_album_crate ? NP_OPEN_FROM_ALBUM_CRATE
                                                     : NP_OPEN_FROM_LIBRARY,
                        false, runtime->library.vinyl_rotation_radians, runtime->volume_level);
                }
            } else if (action.kind == LIBRARY_ACTION_OPEN_NOW_PLAYING) {
                runtime->home.active_screen = ACTIVE_SCREEN_NOW_PLAYING_SHELL;
                runtime->now_playing_entered_from_home = true;
                now_playing_controller_enter(&runtime->now_playing, NP_RETURN_LIBRARY,
                                             NP_OPEN_FROM_ALBUM_CRATE, false,
                                             runtime->library.vinyl_rotation_radians,
                                             runtime->volume_level);
            } else if (action.kind == LIBRARY_ACTION_TOGGLE_PLAY_PAUSE) {
                struct playback_engine_effects effects;
                if (playback_engine_toggle_play_pause(&runtime->playback, &effects)) {
                    execute_playback_effects(runtime, &effects, now_ns);
                }
            } else if (action.kind == LIBRARY_ACTION_EXIT_TO_HOME) {
                runtime->home.vinyl_rotation_radians = runtime->library.vinyl_rotation_radians;
                home_controller_return_from_shell(&runtime->home, HOME_APP_LIBRARY);
            }
            if (action.favorite_mutated) {
                struct playback_engine_effects effects;
                playback_engine_on_favorites_mutated(&runtime->playback, &effects);
                execute_playback_effects(runtime, &effects, now_ns);
            }
            return;
        }

        enum home_controller_event home_event =
            home_controller_handle_touch(&runtime->home, event);
        if (home_event == HOME_EVENT_TOGGLE_APPEARANCE) {
            uint64_t now_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            runtime->appearance_mode = (runtime->appearance_mode == APPEARANCE_MODE_DARK)
                                           ? APPEARANCE_MODE_LIGHT
                                           : APPEARANCE_MODE_DARK;
            logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "HOME",
                            "appearance mode=%s",
                            runtime->appearance_mode == APPEARANCE_MODE_LIGHT ? "light" : "dark");
            if (runtime->platform->appearance_mode_write != NULL) {
                runtime->platform->appearance_mode_write(runtime->platform->context,
                                                         runtime->appearance_mode);
            }
        }
    }
}

void app_handle_tick(struct app_runtime *runtime, uint64_t now_ns) {
    if (runtime == NULL || runtime->platform == NULL || runtime->shutting_down) {
        return;
    }

    if (runtime->pending_disable_idle_poweroff && now_ns >= runtime->next_ipc_retry_ns) {
        attempt_send_disable_idle_poweroff(runtime, now_ns);
    }

    if (runtime->state == UI_STATE_BOOT_ANIMATION || runtime->state == UI_STATE_WARNING) {
        uint64_t warning_deadline =
            runtime->startup_ns + (uint64_t)AA24_WARNING_TIMEOUT_MS * NANOSECONDS_PER_MILLISECOND;
        bool warning_due = (runtime->state == UI_STATE_BOOT_ANIMATION && !runtime->aa24_received &&
                            now_ns >= warning_deadline);
        if (now_ns >= runtime->next_frame_ns || warning_due) {
            float delta_seconds =
                (now_ns > runtime->last_boot_tick_ns)
                    ? (float)(now_ns - runtime->last_boot_tick_ns) / (float)NANOSECONDS_PER_SECOND
                    : 0.0f;
            if (!warning_due && delta_seconds > 0.05f) {
                delta_seconds = 0.05f;
            }
            runtime->last_boot_tick_ns = now_ns;
            if (runtime->hardware.sd_mounted && runtime->library_scan_started) {
                struct library_db_scan_snapshot scan_snap;
                library_db_scanner_poll(&runtime->library_scanner, &scan_snap);
                struct library_db_counts counts = runtime->library.counts;
                if (scan_snap.ready && !runtime->library_scan_finished_handled) {
                    finish_library_scan_step(runtime);
                    if (runtime->library_db_open_ok) {
                        (void)library_db_read_counts(&runtime->library_db, &counts);
                    }
                    uint64_t scan_end_ns =
                        runtime->platform->monotonic_ns(runtime->platform->context);
                    logger_log_line(
                        &runtime->logger, runtime_uptime_ms(runtime, scan_end_ns), "BOOT",
                        "scan_done ms=%llu tracks=%u albums=%u artists=%u genres=%u playlists=%u "
                        "added=%u updated=%u moved=%u removed=%u tz_start=%d tz_end=%d",
                        (unsigned long long)elapsed_ms_since(scan_end_ns,
                                                             runtime->library_scan_start_ns),
                        (unsigned)counts.tracks, (unsigned)counts.albums,
                        (unsigned)counts.artists, (unsigned)counts.genres,
                        (unsigned)counts.playlists, (unsigned)scan_snap.telemetry.added_count,
                        (unsigned)scan_snap.telemetry.modified_count,
                        (unsigned)(scan_snap.telemetry.moved_count +
                                   scan_snap.telemetry.moved_edited_count),
                        (unsigned)scan_snap.telemetry.deleted_count,
                        (int)scan_snap.telemetry.kernel_tz_minutes_west,
                        (int)scan_snap.telemetry.kernel_tz_end_minutes_west);
                }
                boot_controller_update_library_scan(
                    &runtime->boot, (uint32_t)scan_snap.phase, scan_snap.discovered_files,
                    scan_snap.scanned_files, counts.albums, counts.tracks,
                    scan_snap.music_root_exists, scan_snap.ready);
            }
            if (runtime->boot.phase == BOOT_PHASE_BUFFER && runtime->aa24_received) {
                if (!runtime->hardware.sd_mounted) {
                    if (runtime->boot_sd_missing_since_ns == 0ull) {
                        runtime->boot_sd_missing_since_ns = now_ns;
                        runtime->boot_last_sd_probe_ns = now_ns;
                    }
                    if (elapsed_ms_since(now_ns, runtime->boot_last_sd_probe_ns) >= 500ull) {
                        runtime->boot_last_sd_probe_ns = now_ns;
                        runtime->platform->probe_hardware_status(runtime->platform->context,
                                                                 &runtime->hardware);
                        boot_controller_update_sd_mounted(&runtime->boot,
                                                          runtime->hardware.sd_mounted);
                        if (runtime->hardware.sd_mounted) {
                            runtime->boot_sd_missing_since_ns = 0ull;
                            logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns),
                                            "BOOT", "sd_recovered");
                            if (!runtime->library_scan_started) {
                                runtime->library_prev_sd_mounted = true;
                                begin_library_mount_sync(runtime);
                            }
                        }
                    }
                    if (!runtime->hardware.sd_mounted &&
                        elapsed_ms_since(now_ns, runtime->boot_sd_missing_since_ns) >= 2500ull) {
                        if (library_cover_cache_is_boot_precaching(&runtime->library_covers) ||
                            runtime->boot.cover_precache_active) {
                            library_cover_cache_cancel_boot_precache(&runtime->library_covers);
                            boot_controller_update_cover_precache(
                                &runtime->boot, false, runtime->library_covers.precache_done,
                                runtime->library_covers.precache_total);
                            logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns),
                                            "BOOT", "sd_missing_timeout");
                        }
                    }
                } else {
                    runtime->boot_sd_missing_since_ns = 0ull;
                    if (runtime->library_scan_finished_handled) {
                        if (!runtime->boot_cover_precache_started) {
                            runtime->boot_cover_precache_started = true;
                            runtime->cover_precache_start_ns = now_ns;
                            runtime->boot_last_precache_log_ns = now_ns;
                            library_cover_cache_begin_boot_precache_db(&runtime->library_covers,
                                                                       &runtime->library_db);
                            if (library_cover_cache_is_boot_precaching(&runtime->library_covers)) {
                                boot_controller_update_cover_precache(
                                    &runtime->boot, true, 0u,
                                    runtime->library_covers.precache_total);
                                logger_log_line(&runtime->logger,
                                                runtime_uptime_ms(runtime, now_ns), "BOOT",
                                                "precache_start albums_with_cover=%u",
                                                (unsigned)runtime->library_covers.precache_total);
                                flush_runtime_logs(runtime, false);
                            }
                        } else if (library_cover_cache_is_boot_precaching(
                                       &runtime->library_covers)) {
                            (void)library_cover_cache_step_boot_precache_db(
                                &runtime->library_covers, &runtime->library_db,
                                runtime->platform->monotonic_ns, runtime->platform->context,
                                LIBRARY_COVER_PRECACHE_STAT_BUDGET_NS);
                            uint32_t avail_fail_logs =
                                atomic_load(&runtime->library_covers.precache_fail_log_count);
                            while (runtime->library_covers.precache_fail_log_read_cursor <
                                       avail_fail_logs &&
                                   runtime->library_covers.precache_fail_log_read_cursor <
                                       LIBRARY_COVER_MAX_FAIL_LOGS) {
                                uint32_t slot_idx =
                                    runtime->library_covers.precache_fail_log_read_cursor++;
                                const struct library_cover_fail_entry *entry =
                                    &runtime->library_covers.precache_fail_logs[slot_idx];
                                logger_log_line(&runtime->logger,
                                                runtime_uptime_ms(runtime, now_ns), "BOOT",
                                                "cover_fail done=%u reason=%u path=%s",
                                                (unsigned)entry->done_index,
                                                (unsigned)entry->reason, entry->path);
                            }
                            bool still_precaching =
                                library_cover_cache_is_boot_precaching(&runtime->library_covers);
                            boot_controller_update_cover_precache(
                                &runtime->boot, still_precaching,
                                runtime->library_covers.precache_done,
                                runtime->library_covers.precache_total);
                            if (still_precaching &&
                                elapsed_ms_since(now_ns, runtime->boot_last_precache_log_ns) >=
                                    5000ull) {
                                runtime->boot_last_precache_log_ns = now_ns;
                                logger_log_line(
                                    &runtime->logger, runtime_uptime_ms(runtime, now_ns), "BOOT",
                                    "precache_progress done=%u total=%u decoded=%u hits=%u "
                                    "failed=%u max_ms=%u",
                                    (unsigned)runtime->library_covers.precache_done,
                                    (unsigned)runtime->library_covers.precache_total,
                                    (unsigned)runtime->library_covers.precache_decoded,
                                    (unsigned)runtime->library_covers.precache_disk_hits,
                                    (unsigned)runtime->library_covers.precache_failed,
                                    (unsigned)runtime->library_covers.precache_max_decode_ms);
                                flush_runtime_logs(runtime, false);
                            }
                            if (!still_precaching) {
                                uint64_t precache_end_ns =
                                    runtime->platform->monotonic_ns(runtime->platform->context);
                                logger_log_line(
                                    &runtime->logger, runtime_uptime_ms(runtime, precache_end_ns),
                                    "BOOT",
                                    "precache_done ms=%llu decoded=%u disk_hits=%u "
                                    "ram_preloaded=%u failed=%u max_decode_ms=%u",
                                    (unsigned long long)elapsed_ms_since(
                                        precache_end_ns, runtime->cover_precache_start_ns),
                                    (unsigned)runtime->library_covers.precache_decoded,
                                    (unsigned)runtime->library_covers.precache_disk_hits,
                                    (unsigned)runtime->library_covers.precache_ram_preloaded,
                                    (unsigned)runtime->library_covers.precache_failed,
                                    (unsigned)runtime->library_covers.precache_max_decode_ms);
                                flush_runtime_logs(runtime, false);
                            }
                        }
                    }
                }
            }
            boot_controller_step(&runtime->boot, delta_seconds);

            if (runtime->boot.phase != runtime->last_logged_boot_phase) {
                logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "BOOT",
                                "phase %d -> %d frames=%u max_ms=%u",
                                (int)runtime->last_logged_boot_phase, (int)runtime->boot.phase,
                                (unsigned)runtime->animation_frames,
                                (unsigned)runtime->max_frame_ms);
                runtime->last_logged_boot_phase = runtime->boot.phase;
            }

            if (runtime->boot.phase == BOOT_PHASE_COMPLETED) {
                enter_home_from_boot_complete(runtime, now_ns);
                return;
            }

            enum ui_screen_state next_state =
                (runtime->boot.phase == BOOT_PHASE_WARNING) ? UI_STATE_WARNING
                                                            : UI_STATE_BOOT_ANIMATION;
            if (next_state != runtime->state) {
                set_ui_state(runtime, next_state);
                if (next_state == UI_STATE_WARNING) {
                    freeze_animation_metrics(runtime, now_ns);
                    flush_runtime_logs(runtime, true);
                }
            }

            uint64_t frame_start_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            render_boot_snapshot_frame(runtime);
            uint64_t frame_end_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            uint32_t frame_ms = (uint32_t)elapsed_ms_since(frame_end_ns, frame_start_ns);
            if (frame_ms > runtime->max_frame_ms) {
                runtime->max_frame_ms = frame_ms;
            }
            runtime->animation_frames++;
            runtime->next_frame_ns += BOOT_FRAME_PERIOD_NS;
            if (runtime->next_frame_ns <= frame_end_ns) {
                runtime->next_frame_ns = frame_end_ns + BOOT_FRAME_PERIOD_NS;
            }
        }
        return;
    }

    if (runtime->state != UI_STATE_HOME && runtime->state != UI_STATE_LOCKED) {
        return;
    }

    if (!runtime->crash_count_reset_done &&
        elapsed_ms_since(now_ns, runtime->ready_entered_ns) >= READY_STABILITY_WINDOW_MS) {
        runtime->crash_count_reset_done = true;
        if (runtime->initial_crash_count > 0u && runtime->platform->crash_count_write != NULL) {
            runtime->platform->crash_count_write(runtime->platform->context, 0u);
        }
    }

    if (runtime->library_db_open_ok &&
        (runtime->persistence_dirty || runtime->playback.persistence_dirty) &&
        runtime->persistence_dirty_since_ns > 0ull &&
        elapsed_ms_since(now_ns, runtime->persistence_dirty_since_ns) >= 1500ull) {
        flush_playback_persistence(runtime);
    }

    if (runtime->state == UI_STATE_HOME) {
        if (runtime->sd_followup_first_pending && now_ns >= runtime->sd_followup_first_ns) {
            runtime->sd_followup_first_pending = false;
            runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
            handle_sd_mount_transition(runtime);
            sync_home_telemetry(runtime);
        }

        if (runtime->sd_followup_second_pending && now_ns >= runtime->sd_followup_second_ns) {
            runtime->sd_followup_second_pending = false;
            runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
            handle_sd_mount_transition(runtime);
            sync_home_telemetry(runtime);
        }

        if (elapsed_ms_since(now_ns, runtime->last_battery_poll_ns) >= BATTERY_POLL_INTERVAL_MS) {
            runtime->platform->probe_hardware_status(runtime->platform->context, &runtime->hardware);
            runtime->last_battery_poll_ns = now_ns;
            sync_home_telemetry(runtime);
        }

        if (now_ns >= runtime->next_frame_ns) {
            uint64_t tick_start_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            float delta_seconds =
                (now_ns > runtime->last_home_tick_ns)
                    ? (float)(now_ns - runtime->last_home_tick_ns) / (float)NANOSECONDS_PER_SECOND
                    : 0.0f;
            if (delta_seconds > 0.05f) {
                delta_seconds = 0.05f;
            }
            runtime->last_home_tick_ns = now_ns;
            uint32_t scan_ms = 0u;
            if (runtime->hardware.sd_mounted && runtime->library_scan_started &&
                !runtime->library_scan_finished_handled) {
                uint64_t scan_t0 = runtime->platform->monotonic_ns(runtime->platform->context);
                struct library_db_scan_snapshot scan_snap;
                library_db_scanner_poll(&runtime->library_scanner, &scan_snap);
                if (scan_snap.ready) {
                    finish_library_scan_step(runtime);
                }
                scan_ms = (uint32_t)elapsed_ms_since(
                    runtime->platform->monotonic_ns(runtime->platform->context), scan_t0);
            }
            if (runtime->home.launch_target >= 1.0f &&
                runtime->home.launched_app_id == HOME_APP_LIBRARY &&
                runtime->home.active_screen == ACTIVE_SCREEN_HOME &&
                !runtime->library_hub_reset_on_dive) {
                runtime->library_hub_reset_on_dive = true;
                library_controller_reset_to_hub(&runtime->library, true);
            } else if (runtime->home.launch_target < 0.5f &&
                       runtime->home.active_screen == ACTIVE_SCREEN_HOME) {
                runtime->library_hub_reset_on_dive = false;
            }
            home_controller_step(&runtime->home, delta_seconds);
            if (runtime->home.active_screen == ACTIVE_SCREEN_NOW_PLAYING_SHELL &&
                !runtime->now_playing_entered_from_home) {
                runtime->now_playing_entered_from_home = true;
                now_playing_controller_enter(&runtime->now_playing, NP_RETURN_HOME,
                                             NP_OPEN_FROM_HOME, true,
                                             runtime->home.vinyl_rotation_radians,
                                             runtime->volume_level);
                if (runtime->playback.has_active_track) {
                    now_playing_media_request_active(&runtime->now_playing_media,
                                                     runtime->playback.active_track.album_id,
                                                     runtime->playback.active_track.album_uid,
                                                     runtime->playback.active_track.path);
                }
            }
            uint32_t cover_ms = 0u;
            if (runtime->home.active_screen == ACTIVE_SCREEN_NOW_PLAYING_SHELL) {
                struct playback_engine_effects effects;
                bool volume_changed = false;
                enum now_playing_action_kind np_action = NP_ACTION_NONE;
                now_playing_controller_step(&runtime->now_playing, delta_seconds, &effects,
                                            &volume_changed, &np_action);
                if (volume_changed) {
                    if (runtime->now_playing.volume_level > runtime->max_volume) {
                        runtime->now_playing.volume_level = runtime->max_volume;
                    }
                    runtime->volume_level = runtime->now_playing.volume_level;
                    sync_home_telemetry(runtime);
                    mark_playback_persistence_dirty(runtime, now_ns);
                    char command[IPC_COMMAND_BUFFER_BYTES];
                    size_t length =
                        protocol_format_set_volume(runtime->volume_level, command, sizeof(command));
                    if (length > 0u) {
                        (void)runtime->platform->ipc_send_player(runtime->platform->context,
                                                                 command, length);
                    }
                }
                execute_playback_effects(runtime, &effects, now_ns);
                if (np_action != NP_ACTION_NONE) {
                    handle_now_playing_action(runtime, np_action);
                }
                runtime->home.vinyl_rotation_radians = runtime->now_playing.vinyl_rotation_radians;
                runtime->library.vinyl_rotation_radians = runtime->now_playing.vinyl_rotation_radians;
            } else if (runtime->home.active_screen == ACTIVE_SCREEN_LIBRARY_SHELL) {
                library_controller_step(&runtime->library, delta_seconds);
                int64_t deleted_playlist_id =
                    library_controller_take_deleted_playlist_id(&runtime->library);
                if (deleted_playlist_id > 0) {
                    struct playback_engine_effects effects;
                    playback_engine_on_playlist_mutated(&runtime->playback, deleted_playlist_id,
                                                        true, &effects);
                    execute_playback_effects(runtime, &effects, now_ns);
                }
                runtime->home.vinyl_rotation_radians = runtime->library.vinyl_rotation_radians;
                uint64_t cov_t0 = runtime->platform->monotonic_ns(runtime->platform->context);
                (void)library_cover_cache_step(&runtime->library_covers,
                                               !library_controller_is_settling(&runtime->library));
                cover_ms = (uint32_t)elapsed_ms_since(
                    runtime->platform->monotonic_ns(runtime->platform->context), cov_t0);
                if (cover_ms > runtime->perf_window_max_cover_ms) {
                    runtime->perf_window_max_cover_ms = cover_ms;
                }
            } else {
                runtime->library.vinyl_rotation_radians = runtime->home.vinyl_rotation_radians;
            }
            uint64_t render_t0 = runtime->platform->monotonic_ns(runtime->platform->context);
            render_home_screen(runtime);
            uint64_t tick_end_ns = runtime->platform->monotonic_ns(runtime->platform->context);
            uint32_t render_ms = (uint32_t)elapsed_ms_since(tick_end_ns, render_t0);
            uint32_t tick_ms = (uint32_t)elapsed_ms_since(tick_end_ns, tick_start_ns);
            if (tick_ms > runtime->perf_window_max_tick_ms) {
                runtime->perf_window_max_tick_ms = tick_ms;
            }

            int current_view_kind = -1;
            if (runtime->home.active_screen == ACTIVE_SCREEN_LIBRARY_SHELL) {
                const struct library_view_frame *current_view =
                    library_controller_current_view(&runtime->library);
                if (current_view != NULL) {
                    current_view_kind = (int)current_view->kind;
                }
            }
            if (tick_ms > 25u &&
                elapsed_ms_since(tick_end_ns, runtime->last_slow_frame_log_ns) >= 1000ull) {
                runtime->last_slow_frame_log_ns = tick_end_ns;
                logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, tick_end_ns), "PERF",
                                "slow tick_ms=%u render_ms=%u cover_ms=%u scan_ms=%u screen=%d "
                                "view=%d",
                                (unsigned)tick_ms, (unsigned)render_ms, (unsigned)cover_ms,
                                (unsigned)scan_ms, (int)runtime->home.active_screen,
                                current_view_kind);
            }

            if (!runtime->home_entry_logged && runtime->home.entry_progress >= 1.0f) {
                runtime->home_entry_logged = true;
                logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "HOME",
                                "entry_complete frames=%u max_ms=%u",
                                (unsigned)runtime->home_frames,
                                (unsigned)runtime->home_max_frame_ms);
                flush_runtime_logs(runtime, false);
            }

            if (runtime->home.active_screen != runtime->last_logged_home_screen ||
                runtime->home.launch_target != runtime->last_logged_launch_target) {
                logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "HOME",
                                "screen %d -> %d app=%d launch=%.0f",
                                (int)runtime->last_logged_home_screen,
                                (int)runtime->home.active_screen,
                                (int)runtime->home.launched_app_id,
                                (double)runtime->home.launch_target);
                runtime->last_logged_home_screen = runtime->home.active_screen;
                runtime->last_logged_launch_target = runtime->home.launch_target;
            }

            uint64_t perf_elapsed_ms = elapsed_ms_since(now_ns, runtime->perf_window_start_ns);
            if (perf_elapsed_ms >= HOME_PERF_LOG_INTERVAL_MS) {
                uint32_t fps_x10 =
                    perf_elapsed_ms > 0ull
                        ? (uint32_t)(((uint64_t)runtime->perf_window_frames * 10000ull) /
                                     perf_elapsed_ms)
                        : 0u;
                uint32_t avg_ms = runtime->perf_window_frames > 0u
                                      ? (runtime->perf_window_total_frame_ms /
                                         runtime->perf_window_frames)
                                      : 0u;
                uint32_t cov_ram_hits = 0u;
                uint32_t cov_disk_hits = 0u;
                uint32_t cov_decodes = 0u;
                uint32_t cov_max_op_ms = 0u;
                library_cover_cache_take_window_metrics(&runtime->library_covers, &cov_ram_hits,
                                                        &cov_disk_hits, &cov_decodes,
                                                        &cov_max_op_ms);
                uint32_t cov_max_ms =
                    runtime->perf_window_max_cover_ms > cov_max_op_ms
                        ? runtime->perf_window_max_cover_ms
                        : cov_max_op_ms;
                logger_log_line(&runtime->logger, runtime_uptime_ms(runtime, now_ns), "PERF",
                                "home frames=%u fps_x10=%u avg_ms=%u max_ms=%u tick_max_ms=%u "
                                "slow_frames=%u screen=%d view=%d cov_ram=%u cov_disk=%u "
                                "cov_dec=%u cov_max_ms=%u bat=%u vol=%u",
                                (unsigned)runtime->perf_window_frames, (unsigned)fps_x10,
                                (unsigned)avg_ms, (unsigned)runtime->perf_window_max_frame_ms,
                                (unsigned)runtime->perf_window_max_tick_ms,
                                (unsigned)runtime->perf_window_slow_frames,
                                (int)runtime->home.active_screen, current_view_kind,
                                (unsigned)cov_ram_hits, (unsigned)cov_disk_hits,
                                (unsigned)cov_decodes, (unsigned)cov_max_ms,
                                (unsigned)runtime->hardware.battery_percent,
                                (unsigned)runtime->home.telemetry.volume_percent);
                runtime->perf_window_start_ns = now_ns;
                runtime->perf_window_frames = 0u;
                runtime->perf_window_total_frame_ms = 0u;
                runtime->perf_window_max_frame_ms = 0u;
                runtime->perf_window_max_tick_ms = 0u;
                runtime->perf_window_max_cover_ms = 0u;
                runtime->perf_window_slow_frames = 0u;
                flush_runtime_logs(runtime, false);
            }

            runtime->next_frame_ns += BOOT_FRAME_PERIOD_NS;
            if (runtime->next_frame_ns <= now_ns) {
                runtime->next_frame_ns = now_ns + BOOT_FRAME_PERIOD_NS;
            }
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

    if (runtime->state == UI_STATE_CRASH_SCREEN || runtime->state == UI_STATE_SHUTTING_DOWN) {
        return -1;
    }

    uint64_t next_deadline = 0ull;

    if (runtime->state == UI_STATE_BOOT_ANIMATION || runtime->state == UI_STATE_WARNING) {
        next_deadline = runtime->next_frame_ns;
        if (runtime->state == UI_STATE_BOOT_ANIMATION && !runtime->aa24_received) {
            uint64_t warning_deadline =
                runtime->startup_ns + (uint64_t)AA24_WARNING_TIMEOUT_MS * NANOSECONDS_PER_MILLISECOND;
            next_deadline = min_deadline_ns(next_deadline, warning_deadline);
        }
        if (runtime->pending_disable_idle_poweroff) {
            next_deadline = min_deadline_ns(next_deadline, runtime->next_ipc_retry_ns);
        }
    } else if (runtime->state == UI_STATE_HOME || runtime->state == UI_STATE_LOCKED) {
        if (runtime->pending_disable_idle_poweroff) {
            next_deadline = min_deadline_ns(next_deadline, runtime->next_ipc_retry_ns);
        }
        if (!runtime->crash_count_reset_done && runtime->initial_crash_count > 0u) {
            uint64_t reset_deadline =
                runtime->ready_entered_ns +
                (uint64_t)READY_STABILITY_WINDOW_MS * NANOSECONDS_PER_MILLISECOND;
            next_deadline = min_deadline_ns(next_deadline, reset_deadline);
        }
        if (runtime->library_db_open_ok &&
            (runtime->persistence_dirty || runtime->playback.persistence_dirty) &&
            runtime->persistence_dirty_since_ns > 0ull) {
            uint64_t persist_deadline =
                runtime->persistence_dirty_since_ns + 1500ull * NANOSECONDS_PER_MILLISECOND;
            next_deadline = min_deadline_ns(next_deadline, persist_deadline);
        }
        if (runtime->state == UI_STATE_HOME) {
            next_deadline = min_deadline_ns(next_deadline, runtime->next_frame_ns);
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
