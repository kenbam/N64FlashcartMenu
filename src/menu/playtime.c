/**
 * @file playtime.c
 * @brief Playtime tracking
 * @ingroup menu
 */

#include <stdlib.h>
#include <string.h>

#include <mini.c/src/mini.h>

#include "playtime.h"
#include "utils/fs.h"

static char *playtime_path = NULL;

static void playtime_entry_free(playtime_entry_t *entry) {
    if (!entry) {
        return;
    }
    free(entry->path);
    entry->path = NULL;
}

void playtime_init (char *path) {
    if (playtime_path) {
        free(playtime_path);
    }
    playtime_path = strdup(path);
}

void playtime_free (playtime_db_t *db) {
    if (!db || !db->entries) {
        return;
    }
    for (uint32_t i = 0; i < db->count; i++) {
        playtime_entry_free(&db->entries[i]);
    }
    free(db->entries);
    db->entries = NULL;
    db->count = 0;
}

static void playtime_add_entry(playtime_db_t *db, const char *path) {
    db->entries = realloc(db->entries, (db->count + 1) * sizeof(playtime_entry_t));
    playtime_entry_t *entry = &db->entries[db->count];
    memset(entry, 0, sizeof(*entry));
    entry->path = strdup(path);
    entry->total_seconds = 0;
    entry->last_session_seconds = 0;
    entry->last_played = 0;
    entry->active_start = 0;
    entry->play_count = 0;
    entry->active = false;
    entry->recent_sessions_count = 0;
    db->count++;
}

static void playtime_push_recent_session(playtime_entry_t *entry, uint64_t duration_seconds, int64_t ended_at) {
    if (!entry || duration_seconds == 0 || ended_at <= 0) {
        return;
    }

    for (int i = PLAYTIME_RECENT_SESSIONS_MAX - 1; i > 0; i--) {
        entry->recent_sessions[i] = entry->recent_sessions[i - 1];
    }

    entry->recent_sessions[0].duration_seconds = duration_seconds;
    entry->recent_sessions[0].ended_at = ended_at;

    if (entry->recent_sessions_count < PLAYTIME_RECENT_SESSIONS_MAX) {
        entry->recent_sessions_count++;
    }
}

playtime_entry_t *playtime_get (playtime_db_t *db, const char *path) {
    if (!db || !path) {
        return NULL;
    }
    for (uint32_t i = 0; i < db->count; i++) {
        if (db->entries[i].path && strcmp(db->entries[i].path, path) == 0) {
            return &db->entries[i];
        }
    }
    return NULL;
}

void playtime_load (playtime_db_t *db) {
    if (!db) {
        return;
    }

    playtime_free(db);

    if (!file_exists(playtime_path)) {
        playtime_save(db);
    }

    mini_t *ini = mini_try_load(playtime_path);
    int64_t count = mini_get_int(ini, "stats", "count", 0);
    if (count < 0) {
        count = 0;
    }

    for (int64_t i = 0; i < count; i++) {
        char key[64];
        sprintf(key, "%lld_path", (long long)i);
        const char *path = mini_get_string(ini, "stats", key, "");
        if (path == NULL || path[0] == '\0') {
            continue;
        }

        playtime_add_entry(db, path);
        playtime_entry_t *entry = &db->entries[db->count - 1];

        sprintf(key, "%lld_total", (long long)i);
        entry->total_seconds = (uint64_t)mini_get_int(ini, "stats", key, 0);

        sprintf(key, "%lld_last_session", (long long)i);
        entry->last_session_seconds = (uint64_t)mini_get_int(ini, "stats", key, 0);

        sprintf(key, "%lld_last_played", (long long)i);
        entry->last_played = (int64_t)mini_get_int(ini, "stats", key, 0);

        sprintf(key, "%lld_active_start", (long long)i);
        entry->active_start = (int64_t)mini_get_int(ini, "stats", key, 0);

        sprintf(key, "%lld_active", (long long)i);
        entry->active = mini_get_int(ini, "stats", key, 0) ? true : false;

        sprintf(key, "%lld_play_count", (long long)i);
        entry->play_count = (uint32_t)mini_get_int(ini, "stats", key, 0);

        sprintf(key, "%lld_recent_count", (long long)i);
        int64_t recent_count = mini_get_int(ini, "stats", key, 0);
        if (recent_count < 0) {
            recent_count = 0;
        }
        if (recent_count > PLAYTIME_RECENT_SESSIONS_MAX) {
            recent_count = PLAYTIME_RECENT_SESSIONS_MAX;
        }
        entry->recent_sessions_count = (uint32_t)recent_count;
        for (uint32_t j = 0; j < entry->recent_sessions_count; j++) {
            sprintf(key, "%lld_recent_%u_duration", (long long)i, (unsigned int)j);
            entry->recent_sessions[j].duration_seconds = (uint64_t)mini_get_int(ini, "stats", key, 0);
            sprintf(key, "%lld_recent_%u_ended_at", (long long)i, (unsigned int)j);
            entry->recent_sessions[j].ended_at = (int64_t)mini_get_int(ini, "stats", key, 0);
        }
    }

    mini_free(ini);
}

void playtime_save (playtime_db_t *db) {
    if (!db) {
        return;
    }

    mini_t *ini = mini_create(playtime_path);

    mini_set_int(ini, "stats", "count", db->count);

    for (uint32_t i = 0; i < db->count; i++) {
        char key[64];
        playtime_entry_t *entry = &db->entries[i];

        sprintf(key, "%u_path", (unsigned int)i);
        mini_set_string(ini, "stats", key, entry->path ? entry->path : "");

        sprintf(key, "%u_total", (unsigned int)i);
        mini_set_int(ini, "stats", key, (long long)entry->total_seconds);

        sprintf(key, "%u_last_session", (unsigned int)i);
        mini_set_int(ini, "stats", key, (long long)entry->last_session_seconds);

        sprintf(key, "%u_last_played", (unsigned int)i);
        mini_set_int(ini, "stats", key, (long long)entry->last_played);

        sprintf(key, "%u_active_start", (unsigned int)i);
        mini_set_int(ini, "stats", key, (long long)entry->active_start);

        sprintf(key, "%u_active", (unsigned int)i);
        mini_set_int(ini, "stats", key, entry->active ? 1 : 0);

        sprintf(key, "%u_play_count", (unsigned int)i);
        mini_set_int(ini, "stats", key, (long long)entry->play_count);

        sprintf(key, "%u_recent_count", (unsigned int)i);
        mini_set_int(ini, "stats", key, (long long)entry->recent_sessions_count);
        for (uint32_t j = 0; j < entry->recent_sessions_count; j++) {
            sprintf(key, "%u_recent_%u_duration", (unsigned int)i, (unsigned int)j);
            mini_set_int(ini, "stats", key, (long long)entry->recent_sessions[j].duration_seconds);
            sprintf(key, "%u_recent_%u_ended_at", (unsigned int)i, (unsigned int)j);
            mini_set_int(ini, "stats", key, (long long)entry->recent_sessions[j].ended_at);
        }
    }

    mini_save(ini, MINI_FLAGS_SKIP_EMPTY_GROUPS);
    mini_free(ini);
}

void playtime_finalize_active (playtime_db_t *db, time_t now) {
    if (!db || now < 0) {
        return;
    }

    bool changed = false;
    for (uint32_t i = 0; i < db->count; i++) {
        playtime_entry_t *entry = &db->entries[i];
        if (!entry->active) {
            continue;
        }

        if (entry->active_start > 0 && now >= entry->active_start) {
            uint64_t delta = (uint64_t)(now - entry->active_start);
            entry->total_seconds += delta;
            entry->last_session_seconds = delta;
            playtime_push_recent_session(entry, delta, (int64_t)now);
            changed = true;
        }

        entry->active = false;
        entry->active_start = 0;
    }

    if (changed) {
        playtime_save(db);
    }
}

void playtime_start_session (playtime_db_t *db, const char *path, time_t now) {
    if (!db || !path || now < 0) {
        return;
    }

    playtime_entry_t *entry = playtime_get(db, path);
    if (!entry) {
        playtime_add_entry(db, path);
        entry = &db->entries[db->count - 1];
    }

    entry->last_played = (int64_t)now;
    entry->active = true;
    entry->active_start = (int64_t)now;
    entry->play_count++;

    playtime_save(db);
}
