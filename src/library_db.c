#include "library_db.h"

#include "sqlite3.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FAT32_MTIME_TOLERANCE_SECONDS 2ll
#define MONTHS_PER_YEAR 12u
#define TWO_DIGIT_YEAR_MODULUS 100
#define SEEK_YEAR_THRESHOLD_MONTHS 12u
#define SEEK_MIN_GROUPS 2u
#define UNORDERED_TRACK_NUMBER 9999u
#define SORT_KEY_BYTES 128u
#define GENRE_TOKEN_BYTES 64u
#define SQL_STATEMENT_BYTES 1024u

static char ascii_tolower_char(char character) {
    if (character >= 'A' && character <= 'Z') {
        return (char)(character + ('a' - 'A'));
    }
    return character;
}

FILE *library_fopen_readonly_noatime(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    int descriptor = -1;
#if defined(O_NOATIME)
    descriptor = open(path, O_RDONLY | O_NOATIME);
    if (descriptor < 0 && (errno == EPERM || errno == EINVAL)) {
        descriptor = open(path, O_RDONLY);
    }
#else
    descriptor = open(path, O_RDONLY);
#endif
    if (descriptor < 0) {
        return NULL;
    }
    FILE *stream = fdopen(descriptor, "rb");
    if (stream == NULL) {
        (void)close(descriptor);
        return NULL;
    }
    return stream;
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
        char left_char = ascii_tolower_char(left[index]);
        char right_char = ascii_tolower_char(right[index]);
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

static void copy_text_bounded(char *destination, size_t capacity, const char *source) {
    if (capacity == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    size_t length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1u;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static void build_sort_key(const char *source, char *destination, size_t capacity) {
    if (capacity == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    size_t start = 0u;
    while (source[start] != '\0' && isspace((unsigned char)source[start])) {
        start++;
    }
    size_t out_index = 0u;
    while (source[start] != '\0' && out_index + 1u < capacity) {
        char character = source[start++];
        if (character >= 'A' && character <= 'Z') {
            character = (char)(character + ('a' - 'A'));
        }
        destination[out_index++] = character;
    }
    while (out_index > 0u && isspace((unsigned char)destination[out_index - 1u])) {
        out_index--;
    }
    destination[out_index] = '\0';
}

static void trim_whitespace(char *text) {
    if (text == NULL) {
        return;
    }
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

static uint64_t hash_two_part_uid(const char *prefix, const char *first, const char *second) {
    char seed[384];
    char first_lower[160];
    char second_lower[160];
    build_sort_key(first, first_lower, sizeof(first_lower));
    if (second != NULL) {
        build_sort_key(second, second_lower, sizeof(second_lower));
        snprintf(seed, sizeof(seed), "%s:%s|%s", prefix, first_lower, second_lower);
    } else {
        snprintf(seed, sizeof(seed), "%s:%s", prefix, first_lower);
    }
    return library_fnv1a_64(seed);
}

static bool execute_sql(sqlite3 *handle, const char *sql) {
    return sqlite3_exec(handle, sql, NULL, NULL, NULL) == SQLITE_OK;
}

static bool configure_pragmas(sqlite3 *handle) {
    char pragma_cache[64];
    snprintf(pragma_cache, sizeof(pragma_cache), "PRAGMA cache_size = -%d;",
             LIBRARY_DB_PAGE_CACHE_KIBIBYTES);
    if (!execute_sql(handle, "PRAGMA journal_mode = DELETE;") ||
        !execute_sql(handle, pragma_cache) ||
        !execute_sql(handle, "PRAGMA temp_store = MEMORY;") ||
        !execute_sql(handle, "PRAGMA foreign_keys = ON;")) {
        return false;
    }
    sqlite3_busy_timeout(handle, LIBRARY_DB_BUSY_TIMEOUT_MS);
    return true;
}

static bool verify_quick_check(sqlite3 *handle) {
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(handle, "PRAGMA quick_check(1);", -1, &statement, NULL) != SQLITE_OK) {
        return false;
    }
    bool ok = false;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *result = sqlite3_column_text(statement, 0);
        ok = (result != NULL && strcmp((const char *)result, "ok") == 0);
    }
    sqlite3_finalize(statement);
    return ok;
}

static bool create_schema(sqlite3 *handle) {
    static const char *const SCHEMA_SQL =
        "CREATE TABLE IF NOT EXISTS meta ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS artists ("
        "  id INTEGER PRIMARY KEY,"
        "  artist_uid INTEGER UNIQUE NOT NULL,"
        "  name TEXT NOT NULL,"
        "  name_sort TEXT NOT NULL,"
        "  album_count INTEGER NOT NULL DEFAULT 0,"
        "  track_count INTEGER NOT NULL DEFAULT 0,"
        "  added_at_seconds INTEGER NOT NULL DEFAULT 0,"
        "  is_favorite INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_artists_name ON artists(name_sort);"
        "CREATE INDEX IF NOT EXISTS idx_artists_liked ON artists(is_favorite DESC, name_sort);"
        "CREATE INDEX IF NOT EXISTS idx_artists_recent ON artists(added_at_seconds DESC, name_sort);"
        "CREATE TABLE IF NOT EXISTS albums ("
        "  id INTEGER PRIMARY KEY,"
        "  album_uid INTEGER UNIQUE NOT NULL,"
        "  artist_id INTEGER NOT NULL REFERENCES artists(id),"
        "  title TEXT NOT NULL,"
        "  title_sort TEXT NOT NULL,"
        "  artist_name TEXT NOT NULL,"
        "  genre_name TEXT NOT NULL,"
        "  year INTEGER NOT NULL DEFAULT 0,"
        "  motif_id INTEGER NOT NULL DEFAULT 0,"
        "  track_count INTEGER NOT NULL DEFAULT 0,"
        "  added_at_seconds INTEGER NOT NULL DEFAULT 0,"
        "  is_favorite INTEGER NOT NULL DEFAULT 0,"
        "  cover_source_kind INTEGER NOT NULL DEFAULT 0,"
        "  cover_path TEXT,"
        "  cover_data_offset INTEGER NOT NULL DEFAULT 0,"
        "  cover_data_length INTEGER NOT NULL DEFAULT 0,"
        "  cover_source_mtime INTEGER NOT NULL DEFAULT 0,"
        "  cover_source_size INTEGER NOT NULL DEFAULT 0,"
        "  cover_cache_status INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_albums_title ON albums(title_sort);"
        "CREATE INDEX IF NOT EXISTS idx_albums_artist ON albums(artist_id, title_sort);"
        "CREATE INDEX IF NOT EXISTS idx_albums_liked ON albums(is_favorite DESC, title_sort);"
        "CREATE INDEX IF NOT EXISTS idx_albums_recent ON albums(added_at_seconds DESC, title_sort);"
        "CREATE INDEX IF NOT EXISTS idx_albums_cover_status ON albums(cover_cache_status);"
        "CREATE TABLE IF NOT EXISTS tracks ("
        "  id INTEGER PRIMARY KEY,"
        "  path TEXT UNIQUE NOT NULL,"
        "  audio_fingerprint INTEGER NOT NULL,"
        "  file_size_bytes INTEGER NOT NULL,"
        "  mtime_seconds INTEGER NOT NULL,"
        "  format_kind INTEGER NOT NULL,"
        "  title TEXT NOT NULL,"
        "  title_sort TEXT NOT NULL,"
        "  artist_name TEXT NOT NULL,"
        "  album_id INTEGER NOT NULL REFERENCES albums(id),"
        "  artist_id INTEGER NOT NULL REFERENCES artists(id),"
        "  genre_raw TEXT NOT NULL,"
        "  disc_number INTEGER NOT NULL DEFAULT 1,"
        "  track_number INTEGER NOT NULL DEFAULT 0,"
        "  duration_seconds INTEGER NOT NULL DEFAULT 0,"
        "  added_at_seconds INTEGER NOT NULL DEFAULT 0,"
        "  is_favorite INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_tracks_fp ON tracks(audio_fingerprint);"
        "CREATE INDEX IF NOT EXISTS idx_tracks_stat ON tracks(file_size_bytes, mtime_seconds);"
        "CREATE INDEX IF NOT EXISTS idx_tracks_title ON tracks(title_sort);"
        "CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album_id, disc_number, track_number, title_sort);"
        "CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist_id, title_sort);"
        "CREATE INDEX IF NOT EXISTS idx_tracks_fav ON tracks(is_favorite, title_sort);"
        "CREATE TABLE IF NOT EXISTS genres ("
        "  id INTEGER PRIMARY KEY,"
        "  genre_uid INTEGER UNIQUE NOT NULL,"
        "  name TEXT NOT NULL,"
        "  name_sort TEXT NOT NULL,"
        "  track_count INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_genres_name ON genres(name_sort);"
        "CREATE TABLE IF NOT EXISTS track_genres ("
        "  track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,"
        "  genre_id INTEGER NOT NULL REFERENCES genres(id) ON DELETE CASCADE,"
        "  PRIMARY KEY (track_id, genre_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_track_genres_genre ON track_genres(genre_id, track_id);"
        "CREATE TABLE IF NOT EXISTS playlists ("
        "  id INTEGER PRIMARY KEY,"
        "  playlist_uid INTEGER UNIQUE NOT NULL,"
        "  name TEXT NOT NULL,"
        "  name_sort TEXT NOT NULL,"
        "  source_m3u TEXT,"
        "  is_dismissed INTEGER NOT NULL DEFAULT 0,"
        "  track_count INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_playlists_name ON playlists(is_dismissed, name_sort);"
        "CREATE TABLE IF NOT EXISTS playlist_tracks ("
        "  playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,"
        "  position INTEGER NOT NULL,"
        "  track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,"
        "  PRIMARY KEY (playlist_id, position)"
        ");"
        "CREATE TABLE IF NOT EXISTS m3u_files ("
        "  path TEXT PRIMARY KEY,"
        "  mtime_seconds INTEGER NOT NULL,"
        "  file_size_bytes INTEGER NOT NULL"
        ");";
    if (!execute_sql(handle, SCHEMA_SQL)) {
        return false;
    }
    char version_sql[128];
    snprintf(version_sql, sizeof(version_sql),
             "INSERT OR IGNORE INTO meta(key, value) VALUES('schema_version', '%d');",
             LIBRARY_DB_SCHEMA_VERSION);
    return execute_sql(handle, version_sql);
}

static bool get_meta_value(sqlite3 *handle, const char *key, char *value_out, size_t capacity) {
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(handle, "SELECT value FROM meta WHERE key = ?1;", -1, &statement,
                           NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(statement, 1, key, -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(statement, 0);
        if (text != NULL) {
            copy_text_bounded(value_out, capacity, (const char *)text);
            found = true;
        }
    }
    sqlite3_finalize(statement);
    return found;
}

static bool set_meta_value(sqlite3 *handle, const char *key, const char *value) {
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(handle,
                           "INSERT INTO meta(key, value) VALUES(?1, ?2) "
                           "ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
                           -1, &statement, NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(statement, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, value, -1, SQLITE_STATIC);
    bool ok = (sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);
    return ok;
}

static sqlite3 *open_and_init_handle(const char *database_path) {
    sqlite3 *handle = NULL;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(database_path, &handle, flags, NULL) != SQLITE_OK) {
        if (handle != NULL) {
            sqlite3_close(handle);
        }
        return NULL;
    }
    if (!configure_pragmas(handle) || !verify_quick_check(handle) || !create_schema(handle)) {
        sqlite3_close(handle);
        return NULL;
    }
    return handle;
}

bool library_db_open(struct library_db *database, const char *directory_path) {
    if (database == NULL || directory_path == NULL || directory_path[0] == '\0') {
        return false;
    }
    memset(database, 0, sizeof(*database));

    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&database->mutex, &attributes);
    pthread_mutexattr_destroy(&attributes);

    copy_text_bounded(database->directory_path, sizeof(database->directory_path), directory_path);
    if (mkdir(directory_path, 0755) != 0 && errno != EEXIST) {
        pthread_mutex_destroy(&database->mutex);
        return false;
    }

    char database_path[LIBRARY_DB_PATH_BYTES + 32u];
    snprintf(database_path, sizeof(database_path), "%s/%s", directory_path, LIBRARY_DB_FILE_NAME);
    sqlite3 *handle = open_and_init_handle(database_path);
    if (handle == NULL) {
        /* Corrupt or unreadable DB: quarantine so user never gets stuck on boot. */
        char corrupt_path[LIBRARY_DB_PATH_BYTES + 32u];
        snprintf(corrupt_path, sizeof(corrupt_path), "%s/%s", directory_path,
                 LIBRARY_DB_CORRUPT_FILE_NAME);
        (void)rename(database_path, corrupt_path);
        handle = open_and_init_handle(database_path);
    }
    if (handle == NULL) {
        pthread_mutex_destroy(&database->mutex);
        return false;
    }
    database->handle = handle;
    return true;
}

void library_db_close(struct library_db *database) {
    if (database == NULL) {
        return;
    }
    if (database->handle != NULL) {
        sqlite3_close(database->handle);
        database->handle = NULL;
    }
    pthread_mutex_destroy(&database->mutex);
    memset(database, 0, sizeof(*database));
}

bool library_db_begin(struct library_db *database) {
    if (database == NULL || database->handle == NULL) {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    bool ok = execute_sql(database->handle, "BEGIN IMMEDIATE;");
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

bool library_db_commit(struct library_db *database) {
    if (database == NULL || database->handle == NULL) {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    bool ok = execute_sql(database->handle, "COMMIT;");
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

void library_db_rollback(struct library_db *database) {
    if (database == NULL || database->handle == NULL) {
        return;
    }
    pthread_mutex_lock(&database->mutex);
    (void)execute_sql(database->handle, "ROLLBACK;");
    pthread_mutex_unlock(&database->mutex);
}

static bool find_or_create_artist(sqlite3 *handle, const char *artist_name, int64_t *artist_id_out) {
    uint64_t artist_uid = hash_two_part_uid("artist", artist_name, NULL);
    char name_sort[SORT_KEY_BYTES];
    build_sort_key(artist_name, name_sort, sizeof(name_sort));

    sqlite3_stmt *select_stmt = NULL;
    if (sqlite3_prepare_v2(handle, "SELECT id FROM artists WHERE artist_uid = ?1;", -1,
                           &select_stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(select_stmt, 1, (sqlite3_int64)artist_uid);
    if (sqlite3_step(select_stmt) == SQLITE_ROW) {
        *artist_id_out = sqlite3_column_int64(select_stmt, 0);
        sqlite3_finalize(select_stmt);
        return true;
    }
    sqlite3_finalize(select_stmt);

    sqlite3_stmt *insert_stmt = NULL;
    if (sqlite3_prepare_v2(handle,
                           "INSERT INTO artists(artist_uid, name, name_sort) VALUES(?1, ?2, ?3);",
                           -1, &insert_stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(insert_stmt, 1, (sqlite3_int64)artist_uid);
    sqlite3_bind_text(insert_stmt, 2, artist_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(insert_stmt, 3, name_sort, -1, SQLITE_STATIC);
    bool ok = (sqlite3_step(insert_stmt) == SQLITE_DONE);
    sqlite3_finalize(insert_stmt);
    if (ok) {
        *artist_id_out = sqlite3_last_insert_rowid(handle);
    }
    return ok;
}

static bool find_or_create_album(sqlite3 *handle, const struct library_db_track_input *input,
                                 const char *album_title, const char *album_artist,
                                 int64_t artist_id, const char *primary_genre,
                                 int64_t *album_id_out) {
    uint64_t album_uid = hash_two_part_uid("album", album_artist, album_title);
    char title_sort[SORT_KEY_BYTES];
    build_sort_key(album_title, title_sort, sizeof(title_sort));

    sqlite3_stmt *select_stmt = NULL;
    if (sqlite3_prepare_v2(
            handle,
            "SELECT id, year, cover_source_kind, COALESCE(cover_path, ''), cover_data_offset, "
            "cover_data_length, cover_source_mtime, cover_source_size, cover_cache_status "
            "FROM albums WHERE album_uid = ?1;",
            -1, &select_stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(select_stmt, 1, (sqlite3_int64)album_uid);
    if (sqlite3_step(select_stmt) == SQLITE_ROW) {
        int64_t album_id = sqlite3_column_int64(select_stmt, 0);
        uint16_t current_year = (uint16_t)sqlite3_column_int(select_stmt, 1);
        uint8_t current_kind = (uint8_t)sqlite3_column_int(select_stmt, 2);
        const char *current_path = (const char *)sqlite3_column_text(select_stmt, 3);
        uint32_t current_offset = (uint32_t)sqlite3_column_int64(select_stmt, 4);
        uint32_t current_length = (uint32_t)sqlite3_column_int64(select_stmt, 5);
        int64_t current_mtime = sqlite3_column_int64(select_stmt, 6);
        uint64_t current_size = (uint64_t)sqlite3_column_int64(select_stmt, 7);

        bool should_update_cover = false;
        if (input->cover_source_kind != LIBRARY_COVER_NONE && input->cover_path != NULL &&
            (current_kind == LIBRARY_COVER_NONE ||
             (input->cover_source_kind == LIBRARY_COVER_SIDECAR &&
              current_kind != LIBRARY_COVER_SIDECAR))) {
            should_update_cover = true;
        } else if (input->cover_source_kind != LIBRARY_COVER_NONE &&
                   input->cover_source_kind == current_kind && input->cover_path != NULL &&
                   current_path != NULL && strcmp(current_path, input->cover_path) == 0 &&
                   (current_offset != input->cover_data_offset ||
                    current_length != input->cover_data_length ||
                    current_size != input->cover_source_size ||
                    llabs(current_mtime - input->cover_source_mtime) >
                        FAT32_MTIME_TOLERANCE_SECONDS)) {
            should_update_cover = true;
        }
        bool update_year = (current_year == 0u && input->year != 0u);
        sqlite3_finalize(select_stmt);

        if (should_update_cover) {
            sqlite3_stmt *update_stmt = NULL;
            if (sqlite3_prepare_v2(
                    handle,
                    "UPDATE albums SET cover_source_kind = ?1, cover_path = ?2, "
                    "cover_data_offset = ?3, cover_data_length = ?4, cover_source_mtime = ?5, "
                    "cover_source_size = ?6, cover_cache_status = ?7 WHERE id = ?8;",
                    -1, &update_stmt, NULL) != SQLITE_OK) {
                return false;
            }
            sqlite3_bind_int(update_stmt, 1, input->cover_source_kind);
            sqlite3_bind_text(update_stmt, 2, input->cover_path, -1, SQLITE_STATIC);
            sqlite3_bind_int64(update_stmt, 3, (sqlite3_int64)input->cover_data_offset);
            sqlite3_bind_int64(update_stmt, 4, (sqlite3_int64)input->cover_data_length);
            sqlite3_bind_int64(update_stmt, 5, input->cover_source_mtime);
            sqlite3_bind_int64(update_stmt, 6, (sqlite3_int64)input->cover_source_size);
            sqlite3_bind_int(update_stmt, 7, LIBRARY_DB_COVER_PENDING);
            sqlite3_bind_int64(update_stmt, 8, album_id);
            (void)sqlite3_step(update_stmt);
            sqlite3_finalize(update_stmt);
        }
        if (update_year) {
            sqlite3_stmt *year_stmt = NULL;
            if (sqlite3_prepare_v2(handle, "UPDATE albums SET year = ?1 WHERE id = ?2;", -1,
                                   &year_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(year_stmt, 1, input->year);
                sqlite3_bind_int64(year_stmt, 2, album_id);
                (void)sqlite3_step(year_stmt);
                sqlite3_finalize(year_stmt);
            }
        }
        *album_id_out = album_id;
        return true;
    }
    sqlite3_finalize(select_stmt);

    enum library_db_cover_status initial_status =
        (input->cover_source_kind != LIBRARY_COVER_NONE && input->cover_path != NULL)
            ? LIBRARY_DB_COVER_PENDING
            : LIBRARY_DB_COVER_NONE;

    sqlite3_stmt *insert_stmt = NULL;
    if (sqlite3_prepare_v2(
            handle,
            "INSERT INTO albums(album_uid, artist_id, title, title_sort, artist_name, "
            "genre_name, year, cover_source_kind, cover_path, cover_data_offset, "
            "cover_data_length, cover_source_mtime, cover_source_size, cover_cache_status) "
            "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14);",
            -1, &insert_stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(insert_stmt, 1, (sqlite3_int64)album_uid);
    sqlite3_bind_int64(insert_stmt, 2, artist_id);
    sqlite3_bind_text(insert_stmt, 3, album_title, -1, SQLITE_STATIC);
    sqlite3_bind_text(insert_stmt, 4, title_sort, -1, SQLITE_STATIC);
    sqlite3_bind_text(insert_stmt, 5, album_artist, -1, SQLITE_STATIC);
    sqlite3_bind_text(insert_stmt, 6, primary_genre, -1, SQLITE_STATIC);
    sqlite3_bind_int(insert_stmt, 7, input->year);
    sqlite3_bind_int(insert_stmt, 8, input->cover_source_kind);
    if (input->cover_source_kind != LIBRARY_COVER_NONE && input->cover_path != NULL) {
        sqlite3_bind_text(insert_stmt, 9, input->cover_path, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(insert_stmt, 9);
    }
    sqlite3_bind_int64(insert_stmt, 10, (sqlite3_int64)input->cover_data_offset);
    sqlite3_bind_int64(insert_stmt, 11, (sqlite3_int64)input->cover_data_length);
    sqlite3_bind_int64(insert_stmt, 12, input->cover_source_mtime);
    sqlite3_bind_int64(insert_stmt, 13, (sqlite3_int64)input->cover_source_size);
    sqlite3_bind_int(insert_stmt, 14, initial_status);
    bool ok = (sqlite3_step(insert_stmt) == SQLITE_DONE);
    sqlite3_finalize(insert_stmt);
    if (ok) {
        *album_id_out = sqlite3_last_insert_rowid(handle);
    }
    return ok;
}

static bool link_track_genres(sqlite3 *handle, int64_t track_id, const char *genre_raw) {
    sqlite3_stmt *clear_stmt = NULL;
    if (sqlite3_prepare_v2(handle, "DELETE FROM track_genres WHERE track_id = ?1;", -1,
                           &clear_stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(clear_stmt, 1, track_id);
    (void)sqlite3_step(clear_stmt);
    sqlite3_finalize(clear_stmt);

    char raw_copy[192];
    copy_text_bounded(raw_copy, sizeof(raw_copy), genre_raw);
    char *cursor = raw_copy;
    uint32_t linked = 0u;
    while (*cursor != '\0' && linked < LIBRARY_DB_MAX_GENRES_PER_TRACK) {
        char *separator = cursor;
        while (*separator != '\0' && *separator != ';' && *separator != ',' &&
               *separator != '/' && *separator != '|') {
            separator++;
        }
        char saved = *separator;
        *separator = '\0';
        char token[GENRE_TOKEN_BYTES];
        copy_text_bounded(token, sizeof(token), cursor);
        trim_whitespace(token);

        if (token[0] != '\0') {
            uint64_t genre_uid = hash_two_part_uid("genre", token, NULL);
            char name_sort[SORT_KEY_BYTES];
            build_sort_key(token, name_sort, sizeof(name_sort));

            sqlite3_stmt *upsert_genre = NULL;
            if (sqlite3_prepare_v2(
                    handle,
                    "INSERT OR IGNORE INTO genres(genre_uid, name, name_sort) VALUES(?1, ?2, ?3);",
                    -1, &upsert_genre, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(upsert_genre, 1, (sqlite3_int64)genre_uid);
                sqlite3_bind_text(upsert_genre, 2, token, -1, SQLITE_STATIC);
                sqlite3_bind_text(upsert_genre, 3, name_sort, -1, SQLITE_STATIC);
                (void)sqlite3_step(upsert_genre);
                sqlite3_finalize(upsert_genre);
            }

            sqlite3_stmt *link_stmt = NULL;
            if (sqlite3_prepare_v2(
                    handle,
                    "INSERT OR IGNORE INTO track_genres(track_id, genre_id) "
                    "SELECT ?1, id FROM genres WHERE genre_uid = ?2;",
                    -1, &link_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(link_stmt, 1, track_id);
                sqlite3_bind_int64(link_stmt, 2, (sqlite3_int64)genre_uid);
                (void)sqlite3_step(link_stmt);
                sqlite3_finalize(link_stmt);
            }
            linked++;
        }

        if (saved == '\0') {
            break;
        }
        cursor = separator + 1;
    }
    return true;
}

static void extract_primary_genre(const char *genre_raw, char *primary_out, size_t capacity) {
    copy_text_bounded(primary_out, capacity, genre_raw);
    for (size_t index = 0u; primary_out[index] != '\0'; index++) {
        char character = primary_out[index];
        if (character == ';' || character == ',' || character == '/' || character == '|') {
            primary_out[index] = '\0';
            break;
        }
    }
    trim_whitespace(primary_out);
    if (primary_out[0] == '\0') {
        copy_text_bounded(primary_out, capacity, "Unknown Genre");
    }
}

bool library_db_upsert_track(struct library_db *database,
                             const struct library_db_track_input *input,
                             int64_t existing_track_id, int64_t *track_id_out) {
    if (database == NULL || database->handle == NULL || input == NULL || input->path == NULL ||
        input->path[0] == '\0') {
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
    const char *genre_raw =
        (input->genre != NULL && input->genre[0] != '\0') ? input->genre : "Unknown Genre";

    char primary_genre[GENRE_TOKEN_BYTES];
    extract_primary_genre(genre_raw, primary_genre, sizeof(primary_genre));
    char title_sort[SORT_KEY_BYTES];
    build_sort_key(title, title_sort, sizeof(title_sort));

    pthread_mutex_lock(&database->mutex);

    int64_t artist_id = 0;
    int64_t album_id = 0;
    if (!find_or_create_artist(database->handle, album_artist, &artist_id) ||
        !find_or_create_album(database->handle, input, album, album_artist, artist_id,
                              primary_genre, &album_id)) {
        pthread_mutex_unlock(&database->mutex);
        return false;
    }

    int64_t target_id = existing_track_id;
    if (target_id <= 0) {
        sqlite3_stmt *lookup_stmt = NULL;
        if (sqlite3_prepare_v2(database->handle, "SELECT id FROM tracks WHERE path = ?1;", -1,
                               &lookup_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(lookup_stmt, 1, input->path, -1, SQLITE_STATIC);
            if (sqlite3_step(lookup_stmt) == SQLITE_ROW) {
                target_id = sqlite3_column_int64(lookup_stmt, 0);
            }
            sqlite3_finalize(lookup_stmt);
        }
    }

    uint16_t disc_number = input->disc_number > 0u ? input->disc_number : 1u;
    bool ok = false;
    if (target_id > 0) {
        sqlite3_stmt *update_stmt = NULL;
        if (sqlite3_prepare_v2(
                database->handle,
                "UPDATE tracks SET path = ?1, audio_fingerprint = ?2, file_size_bytes = ?3, "
                "mtime_seconds = ?4, format_kind = ?5, title = ?6, title_sort = ?7, "
                "artist_name = ?8, album_id = ?9, artist_id = ?10, genre_raw = ?11, "
                "disc_number = ?12, track_number = ?13, duration_seconds = ?14 WHERE id = ?15;",
                -1, &update_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(update_stmt, 1, input->path, -1, SQLITE_STATIC);
            sqlite3_bind_int64(update_stmt, 2, (sqlite3_int64)input->audio_fingerprint);
            sqlite3_bind_int64(update_stmt, 3, (sqlite3_int64)input->file_size_bytes);
            sqlite3_bind_int64(update_stmt, 4, input->mtime_seconds);
            sqlite3_bind_int(update_stmt, 5, input->format_kind);
            sqlite3_bind_text(update_stmt, 6, title, -1, SQLITE_STATIC);
            sqlite3_bind_text(update_stmt, 7, title_sort, -1, SQLITE_STATIC);
            sqlite3_bind_text(update_stmt, 8, artist, -1, SQLITE_STATIC);
            sqlite3_bind_int64(update_stmt, 9, album_id);
            sqlite3_bind_int64(update_stmt, 10, artist_id);
            sqlite3_bind_text(update_stmt, 11, genre_raw, -1, SQLITE_STATIC);
            sqlite3_bind_int(update_stmt, 12, disc_number);
            sqlite3_bind_int(update_stmt, 13, input->track_number);
            sqlite3_bind_int64(update_stmt, 14, (sqlite3_int64)input->duration_seconds);
            sqlite3_bind_int64(update_stmt, 15, target_id);
            ok = (sqlite3_step(update_stmt) == SQLITE_DONE);
            sqlite3_finalize(update_stmt);
        }
    } else {
        sqlite3_stmt *insert_stmt = NULL;
        if (sqlite3_prepare_v2(
                database->handle,
                "INSERT INTO tracks(path, audio_fingerprint, file_size_bytes, mtime_seconds, "
                "format_kind, title, title_sort, artist_name, album_id, artist_id, genre_raw, "
                "disc_number, track_number, duration_seconds, added_at_seconds) "
                "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15);",
                -1, &insert_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(insert_stmt, 1, input->path, -1, SQLITE_STATIC);
            sqlite3_bind_int64(insert_stmt, 2, (sqlite3_int64)input->audio_fingerprint);
            sqlite3_bind_int64(insert_stmt, 3, (sqlite3_int64)input->file_size_bytes);
            sqlite3_bind_int64(insert_stmt, 4, input->mtime_seconds);
            sqlite3_bind_int(insert_stmt, 5, input->format_kind);
            sqlite3_bind_text(insert_stmt, 6, title, -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 7, title_sort, -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 8, artist, -1, SQLITE_STATIC);
            sqlite3_bind_int64(insert_stmt, 9, album_id);
            sqlite3_bind_int64(insert_stmt, 10, artist_id);
            sqlite3_bind_text(insert_stmt, 11, genre_raw, -1, SQLITE_STATIC);
            sqlite3_bind_int(insert_stmt, 12, disc_number);
            sqlite3_bind_int(insert_stmt, 13, input->track_number);
            sqlite3_bind_int64(insert_stmt, 14, (sqlite3_int64)input->duration_seconds);
            sqlite3_bind_int64(insert_stmt, 15, input->added_at_seconds);
            ok = (sqlite3_step(insert_stmt) == SQLITE_DONE);
            sqlite3_finalize(insert_stmt);
            if (ok) {
                target_id = sqlite3_last_insert_rowid(database->handle);
            }
        }
    }

    if (ok) {
        ok = link_track_genres(database->handle, target_id, genre_raw);
    }
    if (ok && track_id_out != NULL) {
        *track_id_out = target_id;
    }

    pthread_mutex_unlock(&database->mutex);
    return ok;
}

bool library_db_update_track_location(struct library_db *database, int64_t track_id,
                                      const char *new_path, int64_t mtime_seconds) {
    if (database == NULL || database->handle == NULL || track_id <= 0 || new_path == NULL ||
        new_path[0] == '\0') {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    bool ok = false;
    if (sqlite3_prepare_v2(database->handle,
                           "UPDATE tracks SET path = ?1, mtime_seconds = ?2 WHERE id = ?3;", -1,
                           &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_text(statement, 1, new_path, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 2, mtime_seconds);
        sqlite3_bind_int64(statement, 3, track_id);
        ok = (sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database->handle) > 0);
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

bool library_db_delete_track(struct library_db *database, int64_t track_id) {
    if (database == NULL || database->handle == NULL || track_id <= 0) {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    bool deleted = false;
    if (sqlite3_prepare_v2(database->handle, "DELETE FROM tracks WHERE id = ?1;", -1, &statement,
                           NULL) == SQLITE_OK) {
        sqlite3_bind_int64(statement, 1, track_id);
        deleted = (sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database->handle) > 0);
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return deleted;
}

bool library_db_get_track_path(struct library_db *database, int64_t track_id, char *path_out,
                               size_t capacity) {
    if (database == NULL || database->handle == NULL || track_id <= 0 || path_out == NULL ||
        capacity == 0u) {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    bool found = false;
    if (sqlite3_prepare_v2(database->handle, "SELECT path FROM tracks WHERE id = ?1;", -1,
                           &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(statement, 1, track_id);
        if (sqlite3_step(statement) == SQLITE_ROW) {
            const unsigned char *path_text = sqlite3_column_text(statement, 0);
            if (path_text != NULL) {
                copy_text_bounded(path_out, capacity, (const char *)path_text);
                found = true;
            }
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return found;
}

bool library_db_find_track_by_path(struct library_db *database, const char *path,
                                   int64_t *track_id_out, int64_t *album_id_out,
                                   uint64_t *album_uid_out) {
    if (database == NULL || database->handle == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    bool found = false;
    if (sqlite3_prepare_v2(
            database->handle,
            "SELECT t.id, t.album_id, a.album_uid "
            "FROM tracks t JOIN albums a ON a.id = t.album_id WHERE t.path = ?1;",
            -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_text(statement, 1, path, -1, SQLITE_STATIC);
        if (sqlite3_step(statement) == SQLITE_ROW) {
            if (track_id_out != NULL) {
                *track_id_out = sqlite3_column_int64(statement, 0);
            }
            if (album_id_out != NULL) {
                *album_id_out = sqlite3_column_int64(statement, 1);
            }
            if (album_uid_out != NULL) {
                *album_uid_out = (uint64_t)sqlite3_column_int64(statement, 2);
            }
            found = true;
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return found;
}

bool library_db_recompute_aggregates(struct library_db *database, uint64_t *deleted_album_uids,
                                     uint32_t capacity, uint32_t *deleted_count_out) {
    if (database == NULL || database->handle == NULL) {
        return false;
    }
    if (deleted_count_out != NULL) {
        *deleted_count_out = 0u;
    }

    pthread_mutex_lock(&database->mutex);

    if (deleted_album_uids != NULL && capacity > 0u && deleted_count_out != NULL) {
        sqlite3_stmt *deleted_stmt = NULL;
        if (sqlite3_prepare_v2(
                database->handle,
                "SELECT album_uid FROM albums "
                "WHERE NOT EXISTS (SELECT 1 FROM tracks WHERE tracks.album_id = albums.id);",
                -1, &deleted_stmt, NULL) == SQLITE_OK) {
            uint32_t count = 0u;
            while (count < capacity && sqlite3_step(deleted_stmt) == SQLITE_ROW) {
                deleted_album_uids[count++] = (uint64_t)sqlite3_column_int64(deleted_stmt, 0);
            }
            *deleted_count_out = count;
            sqlite3_finalize(deleted_stmt);
        }
    }

    bool ok =
        execute_sql(database->handle,
                    "DELETE FROM albums WHERE NOT EXISTS "
                    "(SELECT 1 FROM tracks WHERE tracks.album_id = albums.id);") &&
        execute_sql(database->handle,
                    "DELETE FROM artists WHERE NOT EXISTS "
                    "(SELECT 1 FROM albums WHERE albums.artist_id = artists.id) "
                    "AND NOT EXISTS (SELECT 1 FROM tracks WHERE tracks.artist_id = artists.id);") &&
        execute_sql(database->handle,
                    "DELETE FROM genres WHERE NOT EXISTS "
                    "(SELECT 1 FROM track_genres WHERE track_genres.genre_id = genres.id);") &&
        execute_sql(database->handle,
                    "UPDATE albums SET "
                    "  track_count = (SELECT COUNT(*) FROM tracks WHERE tracks.album_id = albums.id),"
                    "  added_at_seconds = COALESCE((SELECT MAX(added_at_seconds) FROM tracks "
                    "                              WHERE tracks.album_id = albums.id), 0);") &&
        execute_sql(database->handle,
                    "UPDATE artists SET "
                    "  album_count = (SELECT COUNT(*) FROM albums WHERE albums.artist_id = artists.id),"
                    "  track_count = (SELECT COUNT(*) FROM tracks WHERE tracks.artist_id = artists.id),"
                    "  added_at_seconds = COALESCE((SELECT MAX(added_at_seconds) FROM albums "
                    "                              WHERE albums.artist_id = artists.id), 0);") &&
        execute_sql(database->handle,
                    "UPDATE genres SET "
                    "  track_count = (SELECT COUNT(*) FROM track_genres "
                    "                 WHERE track_genres.genre_id = genres.id);") &&
        execute_sql(database->handle,
                    "UPDATE playlists SET "
                    "  track_count = (SELECT COUNT(*) FROM playlist_tracks "
                    "                 WHERE playlist_tracks.playlist_id = playlists.id);");

    if (ok) {
        sqlite3_stmt *motif_stmt = NULL;
        if (sqlite3_prepare_v2(
                database->handle,
                "WITH ordered AS ("
                "  SELECT id, (ROW_NUMBER() OVER (ORDER BY title_sort ASC, id ASC) - 1) % ?1 AS motif "
                "  FROM albums"
                ") "
                "UPDATE albums SET motif_id = (SELECT motif FROM ordered WHERE ordered.id = albums.id);",
                -1, &motif_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(motif_stmt, 1, LIBRARY_COVER_MOTIF_COUNT);
            ok = (sqlite3_step(motif_stmt) == SQLITE_DONE);
            sqlite3_finalize(motif_stmt);
        } else {
            ok = false;
        }
    }

    pthread_mutex_unlock(&database->mutex);
    return ok;
}

void library_db_for_each_track_stamp(struct library_db *database,
                                     library_db_track_stamp_callback callback,
                                     void *callback_context) {
    if (database == NULL || database->handle == NULL || callback == NULL) {
        return;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(
            database->handle,
            "SELECT id, path, file_size_bytes, mtime_seconds, audio_fingerprint FROM tracks;",
            -1, &statement, NULL) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            int64_t track_id = sqlite3_column_int64(statement, 0);
            const char *path = (const char *)sqlite3_column_text(statement, 1);
            uint64_t file_size_bytes = (uint64_t)sqlite3_column_int64(statement, 2);
            int64_t mtime_seconds = sqlite3_column_int64(statement, 3);
            uint64_t audio_fingerprint = (uint64_t)sqlite3_column_int64(statement, 4);
            if (path != NULL) {
                callback(callback_context, track_id, path, file_size_bytes, mtime_seconds,
                         audio_fingerprint);
            }
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
}

static uint32_t query_single_count(sqlite3 *handle, const char *sql) {
    sqlite3_stmt *statement = NULL;
    uint32_t count = 0u;
    if (sqlite3_prepare_v2(handle, sql, -1, &statement, NULL) == SQLITE_OK) {
        if (sqlite3_step(statement) == SQLITE_ROW) {
            count = (uint32_t)sqlite3_column_int64(statement, 0);
        }
        sqlite3_finalize(statement);
    }
    return count;
}

bool library_db_read_counts(struct library_db *database, struct library_db_counts *counts_out) {
    if (database == NULL || database->handle == NULL || counts_out == NULL) {
        return false;
    }
    memset(counts_out, 0, sizeof(*counts_out));
    pthread_mutex_lock(&database->mutex);
    counts_out->tracks = query_single_count(database->handle, "SELECT COUNT(*) FROM tracks;");
    counts_out->albums = query_single_count(database->handle, "SELECT COUNT(*) FROM albums;");
    counts_out->artists = query_single_count(database->handle, "SELECT COUNT(*) FROM artists;");
    counts_out->genres = query_single_count(database->handle, "SELECT COUNT(*) FROM genres;");
    counts_out->playlists = query_single_count(
        database->handle, "SELECT COUNT(*) FROM playlists WHERE is_dismissed = 0;");
    counts_out->favorite_tracks = query_single_count(
        database->handle, "SELECT COUNT(*) FROM tracks WHERE is_favorite = 1;");
    counts_out->favorite_albums = query_single_count(
        database->handle, "SELECT COUNT(*) FROM albums WHERE is_favorite = 1;");
    counts_out->favorite_artists = query_single_count(
        database->handle, "SELECT COUNT(*) FROM artists WHERE is_favorite = 1;");
    pthread_mutex_unlock(&database->mutex);
    return true;
}

bool library_db_toggle_favorite(struct library_db *database, enum library_db_favorite_kind kind,
                                int64_t row_id, bool *is_favorite_out) {
    if (database == NULL || database->handle == NULL || row_id <= 0) {
        return false;
    }
    const char *table_name = NULL;
    if (kind == LIBRARY_DB_FAVORITE_TRACK) {
        table_name = "tracks";
    } else if (kind == LIBRARY_DB_FAVORITE_ALBUM) {
        table_name = "albums";
    } else if (kind == LIBRARY_DB_FAVORITE_ARTIST) {
        table_name = "artists";
    } else {
        return false;
    }

    pthread_mutex_lock(&database->mutex);
    char update_sql[128];
    snprintf(update_sql, sizeof(update_sql),
             "UPDATE %s SET is_favorite = 1 - is_favorite WHERE id = ?1;", table_name);
    sqlite3_stmt *statement = NULL;
    bool ok = false;
    if (sqlite3_prepare_v2(database->handle, update_sql, -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(statement, 1, row_id);
        ok = (sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database->handle) > 0);
        sqlite3_finalize(statement);
    }
    if (ok && is_favorite_out != NULL) {
        char select_sql[96];
        snprintf(select_sql, sizeof(select_sql), "SELECT is_favorite FROM %s WHERE id = ?1;",
                 table_name);
        sqlite3_stmt *select_stmt = NULL;
        if (sqlite3_prepare_v2(database->handle, select_sql, -1, &select_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(select_stmt, 1, row_id);
            if (sqlite3_step(select_stmt) == SQLITE_ROW) {
                *is_favorite_out = (sqlite3_column_int(select_stmt, 0) != 0);
            }
            sqlite3_finalize(select_stmt);
        }
    }
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

bool library_db_set_sort_mode(struct library_db *database, enum library_db_sort_scope scope,
                              enum library_sort_mode sort_mode) {
    if (database == NULL || database->handle == NULL || sort_mode >= LIBRARY_SORT_MODE_COUNT) {
        return false;
    }
    const char *key =
        (scope == LIBRARY_DB_SORT_SCOPE_ARTISTS) ? "artist_sort_mode" : "album_sort_mode";
    char value[16];
    snprintf(value, sizeof(value), "%d", (int)sort_mode);
    pthread_mutex_lock(&database->mutex);
    bool ok = set_meta_value(database->handle, key, value);
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

enum library_sort_mode library_db_get_sort_mode(struct library_db *database,
                                                enum library_db_sort_scope scope) {
    if (database == NULL || database->handle == NULL) {
        return LIBRARY_SORT_ALPHABETICAL;
    }
    const char *key =
        (scope == LIBRARY_DB_SORT_SCOPE_ARTISTS) ? "artist_sort_mode" : "album_sort_mode";
    char value[16];
    pthread_mutex_lock(&database->mutex);
    bool found = get_meta_value(database->handle, key, value, sizeof(value));
    pthread_mutex_unlock(&database->mutex);
    if (!found) {
        return LIBRARY_SORT_ALPHABETICAL;
    }
    int parsed = atoi(value);
    if (parsed < 0 || parsed >= (int)LIBRARY_SORT_MODE_COUNT) {
        return LIBRARY_SORT_ALPHABETICAL;
    }
    return (enum library_sort_mode)parsed;
}

bool library_db_upsert_m3u_playlist(struct library_db *database, const char *name,
                                    const char *source_m3u_path, const char *const *track_paths,
                                    uint32_t track_count) {
    if (database == NULL || database->handle == NULL || name == NULL || name[0] == '\0' ||
        source_m3u_path == NULL || source_m3u_path[0] == '\0') {
        return false;
    }

    uint64_t playlist_uid = hash_two_part_uid("playlist:m3u", source_m3u_path, NULL);
    char name_sort[SORT_KEY_BYTES];
    build_sort_key(name, name_sort, sizeof(name_sort));

    pthread_mutex_lock(&database->mutex);

    int64_t playlist_id = 0;
    int is_dismissed = 0;
    sqlite3_stmt *select_stmt = NULL;
    if (sqlite3_prepare_v2(database->handle,
                           "SELECT id, is_dismissed FROM playlists WHERE playlist_uid = ?1;", -1,
                           &select_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(select_stmt, 1, (sqlite3_int64)playlist_uid);
        if (sqlite3_step(select_stmt) == SQLITE_ROW) {
            playlist_id = sqlite3_column_int64(select_stmt, 0);
            is_dismissed = sqlite3_column_int(select_stmt, 1);
        }
        sqlite3_finalize(select_stmt);
    }

    if (is_dismissed != 0) {
        pthread_mutex_unlock(&database->mutex);
        return true;
    }

    if (playlist_id == 0) {
        sqlite3_stmt *insert_stmt = NULL;
        if (sqlite3_prepare_v2(database->handle,
                               "INSERT INTO playlists(playlist_uid, name, name_sort, source_m3u) "
                               "VALUES(?1, ?2, ?3, ?4);",
                               -1, &insert_stmt, NULL) != SQLITE_OK) {
            pthread_mutex_unlock(&database->mutex);
            return false;
        }
        sqlite3_bind_int64(insert_stmt, 1, (sqlite3_int64)playlist_uid);
        sqlite3_bind_text(insert_stmt, 2, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 3, name_sort, -1, SQLITE_STATIC);
        sqlite3_bind_text(insert_stmt, 4, source_m3u_path, -1, SQLITE_STATIC);
        bool inserted = (sqlite3_step(insert_stmt) == SQLITE_DONE);
        sqlite3_finalize(insert_stmt);
        if (!inserted) {
            pthread_mutex_unlock(&database->mutex);
            return false;
        }
        playlist_id = sqlite3_last_insert_rowid(database->handle);
    } else {
        sqlite3_stmt *clear_stmt = NULL;
        if (sqlite3_prepare_v2(database->handle,
                               "DELETE FROM playlist_tracks WHERE playlist_id = ?1;", -1,
                               &clear_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(clear_stmt, 1, playlist_id);
            (void)sqlite3_step(clear_stmt);
            sqlite3_finalize(clear_stmt);
        }
    }

    uint32_t position = 0u;
    if (track_paths != NULL && track_count > 0u) {
        sqlite3_stmt *insert_track_stmt = NULL;
        if (sqlite3_prepare_v2(
                database->handle,
                "INSERT INTO playlist_tracks(playlist_id, position, track_id) "
                "SELECT ?1, ?2, id FROM tracks WHERE path = ?3;",
                -1, &insert_track_stmt, NULL) == SQLITE_OK) {
            for (uint32_t index = 0u; index < track_count; index++) {
                if (track_paths[index] == NULL) {
                    continue;
                }
                sqlite3_reset(insert_track_stmt);
                sqlite3_bind_int64(insert_track_stmt, 1, playlist_id);
                sqlite3_bind_int64(insert_track_stmt, 2, (sqlite3_int64)position);
                sqlite3_bind_text(insert_track_stmt, 3, track_paths[index], -1, SQLITE_STATIC);
                if (sqlite3_step(insert_track_stmt) == SQLITE_DONE &&
                    sqlite3_changes(database->handle) > 0) {
                    position++;
                }
            }
            sqlite3_finalize(insert_track_stmt);
        }
    }

    sqlite3_stmt *count_stmt = NULL;
    if (sqlite3_prepare_v2(database->handle,
                           "UPDATE playlists SET track_count = ?1 WHERE id = ?2;", -1,
                           &count_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(count_stmt, 1, (sqlite3_int64)position);
        sqlite3_bind_int64(count_stmt, 2, playlist_id);
        (void)sqlite3_step(count_stmt);
        sqlite3_finalize(count_stmt);
    }

    pthread_mutex_unlock(&database->mutex);
    return true;
}

bool library_db_delete_playlist(struct library_db *database, int64_t playlist_id) {
    if (database == NULL || database->handle == NULL || playlist_id <= 0) {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *select_stmt = NULL;
    bool has_source_m3u = false;
    bool found = false;
    if (sqlite3_prepare_v2(database->handle,
                           "SELECT source_m3u FROM playlists WHERE id = ?1 AND is_dismissed = 0;",
                           -1, &select_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(select_stmt, 1, playlist_id);
        if (sqlite3_step(select_stmt) == SQLITE_ROW) {
            found = true;
            has_source_m3u = (sqlite3_column_type(select_stmt, 0) != SQLITE_NULL);
        }
        sqlite3_finalize(select_stmt);
    }
    if (!found) {
        pthread_mutex_unlock(&database->mutex);
        return false;
    }

    bool ok = false;
    if (has_source_m3u) {
        sqlite3_stmt *dismiss_stmt = NULL;
        if (sqlite3_prepare_v2(
                database->handle,
                "UPDATE playlists SET is_dismissed = 1, track_count = 0 WHERE id = ?1;", -1,
                &dismiss_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(dismiss_stmt, 1, playlist_id);
            ok = (sqlite3_step(dismiss_stmt) == SQLITE_DONE);
            sqlite3_finalize(dismiss_stmt);
        }
        sqlite3_stmt *clear_stmt = NULL;
        if (ok && sqlite3_prepare_v2(database->handle,
                                     "DELETE FROM playlist_tracks WHERE playlist_id = ?1;", -1,
                                     &clear_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(clear_stmt, 1, playlist_id);
            (void)sqlite3_step(clear_stmt);
            sqlite3_finalize(clear_stmt);
        }
    } else {
        sqlite3_stmt *delete_stmt = NULL;
        if (sqlite3_prepare_v2(database->handle, "DELETE FROM playlists WHERE id = ?1;", -1,
                               &delete_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(delete_stmt, 1, playlist_id);
            ok = (sqlite3_step(delete_stmt) == SQLITE_DONE);
            sqlite3_finalize(delete_stmt);
        }
    }
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

bool library_db_put_m3u_file(struct library_db *database, const char *path,
                             uint64_t file_size_bytes, int64_t mtime_seconds) {
    if (database == NULL || database->handle == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    bool ok = false;
    if (sqlite3_prepare_v2(
            database->handle,
            "INSERT INTO m3u_files(path, mtime_seconds, file_size_bytes) VALUES(?1, ?2, ?3) "
            "ON CONFLICT(path) DO UPDATE SET mtime_seconds = excluded.mtime_seconds, "
            "file_size_bytes = excluded.file_size_bytes;",
            -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_text(statement, 1, path, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 2, mtime_seconds);
        sqlite3_bind_int64(statement, 3, (sqlite3_int64)file_size_bytes);
        ok = (sqlite3_step(statement) == SQLITE_DONE);
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

bool library_db_remove_m3u_file(struct library_db *database, const char *path) {
    if (database == NULL || database->handle == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *file_stmt = NULL;
    if (sqlite3_prepare_v2(database->handle, "DELETE FROM m3u_files WHERE path = ?1;", -1,
                           &file_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(file_stmt, 1, path, -1, SQLITE_STATIC);
        (void)sqlite3_step(file_stmt);
        sqlite3_finalize(file_stmt);
    }
    sqlite3_stmt *playlist_stmt = NULL;
    bool ok = false;
    if (sqlite3_prepare_v2(database->handle, "DELETE FROM playlists WHERE source_m3u = ?1;", -1,
                           &playlist_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(playlist_stmt, 1, path, -1, SQLITE_STATIC);
        ok = (sqlite3_step(playlist_stmt) == SQLITE_DONE);
        sqlite3_finalize(playlist_stmt);
    }
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

void library_db_for_each_m3u_file(struct library_db *database,
                                  library_db_m3u_stamp_callback callback, void *callback_context) {
    if (database == NULL || database->handle == NULL || callback == NULL) {
        return;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(database->handle,
                           "SELECT path, file_size_bytes, mtime_seconds FROM m3u_files;", -1,
                           &statement, NULL) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const char *path = (const char *)sqlite3_column_text(statement, 0);
            uint64_t size = (uint64_t)sqlite3_column_int64(statement, 1);
            int64_t mtime = sqlite3_column_int64(statement, 2);
            if (path != NULL) {
                callback(callback_context, path, size, mtime);
            }
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
}

static const char *order_clause_for_collection(enum library_db_collection_kind kind,
                                               enum library_sort_mode sort_mode) {
    if (kind == LIBRARY_DB_COLLECTION_ALBUMS || kind == LIBRARY_DB_COLLECTION_ALBUMS_FAVORITES ||
        kind == LIBRARY_DB_COLLECTION_ALBUMS_ARTIST) {
        switch (sort_mode) {
            case LIBRARY_SORT_REVERSE_ALPHABETICAL:
                return "ORDER BY a.title_sort DESC, a.id DESC";
            case LIBRARY_SORT_LIKED_FIRST:
                return "ORDER BY a.is_favorite DESC, a.title_sort ASC, a.id ASC";
            case LIBRARY_SORT_RECENT:
                return "ORDER BY a.added_at_seconds DESC, a.title_sort ASC, a.id ASC";
            case LIBRARY_SORT_OLDEST:
                return "ORDER BY a.added_at_seconds ASC, a.title_sort ASC, a.id ASC";
            case LIBRARY_SORT_ALPHABETICAL:
            default:
                return "ORDER BY a.title_sort ASC, a.id ASC";
        }
    }
    if (kind == LIBRARY_DB_COLLECTION_ARTISTS) {
        switch (sort_mode) {
            case LIBRARY_SORT_REVERSE_ALPHABETICAL:
                return "ORDER BY ar.name_sort DESC, ar.id DESC";
            case LIBRARY_SORT_LIKED_FIRST:
                return "ORDER BY ar.is_favorite DESC, ar.name_sort ASC, ar.id ASC";
            case LIBRARY_SORT_RECENT:
                return "ORDER BY ar.added_at_seconds DESC, ar.name_sort ASC, ar.id ASC";
            case LIBRARY_SORT_OLDEST:
                return "ORDER BY ar.added_at_seconds ASC, ar.name_sort ASC, ar.id ASC";
            case LIBRARY_SORT_ALPHABETICAL:
            default:
                return "ORDER BY ar.name_sort ASC, ar.id ASC";
        }
    }
    return "";
}

uint32_t library_db_count(struct library_db *database,
                          const struct library_db_collection *collection) {
    if (database == NULL || database->handle == NULL || collection == NULL) {
        return 0u;
    }

    const char *sql = NULL;
    bool bind_context = false;
    switch (collection->kind) {
        case LIBRARY_DB_COLLECTION_ALBUMS:
            sql = "SELECT COUNT(*) FROM albums;";
            break;
        case LIBRARY_DB_COLLECTION_ALBUMS_FAVORITES:
            sql = "SELECT COUNT(*) FROM albums WHERE is_favorite = 1;";
            break;
        case LIBRARY_DB_COLLECTION_ALBUMS_ARTIST:
            sql = "SELECT COUNT(*) FROM albums WHERE artist_id = ?1;";
            bind_context = true;
            break;
        case LIBRARY_DB_COLLECTION_ARTISTS:
            sql = "SELECT COUNT(*) FROM artists;";
            break;
        case LIBRARY_DB_COLLECTION_GENRES:
            sql = "SELECT COUNT(*) FROM genres;";
            break;
        case LIBRARY_DB_COLLECTION_PLAYLISTS:
            sql = "SELECT COUNT(*) FROM playlists WHERE is_dismissed = 0;";
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_ALL:
            sql = "SELECT COUNT(*) FROM tracks;";
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_FAVORITES:
            sql = "SELECT COUNT(*) FROM tracks WHERE is_favorite = 1;";
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_ALBUM:
            sql = "SELECT COUNT(*) FROM tracks WHERE album_id = ?1;";
            bind_context = true;
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_ARTIST:
            sql = "SELECT COUNT(*) FROM tracks WHERE artist_id = ?1;";
            bind_context = true;
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_GENRE:
            sql = "SELECT COUNT(*) FROM track_genres WHERE genre_id = ?1;";
            bind_context = true;
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST:
            sql = "SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id = ?1;";
            bind_context = true;
            break;
        default:
            return 0u;
    }

    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    uint32_t count = 0u;
    if (sqlite3_prepare_v2(database->handle, sql, -1, &statement, NULL) == SQLITE_OK) {
        if (bind_context) {
            sqlite3_bind_int64(statement, 1, collection->context_id);
        }
        if (sqlite3_step(statement) == SQLITE_ROW) {
            count = (uint32_t)sqlite3_column_int64(statement, 0);
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return count;
}

static bool prepare_window_statement(sqlite3 *handle, const struct library_db_collection *collection,
                                     enum library_sort_mode sort_mode, uint32_t offset,
                                     int32_t limit, sqlite3_stmt **statement_out) {
    char sql[SQL_STATEMENT_BYTES];
    bool bind_context = false;

    switch (collection->kind) {
        case LIBRARY_DB_COLLECTION_ALBUMS:
        case LIBRARY_DB_COLLECTION_ALBUMS_FAVORITES:
        case LIBRARY_DB_COLLECTION_ALBUMS_ARTIST: {
            const char *where_clause = "";
            if (collection->kind == LIBRARY_DB_COLLECTION_ALBUMS_FAVORITES) {
                where_clause = "WHERE a.is_favorite = 1 ";
            } else if (collection->kind == LIBRARY_DB_COLLECTION_ALBUMS_ARTIST) {
                where_clause = "WHERE a.artist_id = ?1 ";
                bind_context = true;
            }
            snprintf(sql, sizeof(sql),
                     "SELECT a.id, a.album_uid, a.id, a.added_at_seconds, a.track_count, 0, "
                     "a.year, 0, 0, a.motif_id, a.cover_source_kind, a.cover_cache_status, "
                     "a.is_favorite, a.title, a.artist_name "
                     "FROM albums a %s%s LIMIT ?2 OFFSET ?3;",
                     where_clause, order_clause_for_collection(collection->kind, sort_mode));
            break;
        }
        case LIBRARY_DB_COLLECTION_ARTISTS:
            snprintf(sql, sizeof(sql),
                     "SELECT ar.id, ar.artist_uid, 0, ar.added_at_seconds, ar.album_count, 0, "
                     "0, 0, 0, 0, 0, 0, ar.is_favorite, ar.name, '' "
                     "FROM artists ar %s LIMIT ?2 OFFSET ?3;",
                     order_clause_for_collection(collection->kind, sort_mode));
            break;
        case LIBRARY_DB_COLLECTION_GENRES:
            snprintf(sql, sizeof(sql),
                     "SELECT g.id, g.genre_uid, 0, 0, g.track_count, 0, 0, 0, 0, 0, 0, 0, 0, "
                     "g.name, '' FROM genres g ORDER BY g.name_sort ASC, g.id ASC LIMIT ?2 OFFSET ?3;");
            break;
        case LIBRARY_DB_COLLECTION_PLAYLISTS:
            snprintf(sql, sizeof(sql),
                     "SELECT p.id, p.playlist_uid, 0, 0, p.track_count, 0, 0, 0, 0, 0, 0, 0, 0, "
                     "p.name, '' FROM playlists p WHERE p.is_dismissed = 0 "
                     "ORDER BY p.name_sort ASC, p.id ASC LIMIT ?2 OFFSET ?3;");
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_ALL:
            snprintf(sql, sizeof(sql),
                     "SELECT t.id, t.audio_fingerprint, t.album_id, t.added_at_seconds, 0, "
                     "t.duration_seconds, 0, t.track_number, t.disc_number, 0, 0, 0, "
                     "t.is_favorite, t.title, t.artist_name "
                     "FROM tracks t ORDER BY t.title_sort ASC, t.id ASC LIMIT ?2 OFFSET ?3;");
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_FAVORITES:
            snprintf(sql, sizeof(sql),
                     "SELECT t.id, t.audio_fingerprint, t.album_id, t.added_at_seconds, 0, "
                     "t.duration_seconds, 0, t.track_number, t.disc_number, 0, 0, 0, "
                     "t.is_favorite, t.title, t.artist_name "
                     "FROM tracks t WHERE t.is_favorite = 1 "
                     "ORDER BY t.title_sort ASC, t.id ASC LIMIT ?2 OFFSET ?3;");
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_ALBUM:
            bind_context = true;
            snprintf(sql, sizeof(sql),
                     "SELECT t.id, t.audio_fingerprint, t.album_id, t.added_at_seconds, 0, "
                     "t.duration_seconds, 0, t.track_number, t.disc_number, 0, 0, 0, "
                     "t.is_favorite, t.title, t.artist_name "
                     "FROM tracks t WHERE t.album_id = ?1 "
                     "ORDER BY t.disc_number ASC, "
                     "CASE WHEN t.track_number > 0 THEN t.track_number ELSE %u END ASC, "
                     "t.title_sort ASC, t.id ASC LIMIT ?2 OFFSET ?3;",
                     UNORDERED_TRACK_NUMBER);
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_ARTIST:
            bind_context = true;
            snprintf(sql, sizeof(sql),
                     "SELECT t.id, t.audio_fingerprint, t.album_id, t.added_at_seconds, 0, "
                     "t.duration_seconds, 0, t.track_number, t.disc_number, 0, 0, 0, "
                     "t.is_favorite, t.title, a.title "
                     "FROM tracks t JOIN albums a ON a.id = t.album_id "
                     "WHERE t.artist_id = ?1 ORDER BY t.title_sort ASC, t.id ASC LIMIT ?2 OFFSET ?3;");
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_GENRE:
            bind_context = true;
            snprintf(sql, sizeof(sql),
                     "SELECT t.id, t.audio_fingerprint, t.album_id, t.added_at_seconds, 0, "
                     "t.duration_seconds, 0, t.track_number, t.disc_number, 0, 0, 0, "
                     "t.is_favorite, t.title, t.artist_name "
                     "FROM track_genres tg JOIN tracks t ON t.id = tg.track_id "
                     "WHERE tg.genre_id = ?1 ORDER BY t.title_sort ASC, t.id ASC LIMIT ?2 OFFSET ?3;");
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST:
            bind_context = true;
            snprintf(sql, sizeof(sql),
                     "SELECT t.id, t.audio_fingerprint, t.album_id, t.added_at_seconds, 0, "
                     "t.duration_seconds, 0, t.track_number, t.disc_number, 0, 0, 0, "
                     "t.is_favorite, t.title, t.artist_name "
                     "FROM playlist_tracks pt JOIN tracks t ON t.id = pt.track_id "
                     "WHERE pt.playlist_id = ?1 ORDER BY pt.position ASC LIMIT ?2 OFFSET ?3;");
            break;
        default:
            return false;
    }

    if (sqlite3_prepare_v2(handle, sql, -1, statement_out, NULL) != SQLITE_OK) {
        return false;
    }
    if (bind_context) {
        sqlite3_bind_int64(*statement_out, 1, collection->context_id);
    }
    sqlite3_bind_int(*statement_out, 2, limit);
    sqlite3_bind_int64(*statement_out, 3, (sqlite3_int64)offset);
    return true;
}

uint32_t library_db_fetch_window(struct library_db *database,
                                 const struct library_db_collection *collection,
                                 enum library_sort_mode sort_mode, uint32_t offset, uint32_t limit,
                                 struct library_db_row *rows_out) {
    if (database == NULL || database->handle == NULL || collection == NULL || rows_out == NULL ||
        limit == 0u) {
        return 0u;
    }
    uint32_t clamped_limit = (limit > LIBRARY_DB_MAX_WINDOW_ROWS) ? LIBRARY_DB_MAX_WINDOW_ROWS : limit;

    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    uint32_t count = 0u;
    if (prepare_window_statement(database->handle, collection, sort_mode, offset,
                                 (int32_t)clamped_limit, &statement)) {
        while (count < clamped_limit && sqlite3_step(statement) == SQLITE_ROW) {
            struct library_db_row *row = &rows_out[count++];
            memset(row, 0, sizeof(*row));
            row->id = sqlite3_column_int64(statement, 0);
            row->uid = (uint64_t)sqlite3_column_int64(statement, 1);
            row->album_id = sqlite3_column_int64(statement, 2);
            row->added_at_seconds = sqlite3_column_int64(statement, 3);
            row->count = (uint32_t)sqlite3_column_int64(statement, 4);
            row->duration_seconds = (uint32_t)sqlite3_column_int64(statement, 5);
            row->year = (uint16_t)sqlite3_column_int(statement, 6);
            row->track_number = (uint16_t)sqlite3_column_int(statement, 7);
            row->disc_number = (uint16_t)sqlite3_column_int(statement, 8);
            row->motif_id = (uint8_t)sqlite3_column_int(statement, 9);
            row->cover_source_kind = (uint8_t)sqlite3_column_int(statement, 10);
            row->cover_cache_status = (uint8_t)sqlite3_column_int(statement, 11);
            row->is_favorite = (sqlite3_column_int(statement, 12) != 0);
            copy_text_bounded(row->title, sizeof(row->title),
                              (const char *)sqlite3_column_text(statement, 13));
            copy_text_bounded(row->subtitle, sizeof(row->subtitle),
                              (const char *)sqlite3_column_text(statement, 14));
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return count;
}

static char normalize_anchor_letter(const char *label) {
    if (label == NULL) {
        return '#';
    }
    size_t index = 0u;
    while (label[index] != '\0' && isspace((unsigned char)label[index])) {
        index++;
    }
    char first = label[index];
    if (first >= 'a' && first <= 'z') {
        first = (char)(first - ('a' - 'A'));
    }
    return (first >= 'A' && first <= 'Z') ? first : '#';
}

static bool has_month_key(const int32_t *month_keys, uint32_t count, int32_t target) {
    for (uint32_t index = 0u; index < count; index++) {
        if (month_keys[index] == target) {
            return true;
        }
    }
    return false;
}

static bool has_anchor_key(const struct library_seek_anchor *anchors, uint32_t count,
                           const char *key) {
    for (uint32_t index = 0u; index < count; index++) {
        if (strcmp(anchors[index].key, key) == 0) {
            return true;
        }
    }
    return false;
}

uint32_t library_db_build_seek_anchors(struct library_db *database,
                                       const struct library_db_collection *collection,
                                       enum library_sort_mode sort_mode,
                                       struct library_seek_anchor *anchors_out,
                                       uint32_t max_anchors) {
    if (database == NULL || database->handle == NULL || collection == NULL ||
        anchors_out == NULL || max_anchors == 0u) {
        return 0u;
    }

    static const char *const MONTH_NAMES[MONTHS_PER_YEAR] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

    bool supports_date_mode = (collection->kind == LIBRARY_DB_COLLECTION_ALBUMS ||
                               collection->kind == LIBRARY_DB_COLLECTION_ALBUMS_FAVORITES ||
                               collection->kind == LIBRARY_DB_COLLECTION_ALBUMS_ARTIST ||
                               collection->kind == LIBRARY_DB_COLLECTION_ARTISTS);
    bool is_date_sort =
        supports_date_mode && (sort_mode == LIBRARY_SORT_RECENT || sort_mode == LIBRARY_SORT_OLDEST);
    bool is_liked_sort = supports_date_mode && (sort_mode == LIBRARY_SORT_LIKED_FIRST);

    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    if (!prepare_window_statement(database->handle, collection, sort_mode, 0u, -1, &statement)) {
        pthread_mutex_unlock(&database->mutex);
        return 0u;
    }

    uint32_t granularity = 2u; /* 0=year, 1=month, 2=day */
    if (is_date_sort) {
        int32_t distinct_months[32];
        uint32_t month_count = 0u;
        while (sqlite3_step(statement) == SQLITE_ROW) {
            int64_t added_at = sqlite3_column_int64(statement, 3);
            if (added_at <= 0) {
                continue;
            }
            time_t raw_time = (time_t)added_at;
            struct tm utc_tm;
            if (gmtime_r(&raw_time, &utc_tm) != NULL) {
                int32_t month_key = (utc_tm.tm_year + 1900) * 16 + utc_tm.tm_mon;
                if (!has_month_key(distinct_months, month_count, month_key)) {
                    if (month_count < 32u) {
                        distinct_months[month_count++] = month_key;
                    }
                    if (month_count > SEEK_YEAR_THRESHOLD_MONTHS) {
                        break;
                    }
                }
            }
        }
        granularity = (month_count > SEEK_YEAR_THRESHOLD_MONTHS) ? 0u
                      : (month_count >= SEEK_MIN_GROUPS ? 1u : 2u);
        sqlite3_reset(statement);
    }

    uint32_t anchor_count = 0u;
    uint32_t item_index = 0u;
    while (anchor_count < max_anchors && sqlite3_step(statement) == SQLITE_ROW) {
        int64_t added_at = sqlite3_column_int64(statement, 3);
        bool is_favorite = (sqlite3_column_int(statement, 12) != 0);
        const char *title = (const char *)sqlite3_column_text(statement, 13);

        struct library_seek_anchor candidate;
        memset(&candidate, 0, sizeof(candidate));
        candidate.first_item_index = item_index;

        if (is_date_sort) {
            candidate.is_date_mode = true;
            if (added_at <= 0) {
                copy_text_bounded(candidate.key, sizeof(candidate.key), "date:unknown");
                copy_text_bounded(candidate.primary, sizeof(candidate.primary), "\xE2\x80\x94");
                copy_text_bounded(candidate.sub_label, sizeof(candidate.sub_label), "NO DATE");
                copy_text_bounded(candidate.short_label, sizeof(candidate.short_label),
                                  "\xE2\x80\x94");
            } else {
                time_t raw_time = (time_t)added_at;
                struct tm utc_tm;
                (void)gmtime_r(&raw_time, &utc_tm);
                int year_full = utc_tm.tm_year + 1900;
                int short_year = year_full % TWO_DIGIT_YEAR_MODULUS;
                const char *month_name = MONTH_NAMES[utc_tm.tm_mon % (int)MONTHS_PER_YEAR];
                if (granularity == 0u) {
                    snprintf(candidate.key, sizeof(candidate.key), "y:%d", year_full);
                    snprintf(candidate.primary, sizeof(candidate.primary), "%d", year_full);
                    copy_text_bounded(candidate.short_label, sizeof(candidate.short_label),
                                      candidate.primary);
                } else if (granularity == 1u) {
                    snprintf(candidate.key, sizeof(candidate.key), "m:%d-%d", year_full,
                             utc_tm.tm_mon);
                    copy_text_bounded(candidate.primary, sizeof(candidate.primary), month_name);
                    snprintf(candidate.sub_label, sizeof(candidate.sub_label), "%d", year_full);
                    snprintf(candidate.short_label, sizeof(candidate.short_label),
                             "%s \xE2\x80\x99%02d", month_name, short_year);
                } else {
                    snprintf(candidate.key, sizeof(candidate.key), "d:%d-%d-%d", year_full,
                             utc_tm.tm_mon, utc_tm.tm_mday);
                    snprintf(candidate.primary, sizeof(candidate.primary), "%d", utc_tm.tm_mday);
                    snprintf(candidate.sub_label, sizeof(candidate.sub_label), "%s \xE2\x80\x99%02d",
                             month_name, short_year);
                    snprintf(candidate.short_label, sizeof(candidate.short_label), "%s %d",
                             month_name, utc_tm.tm_mday);
                }
            }
        } else {
            char letter = normalize_anchor_letter(title);
            candidate.is_favorite = is_liked_sort && is_favorite;
            snprintf(candidate.key, sizeof(candidate.key), "%s:%c",
                     candidate.is_favorite ? "liked" : "alpha", letter);
            candidate.primary[0] = letter;
            candidate.primary[1] = '\0';
            candidate.short_label[0] = letter;
            candidate.short_label[1] = '\0';
        }

        if (!has_anchor_key(anchors_out, anchor_count, candidate.key)) {
            anchors_out[anchor_count++] = candidate;
        }
        item_index++;
    }

    sqlite3_finalize(statement);
    pthread_mutex_unlock(&database->mutex);
    return anchor_count;
}

bool library_db_write_context_m3u(struct library_db *database,
                                  const struct library_db_collection *collection,
                                  uint32_t start_offset, FILE *output, uint32_t *written_out) {
    if (database == NULL || database->handle == NULL || collection == NULL || output == NULL) {
        return false;
    }
    if (written_out != NULL) {
        *written_out = 0u;
    }

    const char *sql = NULL;
    bool bind_context = false;
    switch (collection->kind) {
        case LIBRARY_DB_COLLECTION_TRACKS_ALL:
            sql = "SELECT path FROM tracks ORDER BY title_sort ASC, id ASC LIMIT -1 OFFSET ?2;";
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_FAVORITES:
            sql = "SELECT path FROM tracks WHERE is_favorite = 1 "
                  "ORDER BY title_sort ASC, id ASC LIMIT -1 OFFSET ?2;";
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_ALBUM:
            bind_context = true;
            sql = "SELECT path FROM tracks WHERE album_id = ?1 "
                  "ORDER BY disc_number ASC, "
                  "CASE WHEN track_number > 0 THEN track_number ELSE 9999 END ASC, "
                  "title_sort ASC, id ASC LIMIT -1 OFFSET ?2;";
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_ARTIST:
            bind_context = true;
            sql = "SELECT path FROM tracks WHERE artist_id = ?1 "
                  "ORDER BY title_sort ASC, id ASC LIMIT -1 OFFSET ?2;";
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_GENRE:
            bind_context = true;
            sql = "SELECT t.path FROM track_genres tg JOIN tracks t ON t.id = tg.track_id "
                  "WHERE tg.genre_id = ?1 ORDER BY t.title_sort ASC, t.id ASC LIMIT -1 OFFSET ?2;";
            break;
        case LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST:
            bind_context = true;
            sql = "SELECT t.path FROM playlist_tracks pt JOIN tracks t ON t.id = pt.track_id "
                  "WHERE pt.playlist_id = ?1 ORDER BY pt.position ASC LIMIT -1 OFFSET ?2;";
            break;
        default:
            return false;
    }

    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    bool ok = false;
    if (sqlite3_prepare_v2(database->handle, sql, -1, &statement, NULL) == SQLITE_OK) {
        if (bind_context) {
            sqlite3_bind_int64(statement, 1, collection->context_id);
        }
        sqlite3_bind_int64(statement, 2, (sqlite3_int64)start_offset);
        uint32_t written = 0u;
        ok = true;
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const char *path = (const char *)sqlite3_column_text(statement, 0);
            if (path != NULL && fprintf(output, "%s\n", path) < 0) {
                ok = false;
                break;
            }
            written++;
        }
        if (written_out != NULL) {
            *written_out = written;
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

bool library_db_write_playlist_shuffled_m3u(struct library_db *database, int64_t playlist_id,
                                            FILE *output, uint32_t *written_out) {
    if (database == NULL || database->handle == NULL || playlist_id <= 0 || output == NULL) {
        return false;
    }
    if (written_out != NULL) {
        *written_out = 0u;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    bool ok = false;
    if (sqlite3_prepare_v2(
            database->handle,
            "SELECT t.path FROM playlist_tracks pt JOIN tracks t ON t.id = pt.track_id "
            "WHERE pt.playlist_id = ?1 ORDER BY RANDOM();",
            -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(statement, 1, playlist_id);
        uint32_t written = 0u;
        ok = true;
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const char *path = (const char *)sqlite3_column_text(statement, 0);
            if (path != NULL && fprintf(output, "%s\n", path) < 0) {
                ok = false;
                break;
            }
            written++;
        }
        if (written_out != NULL) {
            *written_out = written;
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

uint32_t library_db_count_pending_covers(struct library_db *database) {
    if (database == NULL || database->handle == NULL) {
        return 0u;
    }
    pthread_mutex_lock(&database->mutex);
    uint32_t count = query_single_count(
        database->handle, "SELECT COUNT(*) FROM albums WHERE cover_cache_status = 0;");
    pthread_mutex_unlock(&database->mutex);
    return count;
}

uint32_t library_db_query_pending_covers(struct library_db *database,
                                         struct library_db_cover_job *jobs_out, uint32_t capacity) {
    if (database == NULL || database->handle == NULL || jobs_out == NULL || capacity == 0u) {
        return 0u;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    uint32_t count = 0u;
    if (sqlite3_prepare_v2(
            database->handle,
            "SELECT id, album_uid, cover_source_kind, cover_data_offset, cover_data_length, "
            "COALESCE(cover_path, '') FROM albums "
            "WHERE cover_cache_status = 0 ORDER BY title_sort ASC, id ASC LIMIT ?1;",
            -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int(statement, 1, (int)capacity);
        while (count < capacity && sqlite3_step(statement) == SQLITE_ROW) {
            struct library_db_cover_job *job = &jobs_out[count++];
            memset(job, 0, sizeof(*job));
            job->album_id = sqlite3_column_int64(statement, 0);
            job->album_uid = (uint64_t)sqlite3_column_int64(statement, 1);
            job->cover_source_kind = (uint8_t)sqlite3_column_int(statement, 2);
            job->data_offset = (uint32_t)sqlite3_column_int64(statement, 3);
            job->data_length = (uint32_t)sqlite3_column_int64(statement, 4);
            copy_text_bounded(job->source_path, sizeof(job->source_path),
                              (const char *)sqlite3_column_text(statement, 5));
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return count;
}

bool library_db_get_album_cover_job(struct library_db *database, int64_t album_id,
                                    struct library_db_cover_job *job_out) {
    if (database == NULL || database->handle == NULL || album_id <= 0 || job_out == NULL) {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    bool found = false;
    if (sqlite3_prepare_v2(
            database->handle,
            "SELECT id, album_uid, cover_source_kind, cover_data_offset, cover_data_length, "
            "COALESCE(cover_path, '') FROM albums WHERE id = ?1;",
            -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(statement, 1, album_id);
        if (sqlite3_step(statement) == SQLITE_ROW) {
            memset(job_out, 0, sizeof(*job_out));
            job_out->album_id = sqlite3_column_int64(statement, 0);
            job_out->album_uid = (uint64_t)sqlite3_column_int64(statement, 1);
            job_out->cover_source_kind = (uint8_t)sqlite3_column_int(statement, 2);
            job_out->data_offset = (uint32_t)sqlite3_column_int64(statement, 3);
            job_out->data_length = (uint32_t)sqlite3_column_int64(statement, 4);
            copy_text_bounded(job_out->source_path, sizeof(job_out->source_path),
                              (const char *)sqlite3_column_text(statement, 5));
            found = true;
        }
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return found;
}

bool library_db_set_cover_status(struct library_db *database, int64_t album_id,
                                 enum library_db_cover_status status) {
    if (database == NULL || database->handle == NULL || album_id <= 0) {
        return false;
    }
    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *statement = NULL;
    bool ok = false;
    if (sqlite3_prepare_v2(database->handle,
                           "UPDATE albums SET cover_cache_status = ?1 WHERE id = ?2;", -1,
                           &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int(statement, 1, (int)status);
        sqlite3_bind_int64(statement, 2, album_id);
        ok = (sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database->handle) > 0);
        sqlite3_finalize(statement);
    }
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

bool library_db_mark_cover_statuses_batch(struct library_db *database, const int64_t *album_ids,
                                          const uint8_t *statuses, uint32_t count) {
    if (database == NULL || database->handle == NULL) {
        return false;
    }
    if (count == 0u || album_ids == NULL || statuses == NULL) {
        return true;
    }
    pthread_mutex_lock(&database->mutex);
    if (!execute_sql(database->handle, "BEGIN IMMEDIATE;")) {
        pthread_mutex_unlock(&database->mutex);
        return false;
    }
    sqlite3_stmt *statement = NULL;
    bool ok = (sqlite3_prepare_v2(database->handle,
                                  "UPDATE albums SET cover_cache_status = ?1 WHERE id = ?2;", -1,
                                  &statement, NULL) == SQLITE_OK);
    if (ok) {
        for (uint32_t index = 0u; index < count; index++) {
            sqlite3_reset(statement);
            sqlite3_bind_int(statement, 1, (int)statuses[index]);
            sqlite3_bind_int64(statement, 2, album_ids[index]);
            if (sqlite3_step(statement) != SQLITE_DONE) {
                ok = false;
                break;
            }
        }
        sqlite3_finalize(statement);
    }
    if (ok) {
        ok = execute_sql(database->handle, "COMMIT;");
    } else {
        (void)execute_sql(database->handle, "ROLLBACK;");
    }
    pthread_mutex_unlock(&database->mutex);
    return ok;
}

bool library_db_apply_sidecar_cover(struct library_db *database, const char *directory_path,
                                    const char *cover_path, int64_t mtime_seconds,
                                    uint64_t file_size_bytes) {
    if (database == NULL || database->handle == NULL || directory_path == NULL ||
        directory_path[0] == '\0') {
        return false;
    }

    char prefix[LIBRARY_DB_PATH_BYTES + 4u];
    size_t directory_length = strlen(directory_path);
    snprintf(prefix, sizeof(prefix), "%s/%%", directory_path);
    int prefix_char_count = (int)(directory_length + 2u);

    pthread_mutex_lock(&database->mutex);
    sqlite3_stmt *select_stmt = NULL;
    if (sqlite3_prepare_v2(
            database->handle,
            "SELECT DISTINCT a.id, a.cover_source_kind, COALESCE(a.cover_path, ''), "
            "a.cover_source_mtime, a.cover_source_size "
            "FROM albums a JOIN tracks t ON t.album_id = a.id "
            "WHERE t.path LIKE ?1 AND substr(t.path, ?2) NOT LIKE '%/%';",
            -1, &select_stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&database->mutex);
        return false;
    }
    sqlite3_bind_text(select_stmt, 1, prefix, -1, SQLITE_STATIC);
    sqlite3_bind_int(select_stmt, 2, prefix_char_count);

    int64_t album_ids[64];
    uint8_t current_kinds[64];
    bool needs_update[64];
    uint32_t match_count = 0u;

    while (match_count < 64u && sqlite3_step(select_stmt) == SQLITE_ROW) {
        int64_t album_id = sqlite3_column_int64(select_stmt, 0);
        uint8_t current_kind = (uint8_t)sqlite3_column_int(select_stmt, 1);
        const char *current_path = (const char *)sqlite3_column_text(select_stmt, 2);
        int64_t current_mtime = sqlite3_column_int64(select_stmt, 3);
        uint64_t current_size = (uint64_t)sqlite3_column_int64(select_stmt, 4);

        bool dirty = false;
        if (cover_path != NULL && cover_path[0] != '\0') {
            if (current_kind != LIBRARY_COVER_SIDECAR || current_path == NULL ||
                strcmp(current_path, cover_path) != 0 || current_size != file_size_bytes ||
                llabs(current_mtime - mtime_seconds) > FAT32_MTIME_TOLERANCE_SECONDS) {
                dirty = true;
            }
        } else if (current_kind == LIBRARY_COVER_SIDECAR) {
            dirty = true;
        }
        album_ids[match_count] = album_id;
        current_kinds[match_count] = current_kind;
        needs_update[match_count] = dirty;
        match_count++;
    }
    sqlite3_finalize(select_stmt);

    for (uint32_t index = 0u; index < match_count; index++) {
        if (!needs_update[index]) {
            continue;
        }
        sqlite3_stmt *update_stmt = NULL;
        if (cover_path != NULL && cover_path[0] != '\0') {
            if (sqlite3_prepare_v2(
                    database->handle,
                    "UPDATE albums SET cover_source_kind = ?1, cover_path = ?2, "
                    "cover_data_offset = 0, cover_data_length = 0, cover_source_mtime = ?3, "
                    "cover_source_size = ?4, cover_cache_status = ?5 WHERE id = ?6;",
                    -1, &update_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(update_stmt, 1, LIBRARY_COVER_SIDECAR);
                sqlite3_bind_text(update_stmt, 2, cover_path, -1, SQLITE_STATIC);
                sqlite3_bind_int64(update_stmt, 3, mtime_seconds);
                sqlite3_bind_int64(update_stmt, 4, (sqlite3_int64)file_size_bytes);
                sqlite3_bind_int(update_stmt, 5, LIBRARY_DB_COVER_PENDING);
                sqlite3_bind_int64(update_stmt, 6, album_ids[index]);
                (void)sqlite3_step(update_stmt);
                sqlite3_finalize(update_stmt);
            }
        } else if (current_kinds[index] == LIBRARY_COVER_SIDECAR) {
            if (sqlite3_prepare_v2(
                    database->handle,
                    "UPDATE albums SET cover_source_kind = ?1, cover_path = NULL, "
                    "cover_data_offset = 0, cover_data_length = 0, cover_source_mtime = 0, "
                    "cover_source_size = 0, cover_cache_status = ?2 WHERE id = ?3;",
                    -1, &update_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(update_stmt, 1, LIBRARY_COVER_NONE);
                sqlite3_bind_int(update_stmt, 2, LIBRARY_DB_COVER_NONE);
                sqlite3_bind_int64(update_stmt, 3, album_ids[index]);
                (void)sqlite3_step(update_stmt);
                sqlite3_finalize(update_stmt);
            }
        }
    }

    pthread_mutex_unlock(&database->mutex);
    return true;
}
