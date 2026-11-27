#include "test_runner.h"

#include "home_screen.h"

#include <math.h>

static void step_controller_seconds(struct home_controller *controller, float total_seconds,
                                    float step_seconds) {
    uint32_t steps = (uint32_t)lroundf(total_seconds / step_seconds);
    for (uint32_t index = 0u; index < steps; index++) {
        home_controller_step(controller, step_seconds);
    }
}

static void test_volume_normalization_boundaries(void) {
    TEST_ASSERT_EQ_U32(0u, home_normalize_volume_percent(0u));
    TEST_ASSERT_EQ_U32(25u, home_normalize_volume_percent(30u));
    TEST_ASSERT_EQ_U32(50u, home_normalize_volume_percent(60u));
    TEST_ASSERT_EQ_U32(85u, home_normalize_volume_percent(102u));
    TEST_ASSERT_EQ_U32(100u, home_normalize_volume_percent(120u));
    TEST_ASSERT_EQ_U32(100u, home_normalize_volume_percent(200u));
}

static void test_home_entry_and_cyclic_wheel_physics(void) {
    struct home_controller controller;
    home_controller_initialize(&controller);
    home_controller_enter_from_boot(&controller);

    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_HOME, controller.active_screen);
    TEST_ASSERT_EQ_I32(0, controller.focused_slot);
    TEST_ASSERT_TRUE(controller.entry_progress < 0.01f);

    step_controller_seconds(&controller, HOME_ENTRY_DURATION_SECONDS + 0.05f, 1.0f / 60.0f);
    TEST_ASSERT_TRUE(fabsf(controller.entry_progress - 1.0f) < 0.001f);

    /* Horizontal drag by 112 px moves 1 detent right (toward Library slot 1) */
    struct touch_event down = {TOUCH_EVENT_DOWN, 220, 150, 1000ull};
    struct touch_event move = {TOUCH_EVENT_MOVE, 108, 150, 1026ull};
    struct touch_event up = {TOUCH_EVENT_UP, 108, 150, 1052ull};
    home_controller_handle_touch(&controller, &down);
    home_controller_handle_touch(&controller, &move);
    TEST_ASSERT_TRUE(fabsf(controller.scroll_position - 1.0f) < 0.01f);
    home_controller_handle_touch(&controller, &up);

    step_controller_seconds(&controller, 0.6f, 1.0f / 60.0f);
    TEST_ASSERT_EQ_I32(HOME_APP_LIBRARY, home_wrap_app_slot(controller.focused_slot));

    /* Cyclic wrap backwards from slot 0 -> slot 3 (Settings) with 38 Hz + 100 ms gap */
    controller.scroll_position = 0.0f;
    controller.target_slot = 0;
    controller.focused_slot = 0;

    struct touch_event flick_down = {TOUCH_EVENT_DOWN, 120, 150, 2000ull};
    struct touch_event flick_move1 = {TOUCH_EVENT_MOVE, 155, 150, 2026ull};
    struct touch_event flick_move2 = {TOUCH_EVENT_MOVE, 200, 150, 2126ull};
    struct touch_event flick_up = {TOUCH_EVENT_UP, 200, 150, 2140ull};
    home_controller_handle_touch(&controller, &flick_down);
    home_controller_handle_touch(&controller, &flick_move1);
    home_controller_handle_touch(&controller, &flick_move2);
    home_controller_handle_touch(&controller, &flick_up);

    step_controller_seconds(&controller, 0.7f, 1.0f / 60.0f);
    TEST_ASSERT_EQ_I32(HOME_APP_SETTINGS, home_wrap_app_slot(controller.focused_slot));

    /* Revolution normalization when scrolling >= 16 */
    controller.scroll_position = 17.0f;
    controller.target_slot = 17;
    controller.focused_slot = 17;
    home_controller_step(&controller, 1.0f / 60.0f);
    TEST_ASSERT_TRUE(controller.scroll_position >= 0.0f && controller.scroll_position < 4.0f);
    TEST_ASSERT_EQ_I32(HOME_APP_LIBRARY, controller.focused_slot);
}

static void test_home_tap_neighbor_and_inline_previews(void) {
    struct home_controller controller;
    home_controller_initialize(&controller);
    controller.entry_progress = 1.0f;

    /* Tap right neighbor (Library at ~308, 162) centers it without launching */
    struct touch_event tap_right_down = {TOUCH_EVENT_DOWN, 308, 162, 100ull};
    struct touch_event tap_right_up = {TOUCH_EVENT_UP, 308, 162, 140ull};
    home_controller_handle_touch(&controller, &tap_right_down);
    home_controller_handle_touch(&controller, &tap_right_up);
    step_controller_seconds(&controller, 0.7f, 1.0f / 60.0f);
    TEST_ASSERT_EQ_I32(HOME_APP_LIBRARY, home_wrap_app_slot(controller.focused_slot));
    TEST_ASSERT_TRUE(controller.launch_target < 0.01f);

    /* Move to Settings (slot 3) and tap center cube to open inline preview */
    controller.scroll_position = 3.0f;
    controller.target_slot = 3;
    controller.focused_slot = 3;
    struct touch_event tap_hero_down = {TOUCH_EVENT_DOWN, 180, 148, 200ull};
    struct touch_event tap_hero_up = {TOUCH_EVENT_UP, 180, 148, 240ull};
    home_controller_handle_touch(&controller, &tap_hero_down);
    home_controller_handle_touch(&controller, &tap_hero_up);
    TEST_ASSERT_TRUE(controller.launch_target > 0.99f);
    TEST_ASSERT_EQ_I32(HOME_APP_SETTINGS, controller.launched_app_id);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_HOME, controller.active_screen);

    step_controller_seconds(&controller, HOME_LAUNCH_DURATION_SECONDS + 0.05f, 1.0f / 60.0f);
    TEST_ASSERT_TRUE(fabsf(controller.launch_progress - 1.0f) < 0.001f);

    /* Tap Settings row 0 (y=170..194) emits HOME_EVENT_TOGGLE_APPEARANCE */
    struct touch_event row0_down = {TOUCH_EVENT_DOWN, 180, 182, 300ull};
    struct touch_event row0_up = {TOUCH_EVENT_UP, 180, 182, 330ull};
    TEST_ASSERT_EQ_I32(HOME_EVENT_NONE, home_controller_handle_touch(&controller, &row0_down));
    TEST_ASSERT_EQ_I32(HOME_EVENT_TOGGLE_APPEARANCE,
                       home_controller_handle_touch(&controller, &row0_up));

    /* Short swipe < 45% retracts back_swipe_progress without closing */
    struct touch_event short_down = {TOUCH_EVENT_DOWN, 20, 180, 400ull};
    struct touch_event short_move = {TOUCH_EVENT_MOVE, 50, 180, 426ull};
    struct touch_event short_up = {TOUCH_EVENT_UP, 50, 180, 452ull};
    home_controller_handle_touch(&controller, &short_down);
    home_controller_handle_touch(&controller, &short_move);
    TEST_ASSERT_TRUE(controller.back_swipe_progress > 0.25f &&
                     controller.back_swipe_progress < BACK_SWIPE_COMMIT_THRESHOLD);
    home_controller_handle_touch(&controller, &short_up);
    TEST_ASSERT_TRUE(controller.launch_target > 0.99f);
    step_controller_seconds(&controller, 0.2f, 1.0f / 60.0f);
    TEST_ASSERT_TRUE(controller.back_swipe_progress < 0.01f);

    /* Committed swipe >= 45% closes inline preview */
    struct touch_event back_down = {TOUCH_EVENT_DOWN, 20, 180, 600ull};
    struct touch_event back_move = {TOUCH_EVENT_MOVE, 80, 180, 626ull};
    struct touch_event back_up = {TOUCH_EVENT_UP, 80, 180, 652ull};
    home_controller_handle_touch(&controller, &back_down);
    home_controller_handle_touch(&controller, &back_move);
    home_controller_handle_touch(&controller, &back_up);
    TEST_ASSERT_TRUE(controller.launch_target < 0.01f);
}

static void test_now_playing_idle_active_storage_and_library_transitions(void) {
    struct home_controller controller;
    home_controller_initialize(&controller);
    controller.entry_progress = 1.0f;

    /* 1. Now Playing Idle jumps immediately to ACTIVE_SCREEN_NOW_PLAYING_SHELL */
    struct touch_event tap_down = {TOUCH_EVENT_DOWN, 180, 148, 100ull};
    struct touch_event tap_up = {TOUCH_EVENT_UP, 180, 148, 130ull};
    home_controller_handle_touch(&controller, &tap_down);
    home_controller_handle_touch(&controller, &tap_up);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_NOW_PLAYING_SHELL, controller.active_screen);

    /* Tapping BROWSE LIBRARY CTA (180, 254) jumps to Library shell */
    struct touch_event cta_down = {TOUCH_EVENT_DOWN, 180, 254, 200ull};
    struct touch_event cta_up = {TOUCH_EVENT_UP, 180, 254, 230ull};
    home_controller_handle_touch(&controller, &cta_down);
    home_controller_handle_touch(&controller, &cta_up);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_LIBRARY_SHELL, controller.active_screen);

    /* Left-edge swipe returns to Home anchored on Library (slot 1) */
    struct touch_event lib_back_down = {TOUCH_EVENT_DOWN, 24, 180, 300ull};
    struct touch_event lib_back_move = {TOUCH_EVENT_MOVE, 90, 180, 326ull};
    struct touch_event lib_back_up = {TOUCH_EVENT_UP, 90, 180, 352ull};
    home_controller_handle_touch(&controller, &lib_back_down);
    home_controller_handle_touch(&controller, &lib_back_move);
    home_controller_handle_touch(&controller, &lib_back_up);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_HOME, controller.active_screen);
    TEST_ASSERT_EQ_I32(HOME_APP_LIBRARY, home_wrap_app_slot(controller.focused_slot));
    TEST_ASSERT_EQ_I32(HOME_APP_LIBRARY, controller.launched_app_id);
    step_controller_seconds(&controller, HOME_LAUNCH_DURATION_SECONDS + 0.05f, 1.0f / 60.0f);
    TEST_ASSERT_TRUE(controller.launch_progress < 0.01f);

    /* 2. Now Playing Active runs 0.62s direct vinyl zoom preserving +20 deg angle */
    controller.scroll_position = 0.0f;
    controller.target_slot = 0;
    controller.focused_slot = 0;
    controller.telemetry.playback_mode = HOME_PLAYBACK_PLAYING;
    controller.telemetry.duration_ms = 120000u;
    controller.telemetry.elapsed_ms = 10000u;
    float initial_angle = controller.vinyl_rotation_radians;

    home_controller_handle_touch(&controller, &tap_down);
    home_controller_handle_touch(&controller, &tap_up);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_HOME, controller.active_screen);
    TEST_ASSERT_TRUE(controller.launch_target > 0.99f);

    step_controller_seconds(&controller, HOME_LAUNCH_DURATION_SECONDS + 0.02f, 1.0f / 60.0f);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_NOW_PLAYING_SHELL, controller.active_screen);
    float expected_delta = VINYL_SPIN_RADIANS_PER_SECOND * HOME_LAUNCH_DURATION_SECONDS;
    TEST_ASSERT_TRUE(fabsf((controller.vinyl_rotation_radians - initial_angle) - expected_delta) <
                     0.05f);
    TEST_ASSERT_TRUE(controller.telemetry.elapsed_ms > 10500u);

    /* 3. USB Storage opens inline preview and BACK TO HOME closes */
    controller.active_screen = ACTIVE_SCREEN_HOME;
    controller.launch_progress = 0.0f;
    controller.launch_target = 0.0f;
    controller.telemetry.playback_mode = HOME_PLAYBACK_STORAGE;
    home_controller_handle_touch(&controller, &tap_down);
    home_controller_handle_touch(&controller, &tap_up);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_HOME, controller.active_screen);
    TEST_ASSERT_TRUE(controller.launch_target > 0.99f);
    step_controller_seconds(&controller, HOME_LAUNCH_DURATION_SECONDS + 0.05f, 1.0f / 60.0f);

    home_controller_handle_touch(&controller, &cta_down);
    home_controller_handle_touch(&controller, &cta_up);
    TEST_ASSERT_TRUE(controller.launch_target < 0.01f);

    /* 4. Direct return_from_shell restores Home anchored on HOME_APP_LIBRARY */
    controller.active_screen = ACTIVE_SCREEN_LIBRARY_SHELL;
    home_controller_return_from_shell(&controller, HOME_APP_LIBRARY);
    TEST_ASSERT_EQ_I32(ACTIVE_SCREEN_HOME, controller.active_screen);
    TEST_ASSERT_EQ_I32(HOME_APP_LIBRARY, home_wrap_app_slot(controller.focused_slot));
    TEST_ASSERT_EQ_I32(HOME_APP_LIBRARY, controller.launched_app_id);
    TEST_ASSERT_TRUE(controller.launch_progress > 0.99f);
    TEST_ASSERT_TRUE(controller.launch_target < 0.01f);
}

void run_home_screen_tests(void) {
    printf("Suite: home_screen\n");
    RUN_TEST(test_volume_normalization_boundaries);
    RUN_TEST(test_home_entry_and_cyclic_wheel_physics);
    RUN_TEST(test_home_tap_neighbor_and_inline_previews);
    RUN_TEST(test_now_playing_idle_active_storage_and_library_transitions);
}
