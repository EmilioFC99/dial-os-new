#include "boot_sequence.h"

#include "constants.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define BOOT_BUFFER_AUDIO_INIT_BAND_START 0.25f
#define BOOT_BUFFER_WAIT_BAND_START 0.60f
#define BOOT_SWEEP_READY_BAND_START 0.92f

static float clamp_f32(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void update_buffer_sweep_progress(struct boot_controller *controller, float delta_seconds) {
    float buffer_time_ratio =
        clamp_f32(controller->buffer_elapsed_seconds / BOOT_BUFFER_MIN_DURATION_SECONDS, 0.0f, 1.0f);

    if (!controller->audio_ready) {
        float paced_target = buffer_time_ratio * BOOT_SWEEP_HOLD_CAP_BEFORE_READY;
        if (paced_target > controller->sweep_progress) {
            controller->sweep_progress = paced_target;
        }
        controller->sweep_progress =
            clamp_f32(controller->sweep_progress, 0.0f, BOOT_SWEEP_HOLD_CAP_BEFORE_READY);
        return;
    }

    if ((controller->has_library_scan_state && !controller->library_scan_ready) ||
        controller->cover_precache_active) {
        float scan_cap = 0.92f;
        float paced_target = buffer_time_ratio * scan_cap;
        if (paced_target > controller->sweep_progress) {
            controller->sweep_progress = paced_target;
        }
        controller->sweep_progress = clamp_f32(controller->sweep_progress, 0.0f, scan_cap);
        return;
    }

    if (controller->buffer_elapsed_seconds < BOOT_BUFFER_MIN_DURATION_SECONDS) {
        float catchup_step = delta_seconds * BOOT_SWEEP_CATCHUP_RATE_PER_SECOND;
        float min_step = delta_seconds / BOOT_BUFFER_MIN_DURATION_SECONDS;
        float catchup_cap =
            controller->sweep_progress + (catchup_step > min_step ? catchup_step : min_step);
        float next_target = buffer_time_ratio < catchup_cap ? buffer_time_ratio : catchup_cap;
        if (next_target > controller->sweep_progress) {
            controller->sweep_progress = next_target;
        }
        controller->sweep_progress = clamp_f32(controller->sweep_progress, 0.0f, 1.0f);
        return;
    }

    float next_sweep =
        controller->sweep_progress + delta_seconds * BOOT_SWEEP_CATCHUP_RATE_PER_SECOND;
    if (next_sweep > controller->sweep_progress) {
        controller->sweep_progress = next_sweep;
    }
    controller->sweep_progress = clamp_f32(controller->sweep_progress, 0.0f, 1.0f);
}

static float advance_wake_phase(struct boot_controller *controller, float remaining_delta) {
    float next_wake_elapsed = controller->phase_elapsed_seconds + remaining_delta;
    if (next_wake_elapsed >= BOOT_WAKE_DURATION_SECONDS) {
        float wake_used = BOOT_WAKE_DURATION_SECONDS - controller->phase_elapsed_seconds;
        if (wake_used < 0.0f) {
            wake_used = 0.0f;
        }
        if (!controller->audio_ready) {
            controller->stall_timer_seconds += wake_used;
        }
        controller->phase = BOOT_PHASE_BUFFER;
        controller->phase_elapsed_seconds = 0.0f;
        controller->buffer_elapsed_seconds = 0.0f;
        controller->buffer_exit_elapsed_seconds = 0.0f;
        float overflow = next_wake_elapsed - BOOT_WAKE_DURATION_SECONDS;
        return overflow > 0.0f ? overflow : 0.0f;
    }

    if (!controller->audio_ready) {
        controller->stall_timer_seconds += remaining_delta;
    }
    controller->phase_elapsed_seconds = next_wake_elapsed;
    return 0.0f;
}

static bool advance_buffer_phase(struct boot_controller *controller, float remaining_delta,
                                 float *overflow_out) {
    *overflow_out = 0.0f;
    float timeout_seconds = (float)AA24_WARNING_TIMEOUT_MS / (float)MILLISECONDS_PER_SECOND;

    if (!controller->audio_ready) {
        controller->stall_timer_seconds += remaining_delta;
        if (controller->stall_timer_seconds >= timeout_seconds - 1e-4f) {
            float overflow_past_timeout = controller->stall_timer_seconds - timeout_seconds;
            if (overflow_past_timeout < 0.0f) {
                overflow_past_timeout = 0.0f;
            }
            controller->phase_before_warning = BOOT_PHASE_BUFFER;
            controller->phase = BOOT_PHASE_WARNING;
            controller->warning_elapsed_seconds = overflow_past_timeout;
            return false;
        }
    }

    float previous_buffer_elapsed = controller->buffer_elapsed_seconds;
    float previous_sweep_progress = controller->sweep_progress;
    controller->buffer_elapsed_seconds += remaining_delta;
    update_buffer_sweep_progress(controller, remaining_delta);

    bool scan_ready = (!controller->has_library_scan_state || controller->library_scan_ready) &&
                      !controller->cover_precache_active;
    if (controller->audio_ready && scan_ready &&
        controller->buffer_elapsed_seconds >= BOOT_BUFFER_MIN_DURATION_SECONDS &&
        controller->sweep_progress >= 1.0f) {
        float exit_delta = remaining_delta;
        if (previous_buffer_elapsed < BOOT_BUFFER_MIN_DURATION_SECONDS) {
            exit_delta = controller->buffer_elapsed_seconds - BOOT_BUFFER_MIN_DURATION_SECONDS;
        } else if (previous_sweep_progress < 1.0f) {
            exit_delta = 0.0f;
        }
        controller->buffer_exit_elapsed_seconds += clamp_f32(exit_delta, 0.0f, remaining_delta);
        if (controller->buffer_exit_elapsed_seconds >= BOOT_BUFFER_EXIT_DURATION_SECONDS) {
            float exit_overflow =
                controller->buffer_exit_elapsed_seconds - BOOT_BUFFER_EXIT_DURATION_SECONDS;
            controller->buffer_exit_elapsed_seconds = BOOT_BUFFER_EXIT_DURATION_SECONDS;
            controller->phase = BOOT_PHASE_SIGNAL_LOCK;
            controller->phase_elapsed_seconds = 0.0f;
            *overflow_out = clamp_f32(exit_overflow, 0.0f, remaining_delta);
            return true;
        }
    }

    return true;
}

static float advance_signal_lock_phase(struct boot_controller *controller, float remaining_delta) {
    float next_lock_elapsed = controller->phase_elapsed_seconds + remaining_delta;
    if (next_lock_elapsed >= BOOT_SIGNAL_LOCK_DURATION_SECONDS) {
        controller->phase = BOOT_PHASE_HANDOFF;
        controller->phase_elapsed_seconds = 0.0f;
        float overflow = next_lock_elapsed - BOOT_SIGNAL_LOCK_DURATION_SECONDS;
        return overflow > 0.0f ? overflow : 0.0f;
    }

    controller->phase_elapsed_seconds = next_lock_elapsed;
    return 0.0f;
}

static void advance_handoff_phase(struct boot_controller *controller, float remaining_delta) {
    float next_handoff_elapsed = controller->phase_elapsed_seconds + remaining_delta;
    if (next_handoff_elapsed >= BOOT_HANDOFF_DURATION_SECONDS) {
        controller->phase = BOOT_PHASE_COMPLETED;
        controller->phase_elapsed_seconds = BOOT_HANDOFF_DURATION_SECONDS;
        controller->sweep_progress = 1.0f;
        return;
    }

    controller->phase_elapsed_seconds = next_handoff_elapsed;
}

static const char *resolve_buffer_status_label(struct boot_controller *controller) {
    if (controller->cover_precache_active &&
        controller->sweep_progress >= BOOT_BUFFER_WAIT_BAND_START) {
        snprintf(controller->dynamic_status_label, sizeof(controller->dynamic_status_label),
                 "COVERS // %u / %u", (unsigned)controller->cover_precache_done,
                 (unsigned)controller->cover_precache_total);
        return controller->dynamic_status_label;
    }
    if (controller->has_library_scan_state) {
        if (!controller->library_scan_ready) {
            if (controller->library_scan_phase == 2u) {
                return "SCAN // DISCOVERING";
            }
            if (controller->library_scan_phase == 3u) {
                snprintf(controller->dynamic_status_label,
                         sizeof(controller->dynamic_status_label), "SCAN // %u / %u",
                         (unsigned)controller->library_scanned_files,
                         (unsigned)controller->library_discovered_files);
                return controller->dynamic_status_label;
            }
            if (controller->library_scan_phase == 7u || controller->library_scan_phase == 8u) {
                if (controller->sweep_progress >= BOOT_BUFFER_WAIT_BAND_START) {
                    return "SYNC // VERIFYING LIB";
                }
            } else {
                snprintf(controller->dynamic_status_label,
                         sizeof(controller->dynamic_status_label), "INDEX // %u ALBUMS",
                         (unsigned)controller->library_album_count);
                return controller->dynamic_status_label;
            }
        }
        if (controller->audio_ready) {
            if (controller->sweep_progress >= BOOT_SWEEP_READY_BAND_START ||
                controller->buffer_exit_elapsed_seconds > 0.0f) {
                return "LOCK // SYSTEM READY";
            }
            if (controller->sweep_progress >= 0.85f) {
                snprintf(controller->dynamic_status_label,
                         sizeof(controller->dynamic_status_label), "INDEX // %u ALBUMS",
                         (unsigned)controller->library_album_count);
                return controller->dynamic_status_label;
            }
            if (controller->sweep_progress >= BOOT_BUFFER_WAIT_BAND_START) {
                snprintf(controller->dynamic_status_label,
                         sizeof(controller->dynamic_status_label), "SCAN // %u TRACKS",
                         (unsigned)controller->library_track_count);
                return controller->dynamic_status_label;
            }
        }
    }
    if (controller->audio_ready) {
        if (controller->sweep_progress >= BOOT_SWEEP_READY_BAND_START ||
            controller->buffer_exit_elapsed_seconds > 0.0f) {
            return "LOCK // SYSTEM READY";
        }
        if (controller->sweep_progress >= BOOT_BUFFER_WAIT_BAND_START) {
            return "SYNC // SYSTEM READY";
        }
        if (controller->sweep_progress >= BOOT_BUFFER_AUDIO_INIT_BAND_START) {
            return "INIT // AUDIO ENGINE";
        }
        return "INIT // SYSTEM";
    }
    if (controller->sweep_progress >= BOOT_BUFFER_WAIT_BAND_START) {
        return "WAIT // AUDIO ENGINE";
    }
    if (controller->sweep_progress >= BOOT_BUFFER_AUDIO_INIT_BAND_START) {
        return "INIT // AUDIO ENGINE";
    }
    return "INIT // SYSTEM";
}

void boot_controller_initialize(struct boot_controller *controller) {
    if (controller == NULL) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    controller->phase = BOOT_PHASE_WAKE;
    controller->phase_before_warning = BOOT_PHASE_WAKE;
}

void boot_controller_step(struct boot_controller *controller, float delta_seconds) {
    if (controller == NULL || !isfinite(delta_seconds) || delta_seconds <= 0.0f ||
        controller->phase == BOOT_PHASE_COMPLETED) {
        return;
    }

    controller->total_elapsed_seconds += delta_seconds;

    if (controller->phase == BOOT_PHASE_WARNING) {
        controller->warning_elapsed_seconds += delta_seconds;
        return;
    }

    float remaining_delta = delta_seconds;

    if (controller->phase == BOOT_PHASE_WAKE) {
        remaining_delta = advance_wake_phase(controller, remaining_delta);
    }

    if (controller->phase == BOOT_PHASE_BUFFER) {
        float next_delta = 0.0f;
        if (!advance_buffer_phase(controller, remaining_delta, &next_delta)) {
            return;
        }
        remaining_delta = next_delta;
    }

    if (controller->phase == BOOT_PHASE_SIGNAL_LOCK && remaining_delta > 0.0f) {
        remaining_delta = advance_signal_lock_phase(controller, remaining_delta);
    }

    if (controller->phase == BOOT_PHASE_HANDOFF && remaining_delta > 0.0f) {
        advance_handoff_phase(controller, remaining_delta);
    }
}

void boot_controller_mark_audio_ready(struct boot_controller *controller, bool sd_mounted) {
    if (controller == NULL) {
        return;
    }
    controller->audio_ready = true;
    controller->sd_mounted = sd_mounted;

    if (controller->phase == BOOT_PHASE_WARNING) {
        controller->phase =
            controller->phase_before_warning == BOOT_PHASE_WAKE ? BOOT_PHASE_WAKE
                                                                : BOOT_PHASE_BUFFER;
        controller->warning_elapsed_seconds = 0.0f;
        if (controller->stall_timer_seconds > BOOT_WAKE_DURATION_SECONDS) {
            controller->stall_timer_seconds = BOOT_WAKE_DURATION_SECONDS;
        }
    }
}

void boot_controller_retry_from_warning(struct boot_controller *controller) {
    if (controller == NULL || controller->phase != BOOT_PHASE_WARNING) {
        return;
    }
    controller->phase = BOOT_PHASE_BUFFER;
    controller->warning_elapsed_seconds = 0.0f;
    controller->stall_timer_seconds = BOOT_WAKE_DURATION_SECONDS;
    if (controller->sweep_progress > BOOT_SWEEP_HOLD_CAP_BEFORE_READY) {
        controller->sweep_progress = BOOT_SWEEP_HOLD_CAP_BEFORE_READY;
    }
}

void boot_controller_update_sd_mounted(struct boot_controller *controller, bool sd_mounted) {
    if (controller == NULL) {
        return;
    }
    controller->sd_mounted = sd_mounted;
}

void boot_controller_update_library_scan(struct boot_controller *controller, uint32_t scan_phase,
                                         uint32_t discovered_files, uint32_t scanned_files,
                                         uint32_t album_count, uint32_t track_count,
                                         bool music_root_exists, bool scan_ready) {
    if (controller == NULL) {
        return;
    }
    controller->has_library_scan_state = true;
    controller->library_scan_phase = scan_phase;
    controller->library_discovered_files = discovered_files;
    controller->library_scanned_files = scanned_files;
    controller->library_album_count = album_count;
    controller->library_track_count = track_count;
    controller->library_music_root_exists = music_root_exists;
    controller->library_scan_ready = scan_ready;
}

void boot_controller_update_cover_precache(struct boot_controller *controller, bool active,
                                           uint32_t done_count, uint32_t total_count) {
    if (controller == NULL) {
        return;
    }
    controller->cover_precache_active = active;
    controller->cover_precache_done = done_count;
    controller->cover_precache_total = total_count;
}

void boot_controller_get_snapshot(const struct boot_controller *controller,
                                  struct boot_render_snapshot *snapshot_out) {
    if (controller == NULL || snapshot_out == NULL) {
        return;
    }

    float act_progress = 0.0f;
    float buffer_exit_progress = 0.0f;
    float handoff_progress = 0.0f;
    const char *status_label = "MEM // CHECK";

    switch (controller->phase) {
        case BOOT_PHASE_WAKE:
            act_progress =
                clamp_f32(controller->phase_elapsed_seconds / BOOT_WAKE_DURATION_SECONDS, 0.0f, 1.0f);
            status_label = "MEM // CHECK";
            break;
        case BOOT_PHASE_BUFFER:
            act_progress = clamp_f32(
                controller->buffer_elapsed_seconds / BOOT_BUFFER_MIN_DURATION_SECONDS, 0.0f, 1.0f);
            buffer_exit_progress = clamp_f32(
                controller->buffer_exit_elapsed_seconds / BOOT_BUFFER_EXIT_DURATION_SECONDS, 0.0f,
                1.0f);
            status_label = resolve_buffer_status_label((struct boot_controller *)controller);
            break;
        case BOOT_PHASE_SIGNAL_LOCK:
            act_progress = clamp_f32(
                controller->phase_elapsed_seconds / BOOT_SIGNAL_LOCK_DURATION_SECONDS, 0.0f, 1.0f);
            buffer_exit_progress = 1.0f;
            status_label = "LOCK // SYSTEM READY";
            break;
        case BOOT_PHASE_HANDOFF:
            act_progress = 1.0f;
            buffer_exit_progress = 1.0f;
            handoff_progress = clamp_f32(
                controller->phase_elapsed_seconds / BOOT_HANDOFF_DURATION_SECONDS, 0.0f, 1.0f);
            status_label = "LOCK // SYSTEM READY";
            break;
        case BOOT_PHASE_COMPLETED:
            act_progress = 1.0f;
            buffer_exit_progress = 1.0f;
            handoff_progress = 1.0f;
            status_label = "LOCK // SYSTEM READY";
            break;
        case BOOT_PHASE_WARNING:
            act_progress = clamp_f32(
                controller->buffer_elapsed_seconds / BOOT_BUFFER_MIN_DURATION_SECONDS, 0.0f, 1.0f);
            status_label = "POLLING /UI BUS";
            break;
    }

    snapshot_out->phase = controller->phase;
    snapshot_out->act_progress = act_progress;
    snapshot_out->buffer_exit_progress = buffer_exit_progress;
    snapshot_out->handoff_progress = handoff_progress;
    snapshot_out->elapsed_seconds = controller->total_elapsed_seconds;
    snapshot_out->warning_elapsed_seconds = controller->warning_elapsed_seconds;
    snapshot_out->sweep_progress = controller->sweep_progress;
    snapshot_out->status_label = status_label;
    if (controller->has_library_scan_state) {
        struct boot_controller *mutable_controller = (struct boot_controller *)controller;
        if (!controller->sd_mounted || !controller->library_music_root_exists) {
            snprintf(mutable_controller->dynamic_summary_line,
                     sizeof(mutable_controller->dynamic_summary_line),
                     "V1.0 \xC2\xB7 NO FOLDER \xC2\xB7 0 TRACKS");
        } else if (controller->library_track_count == 0u) {
            snprintf(mutable_controller->dynamic_summary_line,
                     sizeof(mutable_controller->dynamic_summary_line),
                     "V1.0 \xC2\xB7 EMPTY LIB \xC2\xB7 0 TRACKS");
        } else {
            snprintf(mutable_controller->dynamic_summary_line,
                     sizeof(mutable_controller->dynamic_summary_line),
                     "V1.0 \xC2\xB7 LIB OK \xC2\xB7 %u TRACKS",
                     (unsigned)controller->library_track_count);
        }
        snapshot_out->summary_line = mutable_controller->dynamic_summary_line;
    } else {
        snapshot_out->summary_line = controller->sd_mounted
                                         ? "V1.0 \xC2\xB7 AUDIO OK \xC2\xB7 SD MOUNTED"
                                         : "V1.0 \xC2\xB7 AUDIO OK \xC2\xB7 NO SD";
    }
    snapshot_out->completed = (controller->phase == BOOT_PHASE_COMPLETED);
    snapshot_out->sd_mounted = controller->sd_mounted;
}
