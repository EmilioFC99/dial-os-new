#ifndef DIALOS_PLATFORM_LINUX_H
#define DIALOS_PLATFORM_LINUX_H

#include "platform.h"
#include "protocol.h"

#include <mqueue.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct platform_linux_state {
    int fb_fd;
    uint32_t *fb_map;
    size_t fb_map_bytes;
    uint32_t fb_stride_pixels;
    uint32_t fb_yres;
    uint32_t fb_page_count;
    uint32_t fb_active_page;
    uint32_t fb_draw_page;
    int event0_fd;
    int event1_fd;
    mqd_t ui_queue;
    mqd_t player_queue;
    uint8_t max_brightness;
    uint32_t rss_ready_kb;
    uint32_t initial_watchdog_restarts;
    pthread_mutex_t sd_free_mutex;
    bool sd_free_probe_running;
    bool sd_free_cached_valid;
    uint64_t sd_free_cached_megabytes;
    bool sd_was_mounted;
};

void platform_linux_set_process_name(void);
void platform_linux_install_crash_handler(uint64_t boot_start_ms);
bool platform_linux_initialize(struct platform_linux_state *state,
                               struct platform_operations *operations_out);
void platform_linux_get_poll_descriptors(const struct platform_linux_state *state,
                                         int *ui_queue_fd_out, int *input_event_fd_out,
                                         int *touch_event_fd_out);
ssize_t platform_linux_receive_ipc(struct platform_linux_state *state,
                                   char *buffer, size_t capacity);
size_t platform_linux_read_input_events(struct platform_linux_state *state,
                                        struct kernel_input_event *events, size_t max_events);
size_t platform_linux_read_touch_events(struct platform_linux_state *state,
                                        struct kernel_input_event *events, size_t max_events);
void platform_linux_shutdown_cleanup(struct platform_linux_state *state);

#endif
