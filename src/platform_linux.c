#include "platform_linux.h"

#include "app.h"
#include "constants.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

extern char **environ;

static uint8_t crash_alt_stack_storage[CRASH_ALT_STACK_BYTES];
static uint64_t crash_boot_start_ms = 0ull;
static volatile sig_atomic_t crash_ui_state_mirror = 0;

static uint64_t read_monotonic_ns_now(void) {
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
        return 0ull;
    }
    return (uint64_t)timestamp.tv_sec * 1000000000ull + (uint64_t)timestamp.tv_nsec;
}

static void sleep_ms_eintr_safe(uint32_t milliseconds) {
    struct timespec remaining;
    remaining.tv_sec = (time_t)(milliseconds / MILLISECONDS_PER_SECOND);
    remaining.tv_nsec =
        (long)((milliseconds % MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND);

    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static bool is_sdcard_mounted(void) {
    struct stat sd_stat;
    struct stat tmp_stat;
    if (stat(PATH_SDCARD_MOUNT, &sd_stat) != 0 || stat(PATH_TMP_ROOT, &tmp_stat) != 0) {
        return false;
    }
    return sd_stat.st_dev != tmp_stat.st_dev;
}

static bool write_text_file_full(const char *file_path, const char *text, size_t length) {
    int file_descriptor = open(file_path, O_WRONLY | O_CLOEXEC);
    if (file_descriptor < 0) {
        return false;
    }
    size_t total_written = 0u;
    bool succeeded = true;
    while (total_written < length) {
        ssize_t written = write(file_descriptor, text + total_written, length - total_written);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            succeeded = false;
            break;
        }
        total_written += (size_t)written;
    }
    close(file_descriptor);
    return succeeded;
}

static bool read_text_file_small(const char *file_path, char *buffer, size_t capacity,
                                 size_t *length_out) {
    if (capacity == 0u) {
        return false;
    }
    int file_descriptor = open(file_path, O_RDONLY | O_CLOEXEC);
    if (file_descriptor < 0) {
        return false;
    }
    ssize_t bytes_read = read(file_descriptor, buffer, capacity - 1u);
    close(file_descriptor);
    if (bytes_read <= 0) {
        return false;
    }
    buffer[bytes_read] = '\0';
    if (length_out != NULL) {
        *length_out = (size_t)bytes_read;
    }
    return true;
}

static void send_panel_ioctl(const char *device_path, unsigned long ioctl_request) {
    int device_fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (device_fd < 0) {
        return;
    }
    (void)ioctl(device_fd, ioctl_request, 0);
    close(device_fd);
}

static void write_pwm_brightness_raw(uint8_t raw_brightness) {
    char text_buffer[16];
    int written = snprintf(text_buffer, sizeof(text_buffer), "%u\n", (unsigned)raw_brightness);
    if (written > 0) {
        (void)write_text_file_full(PATH_BACKLIGHT_BRIGHTNESS, text_buffer, (size_t)written);
    }
}

static uint8_t read_max_brightness(void) {
    char buffer[32];
    size_t length = 0u;
    if (!read_text_file_small(PATH_BACKLIGHT_MAX_BRIGHTNESS, buffer, sizeof(buffer), &length)) {
        return BRIGHTNESS_MAX_DEFAULT;
    }
    uint32_t value = crash_parse_count(buffer, length);
    if (value == 0u || value > BRIGHTNESS_MAX_DEFAULT) {
        return BRIGHTNESS_MAX_DEFAULT;
    }
    return (uint8_t)value;
}

static uint8_t clamp_visible_brightness(const struct platform_linux_state *state,
                                        uint8_t requested_brightness) {
    uint8_t max_allowed = state->max_brightness > 0u ? state->max_brightness : BRIGHTNESS_MAX_DEFAULT;
    uint8_t floor_value =
        BRIGHTNESS_VISIBLE_FLOOR <= max_allowed ? BRIGHTNESS_VISIBLE_FLOOR : max_allowed;
    if (requested_brightness < floor_value) {
        return floor_value;
    }
    if (requested_brightness > max_allowed) {
        return max_allowed;
    }
    return requested_brightness;
}

static uint32_t read_self_rss_kb(void) {
    FILE *status_file = fopen(PATH_PROC_SELF_STATUS, "r");
    if (status_file == NULL) {
        return 0u;
    }
    char line_buffer[256];
    uint32_t rss_kb = 0u;
    while (fgets(line_buffer, sizeof(line_buffer), status_file) != NULL) {
        unsigned long parsed_kb = 0ul;
        if (sscanf(line_buffer, "VmRSS: %lu kB", &parsed_kb) == 1) {
            rss_kb = (uint32_t)parsed_kb;
            break;
        }
    }
    fclose(status_file);
    return rss_kb;
}

static uint32_t count_watchdog_restart_lines(void) {
    FILE *log_file = fopen(PATH_FIIO_PROCESS_FAILED, "r");
    if (log_file == NULL) {
        return 0u;
    }
    char line_buffer[256];
    uint32_t line_count = 0u;
    while (fgets(line_buffer, sizeof(line_buffer), log_file) != NULL) {
        if (line_buffer[0] != '\n' && line_buffer[0] != '\0') {
            line_count++;
        }
    }
    fclose(log_file);
    return line_count;
}

static bool write_file_atomic_synced(const char *directory_path, const char *temp_path,
                                     const char *final_path, const char *content, size_t length) {
    if (directory_path != NULL) {
        if (mkdir(directory_path, 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }

    int temp_fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (temp_fd < 0) {
        return false;
    }

    size_t total_written = 0u;
    bool ok = true;
    while (total_written < length) {
        ssize_t written = write(temp_fd, content + total_written, length - total_written);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        total_written += (size_t)written;
    }

    if (ok && fsync(temp_fd) != 0) {
        ok = false;
    }
    if (close(temp_fd) != 0) {
        ok = false;
    }
    if (ok && rename(temp_path, final_path) != 0) {
        ok = false;
    }
    if (ok && directory_path != NULL) {
        int directory_fd = open(directory_path, O_RDONLY | O_CLOEXEC);
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

static void run_child_command_wait(const char *executable_path, char *const argv[]) {
    pid_t child_pid = 0;
    if (posix_spawn(&child_pid, executable_path, NULL, NULL, argv, environ) != 0) {
        return;
    }
    int child_status = 0;
    while (waitpid(child_pid, &child_status, 0) < 0 && errno == EINTR) {
    }
}

static void fatal_signal_handler(int signal_number, siginfo_t *signal_info, void *raw_context) {
    uint64_t fault_address = 0ull;
    if (signal_info != NULL) {
        fault_address = (uint64_t)(uintptr_t)signal_info->si_addr;
    }

    uint64_t program_counter = 0ull;
    uint64_t return_address = 0ull;
#if defined(__mips__)
    if (raw_context != NULL) {
        const ucontext_t *user_context = (const ucontext_t *)raw_context;
        program_counter = (uint64_t)user_context->uc_mcontext.pc;
        return_address = (uint64_t)user_context->uc_mcontext.gregs[31];
    }
#else
    (void)raw_context;
#endif

    uint64_t now_ms = read_monotonic_ns_now() / NANOSECONDS_PER_MILLISECOND;
    uint64_t uptime_ms = now_ms >= crash_boot_start_ms ? (now_ms - crash_boot_start_ms) : 0ull;
    uint32_t new_crash_count = crash_increment_count_file(PATH_CRASH_COUNT, PATH_CRASH_COUNT_TEMP);

    struct crash_record record = {
        .signal_number = signal_number,
        .fault_address = fault_address,
        .program_counter = program_counter,
        .return_address = return_address,
        .uptime_ms = uptime_ms,
        .ui_state = (int32_t)crash_ui_state_mirror,
        .crash_count = new_crash_count,
    };

    (void)crash_write_log_atomic(&record, is_sdcard_mounted(), PATH_SDCARD_DIALOS_DIRECTORY,
                                 PATH_SDCARD_CRASH_LOG_TEMP, PATH_SDCARD_CRASH_LOG);
    _exit(SIGNAL_EXIT_OFFSET + signal_number);
}

static uint64_t linux_monotonic_ns(void *context) {
    (void)context;
    return read_monotonic_ns_now();
}

static bool linux_display_acquire_surface(void *context, struct framebuffer_surface *surface_out) {
    struct platform_linux_state *state = (struct platform_linux_state *)context;
    if (state->fb_map == NULL || state->fb_fd < 0 || surface_out == NULL) {
        return false;
    }
    state->fb_draw_page =
        state->fb_page_count > 1u ? (state->fb_active_page + 1u) % state->fb_page_count : 0u;
    size_t page_offset_pixels =
        (size_t)state->fb_draw_page * (size_t)state->fb_yres * (size_t)state->fb_stride_pixels;
    surface_out->pixels = state->fb_map + page_offset_pixels;
    surface_out->stride_pixels = state->fb_stride_pixels;
    return true;
}

static void linux_display_present_surface(void *context) {
    struct platform_linux_state *state = (struct platform_linux_state *)context;
    if (state->fb_map == NULL || state->fb_fd < 0) {
        return;
    }
    struct fb_var_screeninfo variable_info;
    if (ioctl(state->fb_fd, FBIOGET_VSCREENINFO, &variable_info) == 0) {
        variable_info.yoffset = state->fb_draw_page * state->fb_yres;
        (void)ioctl(state->fb_fd, FBIOPAN_DISPLAY, &variable_info);
    }
    state->fb_active_page = state->fb_draw_page;
}

static void linux_display_set_brightness(void *context, uint8_t brightness) {
    struct platform_linux_state *state = (struct platform_linux_state *)context;
    write_pwm_brightness_raw(clamp_visible_brightness(state, brightness));
}

static void linux_display_enter_sleep(void *context) {
    (void)context;
    write_pwm_brightness_raw(0u);
    send_panel_ioctl(PATH_TOUCH_PANEL, IOCTL_TOUCH_SLEEP);
    sleep_ms_eintr_safe(PANEL_SLEEP_DELAY_MS);
    send_panel_ioctl(PATH_LCD_PANEL, IOCTL_LCD_SLEEP);
}

static void linux_display_begin_wake(void *context) {
    (void)context;
    send_panel_ioctl(PATH_LCD_PANEL, IOCTL_LCD_WAKE);
    sleep_ms_eintr_safe(PANEL_WAKE_DELAY_MS);
}

static void linux_display_finish_wake(void *context, uint8_t restore_brightness) {
    struct platform_linux_state *state = (struct platform_linux_state *)context;
    write_pwm_brightness_raw(clamp_visible_brightness(state, restore_brightness));
    send_panel_ioctl(PATH_TOUCH_PANEL, IOCTL_TOUCH_WAKE);
}

static bool linux_ipc_send_player(void *context, const char *message, size_t length) {
    struct platform_linux_state *state = (struct platform_linux_state *)context;
    if (state->player_queue == (mqd_t)-1) {
        state->player_queue = mq_open(PATH_QUEUE_PLAYER, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (state->player_queue == (mqd_t)-1) {
            return false;
        }
    }
    if (mq_send(state->player_queue, message, length, 0u) == 0) {
        return true;
    }
    if (errno == EBADF || errno == EINVAL) {
        mq_close(state->player_queue);
        state->player_queue = (mqd_t)-1;
    }
    return false;
}

static void linux_probe_hardware_status(void *context, struct hardware_status *status_out) {
    (void)context;
    if (status_out == NULL) {
        return;
    }
    status_out->sd_mounted = is_sdcard_mounted();
    status_out->battery_valid = false;
    status_out->battery_percent = 0u;

    char battery_text[32];
    size_t length = 0u;
    if (read_text_file_small(PATH_BATTERY_CAPACITY, battery_text, sizeof(battery_text), &length)) {
        size_t index = 0u;
        while (index < length && (battery_text[index] == ' ' || battery_text[index] == '\t')) {
            index++;
        }
        if (index < length && battery_text[index] >= '0' && battery_text[index] <= '9') {
            uint32_t percent = 0u;
            while (index < length && battery_text[index] >= '0' && battery_text[index] <= '9') {
                percent = percent * 10u + (uint32_t)(battery_text[index] - '0');
                index++;
            }
            if (percent > BATTERY_PERCENT_MAX) {
                percent = BATTERY_PERCENT_MAX;
            }
            status_out->battery_valid = true;
            status_out->battery_percent = (uint8_t)percent;
        }
    }
}

static uint32_t linux_crash_count_read(void *context) {
    (void)context;
    char count_text[CRASH_COUNT_BUFFER_BYTES];
    size_t length = 0u;
    if (!read_text_file_small(PATH_CRASH_COUNT, count_text, sizeof(count_text), &length)) {
        return 0u;
    }
    return crash_parse_count(count_text, length);
}

static void linux_crash_count_write(void *context, uint32_t count) {
    (void)context;
    char count_text[CRASH_COUNT_BUFFER_BYTES];
    size_t length = crash_format_count(count, count_text, sizeof(count_text));
    if (length > 0u) {
        (void)write_file_atomic_synced(NULL, PATH_CRASH_COUNT_TEMP, PATH_CRASH_COUNT, count_text,
                                       length);
    }
}

static void linux_telemetry_write(void *context, const struct telemetry_snapshot *snapshot) {
    struct platform_linux_state *state = (struct platform_linux_state *)context;
    if (snapshot == NULL) {
        return;
    }

    uint32_t current_rss_kb = read_self_rss_kb();
    if (!snapshot->is_shutdown_update) {
        state->rss_ready_kb = current_rss_kb;
    }
    uint32_t ready_rss_kb = state->rss_ready_kb > 0u ? state->rss_ready_kb : current_rss_kb;
    bool sd_mounted_now = is_sdcard_mounted();

    char telemetry_buffer[512];
    int written = 0;
    if (snapshot->is_shutdown_update) {
        uint32_t total_watchdog_restarts = count_watchdog_restart_lines();
        uint32_t watchdog_restarts =
            total_watchdog_restarts >= state->initial_watchdog_restarts
                ? (total_watchdog_restarts - state->initial_watchdog_restarts)
                : 0u;
        written = snprintf(
            telemetry_buffer, sizeof(telemetry_buffer),
            "takeover_ms=%llu\n"
            "animation_frames=%u\n"
            "animation_fps_x10=%u\n"
            "max_frame_ms=%u\n"
            "aa24_ms=%llu\n"
            "idle_poweroff_disabled=%u\n"
            "sd_mounted=%u\n"
            "rss_ready_kb=%u\n"
            "uptime_ms=%llu\n"
            "rss_shutdown_kb=%u\n"
            "watchdog_restarts=%u\n",
            (unsigned long long)snapshot->takeover_ms, snapshot->animation_frames,
            snapshot->animation_fps_x10, snapshot->max_frame_ms,
            (unsigned long long)snapshot->aa24_ms, snapshot->idle_poweroff_disabled ? 1u : 0u,
            sd_mounted_now ? 1u : 0u, ready_rss_kb, (unsigned long long)snapshot->uptime_ms,
            current_rss_kb, watchdog_restarts);
    } else {
        written = snprintf(
            telemetry_buffer, sizeof(telemetry_buffer),
            "takeover_ms=%llu\n"
            "animation_frames=%u\n"
            "animation_fps_x10=%u\n"
            "max_frame_ms=%u\n"
            "aa24_ms=%llu\n"
            "idle_poweroff_disabled=%u\n"
            "sd_mounted=%u\n"
            "rss_ready_kb=%u\n",
            (unsigned long long)snapshot->takeover_ms, snapshot->animation_frames,
            snapshot->animation_fps_x10, snapshot->max_frame_ms,
            (unsigned long long)snapshot->aa24_ms, snapshot->idle_poweroff_disabled ? 1u : 0u,
            sd_mounted_now ? 1u : 0u, ready_rss_kb);
    }

    if (written <= 0 || (size_t)written >= sizeof(telemetry_buffer)) {
        return;
    }

    (void)write_file_atomic_synced(NULL, PATH_TMP_BOOT_LOG_TEMP, PATH_TMP_BOOT_LOG, telemetry_buffer,
                                   (size_t)written);
    if (sd_mounted_now) {
        (void)write_file_atomic_synced(PATH_SDCARD_DIALOS_DIRECTORY, PATH_SDCARD_BOOT_LOG_TEMP,
                                       PATH_SDCARD_BOOT_LOG, telemetry_buffer, (size_t)written);
    }
}

static void linux_execute_shutdown(void *context, bool screen_was_locked,
                                   const struct telemetry_snapshot *final_snapshot) {
    struct platform_linux_state *state = (struct platform_linux_state *)context;
    sleep_ms_eintr_safe(SHUTDOWN_MUTE_SETTLE_MS);

    char *killall_argv[] = {PATH_KILLALL, "-9", "fiio_init.sh", NULL};
    run_child_command_wait(PATH_KILLALL, killall_argv);

    char *watchdog_argv[] = {PATH_CMD_WATCHDOG, "stop", NULL};
    run_child_command_wait(PATH_CMD_WATCHDOG, watchdog_argv);

    linux_telemetry_write(context, final_snapshot);
    sync();

    if (!screen_was_locked) {
        write_pwm_brightness_raw(0u);
        send_panel_ioctl(PATH_LCD_PANEL, IOCTL_LCD_SLEEP);
    }

    platform_linux_shutdown_cleanup(state);

    char *poweroff_argv[] = {PATH_POWEROFF, NULL};
    execv(PATH_POWEROFF, poweroff_argv);

    sync();
    (void)reboot(RB_POWER_OFF);
}

static void linux_update_crash_state_mirror(void *context, int32_t ui_state) {
    (void)context;
    crash_ui_state_mirror = (sig_atomic_t)ui_state;
}

void platform_linux_set_process_name(void) {
    (void)prctl(PR_SET_NAME, PROCESS_COMM_NAME, 0, 0, 0);
}

void platform_linux_install_crash_handler(uint64_t boot_start_ms) {
    crash_boot_start_ms = boot_start_ms;
    crash_ui_state_mirror = 0;

    stack_t alt_stack;
    alt_stack.ss_sp = crash_alt_stack_storage;
    alt_stack.ss_size = sizeof(crash_alt_stack_storage);
    alt_stack.ss_flags = 0;
    (void)sigaltstack(&alt_stack, NULL);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = fatal_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;

    static const int FATAL_SIGNALS[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
    for (size_t index = 0u; index < sizeof(FATAL_SIGNALS) / sizeof(FATAL_SIGNALS[0]); index++) {
        (void)sigaction(FATAL_SIGNALS[index], &action, NULL);
    }
}

static void initialize_framebuffer(struct platform_linux_state *state) {
    state->fb_fd = open(PATH_FRAMEBUFFER, O_RDWR | O_CLOEXEC);
    if (state->fb_fd < 0) {
        return;
    }

    struct fb_fix_screeninfo fixed_info;
    struct fb_var_screeninfo variable_info;
    if (ioctl(state->fb_fd, FBIOGET_FSCREENINFO, &fixed_info) != 0 ||
        ioctl(state->fb_fd, FBIOGET_VSCREENINFO, &variable_info) != 0 ||
        variable_info.yres == 0u || fixed_info.line_length < SCREEN_WIDTH * 4u) {
        close(state->fb_fd);
        state->fb_fd = -1;
        return;
    }

    size_t page_bytes = (size_t)fixed_info.line_length * (size_t)variable_info.yres;
    uint32_t pages_by_mem = (uint32_t)(fixed_info.smem_len / page_bytes);
    uint32_t pages_by_virtual = variable_info.yres_virtual / variable_info.yres;
    uint32_t page_count = pages_by_mem < pages_by_virtual ? pages_by_mem : pages_by_virtual;
    if (page_count > FRAMEBUFFER_PAGE_COUNT) {
        page_count = FRAMEBUFFER_PAGE_COUNT;
    }
    if (page_count == 0u && fixed_info.smem_len >= page_bytes) {
        page_count = 1u;
    }
    if (page_count == 0u) {
        close(state->fb_fd);
        state->fb_fd = -1;
        return;
    }

    size_t map_bytes = page_bytes * (size_t)page_count;
    void *mapped = mmap(NULL, map_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, state->fb_fd, 0);
    if (mapped == MAP_FAILED) {
        close(state->fb_fd);
        state->fb_fd = -1;
        return;
    }

    state->fb_map = (uint32_t *)mapped;
    state->fb_map_bytes = map_bytes;
    state->fb_stride_pixels = fixed_info.line_length / 4u;
    state->fb_yres = variable_info.yres;
    state->fb_page_count = page_count;
    state->fb_active_page = 0u;
    state->fb_draw_page = 0u;
}

static void initialize_ui_mqueue(struct platform_linux_state *state) {
    struct mq_attr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.mq_maxmsg = MESSAGE_QUEUE_MAX_MESSAGES;
    attributes.mq_msgsize = MESSAGE_QUEUE_MAX_MESSAGE_BYTES;

    state->ui_queue =
        mq_open(PATH_QUEUE_UI, O_CREAT | O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0666, &attributes);
    if (state->ui_queue == (mqd_t)-1) {
        return;
    }

    struct mq_attr actual_attributes;
    if (mq_getattr(state->ui_queue, &actual_attributes) != 0 ||
        actual_attributes.mq_msgsize <= 0 ||
        (size_t)actual_attributes.mq_msgsize > MESSAGE_QUEUE_RECEIVE_BUFFER_BYTES) {
        mq_close(state->ui_queue);
        state->ui_queue = (mqd_t)-1;
        return;
    }

    char drain_buffer[MESSAGE_QUEUE_RECEIVE_BUFFER_BYTES];
    while (mq_receive(state->ui_queue, drain_buffer, sizeof(drain_buffer), NULL) >= 0) {
    }
}

bool platform_linux_initialize(struct platform_linux_state *state,
                               struct platform_operations *operations_out) {
    if (state == NULL || operations_out == NULL) {
        return false;
    }

    memset(state, 0, sizeof(*state));
    state->fb_fd = -1;
    state->event0_fd = -1;
    state->ui_queue = (mqd_t)-1;
    state->player_queue = (mqd_t)-1;

    /* Recover from any prior crash while LOCKED before first frame draw */
    send_panel_ioctl(PATH_LCD_PANEL, IOCTL_LCD_WAKE);
    sleep_ms_eintr_safe(PANEL_WAKE_DELAY_MS);
    send_panel_ioctl(PATH_TOUCH_PANEL, IOCTL_TOUCH_WAKE);

    state->max_brightness = read_max_brightness();
    state->initial_watchdog_restarts = count_watchdog_restart_lines();
    initialize_framebuffer(state);
    initialize_ui_mqueue(state);

    state->event0_fd = open(PATH_INPUT_EVENTS, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (state->event0_fd >= 0) {
        (void)ioctl(state->event0_fd, EVIOCGRAB, 1);
    }

    operations_out->context = state;
    operations_out->monotonic_ns = linux_monotonic_ns;
    operations_out->display_acquire_surface = linux_display_acquire_surface;
    operations_out->display_present_surface = linux_display_present_surface;
    operations_out->display_set_brightness = linux_display_set_brightness;
    operations_out->display_enter_sleep = linux_display_enter_sleep;
    operations_out->display_begin_wake = linux_display_begin_wake;
    operations_out->display_finish_wake = linux_display_finish_wake;
    operations_out->ipc_send_player = linux_ipc_send_player;
    operations_out->probe_hardware_status = linux_probe_hardware_status;
    operations_out->crash_count_read = linux_crash_count_read;
    operations_out->crash_count_write = linux_crash_count_write;
    operations_out->telemetry_write = linux_telemetry_write;
    operations_out->execute_shutdown = linux_execute_shutdown;
    operations_out->update_crash_state_mirror = linux_update_crash_state_mirror;
    return true;
}

void platform_linux_get_poll_descriptors(const struct platform_linux_state *state,
                                         int *ui_queue_fd_out, int *input_event_fd_out) {
    if (state == NULL) {
        return;
    }
    if (ui_queue_fd_out != NULL) {
        *ui_queue_fd_out = (state->ui_queue != (mqd_t)-1) ? (int)state->ui_queue : -1;
    }
    if (input_event_fd_out != NULL) {
        *input_event_fd_out = state->event0_fd;
    }
}

ssize_t platform_linux_receive_ipc(struct platform_linux_state *state,
                                   char *buffer, size_t capacity) {
    if (state == NULL || state->ui_queue == (mqd_t)-1 || buffer == NULL || capacity == 0u) {
        return -1;
    }
    ssize_t received = -1;
    do {
        received = mq_receive(state->ui_queue, buffer, capacity, NULL);
    } while (received < 0 && errno == EINTR);
    return received;
}

size_t platform_linux_read_input_events(struct platform_linux_state *state,
                                        struct kernel_input_event *events, size_t max_events) {
    if (state == NULL || state->event0_fd < 0 || events == NULL || max_events == 0u) {
        return 0u;
    }
    ssize_t bytes_read = -1;
    do {
        bytes_read = read(state->event0_fd, events, max_events * sizeof(struct kernel_input_event));
    } while (bytes_read < 0 && errno == EINTR);

    if (bytes_read <= 0) {
        return 0u;
    }
    return (size_t)bytes_read / sizeof(struct kernel_input_event);
}

void platform_linux_shutdown_cleanup(struct platform_linux_state *state) {
    if (state == NULL) {
        return;
    }
    if (state->event0_fd >= 0) {
        (void)ioctl(state->event0_fd, EVIOCGRAB, 0);
        close(state->event0_fd);
        state->event0_fd = -1;
    }
    if (state->ui_queue != (mqd_t)-1) {
        mq_close(state->ui_queue);
        state->ui_queue = (mqd_t)-1;
    }
    if (state->player_queue != (mqd_t)-1) {
        mq_close(state->player_queue);
        state->player_queue = (mqd_t)-1;
    }
    if (state->fb_map != NULL && state->fb_map_bytes > 0u) {
        munmap(state->fb_map, state->fb_map_bytes);
        state->fb_map = NULL;
        state->fb_map_bytes = 0u;
    }
    if (state->fb_fd >= 0) {
        close(state->fb_fd);
        state->fb_fd = -1;
    }
}
