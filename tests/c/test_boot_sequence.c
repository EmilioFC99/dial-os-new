#include "test_runner.h"

#include "boot_sequence.h"
#include "constants.h"

#include <math.h>
#include <stdio.h>

static void step_in_increments(struct boot_controller *controller, float total_seconds,
                               float step_seconds) {
    float remaining = total_seconds;
    while (remaining > 0.0001f) {
        float current_step = remaining < step_seconds ? remaining : step_seconds;
        boot_controller_step(controller, current_step);
        remaining -= current_step;
    }
}

static void test_boot_nominal_early_audio_ready(void) {
    struct boot_controller controller;
    struct boot_render_snapshot snapshot;
    boot_controller_initialize(&controller);

    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_WAKE, snapshot.phase);
    TEST_ASSERT_EQ_STR("MEM // CHECK", snapshot.status_label);
    TEST_ASSERT_FALSE(snapshot.completed);

    /* Signal audio ready immediately (early aa24) */
    boot_controller_mark_audio_ready(&controller, true);

    /* Advance through Act 01 Wake (0.95s) */
    step_in_increments(&controller, 0.955f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);
    TEST_ASSERT_EQ_STR("INIT // SYSTEM", snapshot.status_label);

    /* Advance through Act 02 Buffer sweep (6.85s) -> hits 100% solid before exit hold */
    step_in_increments(&controller, 6.855f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.sweep_progress >= 0.999f);
    TEST_ASSERT_EQ_STR("LOCK // SYSTEM READY", snapshot.status_label);

    /* Advance through Act 02 post-100% exit hold (0.35s) into Act 03 Signal Lock */
    step_in_increments(&controller, 0.36f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_SIGNAL_LOCK, snapshot.phase);
    TEST_ASSERT_EQ_STR("LOCK // SYSTEM READY", snapshot.status_label);
    TEST_ASSERT_EQ_STR("V1.0 \xC2\xB7 AUDIO OK \xC2\xB7 SD MOUNTED", snapshot.summary_line);

    /* Advance through Act 03 Signal Lock (1.05s) into Act 04 Handoff */
    step_in_increments(&controller, 1.055f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_HANDOFF, snapshot.phase);

    /* Advance through Act 04 Handoff (0.55s) into Completed */
    step_in_increments(&controller, 0.56f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_COMPLETED, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.completed);
}

static void test_boot_realistic_7_8s_aa24_hold_and_catchup(void) {
    struct boot_controller controller;
    struct boot_render_snapshot snapshot;
    boot_controller_initialize(&controller);

    /* Advance 8.2s without aa24 (past 7.80s): should hold in BUFFER at 95% cap */
    step_in_increments(&controller, 8.2f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.sweep_progress >= 0.949f && snapshot.sweep_progress <= 0.951f);
    TEST_ASSERT_EQ_STR("WAIT // AUDIO ENGINE", snapshot.status_label);

    /* aa24 arrives with no SD mounted: catches up smoothly to 100%, then exits to Signal Lock */
    boot_controller_mark_audio_ready(&controller, false);
    step_in_increments(&controller, 0.15f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.sweep_progress >= 0.999f);

    step_in_increments(&controller, 0.36f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_SIGNAL_LOCK, snapshot.phase);
    TEST_ASSERT_EQ_STR("V1.0 \xC2\xB7 AUDIO OK \xC2\xB7 NO SD", snapshot.summary_line);
}

static void test_boot_warning_boundary_and_late_recovery(void) {
    struct boot_controller controller;
    struct boot_render_snapshot snapshot;
    boot_controller_initialize(&controller);

    /* Just before 12.0s stays in BUFFER */
    step_in_increments(&controller, 11.98f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);

    /* Crossing 12.0s enters WARNING */
    boot_controller_step(&controller, 0.05f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_WARNING, snapshot.phase);
    TEST_ASSERT_EQ_STR("POLLING /UI BUS", snapshot.status_label);
    TEST_ASSERT_TRUE(snapshot.warning_elapsed_seconds >= 0.0f);

    /* Advance 2.5s in WARNING */
    step_in_increments(&controller, 2.5f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_WARNING, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.warning_elapsed_seconds >= 2.49f);

    /* Late aa24 recovers back into BUFFER and finishes Acts 02 -> 04 */
    boot_controller_mark_audio_ready(&controller, true);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);

    step_in_increments(&controller, 2.15f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_COMPLETED, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.completed);
}

static void test_boot_edge_and_null_inputs(void) {
    struct boot_controller controller;
    struct boot_render_snapshot snapshot;
    boot_controller_initialize(&controller);

    /* Non-positive or non-finite deltas are ignored safely */
    boot_controller_step(&controller, 0.0f);
    boot_controller_step(&controller, -1.0f);
    boot_controller_step(&controller, NAN);
    boot_controller_step(&controller, INFINITY);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_WAKE, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.elapsed_seconds == 0.0f);

    /* SD mount toggle updates summary dynamically */
    boot_controller_update_sd_mounted(&controller, true);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_TRUE(snapshot.sd_mounted);
    TEST_ASSERT_EQ_STR("V1.0 \xC2\xB7 AUDIO OK \xC2\xB7 SD MOUNTED", snapshot.summary_line);

    boot_controller_update_sd_mounted(&controller, false);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_FALSE(snapshot.sd_mounted);
    TEST_ASSERT_EQ_STR("V1.0 \xC2\xB7 AUDIO OK \xC2\xB7 NO SD", snapshot.summary_line);

    /* NULL safety */
    boot_controller_initialize(NULL);
    boot_controller_step(NULL, 0.1f);
    boot_controller_mark_audio_ready(NULL, true);
    boot_controller_update_sd_mounted(NULL, true);
    boot_controller_get_snapshot(NULL, &snapshot);
    boot_controller_get_snapshot(&controller, NULL);
}

static void test_boot_library_scan_hold_and_summary_lines(void) {
    struct boot_controller controller;
    struct boot_render_snapshot snapshot;
    boot_controller_initialize(&controller);
    boot_controller_mark_audio_ready(&controller, true);
    boot_controller_update_sd_mounted(&controller, true);

    /* Advance into Act 02 (BUFFER) and hold at 92% while scanning */
    boot_controller_update_library_scan(&controller, 2u, 0u, 0u, 0u, 0u, true, false);
    step_in_increments(&controller, 6.8f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.act_progress <= 0.9201f);
    TEST_ASSERT_EQ_STR("SCAN // DISCOVERING", snapshot.status_label);

    /* Progress scan count updates status label without tripping stall warning */
    boot_controller_update_library_scan(&controller, 3u, 5u, 2u, 1u, 2u, true, false);
    step_in_increments(&controller, 1.5f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);
    TEST_ASSERT_EQ_STR("SCAN // 2 / 5", snapshot.status_label);

    /* Early Act 02 (< 0.60 sweep) preserves INIT status and does not jump sweep_progress */
    struct boot_controller ramp_ctrl;
    boot_controller_initialize(&ramp_ctrl);
    boot_controller_mark_audio_ready(&ramp_ctrl, true);
    boot_controller_update_sd_mounted(&ramp_ctrl, true);
    boot_controller_update_library_scan(&ramp_ctrl, 6u, 5u, 5u, 2u, 5u, true, true);
    boot_controller_update_cover_precache(&ramp_ctrl, true, 1u, 4u);
    step_in_increments(&ramp_ctrl, 1.2f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&ramp_ctrl, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.sweep_progress < 0.25f);
    TEST_ASSERT_TRUE(strcmp(snapshot.status_label, "COVERS // 1 / 4") != 0);

    /* Complete scan -> holds on cover precache if active, then advances */
    boot_controller_update_library_scan(&controller, 6u, 5u, 5u, 2u, 5u, true, true);
    boot_controller_update_cover_precache(&controller, true, 1u, 4u);
    step_in_increments(&controller, 0.5f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_BUFFER, snapshot.phase);
    TEST_ASSERT_TRUE(snapshot.sweep_progress <= 0.9201f);
    TEST_ASSERT_EQ_STR("COVERS // 1 / 4", snapshot.status_label);

    boot_controller_update_cover_precache(&controller, false, 4u, 4u);
    step_in_increments(&controller, 0.5f, 1.0f / 60.0f);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_I32(BOOT_PHASE_SIGNAL_LOCK, snapshot.phase);
    TEST_ASSERT_EQ_STR("V1.0 \xC2\xB7 LIB OK \xC2\xB7 5 TRACKS", snapshot.summary_line);

    /* Empty library and missing folder summary lines */
    boot_controller_update_library_scan(&controller, 4u, 0u, 0u, 0u, 0u, true, true);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_STR("V1.0 \xC2\xB7 EMPTY LIB \xC2\xB7 0 TRACKS", snapshot.summary_line);

    boot_controller_update_library_scan(&controller, 4u, 0u, 0u, 0u, 0u, false, true);
    boot_controller_get_snapshot(&controller, &snapshot);
    TEST_ASSERT_EQ_STR("V1.0 \xC2\xB7 NO FOLDER \xC2\xB7 0 TRACKS", snapshot.summary_line);
}

void run_boot_sequence_tests(void) {
    printf("Suite: boot_sequence\n");
    RUN_TEST(test_boot_nominal_early_audio_ready);
    RUN_TEST(test_boot_realistic_7_8s_aa24_hold_and_catchup);
    RUN_TEST(test_boot_warning_boundary_and_late_recovery);
    RUN_TEST(test_boot_edge_and_null_inputs);
    RUN_TEST(test_boot_library_scan_hold_and_summary_lines);
}
