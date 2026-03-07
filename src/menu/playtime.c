/**
 * @file playtime.c
 * @brief Playtime tracking
 * @ingroup menu
 */

#include <stdlib.h>
#include <string.h>

#include <mini.c/src/mini.h>

#include "../flashcart/flashcart.h"
#include "playtime.h"
#include "utils/fs.h"

static char *playtime_path = NULL;

typedef enum {
    SC64_SETTING_ID_PLAYTIME_ACTIVE = 1,
    SC64_SETTING_ID_PLAYTIME_ROM_HASH = 2,
    SC64_SETTING_ID_PLAYTIME_SECONDS = 3,
} sc64_playtime_setting_id_t;

static uint32_t playtime_hash_string(const char *value) {
    uint32_t h = 2166136261u;
    if (!value) {
        return 0;
    }
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        h ^= (uint32_t)(*p);
        h *= 16777619u;
    }
    return h;
}

static uint32_t playtime_tracker_key_hash(const playtime_entry_t *entry, const char *path, const char *game_id) {
    if (game_id && game_id[0] != '\0') {
        return playtime_hash_string(game_id);
    }
    if (entry && entry->game_id[0] != '\0') {
        return playtime_hash_string(entry->game_id);
    }
    if (path && path[0] != '\0') {
        return playtime_hash_string(path);
    }
    if (entry && entry->path) {
        return playtime_hash_string(entry->path);
    }
    return 0;
}

static bool playtime_sc64_tracker_read(uint32_t *active, uint32_t *rom_hash, uint32_t *seconds) {
    if (!active || !rom_hash || !seconds) {
        return false;
    }
    if (flashcart_get_setting_u32(SC64_SETTING_ID_PLAYTIME_ACTIVE, active) != FLASHCART_OK) {
        return false;
    }
    if (flashcart_get_setting_u32(SC64_SETTING_ID_PLAYTIME_ROM_HASH, rom_hash) != FLASHCART_OK) {
        return false;
    }
    if (flashcart_get_setting_u32(SC64_SETTING_ID_PLAYTIME_SECONDS, seconds) != FLASHCART_OK) {
        return false;
    }
    return true;
}

static bool playtime_sc64_tracker_clear(void) {
    if (flashcart_set_setting_u32(SC64_SETTING_ID_PLAYTIME_SECONDS, 0) != FLASHCART_OK) {
        return false;
    }
    if (flashcart_set_setting_u32(SC64_SETTING_ID_PLAYTIME_ROM_HASH, 0) != FLASHCART_OK) {
        return false;
    }
    if (flashcart_set_setting_u32(SC64_SETTING_ID_PLAYTIME_ACTIVE, 0) != FLASHCART_OK) {
        return false;
    }
    return true;
}

static bool playtime_sc64_tracker_start(const char *path) {
    char game_id[ROM_STABLE_ID_LENGTH] = {0};
    rom_info_get_stable_id_for_path(path, game_id, sizeof(game_id));
    uint32_t rom_hash = playtime_tracker_key_hash(NULL, path, game_id);
    if (rom_hash == 0) {
        return false;
    }
    if (flashcart_set_setting_u32(SC64_SETTING_ID_PLAYTIME_SECONDS, 0) != FLASHCART_OK) {
        return false;
    }
    if (flashcart_set_setting_u32(SC64_SETTING_ID_PLAYTIME_ROM_HASH, rom_hash) != FLASHCART_OK) {
        return false;
    }
    if (flashcart_set_setting_u32(SC64_SETTING_ID_PLAYTIME_ACTIVE, 1) != FLASHCART_OK) {
        return false;
    }
    return true;
}

static void playtime_entry_free(playtime_entry_t *entry) {
    if (!entry) {
        return;
    }
    free(entry->path);
    entry->path = NULL;
}

static bool playtime_entry_matches(const playtime_entry_t *entry, const char *path, const char *game_id) {
    if (!entry) {
        return false;
    }
    if (path && entry->path && strcmp(entry->path, path) == 0) {
        return true;
    }
    if (game_id && game_id[0] != '\0' && entry->game_id[0] != '\0' && strcmp(entry->game_id, game_id) == 0) {
        return true;
    }
    return false;
}

static void playtime_entry_set_identity(playtime_entry_t *entry, const char *path, const char *game_id) {
    if (!entry) {
        return;
    }

    if (path && path[0] != '\0' && (!entry->path || strcmp(entry->path, path) != 0)) {
        free(entry->path);
        entry->path = strdup(path);
    }

    if (game_id && game_id[0] != '\0') {
        snprintf(entry->game_id, sizeof(entry->game_id), "%s", game_id);
    }
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

static void playtime_add_entry(playtime_db_t *db, const char *path, const char *game_id) {
    db->entries = realloc(db->entries, (db->count + 1) * sizeof(playtime_entry_t));
    playtime_entry_t *entry = &db->entries[db->count];
    memset(entry, 0, sizeof(*entry));
    entry->path = path ? strdup(path) : NULL;
    if (game_id && game_id[0] != '\0') {
        snprintf(entry->game_id, sizeof(entry->game_id), "%s", game_id);
    }
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

    char game_id[ROM_STABLE_ID_LENGTH] = {0};
    rom_info_get_stable_id_for_path(path, game_id, sizeof(game_id));

    for (uint32_t i = 0; i < db->count; i++) {
        if (playtime_entry_matches(&db->entries[i], path, game_id)) {
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

    bool migrated = false;
    for (int64_t i = 0; i < count; i++) {
        char key[64];
        sprintf(key, "%lld_path", (long long)i);
        const char *path = mini_get_string(ini, "stats", key, "");
        if (path == NULL || path[0] == '\0') {
            continue;
        }

        sprintf(key, "%lld_game_id", (long long)i);
        const char *game_id = mini_get_string(ini, "stats", key, "");

        playtime_add_entry(db, path, game_id);
        playtime_entry_t *entry = &db->entries[db->count - 1];
        if (entry->game_id[0] == '\0' && rom_info_get_stable_id_for_path(path, entry->game_id, sizeof(entry->game_id))) {
            migrated = true;
        }

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

    if (migrated) {
        playtime_save(db);
    }
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

        sprintf(key, "%u_game_id", (unsigned int)i);
        mini_set_string(ini, "stats", key, entry->game_id);

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

    uint32_t active = 0;
    uint32_t rom_hash = 0;
    uint32_t seconds = 0;
    bool sc64_tracker_available = playtime_sc64_tracker_read(&active, &rom_hash, &seconds);
    if (sc64_tracker_available) {
        if ((active != 0) && (rom_hash != 0) && (seconds > 0)) {
            for (uint32_t i = 0; i < db->count; i++) {
                playtime_entry_t *entry = &db->entries[i];
                if (!entry->path) {
                    continue;
                }
                if (playtime_tracker_key_hash(entry, NULL, NULL) != rom_hash) {
                    continue;
                }
                entry->total_seconds += (uint64_t)seconds;
                entry->last_session_seconds = (uint64_t)seconds;
                entry->last_played = (int64_t)now;
                playtime_push_recent_session(entry, (uint64_t)seconds, (int64_t)now);
                changed = true;
                break;
            }
        }
        playtime_sc64_tracker_clear();
    }

    for (uint32_t i = 0; i < db->count; i++) {
        playtime_entry_t *entry = &db->entries[i];
        if (!entry->active) {
            continue;
        }

        if (sc64_tracker_available) {
            // SC64 firmware tracker is authoritative; clear legacy marker to avoid double-counting.
            changed = true;
        } else {
            if (entry->active_start > 0 && now >= entry->active_start) {
                uint64_t delta = (uint64_t)(now - entry->active_start);
                entry->total_seconds += delta;
                entry->last_session_seconds = delta;
                playtime_push_recent_session(entry, delta, (int64_t)now);
                changed = true;
            }
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

    char game_id[ROM_STABLE_ID_LENGTH] = {0};
    rom_info_get_stable_id_for_path(path, game_id, sizeof(game_id));

    playtime_entry_t *entry = playtime_get(db, path);
    if (!entry) {
        playtime_add_entry(db, path, game_id);
        entry = &db->entries[db->count - 1];
    } else {
        playtime_entry_set_identity(entry, path, game_id);
    }

    entry->last_played = (int64_t)now;
    if (playtime_sc64_tracker_start(path)) {
        entry->active = false;
        entry->active_start = 0;
    } else {
        entry->active = true;
        entry->active_start = (int64_t)now;
    }
    entry->play_count++;

    playtime_save(db);
}
