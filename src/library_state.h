#ifndef DIALOS_LIBRARY_STATE_H
#define DIALOS_LIBRARY_STATE_H

#include "library_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_STATE_MAX_FAVORITES 4096u
#define LIBRARY_STATE_MAX_PLAYLISTS 256u
#define LIBRARY_STATE_MAX_DISMISSED_M3U 128u

struct library_saved_playlist {
    uint64_t playlist_id;
    char name[128];
    char source_m3u[256];
    char **track_paths;
    uint32_t track_count;
};

struct library_state_store {
    uint64_t *favorite_track_ids;
    uint32_t favorite_track_count;
    uint32_t favorite_track_capacity;

    uint64_t *favorite_album_ids;
    uint32_t favorite_album_count;
    uint32_t favorite_album_capacity;

    uint64_t *favorite_artist_ids;
    uint32_t favorite_artist_count;
    uint32_t favorite_artist_capacity;

    struct library_saved_playlist *playlists;
    uint32_t playlist_count;
    uint32_t playlist_capacity;

    char **dismissed_m3u_paths;
    uint32_t dismissed_m3u_count;
    uint32_t dismissed_m3u_capacity;

    enum library_sort_mode album_sort_mode;
    enum library_sort_mode artist_sort_mode;
};

void library_state_initialize(struct library_state_store *store);
void library_state_clear(struct library_state_store *store);
void library_state_destroy(struct library_state_store *store);

bool library_state_load_json(struct library_state_store *store, const char *state_path);
bool library_state_save_json(const struct library_state_store *store, const char *directory_path,
                             const char *temp_path, const char *final_path);

void library_state_apply_to_catalog(const struct library_state_store *store,
                                    struct library_catalog *catalog);
bool library_state_is_m3u_dismissed(const struct library_state_store *store, const char *m3u_path);

bool library_state_toggle_track_favorite(struct library_state_store *store,
                                         struct library_catalog *catalog, uint64_t track_id);
bool library_state_toggle_album_favorite(struct library_state_store *store,
                                         struct library_catalog *catalog, uint64_t album_id);
bool library_state_toggle_artist_favorite(struct library_state_store *store,
                                          struct library_catalog *catalog, uint64_t artist_id);
bool library_state_delete_playlist(struct library_state_store *store,
                                   struct library_catalog *catalog, uint32_t catalog_playlist_index);

#endif
