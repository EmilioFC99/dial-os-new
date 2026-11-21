#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <math.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* =========================================================================
 * Hardware, IPC & UI Constants (V2.57 FiiO Snowsky Disc)
 * ========================================================================= */

#define SCREEN_WIDTH 360u
#define SCREEN_HEIGHT 360u
#define SCREEN_VIRTUAL_HEIGHT 1080u
#define FB_PAGE_COUNT 3u
#define SCREEN_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)
#define SCREEN_CENTER_X 180
#define SCREEN_CENTER_Y 180
#define DISC_MASK_RADIUS 174
#define TOUCH_COORD_MAX 359

#define FB_DEVICE_PATH "/dev/fb0"
#define KEY_DEVICE_PATH "/dev/input/event0"
#define TOUCH_DEVICE_PATH "/dev/input/event1"
#define BACKLIGHT_SYSFS_PRIMARY \
    "/sys/bus/platform/drivers/pwm-backlight/backlight/backlight/backlight/brightness"
#define BACKLIGHT_SYSFS_FALLBACK "/sys/class/backlight/backlight/brightness"
#define BACKLIGHT_INIT_VALUE "180\n"
#define ASOUND_STATUS_PATH "/proc/asound/card0/pcm0p/sub0/status"

#define WATCHDOG_PROCESS_NAME "mq_ui"
#define UI_QUEUE_NAME "/ui"
#define PLAYER_QUEUE_NAME "/player"
#define MQ_MAX_MESSAGES 10L
#define MQ_MAX_MSG_SIZE 8192L
#define MQ_RECV_BUFFER_SIZE 16384u /* Q01: >= 8192 bytes prevents EMSGSIZE */

#define X2000_KEY_PLAY_LONG 268u /* Q03: ignored by mq_player, safe backup */

#define SD_OUTPUT_DIR "/tmp/sdcard/.dial-os"
#define SD_TEST_AUDIO_DIR "/tmp/sdcard/.dial-os/m0_test"
#define SD_FALLBACK_AUDIO_DIR "/tmp/sdcard"
#define REPORT_FILE_PATH "/tmp/sdcard/.dial-os/m0_report.txt"
#define TRACE_FILE_PATH "/tmp/sdcard/.dial-os/m0_ipc_trace.log"

#define M3U_FILE_A "/tmp/dialos_play_A.m3u"
#define M3U_FILE_B "/tmp/dialos_play_B.m3u"

/* Exact V2.57 /player IPC opcodes */
#define IPC_CMD_PLAY_PAUSE "0201000C0000"
#define IPC_CMD_NEXT_TRACK "0201000C0001"
#define IPC_CMD_GET_STATE "02020008"
#define IPC_CMD_WIRED_INPUT_MODE "0642000C0000"
#define IPC_CMD_WIRED_OUTPUT_DAC "0666000C0006"
#define IPC_CMD_WIRED_SOURCE "0657000C0008"
#define IPC_CMD_INITIAL_VOLUME "0715000C0030"
#define IPC_CMD_DISABLE_IDLE_POWEROFF "0664000C0000"
#define IPC_CMD_EQ_OFF "0689000C0000"
#define IPC_CMD_EQ_PRESET_ROCK "0689000C0001"

/* Fixed-point 16.16 rotation LUT */
#define TRIG_LUT_SIZE 1024u
#define TRIG_LUT_MASK 1023u
#define FIXED_ONE 65536
#define FIXED_SHIFT 16

/* UI Colors (XRGB8888) */
#define COLOR_BG 0xFF0D1117u
#define COLOR_HEADER_BG 0xFF161B22u
#define COLOR_TEXT 0xFFE6EBF5u
#define COLOR_MUTED 0xFF9AA5B8u
#define COLOR_ACCENT 0xFF58A6FFu
#define COLOR_GREEN 0xFF3FB950u
#define COLOR_AMBER 0xFFD29922u
#define COLOR_RED 0xFFF85149u
#define COLOR_BTN_RUN 0xFF238636u
#define COLOR_BTN_NEXT 0xFF1F6FEBu
#define COLOR_BTN_DONE 0xFF3FB950u
#define COLOR_BTN_TEXT 0xFFFFFFFFu
#define COLOR_BTN_DARK_TEXT 0xFF0D1117u

/* Bottom touch buttons inside safe circular chord */
#define BTN_RUN_X 52
#define BTN_RUN_Y 286
#define BTN_NEXT_X 190
#define BTN_NEXT_Y 286
#define BTN_WIDTH 118
#define BTN_HEIGHT 46

#define MAX_TRACKS 8u
#define MAX_PATH_LEN 512u
#define MAX_FRAME_TIMINGS 2048u
#define STAGE_COUNT 5u

/* =========================================================================
 * Minimal 8x8 Bitmap Font (ASCII 0x20..0x7E)
 * ========================================================================= */

static const uint8_t FONT_8X8[95][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' ' */
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, /* '!' */
    {0x36, 0x36, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '"' */
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, /* '#' */
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, /* '$' */
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, /* '%' */
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, /* '&' */
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ''' */
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, /* '(' */
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, /* ')' */
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, /* '*' */
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, /* '+' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, /* ',' */
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, /* '-' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, /* '.' */
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, /* '/' */
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, /* '0' */
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, /* '1' */
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, /* '2' */
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, /* '3' */
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, /* '4' */
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, /* '5' */
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, /* '6' */
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, /* '7' */
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, /* '8' */
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, /* '9' */
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, /* ':' */
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, /* ';' */
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, /* '<' */
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, /* '=' */
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, /* '>' */
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, /* '?' */
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, /* '@' */
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, /* 'A' */
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, /* 'B' */
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, /* 'C' */
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, /* 'D' */
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, /* 'E' */
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, /* 'F' */
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, /* 'G' */
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, /* 'H' */
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, /* 'I' */
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, /* 'J' */
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, /* 'K' */
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, /* 'L' */
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, /* 'M' */
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, /* 'N' */
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, /* 'O' */
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, /* 'P' */
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, /* 'Q' */
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, /* 'R' */
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, /* 'S' */
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, /* 'T' */
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, /* 'U' */
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, /* 'V' */
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, /* 'W' */
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, /* 'X' */
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, /* 'Y' */
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, /* 'Z' */
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, /* '[' */
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, /* '\' */
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, /* ']' */
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, /* '^' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, /* '_' */
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '`' */
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, /* 'a' */
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, /* 'b' */
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, /* 'c' */
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00}, /* 'd' */
    {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00}, /* 'e' */
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00}, /* 'f' */
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, /* 'g' */
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, /* 'h' */
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, /* 'i' */
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, /* 'j' */
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, /* 'k' */
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, /* 'l' */
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, /* 'm' */
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, /* 'n' */
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, /* 'o' */
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, /* 'p' */
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, /* 'q' */
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, /* 'r' */
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, /* 's' */
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, /* 't' */
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, /* 'u' */
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, /* 'v' */
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, /* 'w' */
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, /* 'x' */
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, /* 'y' */
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, /* 'z' */
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, /* '{' */
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, /* '|' */
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, /* '}' */
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '~' */
};

/* =========================================================================
 * State Structures
 * ========================================================================= */

typedef enum {
    STATUS_IDLE = 0,
    STATUS_PASS = 1,
    STATUS_FAIL = 2,
    STATUS_LOGGED = 3
} StageStatus;

typedef struct {
    int32_t framebuffer_fd;
    uint32_t *mapped_pixels;
    size_t mapped_bytes;
    uint32_t stride_pixels;
    struct fb_var_screeninfo var_info;
} FramebufferDevice;

typedef struct {
    int32_t key_fd;
    int32_t touch_fd;
    bool touch_down;
    bool touch_pressed_edge;
    bool touch_released_edge;
    bool backup_long_play_edge;
    int32_t touch_x;
    int32_t touch_y;
    bool sample_ready;
    uint32_t syn_count_in_poll;
    uint64_t sample_time_ms;
} InputState;

typedef struct {
    mqd_t ui_queue;
    mqd_t player_queue;
    bool wired_route_sent;
    bool playing;
    bool paused;
    bool eof_seen;
    bool song_db_leak_seen;
    uint32_t position_ms;
    uint32_t duration_ms;
    uint64_t tx_count;
    uint64_t rx_count;
    char last_reply[160];
    char last_song_hint[160];
} IpcState;

typedef struct {
    char paths[MAX_TRACKS][MAX_PATH_LEN];
    char names[MAX_TRACKS][128];
    uint32_t count;
} TrackLibrary;

typedef struct {
    StageStatus status;
    char lines[6][48];
    uint32_t line_count;
} StageSummary;

typedef struct {
    FramebufferDevice display;
    InputState input;
    IpcState ipc;
    TrackLibrary tracks;
    FILE *report_file;
    FILE *trace_file;
    uint32_t current_stage; /* 0..4 */
    bool all_done_synced;
    StageSummary summaries[STAGE_COUNT];
    uint32_t canvas[SCREEN_PIXELS];
    uint32_t texture[SCREEN_PIXELS];
    uint8_t disc_mask[SCREEN_PIXELS];
    int32_t sin_lut[TRIG_LUT_SIZE];
    int32_t cos_lut[TRIG_LUT_SIZE];
} BenchApp;

/* =========================================================================
 * Time & File Helpers
 * ========================================================================= */

static uint64_t monotonic_ms(void) {
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (uint64_t)timestamp.tv_sec * 1000ULL +
           (uint64_t)timestamp.tv_nsec / 1000000ULL;
}

static void sleep_ms(uint32_t milliseconds) {
    struct timespec duration;
    duration.tv_sec = (time_t)(milliseconds / 1000u);
    duration.tv_nsec = (long)((milliseconds % 1000u) * 1000000u);
    nanosleep(&duration, NULL);
}

static void write_sysfs_string(const char *path, const char *value) {
    int32_t sysfs_fd = open(path, O_WRONLY);
    if (sysfs_fd < 0) {
        return;
    }
    size_t length = strlen(value);
    ssize_t written = write(sysfs_fd, value, length);
    (void)written;
    close(sysfs_fd);
}

static void flush_logs_to_sd(BenchApp *app) {
    if (app->report_file != NULL) {
        fflush(app->report_file);
        fsync(fileno(app->report_file));
    }
    if (app->trace_file != NULL) {
        fflush(app->trace_file);
        fsync(fileno(app->trace_file));
    }
}

static void open_sd_logs(BenchApp *app) {
    mkdir(SD_OUTPUT_DIR, 0755);
    app->report_file = fopen(REPORT_FILE_PATH, "w");
    app->trace_file = fopen(TRACE_FILE_PATH, "w");
    if (app->report_file != NULL) {
        fprintf(app->report_file, "DialOS M0 V2.57 Hardware Feasibility Report\n");
        fprintf(app->report_file, "===========================================\n\n");
    }
    if (app->trace_file != NULL) {
        fprintf(app->trace_file, "# DialOS M0 Golden V2.57 IPC Trace\n");
    }
    flush_logs_to_sd(app);
}

/* =========================================================================
 * Display & 8x8 Software Renderer (180-degree Hardware Flip)
 * ========================================================================= */

static bool init_framebuffer(FramebufferDevice *display) {
    display->framebuffer_fd = open(FB_DEVICE_PATH, O_RDWR);
    if (display->framebuffer_fd < 0) {
        return false;
    }
    struct fb_fix_screeninfo fix_info;
    if (ioctl(display->framebuffer_fd, FBIOGET_FSCREENINFO, &fix_info) < 0 ||
        ioctl(display->framebuffer_fd, FBIOGET_VSCREENINFO, &display->var_info) < 0) {
        close(display->framebuffer_fd);
        display->framebuffer_fd = -1;
        return false;
    }
    display->stride_pixels = fix_info.line_length / 4u;
    display->mapped_bytes =
        (size_t)fix_info.line_length * (size_t)(SCREEN_HEIGHT * FB_PAGE_COUNT);
    void *mapped = mmap(NULL, display->mapped_bytes, PROT_READ | PROT_WRITE,
                        MAP_SHARED, display->framebuffer_fd, 0);
    if (mapped == MAP_FAILED) {
        close(display->framebuffer_fd);
        display->framebuffer_fd = -1;
        return false;
    }
    display->mapped_pixels = (uint32_t *)mapped;
    memset(display->mapped_pixels, 0, display->mapped_bytes);

    display->var_info.yoffset = 0u;
    display->var_info.activate = FB_ACTIVATE_NOW;
    ioctl(display->framebuffer_fd, FBIOPAN_DISPLAY, &display->var_info);

    write_sysfs_string(BACKLIGHT_SYSFS_PRIMARY, BACKLIGHT_INIT_VALUE);
    write_sysfs_string(BACKLIGHT_SYSFS_FALLBACK, BACKLIGHT_INIT_VALUE);
    return true;
}

static void fb_blit_180(BenchApp *app, uint32_t page_index) {
    if (app->display.mapped_pixels == NULL) {
        return;
    }
    uint32_t stride = app->display.stride_pixels;
    uint32_t *target_page =
        app->display.mapped_pixels + (size_t)page_index * SCREEN_HEIGHT * stride;
    for (uint32_t row_y = 0u; row_y < SCREEN_HEIGHT; ++row_y) {
        const uint32_t *canvas_row = app->canvas + (size_t)row_y * SCREEN_WIDTH;
        uint32_t *fb_row =
            target_page + (size_t)(TOUCH_COORD_MAX - row_y) * stride;
        for (uint32_t col_x = 0u; col_x < SCREEN_WIDTH; ++col_x) {
            fb_row[TOUCH_COORD_MAX - col_x] = canvas_row[col_x];
        }
    }
}

static void fb_pan_page(BenchApp *app, uint32_t page_index) {
    if (app->display.framebuffer_fd < 0) {
        return;
    }
    app->display.var_info.yoffset = page_index * SCREEN_HEIGHT;
    app->display.var_info.activate = FB_ACTIVATE_NOW;
    ioctl(app->display.framebuffer_fd, FBIOPAN_DISPLAY, &app->display.var_info);
}

static void canvas_clear(BenchApp *app, uint32_t color) {
    for (uint32_t pixel_index = 0u; pixel_index < SCREEN_PIXELS; ++pixel_index) {
        app->canvas[pixel_index] = color;
    }
}

static void canvas_fill_rect(BenchApp *app, int32_t rect_x, int32_t rect_y,
                             int32_t rect_w, int32_t rect_h, uint32_t color) {
    int32_t start_x = rect_x < 0 ? 0 : rect_x;
    int32_t start_y = rect_y < 0 ? 0 : rect_y;
    int32_t end_x = (rect_x + rect_w) > (int32_t)SCREEN_WIDTH
                        ? (int32_t)SCREEN_WIDTH
                        : (rect_x + rect_w);
    int32_t end_y = (rect_y + rect_h) > (int32_t)SCREEN_HEIGHT
                        ? (int32_t)SCREEN_HEIGHT
                        : (rect_y + rect_h);
    for (int32_t pos_y = start_y; pos_y < end_y; ++pos_y) {
        uint32_t *row_ptr = app->canvas + (uint32_t)pos_y * SCREEN_WIDTH;
        for (int32_t pos_x = start_x; pos_x < end_x; ++pos_x) {
            row_ptr[pos_x] = color;
        }
    }
}

static void canvas_fill_circle(BenchApp *app, int32_t center_x, int32_t center_y,
                               int32_t radius, uint32_t color) {
    int32_t radius_sq = radius * radius;
    for (int32_t offset_y = -radius; offset_y <= radius; ++offset_y) {
        int32_t pos_y = center_y + offset_y;
        if (pos_y < 0 || pos_y >= (int32_t)SCREEN_HEIGHT) {
            continue;
        }
        for (int32_t offset_x = -radius; offset_x <= radius; ++offset_x) {
            int32_t pos_x = center_x + offset_x;
            if (pos_x < 0 || pos_x >= (int32_t)SCREEN_WIDTH) {
                continue;
            }
            if (offset_x * offset_x + offset_y * offset_y <= radius_sq) {
                app->canvas[(uint32_t)pos_y * SCREEN_WIDTH + (uint32_t)pos_x] = color;
            }
        }
    }
}

static void canvas_draw_ring(BenchApp *app, int32_t center_x, int32_t center_y,
                             int32_t inner_r, int32_t outer_r, uint32_t color) {
    int32_t inner_sq = inner_r * inner_r;
    int32_t outer_sq = outer_r * outer_r;
    for (int32_t pos_y = 0; pos_y < (int32_t)SCREEN_HEIGHT; ++pos_y) {
        int32_t delta_y = pos_y - center_y;
        for (int32_t pos_x = 0; pos_x < (int32_t)SCREEN_WIDTH; ++pos_x) {
            int32_t delta_x = pos_x - center_x;
            int32_t dist_sq = delta_x * delta_x + delta_y * delta_y;
            if (dist_sq >= inner_sq && dist_sq <= outer_sq) {
                app->canvas[(uint32_t)pos_y * SCREEN_WIDTH + (uint32_t)pos_x] = color;
            }
        }
    }
}

static void canvas_draw_char(BenchApp *app, int32_t draw_x, int32_t draw_y,
                             char glyph_char, uint32_t color, uint32_t scale) {
    uint8_t code = (uint8_t)glyph_char;
    if (code < 0x20u || code > 0x7Eu) {
        code = (uint8_t)'?';
    }
    const uint8_t *bitmap = FONT_8X8[code - 0x20u];
    for (uint32_t glyph_row = 0u; glyph_row < 8u; ++glyph_row) {
        uint8_t row_bits = bitmap[glyph_row];
        for (uint32_t glyph_col = 0u; glyph_col < 8u; ++glyph_col) {
            if ((row_bits & (1u << glyph_col)) != 0u) {
                canvas_fill_rect(app,
                                 draw_x + (int32_t)(glyph_col * scale),
                                 draw_y + (int32_t)(glyph_row * scale),
                                 (int32_t)scale, (int32_t)scale, color);
            }
        }
    }
}

static void canvas_draw_text(BenchApp *app, int32_t draw_x, int32_t draw_y,
                             const char *text, uint32_t color, uint32_t scale) {
    int32_t cursor_x = draw_x;
    for (size_t char_index = 0u; text[char_index] != '\0'; ++char_index) {
        canvas_draw_char(app, cursor_x, draw_y, text[char_index], color, scale);
        cursor_x += (int32_t)(8u * scale);
    }
}

static void canvas_draw_centered(BenchApp *app, int32_t draw_y, const char *text,
                                 uint32_t color, uint32_t scale) {
    size_t length = strlen(text);
    int32_t total_width = (int32_t)(length * 8u * scale);
    int32_t start_x = ((int32_t)SCREEN_WIDTH - total_width) / 2;
    canvas_draw_text(app, start_x, draw_y, text, color, scale);
}

/* =========================================================================
 * IPC Transport (/ui & /player) + Golden Trace Logger
 * ========================================================================= */

static void log_ipc_line(BenchApp *app, const char *direction, const char *frame) {
    if (app->trace_file == NULL) {
        return;
    }
    uint64_t now_ms = monotonic_ms();
    fprintf(app->trace_file, "[%s %llu] %s\n", direction,
            (unsigned long long)now_ms, frame);
}

static void ipc_send_frame(BenchApp *app, const char *frame) {
    if (app->ipc.player_queue == (mqd_t)-1) {
        app->ipc.player_queue = mq_open(PLAYER_QUEUE_NAME, O_WRONLY | O_NONBLOCK);
    }
    if (app->ipc.player_queue == (mqd_t)-1) {
        return;
    }
    size_t length = strlen(frame);
    if (mq_send(app->ipc.player_queue, frame, length, 0u) == 0) {
        app->ipc.tx_count++;
        log_ipc_line(app, "TX", frame);
    }
}

static void ipc_assert_wired_safety(BenchApp *app) {
    if (app->ipc.wired_route_sent || app->ipc.player_queue == (mqd_t)-1) {
        return;
    }
    /* Q16: wake dual CS43131 wired route + disable idle poweroff */
    ipc_send_frame(app, IPC_CMD_WIRED_INPUT_MODE);
    ipc_send_frame(app, IPC_CMD_WIRED_OUTPUT_DAC);
    ipc_send_frame(app, IPC_CMD_WIRED_SOURCE);
    ipc_send_frame(app, IPC_CMD_INITIAL_VOLUME);
    ipc_send_frame(app, IPC_CMD_DISABLE_IDLE_POWEROFF);
    app->ipc.wired_route_sent = true;
}

static void init_ipc(BenchApp *app) {
    struct mq_attr queue_attributes;
    memset(&queue_attributes, 0, sizeof(queue_attributes));
    queue_attributes.mq_maxmsg = MQ_MAX_MESSAGES;
    queue_attributes.mq_msgsize = MQ_MAX_MSG_SIZE;

    app->ipc.ui_queue = mq_open(UI_QUEUE_NAME, O_CREAT | O_RDONLY | O_NONBLOCK,
                                0666, &queue_attributes);
    app->ipc.player_queue = mq_open(PLAYER_QUEUE_NAME, O_WRONLY | O_NONBLOCK);
    ipc_assert_wired_safety(app);
}

static bool json_extract_int(const char *json, const char *key, int32_t *out_value) {
    const char *found = strstr(json, key);
    if (found == NULL) {
        return false;
    }
    found += strlen(key);
    while (*found == ' ' || *found == ':') {
        found++;
    }
    *out_value = (int32_t)strtol(found, NULL, 10);
    return true;
}

static bool path_matches_library(const TrackLibrary *tracks, const char *json) {
    for (uint32_t track_index = 0u; track_index < tracks->count; ++track_index) {
        if (strstr(json, tracks->names[track_index]) != NULL ||
            strstr(json, tracks->paths[track_index]) != NULL) {
            return true;
        }
    }
    return false;
}

static void handle_ui_frame(BenchApp *app, const char *frame) {
    app->ipc.rx_count++;
    log_ipc_line(app, "RX", frame);

    if (strncmp(frame, "a103", 4) == 0 || strncmp(frame, "0103", 4) == 0) {
        if (strlen(frame) >= 16u) {
            app->ipc.position_ms = (uint32_t)strtoul(frame + 8, NULL, 16);
        }
        return;
    }

    snprintf(app->ipc.last_reply, sizeof(app->ipc.last_reply), "%.155s", frame);

    if (strncmp(frame, "a607", 4) == 0 || strncmp(frame, "a620", 4) == 0) {
        app->ipc.wired_route_sent = false;
        ipc_assert_wired_safety(app);
        return;
    }

    if (strncmp(frame, "a60a000C0223", 12) == 0) {
        app->ipc.eof_seen = true;
        app->ipc.playing = false;
        return;
    }

    if (strncmp(frame, "a202", 4) == 0 || strncmp(frame, "a102", 4) == 0) {
        if (strlen(frame) <= 8u) {
            return;
        }
        /* Q14: JSON payload begins at frame + 8 */
        const char *json = frame + 8;
        int32_t state_value = -1;
        json_extract_int(json, "\"state\"", &state_value);

        if (strstr(json, "\"song\"") != NULL) {
            int32_t duration_value = 0;
            if (json_extract_int(json, "\"song_duration_time\"", &duration_value) &&
                duration_value > 0) {
                app->ipc.duration_ms = (uint32_t)duration_value;
            }
            /* Q02: with "song" present, state == 2 means actively playing */
            app->ipc.playing = (state_value != 1);
            app->ipc.paused = (state_value == 1);
            snprintf(app->ipc.last_song_hint, sizeof(app->ipc.last_song_hint),
                     "%.155s", json);
            if (app->tracks.count > 0u && !path_matches_library(&app->tracks, json)) {
                app->ipc.song_db_leak_seen = true;
            }
        } else {
            if (state_value == 0) {
                app->ipc.playing = true;
                app->ipc.paused = false;
            } else if (state_value == 1) {
                app->ipc.playing = false;
                app->ipc.paused = true;
            } else if (state_value == 2) {
                app->ipc.playing = false;
                app->ipc.paused = false;
                app->ipc.eof_seen = true;
            }
            /* Q17: state == 3 teardown is ignored */
        }
    }
}

static void ipc_poll_ui(BenchApp *app, uint32_t max_frames) {
    if (app->ipc.ui_queue == (mqd_t)-1) {
        return;
    }
    if (app->ipc.player_queue == (mqd_t)-1) {
        app->ipc.player_queue = mq_open(PLAYER_QUEUE_NAME, O_WRONLY | O_NONBLOCK);
        ipc_assert_wired_safety(app);
    }
    char recv_buffer[MQ_RECV_BUFFER_SIZE];
    for (uint32_t frame_index = 0u; frame_index < max_frames; ++frame_index) {
        ssize_t received = mq_receive(app->ipc.ui_queue, recv_buffer,
                                      sizeof(recv_buffer) - 1u, NULL);
        if (received < 0) {
            break;
        }
        recv_buffer[received] = '\0';
        handle_ui_frame(app, recv_buffer);
    }
}

static bool write_m3u_and_play(BenchApp *app, char slice_tag,
                               const uint32_t *track_indices, uint32_t track_count) {
    (void)slice_tag;
    static uint32_t m3u_sequence = 0u;
    uint32_t previous_sequence = m3u_sequence;
    m3u_sequence = (m3u_sequence + 1u) & 0xFFu;

    char previous_disk_path[64];
    char disk_m3u_path[64];
    char ipc_m3u_path[64];
    snprintf(previous_disk_path, sizeof(previous_disk_path), "/tmp/dialos_play_%u.m3u", previous_sequence);
    (void)unlink(previous_disk_path);
    snprintf(disk_m3u_path, sizeof(disk_m3u_path), "/tmp/dialos_play_%u.m3u", m3u_sequence);
    snprintf(ipc_m3u_path, sizeof(ipc_m3u_path), "//tmp/dialos_play_%u.m3u", m3u_sequence);

    FILE *m3u_file = fopen(disk_m3u_path, "w");
    if (m3u_file == NULL) {
        return false;
    }
    fprintf(m3u_file, "#EXTM3U\n");
    for (uint32_t list_index = 0u; list_index < track_count; ++list_index) {
        uint32_t track_idx = track_indices[list_index];
        if (track_idx < app->tracks.count) {
            fprintf(m3u_file, "%s\n", app->tracks.paths[track_idx]);
        }
    }
    fclose(m3u_file);

    /* V2.57 wire format: 0100<len>00000004//tmp/dialos_play_<seq>.m3u */
    char frame[128];
    uint32_t total_length = (uint32_t)(16u + strlen(ipc_m3u_path));
    snprintf(frame, sizeof(frame), "0100%04X00000004%s", total_length & 0xFFFFu, ipc_m3u_path);
    ipc_assert_wired_safety(app);
    ipc_send_frame(app, frame);
    return true;
}

/* =========================================================================
 * Input Handling (/dev/input/event0 & /dev/input/event1)
 * ========================================================================= */

static void init_input(InputState *input) {
    input->key_fd = open(KEY_DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    input->touch_fd = open(TOUCH_DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    input->touch_x = SCREEN_CENTER_X;
    input->touch_y = SCREEN_CENTER_Y;
}

static void poll_input(InputState *input) {
    input->touch_pressed_edge = false;
    input->touch_released_edge = false;
    input->backup_long_play_edge = false;
    input->sample_ready = false;
    input->syn_count_in_poll = 0u;

    struct input_event events[32];
    if (input->touch_fd >= 0) {
        for (;;) {
            ssize_t bytes_read = read(input->touch_fd, events, sizeof(events));
            if (bytes_read <= 0) {
                break;
            }
            size_t event_count = (size_t)bytes_read / sizeof(struct input_event);
            for (size_t event_index = 0u; event_index < event_count; ++event_index) {
                const struct input_event *ev = &events[event_index];
                if (ev->type == EV_ABS) {
                    if (ev->code == ABS_MT_TRACKING_ID) {
                        if (ev->value >= 0) {
                            if (!input->touch_down) {
                                input->touch_pressed_edge = true;
                            }
                            input->touch_down = true;
                        } else {
                            if (input->touch_down) {
                                input->touch_released_edge = true;
                            }
                            input->touch_down = false;
                        }
                    } else if (ev->code == ABS_MT_POSITION_X) {
                        int32_t inverted_x = TOUCH_COORD_MAX - ev->value;
                        if (inverted_x < 0) {
                            inverted_x = 0;
                        } else if (inverted_x > TOUCH_COORD_MAX) {
                            inverted_x = TOUCH_COORD_MAX;
                        }
                        input->touch_x = inverted_x;
                    } else if (ev->code == ABS_MT_POSITION_Y) {
                        int32_t inverted_y = TOUCH_COORD_MAX - ev->value;
                        if (inverted_y < 0) {
                            inverted_y = 0;
                        } else if (inverted_y > TOUCH_COORD_MAX) {
                            inverted_y = TOUCH_COORD_MAX;
                        }
                        input->touch_y = inverted_y;
                    }
                } else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
                    input->sample_ready = true;
                    input->syn_count_in_poll++;
                    input->sample_time_ms = monotonic_ms();
                }
            }
        }
    }

    if (input->key_fd >= 0) {
        for (;;) {
            ssize_t bytes_read = read(input->key_fd, events, sizeof(events));
            if (bytes_read <= 0) {
                break;
            }
            size_t event_count = (size_t)bytes_read / sizeof(struct input_event);
            for (size_t event_index = 0u; event_index < event_count; ++event_index) {
                const struct input_event *ev = &events[event_index];
                if (ev->type == EV_KEY && ev->code == X2000_KEY_PLAY_LONG &&
                    ev->value == 1) {
                    input->backup_long_play_edge = true;
                }
            }
        }
    }
}

static void drain_stale_input(InputState *input) {
    struct input_event events[32];
    if (input->touch_fd >= 0) {
        while (read(input->touch_fd, events, sizeof(events)) > 0) {
        }
    }
    if (input->key_fd >= 0) {
        while (read(input->key_fd, events, sizeof(events)) > 0) {
        }
    }
    input->touch_down = false;
    input->touch_pressed_edge = false;
    input->touch_released_edge = false;
    input->backup_long_play_edge = false;
    input->sample_ready = false;
    input->syn_count_in_poll = 0u;
}

static bool point_in_rect(int32_t pos_x, int32_t pos_y,
                          int32_t rect_x, int32_t rect_y,
                          int32_t rect_w, int32_t rect_h) {
    return pos_x >= rect_x && pos_x < (rect_x + rect_w) &&
           pos_y >= rect_y && pos_y < (rect_y + rect_h);
}

/* =========================================================================
 * Read-Only Audio Track Discovery
 * ========================================================================= */

static bool has_audio_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (dot == NULL) {
        return false;
    }
    return strcasecmp(dot, ".flac") == 0 ||
           strcasecmp(dot, ".wav") == 0 ||
           strcasecmp(dot, ".mp3") == 0;
}

static void scan_audio_directory(TrackLibrary *tracks, const char *directory_path,
                                 uint32_t max_depth) {
    if (tracks->count >= MAX_TRACKS) {
        return;
    }
    DIR *directory_handle = opendir(directory_path);
    if (directory_handle == NULL) {
        return;
    }
    struct dirent *entry = NULL;
    while ((entry = readdir(directory_handle)) != NULL && tracks->count < MAX_TRACKS) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory_path, entry->d_name);

        struct stat entry_stat;
        if (stat(full_path, &entry_stat) != 0) {
            continue;
        }
        if (S_ISDIR(entry_stat.st_mode)) {
            if (max_depth > 0u) {
                scan_audio_directory(tracks, full_path, max_depth - 1u);
            }
        } else if (S_ISREG(entry_stat.st_mode) && has_audio_extension(entry->d_name)) {
            uint32_t idx = tracks->count;
            snprintf(tracks->paths[idx], sizeof(tracks->paths[idx]), "%s", full_path);
            snprintf(tracks->names[idx], sizeof(tracks->names[idx]), "%s", entry->d_name);
            tracks->count++;
        }
    }
    closedir(directory_handle);
}

static void discover_test_tracks(TrackLibrary *tracks) {
    tracks->count = 0u;
    scan_audio_directory(tracks, SD_TEST_AUDIO_DIR, 1u);
    if (tracks->count < 2u) {
        scan_audio_directory(tracks, SD_FALLBACK_AUDIO_DIR, 2u);
    }
}

/* =========================================================================
 * UI Chrome Rendering (Header, Result Summary, Bottom Buttons)
 * ========================================================================= */

static const char *STAGE_TITLES[STAGE_COUNT] = {
    "M0 1/5: ROTATION",
    "M0 2/5: RIM TOUCH",
    "M0 3/5: HANDOFF",
    "M0 4/5: EQ PROBE",
    "M0 5/5: IPC TRACE"
};

static void draw_header(BenchApp *app) {
    canvas_fill_rect(app, 0, 0, (int32_t)SCREEN_WIDTH, 62, COLOR_HEADER_BG);
    canvas_draw_centered(app, 18, STAGE_TITLES[app->current_stage], COLOR_ACCENT, 2u);

    char status_line[48];
    const char *play_label = app->ipc.playing ? "PLAY"
                             : (app->ipc.paused ? "PAUSE" : "STOP");
    uint32_t total_seconds = app->ipc.position_ms / 1000u;
    snprintf(status_line, sizeof(status_line), "%s %02u:%02u | TRK:%u",
             play_label, total_seconds / 60u, total_seconds % 60u,
             app->tracks.count);
    uint32_t status_color = app->ipc.playing ? COLOR_GREEN : COLOR_MUTED;
    canvas_draw_centered(app, 42, status_line, status_color, 1u);
}

static void draw_bottom_buttons(BenchApp *app, bool locked) {
    if (locked) {
        canvas_draw_centered(app, 304, "360 RIM FREE - LOCKED", COLOR_MUTED, 1u);
        return;
    }
    canvas_fill_rect(app, BTN_RUN_X, BTN_RUN_Y, BTN_WIDTH, BTN_HEIGHT, COLOR_BTN_RUN);
    canvas_draw_text(app, BTN_RUN_X + 35, BTN_RUN_Y + 15, "RUN", COLOR_BTN_TEXT, 2u);

    bool is_last = (app->current_stage == STAGE_COUNT - 1u);
    uint32_t next_color = is_last ? COLOR_BTN_DONE : COLOR_BTN_NEXT;
    uint32_t text_color = is_last ? COLOR_BTN_DARK_TEXT : COLOR_BTN_TEXT;
    const char *next_label = is_last ? "DONE" : "NEXT";
    canvas_fill_rect(app, BTN_NEXT_X, BTN_NEXT_Y, BTN_WIDTH, BTN_HEIGHT, next_color);
    canvas_draw_text(app, BTN_NEXT_X + 27, BTN_NEXT_Y + 15, next_label, text_color, 2u);
}

static void draw_idle_or_summary(BenchApp *app) {
    canvas_clear(app, COLOR_BG);
    draw_header(app);

    const StageSummary *summary = &app->summaries[app->current_stage];
    if (summary->status == STATUS_IDLE) {
        canvas_draw_centered(app, 130, "TAP RUN TO START", COLOR_TEXT, 1u);
        if (app->tracks.count > 0u) {
            char track_info[48];
            snprintf(track_info, sizeof(track_info), "T1: %.32s", app->tracks.names[0]);
            canvas_draw_centered(app, 160, track_info, COLOR_MUTED, 1u);
        } else {
            canvas_draw_centered(app, 160, "NO AUDIO FOUND ON SD", COLOR_RED, 1u);
        }
        canvas_draw_centered(app, 195, "HOLD PLAY >=1s BACKUP", COLOR_MUTED, 1u);
    } else {
        const char *badge_text = "LOGGED";
        uint32_t badge_color = COLOR_AMBER;
        if (summary->status == STATUS_PASS) {
            badge_text = "PASS";
            badge_color = COLOR_GREEN;
        } else if (summary->status == STATUS_FAIL) {
            badge_text = "FAIL";
            badge_color = COLOR_RED;
        }
        canvas_draw_centered(app, 82, badge_text, badge_color, 2u);

        int32_t line_y = 112;
        for (uint32_t line_idx = 0u; line_idx < summary->line_count; ++line_idx) {
            canvas_draw_text(app, 32, line_y, summary->lines[line_idx], COLOR_TEXT, 1u);
            line_y += 20;
        }

        if (app->all_done_synced) {
            canvas_draw_centered(app, 248, "ALL 5 STAGES SYNCED!", COLOR_GREEN, 1u);
        } else {
            canvas_draw_centered(app, 248, "SAVED TO SD", COLOR_GREEN, 1u);
        }
    }

    draw_bottom_buttons(app, false);
    fb_blit_180(app, 0u);
    fb_pan_page(app, 0u);
}

/* =========================================================================
 * Stage 1: 360x360 Fixed-Point Rotation & Tearing
 * ========================================================================= */

static void init_rotation_assets(BenchApp *app) {
    const double two_pi = 6.283185307179586;
    for (uint32_t lut_idx = 0u; lut_idx < TRIG_LUT_SIZE; ++lut_idx) {
        double angle = two_pi * (double)lut_idx / (double)TRIG_LUT_SIZE;
        app->sin_lut[lut_idx] = (int32_t)(sin(angle) * (double)FIXED_ONE);
        app->cos_lut[lut_idx] = (int32_t)(cos(angle) * (double)FIXED_ONE);
    }

    for (uint32_t pos_y = 0u; pos_y < SCREEN_HEIGHT; ++pos_y) {
        int32_t delta_y = (int32_t)pos_y - SCREEN_CENTER_Y;
        for (uint32_t pos_x = 0u; pos_x < SCREEN_WIDTH; ++pos_x) {
            int32_t delta_x = (int32_t)pos_x - SCREEN_CENTER_X;
            int32_t dist_sq = delta_x * delta_x + delta_y * delta_y;
            uint32_t idx = pos_y * SCREEN_WIDTH + pos_x;
            app->disc_mask[idx] =
                (dist_sq <= DISC_MASK_RADIUS * DISC_MASK_RADIUS) ? 1u : 0u;

            uint32_t texel = COLOR_BG;
            if (dist_sq <= 22 * 22) {
                texel = COLOR_BG;
            } else if (dist_sq <= 104 * 104) {
                if (delta_x >= 0 && delta_y < 0) {
                    texel = 0xFF2B59C3u;
                } else if (delta_x < 0 && delta_y >= 0) {
                    texel = COLOR_GREEN;
                } else {
                    texel = 0xFF1B263Bu;
                }
                if (abs(delta_x) < 3 && delta_y < 0) {
                    texel = COLOR_TEXT;
                }
            } else if (dist_sq <= 108 * 108) {
                texel = COLOR_ACCENT;
            }
            app->texture[idx] = texel;
        }
    }
}

static uint32_t read_alsa_xruns(bool *readable) {
    FILE *status_file = fopen(ASOUND_STATUS_PATH, "r");
    if (status_file == NULL) {
        *readable = false;
        return 0u;
    }
    *readable = true;
    char line[128];
    uint32_t xruns = 0u;
    while (fgets(line, sizeof(line), status_file) != NULL) {
        if (strstr(line, "xrun") != NULL) {
            xruns++;
        }
    }
    fclose(status_file);
    return xruns;
}

static void render_rotated_disc_frame(BenchApp *app, uint32_t angle_step,
                                      uint32_t tear_bar_x) {
    int32_t sin_val = app->sin_lut[angle_step & TRIG_LUT_MASK];
    int32_t cos_val = app->cos_lut[angle_step & TRIG_LUT_MASK];
    int32_t center_fixed = SCREEN_CENTER_X * FIXED_ONE;

    for (uint32_t pos_y = 0u; pos_y < SCREEN_HEIGHT; ++pos_y) {
        int32_t delta_y = (int32_t)pos_y - SCREEN_CENTER_Y;
        int32_t row_u = center_fixed - delta_y * sin_val - SCREEN_CENTER_X * cos_val;
        int32_t row_v = center_fixed + delta_y * cos_val - SCREEN_CENTER_X * sin_val;
        uint32_t row_offset = pos_y * SCREEN_WIDTH;

        for (uint32_t pos_x = 0u; pos_x < SCREEN_WIDTH; ++pos_x) {
            uint32_t pixel_index = row_offset + pos_x;
            if (app->disc_mask[pixel_index] == 0u) {
                app->canvas[pixel_index] = 0xFF050608u;
            } else {
                int32_t tex_x = row_u >> FIXED_SHIFT;
                int32_t tex_y = row_v >> FIXED_SHIFT;
                if ((uint32_t)tex_x < SCREEN_WIDTH && (uint32_t)tex_y < SCREEN_HEIGHT) {
                    app->canvas[pixel_index] =
                        app->texture[(uint32_t)tex_y * SCREEN_WIDTH + (uint32_t)tex_x];
                } else {
                    app->canvas[pixel_index] = COLOR_BG;
                }
            }
            row_u += cos_val;
            row_v += sin_val;
        }
    }

    canvas_fill_rect(app, (int32_t)tear_bar_x, 64, 8, 224, COLOR_RED);
}

static int compare_uint32(const void *first, const void *second) {
    uint32_t val_a = *(const uint32_t *)first;
    uint32_t val_b = *(const uint32_t *)second;
    if (val_a < val_b) {
        return -1;
    }
    return (val_a > val_b) ? 1 : 0;
}

typedef struct {
    double avg_fps;
    double min_fps;
    uint32_t p99_ms;
    uint32_t frame_count;
} RotationPhaseMetrics;

static void draw_running_progress(BenchApp *app, const char *step_title,
                                  const char *detail_line, uint32_t progress_percent,
                                  uint32_t elapsed_ms) {
    static const char spinner_frames[4] = {'|', '/', '-', '\\'};
    char spinner_char = spinner_frames[(elapsed_ms / 150u) & 3u];

    canvas_clear(app, COLOR_BG);
    draw_header(app);

    char running_banner[48];
    snprintf(running_banner, sizeof(running_banner), "RUNNING %c  %u.%us",
             spinner_char, elapsed_ms / 1000u, (elapsed_ms % 1000u) / 100u);
    canvas_draw_centered(app, 94, running_banner, COLOR_AMBER, 2u);

    canvas_draw_centered(app, 136, step_title, COLOR_TEXT, 1u);
    if (detail_line != NULL && detail_line[0] != '\0') {
        canvas_draw_centered(app, 158, detail_line, COLOR_MUTED, 1u);
    }

    /* Animated horizontal progress bar */
    uint32_t clamped_percent = (progress_percent > 100u) ? 100u : progress_percent;
    int32_t bar_x = 56;
    int32_t bar_y = 190;
    int32_t bar_w = 248;
    int32_t bar_h = 18;
    canvas_fill_rect(app, bar_x, bar_y, bar_w, bar_h, 0xFF1F242Eu);
    int32_t fill_w = (int32_t)(((uint32_t)(bar_w - 4) * clamped_percent) / 100u);
    if (fill_w > 0) {
        canvas_fill_rect(app, bar_x + 2, bar_y + 2, fill_w, bar_h - 4, COLOR_ACCENT);
    }

    char stats_line[48];
    snprintf(stats_line, sizeof(stats_line), "TX:%llu RX:%llu | %u%%",
             (unsigned long long)app->ipc.tx_count,
             (unsigned long long)app->ipc.rx_count,
             clamped_percent);
    canvas_draw_centered(app, 224, stats_line, COLOR_GREEN, 1u);

    canvas_draw_centered(app, 298, "PLEASE WAIT...", COLOR_MUTED, 1u);
    fb_blit_180(app, 0u);
}

static void sleep_with_progress(BenchApp *app, uint32_t total_ms,
                                const char *step_title, const char *detail_line,
                                uint32_t start_percent, uint32_t end_percent) {
    uint64_t wait_start = monotonic_ms();
    uint64_t last_draw = 0ULL;
    while (true) {
        uint64_t now_ms = monotonic_ms();
        uint32_t elapsed = (uint32_t)(now_ms - wait_start);
        ipc_poll_ui(app, 16u);
        if (last_draw == 0ULL || (now_ms - last_draw) >= 60ULL) {
            last_draw = now_ms;
            uint32_t percent = start_percent;
            if (total_ms > 0u && end_percent > start_percent) {
                uint32_t clamped = (elapsed > total_ms) ? total_ms : elapsed;
                percent = start_percent + ((end_percent - start_percent) * clamped) / total_ms;
            }
            draw_running_progress(app, step_title, detail_line, percent, elapsed);
        }
        if (elapsed >= total_ms) {
            break;
        }
        sleep_ms(20u);
    }
}

static RotationPhaseMetrics run_rotation_phase(BenchApp *app, bool triple_buffer,
                                               uint32_t duration_ms) {
    uint32_t frame_times[MAX_FRAME_TIMINGS];
    uint32_t frame_count = 0u;
    uint32_t page_index = 0u;
    uint32_t angle_step = 0u;
    uint32_t max_frame_ms = 1u;
    uint64_t phase_start = monotonic_ms();

    while ((monotonic_ms() - phase_start) < duration_ms) {
        uint64_t frame_start = monotonic_ms();
        uint32_t phase_elapsed_ms = (uint32_t)(frame_start - phase_start);
        ipc_poll_ui(app, 8u);

        /* Smooth time-paced tear bar (140 px/s) so tearing is clearly visible even at 290 fps */
        uint32_t tear_x = ((phase_elapsed_ms * 140u) / 1000u) % (SCREEN_WIDTH - 16u);
        render_rotated_disc_frame(app, angle_step, tear_x);
        draw_header(app);

        if (triple_buffer) {
            page_index = (page_index + 1u) % FB_PAGE_COUNT;
            fb_blit_180(app, page_index);
            fb_pan_page(app, page_index);
        } else {
            fb_blit_180(app, 0u);
        }

        uint32_t elapsed_ms = (uint32_t)(monotonic_ms() - frame_start);
        if (elapsed_ms == 0u) {
            elapsed_ms = 1u;
        }
        if (elapsed_ms > max_frame_ms) {
            max_frame_ms = elapsed_ms;
        }
        if (frame_count < MAX_FRAME_TIMINGS) {
            frame_times[frame_count++] = elapsed_ms;
        }
        angle_step = (angle_step + 8u) & TRIG_LUT_MASK;
    }

    uint64_t total_ms = monotonic_ms() - phase_start;
    RotationPhaseMetrics metrics;
    metrics.frame_count = frame_count;
    metrics.avg_fps = (total_ms > 0ULL) ? ((double)frame_count * 1000.0 / (double)total_ms) : 0.0;
    metrics.min_fps = 1000.0 / (double)max_frame_ms;
    metrics.p99_ms = max_frame_ms;
    if (frame_count > 0u) {
        qsort(frame_times, frame_count, sizeof(uint32_t), compare_uint32);
        uint32_t p99_idx = (frame_count * 99u) / 100u;
        if (p99_idx >= frame_count) {
            p99_idx = frame_count - 1u;
        }
        metrics.p99_ms = frame_times[p99_idx];
    }
    return metrics;
}

static void run_stage_1_rotation(BenchApp *app) {
    if (app->tracks.count < 2u) {
        discover_test_tracks(&app->tracks);
    }
    if (!app->ipc.playing && app->tracks.count > 0u) {
        uint32_t track_zero = 0u;
        write_m3u_and_play(app, 'A', &track_zero, 1u);
        uint64_t wait_start = monotonic_ms();
        uint64_t last_draw = 0ULL;
        while ((monotonic_ms() - wait_start) < 2500ULL) {
            ipc_poll_ui(app, 16u);
            if (app->ipc.playing) {
                break;
            }
            uint64_t now_ms = monotonic_ms();
            if (last_draw == 0ULL || (now_ms - last_draw) >= 60ULL) {
                last_draw = now_ms;
                uint32_t elapsed = (uint32_t)(now_ms - wait_start);
                draw_running_progress(app, "STARTING WIRED AUDIO", app->tracks.names[0],
                                      (elapsed * 100u) / 2500u, elapsed);
            }
            sleep_ms(20u);
        }
    }

    bool alsa_readable = false;
    uint32_t xruns_before = read_alsa_xruns(&alsa_readable);

    fb_pan_page(app, 0u);
    RotationPhaseMetrics direct = run_rotation_phase(app, false, 5000u);
    RotationPhaseMetrics triple = run_rotation_phase(app, true, 5000u);
    fb_pan_page(app, 0u);

    uint32_t xruns_after = read_alsa_xruns(&alsa_readable);
    uint32_t xrun_delta = (xruns_after >= xruns_before) ? (xruns_after - xruns_before) : 0u;

    StageSummary *summary = &app->summaries[0];
    summary->status = (direct.avg_fps >= 60.0 || triple.avg_fps >= 60.0)
                          ? STATUS_PASS
                          : STATUS_FAIL;
    snprintf(summary->lines[0], 48, "1-PG: %.1f avg %.1f min p99:%ums",
             direct.avg_fps, direct.min_fps, direct.p99_ms);
    snprintf(summary->lines[1], 48, "3-PG: %.1f avg %.1f min p99:%ums",
             triple.avg_fps, triple.min_fps, triple.p99_ms);
    if (alsa_readable) {
        snprintf(summary->lines[2], 48, "ALSA XRUN DELTA: %u", xrun_delta);
    } else {
        snprintf(summary->lines[2], 48, "ALSA XRUN: N/A (listen)");
    }
    summary->line_count = 3u;

    if (app->report_file != NULL) {
        fprintf(app->report_file, "[Stage 1: Rotation & Tearing]\n");
        fprintf(app->report_file, "  single_page: avg=%.2f fps, min=%.2f fps, p99=%u ms (%u frames)\n",
                direct.avg_fps, direct.min_fps, direct.p99_ms, direct.frame_count);
        fprintf(app->report_file, "  triple_pan : avg=%.2f fps, min=%.2f fps, p99=%u ms (%u frames)\n",
                triple.avg_fps, triple.min_fps, triple.p99_ms, triple.frame_count);
        if (alsa_readable) {
            fprintf(app->report_file, "  alsa_xrun_delta: %u\n\n", xrun_delta);
        } else {
            fprintf(app->report_file, "  alsa_xrun_delta: unreadable\n\n");
        }
    }
    flush_logs_to_sd(app);
}

/* =========================================================================
 * Stage 2: Rim Touch Sample Rate (10s Button Lockout)
 * ========================================================================= */

static void run_stage_2_touch(BenchApp *app) {
    uint64_t start_ms = monotonic_ms();
    uint64_t last_sample_ms = 0ULL;
    uint64_t active_touch_ms = 0ULL;
    uint32_t sample_count = 0u;
    uint32_t max_gap_ms = 0u;
    uint32_t dropouts = 0u;
    int32_t min_x = 359, max_x = 0, min_y = 359, max_y = 0;
    uint64_t last_draw_ms = 0ULL;

    while ((monotonic_ms() - start_ms) < 10000ULL) {
        poll_input(&app->input);
        ipc_poll_ui(app, 4u);

        if (app->input.syn_count_in_poll > 0u && app->input.touch_down) {
            sample_count += app->input.syn_count_in_poll;
            if (last_sample_ms != 0ULL) {
                uint32_t gap_ms = (uint32_t)(app->input.sample_time_ms - last_sample_ms);
                active_touch_ms += gap_ms;
                if (gap_ms > max_gap_ms) {
                    max_gap_ms = gap_ms;
                }
                if (gap_ms > 50u) {
                    dropouts++;
                }
            }
            last_sample_ms = app->input.sample_time_ms;
            if (app->input.touch_x < min_x) {
                min_x = app->input.touch_x;
            }
            if (app->input.touch_x > max_x) {
                max_x = app->input.touch_x;
            }
            if (app->input.touch_y < min_y) {
                min_y = app->input.touch_y;
            }
            if (app->input.touch_y > max_y) {
                max_y = app->input.touch_y;
            }
        } else if (!app->input.touch_down) {
            last_sample_ms = 0ULL;
        }

        uint64_t now_ms = monotonic_ms();
        if ((now_ms - last_draw_ms) >= 50ULL) {
            last_draw_ms = now_ms;
            canvas_clear(app, COLOR_BG);
            canvas_draw_ring(app, SCREEN_CENTER_X, SCREEN_CENTER_Y, 152, 172, 0xFF3A2E14u);
            if (app->input.touch_down) {
                canvas_fill_circle(app, app->input.touch_x, app->input.touch_y,
                                   12, COLOR_GREEN);
            }
            draw_header(app);
            uint32_t elapsed_sec = (uint32_t)((now_ms - start_ms) / 1000ULL);
            uint32_t remaining_sec = (elapsed_sec < 10u) ? (10u - elapsed_sec) : 0u;
            double live_hz = (now_ms > start_ms)
                                 ? ((double)sample_count * 1000.0 / (double)(now_ms - start_ms))
                                 : 0.0;
            char status_text[48];
            canvas_draw_centered(app, 128, "TRACE OUTER RIM", COLOR_TEXT, 2u);
            snprintf(status_text, sizeof(status_text), "%.1f Hz (%u)", live_hz, sample_count);
            canvas_draw_centered(app, 164, status_text, COLOR_GREEN, 2u);
            snprintf(status_text, sizeof(status_text), "TIME LEFT: %u s", remaining_sec);
            canvas_draw_centered(app, 204, status_text, COLOR_AMBER, 1u);
            draw_bottom_buttons(app, true);
            fb_blit_180(app, 0u);
        }
        sleep_ms(2u);
    }

    double window_hz = (double)sample_count / 10.0;
    double active_hz = (active_touch_ms > 0ULL)
                           ? ((double)sample_count * 1000.0 / (double)active_touch_ms)
                           : 0.0;
    if (sample_count == 0u) {
        min_x = 0;
        min_y = 0;
    }

    StageSummary *summary = &app->summaries[1];
    summary->status = (active_hz >= 60.0 && dropouts == 0u) ? STATUS_PASS : STATUS_FAIL;
    snprintf(summary->lines[0], 48, "ACT:%.1fHz | 10S:%.1fHz", active_hz, window_hz);
    snprintf(summary->lines[1], 48, "N:%u GAP:%ums DROP:%u", sample_count, max_gap_ms, dropouts);
    snprintf(summary->lines[2], 48, "X:%d..%d  Y:%d..%d", min_x, max_x, min_y, max_y);
    summary->line_count = 3u;

    if (app->report_file != NULL) {
        fprintf(app->report_file, "[Stage 2: Rim Touch Rate]\n");
        fprintf(app->report_file,
                "  samples=%u, active_hz=%.2f, window_hz=%.2f, active_ms=%llu, max_gap_ms=%u, dropouts_gt_50ms=%u\n",
                sample_count, active_hz, window_hz, (unsigned long long)active_touch_ms,
                max_gap_ms, dropouts);
        fprintf(app->report_file, "  bounds: x=%d..%d, y=%d..%d\n\n",
                min_x, max_x, min_y, max_y);
    }
    flush_logs_to_sd(app);
}

/* =========================================================================
 * Stage 3: Rolling-Slice Handoff
 * ========================================================================= */

static uint32_t wait_for_track_start_ms(BenchApp *app, uint64_t tx_time_ms,
                                        uint32_t timeout_ms, const char *step_title,
                                        uint32_t start_percent, uint32_t end_percent) {
    uint64_t prev_rx = app->ipc.rx_count;
    uint64_t last_draw = 0ULL;
    while (true) {
        uint64_t now_ms = monotonic_ms();
        uint32_t elapsed = (uint32_t)(now_ms - tx_time_ms);
        ipc_poll_ui(app, 16u);
        if (app->ipc.rx_count > prev_rx && app->ipc.playing) {
            return (elapsed > 0u) ? elapsed : 1u;
        }
        if (last_draw == 0ULL || (now_ms - last_draw) >= 60ULL) {
            last_draw = now_ms;
            uint32_t clamped = (elapsed > timeout_ms) ? timeout_ms : elapsed;
            uint32_t pct = start_percent + ((end_percent - start_percent) * clamped) / timeout_ms;
            draw_running_progress(app, step_title, "WAITING FOR MQ_PLAYER ACK...", pct, elapsed);
        }
        if (elapsed >= timeout_ms) {
            break;
        }
        sleep_ms(15u);
    }
    return 0u;
}

static void run_stage_3_handoff(BenchApp *app) {
    if (app->tracks.count < 2u) {
        discover_test_tracks(&app->tracks);
    }
    StageSummary *summary = &app->summaries[2];
    if (app->tracks.count < 2u) {
        summary->status = STATUS_FAIL;
        snprintf(summary->lines[0], 48, "NEED >=2 TRACKS ON SD");
        summary->line_count = 1u;
        if (app->report_file != NULL) {
            fprintf(app->report_file, "[Stage 3: Rolling-Slice Handoff]\n  FAIL: <2 tracks\n\n");
        }
        flush_logs_to_sd(app);
        return;
    }

    app->ipc.song_db_leak_seen = false;
    uint32_t slice_a[2] = {0u, 1u};
    uint32_t slice_b[1] = {1u};

    draw_running_progress(app, "STEP 1/4: M3U SLICE A", app->tracks.names[0], 5u, 0u);
    uint64_t tx_a = monotonic_ms();
    write_m3u_and_play(app, 'A', slice_a, 2u);
    uint32_t lat_a = wait_for_track_start_ms(app, tx_a, 3000u, "STEP 1/4: M3U SLICE A", 5u, 25u);

    /* Seek near end of track 1 in multi-track M3U A to test natural advance */
    bool natural_advance_ok = false;
    if (app->ipc.duration_ms > 4000u) {
        char seek_frame[32];
        snprintf(seek_frame, sizeof(seek_frame), "01030010%08X",
                 app->ipc.duration_ms - 3000u);
        ipc_send_frame(app, seek_frame);
        uint64_t wait_start = monotonic_ms();
        uint64_t last_draw = 0ULL;
        while ((monotonic_ms() - wait_start) < 6000ULL) {
            ipc_poll_ui(app, 16u);
            if (strstr(app->ipc.last_song_hint, app->tracks.names[1]) != NULL) {
                natural_advance_ok = true;
                break;
            }
            uint64_t now_ms = monotonic_ms();
            if (last_draw == 0ULL || (now_ms - last_draw) >= 60ULL) {
                last_draw = now_ms;
                uint32_t elapsed = (uint32_t)(now_ms - wait_start);
                draw_running_progress(app, "STEP 2/4: NATURAL EOF", "WAITING FOR TRACK 2...",
                                      25u + (elapsed * 30u) / 6000u, elapsed);
            }
            sleep_ms(20u);
        }
    }

    /* Boundary swap A -> B */
    uint64_t tx_b = monotonic_ms();
    write_m3u_and_play(app, 'B', slice_b, 1u);
    uint32_t lat_b = wait_for_track_start_ms(app, tx_b, 3000u, "STEP 3/4: SWAP SLICE B", 55u, 78u);

    /* Q17: Rapid < 2 s switch B -> A */
    sleep_with_progress(app, 400u, "STEP 4/4: RAPID <2S TEST", "TESTING Q17 RATE LIMIT", 78u, 84u);
    uint64_t tx_rapid = monotonic_ms();
    write_m3u_and_play(app, 'A', slice_a, 1u);
    uint32_t lat_rapid = wait_for_track_start_ms(app, tx_rapid, 2500u, "STEP 4/4: RAPID <2S TEST", 84u, 100u);

    summary->status = (lat_a > 0u && lat_b > 0u && !app->ipc.song_db_leak_seen)
                          ? STATUS_PASS
                          : STATUS_FAIL;
    snprintf(summary->lines[0], 48, "M3U A: %u ms | B: %u ms", lat_a, lat_b);
    snprintf(summary->lines[1], 48, "NAT ADV: %s | <2s: %s",
             natural_advance_ok ? "OK" : "NO",
             (lat_rapid > 0u) ? "OK" : "DROP(Q17)");
    snprintf(summary->lines[2], 48, "song.db LEAK: %s",
             app->ipc.song_db_leak_seen ? "YES" : "NONE");
    summary->line_count = 3u;

    if (app->report_file != NULL) {
        fprintf(app->report_file, "[Stage 3: Rolling-Slice Handoff]\n");
        fprintf(app->report_file, "  m3u_a_ms=%u, m3u_b_ms=%u, natural_advance=%s, rapid_lt_2s_ms=%u, song_db_leak=%s\n\n",
                lat_a, lat_b, natural_advance_ok ? "OK" : "NO",
                lat_rapid, app->ipc.song_db_leak_seen ? "YES" : "NONE");
    }
    flush_logs_to_sd(app);
}

/* =========================================================================
 * Stage 4: V2.57 EQ / PEQ Probe
 * ========================================================================= */

static void send_eq_probe(BenchApp *app, const char *frame, const char *step_title,
                          uint32_t start_percent, uint32_t end_percent,
                          char *reply_out, size_t reply_size) {
    app->ipc.last_reply[0] = '\0';
    ipc_send_frame(app, frame);
    uint64_t start_ms = monotonic_ms();
    uint64_t last_draw = 0ULL;
    while ((monotonic_ms() - start_ms) < 800ULL) {
        ipc_poll_ui(app, 16u);
        if (app->ipc.last_reply[0] != '\0') {
            break;
        }
        uint64_t now_ms = monotonic_ms();
        if (last_draw == 0ULL || (now_ms - last_draw) >= 60ULL) {
            last_draw = now_ms;
            uint32_t elapsed = (uint32_t)(now_ms - start_ms);
            uint32_t pct = start_percent + ((end_percent - start_percent) * elapsed) / 800u;
            draw_running_progress(app, step_title, "LISTENING FOR /ui REPLY...", pct, elapsed);
        }
        sleep_ms(20u);
    }
    if (app->ipc.last_reply[0] != '\0') {
        snprintf(reply_out, reply_size, "%s", app->ipc.last_reply);
    } else {
        snprintf(reply_out, reply_size, "NO_REPLY");
    }
}

static void run_stage_4_eq_probe(BenchApp *app) {
    char reply_preset[160];
    char reply_geq[160];
    char reply_peq[160];

    send_eq_probe(app, IPC_CMD_EQ_PRESET_ROCK, "STEP 1/4: 0689 PRESET", 0u, 25u,
                  reply_preset, sizeof(reply_preset));

    const char *geq_json =
        "{\"filterType\":0,\"bands\":[{\"freq\":60,\"gain\":6,\"q\":1.0},"
        "{\"freq\":230,\"gain\":3,\"q\":1.0},{\"freq\":910,\"gain\":-2,\"q\":1.0},"
        "{\"freq\":3600,\"gain\":4,\"q\":1.0},{\"freq\":14000,\"gain\":5,\"q\":1.0}]}";
    char geq_frame[320];
    snprintf(geq_frame, sizeof(geq_frame), "0689%04X0001%s",
             (uint32_t)(12u + strlen(geq_json)), geq_json);
    send_eq_probe(app, geq_frame, "STEP 2/4: 0689 GEQ (TYPE 0)", 25u, 55u,
                  reply_geq, sizeof(reply_geq));

    const char *peq_json =
        "{\"filterType\":1,\"bands\":[{\"freq\":120,\"gain\":5,\"q\":1.4}]}";
    char peq_frame[256];
    snprintf(peq_frame, sizeof(peq_frame), "0678%04X0001%s",
             (uint32_t)(12u + strlen(peq_json)), peq_json);
    send_eq_probe(app, peq_frame, "STEP 3/4: 0678 PEQ (TYPE 1)", 55u, 85u,
                  reply_peq, sizeof(reply_peq));

    ipc_send_frame(app, IPC_CMD_EQ_OFF);
    sleep_with_progress(app, 400u, "STEP 4/4: RESTORE EQ OFF", "SENDING 0689000C0000", 85u, 100u);

    StageSummary *summary = &app->summaries[3];
    summary->status = STATUS_LOGGED;
    snprintf(summary->lines[0], 48, "0689 : %.28s", reply_preset);
    snprintf(summary->lines[1], 48, "GEQ 0: %.28s", reply_geq);
    snprintf(summary->lines[2], 48, "PEQ 1: %.28s", reply_peq);
    snprintf(summary->lines[3], 48, "EQ RESTORED TO OFF");
    summary->line_count = 4u;

    if (app->report_file != NULL) {
        fprintf(app->report_file, "[Stage 4: V2.57 EQ/PEQ Probe]\n");
        fprintf(app->report_file, "  preset_0689 : %s\n", reply_preset);
        fprintf(app->report_file, "  graphic_eq_0: %s\n", reply_geq);
        fprintf(app->report_file, "  peq_probe_1 : %s\n", reply_peq);
        fprintf(app->report_file, "  restored_off: yes\n\n");
    }
    flush_logs_to_sd(app);
}

/* =========================================================================
 * Stage 5: Golden IPC Trace (Q14 / Q17 / Q18)
 * ========================================================================= */

static void run_stage_5_trace(BenchApp *app) {
    if (app->tracks.count == 0u) {
        discover_test_tracks(&app->tracks);
    }
    uint64_t tx_before = app->ipc.tx_count;
    uint64_t rx_before = app->ipc.rx_count;

    if (app->tracks.count > 0u) {
        uint32_t track_zero = 0u;
        write_m3u_and_play(app, 'A', &track_zero, 1u);
        uint64_t wait_start = monotonic_ms();
        uint64_t last_draw = 0ULL;
        while ((monotonic_ms() - wait_start) < 2500ULL) {
            ipc_poll_ui(app, 16u);
            if (app->ipc.duration_ms > 0u) {
                break;
            }
            uint64_t now_ms = monotonic_ms();
            if (last_draw == 0ULL || (now_ms - last_draw) >= 60ULL) {
                last_draw = now_ms;
                uint32_t elapsed = (uint32_t)(now_ms - wait_start);
                draw_running_progress(app, "STEP 1/5: START TRACK", app->tracks.names[0],
                                      (elapsed * 20u) / 2500u, elapsed);
            }
            sleep_ms(30u);
        }
    }

    /* Pause then resume */
    ipc_send_frame(app, IPC_CMD_PLAY_PAUSE);
    sleep_with_progress(app, 1200u, "STEP 2/5: PAUSE TEST", "CAPTURING PAUSE FRAMES", 20u, 32u);
    ipc_send_frame(app, IPC_CMD_PLAY_PAUSE);
    sleep_with_progress(app, 1200u, "STEP 2/5: RESUME TEST", "CAPTURING RESUME FRAMES", 32u, 45u);

    uint32_t duration = (app->ipc.duration_ms > 0u) ? app->ipc.duration_ms : 20000u;
    char seek_frame[32];

    /* 95% seek (Q18) */
    snprintf(seek_frame, sizeof(seek_frame), "01030010%08X", (duration * 95u) / 100u);
    ipc_send_frame(app, seek_frame);
    sleep_with_progress(app, 2000u, "STEP 3/5: SEEK TO 95%", "TESTING Q18 SEEK", 45u, 62u);

    /* 98% seek + wait for natural EOF (Q14 / Q18) */
    app->ipc.eof_seen = false;
    snprintf(seek_frame, sizeof(seek_frame), "01030010%08X", (duration * 98u) / 100u);
    ipc_send_frame(app, seek_frame);

    uint64_t eof_wait_start = monotonic_ms();
    uint64_t last_draw = 0ULL;
    while ((monotonic_ms() - eof_wait_start) < 10000ULL) {
        ipc_poll_ui(app, 32u);
        if (app->ipc.eof_seen) {
            break;
        }
        uint64_t now_ms = monotonic_ms();
        if (last_draw == 0ULL || (now_ms - last_draw) >= 60ULL) {
            last_draw = now_ms;
            uint32_t elapsed = (uint32_t)(now_ms - eof_wait_start);
            draw_running_progress(app, "STEP 4/5: SEEK 98% & EOF", "WAITING FOR NATURAL EOF...",
                                  62u + (elapsed * 38u) / 10000u, elapsed);
        }
        sleep_ms(30u);
    }

    uint64_t tx_delta = app->ipc.tx_count - tx_before;
    uint64_t rx_delta = app->ipc.rx_count - rx_before;

    StageSummary *summary = &app->summaries[4];
    summary->status = app->ipc.eof_seen ? STATUS_PASS : STATUS_LOGGED;
    snprintf(summary->lines[0], 48, "TX: %llu | RX: %llu",
             (unsigned long long)tx_delta, (unsigned long long)rx_delta);
    snprintf(summary->lines[1], 48, "SEEK 95%% & 98%%: SENT");
    snprintf(summary->lines[2], 48, "NATURAL EOF: %s",
             app->ipc.eof_seen ? "CAPTURED" : "TIMEOUT");
    summary->line_count = 3u;

    if (app->report_file != NULL) {
        fprintf(app->report_file, "[Stage 5: Golden IPC Trace]\n");
        fprintf(app->report_file, "  tx_frames=%llu, rx_frames=%llu, natural_eof=%s\n\n",
                (unsigned long long)tx_delta, (unsigned long long)rx_delta,
                app->ipc.eof_seen ? "YES" : "NO");
    }
    flush_logs_to_sd(app);
}

/* =========================================================================
 * Main Event Loop
 * ========================================================================= */

static void execute_current_stage(BenchApp *app) {
    switch (app->current_stage) {
        case 0u:
            run_stage_1_rotation(app);
            break;
        case 1u:
            run_stage_2_touch(app);
            break;
        case 2u:
            run_stage_3_handoff(app);
            break;
        case 3u:
            run_stage_4_eq_probe(app);
            break;
        case 4u:
            run_stage_5_trace(app);
            break;
        default:
            break;
    }
    drain_stale_input(&app->input);
}

int main(void) {
    prctl(PR_SET_NAME, WATCHDOG_PROCESS_NAME, 0, 0, 0);

    static BenchApp app;
    memset(&app, 0, sizeof(app));
    app.display.framebuffer_fd = -1;
    app.input.key_fd = -1;
    app.input.touch_fd = -1;
    app.ipc.ui_queue = (mqd_t)-1;
    app.ipc.player_queue = (mqd_t)-1;

    init_rotation_assets(&app);
    init_framebuffer(&app.display);
    init_input(&app.input);
    init_ipc(&app);
    discover_test_tracks(&app.tracks);
    open_sd_logs(&app);

    ipc_send_frame(&app, IPC_CMD_GET_STATE);
    draw_idle_or_summary(&app);

    uint64_t last_redraw_ms = 0ULL;
    for (;;) {
        poll_input(&app.input);
        ipc_poll_ui(&app, 16u);

        bool trigger_run = false;
        bool trigger_next = false;

        if (app.input.touch_released_edge) {
            if (point_in_rect(app.input.touch_x, app.input.touch_y,
                              BTN_RUN_X, BTN_RUN_Y, BTN_WIDTH, BTN_HEIGHT)) {
                trigger_run = true;
            } else if (point_in_rect(app.input.touch_x, app.input.touch_y,
                                     BTN_NEXT_X, BTN_NEXT_Y, BTN_WIDTH, BTN_HEIGHT)) {
                trigger_next = true;
            }
        }
        if (app.input.backup_long_play_edge) {
            if (app.summaries[app.current_stage].status == STATUS_IDLE) {
                trigger_run = true;
            } else {
                trigger_next = true;
            }
        }

        if (trigger_run) {
            app.all_done_synced = false;
            execute_current_stage(&app);
            draw_idle_or_summary(&app);
        } else if (trigger_next) {
            if (app.current_stage < STAGE_COUNT - 1u) {
                app.current_stage++;
            } else {
                flush_logs_to_sd(&app);
                sync();
                app.all_done_synced = true;
            }
            draw_idle_or_summary(&app);
        }

        uint64_t now_ms = monotonic_ms();
        if ((now_ms - last_redraw_ms) >= 250ULL) {
            last_redraw_ms = now_ms;
            draw_idle_or_summary(&app);
        }
        sleep_ms(16u);
    }

    return 0;
}
