#include "test_runner.h"

#include "app.h"
#include "constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum call_event_kind {
    CALL_NONE = 0,
    CALL_ACQUIRE_SURFACE,
    CALL_PRESENT_SURFACE,
    CALL_SET_BRIGHTNESS,
    CALL_ENTER_SLEEP,
    CALL_BEGIN_WAKE,
    CALL_FINISH_WAKE,
    CALL_IPC_SEND,
    CALL_PROBE_HARDWARE,
    CALL_CRASH_COUNT_WRITE,
    CALL_TELEMETRY_WRITE,
    CALL_EXECUTE_SHUTDOWN
};

struct fake_platform_context {
    uint64_t now_ns;
    uint32_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    uint32_t present_count;
    uint8_t last_brightness;
    bool is_sleeping;
    bool ipc_fail_next;
    uint32_t ipc_fail_remaining;
    char ipc_messages[16][256];
    uint32_t ipc_count;
    struct hardware_status hardware;
    uint32_t crash_count_stored;
    uint32_t crash_count_writes;
    struct telemetry_snapshot last_telemetry;
    uint32_t telemetry_writes;
    bool shutdown_called;
    bool shutdown_screen_was_locked;
    struct telemetry_snapshot shutdown_telemetry;
    int32_t crash_state_mirror;
    bool has_saved_appearance;
    enum appearance_mode saved_appearance;
    uint32_t appearance_writes;
    char log_output[32768];
    size_t log_output_length;
    uint32_t log_flush_count;
    uint32_t log_sync_count;
    enum call_event_kind trace[64];
    uint32_t trace_count;
};

static void record_call(struct fake_platform_context *fake, enum call_event_kind event_kind) {
    if (fake->trace_count < 64u) {
        fake->trace[fake->trace_count++] = event_kind;
    }
}

static uint64_t fake_monotonic_ns(void *context) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    return fake->now_ns;
}

static bool fake_acquire_surface(void *context, struct framebuffer_surface *surface_out) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_ACQUIRE_SURFACE);
    surface_out->pixels = fake->pixels;
    surface_out->stride_pixels = SCREEN_WIDTH;
    return true;
}

static void fake_present_surface(void *context) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_PRESENT_SURFACE);
    fake->present_count++;
}

static void fake_set_brightness(void *context, uint8_t brightness) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_SET_BRIGHTNESS);
    fake->last_brightness = brightness;
}

static void fake_enter_sleep(void *context) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_ENTER_SLEEP);
    fake->is_sleeping = true;
}

static void fake_begin_wake(void *context) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_BEGIN_WAKE);
    fake->is_sleeping = false;
}

static void fake_finish_wake(void *context, uint8_t restore_brightness) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_FINISH_WAKE);
    fake->last_brightness = restore_brightness;
}

static bool fake_ipc_send_player(void *context, const char *message, size_t length) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_IPC_SEND);
    if (fake->ipc_fail_remaining > 0u) {
        fake->ipc_fail_remaining--;
        return false;
    }
    if (fake->ipc_count < 16u && length < 256u) {
        memcpy(fake->ipc_messages[fake->ipc_count], message, length);
        fake->ipc_messages[fake->ipc_count][length] = '\0';
        fake->ipc_count++;
    }
    return true;
}

static void fake_probe_hardware(void *context, struct hardware_status *status_out) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_PROBE_HARDWARE);
    *status_out = fake->hardware;
}

static uint32_t fake_crash_count_read(void *context) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    return fake->crash_count_stored;
}

static void fake_crash_count_write(void *context, uint32_t count) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_CRASH_COUNT_WRITE);
    fake->crash_count_stored = count;
    fake->crash_count_writes++;
}

static void fake_telemetry_write(void *context, const struct telemetry_snapshot *snapshot) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_TELEMETRY_WRITE);
    fake->last_telemetry = *snapshot;
    fake->telemetry_writes++;
}

static void fake_execute_shutdown(void *context, bool screen_was_locked,
                                  const struct telemetry_snapshot *final_snapshot) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    record_call(fake, CALL_EXECUTE_SHUTDOWN);
    fake->shutdown_called = true;
    fake->shutdown_screen_was_locked = screen_was_locked;
    fake->shutdown_telemetry = *final_snapshot;
}

static void fake_update_crash_state_mirror(void *context, int32_t ui_state) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    fake->crash_state_mirror = ui_state;
}

static bool fake_appearance_mode_read(void *context, enum appearance_mode *mode_out) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    if (!fake->has_saved_appearance || mode_out == NULL) {
        return false;
    }
    *mode_out = fake->saved_appearance;
    return true;
}

static void fake_appearance_mode_write(void *context, enum appearance_mode mode) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    fake->has_saved_appearance = true;
    fake->saved_appearance = mode;
    fake->appearance_writes++;
}

static bool fake_log_append(void *context, const char *text, size_t length, bool sync_to_disk) {
    struct fake_platform_context *fake = (struct fake_platform_context *)context;
    fake->log_flush_count++;
    if (sync_to_disk) {
        fake->log_sync_count++;
    }
    if (fake->log_output_length + length < sizeof(fake->log_output)) {
        memcpy(fake->log_output + fake->log_output_length, text, length);
        fake->log_output_length += length;
        fake->log_output[fake->log_output_length] = '\0';
    }
    return true;
}

static void init_fake_platform(struct fake_platform_context *fake,
                               struct platform_operations *operations) {
    memset(fake, 0, sizeof(*fake));
    fake->now_ns = 1000000000ull;
    fake->hardware.battery_valid = true;
    fake->hardware.battery_percent = 85u;
    fake->hardware.sd_mounted = true;

    operations->context = fake;
    operations->monotonic_ns = fake_monotonic_ns;
    operations->display_acquire_surface = fake_acquire_surface;
    operations->display_present_surface = fake_present_surface;
    operations->display_set_brightness = fake_set_brightness;
    operations->display_enter_sleep = fake_enter_sleep;
    operations->display_begin_wake = fake_begin_wake;
    operations->display_finish_wake = fake_finish_wake;
    operations->ipc_send_player = fake_ipc_send_player;
    operations->probe_hardware_status = fake_probe_hardware;
    operations->crash_count_read = fake_crash_count_read;
    operations->crash_count_write = fake_crash_count_write;
    operations->telemetry_write = fake_telemetry_write;
    operations->execute_shutdown = fake_execute_shutdown;
    operations->update_crash_state_mirror = fake_update_crash_state_mirror;
    operations->appearance_mode_read = fake_appearance_mode_read;
    operations->appearance_mode_write = fake_appearance_mode_write;
    operations->log_append = fake_log_append;
}

static struct inbound_ipc_message make_aa24_message(uint8_t volume, uint8_t max_volume,
                                                    uint8_t brightness) {
    struct inbound_ipc_message message;
    memset(&message, 0, sizeof(message));
    message.kind = IPC_INBOUND_BOOT_CONFIG;
    message.boot_config.volume_level = volume;
    message.boot_config.max_volume = max_volume;
    message.boot_config.brightness = brightness;
    message.boot_config.has_valid_payload = true;
    return message;
}

static void test_crash_formatting_and_count_parsing(void) {
    struct crash_record record = {11, 0x0u, 0x00401234ull, 0x00401000ull, 4210ull, 0, 2u};
    char buffer[CRASH_LOG_BUFFER_BYTES];
    size_t written = crash_format_record(&record, buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(written > 0u);
    TEST_ASSERT_EQ_STR(
        "signal=11\nsi_addr=0x00000000\npc=0x00401234\nra=0x00401000\n"
        "uptime_ms=4210\nui_state=0\ncrash_count=2\n",
        buffer);

    /* Truncation check */
    char tiny_buffer[16];
    size_t truncated = crash_format_record(&record, tiny_buffer, sizeof(tiny_buffer));
    TEST_ASSERT_EQ_U32(15u, (uint32_t)truncated);
    TEST_ASSERT_EQ_U32('\0', (uint32_t)tiny_buffer[15]);

    /* Count parsing */
    TEST_ASSERT_EQ_U32(0u, crash_parse_count("", 0u));
    TEST_ASSERT_EQ_U32(0u, crash_parse_count("0\n", 2u));
    TEST_ASSERT_EQ_U32(3u, crash_parse_count("3\r\n", 3u));
    TEST_ASSERT_EQ_U32(0u, crash_parse_count("abc", 3u));
    TEST_ASSERT_EQ_U32(UINT32_MAX, crash_parse_count("99999999999999", 14u));

    /* Threshold check */
    TEST_ASSERT_FALSE(crash_should_enter_screen(0u));
    TEST_ASSERT_FALSE(crash_should_enter_screen(2u));
    TEST_ASSERT_TRUE(crash_should_enter_screen(3u));
    TEST_ASSERT_TRUE(crash_should_enter_screen(5u));
}

static void test_crash_atomic_files_and_readonly_fallback(void) {
    char temp_template[] = "/tmp/dialos_test_XXXXXX";
    char *temp_directory = mkdtemp(temp_template);
    TEST_ASSERT_TRUE(temp_directory != NULL);

    char count_path[256];
    char count_temp[256];
    char log_dir[256];
    char log_temp[256];
    char log_final[256];
    snprintf(count_path, sizeof(count_path), "%s/crash_count", temp_directory);
    snprintf(count_temp, sizeof(count_temp), "%s/.crash_count.tmp", temp_directory);
    snprintf(log_dir, sizeof(log_dir), "%s/.dial-os", temp_directory);
    snprintf(log_temp, sizeof(log_temp), "%s/.dial-os/.crash.log.tmp", temp_directory);
    snprintf(log_final, sizeof(log_final), "%s/.dial-os/crash.log", temp_directory);

    TEST_ASSERT_EQ_U32(1u, crash_increment_count_file(count_path, count_temp));
    TEST_ASSERT_EQ_U32(2u, crash_increment_count_file(count_path, count_temp));
    TEST_ASSERT_EQ_U32(3u, crash_increment_count_file(count_path, count_temp));

    struct crash_record record = {11, 0x10ull, 0x20ull, 0x30ull, 500ull, 2, 3u};
    /* Unmounted SD skips cleanly */
    TEST_ASSERT_FALSE(crash_write_log_atomic(&record, false, log_dir, log_temp, log_final));
    /* Mounted SD writes atomically */
    TEST_ASSERT_TRUE(crash_write_log_atomic(&record, true, log_dir, log_temp, log_final));

    FILE *log_file = fopen(log_final, "r");
    TEST_ASSERT_TRUE(log_file != NULL);
    char contents[256];
    size_t read_bytes = fread(contents, 1u, sizeof(contents) - 1u, log_file);
    contents[read_bytes] = '\0';
    fclose(log_file);
    TEST_ASSERT_TRUE(strstr(contents, "crash_count=3\n") != NULL);

    /* Read-only directory triggers graceful failure without crashing */
    TEST_ASSERT_EQ_I32(0, chmod(log_dir, 0555));
    TEST_ASSERT_FALSE(crash_write_log_atomic(&record, true, log_dir, log_temp, log_final));
    chmod(log_dir, 0755);

    unlink(log_final);
    rmdir(log_dir);
    unlink(count_path);
    rmdir(temp_directory);
}

static void advance_runtime_ticks(struct app_runtime *runtime, struct fake_platform_context *fake,
                                  uint64_t duration_ms) {
    uint64_t end_ns = fake->now_ns + duration_ms * NANOSECONDS_PER_MILLISECOND;
    while (fake->now_ns < end_ns) {
        fake->now_ns += BOOT_FRAME_PERIOD_NS;
        if (fake->now_ns > end_ns) {
            fake->now_ns = end_ns;
        }
        app_handle_tick(runtime, fake->now_ns);
    }
}

static void test_nominal_boot_aa24_retry_and_a607(void) {
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);

    TEST_ASSERT_EQ_I32(UI_STATE_BOOT_ANIMATION, runtime.state);
    TEST_ASSERT_EQ_U32(1u, fake.present_count);
    TEST_ASSERT_EQ_U32(BRIGHTNESS_BOOT_DEFAULT, fake.last_brightness);
    TEST_ASSERT_EQ_U32(1u, runtime.animation_frames);

    /* Early tick does not draw extra frames */
    fake.now_ns += 5000000ull;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_EQ_U32(1u, runtime.animation_frames);

    /* Paced frame advances once deadline passes */
    fake.now_ns += BOOT_FRAME_PERIOD_NS;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_EQ_U32(2u, runtime.animation_frames);

    /* aa24 applies brightness & queues 0664 immediately, while boot choreography continues */
    fake.ipc_fail_remaining = 1u;
    fake.now_ns += 100000000ull;
    struct inbound_ipc_message aa24 = make_aa24_message(48u, 85u, 22u);
    app_handle_ipc_message(&runtime, &aa24);

    TEST_ASSERT_EQ_I32(UI_STATE_BOOT_ANIMATION, runtime.state);
    TEST_ASSERT_EQ_U32(22u, fake.last_brightness);
    TEST_ASSERT_FALSE(runtime.idle_poweroff_disabled);
    TEST_ASSERT_TRUE(runtime.pending_disable_idle_poweroff);
    TEST_ASSERT_EQ_U32(0u, fake.telemetry_writes);

    /* Retry at +500 ms succeeds while still in boot animation */
    advance_runtime_ticks(&runtime, &fake, IPC_RETRY_INTERVAL_MS);
    TEST_ASSERT_TRUE(runtime.idle_poweroff_disabled);
    TEST_ASSERT_FALSE(runtime.pending_disable_idle_poweroff);
    TEST_ASSERT_EQ_U32(1u, fake.ipc_count);
    TEST_ASSERT_EQ_STR("0664000C0000", fake.ipc_messages[0]);

    /* Complete remaining boot acts (~9.75s total) -> enters READY and writes telemetry */
    advance_runtime_ticks(&runtime, &fake, 9300u);
    TEST_ASSERT_EQ_I32(UI_STATE_READY, runtime.state);
    TEST_ASSERT_EQ_U32(1u, fake.telemetry_writes);
    TEST_ASSERT_TRUE(fake.last_telemetry.idle_poweroff_disabled);

    /* a607 triggers one-time re-send of 0664000C0000 */
    struct inbound_ipc_message a607 = {0};
    a607.kind = IPC_INBOUND_INPUT_MODE_READY;
    app_handle_ipc_message(&runtime, &a607);
    TEST_ASSERT_EQ_U32(2u, fake.ipc_count);
    TEST_ASSERT_EQ_STR("0664000C0000", fake.ipc_messages[1]);

    /* Second a607 does not spam 0664 */
    app_handle_ipc_message(&runtime, &a607);
    TEST_ASSERT_EQ_U32(2u, fake.ipc_count);
    app_destroy(&runtime);
}

static void test_warning_timeout_and_late_recovery(void) {
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);

    /* Just before 12000 ms stays in BOOT_ANIMATION */
    advance_runtime_ticks(&runtime, &fake, AA24_WARNING_TIMEOUT_MS - 100u);
    TEST_ASSERT_EQ_I32(UI_STATE_BOOT_ANIMATION, runtime.state);

    /* At 12000 ms enters WARNING with finite poll timeout for 60 fps animation */
    fake.now_ns = runtime.startup_ns + AA24_WARNING_TIMEOUT_MS * NANOSECONDS_PER_MILLISECOND;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_EQ_I32(UI_STATE_WARNING, runtime.state);
    int32_t warning_poll_ms = app_compute_poll_timeout_ms(&runtime, fake.now_ns);
    TEST_ASSERT_TRUE(warning_poll_ms > 0 && warning_poll_ms <= 17);

    /* Late aa24 recovers into BOOT_ANIMATION (Act 02) and finishes into READY */
    fake.now_ns += 3000ull * NANOSECONDS_PER_MILLISECOND;
    struct inbound_ipc_message aa24 = make_aa24_message(30u, 100u, 150u);
    app_handle_ipc_message(&runtime, &aa24);
    TEST_ASSERT_EQ_I32(UI_STATE_BOOT_ANIMATION, runtime.state);
    TEST_ASSERT_TRUE(runtime.idle_poweroff_disabled);

    advance_runtime_ticks(&runtime, &fake, 2200u);
    TEST_ASSERT_EQ_I32(UI_STATE_READY, runtime.state);
    app_destroy(&runtime);
}

static void test_aa22_delayed_sd_probes_and_stability_reset(void) {
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);
    fake.crash_count_stored = 2u;
    fake.hardware.sd_mounted = false;

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);
    TEST_ASSERT_EQ_I32(UI_STATE_BOOT_ANIMATION, runtime.state);

    struct inbound_ipc_message aa24 = make_aa24_message(20u, 80u, 100u);
    app_handle_ipc_message(&runtime, &aa24);
    advance_runtime_ticks(&runtime, &fake, 9850u);
    TEST_ASSERT_EQ_I32(UI_STATE_READY, runtime.state);
    uint32_t presents_at_ready = fake.present_count;

    /* aa22 triggers immediate probe plus +500 ms and +1500 ms follow-ups */
    struct inbound_ipc_message aa22 = {0};
    aa22.kind = IPC_INBOUND_SDCARD_EVENT;
    aa22.sdcard_inserted = true;
    app_handle_ipc_message(&runtime, &aa22);
    TEST_ASSERT_EQ_U32(presents_at_ready, fake.present_count);

    /* Simulate mount completing just before +500 ms probe */
    fake.hardware.sd_mounted = true;
    fake.now_ns += SD_MOUNT_FOLLOWUP_FIRST_MS * NANOSECONDS_PER_MILLISECOND;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_TRUE(runtime.hardware.sd_mounted);
    TEST_ASSERT_TRUE(runtime.home.telemetry.sd_mounted);
    TEST_ASSERT_FALSE(runtime.sd_followup_first_pending);

    /* +1500 ms probe clears second pending follow-up */
    fake.now_ns += 1000ull * NANOSECONDS_PER_MILLISECOND;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_FALSE(runtime.sd_followup_second_pending);

    /* Stability reset at 9999 ms has not reset yet; at 10000 ms resets to 0 */
    fake.now_ns = runtime.ready_entered_ns + (READY_STABILITY_WINDOW_MS - 1u) * NANOSECONDS_PER_MILLISECOND;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_EQ_U32(0u, fake.crash_count_writes);

    fake.now_ns = runtime.ready_entered_ns + READY_STABILITY_WINDOW_MS * NANOSECONDS_PER_MILLISECOND;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_EQ_U32(1u, fake.crash_count_writes);
    TEST_ASSERT_EQ_U32(0u, fake.crash_count_stored);
    app_destroy(&runtime);
}

static void test_volume_lock_wake_ordering_and_shutdown(void) {
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);

    struct inbound_ipc_message aa24 = make_aa24_message(0u, 2u, 140u);
    app_handle_ipc_message(&runtime, &aa24);
    advance_runtime_ticks(&runtime, &fake, 9850u);
    TEST_ASSERT_EQ_I32(UI_STATE_READY, runtime.state);

    /* Clamp at 0 on Vol- and max_volume (2) on Vol+ */
    app_handle_button_action(&runtime, BUTTON_ACTION_VOLUME_DOWN);
    TEST_ASSERT_EQ_U32(0u, runtime.volume_level);
    app_handle_button_action(&runtime, BUTTON_ACTION_VOLUME_UP);
    app_handle_button_action(&runtime, BUTTON_ACTION_VOLUME_UP);
    app_handle_button_action(&runtime, BUTTON_ACTION_VOLUME_UP);
    TEST_ASSERT_EQ_U32(2u, runtime.volume_level);

    /* Lock gates volume taps */
    app_handle_button_action(&runtime, BUTTON_ACTION_POWER_SHORT);
    TEST_ASSERT_EQ_I32(UI_STATE_LOCKED, runtime.state);
    TEST_ASSERT_TRUE(fake.is_sleeping);
    app_handle_button_action(&runtime, BUTTON_ACTION_VOLUME_DOWN);
    TEST_ASSERT_EQ_U32(2u, runtime.volume_level);

    /* Wake must call begin_wake -> probe -> acquire -> present -> finish_wake */
    fake.trace_count = 0u;
    app_handle_button_action(&runtime, BUTTON_ACTION_POWER_SHORT);
    TEST_ASSERT_EQ_I32(UI_STATE_READY, runtime.state);
    TEST_ASSERT_EQ_U32(5u, fake.trace_count);
    TEST_ASSERT_EQ_I32(CALL_BEGIN_WAKE, fake.trace[0]);
    TEST_ASSERT_EQ_I32(CALL_PROBE_HARDWARE, fake.trace[1]);
    TEST_ASSERT_EQ_I32(CALL_ACQUIRE_SURFACE, fake.trace[2]);
    TEST_ASSERT_EQ_I32(CALL_PRESENT_SURFACE, fake.trace[3]);
    TEST_ASSERT_EQ_I32(CALL_FINISH_WAKE, fake.trace[4]);
    TEST_ASSERT_EQ_U32(140u, fake.last_brightness);

    /* Power long-press while LOCKED must NOT draw shutdown frame */
    app_handle_button_action(&runtime, BUTTON_ACTION_POWER_SHORT);
    TEST_ASSERT_EQ_I32(UI_STATE_LOCKED, runtime.state);
    uint32_t presents_before_shutdown = fake.present_count;
    fake.now_ns += 5000ull * NANOSECONDS_PER_MILLISECOND;
    app_handle_button_action(&runtime, BUTTON_ACTION_POWER_LONG);
    TEST_ASSERT_TRUE(app_is_shutting_down(&runtime));
    TEST_ASSERT_TRUE(fake.shutdown_called);
    TEST_ASSERT_TRUE(fake.shutdown_screen_was_locked);
    TEST_ASSERT_EQ_U32(presents_before_shutdown, fake.present_count);
    TEST_ASSERT_EQ_STR("0715000C0000", fake.ipc_messages[fake.ipc_count - 1u]);
    app_destroy(&runtime);
}

static void test_crash_screen_gate_and_ipc_ignoring(void) {
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);
    fake.crash_count_stored = CRASH_LOOP_THRESHOLD;

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);

    TEST_ASSERT_EQ_I32(UI_STATE_CRASH_SCREEN, runtime.state);
    TEST_ASSERT_EQ_I32(-1, app_compute_poll_timeout_ms(&runtime, fake.now_ns));
    TEST_ASSERT_EQ_U32(1u, fake.ipc_count);
    TEST_ASSERT_EQ_STR("0664000C0000", fake.ipc_messages[0]);

    /* Inbound aa24 is ignored in CRASH_SCREEN */
    struct inbound_ipc_message aa24 = make_aa24_message(30u, 100u, 150u);
    app_handle_ipc_message(&runtime, &aa24);
    TEST_ASSERT_EQ_I32(UI_STATE_CRASH_SCREEN, runtime.state);

    /* Power long-press draws shutdown frame and shuts down */
    uint32_t presents_before = fake.present_count;
    app_handle_button_action(&runtime, BUTTON_ACTION_POWER_LONG);
    TEST_ASSERT_TRUE(fake.shutdown_called);
    TEST_ASSERT_FALSE(fake.shutdown_screen_was_locked);
    TEST_ASSERT_EQ_U32(presents_before + 1u, fake.present_count);
    app_destroy(&runtime);
}

static void test_home_touch_appearance_persistence_and_live_telemetry(void) {
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);
    fake.has_saved_appearance = true;
    fake.saved_appearance = APPEARANCE_MODE_LIGHT;
    fake.hardware.sd_free_valid = true;
    fake.hardware.sd_free_megabytes = 43622ull;

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);
    TEST_ASSERT_EQ_I32(APPEARANCE_MODE_LIGHT, runtime.appearance_mode);

    /* Finish boot into UI_STATE_HOME */
    struct inbound_ipc_message aa24 = make_aa24_message(60u, 102u, 180u);
    app_handle_ipc_message(&runtime, &aa24);
    advance_runtime_ticks(&runtime, &fake, 9850u);
    TEST_ASSERT_EQ_I32(UI_STATE_HOME, runtime.state);
    TEST_ASSERT_EQ_U32(50u, runtime.home.telemetry.volume_percent);
    TEST_ASSERT_EQ_U32(85u, runtime.home.telemetry.volume_limit_percent);

    /* Advance 60 FPS frames on Home */
    uint32_t presents_before = fake.present_count;
    advance_runtime_ticks(&runtime, &fake, 850u);
    TEST_ASSERT_TRUE(fake.present_count > presents_before + 40u);
    TEST_ASSERT_TRUE(runtime.home_frames > 40u);

    /* Live IPC playback, elapsed, volume, and stop updates */
    const char *play_json =
        "{\"song\":\"{\\\"song_name\\\":\\\"Test Track\\\",\\\"song_duration_time\\\":180000}\",\"state\":0}";
    char play_frame[256];
    size_t play_total = 8u + strlen(play_json);
    snprintf(play_frame, sizeof(play_frame), "a202%04X%s", (unsigned)play_total, play_json);
    struct inbound_ipc_message play_msg;
    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc(play_frame, play_total, &play_msg));
    app_handle_ipc_message(&runtime, &play_msg);
    TEST_ASSERT_EQ_I32(HOME_PLAYBACK_PLAYING, runtime.home.telemetry.playback_mode);
    TEST_ASSERT_EQ_STR("Test Track", runtime.home.telemetry.track_title);
    TEST_ASSERT_EQ_U32(180000u, runtime.home.telemetry.duration_ms);

    struct inbound_ipc_message elapsed_msg;
    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a103001000004A38", 16u, &elapsed_msg));
    app_handle_ipc_message(&runtime, &elapsed_msg);
    TEST_ASSERT_EQ_U32(19000u, runtime.home.telemetry.elapsed_ms);

    struct inbound_ipc_message vol_msg;
    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a714000C0078", 12u, &vol_msg));
    app_handle_ipc_message(&runtime, &vol_msg);
    TEST_ASSERT_EQ_U32(102u, runtime.volume_level);
    TEST_ASSERT_EQ_U32(85u, runtime.home.telemetry.volume_percent);

    /* Drag to Settings (slot 3), tap to open inline preview, tap row 0 to toggle Light -> Dark */
    struct touch_event drag_down = {TOUCH_EVENT_DOWN, 120, 150, 10000ull};
    struct touch_event drag_move = {TOUCH_EVENT_MOVE, 232, 150, 10026ull};
    struct touch_event drag_up = {TOUCH_EVENT_UP, 232, 150, 10052ull};
    app_handle_touch_event(&runtime, &drag_down);
    app_handle_touch_event(&runtime, &drag_move);
    app_handle_touch_event(&runtime, &drag_up);
    advance_runtime_ticks(&runtime, &fake, 600u);
    TEST_ASSERT_EQ_I32(HOME_APP_SETTINGS, home_wrap_app_slot(runtime.home.focused_slot));

    struct touch_event tap_hero_down = {TOUCH_EVENT_DOWN, 180, 148, 10700ull};
    struct touch_event tap_hero_up = {TOUCH_EVENT_UP, 180, 148, 10730ull};
    app_handle_touch_event(&runtime, &tap_hero_down);
    app_handle_touch_event(&runtime, &tap_hero_up);
    advance_runtime_ticks(&runtime, &fake, 650u);

    struct touch_event row0_down = {TOUCH_EVENT_DOWN, 180, 182, 11400ull};
    struct touch_event row0_up = {TOUCH_EVENT_UP, 180, 182, 11430ull};
    app_handle_touch_event(&runtime, &row0_down);
    app_handle_touch_event(&runtime, &row0_up);
    TEST_ASSERT_EQ_I32(APPEARANCE_MODE_DARK, runtime.appearance_mode);
    TEST_ASSERT_EQ_I32(APPEARANCE_MODE_DARK, fake.saved_appearance);
    TEST_ASSERT_EQ_U32(1u, fake.appearance_writes);

    /* Power long-press logs Home FPS telemetry */
    app_handle_button_action(&runtime, BUTTON_ACTION_POWER_LONG);
    TEST_ASSERT_TRUE(fake.shutdown_called);
    TEST_ASSERT_TRUE(fake.shutdown_telemetry.home_frames > 0u);
    TEST_ASSERT_TRUE(fake.shutdown_telemetry.home_fps_x10 > 0u);
    app_destroy(&runtime);
}

static void test_warning_tap_to_retry_recovery(void) {
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);
    advance_runtime_ticks(&runtime, &fake, AA24_WARNING_TIMEOUT_MS + 50u);
    TEST_ASSERT_EQ_I32(UI_STATE_WARNING, runtime.state);

    /* Tap on TAP TO RETRY button (180, 262) returns to BOOT_PHASE_BUFFER */
    struct touch_event retry_down = {TOUCH_EVENT_DOWN, 180, 262, 13000ull};
    struct touch_event retry_up = {TOUCH_EVENT_UP, 180, 262, 13030ull};
    app_handle_touch_event(&runtime, &retry_down);
    app_handle_touch_event(&runtime, &retry_up);
    TEST_ASSERT_EQ_I32(UI_STATE_BOOT_ANIMATION, runtime.state);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, runtime.boot.phase);
    app_destroy(&runtime);
}

static void test_boot_large_stall_delta_does_not_skip_signal_lock_and_handoff(void) {
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);

    /* aa1d/aa1e/aa1f during boot must NOT trigger synchronous hardware probes */
    uint32_t probes_before_events = 0u;
    for (uint32_t index = 0u; index < fake.trace_count; index++) {
        if (fake.trace[index] == CALL_PROBE_HARDWARE) {
            probes_before_events++;
        }
    }
    struct inbound_ipc_message bat_msg = {0};
    bat_msg.kind = IPC_INBOUND_BATTERY_EVENT;
    app_handle_ipc_message(&runtime, &bat_msg);
    struct inbound_ipc_message usb_msg = {0};
    usb_msg.kind = IPC_INBOUND_USB_CABLE_EVENT;
    usb_msg.usb_connected = true;
    app_handle_ipc_message(&runtime, &usb_msg);

    uint32_t probes_after_events = 0u;
    for (uint32_t index = 0u; index < fake.trace_count; index++) {
        if (fake.trace[index] == CALL_PROBE_HARDWARE) {
            probes_after_events++;
        }
    }
    TEST_ASSERT_EQ_U32(probes_before_events, probes_after_events);

    /* Send aa24 and advance to just before Signal Lock (~7.9s) */
    struct inbound_ipc_message aa24 = make_aa24_message(48u, 85u, 180u);
    app_handle_ipc_message(&runtime, &aa24);
    advance_runtime_ticks(&runtime, &fake, 7900u);
    TEST_ASSERT_EQ_I32(UI_STATE_BOOT_ANIMATION, runtime.state);

    /* Inject a 3000 ms OS stall: clamped boot delta must NOT skip Acts 03 & 04 into Home */
    fake.now_ns += 3000ull * NANOSECONDS_PER_MILLISECOND;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_EQ_I32(UI_STATE_BOOT_ANIMATION, runtime.state);
    TEST_ASSERT_TRUE(runtime.boot.phase != BOOT_PHASE_COMPLETED);
    app_destroy(&runtime);
}

static void test_persistent_event_and_performance_logging(void) {
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);

    struct inbound_ipc_message aa24 = make_aa24_message(48u, 85u, 180u);
    app_handle_ipc_message(&runtime, &aa24);
    advance_runtime_ticks(&runtime, &fake, 9850u);
    TEST_ASSERT_EQ_I32(UI_STATE_HOME, runtime.state);

    /* Complete Home entry (0.78s) + 5.0s performance heartbeat window */
    advance_runtime_ticks(&runtime, &fake, 5900u);
    TEST_ASSERT_TRUE(fake.log_flush_count > 0u);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "=== SESSION START ===") != NULL);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "BOOT start") != NULL);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "IPC rx aa24") != NULL);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "BOOT complete -> home") != NULL);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "HOME entry_complete") != NULL);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "PERF home") != NULL);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "tick_max_ms=") != NULL);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "cov_ram=") != NULL);

    /* Power lock and shutdown flush with sync_to_disk = true */
    uint32_t syncs_before_lock = fake.log_sync_count;
    app_handle_button_action(&runtime, BUTTON_ACTION_POWER_SHORT);
    TEST_ASSERT_TRUE(fake.log_sync_count > syncs_before_lock);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "POWER lock") != NULL);

    app_handle_button_action(&runtime, BUTTON_ACTION_POWER_SHORT);
    app_handle_button_action(&runtime, BUTTON_ACTION_POWER_LONG);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "POWER shutdown") != NULL);
    app_destroy(&runtime);
}

static void wait_for_library_scan_ready(struct app_runtime *runtime,
                                        struct fake_platform_context *fake) {
    for (uint32_t spin = 0u;
         spin < 500u && runtime->library_scan_started && !runtime->library_scan_finished_handled;
         spin++) {
        usleep(1000u);
        advance_runtime_ticks(runtime, fake, 20u);
    }
    TEST_ASSERT_TRUE(runtime->library_scan_finished_handled);
}

static void test_library_boot_scan_playback_and_sd_hotswap(void) {
    char base_directory[256];
    char music_directory[300];
    char dialos_directory[300];
    char temp_directory[300];
    snprintf(base_directory, sizeof(base_directory), "/tmp/dialos_app_lib_%d", (int)getpid());
    char remove_command[320];
    snprintf(remove_command, sizeof(remove_command), "rm -rf \"%s\"", base_directory);
    (void)system(remove_command);
    mkdir(base_directory, 0755);
    snprintf(music_directory, sizeof(music_directory), "%s/sdcard", base_directory);
    snprintf(dialos_directory, sizeof(dialos_directory), "%s/sdcard/.dial-os", base_directory);
    snprintf(temp_directory, sizeof(temp_directory), "%s/tmp", base_directory);
    mkdir(music_directory, 0755);
    mkdir(dialos_directory, 0755);
    mkdir(temp_directory, 0755);

    /* Create a minimal WAV file inside music_directory/Artist/Album/ */
    char artist_directory[340], album_directory[380], wav_path[420];
    snprintf(artist_directory, sizeof(artist_directory), "%s/Test Artist", music_directory);
    snprintf(album_directory, sizeof(album_directory), "%s/Test Album", artist_directory);
    snprintf(wav_path, sizeof(wav_path), "%s/01 - Test Song.wav", album_directory);
    mkdir(artist_directory, 0755);
    mkdir(album_directory, 0755);
    FILE *wav_file = fopen(wav_path, "wb");
    TEST_ASSERT_TRUE(wav_file != NULL);
    uint8_t wav_header[44] = {'R','I','F','F',36,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
                              0x44,0xAC,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    fwrite(wav_header, 1u, 44u, wav_file);
    fclose(wav_file);

    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);
    operations.library_music_root = music_directory;
    operations.library_dialos_directory = dialos_directory;
    operations.library_temp_directory = temp_directory;

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);
    struct inbound_ipc_message aa24 = make_aa24_message(48u, 85u, 180u);
    app_handle_ipc_message(&runtime, &aa24);
    wait_for_library_scan_ready(&runtime, &fake);
    advance_runtime_ticks(&runtime, &fake, 10000u);

    TEST_ASSERT_EQ_I32(UI_STATE_HOME, runtime.state);
    TEST_ASSERT_EQ_U32(1u, runtime.home.telemetry.track_count);
    TEST_ASSERT_EQ_U32(1u, runtime.home.telemetry.album_count);

    /* Enter Library via Home carousel (drag to Library slot 1, tap center) */
    struct touch_event drag_down = {TOUCH_EVENT_DOWN, 240, 150, 11000ull};
    struct touch_event drag_move = {TOUCH_EVENT_MOVE, 128, 150, 11030ull};
    struct touch_event drag_up = {TOUCH_EVENT_UP, 128, 150, 11060ull};
    app_handle_touch_event(&runtime, &drag_down);
    app_handle_touch_event(&runtime, &drag_move);
    app_handle_touch_event(&runtime, &drag_up);
    advance_runtime_ticks(&runtime, &fake, 600u);

    struct touch_event tap_down = {TOUCH_EVENT_DOWN, 180, 148, 12000ull};
    struct touch_event tap_up = {TOUCH_EVENT_UP, 180, 148, 12030ull};
    app_handle_touch_event(&runtime, &tap_down);
    app_handle_touch_event(&runtime, &tap_up);
    advance_runtime_ticks(&runtime, &fake, 700u);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_LIBRARY_SHELL, runtime.home.active_screen);

    /* Tap All Songs tile (index 5 -> scroll down 2 or tap after scrolling) */
    runtime.library.view_stack[0].scroll_position = 2.0f;
    runtime.library.view_stack[0].target_index = 2;
    struct touch_event all_songs_down = {TOUCH_EVENT_DOWN, 244, 152, 13000ull};
    struct touch_event all_songs_up = {TOUCH_EVENT_UP, 244, 152, 13030ull};
    app_handle_touch_event(&runtime, &all_songs_down);
    app_handle_touch_event(&runtime, &all_songs_up);
    advance_runtime_ticks(&runtime, &fake, 350u);
    TEST_ASSERT_EQ_I32(LIB_VIEW_TRACKS, library_controller_current_view(&runtime.library)->kind);

    /* Tap track 0 -> sends 0100 M3U IPC and stays inside Library */
    uint32_t ipc_before = fake.ipc_count;
    struct touch_event track_down = {TOUCH_EVENT_DOWN, 180, 126, 14000ull};
    struct touch_event track_up = {TOUCH_EVENT_UP, 180, 126, 14030ull};
    app_handle_touch_event(&runtime, &track_down);
    app_handle_touch_event(&runtime, &track_up);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_LIBRARY_SHELL, runtime.home.active_screen);
    TEST_ASSERT_TRUE(fake.ipc_count > ipc_before);
    TEST_ASSERT_TRUE(strncmp(fake.ipc_messages[fake.ipc_count - 1u], "0100", 4u) == 0);

    /* Tap same active track -> sends 0201 toggle */
    ipc_before = fake.ipc_count;
    app_handle_touch_event(&runtime, &track_down);
    app_handle_touch_event(&runtime, &track_up);
    TEST_ASSERT_EQ_U32(ipc_before + 1u, fake.ipc_count);
    TEST_ASSERT_EQ_STR("0201000C0000", fake.ipc_messages[fake.ipc_count - 1u]);

    /* SD hot-unplug (aa22 inserted=0) clears catalog & zeros counts */
    fake.hardware.sd_mounted = false;
    struct inbound_ipc_message sdcard_removed = {0};
    sdcard_removed.kind = IPC_INBOUND_SDCARD_EVENT;
    sdcard_removed.sdcard_inserted = false;
    app_handle_ipc_message(&runtime, &sdcard_removed);
    TEST_ASSERT_EQ_U32(0u, runtime.home.telemetry.track_count);
    TEST_ASSERT_FALSE(runtime.library_db_open_ok);

    app_destroy(&runtime);
    (void)system(remove_command);
}

static void write_minimal_2x2_png(const char *path) {
    /* Valid 2x2 RGBA/RGB stored-deflate PNG (67 bytes) */
    static const uint8_t PNG_2X2[79] = {
        0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au,
        0x00u, 0x00u, 0x00u, 0x0Du, 0x49u, 0x48u, 0x44u, 0x52u,
        0x00u, 0x00u, 0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x02u,
        0x08u, 0x02u, 0x00u, 0x00u, 0x00u, 0xFDu, 0xD4u, 0x9Au, 0x73u,
        0x00u, 0x00u, 0x00u, 0x19u, 0x49u, 0x44u, 0x41u, 0x54u,
        0x78u, 0x01u, 0x01u, 0x0Eu, 0x00u, 0xF1u, 0xFFu,
        0x00u, 0xFFu, 0x00u, 0x00u, 0xFFu, 0x00u, 0x00u,
        0x00u, 0xFFu, 0x00u, 0x00u, 0xFFu, 0x00u, 0x00u,
        0x23u, 0xC8u, 0x05u, 0xF9u, 0x6Du, 0x62u, 0x7Du, 0xD4u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x49u, 0x45u, 0x4Eu, 0x44u,
    };
    /* Note: append IEND CRC32 (0xAE 0x42 0x60 0x82) */
    static const uint8_t IEND_CRC[4] = {0xAEu, 0x42u, 0x60u, 0x82u};
    FILE *file = fopen(path, "wb");
    if (file != NULL) {
        fwrite(PNG_2X2, 1u, sizeof(PNG_2X2), file);
        fwrite(IEND_CRC, 1u, sizeof(IEND_CRC), file);
        fclose(file);
    }
}

static void test_boot_cover_precache_defers_past_wake_and_recovers_sd_flicker(void) {
    char base_directory[256];
    char music_directory[300];
    char dialos_directory[300];
    char temp_directory[300];
    snprintf(base_directory, sizeof(base_directory), "/tmp/dialos_app_pre_%d", (int)getpid());
    char remove_command[320];
    snprintf(remove_command, sizeof(remove_command), "rm -rf \"%s\"", base_directory);
    (void)system(remove_command);
    mkdir(base_directory, 0755);
    snprintf(music_directory, sizeof(music_directory), "%s/sdcard", base_directory);
    snprintf(dialos_directory, sizeof(dialos_directory), "%s/sdcard/.dial-os", base_directory);
    snprintf(temp_directory, sizeof(temp_directory), "%s/tmp", base_directory);
    mkdir(music_directory, 0755);
    mkdir(dialos_directory, 0755);
    mkdir(temp_directory, 0755);

    char artist_directory[340], album_directory[380], wav_path[420], cover_path[420];
    snprintf(artist_directory, sizeof(artist_directory), "%s/Test Artist", music_directory);
    snprintf(album_directory, sizeof(album_directory), "%s/Test Album", artist_directory);
    snprintf(wav_path, sizeof(wav_path), "%s/01 - Test Song.wav", album_directory);
    snprintf(cover_path, sizeof(cover_path), "%s/cover.png", album_directory);
    mkdir(artist_directory, 0755);
    mkdir(album_directory, 0755);
    FILE *wav_file = fopen(wav_path, "wb");
    TEST_ASSERT_TRUE(wav_file != NULL);
    uint8_t wav_header[44] = {'R','I','F','F',36,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
                              0x44,0xAC,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    fwrite(wav_header, 1u, 44u, wav_file);
    fclose(wav_file);
    write_minimal_2x2_png(cover_path);

    /* Case 1: Act 01 must NOT precache; transient SD false at aa24 recovers within 500 ms */
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);
    operations.library_music_root = music_directory;
    operations.library_dialos_directory = dialos_directory;
    operations.library_temp_directory = temp_directory;

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);

    /* Advance 900 ms (still in Act 01 WAKE): precache must NOT have started */
    advance_runtime_ticks(&runtime, &fake, 900u);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_WAKE, runtime.boot.phase);
    TEST_ASSERT_FALSE(runtime.boot_cover_precache_started);
    TEST_ASSERT_EQ_U32(0u, runtime.library_covers.precache_decoded);

    /* Enter Act 02 and deliver aa24 while SD momentarily reports unmounted (remount race) */
    advance_runtime_ticks(&runtime, &fake, 200u);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, runtime.boot.phase);
    wait_for_library_scan_ready(&runtime, &fake);
    fake.hardware.sd_mounted = false;
    struct inbound_ipc_message aa24 = make_aa24_message(48u, 85u, 180u);
    app_handle_ipc_message(&runtime, &aa24);
    TEST_ASSERT_EQ_U32(1u, runtime.home.telemetry.album_count);

    /* Step 600 ms while unmounted, then restore SD mount -> 500 ms probe recovers */
    advance_runtime_ticks(&runtime, &fake, 600u);
    fake.hardware.sd_mounted = true;
    advance_runtime_ticks(&runtime, &fake, 1000u);
    for (uint32_t spin = 0u;
         spin < 500u && library_cover_cache_is_boot_precaching(&runtime.library_covers); spin++) {
        usleep(1000u);
        advance_runtime_ticks(&runtime, &fake, 20u);
    }
    advance_runtime_ticks(&runtime, &fake, 8000u);

    TEST_ASSERT_EQ_I32(UI_STATE_HOME, runtime.state);
    TEST_ASSERT_EQ_U32(1u, runtime.library_covers.precache_decoded);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "BOOT precache_start") != NULL);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "BOOT precache_done") != NULL);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "BOOT sd_recovered") != NULL);
    struct library_db_collection albums_col = {LIBRARY_DB_COLLECTION_ALBUMS, 0};
    struct library_db_row album_row;
    TEST_ASSERT_EQ_U32(1u, library_db_fetch_window(&runtime.library_db, &albums_col,
                                                   LIBRARY_SORT_ALPHABETICAL, 0u, 1u, &album_row));
    TEST_ASSERT_TRUE(library_cover_cache_lookup_or_load_disk(&runtime.library_covers,
                                                             album_row.uid) != NULL);
    app_destroy(&runtime);

    /* Case 2: If SD stays unmounted > 2.5 s after aa24, boot finishes without deadlocking */
    init_fake_platform(&fake, &operations);
    operations.library_music_root = music_directory;
    operations.library_dialos_directory = dialos_directory;
    operations.library_temp_directory = temp_directory;
    fake.hardware.sd_mounted = false;
    app_initialize(&runtime, &operations);
    app_handle_ipc_message(&runtime, &aa24);
    advance_runtime_ticks(&runtime, &fake, 10000u);
    TEST_ASSERT_EQ_I32(UI_STATE_HOME, runtime.state);
    app_destroy(&runtime);

    (void)system(remove_command);
}

static void test_warm_boot_delta_check_runs_in_boot_and_preserves_home_counts(void) {
    char base_directory[256];
    char music_directory[300];
    char dialos_directory[300];
    char temp_directory[300];
    snprintf(base_directory, sizeof(base_directory), "/tmp/dialos_app_delta_%d", (int)getpid());
    char remove_command[320];
    snprintf(remove_command, sizeof(remove_command), "rm -rf \"%s\"", base_directory);
    (void)system(remove_command);
    mkdir(base_directory, 0755);
    snprintf(music_directory, sizeof(music_directory), "%s/sdcard", base_directory);
    snprintf(dialos_directory, sizeof(dialos_directory), "%s/sdcard/.dial-os", base_directory);
    snprintf(temp_directory, sizeof(temp_directory), "%s/tmp", base_directory);
    mkdir(music_directory, 0755);
    mkdir(dialos_directory, 0755);
    mkdir(temp_directory, 0755);

    char artist_directory[340], album_directory[380], wav_one[420], wav_two[420];
    snprintf(artist_directory, sizeof(artist_directory), "%s/Test Artist", music_directory);
    snprintf(album_directory, sizeof(album_directory), "%s/Test Album", artist_directory);
    snprintf(wav_one, sizeof(wav_one), "%s/01 - First.wav", album_directory);
    snprintf(wav_two, sizeof(wav_two), "%s/02 - Second.wav", album_directory);
    mkdir(artist_directory, 0755);
    mkdir(album_directory, 0755);
    uint8_t wav_header[44] = {'R','I','F','F',36,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
                              0x44,0xAC,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    FILE *file_one = fopen(wav_one, "wb");
    TEST_ASSERT_TRUE(file_one != NULL);
    fwrite(wav_header, 1u, 44u, file_one);
    fclose(file_one);

    /* Boot 1 (cold): builds library.idx */
    struct fake_platform_context fake;
    struct platform_operations operations;
    init_fake_platform(&fake, &operations);
    operations.library_music_root = music_directory;
    operations.library_dialos_directory = dialos_directory;
    operations.library_temp_directory = temp_directory;

    struct app_runtime runtime;
    app_initialize(&runtime, &operations);
    struct inbound_ipc_message aa24 = make_aa24_message(48u, 85u, 180u);
    app_handle_ipc_message(&runtime, &aa24);
    wait_for_library_scan_ready(&runtime, &fake);
    advance_runtime_ticks(&runtime, &fake, 10000u);
    TEST_ASSERT_EQ_I32(UI_STATE_HOME, runtime.state);
    TEST_ASSERT_EQ_U32(1u, runtime.home.telemetry.track_count);
    app_destroy(&runtime);

    /* Boot 2 (warm, unchanged SD): incremental sync runs during boot, Home stays idle & populated */
    init_fake_platform(&fake, &operations);
    operations.library_music_root = music_directory;
    operations.library_dialos_directory = dialos_directory;
    operations.library_temp_directory = temp_directory;
    app_initialize(&runtime, &operations);
    app_handle_ipc_message(&runtime, &aa24);
    wait_for_library_scan_ready(&runtime, &fake);
    advance_runtime_ticks(&runtime, &fake, 10900u);
    TEST_ASSERT_EQ_I32(UI_STATE_HOME, runtime.state);
    TEST_ASSERT_EQ_U32(1u, runtime.home.telemetry.track_count);
    TEST_ASSERT_EQ_U32(1u, runtime.home.telemetry.album_count);
    TEST_ASSERT_TRUE(runtime.library_scan_finished_handled);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "added=0 updated=0 moved=0 removed=0") != NULL);
    app_destroy(&runtime);

    /* Boot 3 (warm, 2nd track added): incremental sync adds 1 track during boot, Home opens with 2 */
    FILE *file_two = fopen(wav_two, "wb");
    TEST_ASSERT_TRUE(file_two != NULL);
    fwrite(wav_header, 1u, 44u, file_two);
    fclose(file_two);
    init_fake_platform(&fake, &operations);
    operations.library_music_root = music_directory;
    operations.library_dialos_directory = dialos_directory;
    operations.library_temp_directory = temp_directory;
    app_initialize(&runtime, &operations);
    app_handle_ipc_message(&runtime, &aa24);
    wait_for_library_scan_ready(&runtime, &fake);
    advance_runtime_ticks(&runtime, &fake, 10900u);
    TEST_ASSERT_EQ_I32(UI_STATE_HOME, runtime.state);
    TEST_ASSERT_EQ_U32(2u, runtime.home.telemetry.track_count);
    TEST_ASSERT_EQ_U32(1u, runtime.home.telemetry.album_count);
    TEST_ASSERT_TRUE(strstr(fake.log_output, "added=1 updated=0 moved=0 removed=0") != NULL);
    app_destroy(&runtime);

    (void)system(remove_command);
}

void run_app_tests(void) {
    printf("Suite: app\n");
    RUN_TEST(test_crash_formatting_and_count_parsing);
    RUN_TEST(test_crash_atomic_files_and_readonly_fallback);
    RUN_TEST(test_nominal_boot_aa24_retry_and_a607);
    RUN_TEST(test_warning_timeout_and_late_recovery);
    RUN_TEST(test_aa22_delayed_sd_probes_and_stability_reset);
    RUN_TEST(test_volume_lock_wake_ordering_and_shutdown);
    RUN_TEST(test_crash_screen_gate_and_ipc_ignoring);
    RUN_TEST(test_home_touch_appearance_persistence_and_live_telemetry);
    RUN_TEST(test_warning_tap_to_retry_recovery);
    RUN_TEST(test_boot_large_stall_delta_does_not_skip_signal_lock_and_handoff);
    RUN_TEST(test_persistent_event_and_performance_logging);
    RUN_TEST(test_library_boot_scan_playback_and_sd_hotswap);
    RUN_TEST(test_boot_cover_precache_defers_past_wake_and_recovers_sd_flicker);
    RUN_TEST(test_warm_boot_delta_check_runs_in_boot_and_preserves_home_counts);
}
