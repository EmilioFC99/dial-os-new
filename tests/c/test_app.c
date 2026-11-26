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
    char ipc_messages[16][32];
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
    if (fake->ipc_count < 16u && length < 32u) {
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
    TEST_ASSERT_EQ_U32(presents_at_ready + 1u, fake.present_count);
    TEST_ASSERT_TRUE(runtime.hardware.sd_mounted);

    /* +1500 ms probe runs without redundant redraw because state unchanged */
    fake.now_ns += 1000ull * NANOSECONDS_PER_MILLISECOND;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_EQ_U32(presents_at_ready + 1u, fake.present_count);

    /* Stability reset at 9999 ms has not reset yet; at 10000 ms resets to 0 */
    fake.now_ns = runtime.ready_entered_ns + (READY_STABILITY_WINDOW_MS - 1u) * NANOSECONDS_PER_MILLISECOND;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_EQ_U32(0u, fake.crash_count_writes);

    fake.now_ns = runtime.ready_entered_ns + READY_STABILITY_WINDOW_MS * NANOSECONDS_PER_MILLISECOND;
    app_handle_tick(&runtime, fake.now_ns);
    TEST_ASSERT_EQ_U32(1u, fake.crash_count_writes);
    TEST_ASSERT_EQ_U32(0u, fake.crash_count_stored);
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
}
