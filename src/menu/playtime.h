/**
 * @file playtime.h
 * @brief Playtime tracking
 * @ingroup menu
 */

#ifndef PLAYTIME_H__
#define PLAYTIME_H__

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define PLAYTIME_RECENT_SESSIONS_MAX 5

typedef struct {
    uint64_t duration_seconds;
    int64_t ended_at;
} playtime_session_t;

typedef struct {
    char *path;
    uint64_t total_seconds;
    uint64_t last_session_seconds;
    int64_t last_played;
    int64_t active_start;
    uint32_t play_count;
    bool active;
    playtime_session_t recent_sessions[PLAYTIME_RECENT_SESSIONS_MAX];
    uint32_t recent_sessions_count;
} playtime_entry_t;

typedef struct {
    playtime_entry_t *entries;
    uint32_t count;
} playtime_db_t;

void playtime_init(char *path);
void playtime_load(playtime_db_t *db);
void playtime_save(playtime_db_t *db);
void playtime_finalize_active(playtime_db_t *db, time_t now);
void playtime_start_session(playtime_db_t *db, const char *path, time_t now);
playtime_entry_t *playtime_get(playtime_db_t *db, const char *path);
void playtime_free(playtime_db_t *db);

#endif /* PLAYTIME_H__ */
