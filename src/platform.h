#ifndef DIALOS_PLATFORM_H
#define DIALOS_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "render.h"
#include "theme.h"

struct telemetry_snapshot {
    uint64_t takeover_ms;
    uint32_t animation_frames;
    uint32_t animation_fps_x10;
    uint32_t max_frame_ms;
    uint64_t aa24_ms;
    bool idle_poweroff_disabled;
    bool sd_mounted;
    uint32_t rss_ready_kb;
    bool is_shutdown_update;
    uint64_t uptime_ms;
    uint32_t rss_shutdown_kb;
    uint32_t watchdog_restarts;
    uint32_t home_frames;
    uint32_t home_fps_x10;
    uint32_t home_max_frame_ms;
};

struct hardware_status {
    bool battery_valid;
    uint8_t battery_percent;
    bool sd_mounted;
    bool usb_storage_active;
    bool sd_free_valid;
    uint64_t sd_free_megabytes;
};

struct platform_operations {
    void *context;
    uint64_t (*monotonic_ns)(void *context);
    bool (*display_acquire_surface)(void *context, struct framebuffer_surface *surface_out);
    void (*display_present_surface)(void *context);
    void (*display_set_brightness)(void *context, uint8_t brightness);
    void (*display_enter_sleep)(void *context);
    void (*display_begin_wake)(void *context);
    void (*display_finish_wake)(void *context, uint8_t restore_brightness);
    bool (*ipc_send_player)(void *context, const char *message, size_t length);
    void (*probe_hardware_status)(void *context, struct hardware_status *status_out);
    uint32_t (*crash_count_read)(void *context);
    void (*crash_count_write)(void *context, uint32_t count);
    void (*telemetry_write)(void *context, const struct telemetry_snapshot *snapshot);
    void (*execute_shutdown)(void *context, bool screen_was_locked,
                             const struct telemetry_snapshot *final_snapshot);
    void (*update_crash_state_mirror)(void *context, int32_t ui_state);
    bool (*appearance_mode_read)(void *context, enum appearance_mode *mode_out);
    void (*appearance_mode_write)(void *context, enum appearance_mode mode);
    bool (*log_append)(void *context, const char *text, size_t length, bool sync_to_disk);
    const char *library_music_root;
    const char *library_dialos_directory;
    const char *library_temp_directory;
};

#endif
