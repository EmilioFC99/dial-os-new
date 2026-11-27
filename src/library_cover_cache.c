#include "library_cover_cache.h"

#include "jpeg_fast_thumb.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop

static void copy_path_bounded(char *output, size_t capacity, const char *input) {
    if (capacity == 0u) {
        return;
    }
    if (input == NULL) {
        output[0] = '\0';
        return;
    }
    size_t length = strlen(input);
    if (length >= capacity) {
        length = capacity - 1u;
    }
    memcpy(output, input, length);
    output[length] = '\0';
}

void library_cover_cache_initialize(struct library_cover_cache *cache,
                                    const char *covers_directory) {
    if (cache == NULL) {
        return;
    }
    memset(cache, 0, sizeof(*cache));
    copy_path_bounded(cache->covers_directory, sizeof(cache->covers_directory), covers_directory);
    cache->slots = calloc(LIBRARY_COVER_LRU_SLOTS, sizeof(struct library_cover_slot));
}

void library_cover_cache_set_directory(struct library_cover_cache *cache,
                                       const char *covers_directory) {
    if (cache == NULL) {
        return;
    }
    copy_path_bounded(cache->covers_directory, sizeof(cache->covers_directory), covers_directory);
}

void library_cover_cache_clear(struct library_cover_cache *cache) {
    if (cache == NULL) {
        return;
    }
    library_cover_cache_cancel_boot_precache(cache);
    if (cache->slots != NULL) {
        memset(cache->slots, 0, LIBRARY_COVER_LRU_SLOTS * sizeof(struct library_cover_slot));
    }
    cache->pending_count = 0u;
    cache->failed_count = 0u;
    cache->access_tick = 0ull;
    cache->precache_active = false;
    cache->precache_cursor = 0u;
    cache->precache_total = 0u;
    cache->precache_done = 0u;
    cache->precache_decoded = 0u;
    cache->precache_disk_hits = 0u;
    cache->precache_ram_preloaded = 0u;
    cache->precache_failed = 0u;
    cache->precache_max_decode_ms = 0u;
    cache->last_fail_reason = LIBRARY_COVER_FAIL_NONE;
    cache->last_fail_path[0] = '\0';
    cache->precache_fail_log_read_cursor = 0u;
    atomic_store(&cache->precache_fail_log_count, 0u);
    cache->window_ram_hits = 0u;
    cache->window_disk_hits = 0u;
    cache->window_decodes = 0u;
    cache->window_max_op_ms = 0u;
}

void library_cover_cache_destroy(struct library_cover_cache *cache) {
    if (cache == NULL) {
        return;
    }
    library_cover_cache_cancel_boot_precache(cache);
    free(cache->slots);
    memset(cache, 0, sizeof(*cache));
}

static bool is_album_marked_failed(const struct library_cover_cache *cache, uint64_t album_id) {
    for (uint32_t index = 0u; index < cache->failed_count; index++) {
        if (cache->failed_album_ids[index] == album_id) {
            return true;
        }
    }
    return false;
}

static void mark_album_failed(struct library_cover_cache *cache, uint64_t album_id) {
    if (is_album_marked_failed(cache, album_id)) {
        return;
    }
    if (cache->failed_count < LIBRARY_COVER_FAILED_SET_CAPACITY) {
        cache->failed_album_ids[cache->failed_count++] = album_id;
    }
}

static struct library_cover_slot *allocate_lru_slot(struct library_cover_cache *cache,
                                                    uint64_t album_id) {
    if (cache->slots == NULL) {
        return NULL;
    }
    uint32_t victim_index = 0u;
    uint64_t oldest_tick = UINT64_MAX;
    for (uint32_t index = 0u; index < LIBRARY_COVER_LRU_SLOTS; index++) {
        if (!cache->slots[index].occupied) {
            victim_index = index;
            break;
        }
        if (cache->slots[index].album_id == album_id) {
            victim_index = index;
            break;
        }
        if (cache->slots[index].last_used_tick < oldest_tick) {
            oldest_tick = cache->slots[index].last_used_tick;
            victim_index = index;
        }
    }
    struct library_cover_slot *slot = &cache->slots[victim_index];
    slot->occupied = true;
    slot->album_id = album_id;
    slot->last_used_tick = ++cache->access_tick;
    return slot;
}

static bool is_album_pending(const struct library_cover_cache *cache, uint64_t album_id) {
    for (uint32_t index = 0u; index < cache->pending_count; index++) {
        if (cache->pending[index].album_id == album_id) {
            return true;
        }
    }
    return false;
}

static bool has_slot_in_ram(const struct library_cover_cache *cache, uint64_t album_id) {
    if (cache->slots == NULL || album_id == 0ull) {
        return false;
    }
    for (uint32_t index = 0u; index < LIBRARY_COVER_LRU_SLOTS; index++) {
        if (cache->slots[index].occupied && cache->slots[index].album_id == album_id) {
            return true;
        }
    }
    return false;
}

const uint16_t *library_cover_cache_lookup(struct library_cover_cache *cache, uint64_t album_id) {
    if (cache == NULL || cache->slots == NULL || album_id == 0ull) {
        return NULL;
    }
    for (uint32_t index = 0u; index < LIBRARY_COVER_LRU_SLOTS; index++) {
        if (cache->slots[index].occupied && cache->slots[index].album_id == album_id) {
            cache->slots[index].last_used_tick = ++cache->access_tick;
            cache->window_ram_hits++;
            return cache->slots[index].pixels;
        }
    }
    return NULL;
}

bool library_cover_cache_request(struct library_cover_cache *cache, uint64_t album_id,
                                 uint8_t cover_source_kind, const char *source_path,
                                 uint32_t data_offset, uint32_t data_length) {
    if (cache == NULL || album_id == 0ull || cover_source_kind == LIBRARY_COVER_NONE ||
        source_path == NULL || source_path[0] == '\0') {
        return false;
    }
    if (library_cover_cache_lookup(cache, album_id) != NULL ||
        is_album_marked_failed(cache, album_id)) {
        return false;
    }
    for (uint32_t index = 0u; index < cache->pending_count; index++) {
        if (cache->pending[index].album_id == album_id) {
            return true;
        }
    }
    if (cache->pending_count >= LIBRARY_COVER_PENDING_QUEUE_CAPACITY) {
        return false;
    }
    struct library_cover_request *request = &cache->pending[cache->pending_count++];
    memset(request, 0, sizeof(*request));
    request->album_id = album_id;
    request->cover_source_kind = cover_source_kind;
    copy_path_bounded(request->source_path, sizeof(request->source_path), source_path);
    request->data_offset = data_offset;
    request->data_length = data_length;
    return true;
}

static bool has_rgb565_on_disk(const struct library_cover_cache *cache, uint64_t album_id) {
    if (cache->covers_directory[0] == '\0' || album_id == 0ull) {
        return false;
    }
    char cache_path[384];
    snprintf(cache_path, sizeof(cache_path), "%s/%016" PRIx64 ".rgb565", cache->covers_directory,
             album_id);
    struct stat file_stat;
    return stat(cache_path, &file_stat) == 0 &&
           (uint64_t)file_stat.st_size == (uint64_t)LIBRARY_COVER_BYTE_SIZE;
}

static const uint16_t *try_load_rgb565_from_disk(struct library_cover_cache *cache,
                                                 uint64_t album_id) {
    if (cache->covers_directory[0] == '\0') {
        return NULL;
    }
    char cache_path[384];
    snprintf(cache_path, sizeof(cache_path), "%s/%016" PRIx64 ".rgb565", cache->covers_directory,
             album_id);
    struct stat file_stat;
    if (stat(cache_path, &file_stat) != 0 ||
        (uint64_t)file_stat.st_size != (uint64_t)LIBRARY_COVER_BYTE_SIZE) {
        return NULL;
    }
    FILE *file = fopen(cache_path, "rb");
    if (file == NULL) {
        return NULL;
    }
    struct library_cover_slot *slot = allocate_lru_slot(cache, album_id);
    if (slot == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read_bytes = fread(slot->pixels, 1u, LIBRARY_COVER_BYTE_SIZE, file);
    fclose(file);
    if (read_bytes != LIBRARY_COVER_BYTE_SIZE) {
        slot->occupied = false;
        return NULL;
    }
    return slot->pixels;
}

const uint16_t *library_cover_cache_lookup_or_load_disk(struct library_cover_cache *cache,
                                                        uint64_t album_id) {
    const uint16_t *ram_hit = library_cover_cache_lookup(cache, album_id);
    if (ram_hit != NULL) {
        return ram_hit;
    }
    if (cache == NULL || album_id == 0ull || is_album_marked_failed(cache, album_id) ||
        is_album_pending(cache, album_id)) {
        return NULL;
    }
    const uint16_t *disk_hit = try_load_rgb565_from_disk(cache, album_id);
    if (disk_hit != NULL) {
        cache->window_disk_hits++;
    }
    return disk_hit;
}

static void save_rgb565_to_disk_atomic(const struct library_cover_cache *cache, uint64_t album_id,
                                       const uint16_t *pixels) {
    if (cache->covers_directory[0] == '\0' || pixels == NULL) {
        return;
    }
    if (mkdir(cache->covers_directory, 0755) != 0 && errno != EEXIST) {
        return;
    }
    char temp_path[384];
    char final_path[384];
    snprintf(temp_path, sizeof(temp_path), "%s/.%016" PRIx64 ".rgb565.tmp",
             cache->covers_directory, album_id);
    snprintf(final_path, sizeof(final_path), "%s/%016" PRIx64 ".rgb565",
             cache->covers_directory, album_id);

    int fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return;
    }
    const uint8_t *bytes = (const uint8_t *)pixels;
    size_t written_total = 0u;
    bool ok = true;
    while (written_total < LIBRARY_COVER_BYTE_SIZE) {
        ssize_t chunk = write(fd, bytes + written_total, LIBRARY_COVER_BYTE_SIZE - written_total);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        written_total += (size_t)chunk;
    }
    close(fd);
    if (!ok || rename(temp_path, final_path) != 0) {
        (void)unlink(temp_path);
    }
}

static void downsample_rgb888_to_rgb565(const uint8_t *rgb_pixels, uint32_t source_width,
                                        uint32_t source_height, uint16_t *dest_pixels) {
    for (uint32_t dest_y = 0u; dest_y < LIBRARY_COVER_SIZE_PX; dest_y++) {
        uint32_t y_start = (dest_y * source_height) / LIBRARY_COVER_SIZE_PX;
        uint32_t y_end = ((dest_y + 1u) * source_height) / LIBRARY_COVER_SIZE_PX;
        if (y_end <= y_start) {
            y_end = y_start + 1u;
        }
        if (y_end > source_height) {
            y_end = source_height;
        }
        uint32_t step_y = (y_end - y_start) > 4u ? ((y_end - y_start) / 4u) : 1u;
        for (uint32_t dest_x = 0u; dest_x < LIBRARY_COVER_SIZE_PX; dest_x++) {
            uint32_t x_start = (dest_x * source_width) / LIBRARY_COVER_SIZE_PX;
            uint32_t x_end = ((dest_x + 1u) * source_width) / LIBRARY_COVER_SIZE_PX;
            if (x_end <= x_start) {
                x_end = x_start + 1u;
            }
            if (x_end > source_width) {
                x_end = source_width;
            }
            uint32_t step_x = (x_end - x_start) > 4u ? ((x_end - x_start) / 4u) : 1u;

            uint32_t sum_red = 0u;
            uint32_t sum_green = 0u;
            uint32_t sum_blue = 0u;
            uint32_t sample_count = 0u;
            for (uint32_t sample_y = y_start; sample_y < y_end; sample_y += step_y) {
                const uint8_t *row_ptr = rgb_pixels + ((size_t)sample_y * source_width) * 3u;
                for (uint32_t sample_x = x_start; sample_x < x_end; sample_x += step_x) {
                    const uint8_t *pixel = row_ptr + (size_t)sample_x * 3u;
                    sum_red += pixel[0];
                    sum_green += pixel[1];
                    sum_blue += pixel[2];
                    sample_count++;
                }
            }
            uint32_t red8 = sum_red / sample_count;
            uint32_t green8 = sum_green / sample_count;
            uint32_t blue8 = sum_blue / sample_count;
            uint16_t rgb565 = (uint16_t)(((red8 >> 3u) << 11u) | ((green8 >> 2u) << 5u) |
                                         (blue8 >> 3u));
            dest_pixels[dest_y * LIBRARY_COVER_SIZE_PX + dest_x] = rgb565;
        }
    }
}

static void record_cover_failure(struct library_cover_cache *cache,
                                 enum library_cover_fail_reason reason, const char *source_path) {
    cache->last_fail_reason = reason;
    copy_path_bounded(cache->last_fail_path, sizeof(cache->last_fail_path), source_path);
}

static bool decode_request_to_pixels(const struct library_cover_request *request,
                                     uint16_t *pixels_out,
                                     enum library_cover_fail_reason *fail_reason_out) {
    if (jpeg_fast_thumb_decode_file(request->source_path, request->data_offset,
                                    request->data_length, pixels_out, LIBRARY_COVER_SIZE_PX)) {
        return true;
    }

    FILE *file = fopen(request->source_path, "rb");
    if (file == NULL) {
        *fail_reason_out = LIBRARY_COVER_FAIL_OPEN;
        return false;
    }

    size_t payload_length = 0u;
    if (request->cover_source_kind == LIBRARY_COVER_SIDECAR) {
        if (fseek(file, 0L, SEEK_END) != 0) {
            fclose(file);
            *fail_reason_out = LIBRARY_COVER_FAIL_SIZE;
            return false;
        }
        long file_size = ftell(file);
        if (file_size <= 0 || (uint64_t)file_size > LIBRARY_COVER_MAX_INPUT_BYTES ||
            fseek(file, 0L, SEEK_SET) != 0) {
            fclose(file);
            *fail_reason_out = LIBRARY_COVER_FAIL_SIZE;
            return false;
        }
        payload_length = (size_t)file_size;
    } else {
        if (request->data_length == 0u || request->data_length > LIBRARY_COVER_MAX_INPUT_BYTES ||
            fseek(file, (long)request->data_offset, SEEK_SET) != 0) {
            fclose(file);
            *fail_reason_out = LIBRARY_COVER_FAIL_SIZE;
            return false;
        }
        payload_length = request->data_length;
    }

    uint8_t *raw_bytes = malloc(payload_length);
    if (raw_bytes == NULL) {
        fclose(file);
        *fail_reason_out = LIBRARY_COVER_FAIL_SIZE;
        return false;
    }
    size_t read_bytes = fread(raw_bytes, 1u, payload_length, file);
    fclose(file);
    if (read_bytes != payload_length) {
        free(raw_bytes);
        *fail_reason_out = LIBRARY_COVER_FAIL_OPEN;
        return false;
    }

    int image_width = 0;
    int image_height = 0;
    int image_channels = 0;
    if (!stbi_info_from_memory(raw_bytes, (int)payload_length, &image_width, &image_height,
                               &image_channels) ||
        image_width <= 0 || image_height <= 0 ||
        (uint32_t)image_width > LIBRARY_COVER_MAX_DIMENSION_PX ||
        (uint32_t)image_height > LIBRARY_COVER_MAX_DIMENSION_PX) {
        free(raw_bytes);
        *fail_reason_out = LIBRARY_COVER_FAIL_INFO;
        return false;
    }

    int decoded_width = 0;
    int decoded_height = 0;
    int decoded_channels = 0;
    uint8_t *decoded = stbi_load_from_memory(raw_bytes, (int)payload_length, &decoded_width,
                                             &decoded_height, &decoded_channels, 3);
    free(raw_bytes);
    if (decoded == NULL || decoded_width <= 0 || decoded_height <= 0) {
        *fail_reason_out = LIBRARY_COVER_FAIL_DECODE;
        return false;
    }

    downsample_rgb888_to_rgb565(decoded, (uint32_t)decoded_width, (uint32_t)decoded_height,
                                pixels_out);
    stbi_image_free(decoded);
    return true;
}

static bool decode_request_to_cache(struct library_cover_cache *cache,
                                    const struct library_cover_request *request) {
    struct library_cover_slot *slot = allocate_lru_slot(cache, request->album_id);
    if (slot == NULL) {
        record_cover_failure(cache, LIBRARY_COVER_FAIL_SLOT, request->source_path);
        return false;
    }
    enum library_cover_fail_reason fail_reason = LIBRARY_COVER_FAIL_NONE;
    if (!decode_request_to_pixels(request, slot->pixels, &fail_reason)) {
        slot->occupied = false;
        record_cover_failure(cache, fail_reason, request->source_path);
        return false;
    }
    save_rgb565_to_disk_atomic(cache, request->album_id, slot->pixels);
    return true;
}

bool library_cover_cache_step(struct library_cover_cache *cache, bool allow_heavy_decode) {
    if (cache == NULL || cache->pending_count == 0u) {
        return false;
    }

    /* Fast path: fulfill any queued disk .rgb565 cache hit first */
    for (uint32_t index = 0u; index < cache->pending_count; index++) {
        uint64_t album_id = cache->pending[index].album_id;
        if (try_load_rgb565_from_disk(cache, album_id) != NULL) {
            for (uint32_t shift = index; shift + 1u < cache->pending_count; shift++) {
                cache->pending[shift] = cache->pending[shift + 1u];
            }
            cache->pending_count--;
            cache->window_disk_hits++;
            return true;
        }
    }

    if (!allow_heavy_decode) {
        return false;
    }

    struct library_cover_request request = cache->pending[0];
    for (uint32_t shift = 0u; shift + 1u < cache->pending_count; shift++) {
        cache->pending[shift] = cache->pending[shift + 1u];
    }
    cache->pending_count--;

    if (decode_request_to_cache(cache, &request)) {
        cache->window_decodes++;
    } else {
        mark_album_failed(cache, request.album_id);
    }
    return true;
}

static uint64_t worker_monotonic_ns(void) {
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
        return 0ull;
    }
    return (uint64_t)timestamp.tv_sec * 1000000000ull + (uint64_t)timestamp.tv_nsec;
}

static void *precache_worker_main(void *argument) {
    struct library_cover_cache *cache = (struct library_cover_cache *)argument;
    uint16_t pixels[LIBRARY_COVER_PIXEL_COUNT];
    uint32_t max_decode_ms = 0u;

    for (uint32_t job_idx = 0u; job_idx < cache->precache_job_count; job_idx++) {
        if (atomic_load(&cache->precache_cancel_requested)) {
            break;
        }
        const struct library_cover_request *job = &cache->precache_jobs[job_idx];
        if (has_rgb565_on_disk(cache, job->album_id)) {
            atomic_fetch_add(&cache->precache_atomic_disk_hits, 1u);
            atomic_fetch_add(&cache->precache_atomic_done, 1u);
            continue;
        }

        uint64_t start_ns = worker_monotonic_ns();
        enum library_cover_fail_reason fail_reason = LIBRARY_COVER_FAIL_NONE;
        if (decode_request_to_pixels(job, pixels, &fail_reason)) {
            save_rgb565_to_disk_atomic(cache, job->album_id, pixels);
            atomic_fetch_add(&cache->precache_atomic_decoded, 1u);
        } else {
            uint32_t fail_slot = atomic_load(&cache->precache_fail_log_count);
            if (fail_slot < LIBRARY_COVER_MAX_FAIL_LOGS) {
                cache->precache_fail_logs[fail_slot].done_index =
                    atomic_load(&cache->precache_atomic_done) + 1u;
                cache->precache_fail_logs[fail_slot].reason = fail_reason;
                copy_path_bounded(cache->precache_fail_logs[fail_slot].path,
                                  sizeof(cache->precache_fail_logs[fail_slot].path),
                                  job->source_path);
                atomic_store(&cache->precache_fail_log_count, fail_slot + 1u);
            }
            atomic_fetch_add(&cache->precache_atomic_failed, 1u);
        }
        uint64_t elapsed_ms = (worker_monotonic_ns() - start_ns) / 1000000ull;
        if ((uint32_t)elapsed_ms > max_decode_ms) {
            max_decode_ms = (uint32_t)elapsed_ms;
            atomic_store(&cache->precache_atomic_max_decode_ms, max_decode_ms);
        }
        atomic_fetch_add(&cache->precache_atomic_done, 1u);
    }

    atomic_store(&cache->precache_worker_finished, true);
    return NULL;
}

void library_cover_cache_begin_boot_precache(struct library_cover_cache *cache,
                                             const struct library_catalog *catalog) {
    if (cache == NULL) {
        return;
    }
    library_cover_cache_cancel_boot_precache(cache);
    cache->precache_active = false;
    cache->precache_cursor = 0u;
    cache->precache_total = 0u;
    cache->precache_done = 0u;
    cache->precache_decoded = 0u;
    cache->precache_disk_hits = 0u;
    cache->precache_ram_preloaded = 0u;
    cache->precache_failed = 0u;
    cache->precache_max_decode_ms = 0u;
    cache->precache_fail_log_read_cursor = 0u;
    atomic_store(&cache->precache_cancel_requested, false);
    atomic_store(&cache->precache_worker_finished, false);
    atomic_store(&cache->precache_atomic_done, 0u);
    atomic_store(&cache->precache_atomic_decoded, 0u);
    atomic_store(&cache->precache_atomic_disk_hits, 0u);
    atomic_store(&cache->precache_atomic_failed, 0u);
    atomic_store(&cache->precache_atomic_max_decode_ms, 0u);
    atomic_store(&cache->precache_fail_log_count, 0u);

    if (catalog == NULL || catalog->album_count == 0u || cache->covers_directory[0] == '\0') {
        return;
    }
    uint32_t cover_count = 0u;
    for (uint32_t index = 0u; index < catalog->album_count; index++) {
        if (catalog->albums[index].cover_source_kind != LIBRARY_COVER_NONE) {
            cover_count++;
        }
    }
    if (cover_count == 0u) {
        return;
    }

    cache->precache_jobs = calloc(cover_count, sizeof(struct library_cover_request));
    if (cache->precache_jobs == NULL) {
        return;
    }
    uint32_t job_index = 0u;
    for (uint32_t index = 0u; index < catalog->album_count; index++) {
        const struct library_album *album = &catalog->albums[index];
        if (album->cover_source_kind == LIBRARY_COVER_NONE) {
            continue;
        }
        struct library_cover_request *job = &cache->precache_jobs[job_index++];
        job->album_id = album->album_id;
        job->cover_source_kind = album->cover_source_kind;
        copy_path_bounded(job->source_path, sizeof(job->source_path),
                          library_catalog_get_string(catalog, album->cover_path_offset));
        job->data_offset = album->cover_data_offset;
        job->data_length = album->cover_data_length;
    }
    cache->precache_job_count = job_index;
    cache->precache_total = job_index;
    cache->precache_active = true;

    if (pthread_create(&cache->precache_thread, NULL, precache_worker_main, cache) == 0) {
        cache->precache_thread_running = true;
    } else {
        /* Fallback to synchronous stepping if thread creation fails */
        cache->precache_thread_running = false;
        free(cache->precache_jobs);
        cache->precache_jobs = NULL;
        cache->precache_job_count = 0u;
    }
}

void library_cover_cache_cancel_boot_precache(struct library_cover_cache *cache) {
    if (cache == NULL) {
        return;
    }
    if (cache->precache_thread_running) {
        atomic_store(&cache->precache_cancel_requested, true);
        (void)pthread_join(cache->precache_thread, NULL);
        cache->precache_thread_running = false;
    }
    free(cache->precache_jobs);
    cache->precache_jobs = NULL;
    cache->precache_job_count = 0u;
    cache->precache_active = false;
}

bool library_cover_cache_is_boot_precaching(const struct library_cover_cache *cache) {
    return cache != NULL && cache->precache_active;
}

bool library_cover_cache_step_boot_precache(struct library_cover_cache *cache,
                                            const struct library_catalog *catalog,
                                            uint64_t (*monotonic_ns)(void *context),
                                            void *clock_context, uint64_t budget_ns) {
    if (cache == NULL || !cache->precache_active || catalog == NULL) {
        return false;
    }

    if (cache->precache_thread_running) {
        cache->precache_done = atomic_load(&cache->precache_atomic_done);
        cache->precache_decoded = atomic_load(&cache->precache_atomic_decoded);
        cache->precache_disk_hits = atomic_load(&cache->precache_atomic_disk_hits);
        cache->precache_failed = atomic_load(&cache->precache_atomic_failed);
        cache->precache_max_decode_ms = atomic_load(&cache->precache_atomic_max_decode_ms);

        if (!atomic_load(&cache->precache_worker_finished)) {
            return true;
        }
        (void)pthread_join(cache->precache_thread, NULL);
        cache->precache_thread_running = false;
        free(cache->precache_jobs);
        cache->precache_jobs = NULL;
        cache->precache_job_count = 0u;
        cache->precache_cursor = 0u;
    }

    if (cache->precache_jobs == NULL && !cache->precache_thread_running &&
        atomic_load(&cache->precache_worker_finished)) {
        /* Cooperative RAM preload of up to LIBRARY_COVER_BOOT_RAM_PRELOAD_LIMIT covers */
        uint64_t start_ns = (monotonic_ns != NULL) ? monotonic_ns(clock_context) : 0ull;
        while (cache->precache_cursor < catalog->album_count &&
               cache->precache_ram_preloaded < LIBRARY_COVER_BOOT_RAM_PRELOAD_LIMIT) {
            const struct library_album *album = &catalog->albums[cache->precache_cursor];
            cache->precache_cursor++;
            if (album->cover_source_kind == LIBRARY_COVER_NONE) {
                continue;
            }
            if (!has_slot_in_ram(cache, album->album_id) &&
                try_load_rgb565_from_disk(cache, album->album_id) != NULL) {
                cache->precache_ram_preloaded++;
            }
            if (monotonic_ns != NULL && budget_ns > 0ull &&
                (monotonic_ns(clock_context) - start_ns) >= budget_ns) {
                break;
            }
        }
        if (cache->precache_cursor >= catalog->album_count ||
            cache->precache_ram_preloaded >= LIBRARY_COVER_BOOT_RAM_PRELOAD_LIMIT) {
            cache->precache_active = false;
        }
        return true;
    }

    /* Synchronous fallback path if pthread_create failed */
    uint64_t start_ns = (monotonic_ns != NULL) ? monotonic_ns(clock_context) : 0ull;
    bool did_work = false;
    while (cache->precache_cursor < catalog->album_count) {
        const struct library_album *album = &catalog->albums[cache->precache_cursor];
        cache->precache_cursor++;
        if (album->cover_source_kind == LIBRARY_COVER_NONE) {
            continue;
        }
        did_work = true;
        if (has_rgb565_on_disk(cache, album->album_id)) {
            cache->precache_disk_hits++;
            cache->precache_done++;
            if (cache->precache_ram_preloaded < LIBRARY_COVER_BOOT_RAM_PRELOAD_LIMIT &&
                !has_slot_in_ram(cache, album->album_id) &&
                try_load_rgb565_from_disk(cache, album->album_id) != NULL) {
                cache->precache_ram_preloaded++;
            }
            if (monotonic_ns != NULL && budget_ns > 0ull &&
                (monotonic_ns(clock_context) - start_ns) >= budget_ns) {
                break;
            }
            continue;
        }
        struct library_cover_request request;
        memset(&request, 0, sizeof(request));
        request.album_id = album->album_id;
        request.cover_source_kind = album->cover_source_kind;
        copy_path_bounded(request.source_path, sizeof(request.source_path),
                          library_catalog_get_string(catalog, album->cover_path_offset));
        request.data_offset = album->cover_data_offset;
        request.data_length = album->cover_data_length;
        if (decode_request_to_cache(cache, &request)) {
            cache->precache_decoded++;
        } else {
            cache->precache_failed++;
            mark_album_failed(cache, album->album_id);
        }
        cache->precache_done++;
        break;
    }
    if (cache->precache_cursor >= catalog->album_count) {
        cache->precache_active = false;
    }
    return did_work;
}

void library_cover_cache_take_window_metrics(struct library_cover_cache *cache,
                                           uint32_t *ram_hits_out, uint32_t *disk_hits_out,
                                           uint32_t *decodes_out, uint32_t *max_op_ms_out) {
    if (cache == NULL) {
        return;
    }
    if (ram_hits_out != NULL) {
        *ram_hits_out = cache->window_ram_hits;
    }
    if (disk_hits_out != NULL) {
        *disk_hits_out = cache->window_disk_hits;
    }
    if (decodes_out != NULL) {
        *decodes_out = cache->window_decodes;
    }
    if (max_op_ms_out != NULL) {
        *max_op_ms_out = cache->window_max_op_ms;
    }
    cache->window_ram_hits = 0u;
    cache->window_disk_hits = 0u;
    cache->window_decodes = 0u;
    cache->window_max_op_ms = 0u;
}
