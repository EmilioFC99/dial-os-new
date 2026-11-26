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
void boot_controller_get_snapshot(const struct boot_controller *controller,
                                  struct boot_render_snapshot *snapshot_out);

#endif
