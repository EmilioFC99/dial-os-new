#include "test_runner.h"

#include "library_db.h"
#include "sqlite3.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SCALE_TRACK_COUNT 25000u
#define SCALE_TRACKS_PER_ALBUM 10u
#define SCALE_ALBUMS_PER_ARTIST 5u

static void make_work_directory(char *path_out, size_t capacity) {
    snprintf(path_out, capacity, "/tmp/dialos_db_XXXXXX");
    TEST_ASSERT_TRUE(mkdtemp(path_out) != NULL);
}

static void remove_tree(const char *path) {
    char command[512];
    snprintf(command, sizeof(command), "rm -rf \"%s\"", path);
    (void)system(command);
}

static bool file_exists(const char *path) {
    struct stat file_stat;
    return stat(path, &file_stat) == 0;
}

static struct library_db_track_input make_input(const char *path, const char *title,
                                                const char *artist, const char *album,
                                                const char *genre, uint16_t track_number,
                                                int64_t added_at_seconds) {
    struct library_db_track_input input;
    memset(&input, 0, sizeof(input));
    input.path = path;
    input.audio_fingerprint = library_fnv1a_64(path);
    input.file_size_bytes = 1000u;
    input.mtime_seconds = added_at_seconds;
    input.format_kind = LIBRARY_FORMAT_FLAC;
    input.title = title;
    input.artist = artist;
    input.album_artist = artist;
    input.album = album;
    input.genre = genre;
    input.track_number = track_number;
    input.duration_seconds = 100u;
    input.added_at_seconds = added_at_seconds;
    return input;
}

static int64_t insert_track(struct library_db *database, const char *path, const char *title,
                            const char *artist, const char *album, const char *genre,
                            uint16_t track_number, int64_t added_at_seconds) {
    struct library_db_track_input input =
        make_input(path, title, artist, album, genre, track_number, added_at_seconds);
    int64_t track_id = 0;
    bool inserted = library_db_upsert_track(database, &input, 0, &track_id);
    if (!inserted) {
        fprintf(stderr, "insert_track failed for %s\n", path);
    }
    return inserted ? track_id : -1;
}

static void query_text(struct library_db *database, const char *sql, char *text_out,
                       size_t capacity) {
    sqlite3_stmt *statement = NULL;
    text_out[0] = '\0';
    if (sqlite3_prepare_v2((sqlite3 *)database->handle, sql, -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *value = sqlite3_column_text(statement, 0);
        if (value != NULL) {
            snprintf(text_out, capacity, "%s", (const char *)value);
        }
    }
    sqlite3_finalize(statement);
}

static void seed_small_library(struct library_db *database) {
    insert_track(database, "/m/a/1.flac", "Alpha", "Artist One", "Album A", "House; Electronic", 1,
                 1700000000);
    insert_track(database, "/m/a/2.flac", "Bravo", "Artist One", "Album A", "House", 2, 1700000100);
    insert_track(database, "/m/b/1.flac", "Charlie", "Artist Two", "Beta Album", "Ambient", 1,
                 1710000000);
    insert_track(database, "/m/c/1.flac", "9 Lives", "Artist Three", "Zeta", "Dub", 1, 1720000000);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(database, NULL, 0u, NULL));
}

static void test_open_creates_schema(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));

    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    char text[64];
    query_text(&database, "PRAGMA journal_mode", text, sizeof(text));
    TEST_ASSERT_EQ_STR("delete", text);
    query_text(&database, "PRAGMA foreign_keys", text, sizeof(text));
    TEST_ASSERT_EQ_STR("1", text);
    query_text(&database, "SELECT value FROM meta WHERE key = 'schema_version'", text,
               sizeof(text));
    TEST_ASSERT_EQ_STR("1", text);

    struct library_db_counts counts;
    TEST_ASSERT_TRUE(library_db_read_counts(&database, &counts));
    TEST_ASSERT_EQ_U32(0u, counts.tracks);
    TEST_ASSERT_EQ_U32(0u, counts.albums);
    library_db_close(&database);

    /* Verify O_NOATIME read-only helper opens existing files and rejects invalid inputs. */
    char db_file[256];
    snprintf(db_file, sizeof(db_file), "%s/library.db", work_directory);
    FILE *opened = library_fopen_readonly_noatime(db_file);
    TEST_ASSERT_TRUE(opened != NULL);
    fclose(opened);
    TEST_ASSERT_TRUE(library_fopen_readonly_noatime(NULL) == NULL);
    TEST_ASSERT_TRUE(library_fopen_readonly_noatime("") == NULL);
    TEST_ASSERT_TRUE(library_fopen_readonly_noatime("/nonexistent/dialos_missing_file") == NULL);

    remove_tree(work_directory);
}

static void test_upsert_counts_multi_genre_and_motifs(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    seed_small_library(&database);

    struct library_db_counts counts;
    TEST_ASSERT_TRUE(library_db_read_counts(&database, &counts));
    TEST_ASSERT_EQ_U32(4u, counts.tracks);
    TEST_ASSERT_EQ_U32(3u, counts.albums);
    TEST_ASSERT_EQ_U32(3u, counts.artists);
    TEST_ASSERT_EQ_U32(4u, counts.genres); /* House, Electronic, Ambient, Dub */
    TEST_ASSERT_EQ_U32(0u, counts.playlists);

    struct library_db_collection albums = {LIBRARY_DB_COLLECTION_ALBUMS, 0};
    struct library_db_row rows[8];
    TEST_ASSERT_EQ_U32(3u, library_db_fetch_window(&database, &albums, LIBRARY_SORT_ALPHABETICAL,
                                                   0u, 8u, rows));
    TEST_ASSERT_EQ_STR("Album A", rows[0].title);
    TEST_ASSERT_EQ_U32(2u, rows[0].count);
    TEST_ASSERT_EQ_U32(0u, rows[0].motif_id);
    TEST_ASSERT_EQ_U32(1u, rows[1].motif_id);
    TEST_ASSERT_EQ_U32(1700000100u, (uint32_t)rows[0].added_at_seconds);

    /* Same album tags in a different folder must land in the same album row. */
    insert_track(&database, "/elsewhere/3.flac", "Echo", "Artist One", "Album A", "House", 3,
                 1730000000);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&database, NULL, 0u, NULL));
    TEST_ASSERT_TRUE(library_db_read_counts(&database, &counts));
    TEST_ASSERT_EQ_U32(3u, counts.albums);
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_favorites_and_sort_modes_persist(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    seed_small_library(&database);

    struct library_db_collection tracks = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};
    struct library_db_row rows[8];
    TEST_ASSERT_EQ_U32(4u, library_db_fetch_window(&database, &tracks, LIBRARY_SORT_ALPHABETICAL,
                                                   0u, 8u, rows));
    TEST_ASSERT_EQ_STR("9 Lives", rows[0].title);
    bool state = false;
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_TRACK, rows[1].id,
                                                &state));
    TEST_ASSERT_TRUE(state);
    struct library_db_collection albums = {LIBRARY_DB_COLLECTION_ALBUMS, 0};
    TEST_ASSERT_EQ_U32(3u, library_db_fetch_window(&database, &albums, LIBRARY_SORT_ALPHABETICAL,
                                                   0u, 8u, rows));
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_ALBUM, rows[2].id,
                                                &state));
    struct library_db_collection favorite_albums = {LIBRARY_DB_COLLECTION_ALBUMS_FAVORITES, 0};
    TEST_ASSERT_EQ_U32(1u, library_db_count(&database, &favorite_albums));
    struct library_db_row favorite_album_rows[4];
    TEST_ASSERT_EQ_U32(1u, library_db_fetch_window(&database, &favorite_albums,
                                                   LIBRARY_SORT_ALPHABETICAL, 0u, 4u,
                                                   favorite_album_rows));
    TEST_ASSERT_EQ_U32((uint32_t)rows[2].id, (uint32_t)favorite_album_rows[0].id);
    struct library_db_collection artists = {LIBRARY_DB_COLLECTION_ARTISTS, 0};
    TEST_ASSERT_EQ_U32(3u, library_db_fetch_window(&database, &artists, LIBRARY_SORT_ALPHABETICAL,
                                                   0u, 8u, rows));
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_ARTIST, rows[0].id,
                                                &state));
    TEST_ASSERT_TRUE(library_db_set_sort_mode(&database, LIBRARY_DB_SORT_SCOPE_ALBUMS,
                                              LIBRARY_SORT_RECENT));
    TEST_ASSERT_TRUE(library_db_set_sort_mode(&database, LIBRARY_DB_SORT_SCOPE_ARTISTS,
                                              LIBRARY_SORT_LIKED_FIRST));
    library_db_close(&database);

    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    struct library_db_counts counts;
    TEST_ASSERT_TRUE(library_db_read_counts(&database, &counts));
    TEST_ASSERT_EQ_U32(1u, counts.favorite_tracks);
    TEST_ASSERT_EQ_U32(1u, counts.favorite_albums);
    TEST_ASSERT_EQ_U32(1u, counts.favorite_artists);
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_RECENT,
                       library_db_get_sort_mode(&database, LIBRARY_DB_SORT_SCOPE_ALBUMS));
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_LIKED_FIRST,
                       library_db_get_sort_mode(&database, LIBRARY_DB_SORT_SCOPE_ARTISTS));

    bool state_after = true;
    TEST_ASSERT_FALSE(library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_TRACK, 999999,
                                                 &state_after));
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_album_sort_modes_and_track_scopes(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    seed_small_library(&database);

    struct library_db_collection albums = {LIBRARY_DB_COLLECTION_ALBUMS, 0};
    struct library_db_row rows[8];
    library_db_fetch_window(&database, &albums, LIBRARY_SORT_ALPHABETICAL, 0u, 8u, rows);
    TEST_ASSERT_EQ_STR("Album A", rows[0].title);
    TEST_ASSERT_EQ_STR("Beta Album", rows[1].title);
    TEST_ASSERT_EQ_STR("Zeta", rows[2].title);
    int64_t beta_id = rows[1].id;
    int64_t album_a_id = rows[0].id;

    library_db_fetch_window(&database, &albums, LIBRARY_SORT_REVERSE_ALPHABETICAL, 0u, 8u, rows);
    TEST_ASSERT_EQ_STR("Zeta", rows[0].title);

    bool state = false;
    library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_ALBUM, beta_id, &state);
    library_db_fetch_window(&database, &albums, LIBRARY_SORT_LIKED_FIRST, 0u, 8u, rows);
    TEST_ASSERT_EQ_STR("Beta Album", rows[0].title);
    TEST_ASSERT_TRUE(rows[0].is_favorite);

    library_db_fetch_window(&database, &albums, LIBRARY_SORT_RECENT, 0u, 8u, rows);
    TEST_ASSERT_EQ_STR("Zeta", rows[0].title);
    library_db_fetch_window(&database, &albums, LIBRARY_SORT_OLDEST, 0u, 8u, rows);
    TEST_ASSERT_EQ_STR("Album A", rows[0].title);

    /* Window offsets and limits. */
    TEST_ASSERT_EQ_U32(1u, library_db_fetch_window(&database, &albums, LIBRARY_SORT_ALPHABETICAL,
                                                   2u, 8u, rows));
    TEST_ASSERT_EQ_STR("Zeta", rows[0].title);
    TEST_ASSERT_EQ_U32(0u, library_db_fetch_window(&database, &albums, LIBRARY_SORT_ALPHABETICAL,
                                                   9u, 8u, rows));

    struct library_db_collection album_tracks = {LIBRARY_DB_COLLECTION_TRACKS_ALBUM, album_a_id};
    TEST_ASSERT_EQ_U32(2u, library_db_count(&database, &album_tracks));
    library_db_fetch_window(&database, &album_tracks, LIBRARY_SORT_ALPHABETICAL, 0u, 8u, rows);
    TEST_ASSERT_EQ_STR("Alpha", rows[0].title);
    TEST_ASSERT_EQ_STR("Bravo", rows[1].title);

    struct library_db_collection favorites = {LIBRARY_DB_COLLECTION_TRACKS_FAVORITES, 0};
    TEST_ASSERT_EQ_U32(0u, library_db_count(&database, &favorites));
    library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_TRACK, rows[1].id, &state);
    TEST_ASSERT_EQ_U32(1u, library_db_count(&database, &favorites));

    struct library_db_collection genres = {LIBRARY_DB_COLLECTION_GENRES, 0};
    TEST_ASSERT_EQ_U32(4u, library_db_fetch_window(&database, &genres, LIBRARY_SORT_ALPHABETICAL,
                                                   0u, 8u, rows));
    TEST_ASSERT_EQ_STR("Ambient", rows[0].title);
    int64_t house_id = 0;
    for (uint32_t index = 0u; index < 4u; index++) {
        if (strcmp(rows[index].title, "House") == 0) {
            house_id = rows[index].id;
            TEST_ASSERT_EQ_U32(2u, rows[index].count);
        }
    }
    struct library_db_collection house_tracks = {LIBRARY_DB_COLLECTION_TRACKS_GENRE, house_id};
    TEST_ASSERT_EQ_U32(2u, library_db_count(&database, &house_tracks));

    struct library_db_collection artists = {LIBRARY_DB_COLLECTION_ARTISTS, 0};
    library_db_fetch_window(&database, &artists, LIBRARY_SORT_ALPHABETICAL, 0u, 8u, rows);
    TEST_ASSERT_EQ_STR("Artist One", rows[0].title);
    TEST_ASSERT_EQ_U32(1u, rows[0].count);
    struct library_db_collection artist_albums = {LIBRARY_DB_COLLECTION_ALBUMS_ARTIST, rows[0].id};
    TEST_ASSERT_EQ_U32(1u, library_db_count(&database, &artist_albums));
    struct library_db_collection artist_tracks = {LIBRARY_DB_COLLECTION_TRACKS_ARTIST, rows[0].id};
    TEST_ASSERT_EQ_U32(2u, library_db_count(&database, &artist_tracks));

    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_playlists_dismissal_and_cascade(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    seed_small_library(&database);

    const char *paths[3] = {"/m/c/1.flac", "/m/a/1.flac", "/missing/none.flac"};
    TEST_ASSERT_TRUE(library_db_upsert_m3u_playlist(&database, "Late Mix", "/m/mix.m3u", paths, 3u));
    struct library_db_collection playlists = {LIBRARY_DB_COLLECTION_PLAYLISTS, 0};
    struct library_db_row rows[4];
    TEST_ASSERT_EQ_U32(1u, library_db_fetch_window(&database, &playlists,
                                                   LIBRARY_SORT_ALPHABETICAL, 0u, 4u, rows));
    TEST_ASSERT_EQ_U32(2u, rows[0].count); /* unresolved path skipped */
    struct library_db_collection playlist_tracks = {LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST, rows[0].id};
    library_db_fetch_window(&database, &playlist_tracks, LIBRARY_SORT_ALPHABETICAL, 0u, 4u, rows);
    TEST_ASSERT_EQ_STR("9 Lives", rows[0].title); /* curated order, not A-Z: Alpha is 2nd */
    TEST_ASSERT_EQ_STR("Alpha", rows[1].title);

    library_db_fetch_window(&database, &playlists, LIBRARY_SORT_ALPHABETICAL, 0u, 4u, rows);
    int64_t playlist_id = rows[0].id;
    TEST_ASSERT_TRUE(library_db_delete_playlist(&database, playlist_id));
    TEST_ASSERT_EQ_U32(0u, library_db_count(&database, &playlists));
    /* A rescan of the same .m3u must not resurrect a dismissed playlist. */
    TEST_ASSERT_TRUE(library_db_upsert_m3u_playlist(&database, "Late Mix", "/m/mix.m3u", paths, 3u));
    TEST_ASSERT_EQ_U32(0u, library_db_count(&database, &playlists));
    TEST_ASSERT_FALSE(library_db_delete_playlist(&database, 424242));

    /* Deleting a track cascades out of an active playlist. */
    TEST_ASSERT_TRUE(library_db_upsert_m3u_playlist(&database, "Other", "/m/other.m3u", paths, 2u));
    TEST_ASSERT_EQ_U32(1u, library_db_count(&database, &playlists));
    library_db_fetch_window(&database, &playlists, LIBRARY_SORT_ALPHABETICAL, 0u, 4u, rows);
    struct library_db_collection other_tracks = {LIBRARY_DB_COLLECTION_TRACKS_PLAYLIST, rows[0].id};
    struct library_db_row track_rows[4];
    library_db_fetch_window(&database, &other_tracks, LIBRARY_SORT_ALPHABETICAL, 0u, 4u, track_rows);
    TEST_ASSERT_TRUE(library_db_delete_track(&database, track_rows[0].id));
    TEST_ASSERT_EQ_U32(1u, library_db_count(&database, &other_tracks));

    TEST_ASSERT_TRUE(library_db_put_m3u_file(&database, "/m/other.m3u", 50u, 1234));
    TEST_ASSERT_TRUE(library_db_remove_m3u_file(&database, "/m/other.m3u"));
    TEST_ASSERT_EQ_U32(0u, library_db_count(&database, &playlists));
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_seek_anchors_alphabetical_liked_and_date(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    seed_small_library(&database);

    struct library_db_collection tracks = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};
    struct library_seek_anchor anchors[64];
    uint32_t count = library_db_build_seek_anchors(&database, &tracks, LIBRARY_SORT_ALPHABETICAL,
                                                   anchors, 64u);
    TEST_ASSERT_EQ_U32(4u, count); /* '#', A, B, C */
    TEST_ASSERT_EQ_STR("#", anchors[0].primary);
    TEST_ASSERT_EQ_U32(0u, anchors[0].first_item_index);
    TEST_ASSERT_EQ_STR("A", anchors[1].primary);
    TEST_ASSERT_EQ_U32(1u, anchors[1].first_item_index);
    TEST_ASSERT_EQ_STR("C", anchors[3].primary);
    TEST_ASSERT_EQ_U32(3u, anchors[3].first_item_index);

    struct library_db_collection albums = {LIBRARY_DB_COLLECTION_ALBUMS, 0};
    count = library_db_build_seek_anchors(&database, &albums, LIBRARY_SORT_REVERSE_ALPHABETICAL,
                                          anchors, 64u);
    TEST_ASSERT_EQ_U32(3u, count);
    TEST_ASSERT_EQ_STR("Z", anchors[0].primary);
    TEST_ASSERT_EQ_STR("A", anchors[2].primary);

    struct library_db_row rows[8];
    library_db_fetch_window(&database, &albums, LIBRARY_SORT_ALPHABETICAL, 0u, 8u, rows);
    bool state = false;
    library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_ALBUM, rows[2].id, &state);
    count = library_db_build_seek_anchors(&database, &albums, LIBRARY_SORT_LIKED_FIRST, anchors,
                                          64u);
    TEST_ASSERT_EQ_U32(3u, count);
    TEST_ASSERT_TRUE(anchors[0].is_favorite);
    TEST_ASSERT_EQ_STR("Z", anchors[0].primary);
    TEST_ASSERT_FALSE(anchors[1].is_favorite);

    /* Three albums: 1700000100 (Nov 2023), 1710000000 (Mar 2024), 1720000000 (Jul 2024) UTC. */
    count = library_db_build_seek_anchors(&database, &albums, LIBRARY_SORT_RECENT, anchors, 64u);
    TEST_ASSERT_TRUE(count >= 2u);
    TEST_ASSERT_TRUE(anchors[0].is_date_mode);
    TEST_ASSERT_EQ_STR("JUL", anchors[0].primary);
    TEST_ASSERT_EQ_STR("2024", anchors[0].sub_label);
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_write_context_m3u_streams_from_offset(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    seed_small_library(&database);

    char output_path[256];
    snprintf(output_path, sizeof(output_path), "%s/queue.m3u", work_directory);
    FILE *output = fopen(output_path, "w");
    TEST_ASSERT_TRUE(output != NULL);
    struct library_db_collection tracks = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};
    uint32_t written = 0u;
    TEST_ASSERT_TRUE(library_db_write_context_m3u(&database, &tracks, 2u, output, &written));
    fclose(output);
    TEST_ASSERT_EQ_U32(2u, written);

    char contents[256];
    FILE *input = fopen(output_path, "r");
    size_t length = fread(contents, 1u, sizeof(contents) - 1u, input);
    fclose(input);
    contents[length] = '\0';
    TEST_ASSERT_EQ_STR("/m/a/2.flac\n/m/b/1.flac\n", contents);

    char path_text[LIBRARY_DB_PATH_BYTES];
    TEST_ASSERT_TRUE(library_db_get_track_path(&database, 1, path_text, sizeof(path_text)));
    TEST_ASSERT_EQ_STR("/m/a/1.flac", path_text);
    TEST_ASSERT_FALSE(library_db_get_track_path(&database, 5000, path_text, sizeof(path_text)));

    int64_t found_track_id = 0;
    int64_t found_album_id = 0;
    uint64_t found_album_uid = 0u;
    TEST_ASSERT_TRUE(library_db_find_track_by_path(&database, "/m/a/1.flac", &found_track_id,
                                                    &found_album_id, &found_album_uid));
    TEST_ASSERT_EQ_I32(1, (int32_t)found_track_id);
    TEST_ASSERT_TRUE(found_album_id > 0);
    TEST_ASSERT_TRUE(found_album_uid != 0u);
    TEST_ASSERT_FALSE(library_db_find_track_by_path(&database, "/no/such/track.flac",
                                                     &found_track_id, &found_album_id,
                                                     &found_album_uid));
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_pending_covers_lifecycle_and_sidecar_changes(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));

    struct library_db_track_input sidecar_track =
        make_input("/m/a/1.flac", "Alpha", "Artist One", "Album A", "House", 1, 1700000000);
    sidecar_track.cover_source_kind = LIBRARY_COVER_SIDECAR;
    sidecar_track.cover_path = "/m/a/cover.jpg";
    sidecar_track.cover_source_mtime = 500;
    sidecar_track.cover_source_size = 2048u;
    int64_t track_id = 0;
    TEST_ASSERT_TRUE(library_db_upsert_track(&database, &sidecar_track, 0, &track_id));
    insert_track(&database, "/m/b/1.flac", "Bravo", "Artist Two", "No Cover Album", "Dub", 1,
                 1700000001);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&database, NULL, 0u, NULL));

    TEST_ASSERT_EQ_U32(1u, library_db_count_pending_covers(&database));
    struct library_db_cover_job jobs[4];
    TEST_ASSERT_EQ_U32(1u, library_db_query_pending_covers(&database, jobs, 4u));
    TEST_ASSERT_EQ_STR("/m/a/cover.jpg", jobs[0].source_path);
    TEST_ASSERT_EQ_U32(LIBRARY_COVER_SIDECAR, jobs[0].cover_source_kind);

    struct library_db_cover_job single_job;
    TEST_ASSERT_TRUE(library_db_get_album_cover_job(&database, jobs[0].album_id, &single_job));
    TEST_ASSERT_EQ_STR("/m/a/cover.jpg", single_job.source_path);
    TEST_ASSERT_FALSE(library_db_get_album_cover_job(&database, 999999, &single_job));

    uint8_t status_cached = LIBRARY_DB_COVER_CACHED;
    TEST_ASSERT_TRUE(library_db_mark_cover_statuses_batch(&database, &jobs[0].album_id,
                                                          &status_cached, 1u));
    TEST_ASSERT_EQ_U32(0u, library_db_query_pending_covers(&database, jobs, 4u));

    /* Identical sidecar: nothing becomes pending. */
    TEST_ASSERT_TRUE(library_db_apply_sidecar_cover(&database, "/m/a", "/m/a/cover.jpg", 500, 2048u));
    TEST_ASSERT_EQ_U32(0u, library_db_count_pending_covers(&database));
    /* Replaced sidecar: only that album becomes pending. */
    TEST_ASSERT_TRUE(library_db_apply_sidecar_cover(&database, "/m/a", "/m/a/cover.jpg", 900, 4096u));
    TEST_ASSERT_EQ_U32(1u, library_db_count_pending_covers(&database));
    /* Removed sidecar with no embedded fallback: album has no cover. */
    TEST_ASSERT_TRUE(library_db_apply_sidecar_cover(&database, "/m/a", NULL, 0, 0u));
    TEST_ASSERT_EQ_U32(0u, library_db_count_pending_covers(&database));
    /* A new sidecar appears next to an uncovered album. */
    TEST_ASSERT_TRUE(library_db_apply_sidecar_cover(&database, "/m/b", "/m/b/folder.png", 10, 99u));
    TEST_ASSERT_EQ_U32(1u, library_db_count_pending_covers(&database));
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_track_identity_survives_update_and_move(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    int64_t track_id = insert_track(&database, "/m/a/1.flac", "Alpha", "Artist One", "Album A",
                                    "House", 1, 1700000000);
    bool state = false;
    TEST_ASSERT_TRUE(library_db_toggle_favorite(&database, LIBRARY_DB_FAVORITE_TRACK, track_id,
                                                &state));

    /* Cheap move: only path and mtime change. */
    TEST_ASSERT_TRUE(library_db_update_track_location(&database, track_id, "/new/spot/1.flac", 1700000555));
    char path_text[LIBRARY_DB_PATH_BYTES];
    TEST_ASSERT_TRUE(library_db_get_track_path(&database, track_id, path_text, sizeof(path_text)));
    TEST_ASSERT_EQ_STR("/new/spot/1.flac", path_text);

    /* Move plus tag edit: same row id, new album, favorite kept, original added date kept. */
    struct library_db_track_input edited =
        make_input("/third/1.flac", "Alpha Renamed", "Artist One", "Album Z", "House", 1, 1800000000);
    int64_t updated_id = 0;
    TEST_ASSERT_TRUE(library_db_upsert_track(&database, &edited, track_id, &updated_id));
    TEST_ASSERT_EQ_I32(track_id, updated_id);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&database, NULL, 0u, NULL));
    struct library_db_counts counts;
    library_db_read_counts(&database, &counts);
    TEST_ASSERT_EQ_U32(1u, counts.tracks);
    TEST_ASSERT_EQ_U32(1u, counts.albums);
    TEST_ASSERT_EQ_U32(1u, counts.favorite_tracks);
    struct library_db_collection tracks = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};
    struct library_db_row rows[2];
    library_db_fetch_window(&database, &tracks, LIBRARY_SORT_ALPHABETICAL, 0u, 2u, rows);
    TEST_ASSERT_EQ_STR("Alpha Renamed", rows[0].title);
    TEST_ASSERT_EQ_I32(1700000000, rows[0].added_at_seconds);

    /* Upserting an unchanged path finds the existing row instead of duplicating it. */
    int64_t same_id = 0;
    TEST_ASSERT_TRUE(library_db_upsert_track(&database, &edited, 0, &same_id));
    TEST_ASSERT_EQ_I32(track_id, same_id);
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_delete_prunes_empty_parents_and_reports_albums(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    int64_t only_track = insert_track(&database, "/m/z/1.flac", "Solo", "Lone Artist", "Lone Album",
                                      "Rare", 1, 1700000000);
    insert_track(&database, "/m/a/1.flac", "Alpha", "Artist One", "Album A", "House", 1, 1700000001);
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&database, NULL, 0u, NULL));

    struct library_db_collection albums = {LIBRARY_DB_COLLECTION_ALBUMS, 0};
    struct library_db_row rows[4];
    library_db_fetch_window(&database, &albums, LIBRARY_SORT_ALPHABETICAL, 0u, 4u, rows);
    uint64_t lone_album_uid = 0u;
    for (uint32_t index = 0u; index < 2u; index++) {
        if (strcmp(rows[index].title, "Lone Album") == 0) {
            lone_album_uid = rows[index].uid;
        }
    }
    TEST_ASSERT_TRUE(lone_album_uid != 0u);

    TEST_ASSERT_TRUE(library_db_delete_track(&database, only_track));
    uint64_t deleted_uids[8];
    uint32_t deleted_count = 0u;
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&database, deleted_uids, 8u, &deleted_count));
    TEST_ASSERT_EQ_U32(1u, deleted_count);
    TEST_ASSERT_EQ_U64(lone_album_uid, deleted_uids[0]);
    struct library_db_counts counts;
    library_db_read_counts(&database, &counts);
    TEST_ASSERT_EQ_U32(1u, counts.albums);
    TEST_ASSERT_EQ_U32(1u, counts.artists);
    TEST_ASSERT_EQ_U32(1u, counts.genres);
    TEST_ASSERT_FALSE(library_db_delete_track(&database, only_track));
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_corrupt_database_is_quarantined_and_recreated(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    char database_path[256];
    snprintf(database_path, sizeof(database_path), "%s/library.db", work_directory);
    FILE *garbage = fopen(database_path, "wb");
    TEST_ASSERT_TRUE(garbage != NULL);
    for (uint32_t index = 0u; index < 4096u; index++) {
        fputc((int)((index * 37u) & 0xFFu), garbage);
    }
    fclose(garbage);

    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    char quarantine_path[256];
    snprintf(quarantine_path, sizeof(quarantine_path), "%s/%s", work_directory,
             LIBRARY_DB_CORRUPT_FILE_NAME);
    TEST_ASSERT_TRUE(file_exists(quarantine_path));
    struct library_db_counts counts;
    TEST_ASSERT_TRUE(library_db_read_counts(&database, &counts));
    TEST_ASSERT_EQ_U32(0u, counts.tracks);
    TEST_ASSERT_TRUE(insert_track(&database, "/m/a/1.flac", "Alpha", "A", "B", "C", 1, 1) > 0);
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_empty_database_queries_are_calm(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));
    struct library_db_collection albums = {LIBRARY_DB_COLLECTION_ALBUMS, 0};
    struct library_db_collection tracks = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};
    struct library_db_row rows[4];
    struct library_seek_anchor anchors[8];
    TEST_ASSERT_EQ_U32(0u, library_db_count(&database, &albums));
    TEST_ASSERT_EQ_U32(0u, library_db_fetch_window(&database, &albums, LIBRARY_SORT_RECENT, 0u, 4u, rows));
    TEST_ASSERT_EQ_U32(0u, library_db_build_seek_anchors(&database, &tracks, LIBRARY_SORT_ALPHABETICAL, anchors, 8u));
    TEST_ASSERT_EQ_U32(0u, library_db_count_pending_covers(&database));
    TEST_ASSERT_EQ_I32(LIBRARY_SORT_ALPHABETICAL,
                       library_db_get_sort_mode(&database, LIBRARY_DB_SORT_SCOPE_ALBUMS));
    uint32_t written = 99u;
    FILE *sink = tmpfile();
    TEST_ASSERT_TRUE(library_db_write_context_m3u(&database, &tracks, 0u, sink, &written));
    fclose(sink);
    TEST_ASSERT_EQ_U32(0u, written);
    library_db_close(&database);
    remove_tree(work_directory);
}

static void test_scale_has_no_track_cap_and_windows_stay_fast(void) {
    char work_directory[128];
    make_work_directory(work_directory, sizeof(work_directory));
    struct library_db database;
    TEST_ASSERT_TRUE(library_db_open(&database, work_directory));

    TEST_ASSERT_TRUE(library_db_begin(&database));
    for (uint32_t index = 0u; index < SCALE_TRACK_COUNT; index++) {
        char path[96];
        char title[48];
        char album[48];
        char artist[48];
        uint32_t album_number = index / SCALE_TRACKS_PER_ALBUM;
        snprintf(path, sizeof(path), "/music/%05u/%03u.flac", album_number,
                 index % SCALE_TRACKS_PER_ALBUM);
        snprintf(title, sizeof(title), "Song %05u", index);
        snprintf(album, sizeof(album), "Album %05u", album_number);
        snprintf(artist, sizeof(artist), "Artist %04u", album_number / SCALE_ALBUMS_PER_ARTIST);
        struct library_db_track_input input =
            make_input(path, title, artist, album, "Rock", (uint16_t)(index % SCALE_TRACKS_PER_ALBUM + 1u),
                       1700000000 + (int64_t)index);
        TEST_ASSERT_TRUE(library_db_upsert_track(&database, &input, 0, NULL));
    }
    TEST_ASSERT_TRUE(library_db_commit(&database));
    TEST_ASSERT_TRUE(library_db_recompute_aggregates(&database, NULL, 0u, NULL));

    struct library_db_counts counts;
    TEST_ASSERT_TRUE(library_db_read_counts(&database, &counts));
    TEST_ASSERT_EQ_U32(SCALE_TRACK_COUNT, counts.tracks);
    TEST_ASSERT_EQ_U32(SCALE_TRACK_COUNT / SCALE_TRACKS_PER_ALBUM, counts.albums);
    TEST_ASSERT_EQ_U32(SCALE_TRACK_COUNT / SCALE_TRACKS_PER_ALBUM / SCALE_ALBUMS_PER_ARTIST,
                       counts.artists);

    struct library_db_collection tracks = {LIBRARY_DB_COLLECTION_TRACKS_ALL, 0};
    struct library_db_row rows[LIBRARY_DB_MAX_WINDOW_ROWS];
    TEST_ASSERT_EQ_U32(LIBRARY_DB_MAX_WINDOW_ROWS,
                       library_db_fetch_window(&database, &tracks, LIBRARY_SORT_ALPHABETICAL,
                                               12500u, LIBRARY_DB_MAX_WINDOW_ROWS, rows));
    TEST_ASSERT_EQ_STR("Song 12500", rows[0].title);
    TEST_ASSERT_EQ_U32(10u, library_db_fetch_window(&database, &tracks, LIBRARY_SORT_ALPHABETICAL,
                                                    24990u, LIBRARY_DB_MAX_WINDOW_ROWS, rows));
    TEST_ASSERT_EQ_STR("Song 24990", rows[0].title);

    struct library_seek_anchor anchors[64];
    uint32_t anchor_count = library_db_build_seek_anchors(&database, &tracks, LIBRARY_SORT_ALPHABETICAL, anchors, 64u);
    TEST_ASSERT_EQ_U32(1u, anchor_count); /* every title starts with "S" */

    FILE *sink = tmpfile();
    uint32_t written = 0u;
    TEST_ASSERT_TRUE(library_db_write_context_m3u(&database, &tracks, 24990u, sink, &written));
    fclose(sink);
    TEST_ASSERT_EQ_U32(10u, written);
    library_db_close(&database);
    remove_tree(work_directory);
}

void run_library_db_tests(void) {
    printf("Library DB tests...\n");
    RUN_TEST(test_open_creates_schema);
    RUN_TEST(test_upsert_counts_multi_genre_and_motifs);
    RUN_TEST(test_favorites_and_sort_modes_persist);
    RUN_TEST(test_album_sort_modes_and_track_scopes);
    RUN_TEST(test_playlists_dismissal_and_cascade);
    RUN_TEST(test_seek_anchors_alphabetical_liked_and_date);
    RUN_TEST(test_write_context_m3u_streams_from_offset);
    RUN_TEST(test_pending_covers_lifecycle_and_sidecar_changes);
    RUN_TEST(test_track_identity_survives_update_and_move);
    RUN_TEST(test_delete_prunes_empty_parents_and_reports_albums);
    RUN_TEST(test_corrupt_database_is_quarantined_and_recreated);
    RUN_TEST(test_empty_database_queries_are_calm);
    RUN_TEST(test_scale_has_no_track_cap_and_windows_stay_fast);
}
