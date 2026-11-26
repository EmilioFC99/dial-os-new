#include "test_runner.h"

#include "constants.h"
#include "protocol.h"

#include <string.h>

static void test_parse_golden_aa24_snapshot(void) {
    const char *golden_frame =
        "aa2402700000"
        "{\"volume\":48,\"brightness\":18,\"dac_filter\":1,\"balance\":0,\"battery\":5,"
        "\"max_volume\":85,\"power_save_time\":0,\"gain\":1,\"screen_rotation\":0,"
        "\"language\":2,\"theme_mode\":2,\"charge_protect\":0,\"display_time\":1,"
        "\"lock_theme\":5,\"eq_type\":3,\"bt_codec\":4,\"wifi_status\":0,\"bt_status\":0,"
        "\"play_mode\":0,\"spdif\":0,\"auto_time\":0,\"dre_status\":1,\"local_img_anim\":1,"
        "\"img_anim_brightness\":0,\"key_single_click\":1,\"key_double_click\":0,"
        "\"key_long_press\":1,\"folder_jump\":1,\"folder_gapless\":0,\"artist_type\":0,"
        "\"work_mode_select\":0,\"input_mode\":8,\"replay_gain\":0,\"online_cover\":0,"
        "\"online_lyric\":0,\"track_display\":1,\"list_oper_mode\":1,\"font_size\":0}";
    struct inbound_ipc_message message;
    bool parsed = protocol_parse_inbound_ipc(golden_frame, strlen(golden_frame), &message);

    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQ_I32(IPC_INBOUND_BOOT_CONFIG, message.kind);
    TEST_ASSERT_TRUE(message.boot_config.has_valid_payload);
    TEST_ASSERT_EQ_U32(48u, message.boot_config.volume_level);
    TEST_ASSERT_EQ_U32(85u, message.boot_config.max_volume);
    TEST_ASSERT_EQ_U32(18u, message.boot_config.brightness);
}

static void test_parse_aa24_key_boundaries_and_clamping(void) {
    /* img_anim_brightness precedes brightness and max_volume precedes volume */
    const char *boundary_frame =
        "aa2400610000"
        "{\"img_anim_brightness\":0,\"brightness\":400,\"max_volume\":150,\"volume\":130,"
        "\"x_volume\":9}";
    struct inbound_ipc_message message;
    bool parsed = protocol_parse_inbound_ipc(boundary_frame, strlen(boundary_frame), &message);

    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQ_I32(IPC_INBOUND_BOOT_CONFIG, message.kind);
    TEST_ASSERT_TRUE(message.boot_config.has_valid_payload);
    TEST_ASSERT_EQ_U32(BRIGHTNESS_MAX_DEFAULT, message.boot_config.brightness);
    TEST_ASSERT_EQ_U32(VOLUME_HARD_MAX, message.boot_config.max_volume);
    TEST_ASSERT_EQ_U32(VOLUME_HARD_MAX, message.boot_config.volume_level);

    /* Negative and low brightness clamping, plus volume clamped against max_volume */
    const char *clamp_frame =
        "aa2400380000"
        "{\"brightness\":2,\"max_volume\":60,\"volume\":95}";
    parsed = protocol_parse_inbound_ipc(clamp_frame, strlen(clamp_frame), &message);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQ_U32(BRIGHTNESS_VISIBLE_FLOOR, message.boot_config.brightness);
    TEST_ASSERT_EQ_U32(60u, message.boot_config.max_volume);
    TEST_ASSERT_EQ_U32(60u, message.boot_config.volume_level);

    /* Non-numeric / negative values fall back to safe defaults */
    const char *fallback_frame =
        "aa24003B0000"
        "{\"brightness\":-5,\"max_volume\":0,\"volume\":\"bad\"}";
    parsed = protocol_parse_inbound_ipc(fallback_frame, strlen(fallback_frame), &message);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_TRUE(message.boot_config.has_valid_payload);
    TEST_ASSERT_EQ_U32(BRIGHTNESS_BOOT_DEFAULT, message.boot_config.brightness);
    TEST_ASSERT_EQ_U32(1u, message.boot_config.max_volume);
    TEST_ASSERT_EQ_U32(1u, message.boot_config.volume_level);
}

static void test_parse_aa24_malformed_payload(void) {
    const char *bad_json = "aa2400130000notjson";
    struct inbound_ipc_message message;
    bool parsed = protocol_parse_inbound_ipc(bad_json, strlen(bad_json), &message);

    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQ_I32(IPC_INBOUND_BOOT_CONFIG, message.kind);
    TEST_ASSERT_FALSE(message.boot_config.has_valid_payload);
    TEST_ASSERT_EQ_U32(VOLUME_DEFAULT, message.boot_config.volume_level);
    TEST_ASSERT_EQ_U32(MAX_VOLUME_DEFAULT, message.boot_config.max_volume);
    TEST_ASSERT_EQ_U32(BRIGHTNESS_BOOT_DEFAULT, message.boot_config.brightness);
}

static void test_parse_aa22_a607_other_and_invalid(void) {
    struct inbound_ipc_message message;

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("aa22000c0001", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_SDCARD_EVENT, message.kind);
    TEST_ASSERT_TRUE(message.sdcard_inserted);

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("aa22000C0000", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_SDCARD_EVENT, message.kind);
    TEST_ASSERT_FALSE(message.sdcard_inserted);

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a607000C0008", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_INPUT_MODE_READY, message.kind);

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a999000C0000", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_OTHER, message.kind);

    /* Truncated / invalid header / declared length mismatch */
    TEST_ASSERT_FALSE(protocol_parse_inbound_ipc("aa24", 4u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_INVALID, message.kind);
    TEST_ASSERT_FALSE(protocol_parse_inbound_ipc("aa2400200000", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_INVALID, message.kind);
    TEST_ASSERT_FALSE(protocol_parse_inbound_ipc("aa2400080000", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_INVALID, message.kind);
    TEST_ASSERT_FALSE(protocol_parse_inbound_ipc("aa24zzzz0000", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_INVALID, message.kind);
    TEST_ASSERT_FALSE(protocol_parse_inbound_ipc(NULL, 12u, &message));
}

static void test_format_outbound_commands(void) {
    char command_buffer[16];
    size_t written = protocol_format_disable_idle_poweroff(command_buffer, sizeof(command_buffer));
    TEST_ASSERT_EQ_U32(12u, (uint32_t)written);
    TEST_ASSERT_EQ_STR("0664000C0000", command_buffer);

    written = protocol_format_set_volume(0u, command_buffer, sizeof(command_buffer));
    TEST_ASSERT_EQ_U32(12u, (uint32_t)written);
    TEST_ASSERT_EQ_STR("0715000C0000", command_buffer);

    written = protocol_format_set_volume(48u, command_buffer, sizeof(command_buffer));
    TEST_ASSERT_EQ_U32(12u, (uint32_t)written);
    TEST_ASSERT_EQ_STR("0715000C0030", command_buffer);

    written = protocol_format_set_volume(200u, command_buffer, sizeof(command_buffer));
    TEST_ASSERT_EQ_U32(12u, (uint32_t)written);
    TEST_ASSERT_EQ_STR("0715000C0078", command_buffer);

    char tiny_buffer[12];
    TEST_ASSERT_EQ_U32(0u, (uint32_t)protocol_format_disable_idle_poweroff(tiny_buffer, sizeof(tiny_buffer)));
    TEST_ASSERT_EQ_U32(0u, (uint32_t)protocol_format_set_volume(10u, tiny_buffer, sizeof(tiny_buffer)));
    TEST_ASSERT_EQ_U32(0u, (uint32_t)protocol_format_set_volume(10u, NULL, 16u));
}

static void test_decode_input_events(void) {
    struct kernel_input_event event = {0u, 0u, INPUT_EVENT_TYPE_KEY, KEYCODE_VOLUME_UP_PRIMARY, 1};
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_VOLUME_UP, protocol_decode_input_event(&event));

    event.code = KEYCODE_VOLUME_UP_ALT_A;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_VOLUME_UP, protocol_decode_input_event(&event));
    event.code = KEYCODE_VOLUME_UP_ALT_B;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_VOLUME_UP, protocol_decode_input_event(&event));

    event.code = KEYCODE_VOLUME_DOWN_PRIMARY;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_VOLUME_DOWN, protocol_decode_input_event(&event));
    event.code = KEYCODE_VOLUME_DOWN_ALT_A;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_VOLUME_DOWN, protocol_decode_input_event(&event));
    event.code = KEYCODE_VOLUME_DOWN_ALT_B;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_VOLUME_DOWN, protocol_decode_input_event(&event));

    event.code = KEYCODE_POWER_SHORT_PRIMARY;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_POWER_SHORT, protocol_decode_input_event(&event));
    event.code = KEYCODE_POWER_SHORT_ALT;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_POWER_SHORT, protocol_decode_input_event(&event));
    event.code = KEYCODE_POWER_LONG;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_POWER_LONG, protocol_decode_input_event(&event));

    /* Release (0), repeat (2), non-key EV_SYN (0), unknown code, NULL */
    event.value = 0;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_NONE, protocol_decode_input_event(&event));
    event.value = 2;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_NONE, protocol_decode_input_event(&event));
    event.value = 1;
    event.type = 0u;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_NONE, protocol_decode_input_event(&event));
    event.type = INPUT_EVENT_TYPE_KEY;
    event.code = 999u;
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_NONE, protocol_decode_input_event(&event));
    TEST_ASSERT_EQ_I32(BUTTON_ACTION_NONE, protocol_decode_input_event(NULL));
}

static void test_parse_playback_volume_and_usb_events(void) {
    struct inbound_ipc_message message;

    /* Real V2.57 a202 compact 8-byte header with double-escaped song JSON */
    const char *track_frame =
        "a2020105{\"song\":\"{\\\"song_name\\\":\\\"Midnight \\\\\\\"City\\\\\\\"\\\","
        "\\\"song_duration_time\\\":243000,\\\"song_artist_name\\\":\\\"M83\\\"}\","
        "\"state\":2,\"playerflag\":4}";
    /* Recompute exact 4-hex length so test fixture is valid */
    char formatted_track_frame[512];
    const char *track_json =
        "{\"song\":\"{\\\"song_name\\\":\\\"Midnight \\\\\\\"City\\\\\\\"\\\","
        "\\\"song_duration_time\\\":243000,\\\"song_artist_name\\\":\\\"M83\\\"}\","
        "\"state\":2,\"playerflag\":4}";
    size_t track_total_length = 8u + strlen(track_json);
    snprintf(formatted_track_frame, sizeof(formatted_track_frame), "a202%04X%s",
             (unsigned)track_total_length, track_json);
    (void)track_frame;

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc(formatted_track_frame, track_total_length, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_PLAYBACK_STATUS, message.kind);
    TEST_ASSERT_TRUE(message.playback_status.has_state);
    TEST_ASSERT_EQ_I32(IPC_PLAYBACK_PLAYING, message.playback_status.state);
    TEST_ASSERT_TRUE(message.playback_status.has_track_metadata);
    TEST_ASSERT_EQ_STR("Midnight \"City\"", message.playback_status.track_title);
    TEST_ASSERT_EQ_U32(243000u, message.playback_status.duration_ms);

    /* Minimal playing and paused state-only a202 frames */
    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a2020013{\"state\":0}", 19u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_PLAYBACK_STATUS, message.kind);
    TEST_ASSERT_TRUE(message.playback_status.has_state);
    TEST_ASSERT_EQ_I32(IPC_PLAYBACK_PLAYING, message.playback_status.state);
    TEST_ASSERT_FALSE(message.playback_status.has_track_metadata);

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a2020013{\"state\":1}", 19u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_PLAYBACK_STATUS, message.kind);
    TEST_ASSERT_EQ_I32(IPC_PLAYBACK_PAUSED, message.playback_status.state);

    /* Oversized title truncation and malformed a202 */
    char long_title_json[320];
    char long_title_frame[340];
    memset(long_title_json, 'A', 160u);
    long_title_json[160] = '\0';
    snprintf(long_title_frame, sizeof(long_title_frame),
             "a202%04X{\"song\":\"{\\\"song_name\\\":\\\"%s\\\",\\\"song_duration_time\\\":1000}\",\"state\":0}",
             (unsigned)(8u + 58u + 160u), long_title_json);
    /* Build cleanly with snprintf for exact length */
    char inner_json[256];
    snprintf(inner_json, sizeof(inner_json),
             "{\"song\":\"{\\\"song_name\\\":\\\"%s\\\",\\\"song_duration_time\\\":1000}\",\"state\":0}",
             long_title_json);
    size_t inner_len = strlen(inner_json);
    snprintf(long_title_frame, sizeof(long_title_frame), "a202%04X%s",
             (unsigned)(8u + inner_len), inner_json);
    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc(long_title_frame, 8u + inner_len, &message));
    TEST_ASSERT_TRUE(message.playback_status.has_track_metadata);
    TEST_ASSERT_EQ_U32((uint32_t)(TRACK_TITLE_CAPACITY - 1u),
                       (uint32_t)strlen(message.playback_status.track_title));

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a2020010not_json", 16u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_PLAYBACK_STATUS, message.kind);
    TEST_ASSERT_FALSE(message.playback_status.has_state);

    /* a103 elapsed ms (compact 8-byte header + 8 hex digits = 16 bytes) */
    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a103001000004A38", 16u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_PLAYBACK_ELAPSED, message.kind);
    TEST_ASSERT_EQ_U32(19000u, message.elapsed_ms);
    TEST_ASSERT_FALSE(protocol_parse_inbound_ipc("a10300100000ZZZZ", 16u, &message));

    /* a102 stop, a714/a715 volume, aa1f USB cable, aa1d/aa1e battery */
    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a102000C0000", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_PLAYBACK_STOP, message.kind);

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a714000C004A", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_VOLUME_UPDATE, message.kind);
    TEST_ASSERT_EQ_U32(74u, message.volume_level);

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a715000C00FF", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_VOLUME_UPDATE, message.kind);
    TEST_ASSERT_EQ_U32(VOLUME_HARD_MAX, message.volume_level);

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("aa1f000C0001", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_USB_CABLE_EVENT, message.kind);
    TEST_ASSERT_TRUE(message.usb_connected);

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("aa1f000C0000", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_USB_CABLE_EVENT, message.kind);
    TEST_ASSERT_FALSE(message.usb_connected);

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("aa1d000C0005", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_BATTERY_EVENT, message.kind);
    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("aa1e000C0001", 12u, &message));
    TEST_ASSERT_EQ_I32(IPC_INBOUND_BATTERY_EVENT, message.kind);
}

static void test_decode_touch_events(void) {
    struct touch_event_decoder decoder;
    struct touch_event touch;
    protocol_touch_decoder_initialize(&decoder);

    struct kernel_input_event events[5] = {
        {1u, 20000u, INPUT_EVENT_TYPE_ABS, ABS_CODE_MT_TRACKING_ID, 0},
        {1u, 20000u, INPUT_EVENT_TYPE_ABS, ABS_CODE_MT_POSITION_X, 100},
        {1u, 20000u, INPUT_EVENT_TYPE_ABS, ABS_CODE_MT_POSITION_Y, 200},
        {1u, 20000u, INPUT_EVENT_TYPE_KEY, KEYCODE_BTN_TOUCH, 1},
        {1u, 20000u, INPUT_EVENT_TYPE_SYN, SYN_CODE_REPORT, 0}
    };

    for (size_t index = 0u; index < 4u; index++) {
        TEST_ASSERT_EQ_I32(TOUCH_EVENT_NONE,
                           protocol_touch_decoder_feed(&decoder, &events[index], &touch));
    }
    TEST_ASSERT_EQ_I32(TOUCH_EVENT_DOWN,
                       protocol_touch_decoder_feed(&decoder, &events[4], &touch));
    TEST_ASSERT_EQ_I32(259, touch.x);
    TEST_ASSERT_EQ_I32(159, touch.y);
    TEST_ASSERT_EQ_U64(1020ull, touch.timestamp_ms);

    /* Move updates X while retaining Y */
    struct kernel_input_event move_x = {1u, 46000u, INPUT_EVENT_TYPE_ABS, ABS_CODE_MT_POSITION_X, 120};
    struct kernel_input_event syn = {1u, 46000u, INPUT_EVENT_TYPE_SYN, SYN_CODE_REPORT, 0};
    TEST_ASSERT_EQ_I32(TOUCH_EVENT_NONE, protocol_touch_decoder_feed(&decoder, &move_x, &touch));
    TEST_ASSERT_EQ_I32(TOUCH_EVENT_MOVE, protocol_touch_decoder_feed(&decoder, &syn, &touch));
    TEST_ASSERT_EQ_I32(239, touch.x);
    TEST_ASSERT_EQ_I32(159, touch.y);

    /* Out-of-bounds raw coordinates clamp to [0..359] */
    struct kernel_input_event clamp_x = {1u, 60000u, INPUT_EVENT_TYPE_ABS, ABS_CODE_MT_POSITION_X, 500};
    struct kernel_input_event clamp_y = {1u, 60000u, INPUT_EVENT_TYPE_ABS, ABS_CODE_MT_POSITION_Y, -10};
    protocol_touch_decoder_feed(&decoder, &clamp_x, &touch);
    protocol_touch_decoder_feed(&decoder, &clamp_y, &touch);
    TEST_ASSERT_EQ_I32(TOUCH_EVENT_MOVE, protocol_touch_decoder_feed(&decoder, &syn, &touch));
    TEST_ASSERT_EQ_I32(0, touch.x);
    TEST_ASSERT_EQ_I32(359, touch.y);

    /* Lift via tracking ID -1 */
    struct kernel_input_event lift = {1u, 80000u, INPUT_EVENT_TYPE_ABS, ABS_CODE_MT_TRACKING_ID, -1};
    protocol_touch_decoder_feed(&decoder, &lift, &touch);
    TEST_ASSERT_EQ_I32(TOUCH_EVENT_UP, protocol_touch_decoder_feed(&decoder, &syn, &touch));
    TEST_ASSERT_EQ_I32(0, touch.x);
    TEST_ASSERT_EQ_I32(359, touch.y);

    /* Duplicate SYN_REPORT while idle emits NONE */
    TEST_ASSERT_EQ_I32(TOUCH_EVENT_NONE, protocol_touch_decoder_feed(&decoder, &syn, &touch));
}

void run_protocol_tests(void) {
    printf("Suite: protocol\n");
    RUN_TEST(test_parse_golden_aa24_snapshot);
    RUN_TEST(test_parse_aa24_key_boundaries_and_clamping);
    RUN_TEST(test_parse_aa24_malformed_payload);
    RUN_TEST(test_parse_aa22_a607_other_and_invalid);
    RUN_TEST(test_format_outbound_commands);
    RUN_TEST(test_decode_input_events);
    RUN_TEST(test_parse_playback_volume_and_usb_events);
    RUN_TEST(test_decode_touch_events);
}
