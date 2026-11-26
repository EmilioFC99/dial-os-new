#ifndef DIALOS_CONSTANTS_H
#define DIALOS_CONSTANTS_H

#include <stdint.h>

/* Display and circular safe-area geometry (360x360 mounted 180 degrees upside down) */
#define SCREEN_WIDTH 360u
#define SCREEN_HEIGHT 360u
#define SCREEN_COORDINATE_MAX 359u
#define SCREEN_CENTER_X 180
#define SCREEN_CENTER_Y 180
#define DISPLAY_BEZEL_RADIUS 180
#define SAFE_CIRCLE_RADIUS 174
#define OUTER_TICK_RADIUS 160
#define TEXT_CHORD_RADIUS 160
#define ORBITAL_ARC_RADIUS 148
#define REGISTRATION_MARK_RADIUS 136
#define INNER_STAGE_RADIUS 112
#define FRAMEBUFFER_PAGE_COUNT 3u

#define FULL_CIRCLE_DEGREES 360
#define COLOR_OUTSIDE_BEZEL 0x00000000u

/* Boot choreography durations and sweep pacing */
#define BOOT_WAKE_DURATION_SECONDS 0.95f
#define BOOT_BUFFER_MIN_DURATION_SECONDS 6.85f
#define BOOT_BUFFER_EXIT_DURATION_SECONDS 0.35f
#define BOOT_SIGNAL_LOCK_DURATION_SECONDS 1.05f
#define BOOT_HANDOFF_DURATION_SECONDS 0.55f
#define BOOT_SWEEP_HOLD_CAP_BEFORE_READY 0.95f
#define BOOT_SWEEP_CATCHUP_RATE_PER_SECOND 0.65f

/* Home screen, wheel physics, and transition constants */
#define HOME_ENTRY_DURATION_SECONDS 0.78f
#define HOME_LAUNCH_DURATION_SECONDS 0.62f
#define HOME_DRAG_PIXELS_PER_DETENT 112.0f
#define HOME_SNAP_SPRING_RATE 13.5f
#define HOME_TAP_DRIFT_LIMIT_PX 10.0f
#define BOOT_RETRY_TAP_DRIFT_LIMIT_PX 14.0f
#define BACK_SWIPE_PULL_DISTANCE_PX 96.0f
#define BACK_SWIPE_COMMIT_THRESHOLD 0.45f
#define BACK_SWIPE_RETRACT_RATE 4.5f
#define LEFT_EDGE_SWIPE_ZONE_PX 68.0f
#define VINYL_SPIN_RADIANS_PER_SECOND 0.56299f
#define TRACK_TITLE_CAPACITY 128u

/* Unicode codepoints */
#define CODEPOINT_MIDDLE_DOT 0x00B7u /* U+00B7 '·': MIDDLE DOT */

/* Unit conversions and timing budgets */
#define MILLISECONDS_PER_SECOND 1000u
#define NANOSECONDS_PER_MILLISECOND 1000000ull
#define NANOSECONDS_PER_SECOND 1000000000ull
#define BOOT_FRAME_PERIOD_NS 16666667ull
#define AA24_WARNING_TIMEOUT_MS 12000u
#define READY_STABILITY_WINDOW_MS 10000u
#define BATTERY_POLL_INTERVAL_MS 30000u
#define HOME_PERF_LOG_INTERVAL_MS 5000u
#define FRAME_SLOW_THRESHOLD_MS 17u
#define LOGGER_BUFFER_CAPACITY_BYTES 16384u
#define LOGGER_LINE_MAX_BYTES 256u
#define LOGGER_ROTATE_MAX_BYTES 262144u
#define SD_MOUNT_FOLLOWUP_FIRST_MS 500u
#define SD_MOUNT_FOLLOWUP_SECOND_MS 1500u
#define IPC_RETRY_INTERVAL_MS 500u
#define IPC_DISABLE_IDLE_MAX_RETRIES 10u
#define PANEL_SLEEP_DELAY_MS 120u
#define PANEL_WAKE_DELAY_MS 50u
#define SHUTDOWN_MUTE_SETTLE_MS 150u

/* Crash containment and POSIX message queue sizing */
#define CRASH_LOOP_THRESHOLD 3u
#define CRASH_ALT_STACK_BYTES 32768u
#define CRASH_LOG_BUFFER_BYTES 512u
#define CRASH_COUNT_BUFFER_BYTES 32u
#define SIGNAL_EXIT_OFFSET 128
#define MESSAGE_QUEUE_MAX_MESSAGES 10
#define MESSAGE_QUEUE_MAX_MESSAGE_BYTES 8192
#define MESSAGE_QUEUE_RECEIVE_BUFFER_BYTES 16384u
#define IPC_ENVELOPE_HEADER_BYTES 12u
#define IPC_COMPACT_HEADER_BYTES 8u
#define IPC_OPCODE_BYTES 4u
#define IPC_HEX_FIELD_BYTES 4u
#define IPC_HEX_U32_BYTES 8u
#define IPC_COMMAND_BUFFER_BYTES 13u
#define SONG_UNESCAPED_BUFFER_BYTES 2048u

/* Brightness, volume, and battery bounds */
#define BRIGHTNESS_BOOT_DEFAULT 180u
#define BRIGHTNESS_VISIBLE_FLOOR 10u
#define BRIGHTNESS_MAX_DEFAULT 255u
#define VOLUME_DEFAULT 30u
#define MAX_VOLUME_DEFAULT 120u
#define VOLUME_HARD_MAX 120u
#define BATTERY_PERCENT_MAX 100u

/* Verified V2.57 /dev/input/event0 (keys) and /dev/input/event1 (cst816t touch) constants */
#define INPUT_EVENT_TYPE_SYN 0x00u      /* EV_SYN: synchronization event */
#define INPUT_EVENT_TYPE_KEY 0x01u      /* EV_KEY: key/button press or release */
#define INPUT_EVENT_TYPE_ABS 0x03u      /* EV_ABS: absolute touch axis */
#define SYN_CODE_REPORT 0x00u           /* SYN_REPORT: end of multi-touch packet */
#define KEYCODE_BTN_TOUCH 0x14Au        /* BTN_TOUCH (330): finger contact state */
#define ABS_CODE_MT_POSITION_X 0x35u    /* ABS_MT_POSITION_X (53): touch X coordinate */
#define ABS_CODE_MT_POSITION_Y 0x36u    /* ABS_MT_POSITION_Y (54): touch Y coordinate */
#define ABS_CODE_MT_TRACKING_ID 0x39u   /* ABS_MT_TRACKING_ID (57): active slot ID (-1 on lift) */
#define INPUT_EVENT_VALUE_PRESS 1
#define KEYCODE_VOLUME_UP_PRIMARY 251u
#define KEYCODE_VOLUME_UP_ALT_A 263u
#define KEYCODE_VOLUME_UP_ALT_B 266u
#define KEYCODE_VOLUME_DOWN_PRIMARY 252u
#define KEYCODE_VOLUME_DOWN_ALT_A 262u
#define KEYCODE_VOLUME_DOWN_ALT_B 267u
#define KEYCODE_POWER_SHORT_PRIMARY 259u
#define KEYCODE_POWER_SHORT_ALT 265u
#define KEYCODE_POWER_LONG 264u

/* Verified V2.57 hardware ioctls */
#define IOCTL_MIPS_WAITFORVSYNC 0x80044620u /* MIPS _IOW('F', 0x20, uint32_t) */
#define IOCTL_LCD_SLEEP 0x2000ef01u         /* /dev/lcd_st77916 sleep command */
#define IOCTL_LCD_WAKE 0x2000ef02u          /* /dev/lcd_st77916 wake command */
#define IOCTL_TOUCH_SLEEP 0x2000ef03u       /* /dev/cst816t sleep command */
#define IOCTL_TOUCH_WAKE 0x2000ef04u        /* /dev/cst816t wake command */
#define IOCTL_MIPS_INPUT_GRAB 0x80044590u   /* MIPS EVIOCGRAB: _IOW('E', 0x90, int) */

/* Verified V2.57 device, sysfs, IPC, and atomic storage paths */
#define PATH_FRAMEBUFFER "/dev/fb0"
#define PATH_INPUT_EVENTS "/dev/input/event0"
#define PATH_TOUCH_INPUT_EVENTS "/dev/input/event1"
#define PATH_LCD_PANEL "/dev/lcd_st77916"
#define PATH_TOUCH_PANEL "/dev/cst816t"
#define PATH_USB_STORAGE_UDC "/sys/kernel/config/usb_gadget/storage_demo/UDC"
#define PATH_BACKLIGHT_BRIGHTNESS \
    "/sys/bus/platform/drivers/pwm-backlight/backlight/backlight/backlight/brightness"
#define PATH_BACKLIGHT_MAX_BRIGHTNESS \
    "/sys/bus/platform/drivers/pwm-backlight/backlight/backlight/backlight/max_brightness"
#define PATH_BATTERY_CAPACITY "/sys/class/power_supply/cw221X-bat/capacity"
#define PATH_PROC_SELF_STATUS "/proc/self/status"
#define PATH_FIIO_PROCESS_FAILED "/usr/data/fiio/log/process_failed.txt"
#define PATH_QUEUE_UI "/ui"
#define PATH_QUEUE_PLAYER "/player"
#define PATH_TMP_ROOT "/tmp"
#define PATH_SDCARD_MOUNT "/tmp/sdcard"
#define PATH_SDCARD_DIALOS_DIRECTORY "/tmp/sdcard/.dial-os"
#define PATH_INTERNAL_DIALOS_DIRECTORY "/usr/data/.dial-os"
#define PATH_APPEARANCE_CONFIG "/usr/data/.dial-os/appearance"
#define PATH_APPEARANCE_CONFIG_TEMP "/usr/data/.dial-os/.appearance.tmp"
#define PATH_CRASH_COUNT "/tmp/dialos_crash_count"
#define PATH_CRASH_COUNT_TEMP "/tmp/.dialos_crash_count.tmp"
#define PATH_TMP_BOOT_LOG "/tmp/dialos_boot.log"
#define PATH_TMP_BOOT_LOG_TEMP "/tmp/.dialos_boot.log.tmp"
#define PATH_TMP_EVENT_LOG "/tmp/dialos.log"
#define PATH_TMP_EVENT_LOG_PREV "/tmp/dialos.log.prev"
#define PATH_SDCARD_BOOT_LOG "/tmp/sdcard/.dial-os/boot.log"
#define PATH_SDCARD_BOOT_LOG_TEMP "/tmp/sdcard/.dial-os/.boot.log.tmp"
#define PATH_SDCARD_EVENT_LOG "/tmp/sdcard/.dial-os/dialos.log"
#define PATH_SDCARD_EVENT_LOG_PREV "/tmp/sdcard/.dial-os/dialos.log.prev"
#define PATH_SDCARD_CRASH_LOG "/tmp/sdcard/.dial-os/crash.log"
#define PATH_SDCARD_CRASH_LOG_TEMP "/tmp/sdcard/.dial-os/.crash.log.tmp"
#define PATH_CMD_WATCHDOG "/usr/bin/cmd_watchdog"
#define PATH_KILLALL "/usr/bin/killall"
#define PATH_POWEROFF "/sbin/poweroff"
#define PROCESS_COMM_NAME "mq_ui"

#endif
