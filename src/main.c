#include "app.h"
#include "constants.h"
#include "platform_linux.h"
#include "protocol.h"

#include <errno.h>
#include <poll.h>
#include <time.h>

static uint64_t read_boot_monotonic_ms(void) {
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
        return 0ull;
    }
    return (uint64_t)timestamp.tv_sec * MILLISECONDS_PER_SECOND +
           (uint64_t)timestamp.tv_nsec / NANOSECONDS_PER_MILLISECOND;
}

int main(void) {
    platform_linux_set_process_name();
    platform_linux_install_crash_handler(read_boot_monotonic_ms());

    struct platform_linux_state linux_state;
    struct platform_operations platform_ops;
    if (!platform_linux_initialize(&linux_state, &platform_ops)) {
        return 1;
    }

    struct app_runtime runtime;
    app_initialize(&runtime, &platform_ops);

    int ui_queue_fd = -1;
    int input_event_fd = -1;
    platform_linux_get_poll_descriptors(&linux_state, &ui_queue_fd, &input_event_fd);

    char ipc_buffer[MESSAGE_QUEUE_RECEIVE_BUFFER_BYTES];
    struct kernel_input_event input_events[16];

    while (!app_is_shutting_down(&runtime)) {
        struct pollfd poll_descriptors[2];
        nfds_t descriptor_count = 0u;
        int ui_slot = -1;
        int input_slot = -1;

        if (ui_queue_fd >= 0) {
            ui_slot = (int)descriptor_count;
            poll_descriptors[descriptor_count].fd = ui_queue_fd;
            poll_descriptors[descriptor_count].events = POLLIN;
            poll_descriptors[descriptor_count].revents = 0;
            descriptor_count++;
        }
        if (input_event_fd >= 0) {
            input_slot = (int)descriptor_count;
            poll_descriptors[descriptor_count].fd = input_event_fd;
            poll_descriptors[descriptor_count].events = POLLIN;
            poll_descriptors[descriptor_count].revents = 0;
            descriptor_count++;
        }

        uint64_t now_ns = platform_ops.monotonic_ns(platform_ops.context);
        int32_t timeout_ms = app_compute_poll_timeout_ms(&runtime, now_ns);
        int ready_count = poll(poll_descriptors, descriptor_count, timeout_ms);
        if (ready_count < 0 && errno == EINTR) {
            continue;
        }

        if (ui_slot >= 0 && (poll_descriptors[ui_slot].revents & POLLIN) != 0) {
            while (!app_is_shutting_down(&runtime)) {
                ssize_t received =
                    platform_linux_receive_ipc(&linux_state, ipc_buffer, sizeof(ipc_buffer));
                if (received <= 0) {
                    break;
                }
                struct inbound_ipc_message message;
                if (protocol_parse_inbound_ipc(ipc_buffer, (size_t)received, &message)) {
                    app_handle_ipc_message(&runtime, &message);
                }
            }
        }

        if (input_slot >= 0 && (poll_descriptors[input_slot].revents & POLLIN) != 0) {
            while (!app_is_shutting_down(&runtime)) {
                size_t event_count =
                    platform_linux_read_input_events(&linux_state, input_events,
                                                     sizeof(input_events) / sizeof(input_events[0]));
                if (event_count == 0u) {
                    break;
                }
                for (size_t index = 0u; index < event_count && !app_is_shutting_down(&runtime);
                     index++) {
                    enum physical_button_action action =
                        protocol_decode_input_event(&input_events[index]);
                    if (action != BUTTON_ACTION_NONE) {
                        app_handle_button_action(&runtime, action);
                    }
                }
            }
        }

        if (!app_is_shutting_down(&runtime)) {
            app_handle_tick(&runtime, platform_ops.monotonic_ns(platform_ops.context));
        }
    }

    platform_linux_shutdown_cleanup(&linux_state);
    return 0;
}
