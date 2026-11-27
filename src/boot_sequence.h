#ifndef DIALOS_BOOT_SEQUENCE_H
#define DIALOS_BOOT_SEQUENCE_H

#include <stdbool.h>
#include <stdint.h>

enum boot_phase {
    BOOT_PHASE_WAKE = 0,
    BOOT_PHASE_BUFFER = 1,
    BOOT_PHASE_SIGNAL_LOCK = 2,
    BOOT_PHASE_HANDOFF = 3,
    BOOT_PHASE_WARNING = 4,
    BOOT_PHASE_COMPLETED = 5
};

struct boot_controller {
    enum boot_phase phase;
    enum boot_phase phase_before_warning;
    float total_elapsed_seconds;
    float phase_elapsed_seconds;
    float buffer_elapsed_seconds;
    float buffer_exit_elapsed_seconds;
    float stall_timer_seconds;
    float warning_elapsed_seconds;
    float sweep_progress;
    bool audio_ready;
    bool sd_mounted;
    bool has_library_scan_state;
    bool library_scan_ready;
    bool library_music_root_exists;
    uint32_t library_scan_phase;
    uint32_t library_discovered_files;
    uint32_t library_scanned_files;
    uint32_t library_album_count;
    uint32_t library_track_count;
    bool cover_precache_active;
    uint32_t cover_precache_done;
    uint32_t cover_precache_total;
    char dynamic_status_label[48];
    char dynamic_summary_line[64];
};

struct boot_render_snapshot {
    enum boot_phase phase;
    float act_progress;
    float buffer_exit_progress;
    float handoff_progress;
    float elapsed_seconds;
    float warning_elapsed_seconds;
    float sweep_progress;
    const char *status_label;
    const char *summary_line;
    bool completed;
    bool sd_mounted;
};

void boot_controller_initialize(struct boot_controller *controller);
void boot_controller_step(struct boot_controller *controller, float delta_seconds);
void boot_controller_mark_audio_ready(struct boot_controller *controller, bool sd_mounted);
void boot_controller_retry_from_warning(struct boot_controller *controller);
void boot_controller_update_sd_mounted(struct boot_controller *controller, bool sd_mounted);
void boot_controller_update_library_scan(struct boot_controller *controller, uint32_t scan_phase,
                                         uint32_t discovered_files, uint32_t scanned_files,
                                         uint32_t album_count, uint32_t track_count,
                                         bool music_root_exists, bool scan_ready);
void boot_controller_update_cover_precache(struct boot_controller *controller, bool active,
                                           uint32_t done_count, uint32_t total_count);
void boot_controller_get_snapshot(const struct boot_controller *controller,
                                  struct boot_render_snapshot *snapshot_out);

#endif
