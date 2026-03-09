/**
 * @file boxart.c
 * @brief Implementation of the boxart UI component.
 * @ingroup ui_components
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "../ui_components.h"
#include "../native_image.h"
#include "../path.h"
#include "../png_decoder.h"
#include "constants.h"
#include "utils/fs.h"

#define OLD_BOXART_DIRECTORY       "menu/boxart"
#define METADATA_BASE_DIRECTORY    "menu/metadata"
#define HOMEBREW_ID_SUBDIRECTORY   "homebrew"
#define BOXART_CACHE_DIR           "menu/cache/thumbs"
#define BOXART_CACHE_MAGIC         (0x42584154) /* BXAT */
#define BOXART_THUMB_CACHE_ENTRIES (16)
#define BOXART_LOAD_QUEUE_MAX      (24)
#define BOXART_NATIVE_SIDECAR      ".nimg"

typedef struct {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t size;
} boxart_cache_metadata_t;

typedef struct {
    bool valid;
    uint64_t key_hash;
    uint32_t last_used_tick;
    char *key_path;
    surface_t *image;
} boxart_thumb_cache_entry_t;

typedef struct {
    component_boxart_t *component;
    char *cache_key;
    char *cache_path;
} boxart_load_context_t;

static boxart_thumb_cache_entry_t g_boxart_thumb_cache[BOXART_THUMB_CACHE_ENTRIES];
static uint32_t g_boxart_thumb_cache_tick = 1;
static boxart_load_context_t *g_boxart_active_load_ctx = NULL;
static boxart_load_context_t *g_boxart_load_queue[BOXART_LOAD_QUEUE_MAX];
static int g_boxart_load_queue_count = 0;

static void boxart_queue_pump(void);
static void png_decoder_callback(png_err_t err, surface_t *decoded_image, void *callback_data);

static bool string_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) {
        return false;
    }
    size_t s_len = strlen(s);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > s_len) {
        return false;
    }
    return strcmp(s + (s_len - suffix_len), suffix) == 0;
}

static uint64_t fnv1a64_str(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    if (!s) {
        return h;
    }
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static surface_t *surface_clone_rgba16(surface_t *src) {
    if (!src || !src->buffer) {
        return NULL;
    }

    surface_t *dst = calloc(1, sizeof(surface_t));
    if (!dst) {
        return NULL;
    }

    *dst = surface_alloc(FMT_RGBA16, src->width, src->height);
    if (!dst->buffer) {
        free(dst);
        return NULL;
    }

    size_t size = (size_t)src->height * (size_t)src->stride;
    memcpy(dst->buffer, src->buffer, size);
    return dst;
}

static void surface_free_owned(surface_t **image) {
    if (image && *image) {
        surface_free(*image);
        free(*image);
        *image = NULL;
    }
}

static void boxart_thumb_cache_entry_clear(boxart_thumb_cache_entry_t *entry) {
    if (!entry) {
        return;
    }
    free(entry->key_path);
    entry->key_path = NULL;
    surface_free_owned(&entry->image);
    memset(entry, 0, sizeof(*entry));
}

static boxart_thumb_cache_entry_t *boxart_thumb_cache_find(const char *key_path) {
    if (!key_path) {
        return NULL;
    }

    uint64_t key_hash = fnv1a64_str(key_path);
    for (int i = 0; i < BOXART_THUMB_CACHE_ENTRIES; i++) {
        boxart_thumb_cache_entry_t *entry = &g_boxart_thumb_cache[i];
        if (!entry->valid || entry->key_hash != key_hash || !entry->key_path) {
            continue;
        }
        if (strcmp(entry->key_path, key_path) == 0) {
            entry->last_used_tick = ++g_boxart_thumb_cache_tick;
            return entry;
        }
    }

    return NULL;
}

static boxart_thumb_cache_entry_t *boxart_thumb_cache_evict_target(void) {
    boxart_thumb_cache_entry_t *oldest = &g_boxart_thumb_cache[0];
    for (int i = 0; i < BOXART_THUMB_CACHE_ENTRIES; i++) {
        if (!g_boxart_thumb_cache[i].valid) {
            return &g_boxart_thumb_cache[i];
        }
        if (g_boxart_thumb_cache[i].last_used_tick < oldest->last_used_tick) {
            oldest = &g_boxart_thumb_cache[i];
        }
    }
    return oldest;
}

static bool boxart_thumb_cache_put(const char *key_path, surface_t *image) {
    if (!key_path || !image || !image->buffer) {
        return false;
    }

    boxart_thumb_cache_entry_t *entry = boxart_thumb_cache_find(key_path);
    if (!entry) {
        entry = boxart_thumb_cache_evict_target();
        boxart_thumb_cache_entry_clear(entry);
        entry->key_path = strdup(key_path);
        if (!entry->key_path) {
            return false;
        }
        entry->key_hash = fnv1a64_str(key_path);
        entry->valid = true;
    } else {
        surface_free_owned(&entry->image);
    }

    entry->image = surface_clone_rgba16(image);
    if (!entry->image) {
        boxart_thumb_cache_entry_clear(entry);
        return false;
    }
    entry->last_used_tick = ++g_boxart_thumb_cache_tick;
    return true;
}

static surface_t *boxart_thumb_cache_get_clone(const char *key_path) {
    boxart_thumb_cache_entry_t *entry = boxart_thumb_cache_find(key_path);
    if (!entry || !entry->image) {
        return NULL;
    }
    return surface_clone_rgba16(entry->image);
}

static char *boxart_thumb_cache_file_path(const char *storage_prefix, const char *key_path) {
    if (!storage_prefix || !key_path) {
        return NULL;
    }

    path_t *cache_dir = path_init(storage_prefix, BOXART_CACHE_DIR);
    if (!cache_dir) {
        return NULL;
    }
    directory_create(path_get(cache_dir));

    uint64_t hash = fnv1a64_str(key_path);
    char file_name[32];
    snprintf(file_name, sizeof(file_name), "bx_%016" PRIx64 ".cache", hash);
    path_push(cache_dir, file_name);

    char *out = strdup(path_get(cache_dir));
    path_free(cache_dir);
    return out;
}

static surface_t *boxart_thumb_cache_load_disk_clone(const char *storage_prefix, const char *key_path) {
    char *cache_path = boxart_thumb_cache_file_path(storage_prefix, key_path);
    if (!cache_path) {
        return NULL;
    }

    FILE *f = fopen(cache_path, "rb");
    free(cache_path);
    if (!f) {
        return NULL;
    }

    boxart_cache_metadata_t meta = {0};
    if (fread(&meta, sizeof(meta), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    bool invalid = (meta.magic != BOXART_CACHE_MAGIC)
        || (meta.width == 0) || (meta.height == 0)
        || (meta.width > BOXART_WIDTH_MAX) || (meta.height > BOXART_HEIGHT_MAX);
    if (invalid) {
        fclose(f);
        return NULL;
    }

    surface_t *image = calloc(1, sizeof(surface_t));
    if (!image) {
        fclose(f);
        return NULL;
    }
    *image = surface_alloc(FMT_RGBA16, meta.width, meta.height);
    if (!image->buffer || meta.size != (uint32_t)(image->height * image->stride)) {
        surface_free_owned(&image);
        fclose(f);
        return NULL;
    }

    if (fread(image->buffer, meta.size, 1, f) != 1) {
        surface_free_owned(&image);
        fclose(f);
        return NULL;
    }
    fclose(f);

    // Populate memory cache for subsequent nearby accesses.
    boxart_thumb_cache_put(key_path, image);
    return image;
}

static void boxart_thumb_cache_save_disk(const char *cache_path, surface_t *image) {
    if (!cache_path || !image || !image->buffer) {
        return;
    }
    if (file_exists((char *)cache_path)) {
        return; // Conservative SD behavior: write once on cache miss only.
    }

    FILE *f = fopen(cache_path, "wb");
    if (!f) {
        return;
    }

    boxart_cache_metadata_t meta = {
        .magic = BOXART_CACHE_MAGIC,
        .width = image->width,
        .height = image->height,
        .size = (uint32_t)(image->height * image->stride),
    };
    fwrite(&meta, sizeof(meta), 1, f);
    fwrite(image->buffer, meta.size, 1, f);
    fclose(f);
}

static void boxart_load_context_free(boxart_load_context_t *ctx) {
    if (!ctx) {
        return;
    }
    free(ctx->cache_key);
    free(ctx->cache_path);
    free(ctx);
}

static bool boxart_queue_remove(boxart_load_context_t *ctx) {
    if (!ctx) {
        return false;
    }
    for (int i = 0; i < g_boxart_load_queue_count; i++) {
        if (g_boxart_load_queue[i] != ctx) {
            continue;
        }
        for (int j = i; j < (g_boxart_load_queue_count - 1); j++) {
            g_boxart_load_queue[j] = g_boxart_load_queue[j + 1];
        }
        g_boxart_load_queue[g_boxart_load_queue_count - 1] = NULL;
        g_boxart_load_queue_count--;
        return true;
    }
    return false;
}

static bool boxart_queue_enqueue(boxart_load_context_t *ctx) {
    if (!ctx) {
        return false;
    }
    if (g_boxart_load_queue_count >= BOXART_LOAD_QUEUE_MAX) {
        return false;
    }

    // Avoid duplicate queue entries for the same context.
    for (int i = 0; i < g_boxart_load_queue_count; i++) {
        if (g_boxart_load_queue[i] == ctx) {
            return true;
        }
    }

    g_boxart_load_queue[g_boxart_load_queue_count++] = ctx;
    return true;
}

static void boxart_complete_context_with_image(boxart_load_context_t *ctx, surface_t *image) {
    if (!ctx || !ctx->component) {
        if (image) {
            surface_free(image);
            free(image);
        }
        boxart_load_context_free(ctx);
        return;
    }

    component_boxart_t *b = ctx->component;
    b->loading = false;
    b->load_context = NULL;
    b->image = image;
    boxart_load_context_free(ctx);
}

static void boxart_queue_pump(void) {
    if (g_boxart_active_load_ctx != NULL || png_decoder_is_busy()) {
        return;
    }

    while (g_boxart_load_queue_count > 0) {
        boxart_load_context_t *ctx = g_boxart_load_queue[0];
        for (int i = 1; i < g_boxart_load_queue_count; i++) {
            g_boxart_load_queue[i - 1] = g_boxart_load_queue[i];
        }
        g_boxart_load_queue[--g_boxart_load_queue_count] = NULL;

        if (!ctx) {
            continue;
        }

        if (!ctx->component) {
            boxart_load_context_free(ctx);
            continue;
        }

        // Queue is FIFO and conservative; if decoder unexpectedly busy, put it back at head and stop.
        if (png_decoder_is_busy()) {
            boxart_queue_enqueue(ctx);
            break;
        }

        // Try memory cache only (no SD I/O during draw).
        surface_t *cached = boxart_thumb_cache_get_clone(ctx->cache_key);
        if (cached) {
            boxart_complete_context_with_image(ctx, cached);
            continue;
        }

        surface_t *native = NULL;
        if (string_ends_with(ctx->cache_key, BOXART_NATIVE_SIDECAR)) {
            native = native_image_load_rgba16_file(ctx->cache_key, BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX);
        } else {
            native = native_image_load_sidecar_rgba16(ctx->cache_key, BOXART_NATIVE_SIDECAR, BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX);
        }
        if (native) {
            boxart_thumb_cache_put(ctx->cache_key, native);
            boxart_complete_context_with_image(ctx, native);
            continue;
        }

        if (png_decoder_start(ctx->cache_key, BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX, png_decoder_callback, ctx) == PNG_OK) {
            g_boxart_active_load_ctx = ctx;
            break;
        }

        // If decode could not start (eg. transient), fail closed for this component rather than wedging queue.
        if (ctx->component) {
            ctx->component->loading = false;
            ctx->component->load_context = NULL;
        }
        boxart_load_context_free(ctx);
    }
}

static bool resolve_metadata_boxart_directory (path_t *path, const char *game_code, char *resolved_path, size_t resolved_path_size) {
    if ((path == NULL) || (game_code == NULL)) {
        return false;
    }

    char candidate[32];

    // 1) exact region match
    snprintf(candidate, sizeof(candidate), "%c/%c/%c/%c", game_code[0], game_code[1], game_code[2], game_code[3]);
    path_push(path, candidate);
    if (directory_exists(path_get(path))) {
        if ((resolved_path != NULL) && (resolved_path_size > 0)) {
            snprintf(resolved_path, resolved_path_size, "%s", candidate);
        }
        return true;
    }
    path_pop(path);

    // 2) cross-region fallback for better artwork coverage
    const char fallback_regions[] = { 'E', 'P', 'J', 'U', 'A', '\0' };
    for (size_t i = 0; fallback_regions[i] != '\0'; i++) {
        if (fallback_regions[i] == game_code[3]) {
            continue;
        }
        snprintf(candidate, sizeof(candidate), "%c/%c/%c/%c", game_code[0], game_code[1], game_code[2], fallback_regions[i]);
        path_push(path, candidate);
        if (directory_exists(path_get(path))) {
            if ((resolved_path != NULL) && (resolved_path_size > 0)) {
                snprintf(resolved_path, resolved_path_size, "%s", candidate);
            }
            return true;
        }
        path_pop(path);
    }

    // 3) region-agnostic metadata directory
    snprintf(candidate, sizeof(candidate), "%c/%c/%c", game_code[0], game_code[1], game_code[2]);
    path_push(path, candidate);
    if (directory_exists(path_get(path))) {
        if ((resolved_path != NULL) && (resolved_path_size > 0)) {
            snprintf(resolved_path, resolved_path_size, "%s", candidate);
        }
        return true;
    }
    path_pop(path);

    return false;
}

static bool resolve_boxart_image_path(const char *storage_prefix, const char *game_code, const char *rom_title,
                                      file_image_type_t current_image_view, bool prefer_grid_thumb,
                                      char **resolved_image_path_out) {
    char boxart_path[32] = {0};
    bool found = false;
    path_t *path = path_init(storage_prefix, METADATA_BASE_DIRECTORY);
    if (!path) {
        return false;
    }

    if (game_code[1] == 'E' && game_code[2] == 'D') {
        char safe_title[21];
        memcpy(safe_title, rom_title, 20);
        safe_title[20] = '\0';
        snprintf(boxart_path, sizeof(boxart_path), HOMEBREW_ID_SUBDIRECTORY"/%s", safe_title);
        path_push(path, boxart_path);
    } else {
        if (!resolve_metadata_boxart_directory(path, game_code, boxart_path, sizeof(boxart_path))) {
            path_free(path);
            path = path_init(storage_prefix, OLD_BOXART_DIRECTORY);
            if (!path) {
                return false;
            }
            if (!resolve_metadata_boxart_directory(path, game_code, boxart_path, sizeof(boxart_path))) {
                boxart_path[0] = '\0';
            }
        }
    }

    debugf("Boxart: Using path %s\n", boxart_path);

    if (directory_exists(path_get(path))) {
        switch (current_image_view) {
            case IMAGE_GAMEPAK_FRONT:  path_push(path, "gamepak_front.png"); break;
            case IMAGE_GAMEPAK_BACK:   path_push(path, "gamepak_back.png"); break;
            case IMAGE_BOXART_BACK:    path_push(path, "boxart_back.png"); break;
            case IMAGE_BOXART_LEFT:    path_push(path, "boxart_left.png"); break;
            case IMAGE_BOXART_RIGHT:   path_push(path, "boxart_right.png"); break;
            case IMAGE_BOXART_BOTTOM:  path_push(path, "boxart_bottom.png"); break;
            case IMAGE_BOXART_TOP:     path_push(path, "boxart_top.png"); break;
            default:
                if (prefer_grid_thumb) {
                    path_push(path, "boxart_front.grid.png");
                    if (!file_exists(path_get(path))) {
                        path_pop(path);
                        path_push(path, "boxart_front.png");
                    }
                } else {
                    path_push(path, "boxart_front.png");
                }
                break;
        }

        if (file_exists(path_get(path)) || native_image_sidecar_exists(path_get(path), BOXART_NATIVE_SIDECAR)) {
            *resolved_image_path_out = strdup(path_get(path));
            found = (*resolved_image_path_out != NULL);
        }
    }

    path_free(path);
    return found;
}

/**
 * @brief PNG decoder callback for boxart image loading.
 *
 * Sets the loading flag to false and assigns the decoded image to the boxart component.
 *
 * @param err PNG decoder error code.
 * @param decoded_image Pointer to the decoded image surface.
 * @param callback_data Pointer to the boxart component (component_boxart_t *).
 */
static void png_decoder_callback(png_err_t err, surface_t *decoded_image, void *callback_data) {
    boxart_load_context_t *ctx = (boxart_load_context_t *)callback_data;
    component_boxart_t *b = ctx ? ctx->component : NULL;
    if (g_boxart_active_load_ctx == ctx) {
        g_boxart_active_load_ctx = NULL;
    }
    if (!b) {
        if (decoded_image) {
            surface_free(decoded_image);
            free(decoded_image);
        }
        boxart_load_context_free(ctx);
        return;
    }
    b->loading = false;
    b->load_context = NULL;

    if (err == PNG_OK && decoded_image != NULL) {
        if (ctx->cache_key) {
            boxart_thumb_cache_put(ctx->cache_key, decoded_image);
        }
        if (ctx->cache_path) {
            boxart_thumb_cache_save_disk(ctx->cache_path, decoded_image);
        }
        b->image = surface_clone_rgba16(decoded_image);
        if (!b->image) {
            b->image = decoded_image;
            decoded_image = NULL;
        }
    } else {
        b->image = NULL;
    }

    if (decoded_image) {
        surface_free(decoded_image);
        free(decoded_image);
    }
    boxart_load_context_free(ctx);
    boxart_queue_pump();
}

/**
 * @brief Initialize and load the boxart component for a game.
 *
 * Attempts to locate and load the appropriate boxart image for the given game code and ROM title.
 *
 * @param storage_prefix The storage prefix (e.g., SD card root).
 * @param game_code The 4-character game code.
 * @param rom_title Title of the ROM (may be NULL). If used, it is sanitized for filesystem safety.
 * @param current_image_view The current image view type (front, back, etc.).
 * @return Pointer to the initialized boxart component, or NULL on failure.
 */
static component_boxart_t *ui_components_boxart_init_with_options(const char *storage_prefix, const char *game_code, const char *rom_title,
                                                                  file_image_type_t current_image_view, bool prefer_grid_thumb, bool memory_cache_only, bool async_only) {
    boxart_queue_pump();

    component_boxart_t *b = calloc(1, sizeof(component_boxart_t));
    if (b == NULL) {
        return NULL;
    }

    char *resolved_image_path = NULL;
    if (!resolve_boxart_image_path(storage_prefix, game_code, rom_title, current_image_view, prefer_grid_thumb, &resolved_image_path)) {
        free(b);
        return NULL;
    }

    b->image = boxart_thumb_cache_get_clone(resolved_image_path);
    if (b->image) {
        b->loading = false;
        free(resolved_image_path);
        return b;
    }

    if (memory_cache_only) {
        free(resolved_image_path);
        free(b);
        return NULL;
    }

    if (!async_only) {
        surface_t *native = NULL;
        if (string_ends_with(resolved_image_path, BOXART_NATIVE_SIDECAR)) {
            native = native_image_load_rgba16_file(resolved_image_path, BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX);
        } else {
            native = native_image_load_sidecar_rgba16(resolved_image_path, BOXART_NATIVE_SIDECAR, BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX);
        }
        if (native) {
            boxart_thumb_cache_put(resolved_image_path, native);
            b->image = native;
            b->loading = false;
            free(resolved_image_path);
            return b;
        }

        b->image = boxart_thumb_cache_load_disk_clone(storage_prefix, resolved_image_path);
        if (b->image) {
            b->loading = false;
            free(resolved_image_path);
            return b;
        }
    }

    boxart_load_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        free(resolved_image_path);
        free(b);
        return NULL;
    }
    ctx->component = b;
    ctx->cache_key = resolved_image_path;
    ctx->cache_path = boxart_thumb_cache_file_path(storage_prefix, resolved_image_path);

    b->loading = true;
    b->load_context = ctx;

    if (!png_decoder_is_busy() &&
        png_decoder_start(ctx->cache_key, BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX, png_decoder_callback, ctx) == PNG_OK) {
        g_boxart_active_load_ctx = ctx;
        return b;
    }

    if (boxart_queue_enqueue(ctx)) {
        return b;
    }

    b->loading = false;
    b->load_context = NULL;
    boxart_load_context_free(ctx);
    free(b);
    return NULL;
}

component_boxart_t *ui_components_boxart_init(const char *storage_prefix, const char *game_code, const char *rom_title, file_image_type_t current_image_view) {
    return ui_components_boxart_init_with_options(storage_prefix, game_code, rom_title, current_image_view, false, false, false);
}

component_boxart_t *ui_components_boxart_init_async(const char *storage_prefix, const char *game_code, const char *rom_title, file_image_type_t current_image_view) {
    return ui_components_boxart_init_with_options(storage_prefix, game_code, rom_title, current_image_view, false, false, true);
}

component_boxart_t *ui_components_boxart_init_memory_cached(const char *storage_prefix, const char *game_code, const char *rom_title, file_image_type_t current_image_view) {
    return ui_components_boxart_init_with_options(storage_prefix, game_code, rom_title, current_image_view, false, true, false);
}

component_boxart_t *ui_components_boxart_init_grid(const char *storage_prefix, const char *game_code, const char *rom_title) {
    return ui_components_boxart_init_with_options(storage_prefix, game_code, rom_title, IMAGE_BOXART_FRONT, true, false, false);
}

component_boxart_t *ui_components_boxart_init_grid_memory_cached(const char *storage_prefix, const char *game_code, const char *rom_title) {
    return ui_components_boxart_init_with_options(storage_prefix, game_code, rom_title, IMAGE_BOXART_FRONT, true, true, false);
}

/**
 * @brief Free the boxart component and its resources.
 *
 * @param b Pointer to the boxart component.
 */
void ui_components_boxart_free(component_boxart_t *b) {
    if (b) {
        if (b->loading) {
            boxart_load_context_t *ctx = (boxart_load_context_t *)b->load_context;
            if (ctx != NULL && g_boxart_active_load_ctx == ctx) {
                g_boxart_active_load_ctx = NULL;
                png_decoder_abort();
            } else if (ctx != NULL) {
                boxart_queue_remove(ctx);
            }
            boxart_load_context_free(ctx);
            b->load_context = NULL;
        }
        if (b->image) {
            surface_free(b->image);
            free(b->image);
        }
        free(b);
        boxart_queue_pump();
    }
}

/**
 * @brief Draw the boxart image or a loading placeholder.
 *
 * Draws the loaded boxart image at the appropriate position, or a placeholder if not loaded.
 *
 * @param b Pointer to the boxart component.
 */
void ui_components_boxart_draw(component_boxart_t *b) {
    boxart_queue_pump();

    int box_x = BOXART_X;
    int box_y = BOXART_Y;
    if (b && b->image && b->image->width <= BOXART_WIDTH_MAX && b->image->height <= BOXART_HEIGHT_MAX) {
        rdpq_mode_push();
            rdpq_set_mode_copy(false);
            if (b->image->height == BOXART_HEIGHT_MAX) {
                box_x = BOXART_X_JP;
                box_y = BOXART_Y_JP;
            } else if (b->image->width == BOXART_WIDTH_DD && b->image->height == BOXART_HEIGHT_DD) {
                box_x = BOXART_X_DD;
                box_y = BOXART_Y_DD;
            }
            rdpq_tex_blit(b->image, box_x, box_y, NULL);
        rdpq_mode_pop();
    } else {
        ui_components_box_draw(
            BOXART_X,
            BOXART_Y,
            BOXART_X + BOXART_WIDTH,
            BOXART_Y + BOXART_HEIGHT,
            BOXART_LOADING_COLOR
        );
    }
}
