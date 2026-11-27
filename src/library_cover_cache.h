#ifndef DIALOS_LIBRARY_COVER_CACHE_H
#define DIALOS_LIBRARY_COVER_CACHE_H

#include "library_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_COVER_SIZE_PX 120u
#define LIBRARY_COVER_PIXEL_COUNT (LIBRARY_COVER_SIZE_PX * LIBRARY_COVER_SIZE_PX)
#define LIBRARY_COVER_BYTE_SIZE (LIBRARY_COVER_PIXEL_COUNT * sizeof(uint16_t))
#define LIBRARY_COVER_LRU_SLOTS 128u
#define LIBRARY_COVER_BOOT_RAM_PRELOAD_LIMIT 32u
#define LIBRARY_COVER_PRECACHE_STAT_BUDGET_NS 4000000ull
#define LIBRARY_COVER_PENDING_QUEUE_CAPACITY 12u
#define LIBRARY_COVER_FAILED_SET_CAPACITY 64u
#define LIBRARY_COVER_MAX_INPUT_BYTES (8u * 1024u * 1024u)
#define LIBRARY_COVER_MAX_DIMENSION_PX 4096u

enum library_cover_fail_reason {
    LIBRARY_COVER_FAIL_NONE = 0,
    LIBRARY_COVER_FAIL_OPEN = 1,
    LIBRARY_COVER_FAIL_SIZE = 2,
    LIBRARY_COVER_FAIL_INFO = 3,
    LIBRARY_COVER_FAIL_DECODE = 4,
    LIBRARY_COVER_FAIL_SLOT = 5
};

struct library_cover_slot {
    bool occupied;
    uint64_t album_id;
    uint64_t last_used_tick;
    uint16_t pixels[LIBRARY_COVER_PIXEL_COUNT];
};

struct library_cover_request {
    uint64_t album_id;
    uint8_t cover_source_kind;
    char source_path[512];
    uint32_t data_offset;
    uint32_t data_length;
};

struct library_cover_cache {
    char covers_directory[320];
    struct library_cover_slot *slots;
    uint64_t access_tick;

    struct library_cover_request pending[LIBRARY_COVER_PENDING_QUEUE_CAPACITY];
    uint32_t pending_count;

    uint64_t failed_album_ids[LIBRARY_COVER_FAILED_SET_CAPACITY];
    uint32_t failed_count;

    bool precache_active;
    uint32_t precache_cursor;
    uint32_t precache_total;
    uint32_t precache_done;
    uint32_t precache_decoded;
    uint32_t precache_disk_hits;
    uint32_t precache_ram_preloaded;
    uint32_t precache_failed;
    uint32_t precache_max_decode_ms;
    enum library_cover_fail_reason last_fail_reason;
    char last_fail_path[256];

    uint32_t window_ram_hits;
    uint32_t window_disk_hits;
    uint32_t window_decodes;
    uint32_t window_max_op_ms;
};

void library_cover_cache_initialize(struct library_cover_cache *cache, const char *covers_directory);
void library_cover_cache_clear(struct library_cover_cache *cache);
void library_cover_cache_destroy(struct library_cover_cache *cache);
void library_cover_cache_set_directory(struct library_cover_cache *cache,
                                       const char *covers_directory);

const uint16_t *library_cover_cache_lookup(struct library_cover_cache *cache, uint64_t album_id);
const uint16_t *library_cover_cache_lookup_or_load_disk(struct library_cover_cache *cache,
                                                        uint64_t album_id);
bool library_cover_cache_request(struct library_cover_cache *cache, uint64_t album_id,
                                 uint8_t cover_source_kind, const char *source_path,
                                 uint32_t data_offset, uint32_t data_length);
bool library_cover_cache_step(struct library_cover_cache *cache, bool allow_heavy_decode);

void library_cover_cache_begin_boot_precache(struct library_cover_cache *cache,
                                             const struct library_catalog *catalog);
void library_cover_cache_cancel_boot_precache(struct library_cover_cache *cache);
bool library_cover_cache_is_boot_precaching(const struct library_cover_cache *cache);
bool library_cover_cache_step_boot_precache(struct library_cover_cache *cache,
                                            const struct library_catalog *catalog,
                                            uint64_t (*monotonic_ns)(void *context),
                                            void *clock_context, uint64_t budget_ns);
void library_cover_cache_take_window_metrics(struct library_cover_cache *cache,
                                           uint32_t *ram_hits_out, uint32_t *disk_hits_out,
                                           uint32_t *decodes_out, uint32_t *max_op_ms_out);

#endif
