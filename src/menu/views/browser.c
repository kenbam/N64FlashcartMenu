#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <miniz.h>
#include <miniz_zip.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <libdragon.h>

#include "../cart_load.h"
#include "../disk_pairing.h"
#include "../fonts.h"
#include "../png_decoder.h"
#include "../rom_info.h"
#include "../ui_components/constants.h"
#include "../virtual_pak.h"
#include "utils/fs.h"
#include "utils/hash.h"
#include "views.h"
#include "../sound.h"

static const char *archive_extensions[] = { "zip", NULL };
static const char *cheat_extensions[] = {"cht", "cheats", "datel", "gameshark", NULL};
static const char *disk_extensions[] = { "ndd", NULL };
static const char *emulator_extensions[] = { "nes", "sfc", "smc", "gb", "gbc", "sms", "gg", "sg", "chf", NULL };
static const char *image_extensions[] = { "png", NULL };
static const char *music_extensions[] = { "mp3", "wav64", NULL };
static const char *n64_rom_extensions[] = { "z64", "n64", "v64", "rom", NULL };
static const char *patch_extensions[] = { "bps", "ips", "aps", "ups", "xdelta", NULL };
static const char *playlist_extensions[] = { "m3u", "m3u8", NULL };
// TODO: "eep", "sra", "srm", "fla" could be used if transfered from different flashcarts.
static const char *save_extensions[] = { "sav", NULL };
static const char *text_extensions[] = { "txt", "ini", "yml", "yaml", NULL };

static const char *hidden_root_paths[] = {
    "/menu.bin",
    "/menu",
    "/N64FlashcartMenu.n64",
    "/ED64",
    "/ED64P",
    "/sc64menu.n64",
    // Windows garbage
    "/System Volume Information",
    // macOS garbage
    "/.fseventsd",
    "/.Spotlight-V100",
    "/.Trashes",
    "/.VolumeIcon.icns",
    "/.metadata_never_index",
    NULL,
};

static bool browser_virtual_pak_recovery_active = false;
static bool browser_virtual_pak_recovery_failed = false;
static int browser_virtual_pak_recovery_retry_cooldown = 0;
static bool browser_virtual_pak_recovery_snoozed = false;
static char browser_virtual_pak_recovery_message[128];

/*
 * ========================================================================
 * Playlist 3-tier cache architecture
 * ========================================================================
 *
 * Tier 1 — Memory LRU (playlist_mem_cache[])
 *   Up to PLAYLIST_MEM_CACHE_ENTRIES slots.  Stores parsed entry paths,
 *   playlist properties, and the content hash.  Hit cost: zero SD I/O.
 *   Eviction: least-recently-used tick counter.
 *
 * Tier 2 — Disk binary cache (sd:/menu/cache/playlists/pl_<hash>.cache)
 *   Written after a full M3U parse if the source file meets size/entry
 *   thresholds.  Format: fixed header, length-prefixed strings for props,
 *   then length-prefixed entry paths.  Validated by magic, version, and
 *   FNV-1a content hash of the source M3U bytes.
 *
 * Tier 3 — Full M3U parse
 *   Reads the .m3u/.m3u8 line by line, resolves relative paths, handles
 *   #EXTM3U directives (theme, bgm, bg, viz, grid, smart queries, etc.).
 *   Result is stored into tier 1 and optionally tier 2.
 *
 * Prewarm: on browser init the most-recently-used playlists (stored in
 * recent.txt) are loaded from tier 2 into tier 1 across idle frames to
 * give instant navigation to frequently visited playlists.
 *
 * Disk cache binary format (version 3):
 *   playlist_cache_header_t  (fixed 48-byte header)
 *   len-prefixed string      theme
 *   len-prefixed string      bgm
 *   len-prefixed string      bg
 *   len-prefixed string      screensaver_logo
 *   len-prefixed string[]    entry_count × entry paths
 *
 * Each len-prefixed string: uint32_t length, then `length` bytes (no NUL).
 * Length 0 means empty/NULL string; length > 65536 is rejected on read.
 */

#define PLAYLIST_CACHE_MAGIC   (0x504C4331u)
#define PLAYLIST_CACHE_VERSION (3u)
#define PLAYLIST_CACHE_DIR     "menu/cache/playlists"
#define PLAYLIST_RECENT_FILE   "menu/cache/playlists/recent.txt"
#define PLAYLIST_MEM_CACHE_ENTRIES 6u
#define PLAYLIST_RECENT_LIMIT  8u
#define PLAYLIST_DISK_CACHE_MIN_SOURCE_BYTES 4096u
#define PLAYLIST_DISK_CACHE_MIN_ENTRIES 96

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t source_size;
    int64_t source_mtime;
    uint64_t content_hash;
    uint32_t entry_count;
    int32_t viz_style;
    int32_t viz_intensity;
    int32_t text_panel_enabled;
    int32_t text_panel_alpha;
    int32_t grid_view_enabled;
} playlist_cache_header_t;

typedef struct {
    char *theme;
    char *bgm;
    char *bg;
    char *screensaver_logo;
    int viz_style;
    int viz_intensity;
    int text_panel_enabled;
    int text_panel_alpha;
    int grid_view;
} playlist_props_t;

#define PLAYLIST_PROPS_DEFAULT { NULL, NULL, NULL, NULL, -1, -1, -1, -1, -1 }

static void playlist_props_free(playlist_props_t *props) {
    if (!props) return;
    free(props->theme);
    free(props->bgm);
    free(props->bg);
    free(props->screensaver_logo);
    *props = (playlist_props_t)PLAYLIST_PROPS_DEFAULT;
}

typedef struct {
    bool valid;
    uint32_t last_used_tick;
    char *playlist_path;
    uint64_t content_hash;
    playlist_props_t props;
    int entry_count;
    char **entry_paths;
} playlist_mem_cache_entry_t;

static playlist_mem_cache_entry_t playlist_mem_cache[PLAYLIST_MEM_CACHE_ENTRIES];
static uint32_t playlist_mem_cache_tick = 1;
static bool playlist_active_loaded = false;
static char playlist_active_path[512];
static uint64_t playlist_active_content_hash = 0;
static bool playlist_active_static = false;
static playlist_props_t playlist_active_props = PLAYLIST_PROPS_DEFAULT;

static char playlist_recent_file_path[512];
static char playlist_recent_paths[PLAYLIST_RECENT_LIMIT][512];
static int playlist_recent_count = 0;
static int playlist_recent_prewarm_index = 0;
static int playlist_recent_prewarm_cooldown = 0;
static bool playlist_recent_loaded = false;

static bool string_ends_with_ignore_case(const char *value, const char *suffix) {
    if (!value || !suffix) {
        return false;
    }
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    if (suffix_len == 0 || suffix_len > value_len) {
        return false;
    }
    const char *tail = value + (value_len - suffix_len);
    for (size_t i = 0; i < suffix_len; i++) {
        char a = tail[i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static void browser_list_free(menu_t *menu);
static bool playlist_append_rom_entry(menu_t *menu, const char *normalized_path, int *capacity);
static bool playlist_prepend_text_entry(menu_t *menu, const char *entry_path, int *capacity);
static char *playlist_find_context_text_path(path_t *playlist_path);
static bool browser_reserve_entry_capacity(menu_t *menu, int *capacity, int extra_entries);
static char *trim_line(char *line);
static bool playlist_cache_read_string(FILE *f, char **out);
static char *playlist_cache_build_path(menu_t *menu, const char *playlist_path);
static bool playlist_should_use_disk_cache(size_t file_size, int entry_count);

static void playlist_mem_cache_entry_clear(playlist_mem_cache_entry_t *entry) {
    if (!entry) {
        return;
    }
    free(entry->playlist_path);
    playlist_props_free(&entry->props);
    if (entry->entry_paths) {
        for (int i = 0; i < entry->entry_count; i++) {
            free(entry->entry_paths[i]);
        }
    }
    free(entry->entry_paths);
    memset(entry, 0, sizeof(*entry));
}

static void playlist_active_clear(void) {
    playlist_active_loaded = false;
    playlist_active_path[0] = '\0';
    playlist_active_content_hash = 0;
    playlist_active_static = false;
    playlist_props_free(&playlist_active_props);
}

static void playlist_active_store(
    const char *playlist_path, uint64_t content_hash, bool is_static,
    const playlist_props_t *props
) {
    if (!playlist_path) {
        playlist_active_clear();
        return;
    }
    snprintf(playlist_active_path, sizeof(playlist_active_path), "%s", playlist_path);
    playlist_active_content_hash = content_hash;
    playlist_active_static = is_static;
    playlist_props_free(&playlist_active_props);
    playlist_active_props.theme = props->theme ? strdup(props->theme) : NULL;
    playlist_active_props.bgm = props->bgm ? strdup(props->bgm) : NULL;
    playlist_active_props.bg = props->bg ? strdup(props->bg) : NULL;
    playlist_active_props.screensaver_logo = props->screensaver_logo ? strdup(props->screensaver_logo) : NULL;
    playlist_active_props.viz_style = props->viz_style;
    playlist_active_props.viz_intensity = props->viz_intensity;
    playlist_active_props.text_panel_enabled = props->text_panel_enabled;
    playlist_active_props.text_panel_alpha = props->text_panel_alpha;
    playlist_active_props.grid_view = props->grid_view;
    playlist_active_loaded = true;
}

static playlist_mem_cache_entry_t *playlist_mem_cache_find(const char *playlist_path, uint64_t content_hash) {
    if (!playlist_path || !content_hash) {
        return NULL;
    }
    for (size_t i = 0; i < PLAYLIST_MEM_CACHE_ENTRIES; i++) {
        playlist_mem_cache_entry_t *entry = &playlist_mem_cache[i];
        if (!entry->valid || !entry->playlist_path) {
            continue;
        }
        if (strcmp(entry->playlist_path, playlist_path) != 0) {
            continue;
        }
        if (entry->content_hash != content_hash) {
            continue;
        }
        entry->last_used_tick = ++playlist_mem_cache_tick;
        return entry;
    }
    return NULL;
}

static playlist_mem_cache_entry_t *playlist_mem_cache_alloc(void) {
    playlist_mem_cache_entry_t *empty = NULL;
    playlist_mem_cache_entry_t *oldest = &playlist_mem_cache[0];
    for (size_t i = 0; i < PLAYLIST_MEM_CACHE_ENTRIES; i++) {
        playlist_mem_cache_entry_t *entry = &playlist_mem_cache[i];
        if (!entry->valid) {
            empty = entry;
            break;
        }
        if (entry->last_used_tick < oldest->last_used_tick) {
            oldest = entry;
        }
    }

    playlist_mem_cache_entry_t *slot = empty ? empty : oldest;
    playlist_mem_cache_entry_clear(slot);
    slot->valid = true;
    slot->last_used_tick = ++playlist_mem_cache_tick;
    return slot;
}

static bool playlist_mem_cache_store(
    const char *playlist_path,
    uint64_t content_hash,
    const playlist_props_t *props,
    const char * const *entry_paths,
    int entry_count
) {
    if (!playlist_path || entry_count < 0) {
        return false;
    }

    playlist_mem_cache_entry_t *entry = playlist_mem_cache_alloc();
    if (!entry) {
        return false;
    }

    entry->playlist_path = strdup(playlist_path);
    entry->props.theme = props->theme ? strdup(props->theme) : NULL;
    entry->props.bgm = props->bgm ? strdup(props->bgm) : NULL;
    entry->props.bg = props->bg ? strdup(props->bg) : NULL;
    entry->props.screensaver_logo = props->screensaver_logo ? strdup(props->screensaver_logo) : NULL;
    entry->props.viz_style = props->viz_style;
    entry->props.viz_intensity = props->viz_intensity;
    entry->props.text_panel_enabled = props->text_panel_enabled;
    entry->props.text_panel_alpha = props->text_panel_alpha;
    entry->props.grid_view = props->grid_view;
    entry->content_hash = content_hash;
    entry->entry_count = entry_count;

    if (!entry->playlist_path) {
        playlist_mem_cache_entry_clear(entry);
        return false;
    }

    if (entry_count > 0) {
        entry->entry_paths = calloc((size_t)entry_count, sizeof(char *));
        if (!entry->entry_paths) {
            playlist_mem_cache_entry_clear(entry);
            return false;
        }
        for (int i = 0; i < entry_count; i++) {
            entry->entry_paths[i] = entry_paths[i] ? strdup(entry_paths[i]) : strdup("");
            if (!entry->entry_paths[i]) {
                playlist_mem_cache_entry_clear(entry);
                return false;
            }
        }
    }

    return true;
}

static bool playlist_mem_cache_try_load(
    menu_t *menu,
    const char *playlist_path,
    uint64_t content_hash,
    int *playlist_capacity,
    playlist_props_t *props
) {
    if (!menu || !playlist_path) {
        return false;
    }

    playlist_mem_cache_entry_t *entry = playlist_mem_cache_find(playlist_path, content_hash);
    if (!entry) {
        return false;
    }

    props->theme = entry->props.theme ? strdup(entry->props.theme) : NULL;
    props->bgm = entry->props.bgm ? strdup(entry->props.bgm) : NULL;
    props->bg = entry->props.bg ? strdup(entry->props.bg) : NULL;
    props->screensaver_logo = entry->props.screensaver_logo ? strdup(entry->props.screensaver_logo) : NULL;
    props->viz_style = entry->props.viz_style;
    props->viz_intensity = entry->props.viz_intensity;
    props->text_panel_enabled = entry->props.text_panel_enabled;
    props->text_panel_alpha = entry->props.text_panel_alpha;
    props->grid_view = entry->props.grid_view;

    if (entry->entry_count > 0 && !browser_reserve_entry_capacity(menu, playlist_capacity, entry->entry_count)) {
        playlist_props_free(props);
        browser_list_free(menu);
        menu->browser.playlist = true;
        return false;
    }

    for (int i = 0; i < entry->entry_count; i++) {
        if (!playlist_append_rom_entry(menu, entry->entry_paths[i], playlist_capacity)) {
            playlist_props_free(props);
            browser_list_free(menu);
            menu->browser.playlist = true;
            return false;
        }
    }

    return true;
}

static void playlist_mem_cache_save(
    menu_t *menu,
    const char *playlist_path,
    uint64_t content_hash,
    const playlist_props_t *props
) {
    if (!menu || !playlist_path || !content_hash) {
        return;
    }

    const char **entry_paths = calloc((size_t)menu->browser.entries, sizeof(char *));
    if (!entry_paths && menu->browser.entries > 0) {
        return;
    }
    for (int i = 0; i < menu->browser.entries; i++) {
        entry_paths[i] = menu->browser.list[i].path ? menu->browser.list[i].path : "";
    }

    playlist_mem_cache_store(
        playlist_path,
        content_hash,
        props,
        entry_paths,
        menu->browser.entries
    );

    free(entry_paths);
}

static void playlist_recent_init(menu_t *menu) {
    if (!menu || playlist_recent_loaded) {
        return;
    }

    playlist_recent_file_path[0] = '\0';
    path_t *recent_path = path_init(menu->storage_prefix, PLAYLIST_RECENT_FILE);
    if (!recent_path) {
        return;
    }
    snprintf(playlist_recent_file_path, sizeof(playlist_recent_file_path), "%s", path_get(recent_path));
    path_free(recent_path);

    playlist_recent_count = 0;
    FILE *f = fopen(playlist_recent_file_path, "rb");
    if (f) {
        char line[512];
        while (playlist_recent_count < (int)PLAYLIST_RECENT_LIMIT && fgets(line, sizeof(line), f) != NULL) {
            char *trimmed = trim_line(line);
            if (!trimmed || trimmed[0] == '\0') {
                continue;
            }
            snprintf(playlist_recent_paths[playlist_recent_count], sizeof(playlist_recent_paths[playlist_recent_count]), "%s", trimmed);
            playlist_recent_count++;
        }
        fclose(f);
    }

    playlist_recent_prewarm_index = 0;
    playlist_recent_prewarm_cooldown = 3;
    playlist_recent_loaded = true;
}

static void playlist_recent_save(void) {
    if (!playlist_recent_loaded || !playlist_recent_file_path[0]) {
        return;
    }

    FILE *f = fopen(playlist_recent_file_path, "wb");
    if (!f) {
        return;
    }
    for (int i = 0; i < playlist_recent_count; i++) {
        if (playlist_recent_paths[i][0] == '\0') {
            continue;
        }
        fprintf(f, "%s\n", playlist_recent_paths[i]);
    }
    fclose(f);
}

static void playlist_recent_remember(const char *playlist_path) {
    if (!playlist_recent_loaded || !playlist_path || !playlist_path[0]) {
        return;
    }

    int found = -1;
    for (int i = 0; i < playlist_recent_count; i++) {
        if (strcmp(playlist_recent_paths[i], playlist_path) == 0) {
            found = i;
            break;
        }
    }

    if (found == 0) {
        return;
    }

    int limit = playlist_recent_count;
    if (found < 0 && limit < (int)PLAYLIST_RECENT_LIMIT) {
        limit++;
    }

    for (int i = (found > 0) ? found : limit - 1; i > 0; i--) {
        snprintf(playlist_recent_paths[i], sizeof(playlist_recent_paths[i]), "%s", playlist_recent_paths[i - 1]);
    }
    snprintf(playlist_recent_paths[0], sizeof(playlist_recent_paths[0]), "%s", playlist_path);
    playlist_recent_count = limit;

    playlist_recent_save();
    playlist_recent_prewarm_index = 0;
    playlist_recent_prewarm_cooldown = 3;
}

static bool playlist_mem_cache_prewarm_from_disk(menu_t *menu, const char *playlist_path) {
    if (!menu || !playlist_path || !playlist_path[0]) {
        return false;
    }

    char *cache_path = playlist_cache_build_path(menu, playlist_path);
    if (!cache_path) {
        return false;
    }

    FILE *f = fopen(cache_path, "rb");
    free(cache_path);
    if (!f) {
        return false;
    }

    playlist_cache_header_t header = {0};
    bool ok = (fread(&header, sizeof(header), 1, f) == 1);
    ok = ok && (header.magic == PLAYLIST_CACHE_MAGIC);
    ok = ok && (header.version == PLAYLIST_CACHE_VERSION);
    ok = ok && (header.content_hash != 0);
    ok = ok && (header.entry_count < 65536u);
    if (!ok) {
        fclose(f);
        return false;
    }

    if (playlist_mem_cache_find(playlist_path, header.content_hash)) {
        fclose(f);
        return true;
    }

    playlist_props_t props = PLAYLIST_PROPS_DEFAULT;
    char **entry_paths = NULL;

    ok = playlist_cache_read_string(f, &props.theme) &&
         playlist_cache_read_string(f, &props.bgm) &&
         playlist_cache_read_string(f, &props.bg) &&
         playlist_cache_read_string(f, &props.screensaver_logo);
    if (ok) {
        props.viz_style = header.viz_style;
        props.viz_intensity = header.viz_intensity;
        props.text_panel_enabled = header.text_panel_enabled;
        props.text_panel_alpha = header.text_panel_alpha;
        props.grid_view = header.grid_view_enabled;
    }
    if (ok && header.entry_count > 0) {
        entry_paths = calloc((size_t)header.entry_count, sizeof(char *));
        ok = (entry_paths != NULL);
    }
    for (uint32_t i = 0; ok && i < header.entry_count; i++) {
        ok = playlist_cache_read_string(f, &entry_paths[i]) && entry_paths[i] != NULL;
    }
    fclose(f);

    if (ok) {
        const char * const *paths_const = (const char * const *)entry_paths;
        ok = playlist_mem_cache_store(
            playlist_path,
            header.content_hash,
            &props,
            paths_const,
            (int)header.entry_count
        );
    }

    playlist_props_free(&props);
    if (entry_paths) {
        for (uint32_t i = 0; i < header.entry_count; i++) {
            free(entry_paths[i]);
        }
    }
    free(entry_paths);
    return ok;
}

static void playlist_recent_prewarm_tick(menu_t *menu) {
    if (!menu || !playlist_recent_loaded || playlist_recent_count <= 0) {
        return;
    }
    if (playlist_recent_prewarm_index >= playlist_recent_count) {
        return;
    }
    if (png_decoder_is_busy()) {
        return;
    }
    if (playlist_recent_prewarm_cooldown > 0) {
        playlist_recent_prewarm_cooldown--;
        return;
    }

    const char *path = playlist_recent_paths[playlist_recent_prewarm_index];
    if (path && path[0] != '\0') {
        playlist_mem_cache_prewarm_from_disk(menu, path);
    }
    playlist_recent_prewarm_index++;
    playlist_recent_prewarm_cooldown = 3;
}

static bool playlist_cache_write_string(FILE *f, const char *value) {
    uint32_t len = (value && value[0] != '\0') ? (uint32_t)strlen(value) : 0;
    if (fwrite(&len, sizeof(len), 1, f) != 1) {
        return false;
    }
    if (len > 0 && fwrite(value, 1, len, f) != len) {
        return false;
    }
    return true;
}

static bool playlist_cache_read_string(FILE *f, char **out) {
    uint32_t len = 0;
    if (out) {
        *out = NULL;
    }
    if (fread(&len, sizeof(len), 1, f) != 1) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (len > 65536u) {
        return false;
    }
    char *value = calloc(1, (size_t)len + 1);
    if (!value) {
        return false;
    }
    if (fread(value, 1, len, f) != len) {
        free(value);
        return false;
    }
    if (out) {
        *out = value;
    } else {
        free(value);
    }
    return true;
}

static char *playlist_cache_build_path(menu_t *menu, const char *playlist_path) {
    if (!menu || !playlist_path || !playlist_path[0]) {
        return NULL;
    }

    path_t *cache_dir = path_init(menu->storage_prefix, PLAYLIST_CACHE_DIR);
    if (!cache_dir) {
        return NULL;
    }
    if (directory_create(path_get(cache_dir))) {
        path_free(cache_dir);
        return NULL;
    }

    uint64_t hash = fnv1a64_str(playlist_path);
    char file_name[40];
    snprintf(file_name, sizeof(file_name), "pl_%016" PRIx64 ".cache", hash);
    path_push(cache_dir, file_name);

    char *out = strdup(path_get(cache_dir));
    path_free(cache_dir);
    return out;
}

static bool playlist_cache_try_load(
    menu_t *menu,
    const char *playlist_path,
    uint64_t content_hash,
    size_t file_size,
    int *playlist_capacity,
    playlist_props_t *props,
    const char **cache_source
) {
    if (!menu || !playlist_path || !content_hash) {
        return false;
    }

    if (playlist_mem_cache_try_load(
            menu,
            playlist_path,
            content_hash,
            playlist_capacity,
            props)) {
        if (cache_source) {
            *cache_source = "memory";
        }
        return true;
    }

    if (!playlist_should_use_disk_cache(file_size, 0)) {
        return false;
    }

    char *cache_path = playlist_cache_build_path(menu, playlist_path);
    if (!cache_path) {
        return false;
    }

    FILE *f = fopen(cache_path, "rb");
    free(cache_path);
    if (!f) {
        return false;
    }

    playlist_cache_header_t header = {0};
    bool ok = (fread(&header, sizeof(header), 1, f) == 1);
    ok = ok && (header.magic == PLAYLIST_CACHE_MAGIC);
    ok = ok && (header.version == PLAYLIST_CACHE_VERSION);
    ok = ok && (header.content_hash == content_hash);
    ok = ok && (header.entry_count < 65536u);
    if (!ok) {
        fclose(f);
        return false;
    }

    props->viz_style = header.viz_style;
    props->viz_intensity = header.viz_intensity;
    props->text_panel_enabled = header.text_panel_enabled;
    props->text_panel_alpha = header.text_panel_alpha;
    props->grid_view = header.grid_view_enabled;

    if (!playlist_cache_read_string(f, &props->theme) ||
        !playlist_cache_read_string(f, &props->bgm) ||
        !playlist_cache_read_string(f, &props->bg) ||
        !playlist_cache_read_string(f, &props->screensaver_logo)) {
        goto fail;
    }

    if (header.entry_count > 0 && !browser_reserve_entry_capacity(menu, playlist_capacity, (int)header.entry_count)) {
        goto fail;
    }

    for (uint32_t i = 0; i < header.entry_count; i++) {
        char *entry_path = NULL;
        if (!playlist_cache_read_string(f, &entry_path) || !entry_path) {
            free(entry_path);
            goto fail;
        }
        if (!playlist_append_rom_entry(menu, entry_path, playlist_capacity)) {
            free(entry_path);
            goto fail;
        }
        free(entry_path);
    }

    fclose(f);
    playlist_mem_cache_save(
        menu,
        playlist_path,
        content_hash,
        props
    );
    if (cache_source) {
        *cache_source = "disk";
    }
    return true;

fail:
    fclose(f);
    playlist_props_free(props);
    browser_list_free(menu);
    menu->browser.playlist = true;
    return false;
}

static void playlist_cache_save(
    menu_t *menu,
    const char *playlist_path,
    uint64_t content_hash,
    size_t file_size,
    const playlist_props_t *props
) {
    if (!menu || !playlist_path || !content_hash || menu->browser.entries < 0) {
        return;
    }

    if (!playlist_should_use_disk_cache(file_size, menu->browser.entries)) {
        return;
    }

    char *cache_path = playlist_cache_build_path(menu, playlist_path);
    if (!cache_path) {
        return;
    }

    FILE *f = fopen(cache_path, "wb");
    free(cache_path);
    if (!f) {
        return;
    }

    playlist_cache_header_t header = {
        .magic = PLAYLIST_CACHE_MAGIC,
        .version = PLAYLIST_CACHE_VERSION,
        .source_size = (uint64_t)file_size,
        .source_mtime = 0,
        .content_hash = content_hash,
        .entry_count = (uint32_t)menu->browser.entries,
        .viz_style = props->viz_style,
        .viz_intensity = props->viz_intensity,
        .text_panel_enabled = props->text_panel_enabled,
        .text_panel_alpha = props->text_panel_alpha,
        .grid_view_enabled = props->grid_view,
    };

    bool ok = (fwrite(&header, sizeof(header), 1, f) == 1);
    ok = ok && playlist_cache_write_string(f, props->theme);
    ok = ok && playlist_cache_write_string(f, props->bgm);
    ok = ok && playlist_cache_write_string(f, props->bg);
    ok = ok && playlist_cache_write_string(f, props->screensaver_logo);

    for (int i = 0; ok && i < menu->browser.entries; i++) {
        const char *entry_path = menu->browser.list[i].path ? menu->browser.list[i].path : "";
        ok = playlist_cache_write_string(f, entry_path);
    }

    if (fclose(f) != 0) {
        ok = false;
    }

    if (!ok) {
        char *cache_path_retry = playlist_cache_build_path(menu, playlist_path);
        if (cache_path_retry) {
            remove(cache_path_retry);
            free(cache_path_retry);
        }
    }
}

struct substr { const char *str; size_t len; };
#define substr(str) ((struct substr){ str, sizeof(str) - 1 })

static const struct substr hidden_basenames[] = {
    substr("desktop.ini"), // Windows Explorer settings
    substr("Thumbs.db"),   // Windows Explorer thumbnails
    substr(".DS_Store"),   // macOS Finder settings
};
#define HIDDEN_BASENAMES_COUNT (sizeof(hidden_basenames) / sizeof(hidden_basenames[0]))

static const struct substr hidden_prefixes[] = {
    substr("._"), // macOS "AppleDouble" metadata files
};
#define HIDDEN_PREFIXES_COUNT (sizeof(hidden_prefixes) / sizeof(hidden_prefixes[0]))

static uint32_t random_entry_state = 0x9E3779B9u;

typedef enum {
    RANDOM_MODE_ANY_GAME = 0,
    RANDOM_MODE_UNPLAYED = 1,
    RANDOM_MODE_UNDERPLAYED = 2,
    RANDOM_MODE_FAVORITES = 3,
    RANDOM_MODE_SMART = 4,
} browser_random_mode_t;

typedef struct {
    int index;
    uint64_t total_seconds;
    int64_t last_played;
    uint32_t play_count;
    bool favorite;
    uint64_t smart_score;
} random_candidate_t;

#define SMART_PLAYLIST_MAX_ROOTS 8

typedef enum {
    SMART_PLAYLIST_SORT_TITLE = 0,
    SMART_PLAYLIST_SORT_YEAR,
    SMART_PLAYLIST_SORT_PUBLISHER,
    SMART_PLAYLIST_SORT_RANDOM,
} smart_playlist_sort_t;

typedef struct {
    bool enabled;
    char roots[SMART_PLAYLIST_MAX_ROOTS][512];
    int root_count;
    char title_contains[96];
    char publisher_contains[96];
    char developer_contains[96];
    char genre_contains[64];
    char series_contains[64];
    char modes_contains[96];
    char description_contains[128];
    bool filter_year;
    int year_min;
    int year_max;
    bool filter_age;
    int age_min;
    int age_max;
    bool filter_players;
    int players_min;
    int players_max;
    bool filter_region;
    char region_code;
    smart_playlist_sort_t sort;
} smart_playlist_query_t;

typedef struct {
    char *path;
    char *name;
    char title[ROM_METADATA_NAME_LENGTH];
    char publisher[ROM_METADATA_AUTHOR_LENGTH];
    int year;
    uint32_t random_key;
} smart_playlist_entry_t;

static const smart_playlist_query_t *smart_playlist_sort_query = NULL;

static char *normalize_path (const char *path);
static char *trim_line (char *line);
static const char *format_clock_12h(time_t now, char *buffer, size_t buffer_len);
static int random_candidate_compare_underplayed(const void *a, const void *b);
static int random_candidate_compare_smart(const void *a, const void *b);
static bool browser_entry_is_game(const entry_t *entry);
static char *browser_entry_path(menu_t *menu, int index);
static bool path_is_favorite(menu_t *menu, const char *path);
static int browser_pick_random_index(menu_t *menu);
static bool browser_picker_is_active(menu_t *menu);
static bool browser_is_picker_root(menu_t *menu);
static void browser_close_picker(menu_t *menu, menu_mode_t next_mode);
static bool browser_try_pick_menu_music_file(menu_t *menu);
static bool browser_try_pick_screensaver_logo_file(menu_t *menu);
static bool browser_try_pick_64dd_disk_file(menu_t *menu);
static bool browser_picker_is_64dd_disk(menu_t *menu);
static bool browser_64dd_picker_entry_visible(menu_t *menu, path_t *directory, dir_t *info);
static void browser_restore_playlist_overrides(menu_t *menu);
static void browser_apply_playlist_overrides(menu_t *menu, const playlist_props_t *props);
static void browser_apply_playlist_overrides_deferred(menu_t *menu);
static bool browser_use_playlist_grid(menu_t *menu);
static void browser_playlist_grid_prepare(menu_t *menu, bool defer_work);
static void browser_playlist_grid_draw(menu_t *menu);
static bool playlist_append_rom_entry(menu_t *menu, const char *normalized_path, int *capacity);
static bool playlist_prepend_text_entry(menu_t *menu, const char *entry_path, int *capacity);
static char *playlist_find_context_text_path(path_t *playlist_path);
static bool playlist_append_rom_entry_unique(menu_t *menu, const char *normalized_path, int *capacity);
static char *playlist_cache_build_path(menu_t *menu, const char *playlist_path);
static bool playlist_cache_try_load(menu_t *menu, const char *playlist_path,
    uint64_t content_hash, size_t file_size, int *playlist_capacity,
    playlist_props_t *props, const char **cache_source);
static void playlist_cache_save(menu_t *menu, const char *playlist_path,
    uint64_t content_hash, size_t file_size, const playlist_props_t *props);

typedef struct {
    bool active;
    bool theme_applied;
    int saved_theme;
    bool bgm_applied;
    char *pending_bgm_path;
    bool text_panel_applied;
    bool saved_text_panel_enabled;
    uint8_t saved_text_panel_alpha;
    bool screensaver_logo_applied;
    char *pending_screensaver_logo_path;
    char *saved_screensaver_logo_file;
    bool viz_style_applied;
    int saved_viz_style;
    bool viz_intensity_applied;
    int saved_viz_intensity;
    bool background_applied;
    bool background_loading;
    char *background_path;
    bool background_deferred;
    bool grid_view_applied;
    bool saved_grid_view_enabled;
    int deferred_heavy_frames;
} playlist_override_state_t;

static playlist_override_state_t playlist_override = {0};

typedef struct {
    int frames_left;
    char line1[64];
    char line2[128];
} playlist_toast_t;

typedef struct {
    bool valid;
    char source[12];
    uint32_t open_ms;
    uint32_t parse_ms;
    uint32_t smart_ms;
    uint32_t cache_save_ms;
    uint32_t bg_cache_ms;
    uint32_t bg_decode_queue_ms;
    uint32_t bgm_reload_ms;
    uint32_t logo_reload_ms;
    int entries;
} playlist_perf_t;

static playlist_toast_t playlist_toast = {0};
static playlist_perf_t playlist_perf = {0};
static bool playlist_grid_view_enabled = false;
static int playlist_grid_runtime_override = -1; // -1=playlist/default, 0=list, 1=grid

static uint32_t elapsed_ms(uint64_t start_us) {
    uint64_t now_us = get_ticks_us();
    if (now_us <= start_us) {
        return 0;
    }
    return (uint32_t)((now_us - start_us) / 1000ULL);
}

static void playlist_perf_reset(void) {
    memset(&playlist_perf, 0, sizeof(playlist_perf));
}

static void playlist_toast_show_perf(menu_t *menu) {
    if (!menu || !playlist_perf.valid) {
        return;
    }

    const char *title = menu->browser.directory ? file_basename(path_get(menu->browser.directory)) : "Playlist";
    snprintf(playlist_toast.line1, sizeof(playlist_toast.line1), "%s", title ? title : "Playlist");

    char extra[48] = "";
    if (playlist_perf.smart_ms > 0) {
        snprintf(extra, sizeof(extra), " | smart %lums", (unsigned long)playlist_perf.smart_ms);
    } else if (playlist_perf.parse_ms > 0) {
        snprintf(extra, sizeof(extra), " | parse %lums", (unsigned long)playlist_perf.parse_ms);
    }

    snprintf(
        playlist_toast.line2,
        sizeof(playlist_toast.line2),
        "%s %lums | %d entries%s",
        playlist_perf.source[0] ? playlist_perf.source : "open",
        (unsigned long)playlist_perf.open_ms,
        playlist_perf.entries,
        extra
    );
    playlist_toast.frames_left = 180;
}

static void playlist_perf_commit(menu_t *menu, const char *source, uint32_t open_ms, uint32_t parse_ms, uint32_t smart_ms, uint32_t cache_save_ms, int entries) {
    playlist_perf.valid = true;
    snprintf(playlist_perf.source, sizeof(playlist_perf.source), "%s", source ? source : "open");
    playlist_perf.open_ms = open_ms;
    playlist_perf.parse_ms = parse_ms;
    playlist_perf.smart_ms = smart_ms;
    playlist_perf.cache_save_ms = cache_save_ms;
    playlist_perf.entries = entries;
    playlist_perf.bgm_reload_ms = 0;
    playlist_perf.logo_reload_ms = 0;
    playlist_perf.bg_cache_ms = 0;
    playlist_perf.bg_decode_queue_ms = 0;

    debugf(
        "Playlist perf: %s open=%lums parse=%lums smart=%lums cache_save=%lums entries=%d path=%s\n",
        playlist_perf.source,
        (unsigned long)playlist_perf.open_ms,
        (unsigned long)playlist_perf.parse_ms,
        (unsigned long)playlist_perf.smart_ms,
        (unsigned long)playlist_perf.cache_save_ms,
        playlist_perf.entries,
        menu && menu->browser.directory ? path_get(menu->browser.directory) : "(null)"
    );

    playlist_toast_show_perf(menu);
}

static bool playlist_should_use_disk_cache(size_t file_size, int entry_count) {
    if (file_size < PLAYLIST_DISK_CACHE_MIN_SOURCE_BYTES) {
        return false;
    }
    if (entry_count > 0 && entry_count < PLAYLIST_DISK_CACHE_MIN_ENTRIES) {
        return false;
    }
    return true;
}

typedef struct {
    int entry_index;
    char *entry_path;
    component_boxart_t *boxart;
    bool boxart_resolved;
    uint32_t last_used_frame;
} playlist_grid_thumb_slot_t;

#define PLAYLIST_GRID_VISIBLE_SLOTS 12
#define PLAYLIST_GRID_THUMB_SLOTS 25
static playlist_grid_thumb_slot_t playlist_grid_slots[PLAYLIST_GRID_THUMB_SLOTS];
// Reverse lookup: entry_index → slot_index. -1 = not cached.
// Sized to max playlist entries (64 is plenty).
#define PLAYLIST_GRID_MAX_ENTRIES 64
static int8_t playlist_grid_entry_to_slot[PLAYLIST_GRID_MAX_ENTRIES];
static entry_t *playlist_grid_slots_list = NULL;
static bool playlist_grid_page_mem_warm_done = false;
static uint32_t playlist_grid_frame_counter = 0;

typedef struct {
    bool attempted;
    bool loaded;
    char game_code[4];
    char rom_title[21];
} playlist_grid_meta_index_entry_t;

static playlist_grid_meta_index_entry_t *playlist_grid_meta_index = NULL;
static int playlist_grid_meta_index_count = 0;
static entry_t *playlist_grid_meta_index_list = NULL;

static void playlist_grid_slots_clear(void) {
    for (int i = 0; i < PLAYLIST_GRID_THUMB_SLOTS; i++) {
        if (playlist_grid_slots[i].boxart) {
            ui_components_boxart_free(playlist_grid_slots[i].boxart);
        }
        free(playlist_grid_slots[i].entry_path);
        memset(&playlist_grid_slots[i], 0, sizeof(playlist_grid_slots[i]));
        playlist_grid_slots[i].entry_index = -1;
    }
    memset(playlist_grid_entry_to_slot, -1, sizeof(playlist_grid_entry_to_slot));
    playlist_grid_slots_list = NULL;
    playlist_grid_page_mem_warm_done = false;
    playlist_grid_frame_counter = 0;
}

// Find the slot currently holding this entry_index, or -1. O(1) via reverse lookup.
static int playlist_grid_slot_find(int entry_index) {
    if (entry_index < 0 || entry_index >= PLAYLIST_GRID_MAX_ENTRIES) {
        return -1;
    }
    return playlist_grid_entry_to_slot[entry_index];
}

// Find a free slot, or evict the least-recently-used one.
static int playlist_grid_slot_alloc(void) {
    int best = -1;
    uint32_t best_frame = UINT32_MAX;
    for (int i = 0; i < PLAYLIST_GRID_THUMB_SLOTS; i++) {
        if (playlist_grid_slots[i].entry_index < 0) {
            return i;
        }
        if (playlist_grid_slots[i].last_used_frame < best_frame) {
            best_frame = playlist_grid_slots[i].last_used_frame;
            best = i;
        }
    }
    // Evict LRU slot — clear reverse lookup for old entry.
    if (best >= 0) {
        int old_ei = playlist_grid_slots[best].entry_index;
        if (old_ei >= 0 && old_ei < PLAYLIST_GRID_MAX_ENTRIES) {
            playlist_grid_entry_to_slot[old_ei] = -1;
        }
        if (playlist_grid_slots[best].boxart) {
            ui_components_boxart_free(playlist_grid_slots[best].boxart);
        }
        free(playlist_grid_slots[best].entry_path);
        memset(&playlist_grid_slots[best], 0, sizeof(playlist_grid_slots[best]));
        playlist_grid_slots[best].entry_index = -1;
    }
    return best;
}

static void playlist_grid_meta_index_clear(void) {
    free(playlist_grid_meta_index);
    playlist_grid_meta_index = NULL;
    playlist_grid_meta_index_count = 0;
    playlist_grid_meta_index_list = NULL;
}

static void playlist_grid_meta_index_reset_for_current_list(menu_t *menu) {
    if (!menu) {
        playlist_grid_meta_index_clear();
        return;
    }
    if (playlist_grid_meta_index_list == menu->browser.list &&
        playlist_grid_meta_index_count == menu->browser.entries) {
        return;
    }
    playlist_grid_meta_index_clear();
    if (!menu->browser.playlist || menu->browser.entries <= 0 || !menu->browser.list) {
        return;
    }
    playlist_grid_meta_index = calloc((size_t)menu->browser.entries, sizeof(*playlist_grid_meta_index));
    if (!playlist_grid_meta_index) {
        return;
    }
    playlist_grid_meta_index_count = menu->browser.entries;
    playlist_grid_meta_index_list = menu->browser.list;
}

static bool playlist_grid_get_boxart_meta_by_index(menu_t *menu, int entry_index, char game_code_out[4], char rom_title_out[21]) {
    if (!menu || entry_index < 0 || entry_index >= menu->browser.entries || !menu->browser.list) {
        return false;
    }

    playlist_grid_meta_index_reset_for_current_list(menu);
    if (!playlist_grid_meta_index || entry_index >= playlist_grid_meta_index_count) {
        return false;
    }

    playlist_grid_meta_index_entry_t *idx = &playlist_grid_meta_index[entry_index];
    if (idx->attempted) {
        if (!idx->loaded) {
            return false;
        }
        memcpy(game_code_out, idx->game_code, 4);
        memcpy(rom_title_out, idx->rom_title, 21);
        return true;
    }

    idx->attempted = true;
    entry_t *entry = &menu->browser.list[entry_index];
    if (!entry->path || entry->type != ENTRY_TYPE_ROM) {
        idx->loaded = false;
        return false;
    }

    char quick_game_code[4];
    char quick_title[21];
    if (rom_info_read_quick(entry->path, quick_game_code, quick_title) != ROM_OK) {
        idx->loaded = false;
        return false;
    }

    idx->loaded = true;
    memcpy(idx->game_code, quick_game_code, 4);
    memcpy(idx->rom_title, quick_title, 21);

    memcpy(game_code_out, idx->game_code, 4);
    memcpy(rom_title_out, idx->rom_title, 21);
    return true;
}

static bool playlist_grid_get_boxart_meta_by_index_cached_only(menu_t *menu, int entry_index, char game_code_out[4], char rom_title_out[21]) {
    if (!menu || entry_index < 0 || entry_index >= menu->browser.entries) {
        return false;
    }
    playlist_grid_meta_index_reset_for_current_list(menu);
    if (!playlist_grid_meta_index || entry_index >= playlist_grid_meta_index_count) {
        return false;
    }

    playlist_grid_meta_index_entry_t *idx = &playlist_grid_meta_index[entry_index];
    if (!idx->attempted || !idx->loaded) {
        return false;
    }

    memcpy(game_code_out, idx->game_code, 4);
    memcpy(rom_title_out, idx->rom_title, 21);
    return true;
}

// Incremental prewarm: reads a few entries per frame to avoid blocking on
// playlist load.  Call _start once, then _tick every frame.
static int prewarm_next_index = -1;
static int prewarm_total = 0;

static void playlist_grid_prewarm_start(menu_t *menu) {
    prewarm_next_index = -1;
    prewarm_total = 0;
    if (!menu || !menu->browser.playlist || menu->browser.entries <= 0) {
        return;
    }
    playlist_grid_meta_index_reset_for_current_list(menu);
    if (!playlist_grid_meta_index) {
        return;
    }
    prewarm_next_index = 0;
    prewarm_total = menu->browser.entries;
}

#define PREWARM_PER_FRAME 2

static void playlist_grid_prewarm_tick(menu_t *menu) {
    if (prewarm_next_index < 0 || prewarm_next_index >= prewarm_total) {
        return;
    }
    char gc[4], title[21];
    for (int i = 0; i < PREWARM_PER_FRAME && prewarm_next_index < prewarm_total; i++, prewarm_next_index++) {
        if (playlist_grid_get_boxart_meta_by_index(menu, prewarm_next_index, gc, title)) {
            ui_components_boxart_prewarm_dir(menu->storage_prefix, gc, title);
        }
    }
}

static const char *browser_grid_display_name(const char *name, char *buffer, size_t buffer_size) {
    if (!name || !buffer || buffer_size == 0) {
        return "";
    }
    snprintf(buffer, buffer_size, "%s", name);

    char *dot = strrchr(buffer, '.');
    if (dot) {
        *dot = '\0';
    }

    for (char *p = buffer; *p; p++) {
        if (*p == '_') {
            *p = ' ';
        }
    }

    // If the filename starts with an article marker or long region suffixes, leave them; just trim double spaces.
    for (char *p = buffer; *p; p++) {
        if (*p == ' ' && p[1] == ' ') {
            memmove(p, p + 1, strlen(p));
            p--;
        }
    }
    return buffer;
}

// Register entry→slot mapping in the reverse lookup table.
static void playlist_grid_slot_register(int entry_index, int slot_index) {
    if (entry_index >= 0 && entry_index < PLAYLIST_GRID_MAX_ENTRIES) {
        playlist_grid_entry_to_slot[entry_index] = (int8_t)slot_index;
    }
}

// Returns the slot index used, or -1 on failure.
static int playlist_grid_slot_prepare(menu_t *menu, int entry_index, bool memory_cache_only) {
    if (!menu || entry_index < 0 || entry_index >= menu->browser.entries ||
        entry_index >= PLAYLIST_GRID_MAX_ENTRIES) {
        return -1;
    }

    entry_t *entry = &menu->browser.list[entry_index];
    if (entry->type != ENTRY_TYPE_ROM || !entry->path) {
        return -1;
    }

    // Check if this entry already has a slot.
    int si = playlist_grid_slot_find(entry_index);
    if (si >= 0) {
        playlist_grid_thumb_slot_t *slot = &playlist_grid_slots[si];
        slot->last_used_frame = playlist_grid_frame_counter;
        if (slot->boxart_resolved || memory_cache_only) {
            return si;
        }
        // Slot exists but needs full resolution — fall through.
    } else {
        if (memory_cache_only) {
            char game_code[4];
            char safe_title[21];
            if (!playlist_grid_get_boxart_meta_by_index_cached_only(menu, entry_index, game_code, safe_title)) {
                return -1;
            }
            si = playlist_grid_slot_alloc();
            if (si < 0) return -1;
            playlist_grid_thumb_slot_t *slot = &playlist_grid_slots[si];
            slot->entry_index = entry_index;
            slot->entry_path = strdup(entry->path);
            slot->last_used_frame = playlist_grid_frame_counter;
            slot->boxart = ui_components_boxart_init_grid_memory_cached(menu->storage_prefix, game_code, safe_title);
            playlist_grid_slot_register(entry_index, si);
            return si;
        }
        si = playlist_grid_slot_alloc();
        if (si < 0) return -1;
        playlist_grid_thumb_slot_t *slot = &playlist_grid_slots[si];
        slot->entry_index = entry_index;
        slot->entry_path = strdup(entry->path);
        slot->last_used_frame = playlist_grid_frame_counter;
        playlist_grid_slot_register(entry_index, si);
    }

    // Full resolution (does I/O).
    playlist_grid_thumb_slot_t *slot = &playlist_grid_slots[si];
    char game_code[4];
    char safe_title[21];
    bool have_meta = playlist_grid_get_boxart_meta_by_index(menu, entry_index, game_code, safe_title);
    if (have_meta) {
        if (slot->boxart) {
            ui_components_boxart_free(slot->boxart);
        }
        slot->boxart = ui_components_boxart_init_grid(menu->storage_prefix, game_code, safe_title);
    }
    slot->boxart_resolved = true;
    return si;
}

static bool path_is_hidden (path_t *path) {
    char *stripped_path = strip_fs_prefix(path_get(path));

    // Check for hidden files based on full path
    for (size_t i = 0; hidden_root_paths[i] != NULL; i++) {
        if (strcmp(stripped_path, hidden_root_paths[i]) == 0) {
            return true;
        }
    }

    char *basename = file_basename(stripped_path);
    size_t basename_len = strlen(basename);

    // Check for hidden files based on filename
    for (size_t i = 0; i < HIDDEN_BASENAMES_COUNT; i++) {
        if (basename_len == hidden_basenames[i].len &&
            strncmp(basename, hidden_basenames[i].str, hidden_basenames[i].len) == 0) {
            return true;
        }
    }
    // Check for hidden files based on filename prefix
    for (size_t i = 0; i < HIDDEN_PREFIXES_COUNT; i++) {
        if (basename_len > hidden_prefixes[i].len &&
            strncmp(basename, hidden_prefixes[i].str, hidden_prefixes[i].len) == 0) {
            return true;
        }
    }

    return false;
}

static const uint8_t entry_type_sort_priority[] = {
    [ENTRY_TYPE_DIR]       = 0,
    [ENTRY_TYPE_ARCHIVE]   = 1,
    [ENTRY_TYPE_DISK]      = 2,
    [ENTRY_TYPE_EMULATOR]  = 3,
    [ENTRY_TYPE_IMAGE]     = 4,
    [ENTRY_TYPE_MUSIC]     = 5,
    [ENTRY_TYPE_ROM]       = 6,
    [ENTRY_TYPE_ROM_CHEAT] = 7,
    [ENTRY_TYPE_ROM_PATCH] = 8,
    [ENTRY_TYPE_PLAYLIST]  = 9,
    [ENTRY_TYPE_SAVE]      = 10,
    [ENTRY_TYPE_TEXT]       = 11,
    [ENTRY_TYPE_OTHER]     = 12,
    [ENTRY_TYPE_ARCHIVED]  = 13,
};

static int compare_entry (const void *pa, const void *pb) {
    const entry_t *a = (const entry_t *) (pa);
    const entry_t *b = (const entry_t *) (pb);

    int pri_a = entry_type_sort_priority[a->type];
    int pri_b = entry_type_sort_priority[b->type];
    if (pri_a != pri_b) {
        return pri_a - pri_b;
    }

    return strcasecmp((const char *) (a->name), (const char *) (b->name));
}

static bool browser_picker_is_active(menu_t *menu) {
    return menu && menu->browser.picker != BROWSER_PICKER_NONE;
}

static bool browser_is_picker_root(menu_t *menu) {
    if (!browser_picker_is_active(menu) || !menu->browser.directory || !menu->browser.picker_root) {
        return false;
    }
    return path_are_match(menu->browser.directory, menu->browser.picker_root);
}

static void browser_close_picker(menu_t *menu, menu_mode_t next_mode) {
    if (!menu) {
        return;
    }

    menu->browser.picker = BROWSER_PICKER_NONE;
    menu->browser.picker_return_mode = MENU_MODE_BROWSER;
    if (menu->browser.picker_root) {
        path_free(menu->browser.picker_root);
        menu->browser.picker_root = NULL;
    }
    menu->next_mode = next_mode;
}

static bool browser_try_pick_menu_music_file(menu_t *menu) {
    if (!menu || menu->browser.picker != BROWSER_PICKER_MENU_BGM || !menu->browser.entry) {
        return false;
    }

    if (menu->browser.entry->type != ENTRY_TYPE_MUSIC) {
        if (menu->browser.entry->type != ENTRY_TYPE_DIR) {
            menu_show_error(menu, "Select an MP3 file");
        }
        return false;
    }

    char *entry_path = browser_entry_path(menu, menu->browser.selected);
    if (!entry_path) {
        menu_show_error(menu, "Failed to resolve file path");
        return true;
    }
    if (menu->settings.bgm_file) {
        free(menu->settings.bgm_file);
    }
    menu->settings.bgm_file = strdup(strip_fs_prefix(entry_path));
    free(entry_path);
    menu->bgm_reload_requested = true;
    settings_save(&menu->settings);
    browser_close_picker(menu, MENU_MODE_SETTINGS_EDITOR);
    return true;
}

static bool browser_try_pick_screensaver_logo_file(menu_t *menu) {
    if (!menu || menu->browser.picker != BROWSER_PICKER_SCREENSAVER_LOGO || !menu->browser.entry) {
        return false;
    }

    if (menu->browser.entry->type != ENTRY_TYPE_IMAGE) {
        if (menu->browser.entry->type != ENTRY_TYPE_DIR) {
            menu_show_error(menu, "Select a PNG file");
        }
        return false;
    }

    char *entry_path = browser_entry_path(menu, menu->browser.selected);
    if (!entry_path) {
        menu_show_error(menu, "Failed to resolve file path");
        return true;
    }
    if (menu->settings.screensaver_logo_file) {
        free(menu->settings.screensaver_logo_file);
    }
    menu->settings.screensaver_logo_file = strdup(strip_fs_prefix(entry_path));
    free(entry_path);
    menu->screensaver_logo_reload_requested = true;
    settings_save(&menu->settings);
    browser_close_picker(menu, MENU_MODE_SETTINGS_EDITOR);
    return true;
}

static bool browser_try_pick_64dd_disk_file(menu_t *menu) {
    if (!menu || !menu->browser.entry) {
        return false;
    }
    if (menu->browser.picker != BROWSER_PICKER_64DD_DISK_LAUNCH &&
        menu->browser.picker != BROWSER_PICKER_64DD_DISK_DEFAULT) {
        return false;
    }

    if (menu->browser.entry->type != ENTRY_TYPE_DISK) {
        if (menu->browser.entry->type != ENTRY_TYPE_DIR) {
            menu_show_error(menu, "Select a 64DD disk file");
        }
        return false;
    }
    if (!menu->load.rom_path || !path_has_value(menu->load.rom_path)) {
        menu_show_error(menu, "Couldn't locate ROM for 64DD pairing");
        return true;
    }

    char *entry_path = browser_entry_path(menu, menu->browser.selected);
    if (!entry_path) {
        menu_show_error(menu, "Failed to resolve file path");
        return true;
    }

    path_t *disk_path = path_create(entry_path);
    free(entry_path);
    if (!disk_path) {
        menu_show_error(menu, "Failed to allocate disk path");
        return true;
    }

    disk_info_t disk_info;
    disk_err_t err = disk_info_load(disk_path, &disk_info);
    if (err != DISK_OK) {
        path_free(disk_path);
        menu_show_error(menu, err == DISK_ERR_INVALID ? "Invalid 64DD disk file" : "Couldn't open 64DD disk file");
        return true;
    }

    if (!disk_pairing_region_matches_rom(&menu->load.rom_info, &disk_info)) {
        path_free(disk_path);
        menu_show_error(menu, "Disk region doesn't match this ROM");
        return true;
    }
    if (!disk_pairing_disk_id_matches_rom_game_code(menu->load.rom_info.game_code, disk_info.id)) {
        path_free(disk_path);
        menu_show_error(menu, "Disk isn't compatible with this ROM");
        return true;
    }

    rom_config_setting_set_default_disk_path(menu->load.rom_path, &menu->load.rom_info, strip_fs_prefix(path_get(disk_path)));

    if (menu->browser.picker == BROWSER_PICKER_64DD_DISK_DEFAULT) {
        path_free(disk_path);
        browser_close_picker(menu, MENU_MODE_LOAD_ROM);
        return true;
    }

    path_free(menu->load.disk_slots.primary.disk_path);
    menu->load.disk_slots.primary.disk_path = disk_path;
    menu->load.combined_disk_rom = true;
    menu->load.back_mode = MENU_MODE_LOAD_ROM;
    menu->load_pending.disk_file = true;
    browser_close_picker(menu, MENU_MODE_LOAD_DISK);
    return true;
}

static bool browser_picker_is_64dd_disk(menu_t *menu) {
    return menu &&
        (menu->browser.picker == BROWSER_PICKER_64DD_DISK_LAUNCH ||
         menu->browser.picker == BROWSER_PICKER_64DD_DISK_DEFAULT);
}

static bool browser_64dd_picker_entry_visible(menu_t *menu, path_t *directory, dir_t *info) {
    if (!browser_picker_is_64dd_disk(menu) || !directory || !info) {
        return true;
    }

    path_t *candidate = path_clone_push(directory, info->d_name);
    if (!candidate) {
        return false;
    }

    bool visible;
    if (info->d_type == DT_DIR) {
        visible = disk_pairing_directory_has_match_recursive(&menu->load.rom_info, candidate);
    } else {
        visible = disk_pairing_path_matches_rom(&menu->load.rom_info, candidate);
    }
    path_free(candidate);
    return visible;
}

static int compare_entry_reverse (const void *pa, const void *pb) {
    return compare_entry(pb, pa);
}

static void browser_apply_sort (menu_t *menu) {
    if ((menu->browser.entries <= 1) || (menu->browser.list == NULL)) {
        return;
    }

    entry_t *selected_entry = menu->browser.entry;
    const char *selected_name = (selected_entry != NULL) ? selected_entry->name : NULL;
    const char *selected_path = (selected_entry != NULL) ? selected_entry->path : NULL;
    entry_type_t selected_type = (selected_entry != NULL) ? selected_entry->type : ENTRY_TYPE_OTHER;

    switch (menu->browser.sort_mode) {
        case BROWSER_SORT_AZ:
            qsort(menu->browser.list, menu->browser.entries, sizeof(entry_t), compare_entry);
            break;
        case BROWSER_SORT_ZA:
            qsort(menu->browser.list, menu->browser.entries, sizeof(entry_t), compare_entry_reverse);
            break;
        case BROWSER_SORT_CUSTOM:
        default:
            break;
    }

    if (menu->browser.playlist) {
        playlist_grid_meta_index_clear();
        playlist_grid_slots_clear();
    }

    if (selected_name == NULL) {
        return;
    }

    for (int32_t i = 0; i < menu->browser.entries; i++) {
        entry_t *entry = &menu->browser.list[i];
        if (entry->type != selected_type) {
            continue;
        }
        if (strcmp(entry->name, selected_name) != 0) {
            continue;
        }
        if (((entry->path == NULL) && (selected_path == NULL)) ||
            ((entry->path != NULL) && (selected_path != NULL) && strcmp(entry->path, selected_path) == 0)) {
            menu->browser.selected = i;
            menu->browser.entry = entry;
            return;
        }
    }

    if (menu->browser.selected >= menu->browser.entries) {
        menu->browser.selected = menu->browser.entries - 1;
    }
    if (menu->browser.selected < 0) {
        menu->browser.selected = 0;
    }
    menu->browser.entry = &menu->browser.list[menu->browser.selected];
}

static const char *browser_sort_mode_string (menu_t *menu) {
    switch (menu->browser.sort_mode) {
        case BROWSER_SORT_CUSTOM: return "Custom";
        case BROWSER_SORT_AZ: return "A-Z";
        case BROWSER_SORT_ZA: return "Z-A";
        default: return "A-Z";
    }
}

static int random_candidate_compare_underplayed(const void *a, const void *b) {
    const random_candidate_t *lhs = (const random_candidate_t *)a;
    const random_candidate_t *rhs = (const random_candidate_t *)b;
    if (lhs->total_seconds < rhs->total_seconds) return -1;
    if (lhs->total_seconds > rhs->total_seconds) return 1;
    return lhs->index - rhs->index;
}

static int random_candidate_compare_smart(const void *a, const void *b) {
    const random_candidate_t *lhs = (const random_candidate_t *)a;
    const random_candidate_t *rhs = (const random_candidate_t *)b;
    if (lhs->smart_score < rhs->smart_score) return 1;
    if (lhs->smart_score > rhs->smart_score) return -1;
    if (lhs->total_seconds < rhs->total_seconds) return -1;
    if (lhs->total_seconds > rhs->total_seconds) return 1;
    return lhs->index - rhs->index;
}

static bool browser_entry_is_game(const entry_t *entry) {
    if (!entry) {
        return false;
    }
    return entry->type == ENTRY_TYPE_ROM || entry->type == ENTRY_TYPE_DISK || entry->type == ENTRY_TYPE_EMULATOR;
}

static char *browser_entry_path(menu_t *menu, int index) {
    if (!menu || index < 0 || index >= menu->browser.entries) {
        return NULL;
    }

    entry_t *entry = &menu->browser.list[index];
    if (entry->path) {
        return strdup(entry->path);
    }
    if (!entry->name) {
        return NULL;
    }

    path_t *path = path_clone_push(menu->browser.directory, entry->name);
    if (!path) {
        return NULL;
    }

    char *resolved = strdup(path_get(path));
    path_free(path);
    return resolved;
}

static bool path_is_favorite(menu_t *menu, const char *path) {
    if (!menu || !path) {
        return false;
    }

    char game_id[ROM_STABLE_ID_LENGTH] = {0};
    rom_info_get_stable_id_for_path_cached(path, game_id, sizeof(game_id));

    for (int i = 0; i < FAVORITES_COUNT; i++) {
        bookkeeping_item_t *item = &menu->bookkeeping.favorite_items[i];
        if (item->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY || item->primary_path == NULL) {
            continue;
        }
        if (strcmp(path_get(item->primary_path), path) == 0) {
            return true;
        }
        if (game_id[0] != '\0' &&
            item->game_id[0] != '\0' &&
            strcmp(item->game_id, game_id) == 0) {
            return true;
        }
    }
    return false;
}

static int browser_pick_random_index(menu_t *menu) {
    if (!menu || menu->browser.entries <= 0) {
        return -1;
    }

    int mode = menu->settings.browser_random_mode;
    if (mode < RANDOM_MODE_ANY_GAME || mode > RANDOM_MODE_SMART) {
        mode = RANDOM_MODE_ANY_GAME;
    }

    random_candidate_t *candidates = malloc((size_t)menu->browser.entries * sizeof(random_candidate_t));
    if (!candidates) {
        return -1;
    }

    int count = 0;
    for (int i = 0; i < menu->browser.entries; i++) {
        entry_t *entry = &menu->browser.list[i];
        if (!browser_entry_is_game(entry)) {
            continue;
        }

        char *entry_path = browser_entry_path(menu, i);
        if (!entry_path) {
            continue;
        }

        bool keep = false;
        uint64_t total_seconds = 0;
        int64_t last_played = 0;
        uint32_t play_count = 0;
        bool favorite = false;
        uint64_t smart_score = 0;

        if (mode == RANDOM_MODE_ANY_GAME) {
            keep = true;
        } else if (mode == RANDOM_MODE_UNPLAYED) {
            playtime_entry_t *stat = playtime_get_if_cached(&menu->playtime, entry_path);
            keep = (stat == NULL || stat->play_count == 0);
        } else if (mode == RANDOM_MODE_UNDERPLAYED) {
            playtime_entry_t *stat = playtime_get_if_cached(&menu->playtime, entry_path);
            total_seconds = stat ? stat->total_seconds : 0;
            keep = true;
        } else if (mode == RANDOM_MODE_FAVORITES) {
            keep = path_is_favorite(menu, entry_path);
        } else if (mode == RANDOM_MODE_SMART) {
            playtime_entry_t *stat = playtime_get_if_cached(&menu->playtime, entry_path);
            total_seconds = stat ? stat->total_seconds : 0;
            last_played = stat ? stat->last_played : 0;
            play_count = stat ? stat->play_count : 0;
            favorite = path_is_favorite(menu, entry_path);

            uint64_t age_days = 3650;
            if (last_played > 0 && menu->current_time > last_played) {
                age_days = (uint64_t)(menu->current_time - last_played) / 86400u;
                if (age_days > 3650) {
                    age_days = 3650;
                }
            }

            uint64_t total_hours = total_seconds / 3600u;
            if (total_hours > 500) {
                total_hours = 500;
            }
            uint64_t capped_play_count = play_count;
            if (capped_play_count > 200) {
                capped_play_count = 200;
            }

            smart_score += (play_count == 0) ? 1000000u : 0u;
            smart_score += age_days * 1000u;
            smart_score += favorite ? 250000u : 0u;
            smart_score += (500u - total_hours) * 100u;
            smart_score += (200u - capped_play_count) * 50u;
            keep = true;
        }

        if (keep) {
            candidates[count].index = i;
            candidates[count].total_seconds = total_seconds;
            candidates[count].last_played = last_played;
            candidates[count].play_count = play_count;
            candidates[count].favorite = favorite;
            candidates[count].smart_score = smart_score;
            count++;
        }

        free(entry_path);
    }

    if (count == 0 && mode != RANDOM_MODE_ANY_GAME) {
        menu->settings.browser_random_mode = RANDOM_MODE_ANY_GAME;
        settings_save(&menu->settings);
        free(candidates);
        return browser_pick_random_index(menu);
    }

    int next = -1;
    if (count > 0) {
        random_entry_state = (random_entry_state * 1664525u) + 1013904223u + (uint32_t)menu->browser.selected + (uint32_t)menu->browser.entries + (uint32_t)mode;

        if (mode == RANDOM_MODE_UNDERPLAYED || mode == RANDOM_MODE_SMART) {
            qsort(
                candidates,
                (size_t)count,
                sizeof(random_candidate_t),
                (mode == RANDOM_MODE_SMART) ? random_candidate_compare_smart : random_candidate_compare_underplayed
            );
            int pool = (mode == RANDOM_MODE_SMART) ? (count / 3) : (count / 4);
            if (pool < 1) {
                pool = 1;
            }
            next = candidates[(int)(random_entry_state % (uint32_t)pool)].index;
        } else {
            next = candidates[(int)(random_entry_state % (uint32_t)count)].index;
        }

        if (next == menu->browser.selected && count > 1) {
            for (int i = 0; i < count; i++) {
                if (candidates[i].index != menu->browser.selected) {
                    next = candidates[i].index;
                    break;
                }
            }
        }
    }

    free(candidates);
    return next;
}

static void browser_list_free (menu_t *menu) {
    playlist_grid_meta_index_clear();
    playlist_grid_slots_clear();
    playlist_active_clear();

    if (menu->browser.archive) {
        mz_zip_reader_end(&menu->browser.zip);
    }
    menu->browser.archive = false;
    menu->browser.playlist = false;

    for (int i = menu->browser.entries - 1; i >= 0; i--) {
        free(menu->browser.list[i].name);
        free(menu->browser.list[i].path);
    }

    free(menu->browser.list);

    menu->browser.list = NULL;
    menu->browser.entries = 0;
    menu->browser.entry = NULL;
    menu->browser.selected = -1;
}

void view_browser_deinit (menu_t *menu) {
    if (!menu) {
        return;
    }
    browser_list_free(menu);
    for (size_t i = 0; i < PLAYLIST_MEM_CACHE_ENTRIES; i++) {
        playlist_mem_cache_entry_clear(&playlist_mem_cache[i]);
    }
}

static bool load_archive (menu_t *menu) {
    browser_list_free(menu);

    mz_zip_zero_struct(&menu->browser.zip);
    if (!mz_zip_reader_init_file(&menu->browser.zip, path_get(menu->browser.directory), 0)) {
        return true;
    }

    menu->browser.archive = true;
    menu->browser.entries = (int32_t)mz_zip_reader_get_num_files(&menu->browser.zip);
    menu->browser.list = malloc(menu->browser.entries * sizeof(entry_t));
    if (!menu->browser.list) {
        browser_list_free(menu);
        return true;
    }

    for (int32_t i = 0; i < menu->browser.entries; i++) {
        entry_t *entry = &menu->browser.list[i];

        mz_zip_archive_file_stat info;
        if (!mz_zip_reader_file_stat(&menu->browser.zip, i, &info)) {
            browser_list_free(menu);
            return true;
        }

        entry->name = strdup(info.m_filename);
        if (!entry->name) {
            browser_list_free(menu);
            return true;
        }
        entry->path = NULL;

        entry->type = ENTRY_TYPE_ARCHIVED;
        entry->size = info.m_uncomp_size;
        entry->index = i;
    }

    if (menu->browser.entries > 0) {
        menu->browser.selected = 0;
        menu->browser.entry = &menu->browser.list[menu->browser.selected];
    }

    browser_apply_sort(menu);

    return false;
}

static char *trim_line (char *line) {
    if (line == NULL) {
        return NULL;
    }

    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[len - 1] = '\0';
        len--;
    }

    while (*line == ' ' || *line == '\t') {
        line++;
    }

    return line;
}

static void playlist_background_callback (png_err_t err, surface_t *decoded_image, void *callback_data) {
    (void)callback_data;
    playlist_override.background_loading = false;

    if (err != PNG_OK) {
        if (decoded_image) {
            surface_free(decoded_image);
            free(decoded_image);
        }
        return;
    }

    if (!playlist_override.active) {
        surface_free(decoded_image);
        free(decoded_image);
        return;
    }

    ui_components_background_replace_image_temporary(decoded_image);
    if (playlist_override.background_path) {
        ui_components_background_save_temporary_cache(playlist_override.background_path);
    }
    playlist_override.background_applied = true;
}

static bool browser_use_playlist_grid(menu_t *menu) {
    if (!menu) {
        return false;
    }
    if (!menu->browser.playlist) {
        return false;
    }
    if (menu->browser.picker != BROWSER_PICKER_NONE) {
        return false;
    }
    if (playlist_grid_runtime_override == 0 || playlist_grid_runtime_override == 1) {
        return (playlist_grid_runtime_override == 1);
    }
    return playlist_grid_view_enabled;
}

static void browser_playlist_grid_draw(menu_t *menu) {
    if (!menu) {
        return;
    }

    const int screen_w = display_get_width();
    const int screen_h = display_get_height();
    const int cols = 4;
    const int rows = 3;
    const int page_size = cols * rows;
    const int gap_x = 4;
    const int gap_y = 4;
    const int x0_area = VISIBLE_AREA_X0 + BORDER_THICKNESS + 2;
    const int x1_area = VISIBLE_AREA_X1 - BORDER_THICKNESS - 2;
    const int area_w = x1_area - x0_area;
    const int tile_w = (area_w - (cols - 1) * gap_x) / cols;
    const int header_h = 14;
    const int header_y = VISIBLE_AREA_Y0 + TAB_HEIGHT + BORDER_THICKNESS + 2;
    const int grid_y = header_y + header_h + 2;
    const int grid_area_h = LAYOUT_ACTIONS_SEPARATOR_Y - grid_y;
    const int tile_h = (grid_area_h - (rows - 1) * gap_y) / rows;
    const int grid_x = x0_area + (area_w - cols * tile_w - (cols - 1) * gap_x) / 2;

    int selected = menu->browser.selected;
    int entries = menu->browser.entries;
    if (entries <= 0) {
        ui_components_file_list_draw(menu->browser.list, menu->browser.entries, menu->browser.selected);
        return;
    }
    if (selected < 0) selected = 0;
    if (selected >= entries) selected = entries - 1;

    int page_start = (selected / page_size) * page_size;
    int visible = entries - page_start;
    if (visible > page_size) visible = page_size;

    if (playlist_grid_slots_list != menu->browser.list) {
        playlist_grid_slots_clear();
        playlist_grid_slots_list = menu->browser.list;
    }

    for (int i = 0; i < visible; i++) {
        int entry_index = page_start + i;
        int col = i % cols;
        int row = i / cols;
        int x0 = grid_x + col * (tile_w + gap_x);
        int y0 = grid_y + row * (tile_h + gap_y);
        int x1 = x0 + tile_w;
        bool is_selected = (entry_index == selected);

        int si = playlist_grid_slot_find(entry_index);
        playlist_grid_thumb_slot_t *slot = (si >= 0) ? &playlist_grid_slots[si] : NULL;
        if (slot && slot->boxart && slot->boxart->image) {
            surface_t *img = slot->boxart->image;
            float sx = (float)tile_w / (float)img->width;
            float sy = (float)tile_h / (float)img->height;
            float s = (sx < sy) ? sx : sy;
            int draw_w = (int)(img->width * s);
            int draw_h = (int)(img->height * s);
            if (draw_w < 1) draw_w = 1;
            if (draw_h < 1) draw_h = 1;
            int draw_x = x0 + (tile_w - draw_w) / 2;
            int draw_y = y0 + (tile_h - draw_h) / 2;

            rdpq_mode_push();
                rdpq_set_mode_standard();
                rdpq_mode_combiner(RDPQ_COMBINER_TEX);
                rdpq_mode_filter(FILTER_BILINEAR);
                rdpq_set_scissor(x0, y0, x1, y0 + tile_h);
                rdpq_tex_blit(img, draw_x, draw_y, &(rdpq_blitparms_t){
                    .scale_x = s,
                    .scale_y = s,
                });
                rdpq_set_scissor(0, 0, screen_w, screen_h);
            rdpq_mode_pop();
        } else {
            char name_buf[64];
            const char *label;
            if (slot && slot->boxart && slot->boxart->loading) {
                label = "...";
            } else {
                entry_t *entry = &menu->browser.list[entry_index];
                label = browser_grid_display_name(entry->name, name_buf, sizeof(name_buf));
            }
            rdpq_set_scissor(x0, y0, x1, y0 + tile_h);
            rdpq_text_printf(&(rdpq_textparms_t){
                .width = tile_w, .height = tile_h,
                .align = ALIGN_CENTER, .valign = VALIGN_CENTER,
                .wrap = WRAP_WORD,
            }, FNT_DEFAULT, x0, y0, "%s", label);
            rdpq_set_scissor(0, 0, screen_w, screen_h);
        }

        if (is_selected) {
            int sy1 = y0 + tile_h;
            rdpq_mode_push();
                rdpq_set_mode_fill(RGBA32(0x30, 0x70, 0xFF, 0xFF));
                rdpq_fill_rectangle(x0 - 2, y0 - 2, x1 + 2, y0);        // top
                rdpq_fill_rectangle(x0 - 2, sy1, x1 + 2, sy1 + 2);      // bottom
                rdpq_fill_rectangle(x0 - 2, y0, x0, sy1);                // left
                rdpq_fill_rectangle(x1, y0, x1 + 2, sy1);                // right
            rdpq_mode_pop();
        }
    }

    // Header: selected game name + page info above the grid
    entry_t *sel = (selected >= 0 && selected < entries) ? &menu->browser.list[selected] : NULL;
    char caption_buf[128];
    rdpq_set_scissor(x0_area, header_y, x1_area, header_y + header_h);
    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "@%d,%d\n%s",
        x0_area,
        header_y,
        sel ? browser_grid_display_name(sel->name, caption_buf, sizeof(caption_buf)) : ""
    );
    ui_components_main_text_draw(
        STL_GRAY,
        ALIGN_RIGHT, VALIGN_TOP,
        "@%d,%d\n%d/%d  Pg %d/%d",
        x1_area,
        header_y,
        selected + 1, entries,
        (selected / page_size) + 1,
        (entries + page_size - 1) / page_size
    );
    rdpq_set_scissor(0, 0, screen_w, screen_h);
}

static void browser_playlist_grid_prepare(menu_t *menu, bool defer_work) {
    if (!menu || !browser_use_playlist_grid(menu)) {
        return;
    }

    const int cols = 4;
    const int rows = 3;
    const int page_size = cols * rows;
    int entries = menu->browser.entries;
    int selected = menu->browser.selected;
    if (entries <= 0) {
        return;
    }
    if (selected < 0) selected = 0;
    if (selected >= entries) selected = entries - 1;

    int page_start = (selected / page_size) * page_size;
    int visible = entries - page_start;
    if (visible > page_size) visible = page_size;
    if (visible <= 0) return;

    playlist_grid_frame_counter++;

    if (playlist_grid_slots_list != menu->browser.list) {
        playlist_grid_slots_clear();
        playlist_grid_slots_list = menu->browser.list;
    }

    // First pass: try memory cache only for visible tiles (instant, no I/O).
    for (int i = 0; i < visible; i++) {
        int ei = page_start + i;
        int si = playlist_grid_slot_find(ei);
        if (si >= 0) {
            playlist_grid_slots[si].last_used_frame = playlist_grid_frame_counter;
        } else {
            playlist_grid_slot_prepare(menu, ei, true);
        }
    }

    if (defer_work) {
        return;
    }

    // Resolve up to 2 tiles per frame: current page first, then prefetch next.
    // Selected tile always gets priority.
    int resolved = 0;
    int sel_si = playlist_grid_slot_find(selected);
    if (sel_si < 0 || !playlist_grid_slots[sel_si].boxart_resolved) {
        playlist_grid_slot_prepare(menu, selected, false);
        resolved++;
    }
    for (int i = 0; i < visible && resolved < 2; i++) {
        int ei = page_start + i;
        if (ei == selected) continue;
        int si = playlist_grid_slot_find(ei);
        if (si < 0 || !playlist_grid_slots[si].boxart_resolved) {
            playlist_grid_slot_prepare(menu, ei, false);
            resolved++;
        }
    }
    if (resolved > 0) return;
    // Current page done — prefetch next page.
    int next_page_start = page_start + page_size;
    if (next_page_start < entries) {
        int next_visible = entries - next_page_start;
        if (next_visible > page_size) next_visible = page_size;
        for (int i = 0; i < next_visible; i++) {
            int ei = next_page_start + i;
            int si = playlist_grid_slot_find(ei);
            if (si < 0 || !playlist_grid_slots[si].boxart_resolved) {
                playlist_grid_slot_prepare(menu, ei, false);
                return;
            }
        }
    }
}

void browser_playlist_perf_note_bgm_reload(uint32_t ms) {
    playlist_perf.bgm_reload_ms = ms;
    debugf("Playlist perf: bgm reload=%lums\n", (unsigned long)ms);
}

void browser_playlist_perf_note_screensaver_logo_reload(uint32_t ms) {
    playlist_perf.logo_reload_ms = ms;
    debugf("Playlist perf: screensaver logo reload=%lums\n", (unsigned long)ms);
}

static void playlist_toast_draw(void) {
    if (playlist_toast.frames_left <= 0) {
        return;
    }

    int x0 = 28;
    int x1 = display_get_width() - 28;
    int y0 = 46;
    int y1 = y0 + 44;

    ui_components_box_draw(x0, y0, x1, y1, RGBA32(0x00, 0x00, 0x00, 0xA8));
    ui_components_border_draw(x0, y0, x1, y1);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = x1 - x0 - 12, .height = 18 },
                     FNT_DEFAULT, x0 + 6, y0 + 6, "^%02X%s", STL_YELLOW, playlist_toast.line1);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = x1 - x0 - 12, .height = 18, .wrap = WRAP_ELLIPSES },
                     FNT_DEFAULT, x0 + 6, y0 + 24, "%s", playlist_toast.line2);
}

static int playlist_theme_id_from_string(const char *value) {
    if (!value || !value[0]) {
        return -1;
    }

    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end && *end == '\0') {
        if (parsed >= 0 && parsed < ui_components_theme_count()) {
            return (int)parsed;
        }
    }

    for (int i = 0; i < ui_components_theme_count(); i++) {
        if (strcasecmp(value, ui_components_theme_name(i)) == 0) {
            return i;
        }
    }
    return -1;
}

static char *playlist_resolve_path(menu_t *menu, path_t *playlist_dir, const char *raw_path) {
    if (!menu || !playlist_dir || !raw_path || !raw_path[0]) {
        return NULL;
    }

    path_t *resolved = NULL;
    if (strstr(raw_path, ":/") != NULL) {
        resolved = path_create(raw_path);
    } else if (raw_path[0] == '/') {
        resolved = path_init(menu->storage_prefix, (char *)raw_path);
    } else {
        resolved = path_clone(playlist_dir);
        path_push(resolved, (char *)raw_path);
    }

    char *normalized = normalize_path(path_get(resolved));
    path_free(resolved);
    return normalized;
}

static void smart_playlist_query_init(smart_playlist_query_t *query) {
    if (!query) {
        return;
    }
    memset(query, 0, sizeof(*query));
    query->sort = SMART_PLAYLIST_SORT_TITLE;
}

static bool smart_playlist_add_root(smart_playlist_query_t *query, const char *root_path) {
    if (!query || !root_path || !root_path[0] || query->root_count >= SMART_PLAYLIST_MAX_ROOTS) {
        return false;
    }
    for (int i = 0; i < query->root_count; i++) {
        if (strcmp(query->roots[i], root_path) == 0) {
            return true;
        }
    }
    snprintf(query->roots[query->root_count], sizeof(query->roots[query->root_count]), "%s", root_path);
    query->root_count++;
    query->enabled = true;
    return true;
}

static bool string_contains_ignore_case(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) {
        return false;
    }
    size_t needle_len = strlen(needle);
    size_t haystack_len = strlen(haystack);
    if (needle_len > haystack_len) {
        return false;
    }

    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        bool match = true;
        for (size_t j = 0; j < needle_len; j++) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

static bool smart_playlist_parse_range(const char *value, int *out_min, int *out_max) {
    if (!value || !value[0] || !out_min || !out_max) {
        return false;
    }

    if (strncmp(value, ">=", 2) == 0) {
        *out_min = atoi(value + 2);
        *out_max = 9999;
        return true;
    }
    if (strncmp(value, "<=", 2) == 0) {
        *out_min = -9999;
        *out_max = atoi(value + 2);
        return true;
    }

    const char *dash = strchr(value, '-');
    if (dash && dash != value) {
        char left[32];
        size_t left_len = (size_t)(dash - value);
        if (left_len >= sizeof(left)) {
            left_len = sizeof(left) - 1;
        }
        memcpy(left, value, left_len);
        left[left_len] = '\0';
        *out_min = atoi(left);
        *out_max = atoi(dash + 1);
        return true;
    }

    *out_min = atoi(value);
    *out_max = *out_min;
    return true;
}

static bool smart_playlist_ranges_overlap(int lhs_min, int lhs_max, int rhs_min, int rhs_max) {
    return (lhs_min <= rhs_max) && (rhs_min <= lhs_max);
}

static bool smart_playlist_parse_region(const char *value, char *out_region) {
    if (!value || !value[0] || !out_region) {
        return false;
    }

    if (value[1] == '\0') {
        char c = value[0];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        *out_region = c;
        return true;
    }

    if (strcasecmp(value, "USA") == 0 || strcasecmp(value, "US") == 0 || strcasecmp(value, "NTSC") == 0) {
        *out_region = 'E';
        return true;
    }
    if (strcasecmp(value, "PAL") == 0 || strcasecmp(value, "EUR") == 0 || strcasecmp(value, "EUROPE") == 0) {
        *out_region = 'P';
        return true;
    }
    if (strcasecmp(value, "JPN") == 0 || strcasecmp(value, "JP") == 0 || strcasecmp(value, "JAPAN") == 0) {
        *out_region = 'J';
        return true;
    }
    if (strcasecmp(value, "AUS") == 0 || strcasecmp(value, "AUSTRALIA") == 0) {
        *out_region = 'U';
        return true;
    }

    return false;
}

static smart_playlist_sort_t smart_playlist_parse_sort(const char *value) {
    if (!value || !value[0]) {
        return SMART_PLAYLIST_SORT_TITLE;
    }
    if (strcasecmp(value, "YEAR") == 0) {
        return SMART_PLAYLIST_SORT_YEAR;
    }
    if (strcasecmp(value, "PUBLISHER") == 0 || strcasecmp(value, "AUTHOR") == 0 || strcasecmp(value, "STUDIO") == 0) {
        return SMART_PLAYLIST_SORT_PUBLISHER;
    }
    if (strcasecmp(value, "RANDOM") == 0 || strcasecmp(value, "SHUFFLE") == 0) {
        return SMART_PLAYLIST_SORT_RANDOM;
    }
    return SMART_PLAYLIST_SORT_TITLE;
}

static bool smart_playlist_should_scan_dir(const char *dirname) {
    if (!dirname || !dirname[0]) {
        return false;
    }
    if (strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0) {
        return false;
    }
    if (dirname[0] == '.') {
        return false;
    }
    if (strcmp(dirname, "menu") == 0 || strcmp(dirname, "ED64") == 0 || strcmp(dirname, "ED64P") == 0 ||
        strcmp(dirname, "System Volume Information") == 0) {
        return false;
    }
    return true;
}

static void smart_playlist_title_from_rom_info(const rom_info_t *rom_info, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!rom_info) {
        return;
    }
    if (rom_info->metadata.name[0] != '\0') {
        snprintf(out, out_len, "%s", rom_info->metadata.name);
        return;
    }

    size_t len = 0;
    while (len < sizeof(rom_info->title) && rom_info->title[len] != '\0') {
        len++;
    }
    while (len > 0 && rom_info->title[len - 1] == ' ') {
        len--;
    }
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, rom_info->title, len);
    out[len] = '\0';
}

static bool smart_playlist_matches(menu_t *menu, const smart_playlist_query_t *query, const char *rom_path, rom_info_t *rom_info, smart_playlist_entry_t *entry_out) {
    (void)menu;
    if (!query || !rom_path || !rom_info || !entry_out) {
        return false;
    }

    char title[ROM_METADATA_NAME_LENGTH];
    smart_playlist_title_from_rom_info(rom_info, title, sizeof(title));
    const char *publisher = rom_info->metadata.author;
    const char *developer = rom_info->metadata.developer;
    const char *genre = rom_info->metadata.genre;
    const char *series = rom_info->metadata.series;
    const char *modes = rom_info->metadata.modes;

    if (query->title_contains[0] != '\0') {
        if (title[0] == '\0' || !string_contains_ignore_case(title, query->title_contains)) {
            return false;
        }
    }
    if (query->publisher_contains[0] != '\0') {
        if (publisher[0] == '\0' || !string_contains_ignore_case(publisher, query->publisher_contains)) {
            return false;
        }
    }
    if (query->developer_contains[0] != '\0') {
        if (developer[0] == '\0' || !string_contains_ignore_case(developer, query->developer_contains)) {
            return false;
        }
    }
    if (query->genre_contains[0] != '\0') {
        if (genre[0] == '\0' || !string_contains_ignore_case(genre, query->genre_contains)) {
            return false;
        }
    }
    if (query->series_contains[0] != '\0') {
        if (series[0] == '\0' || !string_contains_ignore_case(series, query->series_contains)) {
            return false;
        }
    }
    if (query->modes_contains[0] != '\0') {
        if (modes[0] == '\0' || !string_contains_ignore_case(modes, query->modes_contains)) {
            return false;
        }
    }
    if (query->description_contains[0] != '\0') {
        bool short_match = rom_info->metadata.short_desc[0] != '\0' &&
                           string_contains_ignore_case(rom_info->metadata.short_desc, query->description_contains);
        bool long_match = rom_info->metadata.long_desc[0] != '\0' &&
                          string_contains_ignore_case(rom_info->metadata.long_desc, query->description_contains);
        if (!short_match && !long_match) {
            return false;
        }
    }
    if (query->filter_year) {
        if (rom_info->metadata.release_year < query->year_min || rom_info->metadata.release_year > query->year_max) {
            return false;
        }
    }
    if (query->filter_age) {
        if (rom_info->metadata.age_rating < query->age_min || rom_info->metadata.age_rating > query->age_max) {
            return false;
        }
    }
    if (query->filter_players) {
        int players_min = rom_info->metadata.players_min;
        int players_max = rom_info->metadata.players_max;
        if ((players_min < 0) && (players_max < 0)) {
            return false;
        }
        if (players_min < 0) {
            players_min = players_max;
        }
        if (players_max < 0) {
            players_max = players_min;
        }
        if (!smart_playlist_ranges_overlap(players_min, players_max, query->players_min, query->players_max)) {
            return false;
        }
    }
    if (query->filter_region) {
        char rom_region = rom_info->game_code[3];
        if (rom_region >= 'a' && rom_region <= 'z') {
            rom_region = (char)(rom_region - 'a' + 'A');
        }
        if (rom_region != query->region_code) {
            return false;
        }
    }

    memset(entry_out, 0, sizeof(*entry_out));
    entry_out->path = strdup(rom_path);
    if (!entry_out->path) {
        return false;
    }
    entry_out->name = strdup(file_basename(entry_out->path));
    if (!entry_out->name) {
        free(entry_out->path);
        entry_out->path = NULL;
        return false;
    }
    snprintf(entry_out->title, sizeof(entry_out->title), "%s", title);
    snprintf(entry_out->publisher, sizeof(entry_out->publisher), "%s", publisher);
    entry_out->year = rom_info->metadata.release_year;
    entry_out->random_key = random_entry_state = (random_entry_state * 1664525u) + 1013904223u;
    return true;
}

static bool smart_playlist_query_needs_long_description(const smart_playlist_query_t *query) {
    return query && query->description_contains[0] != '\0';
}

static int smart_playlist_entry_compare(const void *a, const void *b, void *ctx) {
    const smart_playlist_query_t *query = (const smart_playlist_query_t *)ctx;
    const smart_playlist_entry_t *lhs = (const smart_playlist_entry_t *)a;
    const smart_playlist_entry_t *rhs = (const smart_playlist_entry_t *)b;

    if (query->sort == SMART_PLAYLIST_SORT_YEAR) {
        if (lhs->year < rhs->year) return -1;
        if (lhs->year > rhs->year) return 1;
        return strcasecmp(lhs->title, rhs->title);
    }
    if (query->sort == SMART_PLAYLIST_SORT_PUBLISHER) {
        int cmp = strcasecmp(lhs->publisher, rhs->publisher);
        if (cmp != 0) return cmp;
        return strcasecmp(lhs->title, rhs->title);
    }
    if (query->sort == SMART_PLAYLIST_SORT_RANDOM) {
        if (lhs->random_key < rhs->random_key) return -1;
        if (lhs->random_key > rhs->random_key) return 1;
        return 0;
    }
    return strcasecmp(lhs->title, rhs->title);
}

static int smart_playlist_entry_compare_wrapper(const void *a, const void *b) {
    return smart_playlist_entry_compare(a, b, (void *)smart_playlist_sort_query);
}

static void smart_playlist_entry_free(smart_playlist_entry_t *entry) {
    if (!entry) {
        return;
    }
    free(entry->path);
    free(entry->name);
    memset(entry, 0, sizeof(*entry));
}

static bool playlist_has_path(menu_t *menu, const char *path) {
    if (!menu || !path) {
        return false;
    }
    for (int i = 0; i < menu->browser.entries; i++) {
        if (menu->browser.list[i].path && strcmp(menu->browser.list[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

static bool browser_reserve_entry_capacity(menu_t *menu, int *capacity, int extra_entries) {
    if (!menu || !capacity || extra_entries <= 0) {
        return false;
    }

    int needed = menu->browser.entries + extra_entries;
    if (needed < 0 || needed > 16384) {
        return false;
    }
    if (*capacity >= needed) {
        return true;
    }

    int next_capacity = (*capacity > 0) ? *capacity : 16;
    while (next_capacity < needed) {
        next_capacity *= 2;
        if (next_capacity > 16384) {
            return false;
        }
    }

    entry_t *next = realloc(menu->browser.list, (size_t)next_capacity * sizeof(entry_t));
    if (!next) {
        return false;
    }

    menu->browser.list = next;
    *capacity = next_capacity;
    return true;
}

static bool playlist_append_rom_entry(menu_t *menu, const char *normalized_path, int *capacity) {
    if (!menu || !normalized_path || !normalized_path[0]) {
        return false;
    }

    if (!browser_reserve_entry_capacity(menu, capacity, 1)) {
        return false;
    }

    entry_t *entry = &menu->browser.list[menu->browser.entries++];
    memset(entry, 0, sizeof(*entry));
    entry->name = strdup(file_basename((char *)normalized_path));
    entry->path = strdup(normalized_path);
    if (!entry->name || !entry->path) {
        free(entry->name);
        free(entry->path);
        memset(entry, 0, sizeof(*entry));
        menu->browser.entries--;
        return false;
    }
    entry->type = ENTRY_TYPE_ROM;
    entry->size = -1;
    entry->index = menu->browser.entries - 1;
    playtime_entry_t *pt = playtime_get_if_cached(&menu->playtime, normalized_path);
    entry->last_played = pt ? pt->last_played : 0;
    return true;
}

static bool playlist_append_rom_entry_unique(menu_t *menu, const char *normalized_path, int *capacity) {
    if (playlist_has_path(menu, normalized_path)) {
        return true;
    }
    return playlist_append_rom_entry(menu, normalized_path, capacity);
}

static bool playlist_prepend_text_entry(menu_t *menu, const char *entry_path, int *capacity) {
    if (!menu || !entry_path || !entry_path[0]) {
        return false;
    }

    if (!browser_reserve_entry_capacity(menu, capacity, 1)) {
        return false;
    }

    if (menu->browser.entries > 0) {
        memmove(&menu->browser.list[1], &menu->browser.list[0], (size_t)menu->browser.entries * sizeof(entry_t));
    }

    menu->browser.entries++;
    entry_t *entry = &menu->browser.list[0];
    memset(entry, 0, sizeof(*entry));
    entry->name = strdup(file_basename((char *)entry_path));
    entry->path = strdup(entry_path);
    entry->type = ENTRY_TYPE_TEXT;
    entry->size = -1;

    if (!entry->name || !entry->path) {
        free(entry->name);
        free(entry->path);
        if (menu->browser.entries > 1) {
            memmove(&menu->browser.list[0], &menu->browser.list[1], (size_t)(menu->browser.entries - 1) * sizeof(entry_t));
        }
        menu->browser.entries--;
        return false;
    }

    for (int i = 0; i < menu->browser.entries; i++) {
        menu->browser.list[i].index = i;
    }
    return true;
}

static char *playlist_find_context_text_path(path_t *playlist_path) {
    if (!playlist_path) {
        return NULL;
    }

    path_t *dir = path_clone(playlist_path);
    if (!dir) {
        return NULL;
    }

    const char *playlist_full_path = path_get(playlist_path);
    char *playlist_name = file_basename((char *)playlist_full_path);
    if (!playlist_name || !playlist_name[0]) {
        path_free(dir);
        return NULL;
    }

    char stem[256];
    snprintf(stem, sizeof(stem), "%s", playlist_name);
    char *dot = strrchr(stem, '.');
    if (dot) {
        *dot = '\0';
    }
    if (!stem[0]) {
        path_free(dir);
        return NULL;
    }

    char context_name[300];
    snprintf(context_name, sizeof(context_name), "_%s.txt", stem);

    path_pop(dir);
    path_push(dir, context_name);

    char *normalized = NULL;
    if (file_exists(path_get(dir))) {
        normalized = normalize_path(path_get(dir));
    }
    path_free(dir);
    return normalized;
}

static bool smart_playlist_collect_dir(
    menu_t *menu,
    path_t *dir_path,
    const smart_playlist_query_t *query,
    smart_playlist_entry_t **entries,
    int *count,
    int *capacity,
    int depth
) {
    if (!menu || !dir_path || !query || !entries || !count || !capacity || depth > 6) {
        return false;
    }
    if (!directory_exists(path_get(dir_path))) {
        return true;
    }

    dir_t info;
    int result = dir_findfirst(path_get(dir_path), &info);
    while (result == 0) {
        if (info.d_type == DT_DIR) {
            if (smart_playlist_should_scan_dir(info.d_name)) {
                path_t *subdir = path_clone_push(dir_path, info.d_name);
                if (!subdir) {
                    return false;
                }
                bool ok = smart_playlist_collect_dir(menu, subdir, query, entries, count, capacity, depth + 1);
                path_free(subdir);
                if (!ok) {
                    return false;
                }
            }
        } else if (file_has_extensions(info.d_name, n64_rom_extensions)) {
            path_t *candidate_path = path_clone_push(dir_path, info.d_name);
            if (!candidate_path) {
                return false;
            }
            char *normalized = normalize_path(path_get(candidate_path));
            path_free(candidate_path);
            if (!normalized) {
                return false;
            }
            path_t *rom_path = path_create(normalized);
            rom_info_t *rom_info = calloc(1, sizeof(rom_info_t));
            if (!rom_info) {
                free(normalized);
                path_free(rom_path);
                return false;
            }
            rom_load_options_t load_options = {
                .include_config = false,
                .include_long_description = smart_playlist_query_needs_long_description(query),
            };
            sound_poll();
            if (rom_path && rom_config_load_ex(rom_path, rom_info, &load_options) == ROM_OK) {
                smart_playlist_entry_t candidate = {0};
                if (smart_playlist_matches(menu, query, normalized, rom_info, &candidate)) {
                    int needed = *count + 1;
                    if (needed > 16384) {
                        smart_playlist_entry_free(&candidate);
                        continue;
                    }
                    if (*capacity < needed) {
                        int next_capacity = (*capacity > 0) ? *capacity : 32;
                        while (next_capacity < needed && next_capacity <= 16384) {
                            next_capacity *= 2;
                        }
                        smart_playlist_entry_t *next = realloc(*entries, (size_t)next_capacity * sizeof(**entries));
                        if (!next) {
                            free(normalized);
                            path_free(rom_path);
                            free(rom_info);
                            smart_playlist_entry_free(&candidate);
                            return false;
                        }
                        *entries = next;
                        *capacity = next_capacity;
                    }
                    (*entries)[*count] = candidate;
                    (*count)++;
                }
            }
            free(rom_info);
            path_free(rom_path);
            free(normalized);
        }
        result = dir_findnext(path_get(dir_path), &info);
    }

    return true;
}

static int smart_playlist_entry_compare_path(const void *a, const void *b) {
    const smart_playlist_entry_t *lhs = (const smart_playlist_entry_t *)a;
    const smart_playlist_entry_t *rhs = (const smart_playlist_entry_t *)b;
    const char *lhs_path = lhs->path ? lhs->path : "";
    const char *rhs_path = rhs->path ? rhs->path : "";
    return strcmp(lhs_path, rhs_path);
}

static int smart_playlist_deduplicate_entries(smart_playlist_entry_t *entries, int count) {
    if (!entries || count <= 1) {
        return count;
    }

    qsort(entries, (size_t)count, sizeof(*entries), smart_playlist_entry_compare_path);

    int write_index = 1;
    for (int read_index = 1; read_index < count; read_index++) {
        if (entries[read_index].path &&
            entries[write_index - 1].path &&
            strcmp(entries[write_index - 1].path, entries[read_index].path) == 0) {
            smart_playlist_entry_free(&entries[read_index]);
            continue;
        }
        if (write_index != read_index) {
            entries[write_index] = entries[read_index];
            memset(&entries[read_index], 0, sizeof(entries[read_index]));
        }
        write_index++;
    }

    return write_index;
}

static void smart_playlist_sort_entries(smart_playlist_entry_t *entries, int count, const smart_playlist_query_t *query) {
    if (!entries || count <= 1 || !query) {
        return;
    }
    smart_playlist_sort_query = query;
    qsort(entries, (size_t)count, sizeof(*entries), smart_playlist_entry_compare_wrapper);
    smart_playlist_sort_query = NULL;
}

static void playlist_parse_directive(menu_t *menu, path_t *playlist_dir, const char *line, playlist_props_t *props, smart_playlist_query_t *smart_query) {
    if (!line || line[0] != '#') {
        return;
    }

    const char *prefix = "#SC64_";
    if (strncasecmp(line, prefix, strlen(prefix)) != 0) {
        return;
    }

    const char *body = line + strlen(prefix);
    const char *sep = strchr(body, '=');
    if (!sep) {
        return;
    }

    size_t key_len = (size_t)(sep - body);
    if (key_len == 0) {
        return;
    }

    char key[32];
    if (key_len >= sizeof(key)) {
        key_len = sizeof(key) - 1;
    }
    memcpy(key, body, key_len);
    key[key_len] = '\0';

    char value_buf[1024];
    snprintf(value_buf, sizeof(value_buf), "%s", sep + 1);
    char *value = trim_line(value_buf);
    if (!value || !value[0]) {
        return;
    }

    if (strcasecmp(key, "THEME") == 0) {
        free(props->theme);
        props->theme = strdup(value);
        return;
    }

    if (strcasecmp(key, "BGM") == 0 || strcasecmp(key, "MUSIC") == 0) {
        char *resolved = playlist_resolve_path(menu, playlist_dir, value);
        if (resolved) {
            free(props->bgm);
            props->bgm = resolved;
        }
        return;
    }

    if (strcasecmp(key, "BACKGROUND") == 0 || strcasecmp(key, "BG") == 0) {
        char *resolved = playlist_resolve_path(menu, playlist_dir, value);
        if (resolved) {
            free(props->bg);
            props->bg = resolved;
        }
        return;
    }

    if (strcasecmp(key, "TEXT_PANEL") == 0 || strcasecmp(key, "TEXT_OVERLAY") == 0) {
        if (strcasecmp(value, "ON") == 0 || strcasecmp(value, "TRUE") == 0 || strcmp(value, "1") == 0) {
            props->text_panel_enabled = 1;
        } else if (strcasecmp(value, "OFF") == 0 || strcasecmp(value, "FALSE") == 0 || strcmp(value, "0") == 0) {
            props->text_panel_enabled = 0;
        }
        return;
    }

    if (strcasecmp(key, "TEXT_PANEL_ALPHA") == 0 || strcasecmp(key, "TEXT_ALPHA") == 0) {
        char *end = NULL;
        long parsed = strtol(value, &end, 10);
        if (end && *end == '\0') {
            if (parsed < 0) parsed = 0;
            if (parsed > 255) parsed = 255;
            props->text_panel_alpha = (int)parsed;
        }
        return;
    }

    if (strcasecmp(key, "SCREENSAVER_LOGO") == 0 || strcasecmp(key, "SAVER_LOGO") == 0) {
        char *resolved = playlist_resolve_path(menu, playlist_dir, value);
        if (resolved) {
            free(props->screensaver_logo);
            props->screensaver_logo = resolved;
        }
        return;
    }

    if (strcasecmp(key, "VIZ_STYLE") == 0 || strcasecmp(key, "VISUALIZER_STYLE") == 0) {
        if (strcasecmp(value, "BARS") == 0) props->viz_style = UI_BACKGROUND_VISUALIZER_BARS;
        else if (strcasecmp(value, "PULSE") == 0 || strcasecmp(value, "PULSE_WASH") == 0 || strcasecmp(value, "PULSEWASH") == 0) props->viz_style = UI_BACKGROUND_VISUALIZER_PULSE_WASH;
        else if (strcasecmp(value, "SUNBURST") == 0) props->viz_style = UI_BACKGROUND_VISUALIZER_SUNBURST;
        else if (strcasecmp(value, "SCOPE") == 0 || strcasecmp(value, "OSC") == 0 || strcasecmp(value, "OSCILLOSCOPE") == 0) props->viz_style = UI_BACKGROUND_VISUALIZER_OSCILLOSCOPE;
        else {
            char *end = NULL;
            long parsed = strtol(value, &end, 10);
            if (end && *end == '\0' && parsed >= 0 && parsed <= UI_BACKGROUND_VISUALIZER_OSCILLOSCOPE) {
                props->viz_style = (int)parsed;
            }
        }
        return;
    }

    if (strcasecmp(key, "VIZ_INTENSITY") == 0 || strcasecmp(key, "VISUALIZER_INTENSITY") == 0) {
        if (strcasecmp(value, "SUBTLE") == 0 || strcasecmp(value, "LOW") == 0) props->viz_intensity = 0;
        else if (strcasecmp(value, "NORMAL") == 0 || strcasecmp(value, "MEDIUM") == 0) props->viz_intensity = 1;
        else if (strcasecmp(value, "FULL") == 0 || strcasecmp(value, "HIGH") == 0) props->viz_intensity = 2;
        else {
            char *end = NULL;
            long parsed = strtol(value, &end, 10);
            if (end && *end == '\0' && parsed >= 0 && parsed <= 2) {
                props->viz_intensity = (int)parsed;
            }
        }
        return;
    }

    if (strcasecmp(key, "VIEW") == 0 || strcasecmp(key, "BROWSER_VIEW") == 0 || strcasecmp(key, "LAYOUT") == 0) {
        if (strcasecmp(value, "GRID") == 0 || strcasecmp(value, "BOXART") == 0) {
            props->grid_view = 1;
        } else if (strcasecmp(value, "LIST") == 0) {
            props->grid_view = 0;
        }
        return;
    }

    if (!smart_query) {
        return;
    }

    if (strcasecmp(key, "SMART_ROOT") == 0 || strcasecmp(key, "ROOT") == 0) {
        char *resolved = playlist_resolve_path(menu, playlist_dir, value);
        if (resolved) {
            smart_playlist_add_root(smart_query, resolved);
            free(resolved);
        }
        return;
    }

    if (strcasecmp(key, "FILTER_TITLE") == 0 || strcasecmp(key, "FILTER_NAME") == 0) {
        snprintf(smart_query->title_contains, sizeof(smart_query->title_contains), "%s", value);
        smart_query->enabled = true;
        return;
    }

    if (strcasecmp(key, "FILTER_PUBLISHER") == 0 || strcasecmp(key, "FILTER_AUTHOR") == 0) {
        snprintf(smart_query->publisher_contains, sizeof(smart_query->publisher_contains), "%s", value);
        smart_query->enabled = true;
        return;
    }

    if (strcasecmp(key, "FILTER_DEVELOPER") == 0 || strcasecmp(key, "FILTER_DEV") == 0 ||
        strcasecmp(key, "FILTER_STUDIO") == 0 || strcasecmp(key, "DEVELOPER") == 0) {
        snprintf(smart_query->developer_contains, sizeof(smart_query->developer_contains), "%s", value);
        smart_query->enabled = true;
        return;
    }

    if (strcasecmp(key, "FILTER_GENRE") == 0 || strcasecmp(key, "GENRE") == 0) {
        snprintf(smart_query->genre_contains, sizeof(smart_query->genre_contains), "%s", value);
        smart_query->enabled = true;
        return;
    }

    if (strcasecmp(key, "FILTER_SERIES") == 0 || strcasecmp(key, "SERIES") == 0 || strcasecmp(key, "FRANCHISE") == 0) {
        snprintf(smart_query->series_contains, sizeof(smart_query->series_contains), "%s", value);
        smart_query->enabled = true;
        return;
    }

    if (strcasecmp(key, "FILTER_MODES") == 0 || strcasecmp(key, "FILTER_MODE") == 0 || strcasecmp(key, "MODES") == 0) {
        snprintf(smart_query->modes_contains, sizeof(smart_query->modes_contains), "%s", value);
        smart_query->enabled = true;
        return;
    }

    if (strcasecmp(key, "FILTER_DESCRIPTION") == 0 || strcasecmp(key, "FILTER_DESC") == 0 || strcasecmp(key, "FILTER_BLURB") == 0) {
        snprintf(smart_query->description_contains, sizeof(smart_query->description_contains), "%s", value);
        smart_query->enabled = true;
        return;
    }

    if (strcasecmp(key, "FILTER_YEAR") == 0 || strcasecmp(key, "YEAR") == 0) {
        if (smart_playlist_parse_range(value, &smart_query->year_min, &smart_query->year_max)) {
            smart_query->filter_year = true;
            smart_query->enabled = true;
        }
        return;
    }

    if (strcasecmp(key, "FILTER_AGE") == 0 || strcasecmp(key, "AGE") == 0) {
        if (smart_playlist_parse_range(value, &smart_query->age_min, &smart_query->age_max)) {
            smart_query->filter_age = true;
            smart_query->enabled = true;
        }
        return;
    }

    if (strcasecmp(key, "FILTER_PLAYERS") == 0 || strcasecmp(key, "PLAYERS") == 0) {
        if (smart_playlist_parse_range(value, &smart_query->players_min, &smart_query->players_max)) {
            smart_query->filter_players = true;
            smart_query->enabled = true;
        }
        return;
    }

    if (strcasecmp(key, "FILTER_REGION") == 0 || strcasecmp(key, "REGION") == 0) {
        char region_code = '\0';
        if (smart_playlist_parse_region(value, &region_code)) {
            smart_query->filter_region = true;
            smart_query->region_code = region_code;
            smart_query->enabled = true;
        }
        return;
    }

    if (strcasecmp(key, "SMART_SORT") == 0 || strcasecmp(key, "SORT") == 0) {
        smart_query->sort = smart_playlist_parse_sort(value);
        smart_query->enabled = true;
        return;
    }
}

static void browser_restore_playlist_overrides(menu_t *menu) {
    if (!playlist_override.active) {
        return;
    }

    if (playlist_override.background_loading) {
        png_decoder_abort();
        playlist_override.background_loading = false;
    }
    if (playlist_override.background_applied) {
        ui_components_background_reload_cache();
    }
    if (playlist_override.theme_applied) {
        ui_components_set_theme(playlist_override.saved_theme);
    }
    if (playlist_override.bgm_applied) {
        free(menu->runtime_bgm_override_file);
        menu->runtime_bgm_override_file = NULL;
        menu->bgm_reload_requested = true;
    }
    if (playlist_override.text_panel_applied) {
        ui_components_set_text_panel(playlist_override.saved_text_panel_enabled, playlist_override.saved_text_panel_alpha);
    }
    if (playlist_override.screensaver_logo_applied) {
        if (menu->settings.screensaver_logo_file) {
            free(menu->settings.screensaver_logo_file);
        }
        menu->settings.screensaver_logo_file = playlist_override.saved_screensaver_logo_file ? strdup(playlist_override.saved_screensaver_logo_file) : strdup("");
        menu->screensaver_logo_reload_requested = true;
    }
    if (playlist_override.viz_style_applied) {
        ui_components_background_set_visualizer_style(playlist_override.saved_viz_style);
    }
    if (playlist_override.viz_intensity_applied) {
        ui_components_background_set_visualizer_intensity(playlist_override.saved_viz_intensity);
    }
    if (playlist_override.grid_view_applied) {
        playlist_grid_view_enabled = playlist_override.saved_grid_view_enabled;
    }
    playlist_grid_runtime_override = -1;
    playlist_grid_slots_clear();

    free(playlist_override.background_path);
    free(playlist_override.pending_bgm_path);
    free(playlist_override.pending_screensaver_logo_path);
    free(playlist_override.saved_screensaver_logo_file);
    memset(&playlist_override, 0, sizeof(playlist_override));
    playlist_toast.frames_left = 0;
}

static void browser_apply_playlist_overrides(menu_t *menu, const playlist_props_t *props) {
    if (!menu || !props) {
        return;
    }

    int theme_id = playlist_theme_id_from_string(props->theme);
    bool want_theme = (theme_id >= 0);
    bool want_bgm = (props->bgm && props->bgm[0] != '\0');
    bool want_bg = (props->bg && props->bg[0] != '\0');
    bool want_viz_style = (props->viz_style >= 0 && props->viz_style <= UI_BACKGROUND_VISUALIZER_OSCILLOSCOPE);
    bool want_viz_intensity = (props->viz_intensity >= 0 && props->viz_intensity <= 2);
    bool want_text_panel_enabled = (props->text_panel_enabled == 0 || props->text_panel_enabled == 1);
    bool want_text_panel_alpha = (props->text_panel_alpha >= 0 && props->text_panel_alpha <= 255);
    bool want_screensaver_logo = (props->screensaver_logo && props->screensaver_logo[0] != '\0');
    bool want_grid_view = (props->grid_view == 0 || props->grid_view == 1);
    if (!want_theme && !want_bgm && !want_bg && !want_viz_style && !want_viz_intensity && !want_text_panel_enabled && !want_text_panel_alpha && !want_screensaver_logo && !want_grid_view) {
        return;
    }

    playlist_override.active = true;
    playlist_override.saved_theme = ui_components_get_theme();
    playlist_override.saved_viz_style = menu->settings.background_visualizer_style;
    playlist_override.saved_viz_intensity = menu->settings.background_visualizer_intensity;
    playlist_override.saved_text_panel_enabled = menu->settings.text_panel_enabled;
    playlist_override.saved_text_panel_alpha = menu->settings.text_panel_alpha;
    playlist_override.saved_screensaver_logo_file = strdup(menu->settings.screensaver_logo_file ? menu->settings.screensaver_logo_file : "");
    playlist_override.saved_grid_view_enabled = playlist_grid_view_enabled;

    if (want_theme) {
        ui_components_set_theme(theme_id);
        playlist_override.theme_applied = true;
    }

    if (want_bgm) {
        free(playlist_override.pending_bgm_path);
        playlist_override.pending_bgm_path = strdup(props->bgm);
    }
    if (want_text_panel_enabled || want_text_panel_alpha) {
        bool enabled = want_text_panel_enabled ? (props->text_panel_enabled != 0) : menu->settings.text_panel_enabled;
        uint8_t alpha = want_text_panel_alpha ? (uint8_t)props->text_panel_alpha : menu->settings.text_panel_alpha;
        ui_components_set_text_panel(enabled, alpha);
        playlist_override.text_panel_applied = true;
    }
    if (want_screensaver_logo) {
        free(playlist_override.pending_screensaver_logo_path);
        playlist_override.pending_screensaver_logo_path = strdup(strip_fs_prefix((char *)props->screensaver_logo));
    }

    if (want_viz_style) {
        ui_components_background_set_visualizer_style(props->viz_style);
        playlist_override.viz_style_applied = true;
    }
    if (want_viz_intensity) {
        ui_components_background_set_visualizer_intensity(props->viz_intensity);
        playlist_override.viz_intensity_applied = true;
    }
    if (want_grid_view) {
        playlist_grid_view_enabled = (props->grid_view == 1);
        playlist_override.grid_view_applied = true;
        playlist_grid_runtime_override = -1;
        playlist_grid_slots_clear();
    }

    if (want_bg) {
        free(playlist_override.background_path);
        playlist_override.background_path = strdup(props->bg);
        playlist_override.background_deferred = (playlist_override.background_path != NULL);
    }

    // Disabled for now: too intrusive during normal browsing.
    (void)want_theme;
    (void)want_bgm;
    (void)want_bg;
    (void)want_viz_style;
    (void)want_viz_intensity;
    if (playlist_override.pending_bgm_path || playlist_override.pending_screensaver_logo_path || playlist_override.background_deferred) {
        playlist_override.deferred_heavy_frames = 1;
    }
    playlist_toast.frames_left = 0;
}

static void browser_apply_playlist_overrides_deferred(menu_t *menu) {
    if (!menu || !playlist_override.active) {
        return;
    }

    if (playlist_override.deferred_heavy_frames > 0) {
        playlist_override.deferred_heavy_frames--;
        return;
    }

    if (playlist_override.background_deferred && playlist_override.background_path) {
        uint64_t start_us = get_ticks_us();
        if (ui_components_background_load_temporary_cached(playlist_override.background_path)) {
            playlist_override.background_applied = true;
            playlist_override.background_deferred = false;
            playlist_perf.bg_cache_ms = elapsed_ms(start_us);
            debugf("Playlist perf: background cache load=%lums path=%s\n", (unsigned long)playlist_perf.bg_cache_ms, playlist_override.background_path);
            return;
        }
        if (!png_decoder_is_busy()) {
            start_us = get_ticks_us();
            png_err_t err = png_decoder_start(
                playlist_override.background_path,
                640,
                480,
                playlist_background_callback,
                NULL
            );
            if (err == PNG_OK) {
                playlist_override.background_loading = true;
                playlist_override.background_deferred = false;
                playlist_perf.bg_decode_queue_ms = elapsed_ms(start_us);
                debugf("Playlist perf: background decode queued=%lums path=%s\n", (unsigned long)playlist_perf.bg_decode_queue_ms, playlist_override.background_path);
            }
        }
        return;
    }

    if (playlist_override.pending_screensaver_logo_path) {
        if (menu->settings.screensaver_logo_file &&
            strcmp(menu->settings.screensaver_logo_file, playlist_override.pending_screensaver_logo_path) == 0) {
            free(playlist_override.pending_screensaver_logo_path);
            playlist_override.pending_screensaver_logo_path = NULL;
            return;
        }
        if (menu->settings.screensaver_logo_file) {
            free(menu->settings.screensaver_logo_file);
        }
        menu->settings.screensaver_logo_file = strdup(playlist_override.pending_screensaver_logo_path);
        if (menu->settings.screensaver_logo_file) {
            menu->screensaver_logo_reload_requested = true;
            playlist_override.screensaver_logo_applied = true;
        }
        free(playlist_override.pending_screensaver_logo_path);
        playlist_override.pending_screensaver_logo_path = NULL;
        return;
    }

    if (playlist_override.pending_bgm_path) {
        if (menu->runtime_bgm_override_file &&
            strcmp(menu->runtime_bgm_override_file, playlist_override.pending_bgm_path) == 0) {
            free(playlist_override.pending_bgm_path);
            playlist_override.pending_bgm_path = NULL;
            return;
        }
        free(menu->runtime_bgm_override_file);
        menu->runtime_bgm_override_file = strdup(playlist_override.pending_bgm_path);
        menu->bgm_reload_requested = true;
        playlist_override.bgm_applied = (menu->runtime_bgm_override_file != NULL);
        free(playlist_override.pending_bgm_path);
        playlist_override.pending_bgm_path = NULL;
    }
}

static bool smart_playlist_execute(menu_t *menu, smart_playlist_query_t *query, int *playlist_capacity) {
    if (!query->enabled) {
        return true;
    }

    smart_playlist_entry_t *generated = NULL;
    int generated_count = 0;
    int generated_capacity = 0;

    if (query->root_count == 0) {
        path_t *default_root = path_init(menu->storage_prefix, "/");
        if (!default_root) {
            return false;
        }
        smart_playlist_add_root(query, path_get(default_root));
        path_free(default_root);
    }

    for (int i = 0; i < query->root_count; i++) {
        path_t *root = path_create(query->roots[i]);
        if (!root) {
            continue;
        }
        bool ok = smart_playlist_collect_dir(menu, root, query, &generated, &generated_count, &generated_capacity, 0);
        path_free(root);
        if (!ok) {
            for (int j = 0; j < generated_count; j++) {
                smart_playlist_entry_free(&generated[j]);
            }
            free(generated);
            return false;
        }
    }

    generated_count = smart_playlist_deduplicate_entries(generated, generated_count);
    smart_playlist_sort_entries(generated, generated_count, query);
    for (int i = 0; i < generated_count; i++) {
        if (!playlist_append_rom_entry_unique(menu, generated[i].path, playlist_capacity)) {
            for (int j = i; j < generated_count; j++) {
                smart_playlist_entry_free(&generated[j]);
            }
            free(generated);
            return false;
        }
        smart_playlist_entry_free(&generated[i]);
    }
    free(generated);
    return true;
}

static bool load_playlist (menu_t *menu) {
    uint64_t open_start_us = get_ticks_us();
    uint64_t parse_start_us = 0;
    uint64_t smart_start_us = 0;
    uint32_t parse_ms = 0;
    uint32_t smart_ms = 0;
    uint32_t cache_save_ms = 0;
    const char *cache_source = NULL;
    // Fast reuse: if we already have this playlist loaded, skip stat entirely.
    if (playlist_active_loaded &&
        playlist_active_static &&
        menu->browser.playlist &&
        menu->browser.entries > 0 &&
        strcmp(playlist_active_path, path_get(menu->browser.directory)) == 0) {
        playlist_perf_reset();
        playlist_perf_commit(menu, "reuse", 0, 0, 0, 0, menu->browser.entries);
        playlist_recent_remember(path_get(menu->browser.directory));
        browser_apply_playlist_overrides(menu, &playlist_active_props);
        return false;
    }

    playlist_perf_reset();

    // Read entire m3u file in one SD card operation.
    char *file_buf = NULL;
    size_t file_size = 0;
    {
        FILE *f = fopen(path_get(menu->browser.directory), "rb");
        if (f == NULL) {
            return true;
        }
        fseek(f, 0, SEEK_END);
        long flen = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (flen <= 0 || flen >= 65536) {
            fclose(f);
            return true;
        }
        file_buf = malloc((size_t)flen + 1);
        if (!file_buf) {
            fclose(f);
            return true;
        }
        size_t nread = fread(file_buf, 1, (size_t)flen, f);
        fclose(f);
        file_buf[nread] = '\0';
        file_size = nread;
    }

    uint64_t content_hash = fnv1a64_buf(file_buf, file_size);

    browser_list_free(menu);
    int playlist_capacity = 0;

    menu->browser.playlist = true;

    playlist_props_t props = PLAYLIST_PROPS_DEFAULT;

    if (playlist_cache_try_load(
            menu,
            path_get(menu->browser.directory),
            content_hash,
            file_size,
            &playlist_capacity,
            &props,
            &cache_source)) {
        free(file_buf);
        if (menu->browser.entries > 0) {
            menu->browser.selected = 0;
            menu->browser.entry = &menu->browser.list[menu->browser.selected];
        }

        menu->browser.sort_mode = BROWSER_SORT_CUSTOM;
        browser_apply_sort(menu);

        playlist_recent_remember(path_get(menu->browser.directory));
        playlist_active_store(path_get(menu->browser.directory), content_hash, true, &props);
        playlist_perf_commit(menu, cache_source ? cache_source : "cache", elapsed_ms(open_start_us), 0, 0, 0, menu->browser.entries);
        if (props.grid_view) {
            playlist_grid_prewarm_start(menu);
        }
        browser_apply_playlist_overrides(menu, &props);
        char *playlist_context_path = playlist_find_context_text_path(menu->browser.directory);
        if (playlist_context_path) {
            playlist_prepend_text_entry(menu, playlist_context_path, &playlist_capacity);
            free(playlist_context_path);
        }

        playlist_props_free(&props);
            return false;
    }

    path_t *playlist_dir = path_clone(menu->browser.directory);
    path_pop(playlist_dir);
    smart_playlist_query_t smart_query;
    smart_playlist_query_init(&smart_query);
    parse_start_us = get_ticks_us();

    // Parse lines from memory buffer — no further SD card I/O.
    char *buf_cursor = file_buf;
    while (buf_cursor && *buf_cursor) {
        char *line_start = buf_cursor;
        char *eol = strchr(buf_cursor, '\n');
        if (eol) {
            *eol = '\0';
            buf_cursor = eol + 1;
        } else {
            buf_cursor = NULL;
        }
        // Strip trailing \r for Windows-style line endings.
        size_t line_len = strlen(line_start);
        if (line_len > 0 && line_start[line_len - 1] == '\r') {
            line_start[line_len - 1] = '\0';
        }

        char *trimmed = trim_line(line_start);
        if (trimmed == NULL || trimmed[0] == '\0') {
            continue;
        }
        if (trimmed[0] == '#') {
            playlist_parse_directive(menu, playlist_dir, trimmed, &props, &smart_query);
            continue;
        }

        path_t *entry_path = NULL;
        if (strstr(trimmed, ":/") != NULL) {
            entry_path = path_create(trimmed);
        } else if (trimmed[0] == '/') {
            entry_path = path_init(menu->storage_prefix, trimmed);
        } else {
            entry_path = path_clone(playlist_dir);
            path_push(entry_path, trimmed);
        }

        char *normalized = normalize_path(path_get(entry_path));
        if (!normalized) {
            path_free(entry_path);
            free(file_buf);
            playlist_props_free(&props);
            browser_list_free(menu);
            path_free(playlist_dir);
            return true;
        }

        if (!file_has_extensions(normalized, n64_rom_extensions)) {
            free(normalized);
            path_free(entry_path);
            continue;
        }

        if (!playlist_append_rom_entry(menu, normalized, &playlist_capacity)) {
            free(normalized);
            path_free(entry_path);
            free(file_buf);
            playlist_props_free(&props);
            browser_list_free(menu);
            path_free(playlist_dir);
            return true;
        }
        free(normalized);

        path_free(entry_path);
    }
    free(file_buf);
    parse_ms = elapsed_ms(parse_start_us);

    if (smart_query.enabled) {
        smart_start_us = get_ticks_us();
        if (!smart_playlist_execute(menu, &smart_query, &playlist_capacity)) {
            playlist_props_free(&props);
            browser_list_free(menu);
            path_free(playlist_dir);
            return true;
        }
        smart_ms = elapsed_ms(smart_start_us);
    }

    if (menu->browser.entries > 0) {
        menu->browser.selected = 0;
        menu->browser.entry = &menu->browser.list[menu->browser.selected];
    }

    // Preserve m3u order by default for playlist views.
    menu->browser.sort_mode = BROWSER_SORT_CUSTOM;
    browser_apply_sort(menu);

    if (!smart_query.enabled) {
        uint64_t cache_save_start_us = get_ticks_us();
        playlist_mem_cache_save(
            menu,
            path_get(menu->browser.directory),
            content_hash,
            &props
        );
        playlist_cache_save(
            menu,
            path_get(menu->browser.directory),
            content_hash,
            file_size,
            &props
        );
        playlist_recent_remember(path_get(menu->browser.directory));
        cache_save_ms = elapsed_ms(cache_save_start_us);
        playlist_active_store(path_get(menu->browser.directory), content_hash, true, &props);
    } else {
        playlist_active_clear();
    }

    playlist_perf_commit(menu, smart_query.enabled ? "smart" : "parse", elapsed_ms(open_start_us), parse_ms, smart_ms, cache_save_ms, menu->browser.entries);
    char *playlist_context_path = playlist_find_context_text_path(menu->browser.directory);
    if (playlist_context_path) {
        playlist_prepend_text_entry(menu, playlist_context_path, &playlist_capacity);
        free(playlist_context_path);
    }

    if (props.grid_view) {
        playlist_grid_prewarm_start(menu);
    }
    browser_apply_playlist_overrides(menu, &props);
    path_free(playlist_dir);
    playlist_props_free(&props);

    return false;
}

static char *normalize_path (const char *path) {
    if (path == NULL) {
        return NULL;
    }

    size_t path_len = strlen(path);
    if (path_len >= 1024) {
        return NULL;
    }

    const char *prefix_pos = strstr(path, ":/");
    size_t prefix_len = 0;
    if (prefix_pos != NULL) {
        prefix_len = (size_t)(prefix_pos - path) + 2;
    } else if (path[0] == '/') {
        prefix_len = 1;
    }

    static char work[1024];
    memcpy(work, path, path_len + 1);

    static char *seg_ptrs[128];
    size_t seg_count = 0;

    char *cursor = work + prefix_len;
    while (*cursor) {
        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        char *seg = cursor;
        while (*cursor && *cursor != '/') {
            cursor++;
        }
        if (*cursor) {
            *cursor++ = '\0';
        }

        if (strcmp(seg, ".") == 0 || strcmp(seg, "") == 0) {
            continue;
        }
        if (strcmp(seg, "..") == 0) {
            if (seg_count > 0) {
                seg_count--;
            }
            continue;
        }
        if (seg_count < 128) {
            seg_ptrs[seg_count++] = seg;
        }
    }

    char *out = malloc(path_len + 2);
    if (!out) {
        return NULL;
    }

    size_t out_len = 0;
    if (prefix_len > 0) {
        memcpy(out, path, prefix_len);
        out_len = prefix_len;
    }

    for (size_t i = 0; i < seg_count; i++) {
        if (out_len > 0 && out[out_len - 1] != '/') {
            out[out_len++] = '/';
        }
        size_t seg_len = strlen(seg_ptrs[i]);
        memcpy(out + out_len, seg_ptrs[i], seg_len);
        out_len += seg_len;
    }

    if (out_len == 0) {
        out[out_len++] = '.';
    }

    out[out_len] = '\0';
    return out;
}

static bool load_directory (menu_t *menu) {
    int result;
    dir_t info;
    int directory_capacity = 0;

    if (menu->browser.playlist || playlist_override.active) {
        browser_restore_playlist_overrides(menu);
    }

    browser_list_free(menu);

    path_t *path = path_clone(menu->browser.directory);

    result = dir_findfirst(path_get(path), &info);

    while (result == 0) {
        bool hide = false;

        if (!menu->settings.show_protected_entries) {
            path_push(path, info.d_name);
            hide = path_is_hidden(path);
            path_pop(path);
        }

        if (!menu->settings.show_saves_folder) {
            path_push(path, info.d_name);
            // Skip the "saves" directory if it is hidden (this is case sensitive)
            if (strcmp(info.d_name, SAVE_DIRECTORY_NAME) == 0) {
                hide = true;
            }
            path_pop(path);
        }

        if (!hide) {
            // Datel sidecar cheat files are ROM implementation details and clutter normal browsing.
            if (info.d_type != DT_DIR && string_ends_with_ignore_case(info.d_name, ".datel.txt")) {
                result = dir_findnext(path_get(path), &info);
                continue;
            }
            // Hide metadata/config sidecars from normal browsing.
            if (info.d_type != DT_DIR && string_ends_with_ignore_case(info.d_name, ".ini")) {
                result = dir_findnext(path_get(path), &info);
                continue;
            }
            if (!browser_64dd_picker_entry_visible(menu, path, &info)) {
                result = dir_findnext(path_get(path), &info);
                continue;
            }

            if (!browser_reserve_entry_capacity(menu, &directory_capacity, 1)) {
                path_free(path);
                browser_list_free(menu);
                return true;
            }

            entry_t *entry = &menu->browser.list[menu->browser.entries++];
            memset(entry, 0, sizeof(*entry));

            entry->name = strdup(info.d_name);
            if (!entry->name) {
                path_free(path);
                browser_list_free(menu);
                return true;
            }
            entry->path = NULL;

            if (info.d_type == DT_DIR) {
                entry->type = ENTRY_TYPE_DIR;
            } else if (file_has_extensions(entry->name, n64_rom_extensions)) {
                entry->type = ENTRY_TYPE_ROM;
            } else if (file_has_extensions(entry->name, disk_extensions)) {
                entry->type = ENTRY_TYPE_DISK;
            } else if (file_has_extensions(entry->name, patch_extensions)) {
                entry->type = ENTRY_TYPE_ROM_PATCH;
            } else if (file_has_extensions(entry->name, cheat_extensions)) {
                entry->type = ENTRY_TYPE_ROM_CHEAT;
            } else if (file_has_extensions(entry->name, emulator_extensions)) {
                entry->type = ENTRY_TYPE_EMULATOR;
            } else if (file_has_extensions(entry->name, save_extensions)) {
                entry->type = ENTRY_TYPE_SAVE;
            } else if (file_has_extensions(entry->name, image_extensions)) {
                entry->type = ENTRY_TYPE_IMAGE;
            } else if (file_has_extensions(entry->name, text_extensions)) {
                entry->type = ENTRY_TYPE_TEXT;
            } else if (file_has_extensions(entry->name, music_extensions)) {
                entry->type = ENTRY_TYPE_MUSIC;
            } else if (file_has_extensions(entry->name, archive_extensions)) {
                entry->type = ENTRY_TYPE_ARCHIVE;
            } else if (file_has_extensions(entry->name, playlist_extensions)) {
                entry->type = ENTRY_TYPE_PLAYLIST;
            } else {
                entry->type = ENTRY_TYPE_OTHER;
            }

            entry->size = info.d_size;
            entry->index = menu->browser.entries - 1;
        }

        result = dir_findnext(path_get(path), &info);
    }

    path_free(path);

    if (result < -1) {
        browser_list_free(menu);
        return true;
    }

    if (menu->browser.entries > 0) {
        menu->browser.selected = 0;
        menu->browser.entry = &menu->browser.list[menu->browser.selected];
    }

    browser_apply_sort(menu);

    return false;
}

static const char *format_clock_12h(time_t now, char *buffer, size_t buffer_len) {
    if (!buffer || buffer_len == 0) {
        return "";
    }
    struct tm *time_info = localtime(&now);
    if (!time_info) {
        return "";
    }
    if (strftime(buffer, buffer_len, "%m/%d %I:%M %p", time_info) == 0) {
        return "";
    }
    return buffer;
}

static bool reload_directory (menu_t *menu) {
    int selected = menu->browser.selected;

    if (load_directory(menu)) {
        return true;
    }

    menu->browser.selected = selected;
    if (menu->browser.selected >= menu->browser.entries) {
        menu->browser.selected = menu->browser.entries - 1;
    }
    menu->browser.entry = menu->browser.selected >= 0 ? &menu->browser.list[menu->browser.selected] : NULL;

    return false;
}

static bool push_directory (menu_t *menu, char *directory, bool archive) {
    path_t *previous_directory = path_clone(menu->browser.directory);

    path_push(menu->browser.directory, directory);

    if (archive ? load_archive(menu) : load_directory(menu)) {
        path_free(menu->browser.directory);
        menu->browser.directory = previous_directory;
        return true;
    }

    path_free(previous_directory);

    return false;
}

static bool pop_directory (menu_t *menu) {
    path_t *previous_directory = path_clone(menu->browser.directory);

    path_pop(menu->browser.directory);

    if (load_directory(menu)) {
        path_free(menu->browser.directory);
        menu->browser.directory = previous_directory;
        return true;
    }

    for (uint16_t i = 0; i < menu->browser.entries; i++) {
        if (strcmp(menu->browser.list[i].name, path_last_get(previous_directory)) == 0) {
            menu->browser.selected = i;
            menu->browser.entry = &menu->browser.list[menu->browser.selected];
            break;
        }
    }

    path_free(previous_directory);

    return false;
}

static bool select_file (menu_t *menu, path_t *file) {
    path_t *previous_directory = path_clone(menu->browser.directory);
    path_t *target_file = NULL;

    const char *raw = file ? path_get(file) : NULL;
    if (raw == NULL || raw[0] == '\0') {
        path_free(previous_directory);
        return true;
    }

    if (strstr(raw, ":/") != NULL) {
        target_file = path_clone(file);
    } else if (raw[0] == '/') {
        // Backward compatibility: historical entries may store root-relative SD paths.
        char *raw_copy = strdup(raw);
        if (!raw_copy) {
            path_free(previous_directory);
            return true;
        }
        target_file = path_init(menu->storage_prefix, raw_copy);
        free(raw_copy);
    } else {
        target_file = path_init(menu->storage_prefix, "/");
        char *raw_copy = strdup(raw);
        if (!raw_copy) {
            path_free(previous_directory);
            path_free(target_file);
            return true;
        }
        path_push(target_file, raw_copy);
        free(raw_copy);
    }

    if (!target_file) {
        path_free(previous_directory);
        return true;
    }

    path_free(menu->browser.directory);
    menu->browser.directory = path_clone(target_file);
    path_pop(menu->browser.directory);

    if (load_directory(menu)) {
        path_free(menu->browser.directory);
        menu->browser.directory = previous_directory;
        path_free(target_file);
        return true;
    }

    bool found = false;
    for (uint16_t i = 0; i < menu->browser.entries; i++) {
        if (strcmp(menu->browser.list[i].name, path_last_get(target_file)) == 0) {
            menu->browser.selected = i;
            menu->browser.entry = &menu->browser.list[menu->browser.selected];
            found = true;
            break;
        }
    }

    if (!found && menu->browser.entries > 0) {
        menu->browser.selected = 0;
        menu->browser.entry = &menu->browser.list[0];
    }

    path_free(target_file);
    path_free(previous_directory);

    return false;
}

static bool push_playlist (menu_t *menu, char *playlist) {
    path_t *previous_directory = path_clone(menu->browser.directory);

    path_push(menu->browser.directory, playlist);

    if (load_playlist(menu)) {
        path_free(menu->browser.directory);
        menu->browser.directory = previous_directory;
        return true;
    }

    path_free(previous_directory);

    return false;
}

static void show_properties (menu_t *menu, void *arg) {
    menu->next_mode = menu->browser.entry->type == ENTRY_TYPE_ARCHIVED ? MENU_MODE_EXTRACT_FILE : MENU_MODE_FILE_INFO;
}

static void delete_entry (menu_t *menu, void *arg) {
    path_t *path = path_clone_push(menu->browser.directory, menu->browser.entry->name);

    if (remove(path_get(path))) {
        menu->browser.valid = false;
        if (menu->browser.entry->type == ENTRY_TYPE_DIR) {
            menu_show_error(menu, "Couldn't delete directory\nDirectory might not be empty");
        } else {
            menu_show_error(menu, "Couldn't delete file");
        }
        path_free(path);
        return;
    }

    path_free(path);

    if (reload_directory(menu)) {
        menu->browser.valid = false;
        menu_show_error(menu, "Couldn't refresh directory contents after delete operation");
    }
}

static void extract_entry (menu_t *menu, void *arg) {
    menu->load_pending.extract_file = true;
    menu->next_mode = MENU_MODE_EXTRACT_FILE;
}

static void set_default_directory (menu_t *menu, void *arg) {
    free(menu->settings.default_directory);
    menu->settings.default_directory = strdup(strip_fs_prefix(path_get(menu->browser.directory)));
    settings_save(&menu->settings);
}

static void cycle_sort_mode (menu_t *menu, void *arg) {
    (void)arg;
    menu->browser.sort_mode = (browser_sort_t)(((int)menu->browser.sort_mode + 1) % 3);
    menu->settings.browser_sort_mode = (int)menu->browser.sort_mode;
    settings_save(&menu->settings);
    browser_apply_sort(menu);
}

static void set_random_mode(menu_t *menu, void *arg) {
    int mode = (int)(intptr_t)arg;
    if (mode < RANDOM_MODE_ANY_GAME || mode > RANDOM_MODE_SMART) {
        mode = RANDOM_MODE_ANY_GAME;
    }
    menu->settings.browser_random_mode = mode;
    settings_save(&menu->settings);
}

static int get_random_mode_selection(menu_t *menu) {
    if (!menu) {
        return 0;
    }
    if (menu->settings.browser_random_mode < RANDOM_MODE_ANY_GAME || menu->settings.browser_random_mode > RANDOM_MODE_SMART) {
        return 0;
    }
    return menu->settings.browser_random_mode;
}

static component_context_menu_t random_mode_context_menu = {
    .get_default_selection = get_random_mode_selection,
    .list = {
        { .text = "Any game", .action = set_random_mode, .arg = (void *)(intptr_t)RANDOM_MODE_ANY_GAME },
        { .text = "Unplayed", .action = set_random_mode, .arg = (void *)(intptr_t)RANDOM_MODE_UNPLAYED },
        { .text = "Underplayed", .action = set_random_mode, .arg = (void *)(intptr_t)RANDOM_MODE_UNDERPLAYED },
        { .text = "Favorites", .action = set_random_mode, .arg = (void *)(intptr_t)RANDOM_MODE_FAVORITES },
        { .text = "Smart", .action = set_random_mode, .arg = (void *)(intptr_t)RANDOM_MODE_SMART },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static component_context_menu_t entry_context_menu = {
    .list = {
        { .text = "Show entry properties", .action = show_properties },
        { .text = "Delete selected entry", .action = delete_entry },
        { .text = "Set current directory as default", .action = set_default_directory },
        { .text = "Cycle sorting mode", .action = cycle_sort_mode },
        { .text = "Random mode...", .submenu = &random_mode_context_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static component_context_menu_t playlist_context_menu = {
    .list = {
        { .text = "Show entry properties", .action = show_properties },
        { .text = "Cycle sorting mode", .action = cycle_sort_mode },
        { .text = "Random mode...", .submenu = &random_mode_context_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static component_context_menu_t archive_context_menu = {
    .list = {
        { .text = "Show entry properties", .action = show_properties },
        { .text = "Extract selected entry", .action = extract_entry },
        { .text = "Cycle sorting mode", .action = cycle_sort_mode },
        { .text = "Random mode...", .submenu = &random_mode_context_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void set_menu_next_mode (menu_t *menu, void *arg) {
    menu_mode_t next_mode = (menu_mode_t) (arg);
    menu->next_mode = next_mode;
}

static void open_virtual_pak_center(menu_t *menu, void *arg) {
    (void)arg;
    menu->utility_return_mode = MENU_MODE_BROWSER;
    menu->next_mode = MENU_MODE_VIRTUAL_PAK_CENTER;
}

static component_context_menu_t settings_context_menu = {
    .list = {
        { .text = "Random mode...", .submenu = &random_mode_context_menu },
        { .text = "Controller Pak manager", .action = set_menu_next_mode, .arg = (void *) (MENU_MODE_CONTROLLER_PAKFS) },
        { .text = "Virtual Pak center", .action = open_virtual_pak_center },
        { .text = "Menu settings", .action = set_menu_next_mode, .arg = (void *) (MENU_MODE_SETTINGS_EDITOR) },
        { .text = "Time (RTC) settings", .action = set_menu_next_mode, .arg = (void *) (MENU_MODE_RTC) },
        { .text = "Menu information", .action = set_menu_next_mode, .arg = (void *) (MENU_MODE_CREDITS) },
        { .text = "Flashcart information", .action = set_menu_next_mode, .arg = (void *) (MENU_MODE_FLASHCART) },
        { .text = "N64 information", .action = set_menu_next_mode, .arg = (void *) (MENU_MODE_SYSTEM_INFO) },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void process (menu_t *menu) {
    if (playlist_toast.frames_left > 0) {
        playlist_toast.frames_left--;
    }

    if (playlist_override.active &&
        playlist_override.background_path &&
        !playlist_override.background_applied &&
        !playlist_override.background_loading &&
        !png_decoder_is_busy()) {
        png_err_t err = png_decoder_start(playlist_override.background_path, 640, 480, playlist_background_callback, NULL);
        if (err == PNG_OK) {
            playlist_override.background_loading = true;
        }
    }

    component_context_menu_t *active_context_menu = &entry_context_menu;
    if (menu->browser.archive) {
        active_context_menu = &archive_context_menu;
    } else if (menu->browser.playlist) {
        active_context_menu = &playlist_context_menu;
    }

    if (ui_components_context_menu_process(menu, active_context_menu)) {
        return;
    }

    if (ui_components_context_menu_process(menu, &settings_context_menu)) {
        return;
    }

    int scroll_speed = menu->actions.go_fast ? 10 : 1;
    bool moved_selection = false;
    bool use_playlist_grid = browser_use_playlist_grid(menu);

    if (menu->browser.entries > 1) {
        bool moved = false;
        if (use_playlist_grid) {
            const int grid_cols = 4;
            const int page_size = 12;
            if (menu->actions.go_up) {
                menu->browser.selected -= (menu->actions.go_fast ? page_size : grid_cols);
                moved = true;
            } else if (menu->actions.go_down) {
                menu->browser.selected += (menu->actions.go_fast ? page_size : grid_cols);
                moved = true;
            } else if (menu->actions.go_left) {
                menu->browser.selected -= 1;
                moved = true;
            } else if (menu->actions.go_right) {
                menu->browser.selected += 1;
                moved = true;
            }
        } else {
            if (menu->actions.go_up) {
                menu->browser.selected -= scroll_speed;
                moved = true;
            } else if (menu->actions.go_down) {
                menu->browser.selected += scroll_speed;
                moved = true;
            }
        }

        if (moved) {
            moved_selection = true;
            if (menu->browser.selected < 0) {
                menu->browser.selected = 0;
            }
            if (menu->browser.selected >= menu->browser.entries) {
                menu->browser.selected = menu->browser.entries - 1;
            }
            sound_play_effect(SFX_CURSOR);
        }
        menu->browser.entry = &menu->browser.list[menu->browser.selected];
    }

    if (menu->actions.toggle_view && menu->browser.playlist && menu->browser.picker == BROWSER_PICKER_NONE) {
        bool currently_grid = browser_use_playlist_grid(menu);
        playlist_grid_runtime_override = currently_grid ? 0 : 1;
        playlist_grid_slots_clear();
        sound_play_effect(SFX_SETTING);
        return;
    }

    if (menu->actions.lz_context && menu->browser.entries > 1) {
        int next = browser_pick_random_index(menu);
        if (next >= 0 && next < menu->browser.entries) {
            menu->browser.selected = next;
            menu->browser.entry = &menu->browser.list[menu->browser.selected];
            moved_selection = true;
            sound_play_effect(SFX_CURSOR);
        }
        return;
    }

    browser_playlist_grid_prepare(menu, moved_selection);

    // Fail-safe: in empty directories/playlists, allow A or B to go up one level.
    if (menu->browser.entries == 0 &&
        (menu->actions.enter || menu->actions.back) &&
        !browser_is_picker_root(menu) &&
        !path_is_root(menu->browser.directory)) {
        if (pop_directory(menu)) {
            menu->browser.valid = false;
            menu_show_error(menu, "Couldn't open last directory");
        }
        sound_play_effect(SFX_EXIT);
        return;
    }

    if (menu->actions.enter && menu->browser.entry) {
        sound_play_effect(SFX_ENTER);
        if (browser_try_pick_menu_music_file(menu)) {
            return;
        }
        if (browser_try_pick_screensaver_logo_file(menu)) {
            return;
        }
        if (browser_try_pick_64dd_disk_file(menu)) {
            return;
        }
        switch (menu->browser.entry->type) {
            case ENTRY_TYPE_ARCHIVE:
                if (push_directory(menu, menu->browser.entry->name, true)) {
                    menu->browser.valid = false;
                    menu_show_error(menu, "Couldn't open file archive");
                }
                break;
            case ENTRY_TYPE_PLAYLIST:
                if (push_playlist(menu, menu->browser.entry->name)) {
                    menu->browser.valid = false;
                    menu_show_error(menu, "Couldn't open playlist");
                }
                break;
            case ENTRY_TYPE_ARCHIVED:
                menu->next_mode = MENU_MODE_EXTRACT_FILE;
                break;
            case ENTRY_TYPE_DIR:
                if (push_directory(menu, menu->browser.entry->name, false)) {
                    menu->browser.valid = false;
                    menu_show_error(menu, "Couldn't open next directory");
                }
                break;
            case ENTRY_TYPE_DISK:
                menu->next_mode = MENU_MODE_LOAD_DISK;
                break;
            case ENTRY_TYPE_EMULATOR:
                menu->next_mode = MENU_MODE_LOAD_EMULATOR;
                break;
            case ENTRY_TYPE_IMAGE:
                menu->next_mode = MENU_MODE_IMAGE_VIEWER;
                break;
            case ENTRY_TYPE_MUSIC:
                menu->next_mode = MENU_MODE_MUSIC_PLAYER;
                break;
            case ENTRY_TYPE_ROM:
                path_free(menu->load.rom_path);
                menu->load.rom_path = NULL;
                menu->load.back_mode = MENU_MODE_BROWSER;
                menu->next_mode = MENU_MODE_LOAD_ROM;
                break;
            case ENTRY_TYPE_ROM_CHEAT:
                menu->next_mode = MENU_MODE_FILE_INFO; // FIXME: Implement MENU_MODE_LOAD_ROM_CHEAT.
                break;
            case ENTRY_TYPE_ROM_PATCH:
                menu->next_mode = MENU_MODE_FILE_INFO; // FIXME: Implement MENU_MODE_LOAD_ROM_PATCH.
                break;
            case ENTRY_TYPE_TEXT:
                menu->next_mode = MENU_MODE_TEXT_VIEWER;
                break;

            default:
                menu->next_mode = MENU_MODE_FILE_INFO;
                break;
        }
    } else if (menu->actions.back && browser_is_picker_root(menu)) {
        browser_close_picker(menu, menu->browser.picker_return_mode ? menu->browser.picker_return_mode : MENU_MODE_BROWSER);
        sound_play_effect(SFX_EXIT);
    } else if (menu->actions.back && !path_is_root(menu->browser.directory)) {
        if (pop_directory(menu)) {
            menu->browser.valid = false;
            menu_show_error(menu, "Couldn't open last directory");
        }
        sound_play_effect(SFX_EXIT);
    } else if (menu->actions.options && menu->browser.entry) {
        ui_components_context_menu_show(active_context_menu);
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.settings) {
        ui_components_context_menu_show(&settings_context_menu);
        sound_play_effect(SFX_SETTING);
    } else if (!use_playlist_grid && menu->actions.go_right) {
        menu->next_mode = MENU_MODE_HISTORY;
        sound_play_effect(SFX_CURSOR);
    } else if (!use_playlist_grid && menu->actions.go_left) {
        menu->next_mode = MENU_MODE_PLAYTIME;
        sound_play_effect(SFX_CURSOR);
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    ui_components_tabs_common_draw(0);

    ui_components_layout_draw_tabbed();

    bool use_playlist_grid = browser_use_playlist_grid(menu);

    ui_components_set_file_list_top_inset(0);

    if (use_playlist_grid) {
        browser_playlist_grid_draw(menu);
    } else {
        ui_components_set_file_list_last_played_context(&menu->playtime, menu->current_time);
        ui_components_file_list_draw(menu->browser.list, menu->browser.entries, menu->browser.selected);
    }

    const char *action = NULL;

    if (menu->browser.entry) {
        switch (menu->browser.entry->type) {
            case ENTRY_TYPE_DIR: action = "A: Enter"; break;
            case ENTRY_TYPE_ROM: action = "A: Load"; break;
            case ENTRY_TYPE_DISK:
                action = (menu->browser.picker == BROWSER_PICKER_64DD_DISK_LAUNCH || menu->browser.picker == BROWSER_PICKER_64DD_DISK_DEFAULT) ? "A: Select" : "A: Load";
                break;
            case ENTRY_TYPE_IMAGE: action = menu->browser.picker == BROWSER_PICKER_SCREENSAVER_LOGO ? "A: Select" : "A: Show"; break;
            case ENTRY_TYPE_TEXT: action = "A: View"; break;
            case ENTRY_TYPE_MUSIC: action = menu->browser.picker == BROWSER_PICKER_MENU_BGM ? "A: Select" : "A: Play"; break;
            case ENTRY_TYPE_ARCHIVE: action = "A: Open"; break;
            case ENTRY_TYPE_PLAYLIST: action = "A: Open"; break;
            default: action = "A: Info"; break;
        }
    }

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "%s\n"
        "^%02XB: Back^00 | ^%02XL: %s^00",
        menu->browser.entries == 0 ? "" : action,
        path_is_root(menu->browser.directory) ? STL_GRAY : STL_DEFAULT,
        menu->browser.entries > 1 ? STL_DEFAULT : STL_GRAY,
        "Random"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_RIGHT, VALIGN_TOP,
        "^%02XStart: Settings^00\n"
        "^%02XR: Options^00 | Sort:%s",
        menu->browser.entries == 0 ? STL_GRAY : STL_DEFAULT,
        menu->browser.entries == 0 ? STL_GRAY : STL_DEFAULT,
        browser_sort_mode_string(menu)
    );

    if (menu->current_time >= 0) {
        char datetime[32];
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "C-▼▲ Scroll | ◀ ▶ Tabs\n"
            "%s",
            format_clock_12h(menu->current_time, datetime, sizeof(datetime))
        );
    } else {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "C-▼▲ Scroll | ◀ ▶ Tabs\n"
            "\n"
        );
    }

    if (menu->browser.archive) {
        ui_components_context_menu_draw(&archive_context_menu);
    } else if (menu->browser.playlist) {
        ui_components_context_menu_draw(&playlist_context_menu);
    } else {
        ui_components_context_menu_draw(&entry_context_menu);
    }

    ui_components_context_menu_draw(&settings_context_menu);

    playlist_toast_draw();

    if (browser_virtual_pak_recovery_active) {
        if (browser_virtual_pak_recovery_failed) {
            ui_components_messagebox_draw(browser_virtual_pak_recovery_message);
        } else {
            ui_components_loader_draw(0.0f, "Recovering virtual Pak...");
        }
    }

    rdpq_detach_show();
}

void view_browser_init (menu_t *menu) {
    playlist_recent_init(menu);
    browser_virtual_pak_recovery_active = virtual_pak_has_pending_sync();
    browser_virtual_pak_recovery_failed = false;
    browser_virtual_pak_recovery_retry_cooldown = 0;
    browser_virtual_pak_recovery_snoozed = false;
    snprintf(browser_virtual_pak_recovery_message, sizeof(browser_virtual_pak_recovery_message),
        "Recovering virtual Pak...\n\nKeep the same physical Controller Pak inserted in controller 1.");

    if (!menu->browser.valid) {
        ui_components_context_menu_init(&entry_context_menu);
        ui_components_context_menu_init(&archive_context_menu);
        ui_components_context_menu_init(&playlist_context_menu);
        ui_components_context_menu_init(&settings_context_menu);
        if (load_directory(menu)) {
            path_free(menu->browser.directory);
            menu->browser.directory = path_init(menu->storage_prefix, "");
            menu_show_error(menu, "Error while opening initial directory");
        } else {
            menu->browser.valid = true;
        }
    }

    if (menu->browser.select_file) {
        if (select_file(menu, menu->browser.select_file)) {
            menu->browser.valid = false;
            menu_show_error(menu, "Error while navigating to file");
        }
        path_free(menu->browser.select_file);
        menu->browser.select_file = NULL;
    }

    if (menu->browser.reload) {
        menu->browser.reload = false;
        if (reload_directory(menu)) {
            menu_show_error(menu, "Error while reloading current directory");
            menu->browser.valid = false;
        }
    }
}

void view_browser_display (menu_t *menu, surface_t *display) {
    if (browser_virtual_pak_recovery_active) {
        if (menu->actions.back) {
            sound_play_effect(SFX_EXIT);
            browser_virtual_pak_recovery_active = false;
            browser_virtual_pak_recovery_failed = false;
            browser_virtual_pak_recovery_snoozed = true;
        } else if (menu->actions.options) {
            sound_play_effect(SFX_ENTER);
            menu->utility_return_mode = MENU_MODE_BROWSER;
            menu->next_mode = MENU_MODE_VIRTUAL_PAK_CENTER;
            return;
        } else if (menu->actions.settings) {
            sound_play_effect(SFX_ERROR);
            virtual_pak_force_clear_pending();
            browser_virtual_pak_recovery_active = false;
            browser_virtual_pak_recovery_failed = false;
            browser_virtual_pak_recovery_snoozed = true;
        } else if (menu->actions.enter) {
            browser_virtual_pak_recovery_retry_cooldown = 0;
        }

        draw(menu, display);

        if (!browser_virtual_pak_recovery_active || browser_virtual_pak_recovery_snoozed) {
            return;
        }

        if (browser_virtual_pak_recovery_retry_cooldown > 0) {
            browser_virtual_pak_recovery_retry_cooldown--;
        } else {
            virtual_pak_try_sync_pending();
            if (!virtual_pak_has_pending_sync()) {
                browser_virtual_pak_recovery_active = false;
                browser_virtual_pak_recovery_failed = false;
                return;
            }
            browser_virtual_pak_recovery_failed = true;
            browser_virtual_pak_recovery_retry_cooldown = 30;
            snprintf(browser_virtual_pak_recovery_message, sizeof(browser_virtual_pak_recovery_message),
                "Virtual Pak recovery needs the same physical Controller Pak in controller 1.\n\nA: Retry now  B: Continue for now  R: Open center  Start: Force clear");
        }
        return;
    }

    process(menu);

    draw(menu, display);
    browser_apply_playlist_overrides_deferred(menu);
    playlist_recent_prewarm_tick(menu);
    playlist_grid_prewarm_tick(menu);
}
