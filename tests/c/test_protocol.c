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

    TEST_ASSERT_TRUE(protocol_parse_inbound_ipc("a1030010000003E8", 16u, &message));
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

void run_protocol_tests(void) {
    printf("Suite: protocol\n");
    RUN_TEST(test_parse_golden_aa24_snapshot);
    RUN_TEST(test_parse_aa24_key_boundaries_and_clamping);
    RUN_TEST(test_parse_aa24_malformed_payload);
    RUN_TEST(test_parse_aa22_a607_other_and_invalid);
    RUN_TEST(test_format_outbound_commands);
    RUN_TEST(test_decode_input_events);
}
