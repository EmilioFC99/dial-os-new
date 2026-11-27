#ifndef DIALOS_LIBRARY_COVER_CACHE_H
#define DIALOS_LIBRARY_COVER_CACHE_H

#include "library_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_COVER_SIZE_PX 120u
#define LIBRARY_COVER_PIXEL_COUNT (LIBRARY_COVER_SIZE_PX * LIBRARY_COVER_SIZE_PX)
#define LIBRARY_COVER_BYTE_SIZE (LIBRARY_COVER_PIXEL_COUNT * sizeof(uint16_t))
#define LIBRARY_COVER_LRU_SLOTS 24u
#define LIBRARY_COVER_PENDING_QUEUE_CAPACITY 12u
#define LIBRARY_COVER_FAILED_SET_CAPACITY 64u
#define LIBRARY_COVER_MAX_INPUT_BYTES (8u * 1024u * 1024u)
#define LIBRARY_COVER_MAX_DIMENSION_PX 4096u

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
};

void library_cover_cache_initialize(struct library_cover_cache *cache, const char *covers_directory);
void library_cover_cache_clear(struct library_cover_cache *cache);
void library_cover_cache_destroy(struct library_cover_cache *cache);
void library_cover_cache_set_directory(struct library_cover_cache *cache,
                                       const char *covers_directory);

const uint16_t *library_cover_cache_lookup(struct library_cover_cache *cache, uint64_t album_id);
bool library_cover_cache_request(struct library_cover_cache *cache, uint64_t album_id,
                                 uint8_t cover_source_kind, const char *source_path,
                                 uint32_t data_offset, uint32_t data_length);
bool library_cover_cache_step(struct library_cover_cache *cache, bool allow_heavy_decode);

#endif
