#include "library_catalog.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define INDEX_MAGIC 0x58494C44u /* "DLIX" */
#define INDEX_VERSION 1u
#define UNORDERED_TRACK_NUMBER 9999u
#define MONTHS_PER_YEAR 12u

static const struct library_catalog *g_sort_catalog = NULL;
static enum library_sort_mode g_sort_mode = LIBRARY_SORT_ALPHABETICAL;

static char ascii_tolower(char character) {
    if (character >= 'A' && character <= 'Z') {
        return (char)(character + ('a' - 'A'));
    }
    return character;
}

uint64_t library_fnv1a_64(const char *text) {
    uint64_t hash = 14695981039346656037ull;
    if (text == NULL) {
        return hash;
    }
    for (size_t index = 0u; text[index] != '\0'; index++) {
        hash ^= (uint8_t)text[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

int32_t library_compare_ascii_casefold(const char *first, const char *second) {
    const char *left = (first != NULL) ? first : "";
    const char *right = (second != NULL) ? second : "";
    size_t index = 0u;
    while (left[index] != '\0' && right[index] != '\0') {
        char left_char = ascii_tolower(left[index]);
        char right_char = ascii_tolower(right[index]);
        if (left_char != right_char) {
            return (unsigned char)left_char < (unsigned char)right_char ? -1 : 1;
        }
        index++;
    }
    if (left[index] != right[index]) {
        return left[index] == '\0' ? -1 : 1;
    }
    return 0;
}

const char *library_sort_mode_toast_label(enum library_sort_mode mode) {
    switch (mode) {
        case LIBRARY_SORT_ALPHABETICAL:
            return "A\xE2\x80\x93Z";
        case LIBRARY_SORT_REVERSE_ALPHABETICAL:
            return "Z\xE2\x80\x93" "A";
        case LIBRARY_SORT_LIKED_FIRST:
            return "LIKED FIRST";
        case LIBRARY_SORT_RECENT:
            return "MOST RECENT";
        case LIBRARY_SORT_OLDEST:
            return "OLDEST FIRST";
        default:
            return "A\xE2\x80\x93Z";
    }
}

void library_catalog_initialize(struct library_catalog *catalog) {
    if (catalog == NULL) {
        return;
    }
    memset(catalog, 0, sizeof(*catalog));
    catalog->strings_capacity = 4096u;
    catalog->strings = malloc(catalog->strings_capacity);
    if (catalog->strings != NULL) {
        catalog->strings[0] = '\0';
        catalog->strings_used = 1u;
    }
}

void library_catalog_clear(struct library_catalog *catalog) {
    if (catalog == NULL) {
        return;
    }
    catalog->track_count = 0u;
    catalog->album_count = 0u;
    catalog->artist_count = 0u;
    catalog->genre_count = 0u;
    catalog->playlist_count = 0u;
    catalog->album_track_index_count = 0u;
    catalog->artist_album_index_count = 0u;
    catalog->artist_track_index_count = 0u;
    catalog->genre_track_index_count = 0u;
    catalog->playlist_track_index_count = 0u;
    catalog->fingerprint = 0ull;
    catalog->favorite_track_count = 0u;
    catalog->favorite_album_count = 0u;
    catalog->favorite_artist_count = 0u;
    if (catalog->strings != NULL && catalog->strings_capacity > 0u) {
        catalog->strings[0] = '\0';
        catalog->strings_used = 1u;
    }
}

void library_catalog_destroy(struct library_catalog *catalog) {
    if (catalog == NULL) {
        return;
    }
    free(catalog->strings);
    free(catalog->tracks);
    free(catalog->albums);
    free(catalog->artists);
    free(catalog->genres);
    free(catalog->playlists);
    free(catalog->all_track_indices);
    free(catalog->album_track_indices);
    free(catalog->artist_album_indices);
    free(catalog->artist_track_indices);
    free(catalog->genre_track_indices);
    free(catalog->playlist_track_indices);
    memset(catalog, 0, sizeof(*catalog));
}

const char *library_catalog_get_string(const struct library_catalog *catalog, uint32_t offset) {
    if (catalog == NULL || catalog->strings == NULL || offset >= catalog->strings_used) {
        return "";
    }
    return catalog->strings + offset;
}

uint32_t library_catalog_intern_string(struct library_catalog *catalog, const char *text) {
    if (catalog == NULL || text == NULL || text[0] == '\0') {
        return 0u;
    }
    size_t length = strlen(text);
    if (catalog->strings == NULL) {
        catalog->strings_capacity = 4096u;
        catalog->strings = malloc(catalog->strings_capacity);
        if (catalog->strings == NULL) {
            return 0u;
        }
        catalog->strings[0] = '\0';
        catalog->strings_used = 1u;
    }
    if (catalog->strings_used + length + 1u > catalog->strings_capacity) {
        size_t new_capacity = catalog->strings_capacity * 2u;
        while (new_capacity < catalog->strings_used + length + 1u) {
            new_capacity *= 2u;
        }
        if (new_capacity > LIBRARY_MAX_STRING_ARENA_BYTES) {
            new_capacity = LIBRARY_MAX_STRING_ARENA_BYTES;
        }
        if (catalog->strings_used + length + 1u > new_capacity) {
            return 0u;
        }
        char *grown = realloc(catalog->strings, new_capacity);
        if (grown == NULL) {
            return 0u;
        }
        catalog->strings = grown;
        catalog->strings_capacity = new_capacity;
    }
    uint32_t offset = (uint32_t)catalog->strings_used;
    memcpy(catalog->strings + offset, text, length + 1u);
    catalog->strings_used += length + 1u;
    return offset;
}

static void build_lower_key(const char *text, char *output, size_t capacity) {
    if (capacity == 0u) {
        return;
    }
    size_t out_index = 0u;
    if (text != NULL) {
        for (size_t index = 0u; text[index] != '\0' && out_index + 1u < capacity; index++) {
            output[out_index++] = ascii_tolower(text[index]);
        }
    }
    output[out_index] = '\0';
}

static uint32_t find_or_create_artist(struct library_catalog *catalog, const char *artist_name) {
    char key_buffer[256];
    snprintf(key_buffer, sizeof(key_buffer), "artist:");
    build_lower_key(artist_name, key_buffer + 7u, sizeof(key_buffer) - 7u);
    uint64_t artist_id = library_fnv1a_64(key_buffer);

    for (uint32_t index = 0u; index < catalog->artist_count; index++) {
        if (catalog->artists[index].artist_id == artist_id) {
            return index;
        }
    }
    if (catalog->artist_count >= LIBRARY_MAX_ARTISTS) {
        return UINT32_MAX;
    }
    if (catalog->artist_count >= catalog->artist_capacity) {
        uint32_t new_cap = catalog->artist_capacity == 0u ? 64u : catalog->artist_capacity * 2u;
        if (new_cap > LIBRARY_MAX_ARTISTS) {
            new_cap = LIBRARY_MAX_ARTISTS;
        }
        struct library_artist *grown =
            realloc(catalog->artists, (size_t)new_cap * sizeof(struct library_artist));
        if (grown == NULL) {
            return UINT32_MAX;
        }
        catalog->artists = grown;
        catalog->artist_capacity = new_cap;
    }
    uint32_t new_index = catalog->artist_count++;
    memset(&catalog->artists[new_index], 0, sizeof(struct library_artist));
    catalog->artists[new_index].artist_id = artist_id;
    catalog->artists[new_index].name_offset = library_catalog_intern_string(catalog, artist_name);
    return new_index;
}

static uint32_t find_or_create_album(struct library_catalog *catalog, const char *album_title,
                                     const char *album_artist, uint32_t artist_index,
                                     const char *genre, uint16_t year, uint8_t cover_kind,
                                     const char *cover_path, uint32_t cover_offset,
                                     uint32_t cover_length) {
    char artist_lower[160];
    char album_lower[160];
    char key_buffer[384];
    build_lower_key(album_artist, artist_lower, sizeof(artist_lower));
    build_lower_key(album_title, album_lower, sizeof(album_lower));
    snprintf(key_buffer, sizeof(key_buffer), "album:%s\x1F%s", artist_lower, album_lower);
    uint64_t album_id = library_fnv1a_64(key_buffer);

    for (uint32_t index = 0u; index < catalog->album_count; index++) {
        if (catalog->albums[index].album_id == album_id) {
            struct library_album *existing = &catalog->albums[index];
            if (existing->cover_source_kind == LIBRARY_COVER_NONE &&
                cover_kind != LIBRARY_COVER_NONE) {
                existing->cover_source_kind = cover_kind;
                existing->cover_path_offset = library_catalog_intern_string(catalog, cover_path);
                existing->cover_data_offset = cover_offset;
                existing->cover_data_length = cover_length;
            }
            if (existing->year == 0u && year != 0u) {
                existing->year = year;
            }
            return index;
        }
    }
    if (catalog->album_count >= LIBRARY_MAX_ALBUMS) {
        return UINT32_MAX;
    }
    if (catalog->album_count >= catalog->album_capacity) {
        uint32_t new_cap = catalog->album_capacity == 0u ? 64u : catalog->album_capacity * 2u;
        if (new_cap > LIBRARY_MAX_ALBUMS) {
            new_cap = LIBRARY_MAX_ALBUMS;
        }
        struct library_album *grown =
            realloc(catalog->albums, (size_t)new_cap * sizeof(struct library_album));
        if (grown == NULL) {
            return UINT32_MAX;
        }
        catalog->albums = grown;
        catalog->album_capacity = new_cap;
    }
    uint32_t new_index = catalog->album_count++;
    memset(&catalog->albums[new_index], 0, sizeof(struct library_album));
    catalog->albums[new_index].album_id = album_id;
    catalog->albums[new_index].title_offset = library_catalog_intern_string(catalog, album_title);
    catalog->albums[new_index].artist_offset = library_catalog_intern_string(catalog, album_artist);
    catalog->albums[new_index].artist_index = artist_index;
    catalog->albums[new_index].genre_offset = library_catalog_intern_string(catalog, genre);
    catalog->albums[new_index].year = year;
    catalog->albums[new_index].cover_source_kind = cover_kind;
    if (cover_kind != LIBRARY_COVER_NONE && cover_path != NULL) {
        catalog->albums[new_index].cover_path_offset =
            library_catalog_intern_string(catalog, cover_path);
        catalog->albums[new_index].cover_data_offset = cover_offset;
        catalog->albums[new_index].cover_data_length = cover_length;
    }
    return new_index;
}

bool library_catalog_add_track(struct library_catalog *catalog,
                               const struct library_track_input *input) {
    if (catalog == NULL || input == NULL || input->file_path == NULL ||
        catalog->track_count >= LIBRARY_MAX_TRACKS) {
        return false;
    }

    const char *title = (input->title != NULL && input->title[0] != '\0') ? input->title : "Untitled";
    const char *artist =
        (input->artist != NULL && input->artist[0] != '\0') ? input->artist : "Unknown Artist";
    const char *album_artist =
        (input->album_artist != NULL && input->album_artist[0] != '\0') ? input->album_artist
                                                                        : artist;
    const char *album =
        (input->album != NULL && input->album[0] != '\0') ? input->album : "Unknown Album";
    const char *genre =
        (input->genre != NULL && input->genre[0] != '\0') ? input->genre : "Unknown Genre";

    uint32_t artist_index = find_or_create_artist(catalog, album_artist);
    if (artist_index == UINT32_MAX) {
        return false;
    }
    uint32_t album_index = find_or_create_album(
        catalog, album, album_artist, artist_index, genre, input->year, input->cover_source_kind,
        input->cover_path, input->cover_data_offset, input->cover_data_length);
    if (album_index == UINT32_MAX) {
        return false;
    }

    if (catalog->track_count >= catalog->track_capacity) {
        uint32_t new_cap = catalog->track_capacity == 0u ? 128u : catalog->track_capacity * 2u;
        if (new_cap > LIBRARY_MAX_TRACKS) {
            new_cap = LIBRARY_MAX_TRACKS;
        }
        struct library_track *grown =
            realloc(catalog->tracks, (size_t)new_cap * sizeof(struct library_track));
        if (grown == NULL) {
            return false;
        }
        catalog->tracks = grown;
        catalog->track_capacity = new_cap;
    }

    const char *id_seed = (input->relative_path != NULL && input->relative_path[0] != '\0')
                              ? input->relative_path
                              : input->file_path;
    char id_key[600];
    snprintf(id_key, sizeof(id_key), "track:%s", id_seed);

    uint32_t track_index = catalog->track_count++;
    struct library_track *track = &catalog->tracks[track_index];
    memset(track, 0, sizeof(*track));
    track->track_id = library_fnv1a_64(id_key);
    track->path_offset = library_catalog_intern_string(catalog, input->file_path);
    track->title_offset = library_catalog_intern_string(catalog, title);
    track->artist_offset = library_catalog_intern_string(catalog, artist);
    track->album_offset = library_catalog_intern_string(catalog, album);
    track->genre_raw_offset = library_catalog_intern_string(catalog, genre);
    track->album_index = album_index;
    track->artist_index = artist_index;
    track->disc_number = input->disc_number > 0u ? input->disc_number : 1u;
    track->track_number = input->track_number;
    track->duration_seconds = input->duration_seconds;
    track->added_at_seconds = input->added_at_seconds;
    track->file_size_bytes = input->file_size_bytes;
    track->format_kind = input->format_kind;
    return true;
}

static int compare_track_title(const void *first_ptr, const void *second_ptr) {
    uint32_t first_idx = *(const uint32_t *)first_ptr;
    uint32_t second_idx = *(const uint32_t *)second_ptr;
    const char *first_title =
        library_catalog_get_string(g_sort_catalog, g_sort_catalog->tracks[first_idx].title_offset);
    const char *second_title =
        library_catalog_get_string(g_sort_catalog, g_sort_catalog->tracks[second_idx].title_offset);
    int32_t cmp = library_compare_ascii_casefold(first_title, second_title);
    if (cmp != 0) {
        return cmp;
    }
    return first_idx < second_idx ? -1 : (first_idx > second_idx ? 1 : 0);
}

static int compare_album_track_order(const void *first_ptr, const void *second_ptr) {
    uint32_t first_idx = *(const uint32_t *)first_ptr;
    uint32_t second_idx = *(const uint32_t *)second_ptr;
    const struct library_track *left = &g_sort_catalog->tracks[first_idx];
    const struct library_track *right = &g_sort_catalog->tracks[second_idx];
    if (left->disc_number != right->disc_number) {
        return left->disc_number < right->disc_number ? -1 : 1;
    }
    uint16_t left_num = left->track_number > 0u ? left->track_number : UNORDERED_TRACK_NUMBER;
    uint16_t right_num = right->track_number > 0u ? right->track_number : UNORDERED_TRACK_NUMBER;
    if (left_num != right_num) {
        return left_num < right_num ? -1 : 1;
    }
    return library_compare_ascii_casefold(
        library_catalog_get_string(g_sort_catalog, left->path_offset),
        library_catalog_get_string(g_sort_catalog, right->path_offset));
}

static int compare_album_title(const void *first_ptr, const void *second_ptr) {
    uint32_t first_idx = *(const uint32_t *)first_ptr;
    uint32_t second_idx = *(const uint32_t *)second_ptr;
    return library_compare_ascii_casefold(
        library_catalog_get_string(g_sort_catalog, g_sort_catalog->albums[first_idx].title_offset),
        library_catalog_get_string(g_sort_catalog, g_sort_catalog->albums[second_idx].title_offset));
}

static uint32_t find_or_create_genre(struct library_catalog *catalog, const char *genre_name) {
    char key_buffer[160];
    snprintf(key_buffer, sizeof(key_buffer), "genre:");
    build_lower_key(genre_name, key_buffer + 6u, sizeof(key_buffer) - 6u);
    uint64_t genre_id = library_fnv1a_64(key_buffer);

    for (uint32_t index = 0u; index < catalog->genre_count; index++) {
        if (catalog->genres[index].genre_id == genre_id) {
            return index;
        }
    }
    if (catalog->genre_count >= LIBRARY_MAX_GENRES) {
        return UINT32_MAX;
    }
    if (catalog->genre_count >= catalog->genre_capacity) {
        uint32_t new_cap = catalog->genre_capacity == 0u ? 32u : catalog->genre_capacity * 2u;
        if (new_cap > LIBRARY_MAX_GENRES) {
            new_cap = LIBRARY_MAX_GENRES;
        }
        struct library_genre *grown =
            realloc(catalog->genres, (size_t)new_cap * sizeof(struct library_genre));
        if (grown == NULL) {
            return UINT32_MAX;
        }
        catalog->genres = grown;
        catalog->genre_capacity = new_cap;
    }
    uint32_t new_index = catalog->genre_count++;
    memset(&catalog->genres[new_index], 0, sizeof(struct library_genre));
    catalog->genres[new_index].genre_id = genre_id;
    catalog->genres[new_index].name_offset = library_catalog_intern_string(catalog, genre_name);
    return new_index;
}

static void trim_whitespace_inplace(char *text) {
    size_t length = strlen(text);
    while (length > 0u && isspace((unsigned char)text[length - 1u])) {
        text[--length] = '\0';
    }
    size_t start = 0u;
    while (text[start] != '\0' && isspace((unsigned char)text[start])) {
        start++;
    }
    if (start > 0u) {
        memmove(text, text + start, length - start + 1u);
    }
}

bool library_catalog_finalize(struct library_catalog *catalog) {
    if (catalog == NULL) {
        return false;
    }
    free(catalog->all_track_indices);
    free(catalog->album_track_indices);
    free(catalog->artist_album_indices);
    free(catalog->artist_track_indices);
    free(catalog->genre_track_indices);
    catalog->all_track_indices = NULL;
    catalog->album_track_indices = NULL;
    catalog->artist_album_indices = NULL;
    catalog->artist_track_indices = NULL;
    catalog->genre_track_indices = NULL;
    catalog->album_track_index_count = 0u;
    catalog->artist_album_index_count = 0u;
    catalog->artist_track_index_count = 0u;
    catalog->genre_track_index_count = 0u;
    catalog->genre_count = 0u;

    if (catalog->track_count == 0u) {
        return true;
    }

    /* 1. All tracks A-Z */
    catalog->all_track_indices = malloc((size_t)catalog->track_count * sizeof(uint32_t));
    if (catalog->all_track_indices == NULL) {
        return false;
    }
    for (uint32_t index = 0u; index < catalog->track_count; index++) {
        catalog->all_track_indices[index] = index;
    }
    g_sort_catalog = catalog;
    qsort(catalog->all_track_indices, catalog->track_count, sizeof(uint32_t), compare_track_title);

    /* 2. Album tracks */
    catalog->album_track_indices = malloc((size_t)catalog->track_count * sizeof(uint32_t));
    if (catalog->album_track_indices == NULL) {
        return false;
    }
    for (uint32_t album_idx = 0u; album_idx < catalog->album_count; album_idx++) {
        catalog->albums[album_idx].track_count = 0u;
    }
    for (uint32_t track_idx = 0u; track_idx < catalog->track_count; track_idx++) {
        uint32_t album_idx = catalog->tracks[track_idx].album_index;
        if (album_idx < catalog->album_count) {
            catalog->albums[album_idx].track_count++;
            if (catalog->tracks[track_idx].added_at_seconds >
                catalog->albums[album_idx].added_at_seconds) {
                catalog->albums[album_idx].added_at_seconds =
                    catalog->tracks[track_idx].added_at_seconds;
            }
        }
    }
    uint32_t cursor = 0u;
    for (uint32_t album_idx = 0u; album_idx < catalog->album_count; album_idx++) {
        catalog->albums[album_idx].track_start_offset = cursor;
        cursor += catalog->albums[album_idx].track_count;
        catalog->albums[album_idx].track_count = 0u;
    }
    catalog->album_track_index_count = cursor;
    for (uint32_t track_idx = 0u; track_idx < catalog->track_count; track_idx++) {
        uint32_t album_idx = catalog->tracks[track_idx].album_index;
        if (album_idx < catalog->album_count) {
            uint32_t slot = catalog->albums[album_idx].track_start_offset +
                            catalog->albums[album_idx].track_count++;
            catalog->album_track_indices[slot] = track_idx;
        }
    }
    for (uint32_t album_idx = 0u; album_idx < catalog->album_count; album_idx++) {
        if (catalog->albums[album_idx].track_count > 1u) {
            qsort(catalog->album_track_indices + catalog->albums[album_idx].track_start_offset,
                  catalog->albums[album_idx].track_count, sizeof(uint32_t),
                  compare_album_track_order);
        }
    }

    /* Assign deterministic motifs in alphabetical album order */
    if (catalog->album_count > 0u) {
        uint32_t *album_order = malloc((size_t)catalog->album_count * sizeof(uint32_t));
        if (album_order != NULL) {
            for (uint32_t album_idx = 0u; album_idx < catalog->album_count; album_idx++) {
                album_order[album_idx] = album_idx;
            }
            qsort(album_order, catalog->album_count, sizeof(uint32_t), compare_album_title);
            for (uint32_t pos = 0u; pos < catalog->album_count; pos++) {
                catalog->albums[album_order[pos]].motif_id =
                    (uint8_t)(pos % LIBRARY_COVER_MOTIF_COUNT);
            }
            free(album_order);
        }
    }

    /* 3. Artist albums & artist tracks */
    if (catalog->album_count > 0u) {
        catalog->artist_album_indices = malloc((size_t)catalog->album_count * sizeof(uint32_t));
        if (catalog->artist_album_indices == NULL) {
            return false;
        }
    }
    catalog->artist_track_indices = malloc((size_t)catalog->track_count * sizeof(uint32_t));
    if (catalog->artist_track_indices == NULL) {
        return false;
    }
    for (uint32_t artist_idx = 0u; artist_idx < catalog->artist_count; artist_idx++) {
        catalog->artists[artist_idx].album_count = 0u;
        catalog->artists[artist_idx].track_count = 0u;
    }
    for (uint32_t album_idx = 0u; album_idx < catalog->album_count; album_idx++) {
        uint32_t artist_idx = catalog->albums[album_idx].artist_index;
        if (artist_idx < catalog->artist_count) {
            catalog->artists[artist_idx].album_count++;
            if (catalog->albums[album_idx].added_at_seconds >
                catalog->artists[artist_idx].added_at_seconds) {
                catalog->artists[artist_idx].added_at_seconds =
                    catalog->albums[album_idx].added_at_seconds;
            }
        }
    }
    for (uint32_t track_idx = 0u; track_idx < catalog->track_count; track_idx++) {
        uint32_t artist_idx = catalog->tracks[track_idx].artist_index;
        if (artist_idx < catalog->artist_count) {
            catalog->artists[artist_idx].track_count++;
        }
    }
    uint32_t album_cursor = 0u;
    uint32_t track_cursor = 0u;
    for (uint32_t artist_idx = 0u; artist_idx < catalog->artist_count; artist_idx++) {
        catalog->artists[artist_idx].album_start_offset = album_cursor;
        album_cursor += catalog->artists[artist_idx].album_count;
        catalog->artists[artist_idx].album_count = 0u;

        catalog->artists[artist_idx].track_start_offset = track_cursor;
        track_cursor += catalog->artists[artist_idx].track_count;
        catalog->artists[artist_idx].track_count = 0u;
    }
    catalog->artist_album_index_count = album_cursor;
    catalog->artist_track_index_count = track_cursor;

    for (uint32_t album_idx = 0u; album_idx < catalog->album_count; album_idx++) {
        uint32_t artist_idx = catalog->albums[album_idx].artist_index;
        if (artist_idx < catalog->artist_count) {
            uint32_t slot = catalog->artists[artist_idx].album_start_offset +
                            catalog->artists[artist_idx].album_count++;
            catalog->artist_album_indices[slot] = album_idx;
        }
    }
    for (uint32_t track_idx = 0u; track_idx < catalog->track_count; track_idx++) {
        uint32_t artist_idx = catalog->tracks[track_idx].artist_index;
        if (artist_idx < catalog->artist_count) {
            uint32_t slot = catalog->artists[artist_idx].track_start_offset +
                            catalog->artists[artist_idx].track_count++;
            catalog->artist_track_indices[slot] = track_idx;
        }
    }
    for (uint32_t artist_idx = 0u; artist_idx < catalog->artist_count; artist_idx++) {
        if (catalog->artists[artist_idx].album_count > 1u) {
            qsort(catalog->artist_album_indices + catalog->artists[artist_idx].album_start_offset,
                  catalog->artists[artist_idx].album_count, sizeof(uint32_t), compare_album_title);
        }
        if (catalog->artists[artist_idx].track_count > 1u) {
            qsort(catalog->artist_track_indices + catalog->artists[artist_idx].track_start_offset,
                  catalog->artists[artist_idx].track_count, sizeof(uint32_t), compare_track_title);
        }
    }

    /* 4. Multi-genre splitting */
    uint32_t genre_pairs_cap = catalog->track_count * 2u;
    uint32_t *pair_genre = malloc((size_t)genre_pairs_cap * sizeof(uint32_t));
    uint32_t *pair_track = malloc((size_t)genre_pairs_cap * sizeof(uint32_t));
    uint32_t pair_count = 0u;
    if (pair_genre != NULL && pair_track != NULL) {
        for (uint32_t track_idx = 0u; track_idx < catalog->track_count; track_idx++) {
            const char *raw_genre =
                library_catalog_get_string(catalog, catalog->tracks[track_idx].genre_raw_offset);
            char copy_buf[256];
            snprintf(copy_buf, sizeof(copy_buf), "%s", raw_genre[0] != '\0' ? raw_genre : "Unknown Genre");
            char *segment = copy_buf;
            bool added_any = false;
            while (segment != NULL && *segment != '\0') {
                char *sep = strpbrk(segment, ";/,");
                if (sep != NULL) {
                    *sep = '\0';
                }
                trim_whitespace_inplace(segment);
                if (segment[0] != '\0') {
                    uint32_t genre_idx = find_or_create_genre(catalog, segment);
                    if (genre_idx != UINT32_MAX) {
                        bool duplicate = false;
                        for (uint32_t scan = pair_count; scan > 0u && pair_track[scan - 1u] == track_idx;
                             scan--) {
                            if (pair_genre[scan - 1u] == genre_idx) {
                                duplicate = true;
                                break;
                            }
                        }
                        if (!duplicate && pair_count < genre_pairs_cap) {
                            pair_genre[pair_count] = genre_idx;
                            pair_track[pair_count] = track_idx;
                            pair_count++;
                            catalog->genres[genre_idx].track_count++;
                            added_any = true;
                        }
                    }
                }
                segment = (sep != NULL) ? (sep + 1) : NULL;
            }
            if (!added_any && pair_count < genre_pairs_cap) {
                uint32_t genre_idx = find_or_create_genre(catalog, "Unknown Genre");
                if (genre_idx != UINT32_MAX) {
                    pair_genre[pair_count] = genre_idx;
                    pair_track[pair_count] = track_idx;
                    pair_count++;
                    catalog->genres[genre_idx].track_count++;
                }
            }
        }

        if (pair_count > 0u) {
            catalog->genre_track_indices = malloc((size_t)pair_count * sizeof(uint32_t));
            if (catalog->genre_track_indices != NULL) {
                uint32_t g_cursor = 0u;
                for (uint32_t genre_idx = 0u; genre_idx < catalog->genre_count; genre_idx++) {
                    catalog->genres[genre_idx].track_start_offset = g_cursor;
                    g_cursor += catalog->genres[genre_idx].track_count;
                    catalog->genres[genre_idx].track_count = 0u;
                }
                catalog->genre_track_index_count = g_cursor;
                for (uint32_t pair_idx = 0u; pair_idx < pair_count; pair_idx++) {
                    uint32_t genre_idx = pair_genre[pair_idx];
                    uint32_t slot = catalog->genres[genre_idx].track_start_offset +
                                    catalog->genres[genre_idx].track_count++;
                    catalog->genre_track_indices[slot] = pair_track[pair_idx];
                }
                for (uint32_t genre_idx = 0u; genre_idx < catalog->genre_count; genre_idx++) {
                    if (catalog->genres[genre_idx].track_count > 1u) {
                        qsort(catalog->genre_track_indices +
                                  catalog->genres[genre_idx].track_start_offset,
                              catalog->genres[genre_idx].track_count, sizeof(uint32_t),
                              compare_track_title);
                    }
                }
            }
        }
    }
    free(pair_genre);
    free(pair_track);
    library_catalog_recompute_favorite_counts(catalog);
    return true;
}

void library_catalog_recompute_favorite_counts(struct library_catalog *catalog) {
    if (catalog == NULL) {
        return;
    }
    uint32_t fav_tracks = 0u;
    uint32_t fav_albums = 0u;
    uint32_t fav_artists = 0u;
    for (uint32_t idx = 0u; idx < catalog->track_count; idx++) {
        if (catalog->tracks[idx].is_favorite) {
            fav_tracks++;
        }
    }
    for (uint32_t idx = 0u; idx < catalog->album_count; idx++) {
        if (catalog->albums[idx].is_favorite) {
            fav_albums++;
        }
    }
    for (uint32_t idx = 0u; idx < catalog->artist_count; idx++) {
        if (catalog->artists[idx].is_favorite) {
            fav_artists++;
        }
    }
    catalog->favorite_track_count = fav_tracks;
    catalog->favorite_album_count = fav_albums;
    catalog->favorite_artist_count = fav_artists;
}

bool library_catalog_add_playlist(struct library_catalog *catalog, uint64_t playlist_id,
                                  const char *name, const char *source_m3u_path,
                                  const uint32_t *track_indices, uint32_t track_count) {
    if (catalog == NULL || name == NULL || catalog->playlist_count >= LIBRARY_MAX_PLAYLISTS) {
        return false;
    }
    if (catalog->playlist_count >= catalog->playlist_capacity) {
        uint32_t new_cap = catalog->playlist_capacity == 0u ? 16u : catalog->playlist_capacity * 2u;
        if (new_cap > LIBRARY_MAX_PLAYLISTS) {
            new_cap = LIBRARY_MAX_PLAYLISTS;
        }
        struct library_playlist *grown =
            realloc(catalog->playlists, (size_t)new_cap * sizeof(struct library_playlist));
        if (grown == NULL) {
            return false;
        }
        catalog->playlists = grown;
        catalog->playlist_capacity = new_cap;
    }

    uint32_t start_offset = catalog->playlist_track_index_count;
    if (track_count > 0u) {
        if (catalog->playlist_track_index_count + track_count >
            catalog->playlist_track_index_capacity) {
            uint32_t new_cap = catalog->playlist_track_index_capacity == 0u
                                   ? 128u
                                   : catalog->playlist_track_index_capacity * 2u;
            while (new_cap < catalog->playlist_track_index_count + track_count) {
                new_cap *= 2u;
            }
            uint32_t *grown = realloc(catalog->playlist_track_indices,
                                      (size_t)new_cap * sizeof(uint32_t));
            if (grown == NULL) {
                return false;
            }
            catalog->playlist_track_indices = grown;
            catalog->playlist_track_index_capacity = new_cap;
        }
        memcpy(catalog->playlist_track_indices + start_offset, track_indices,
               (size_t)track_count * sizeof(uint32_t));
        catalog->playlist_track_index_count += track_count;
    }

    struct library_playlist *playlist = &catalog->playlists[catalog->playlist_count++];
    memset(playlist, 0, sizeof(*playlist));
    playlist->playlist_id = playlist_id;
    playlist->name_offset = library_catalog_intern_string(catalog, name);
    playlist->source_m3u_offset =
        (source_m3u_path != NULL && source_m3u_path[0] != '\0')
            ? library_catalog_intern_string(catalog, source_m3u_path)
            : 0u;
    playlist->track_start_offset = start_offset;
    playlist->track_count = track_count;
    return true;
}

bool library_catalog_remove_playlist(struct library_catalog *catalog, uint32_t playlist_index) {
    if (catalog == NULL || playlist_index >= catalog->playlist_count) {
        return false;
    }
    for (uint32_t idx = playlist_index; idx + 1u < catalog->playlist_count; idx++) {
        catalog->playlists[idx] = catalog->playlists[idx + 1u];
    }
    catalog->playlist_count--;
    return true;
}

int32_t library_catalog_find_track_by_path(const struct library_catalog *catalog,
                                           const char *file_path) {
    if (catalog == NULL || file_path == NULL) {
        return -1;
    }
    for (uint32_t idx = 0u; idx < catalog->track_count; idx++) {
        if (strcmp(library_catalog_get_string(catalog, catalog->tracks[idx].path_offset),
                   file_path) == 0) {
            return (int32_t)idx;
        }
    }
    return -1;
}

int32_t library_catalog_find_track_by_id(const struct library_catalog *catalog, uint64_t track_id) {
    if (catalog == NULL) {
        return -1;
    }
    for (uint32_t idx = 0u; idx < catalog->track_count; idx++) {
        if (catalog->tracks[idx].track_id == track_id) {
            return (int32_t)idx;
        }
    }
    return -1;
}

int32_t library_catalog_find_album_by_id(const struct library_catalog *catalog, uint64_t album_id) {
    if (catalog == NULL) {
        return -1;
    }
    for (uint32_t idx = 0u; idx < catalog->album_count; idx++) {
        if (catalog->albums[idx].album_id == album_id) {
            return (int32_t)idx;
        }
    }
    return -1;
}

int32_t library_catalog_find_artist_by_id(const struct library_catalog *catalog, uint64_t artist_id) {
    if (catalog == NULL) {
        return -1;
    }
    for (uint32_t idx = 0u; idx < catalog->artist_count; idx++) {
        if (catalog->artists[idx].artist_id == artist_id) {
            return (int32_t)idx;
        }
    }
    return -1;
}

static int compare_albums_by_mode(const void *first_ptr, const void *second_ptr) {
    uint32_t first_idx = *(const uint32_t *)first_ptr;
    uint32_t second_idx = *(const uint32_t *)second_ptr;
    const struct library_album *left = &g_sort_catalog->albums[first_idx];
    const struct library_album *right = &g_sort_catalog->albums[second_idx];
    const char *left_title = library_catalog_get_string(g_sort_catalog, left->title_offset);
    const char *right_title = library_catalog_get_string(g_sort_catalog, right->title_offset);

    switch (g_sort_mode) {
        case LIBRARY_SORT_REVERSE_ALPHABETICAL:
            return library_compare_ascii_casefold(right_title, left_title);
        case LIBRARY_SORT_LIKED_FIRST:
            if (left->is_favorite != right->is_favorite) {
                return left->is_favorite ? -1 : 1;
            }
            return library_compare_ascii_casefold(left_title, right_title);
        case LIBRARY_SORT_RECENT:
            if (left->added_at_seconds != right->added_at_seconds) {
                return left->added_at_seconds > right->added_at_seconds ? -1 : 1;
            }
            return library_compare_ascii_casefold(left_title, right_title);
        case LIBRARY_SORT_OLDEST: {
            int64_t left_ts = left->added_at_seconds > 0 ? left->added_at_seconds : INT64_MAX;
            int64_t right_ts = right->added_at_seconds > 0 ? right->added_at_seconds : INT64_MAX;
            if (left_ts != right_ts) {
                return left_ts < right_ts ? -1 : 1;
            }
            return library_compare_ascii_casefold(left_title, right_title);
        }
        case LIBRARY_SORT_ALPHABETICAL:
        default:
            return library_compare_ascii_casefold(left_title, right_title);
    }
}

static int compare_artists_by_mode(const void *first_ptr, const void *second_ptr) {
    uint32_t first_idx = *(const uint32_t *)first_ptr;
    uint32_t second_idx = *(const uint32_t *)second_ptr;
    const struct library_artist *left = &g_sort_catalog->artists[first_idx];
    const struct library_artist *right = &g_sort_catalog->artists[second_idx];
    const char *left_name = library_catalog_get_string(g_sort_catalog, left->name_offset);
    const char *right_name = library_catalog_get_string(g_sort_catalog, right->name_offset);

    switch (g_sort_mode) {
        case LIBRARY_SORT_REVERSE_ALPHABETICAL:
            return library_compare_ascii_casefold(right_name, left_name);
        case LIBRARY_SORT_LIKED_FIRST:
            if (left->is_favorite != right->is_favorite) {
                return left->is_favorite ? -1 : 1;
            }
            return library_compare_ascii_casefold(left_name, right_name);
        case LIBRARY_SORT_RECENT:
            if (left->added_at_seconds != right->added_at_seconds) {
                return left->added_at_seconds > right->added_at_seconds ? -1 : 1;
            }
            return library_compare_ascii_casefold(left_name, right_name);
        case LIBRARY_SORT_OLDEST: {
            int64_t left_ts = left->added_at_seconds > 0 ? left->added_at_seconds : INT64_MAX;
            int64_t right_ts = right->added_at_seconds > 0 ? right->added_at_seconds : INT64_MAX;
            if (left_ts != right_ts) {
                return left_ts < right_ts ? -1 : 1;
            }
            return library_compare_ascii_casefold(left_name, right_name);
        }
        case LIBRARY_SORT_ALPHABETICAL:
        default:
            return library_compare_ascii_casefold(left_name, right_name);
    }
}

void library_catalog_sort_album_indices(const struct library_catalog *catalog,
                                        enum library_sort_mode sort_mode, uint32_t *indices,
                                        uint32_t count) {
    if (catalog == NULL || indices == NULL || count <= 1u) {
        return;
    }
    g_sort_catalog = catalog;
    g_sort_mode = sort_mode;
    qsort(indices, count, sizeof(uint32_t), compare_albums_by_mode);
}

void library_catalog_sort_artist_indices(const struct library_catalog *catalog,
                                         enum library_sort_mode sort_mode, uint32_t *indices,
                                         uint32_t count) {
    if (catalog == NULL || indices == NULL || count <= 1u) {
        return;
    }
    g_sort_catalog = catalog;
    g_sort_mode = sort_mode;
    qsort(indices, count, sizeof(uint32_t), compare_artists_by_mode);
}

static char resolve_alpha_bucket(const char *label) {
    if (label == NULL || label[0] == '\0') {
        return '#';
    }
    char first = label[0];
    if (first >= 'a' && first <= 'z') {
        return (char)(first - ('a' - 'A'));
    }
    if (first >= 'A' && first <= 'Z') {
        return first;
    }
    return '#';
}

uint32_t library_build_seek_anchors(const struct library_catalog *catalog,
                                    enum library_sort_mode sort_mode, bool supports_date_mode,
                                    uint32_t item_count, library_seek_label_fn item_getter,
                                    void *user_context, struct library_seek_anchor *anchors_out,
                                    uint32_t max_anchors) {
    if (catalog == NULL || item_getter == NULL || anchors_out == NULL || max_anchors == 0u ||
        item_count == 0u) {
        return 0u;
    }

    bool is_date_sort = supports_date_mode &&
                        (sort_mode == LIBRARY_SORT_RECENT || sort_mode == LIBRARY_SORT_OLDEST);
    uint32_t date_granularity = 0u; /* 0=year, 1=month, 2=day */
    if (is_date_sort) {
        uint32_t month_keys[64];
        uint32_t distinct_months = 0u;
        for (uint32_t idx = 0u; idx < item_count; idx++) {
            int64_t added_at = 0;
            bool fav = false;
            (void)item_getter(catalog, idx, user_context, &added_at, &fav);
            if (added_at > 0) {
                time_t epoch = (time_t)added_at;
                struct tm utc_tm;
                if (gmtime_r(&epoch, &utc_tm) != NULL) {
                    uint32_t ym = (uint32_t)((utc_tm.tm_year + 1900) * 16 + utc_tm.tm_mon);
                    bool found = false;
                    for (uint32_t m_idx = 0u; m_idx < distinct_months; m_idx++) {
                        if (month_keys[m_idx] == ym) {
                            found = true;
                            break;
                        }
                    }
                    if (!found && distinct_months < 64u) {
                        month_keys[distinct_months++] = ym;
                    }
                }
            }
        }
        if (distinct_months > MONTHS_PER_YEAR) {
            date_granularity = 0u;
        } else if (distinct_months >= 2u) {
            date_granularity = 1u;
        } else {
            date_granularity = 2u;
        }
    }

    static const char *MONTH_NAMES[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    uint32_t anchor_count = 0u;

    for (uint32_t idx = 0u; idx < item_count; idx++) {
        int64_t added_at = 0;
        bool is_fav = false;
        const char *label = item_getter(catalog, idx, user_context, &added_at, &is_fav);

        struct library_seek_anchor candidate;
        memset(&candidate, 0, sizeof(candidate));
        candidate.first_item_index = idx;

        if (is_date_sort) {
            candidate.is_date_mode = true;
            if (added_at <= 0) {
                snprintf(candidate.key, sizeof(candidate.key), "date-unknown");
                snprintf(candidate.primary, sizeof(candidate.primary), "\xE2\x80\x94");
                snprintf(candidate.sub_label, sizeof(candidate.sub_label), "NO DATE");
                snprintf(candidate.short_label, sizeof(candidate.short_label), "\xE2\x80\x94");
            } else {
                time_t epoch = (time_t)added_at;
                struct tm utc_tm;
                gmtime_r(&epoch, &utc_tm);
                int year_full = utc_tm.tm_year + 1900;
                int year_short = year_full % 100;
                const char *mon = MONTH_NAMES[(uint32_t)utc_tm.tm_mon % 12u];
                if (date_granularity == 0u) {
                    snprintf(candidate.key, sizeof(candidate.key), "y:%04d", year_full);
                    snprintf(candidate.primary, sizeof(candidate.primary), "%04d", year_full);
                    snprintf(candidate.short_label, sizeof(candidate.short_label), "%04d", year_full);
                } else if (date_granularity == 1u) {
                    snprintf(candidate.key, sizeof(candidate.key), "m:%04d-%02d", year_full,
                             utc_tm.tm_mon + 1);
                    snprintf(candidate.primary, sizeof(candidate.primary), "%s", mon);
                    snprintf(candidate.sub_label, sizeof(candidate.sub_label), "%04d", year_full);
                    snprintf(candidate.short_label, sizeof(candidate.short_label), "%s \xE2\x80\x99%02d",
                             mon, year_short);
                } else {
                    snprintf(candidate.key, sizeof(candidate.key), "d:%04d-%02d-%02d", year_full,
                             utc_tm.tm_mon + 1, utc_tm.tm_mday);
                    snprintf(candidate.primary, sizeof(candidate.primary), "%d", utc_tm.tm_mday);
                    snprintf(candidate.sub_label, sizeof(candidate.sub_label), "%s \xE2\x80\x99%02d",
                             mon, year_short);
                    snprintf(candidate.short_label, sizeof(candidate.short_label), "%s %d", mon,
                             utc_tm.tm_mday);
                }
            }
        } else {
            char letter = resolve_alpha_bucket(label);
            if (sort_mode == LIBRARY_SORT_LIKED_FIRST) {
                snprintf(candidate.key, sizeof(candidate.key), "%s:%c", is_fav ? "liked" : "other",
                         letter);
                candidate.is_favorite = is_fav;
            } else {
                snprintf(candidate.key, sizeof(candidate.key), "%c", letter);
            }
            candidate.primary[0] = letter;
            candidate.primary[1] = '\0';
            candidate.short_label[0] = letter;
            candidate.short_label[1] = '\0';
        }

        bool exists = false;
        for (uint32_t a_idx = 0u; a_idx < anchor_count; a_idx++) {
            if (strcmp(anchors_out[a_idx].key, candidate.key) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists && anchor_count < max_anchors) {
            anchors_out[anchor_count++] = candidate;
        }
    }
    return anchor_count;
}

struct index_file_header {
    uint32_t magic;
    uint32_t version;
    uint64_t fingerprint;
    uint32_t strings_used;
    uint32_t track_count;
    uint32_t album_count;
    uint32_t artist_count;
    uint32_t genre_count;
    uint32_t playlist_count;
    uint32_t album_track_index_count;
    uint32_t artist_album_index_count;
    uint32_t artist_track_index_count;
    uint32_t genre_track_index_count;
    uint32_t playlist_track_index_count;
};

static bool write_all_fd(int fd, const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t written_total = 0u;
    while (written_total < size) {
        ssize_t chunk = write(fd, bytes + written_total, size - written_total);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        written_total += (size_t)chunk;
    }
    return true;
}

bool library_catalog_save_index(const struct library_catalog *catalog, const char *directory_path,
                                const char *temp_path, const char *final_path) {
    if (catalog == NULL || directory_path == NULL || temp_path == NULL || final_path == NULL) {
        return false;
    }
    if (mkdir(directory_path, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    int fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    struct index_file_header header = {
        .magic = INDEX_MAGIC,
        .version = INDEX_VERSION,
        .fingerprint = catalog->fingerprint,
        .strings_used = (uint32_t)catalog->strings_used,
        .track_count = catalog->track_count,
        .album_count = catalog->album_count,
        .artist_count = catalog->artist_count,
        .genre_count = catalog->genre_count,
        .playlist_count = catalog->playlist_count,
        .album_track_index_count = catalog->album_track_index_count,
        .artist_album_index_count = catalog->artist_album_index_count,
        .artist_track_index_count = catalog->artist_track_index_count,
        .genre_track_index_count = catalog->genre_track_index_count,
        .playlist_track_index_count = catalog->playlist_track_index_count,
    };

    bool ok = write_all_fd(fd, &header, sizeof(header));
    if (ok && header.strings_used > 0u) {
        ok = write_all_fd(fd, catalog->strings, header.strings_used);
    }
    if (ok && header.track_count > 0u) {
        ok = write_all_fd(fd, catalog->tracks,
                          (size_t)header.track_count * sizeof(struct library_track));
        if (ok) {
            ok = write_all_fd(fd, catalog->all_track_indices,
                              (size_t)header.track_count * sizeof(uint32_t));
        }
    }
    if (ok && header.album_count > 0u) {
        ok = write_all_fd(fd, catalog->albums,
                          (size_t)header.album_count * sizeof(struct library_album));
    }
    if (ok && header.artist_count > 0u) {
        ok = write_all_fd(fd, catalog->artists,
                          (size_t)header.artist_count * sizeof(struct library_artist));
    }
    if (ok && header.genre_count > 0u) {
        ok = write_all_fd(fd, catalog->genres,
                          (size_t)header.genre_count * sizeof(struct library_genre));
    }
    if (ok && header.playlist_count > 0u) {
        ok = write_all_fd(fd, catalog->playlists,
                          (size_t)header.playlist_count * sizeof(struct library_playlist));
    }
    if (ok && header.album_track_index_count > 0u) {
        ok = write_all_fd(fd, catalog->album_track_indices,
                          (size_t)header.album_track_index_count * sizeof(uint32_t));
    }
    if (ok && header.artist_album_index_count > 0u) {
        ok = write_all_fd(fd, catalog->artist_album_indices,
                          (size_t)header.artist_album_index_count * sizeof(uint32_t));
    }
    if (ok && header.artist_track_index_count > 0u) {
        ok = write_all_fd(fd, catalog->artist_track_indices,
                          (size_t)header.artist_track_index_count * sizeof(uint32_t));
    }
    if (ok && header.genre_track_index_count > 0u) {
        ok = write_all_fd(fd, catalog->genre_track_indices,
                          (size_t)header.genre_track_index_count * sizeof(uint32_t));
    }
    if (ok && header.playlist_track_index_count > 0u) {
        ok = write_all_fd(fd, catalog->playlist_track_indices,
                          (size_t)header.playlist_track_index_count * sizeof(uint32_t));
    }
    if (ok && fsync(fd) != 0) {
        ok = false;
    }
    close(fd);
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

static bool read_exact_file(FILE *file, void *dest, size_t size) {
    if (size == 0u) {
        return true;
    }
    return fread(dest, 1u, size, file) == size;
}

bool library_catalog_load_index(struct library_catalog *catalog, const char *index_path) {
    if (catalog == NULL || index_path == NULL) {
        return false;
    }
    FILE *file = fopen(index_path, "rb");
    if (file == NULL) {
        return false;
    }

    struct index_file_header header;
    if (!read_exact_file(file, &header, sizeof(header)) || header.magic != INDEX_MAGIC ||
        header.version != INDEX_VERSION || header.strings_used == 0u ||
        header.strings_used > LIBRARY_MAX_STRING_ARENA_BYTES ||
        header.track_count > LIBRARY_MAX_TRACKS || header.album_count > LIBRARY_MAX_ALBUMS ||
        header.artist_count > LIBRARY_MAX_ARTISTS || header.genre_count > LIBRARY_MAX_GENRES ||
        header.playlist_count > LIBRARY_MAX_PLAYLISTS ||
        header.album_track_index_count > LIBRARY_MAX_TRACKS ||
        header.artist_album_index_count > LIBRARY_MAX_ALBUMS ||
        header.artist_track_index_count > LIBRARY_MAX_TRACKS ||
        header.genre_track_index_count > LIBRARY_MAX_TRACKS * 4u ||
        header.playlist_track_index_count > LIBRARY_MAX_TRACKS * 4u) {
        fclose(file);
        return false;
    }

    library_catalog_destroy(catalog);
    library_catalog_initialize(catalog);
    catalog->fingerprint = header.fingerprint;

    if (header.strings_used > catalog->strings_capacity) {
        char *grown = realloc(catalog->strings, header.strings_used);
        if (grown == NULL) {
            fclose(file);
            return false;
        }
        catalog->strings = grown;
        catalog->strings_capacity = header.strings_used;
    }
    if (!read_exact_file(file, catalog->strings, header.strings_used)) {
        fclose(file);
        library_catalog_clear(catalog);
        return false;
    }
    catalog->strings_used = header.strings_used;
    catalog->strings[header.strings_used - 1u] = '\0';

    bool ok = true;
    if (header.track_count > 0u) {
        catalog->tracks = malloc((size_t)header.track_count * sizeof(struct library_track));
        catalog->all_track_indices = malloc((size_t)header.track_count * sizeof(uint32_t));
        ok = (catalog->tracks != NULL && catalog->all_track_indices != NULL) &&
             read_exact_file(file, catalog->tracks,
                             (size_t)header.track_count * sizeof(struct library_track)) &&
             read_exact_file(file, catalog->all_track_indices,
                             (size_t)header.track_count * sizeof(uint32_t));
        catalog->track_count = header.track_count;
        catalog->track_capacity = header.track_count;
    }
    if (ok && header.album_count > 0u) {
        catalog->albums = malloc((size_t)header.album_count * sizeof(struct library_album));
        ok = (catalog->albums != NULL) &&
             read_exact_file(file, catalog->albums,
                             (size_t)header.album_count * sizeof(struct library_album));
        catalog->album_count = header.album_count;
        catalog->album_capacity = header.album_count;
    }
    if (ok && header.artist_count > 0u) {
        catalog->artists = malloc((size_t)header.artist_count * sizeof(struct library_artist));
        ok = (catalog->artists != NULL) &&
             read_exact_file(file, catalog->artists,
                             (size_t)header.artist_count * sizeof(struct library_artist));
        catalog->artist_count = header.artist_count;
        catalog->artist_capacity = header.artist_count;
    }
    if (ok && header.genre_count > 0u) {
        catalog->genres = malloc((size_t)header.genre_count * sizeof(struct library_genre));
        ok = (catalog->genres != NULL) &&
             read_exact_file(file, catalog->genres,
                             (size_t)header.genre_count * sizeof(struct library_genre));
        catalog->genre_count = header.genre_count;
        catalog->genre_capacity = header.genre_count;
    }
    if (ok && header.playlist_count > 0u) {
        catalog->playlists = malloc((size_t)header.playlist_count * sizeof(struct library_playlist));
        ok = (catalog->playlists != NULL) &&
             read_exact_file(file, catalog->playlists,
                             (size_t)header.playlist_count * sizeof(struct library_playlist));
        catalog->playlist_count = header.playlist_count;
        catalog->playlist_capacity = header.playlist_count;
    }
    if (ok && header.album_track_index_count > 0u) {
        catalog->album_track_indices =
            malloc((size_t)header.album_track_index_count * sizeof(uint32_t));
        ok = (catalog->album_track_indices != NULL) &&
             read_exact_file(file, catalog->album_track_indices,
                             (size_t)header.album_track_index_count * sizeof(uint32_t));
        catalog->album_track_index_count = header.album_track_index_count;
    }
    if (ok && header.artist_album_index_count > 0u) {
        catalog->artist_album_indices =
            malloc((size_t)header.artist_album_index_count * sizeof(uint32_t));
        ok = (catalog->artist_album_indices != NULL) &&
             read_exact_file(file, catalog->artist_album_indices,
                             (size_t)header.artist_album_index_count * sizeof(uint32_t));
        catalog->artist_album_index_count = header.artist_album_index_count;
    }
    if (ok && header.artist_track_index_count > 0u) {
        catalog->artist_track_indices =
            malloc((size_t)header.artist_track_index_count * sizeof(uint32_t));
        ok = (catalog->artist_track_indices != NULL) &&
             read_exact_file(file, catalog->artist_track_indices,
                             (size_t)header.artist_track_index_count * sizeof(uint32_t));
        catalog->artist_track_index_count = header.artist_track_index_count;
    }
    if (ok && header.genre_track_index_count > 0u) {
        catalog->genre_track_indices =
            malloc((size_t)header.genre_track_index_count * sizeof(uint32_t));
        ok = (catalog->genre_track_indices != NULL) &&
             read_exact_file(file, catalog->genre_track_indices,
                             (size_t)header.genre_track_index_count * sizeof(uint32_t));
        catalog->genre_track_index_count = header.genre_track_index_count;
    }
    if (ok && header.playlist_track_index_count > 0u) {
        catalog->playlist_track_indices =
            malloc((size_t)header.playlist_track_index_count * sizeof(uint32_t));
        ok = (catalog->playlist_track_indices != NULL) &&
             read_exact_file(file, catalog->playlist_track_indices,
                             (size_t)header.playlist_track_index_count * sizeof(uint32_t));
        catalog->playlist_track_index_count = header.playlist_track_index_count;
        catalog->playlist_track_index_capacity = header.playlist_track_index_count;
    }
    fclose(file);
    if (!ok) {
        library_catalog_destroy(catalog);
        library_catalog_initialize(catalog);
        return false;
    }
    library_catalog_recompute_favorite_counts(catalog);
    return true;
}
