#ifndef DIALOS_LIBRARY_CATALOG_H
#define DIALOS_LIBRARY_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBRARY_MAX_TRACKS 16384u
#define LIBRARY_MAX_ALBUMS 4096u
#define LIBRARY_MAX_ARTISTS 4096u
#define LIBRARY_MAX_GENRES 1024u
#define LIBRARY_MAX_PLAYLISTS 256u
#define LIBRARY_MAX_STRING_ARENA_BYTES (4u * 1024u * 1024u)
#define LIBRARY_COVER_MOTIF_COUNT 6u

enum library_sort_mode {
    LIBRARY_SORT_ALPHABETICAL = 0,
    LIBRARY_SORT_REVERSE_ALPHABETICAL = 1,
    LIBRARY_SORT_LIKED_FIRST = 2,
    LIBRARY_SORT_RECENT = 3,
    LIBRARY_SORT_OLDEST = 4,
    LIBRARY_SORT_MODE_COUNT = 5
};

enum library_audio_format {
    LIBRARY_FORMAT_UNKNOWN = 0,
    LIBRARY_FORMAT_FLAC = 1,
    LIBRARY_FORMAT_MP3 = 2,
    LIBRARY_FORMAT_M4A = 3,
    LIBRARY_FORMAT_WAV = 4,
    LIBRARY_FORMAT_OTHER = 5
};

enum library_cover_source_kind {
    LIBRARY_COVER_NONE = 0,
    LIBRARY_COVER_SIDECAR = 1,
    LIBRARY_COVER_EMBEDDED_FLAC = 2,
    LIBRARY_COVER_EMBEDDED_MP3 = 3,
    LIBRARY_COVER_EMBEDDED_M4A = 4
};

struct library_track {
    uint64_t track_id;
    uint32_t path_offset;
    uint32_t title_offset;
    uint32_t artist_offset;
    uint32_t album_offset;
    uint32_t genre_raw_offset;
    uint32_t album_index;
    uint32_t artist_index;
    uint16_t disc_number;
    uint16_t track_number;
    uint32_t duration_seconds;
    int64_t added_at_seconds;
    uint64_t file_size_bytes;
    uint8_t format_kind;
    bool is_favorite;
};

struct library_album {
    uint64_t album_id;
    uint32_t title_offset;
    uint32_t artist_offset;
    uint32_t artist_index;
    uint32_t genre_offset;
    uint16_t year;
    uint8_t motif_id;
    uint8_t cover_source_kind;
    uint32_t cover_path_offset;
    uint32_t cover_data_offset;
    uint32_t cover_data_length;
    uint32_t track_start_offset;
    uint32_t track_count;
    int64_t added_at_seconds;
    bool is_favorite;
};

struct library_artist {
    uint64_t artist_id;
    uint32_t name_offset;
    uint32_t album_start_offset;
    uint32_t album_count;
    uint32_t track_start_offset;
    uint32_t track_count;
    int64_t added_at_seconds;
    bool is_favorite;
};

struct library_genre {
    uint64_t genre_id;
    uint32_t name_offset;
    uint32_t track_start_offset;
    uint32_t track_count;
};

struct library_playlist {
    uint64_t playlist_id;
    uint32_t name_offset;
    uint32_t source_m3u_offset;
    uint32_t track_start_offset;
    uint32_t track_count;
};

struct library_seek_anchor {
    char key[24];
    char primary[16];
    char sub_label[16];
    char short_label[16];
    bool is_date_mode;
    bool is_favorite;
    uint32_t first_item_index;
};

struct library_track_input {
    const char *file_path;
    const char *relative_path;
    const char *title;
    const char *artist;
    const char *album_artist;
    const char *album;
    const char *genre;
    uint16_t year;
    uint16_t disc_number;
    uint16_t track_number;
    uint32_t duration_seconds;
    int64_t added_at_seconds;
    uint64_t file_size_bytes;
    uint8_t format_kind;
    uint8_t cover_source_kind;
    const char *cover_path;
    uint32_t cover_data_offset;
    uint32_t cover_data_length;
    uint64_t audio_start_offset;
    uint64_t audio_payload_bytes;
};

struct library_catalog {
    char *strings;
    size_t strings_used;
    size_t strings_capacity;

    struct library_track *tracks;
    uint32_t track_count;
    uint32_t track_capacity;

    struct library_album *albums;
    uint32_t album_count;
    uint32_t album_capacity;

    struct library_artist *artists;
    uint32_t artist_count;
    uint32_t artist_capacity;

    struct library_genre *genres;
    uint32_t genre_count;
    uint32_t genre_capacity;

    struct library_playlist *playlists;
    uint32_t playlist_count;
    uint32_t playlist_capacity;

    uint32_t *all_track_indices;
    uint32_t *album_track_indices;
    uint32_t album_track_index_count;
    uint32_t *artist_album_indices;
    uint32_t artist_album_index_count;
    uint32_t *artist_track_indices;
    uint32_t artist_track_index_count;
    uint32_t *genre_track_indices;
    uint32_t genre_track_index_count;
    uint32_t *playlist_track_indices;
    uint32_t playlist_track_index_count;
    uint32_t playlist_track_index_capacity;

    uint64_t fingerprint;
    uint32_t favorite_track_count;
    uint32_t favorite_album_count;
    uint32_t favorite_artist_count;
};

uint64_t library_fnv1a_64(const char *text);
int32_t library_compare_ascii_casefold(const char *first, const char *second);
const char *library_sort_mode_toast_label(enum library_sort_mode mode);

void library_catalog_initialize(struct library_catalog *catalog);
void library_catalog_clear(struct library_catalog *catalog);
void library_catalog_destroy(struct library_catalog *catalog);

const char *library_catalog_get_string(const struct library_catalog *catalog, uint32_t offset);
uint32_t library_catalog_intern_string(struct library_catalog *catalog, const char *text);

bool library_catalog_add_track(struct library_catalog *catalog,
                               const struct library_track_input *input);
bool library_catalog_finalize(struct library_catalog *catalog);
void library_catalog_recompute_favorite_counts(struct library_catalog *catalog);

bool library_catalog_add_playlist(struct library_catalog *catalog, uint64_t playlist_id,
                                  const char *name, const char *source_m3u_path,
                                  const uint32_t *track_indices, uint32_t track_count);
bool library_catalog_remove_playlist(struct library_catalog *catalog, uint32_t playlist_index);
int32_t library_catalog_find_track_by_path(const struct library_catalog *catalog,
                                           const char *file_path);
int32_t library_catalog_find_track_by_id(const struct library_catalog *catalog, uint64_t track_id);
int32_t library_catalog_find_album_by_id(const struct library_catalog *catalog, uint64_t album_id);
int32_t library_catalog_find_artist_by_id(const struct library_catalog *catalog, uint64_t artist_id);

void library_catalog_sort_album_indices(const struct library_catalog *catalog,
                                        enum library_sort_mode sort_mode, uint32_t *indices,
                                        uint32_t count);
void library_catalog_sort_artist_indices(const struct library_catalog *catalog,
                                         enum library_sort_mode sort_mode, uint32_t *indices,
                                         uint32_t count);

typedef const char *(*library_seek_label_fn)(const struct library_catalog *catalog,
                                             uint32_t item_index, void *user_context,
                                             int64_t *added_at_out, bool *is_favorite_out);

uint32_t library_build_seek_anchors(const struct library_catalog *catalog,
                                    enum library_sort_mode sort_mode, bool supports_date_mode,
                                    uint32_t item_count, library_seek_label_fn item_getter,
                                    void *user_context, struct library_seek_anchor *anchors_out,
                                    uint32_t max_anchors);

bool library_catalog_save_index(const struct library_catalog *catalog, const char *directory_path,
                                const char *temp_path, const char *final_path);
bool library_catalog_load_index(struct library_catalog *catalog, const char *index_path);

#endif
