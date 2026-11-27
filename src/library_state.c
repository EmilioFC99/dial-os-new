#include "library_state.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void library_state_initialize(struct library_state_store *store) {
    if (store == NULL) {
        return;
    }
    memset(store, 0, sizeof(*store));
    store->album_sort_mode = LIBRARY_SORT_ALPHABETICAL;
    store->artist_sort_mode = LIBRARY_SORT_ALPHABETICAL;
}

void library_state_clear(struct library_state_store *store) {
    if (store == NULL) {
        return;
    }
    store->favorite_track_count = 0u;
    store->favorite_album_count = 0u;
    store->favorite_artist_count = 0u;
    for (uint32_t idx = 0u; idx < store->playlist_count; idx++) {
        for (uint32_t tr_idx = 0u; tr_idx < store->playlists[idx].track_count; tr_idx++) {
            free(store->playlists[idx].track_paths[tr_idx]);
        }
        free(store->playlists[idx].track_paths);
    }
    store->playlist_count = 0u;
    for (uint32_t idx = 0u; idx < store->dismissed_m3u_count; idx++) {
        free(store->dismissed_m3u_paths[idx]);
    }
    store->dismissed_m3u_count = 0u;
    store->album_sort_mode = LIBRARY_SORT_ALPHABETICAL;
    store->artist_sort_mode = LIBRARY_SORT_ALPHABETICAL;
}

void library_state_destroy(struct library_state_store *store) {
    if (store == NULL) {
        return;
    }
    library_state_clear(store);
    free(store->favorite_track_ids);
    free(store->favorite_album_ids);
    free(store->favorite_artist_ids);
    free(store->playlists);
    free(store->dismissed_m3u_paths);
    memset(store, 0, sizeof(*store));
}

static bool contains_u64(const uint64_t *items, uint32_t count, uint64_t target) {
    for (uint32_t idx = 0u; idx < count; idx++) {
        if (items[idx] == target) {
            return true;
        }
    }
    return false;
}

static bool toggle_u64_set(uint64_t **items_ptr, uint32_t *count_ptr, uint32_t *capacity_ptr,
                           uint64_t target) {
    uint64_t *items = *items_ptr;
    uint32_t count = *count_ptr;
    for (uint32_t idx = 0u; idx < count; idx++) {
        if (items[idx] == target) {
            for (uint32_t shift = idx; shift + 1u < count; shift++) {
                items[shift] = items[shift + 1u];
            }
            *count_ptr = count - 1u;
            return false; /* removed */
        }
    }
    if (count >= LIBRARY_STATE_MAX_FAVORITES) {
        return false;
    }
    if (count >= *capacity_ptr) {
        uint32_t new_cap = *capacity_ptr == 0u ? 32u : (*capacity_ptr) * 2u;
        uint64_t *grown = realloc(items, (size_t)new_cap * sizeof(uint64_t));
        if (grown == NULL) {
            return false;
        }
        *items_ptr = grown;
        *capacity_ptr = new_cap;
        items = grown;
    }
    items[count] = target;
    *count_ptr = count + 1u;
    return true; /* added */
}

static void add_dismissed_m3u(struct library_state_store *store, const char *m3u_path) {
    if (m3u_path == NULL || m3u_path[0] == '\0' ||
        library_state_is_m3u_dismissed(store, m3u_path) ||
        store->dismissed_m3u_count >= LIBRARY_STATE_MAX_DISMISSED_M3U) {
        return;
    }
    if (store->dismissed_m3u_count >= store->dismissed_m3u_capacity) {
        uint32_t new_cap =
            store->dismissed_m3u_capacity == 0u ? 16u : store->dismissed_m3u_capacity * 2u;
        char **grown = realloc(store->dismissed_m3u_paths, (size_t)new_cap * sizeof(char *));
        if (grown == NULL) {
            return;
        }
        store->dismissed_m3u_paths = grown;
        store->dismissed_m3u_capacity = new_cap;
    }
    size_t len = strlen(m3u_path);
    char *copy = malloc(len + 1u);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, m3u_path, len + 1u);
    store->dismissed_m3u_paths[store->dismissed_m3u_count++] = copy;
}

bool library_state_is_m3u_dismissed(const struct library_state_store *store, const char *m3u_path) {
    if (store == NULL || m3u_path == NULL || m3u_path[0] == '\0') {
        return false;
    }
    for (uint32_t idx = 0u; idx < store->dismissed_m3u_count; idx++) {
        if (strcmp(store->dismissed_m3u_paths[idx], m3u_path) == 0) {
            return true;
        }
    }
    return false;
}

void library_state_apply_to_catalog(const struct library_state_store *store,
                                    struct library_catalog *catalog) {
    if (store == NULL || catalog == NULL) {
        return;
    }
    for (uint32_t idx = 0u; idx < catalog->track_count; idx++) {
        catalog->tracks[idx].is_favorite = contains_u64(
            store->favorite_track_ids, store->favorite_track_count, catalog->tracks[idx].track_id);
    }
    for (uint32_t idx = 0u; idx < catalog->album_count; idx++) {
        catalog->albums[idx].is_favorite = contains_u64(
            store->favorite_album_ids, store->favorite_album_count, catalog->albums[idx].album_id);
    }
    for (uint32_t idx = 0u; idx < catalog->artist_count; idx++) {
        catalog->artists[idx].is_favorite =
            contains_u64(store->favorite_artist_ids, store->favorite_artist_count,
                         catalog->artists[idx].artist_id);
    }

    /* Drop any imported .m3u playlist that the user previously dismissed */
    for (uint32_t idx = catalog->playlist_count; idx > 0u; idx--) {
        uint32_t pl_idx = idx - 1u;
        const char *m3u_path =
            library_catalog_get_string(catalog, catalog->playlists[pl_idx].source_m3u_offset);
        if (m3u_path[0] != '\0' && library_state_is_m3u_dismissed(store, m3u_path)) {
            library_catalog_remove_playlist(catalog, pl_idx);
        }
    }

    library_catalog_recompute_favorite_counts(catalog);
}

bool library_state_toggle_track_favorite(struct library_state_store *store,
                                         struct library_catalog *catalog, uint64_t track_id) {
    if (store == NULL || catalog == NULL) {
        return false;
    }
    int32_t idx = library_catalog_find_track_by_id(catalog, track_id);
    if (idx < 0) {
        return false;
    }
    bool added = toggle_u64_set(&store->favorite_track_ids, &store->favorite_track_count,
                                &store->favorite_track_capacity, track_id);
    catalog->tracks[idx].is_favorite = added;
    library_catalog_recompute_favorite_counts(catalog);
    return true;
}

bool library_state_toggle_album_favorite(struct library_state_store *store,
                                         struct library_catalog *catalog, uint64_t album_id) {
    if (store == NULL || catalog == NULL) {
        return false;
    }
    int32_t idx = library_catalog_find_album_by_id(catalog, album_id);
    if (idx < 0) {
        return false;
    }
    bool added = toggle_u64_set(&store->favorite_album_ids, &store->favorite_album_count,
                                &store->favorite_album_capacity, album_id);
    catalog->albums[idx].is_favorite = added;
    library_catalog_recompute_favorite_counts(catalog);
    return true;
}

bool library_state_toggle_artist_favorite(struct library_state_store *store,
                                          struct library_catalog *catalog, uint64_t artist_id) {
    if (store == NULL || catalog == NULL) {
        return false;
    }
    int32_t idx = library_catalog_find_artist_by_id(catalog, artist_id);
    if (idx < 0) {
        return false;
    }
    bool added = toggle_u64_set(&store->favorite_artist_ids, &store->favorite_artist_count,
                                &store->favorite_artist_capacity, artist_id);
    catalog->artists[idx].is_favorite = added;
    library_catalog_recompute_favorite_counts(catalog);
    return true;
}

bool library_state_delete_playlist(struct library_state_store *store,
                                   struct library_catalog *catalog,
                                   uint32_t catalog_playlist_index) {
    if (store == NULL || catalog == NULL || catalog_playlist_index >= catalog->playlist_count) {
        return false;
    }
    const char *m3u_path = library_catalog_get_string(
        catalog, catalog->playlists[catalog_playlist_index].source_m3u_offset);
    if (m3u_path[0] != '\0') {
        add_dismissed_m3u(store, m3u_path);
    }
    return library_catalog_remove_playlist(catalog, catalog_playlist_index);
}

static bool write_json_escaped(FILE *file, const char *text) {
    fputc('"', file);
    for (size_t idx = 0u; text[idx] != '\0'; idx++) {
        char ch = text[idx];
        if (ch == '"' || ch == '\\') {
            fputc('\\', file);
        }
        fputc(ch, file);
    }
    fputc('"', file);
    return true;
}

static bool write_u64_array(FILE *file, const char *key, const uint64_t *items, uint32_t count,
                            bool trailing_comma) {
    fprintf(file, "  \"%s\": [", key);
    for (uint32_t idx = 0u; idx < count; idx++) {
        fprintf(file, "%s\"%016" PRIx64 "\"", idx > 0u ? "," : "", items[idx]);
    }
    fprintf(file, "]%s\n", trailing_comma ? "," : "");
    return true;
}

bool library_state_save_json(const struct library_state_store *store, const char *directory_path,
                             const char *temp_path, const char *final_path) {
    if (store == NULL || directory_path == NULL || temp_path == NULL || final_path == NULL) {
        return false;
    }
    if (mkdir(directory_path, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    int fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    FILE *file = fdopen(fd, "w");
    if (file == NULL) {
        close(fd);
        return false;
    }

    fprintf(file, "{\n");
    fprintf(file, "  \"albumSortMode\": %u,\n", (unsigned)store->album_sort_mode);
    fprintf(file, "  \"artistSortMode\": %u,\n", (unsigned)store->artist_sort_mode);
    write_u64_array(file, "favoriteTrackIds", store->favorite_track_ids,
                    store->favorite_track_count, true);
    write_u64_array(file, "favoriteAlbumIds", store->favorite_album_ids,
                    store->favorite_album_count, true);
    write_u64_array(file, "favoriteArtistIds", store->favorite_artist_ids,
                    store->favorite_artist_count, true);
    fprintf(file, "  \"dismissedM3uPaths\": [");
    for (uint32_t idx = 0u; idx < store->dismissed_m3u_count; idx++) {
        if (idx > 0u) {
            fputc(',', file);
        }
        write_json_escaped(file, store->dismissed_m3u_paths[idx]);
    }
    fprintf(file, "]\n}\n");

    fflush(file);
    bool ok = (fsync(fileno(file)) == 0);
    fclose(file);
    if (!ok || rename(temp_path, final_path) != 0) {
        (void)unlink(temp_path);
        return false;
    }
    int dir_fd = open(directory_path, O_RDONLY | O_CLOEXEC);
    if (dir_fd >= 0) {
        (void)fsync(dir_fd);
        close(dir_fd);
    }
    return true;
}

static void parse_hex_u64_array(const char *json, const char *key, uint64_t **items_ptr,
                                uint32_t *count_ptr, uint32_t *cap_ptr) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *found = strstr(json, pattern);
    if (found == NULL) {
        return;
    }
    const char *bracket = strchr(found, '[');
    if (bracket == NULL) {
        return;
    }
    const char *cursor = bracket + 1;
    while (*cursor != '\0' && *cursor != ']') {
        if (*cursor == '"') {
            cursor++;
            uint64_t val = 0ull;
            while (isxdigit((unsigned char)*cursor)) {
                char ch = *cursor;
                uint64_t digit = (ch >= '0' && ch <= '9')
                                     ? (uint64_t)(ch - '0')
                                     : (uint64_t)(10 + tolower((unsigned char)ch) - 'a');
                val = (val << 4u) | digit;
                cursor++;
            }
            if (*cursor == '"') {
                (void)toggle_u64_set(items_ptr, count_ptr, cap_ptr, val);
                cursor++;
            }
        } else {
            cursor++;
        }
    }
}

bool library_state_load_json(struct library_state_store *store, const char *state_path) {
    if (store == NULL || state_path == NULL) {
        return false;
    }
    FILE *file = fopen(state_path, "rb");
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size <= 0 || size > 512L * 1024L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    char *json = malloc((size_t)size + 1u);
    if (json == NULL) {
        fclose(file);
        return false;
    }
    size_t read_bytes = fread(json, 1u, (size_t)size, file);
    fclose(file);
    json[read_bytes] = '\0';

    library_state_clear(store);

    const char *album_sort = strstr(json, "\"albumSortMode\"");
    if (album_sort != NULL) {
        const char *colon = strchr(album_sort, ':');
        if (colon != NULL) {
            unsigned val = (unsigned)strtoul(colon + 1, NULL, 10);
            if (val < LIBRARY_SORT_MODE_COUNT) {
                store->album_sort_mode = (enum library_sort_mode)val;
            }
        }
    }
    const char *artist_sort = strstr(json, "\"artistSortMode\"");
    if (artist_sort != NULL) {
        const char *colon = strchr(artist_sort, ':');
        if (colon != NULL) {
            unsigned val = (unsigned)strtoul(colon + 1, NULL, 10);
            if (val < LIBRARY_SORT_MODE_COUNT) {
                store->artist_sort_mode = (enum library_sort_mode)val;
            }
        }
    }

    parse_hex_u64_array(json, "favoriteTrackIds", &store->favorite_track_ids,
                        &store->favorite_track_count, &store->favorite_track_capacity);
    parse_hex_u64_array(json, "favoriteAlbumIds", &store->favorite_album_ids,
                        &store->favorite_album_count, &store->favorite_album_capacity);
    parse_hex_u64_array(json, "favoriteArtistIds", &store->favorite_artist_ids,
                        &store->favorite_artist_count, &store->favorite_artist_capacity);

    const char *dismissed = strstr(json, "\"dismissedM3uPaths\"");
    if (dismissed != NULL) {
        const char *cursor = strchr(dismissed, '[');
        while (cursor != NULL && *cursor != '\0' && *cursor != ']') {
            if (*cursor == '"') {
                cursor++;
                char path_buf[256];
                size_t out_idx = 0u;
                while (*cursor != '\0' && *cursor != '"') {
                    if (*cursor == '\\' && cursor[1] != '\0') {
                        cursor++;
                    }
                    if (out_idx + 1u < sizeof(path_buf)) {
                        path_buf[out_idx++] = *cursor;
                    }
                    cursor++;
                }
                path_buf[out_idx] = '\0';
                if (*cursor == '"') {
                    cursor++;
                }
                add_dismissed_m3u(store, path_buf);
            } else {
                cursor++;
            }
        }
    }

    free(json);
    return true;
}
