#include <libdragon.h>
#include <mini.c/src/mini.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#include "views.h"
#include "utils/fs.h"
#include "../fonts.h"
#include "../native_image.h"
#include "../png_decoder.h"
#include "../sound.h"
#include "../ui_components/constants.h"

#define MANUAL_PAGE_FILENAME_DIGITS     4
#define MANUAL_MAX_PAGE_WIDTH           1024
#define MANUAL_MAX_PAGE_HEIGHT          1024
#define MANUAL_DEFAULT_MAX_ZOOM         3
#define MANUAL_PAN_STEP_PIXELS          24.0f
#define MANUAL_PAN_STEP_FAST_PIXELS     64.0f
#define MANUAL_MAX_BLIT_COORD           1023
#define MANUAL_NATIVE_SIDECAR           ".nimg"
#define MANUAL_TILED_CACHE_SIZE         12
#define MANUAL_TILED_QUEUE_SIZE         16
#define MANUAL_TILED_PREFETCH_MARGIN    1
#define MANUAL_TILED_LEVEL_MAX          3
#define MANUAL_TMEM_BYTES               4096

static int manual_align_up (int value, int alignment) {
    if (alignment <= 1) {
        return value;
    }
    return ((value + alignment - 1) / alignment) * alignment;
}

typedef struct {
    bool valid;
    int32_t level;
    int32_t page;
    int32_t row;
    int32_t col;
    uint32_t use_id;
    surface_t *image;
} manual_tile_cache_entry_t;

typedef struct {
    int32_t level;
    int32_t page;
    int32_t row;
    int32_t col;
    int32_t priority;
} manual_tile_request_t;

static bool manual_show_ui;
static manual_tile_cache_entry_t manual_tile_cache[MANUAL_TILED_CACHE_SIZE];
static uint32_t manual_tile_use_id;
static bool manual_prefetch_decode_active;
static int32_t manual_prefetch_target_page;
static manual_tile_request_t manual_tile_queue[MANUAL_TILED_QUEUE_SIZE];
static int manual_tile_queue_count;
static bool manual_tile_loading;
static int32_t manual_tile_loading_level;
static int32_t manual_tile_loading_page;
static int32_t manual_tile_loading_row;
static int32_t manual_tile_loading_col;
static FILE *manual_tile_bundle_streams[MANUAL_TILED_LEVEL_MAX];
static uint8_t manual_tile_bundle_stream_buffers[MANUAL_TILED_LEVEL_MAX][32768];
static bool manual_tile_window_valid;
static int32_t manual_tile_window_level;
static int32_t manual_tile_window_page;
static int32_t manual_tile_window_row_start;
static int32_t manual_tile_window_row_end;
static int32_t manual_tile_window_col_start;
static int32_t manual_tile_window_col_end;

static void manual_free_tile_cache (void);
static void manual_free_tile_bundle_data (menu_t *menu);

static FILE *manual_get_tile_bundle_stream (menu_t *menu, int level) {
    if (!menu || level < 0 || level >= MANUAL_TILED_LEVEL_MAX) {
        return NULL;
    }

    if (manual_tile_bundle_streams[level]) {
        return manual_tile_bundle_streams[level];
    }

    path_t *bundle_path = menu->manual.tiled_level_bundle_files[level];
    if (!bundle_path) {
        return NULL;
    }

    FILE *file = fopen(path_get(bundle_path), "rb");
    if (!file) {
        return NULL;
    }

    setvbuf(file, (char *)manual_tile_bundle_stream_buffers[level], _IOFBF, sizeof(manual_tile_bundle_stream_buffers[level]));
    manual_tile_bundle_streams[level] = file;
    return file;
}

static void manual_close_tile_bundle_stream (int level) {
    if (level < 0 || level >= MANUAL_TILED_LEVEL_MAX) {
        return;
    }

    if (manual_tile_bundle_streams[level]) {
        fclose(manual_tile_bundle_streams[level]);
        manual_tile_bundle_streams[level] = NULL;
    }
}

static int manual_get_effective_tile_cache_size (void) {
    return is_memory_expanded() ? MANUAL_TILED_CACHE_SIZE : 8;
}

static bool manual_allow_speculative_prefetch (void) {
    return is_memory_expanded();
}

static int manual_get_tiled_selected_level (menu_t *menu) {
    if (!menu || menu->manual.tiled_level_count <= 1) {
        return 0;
    }

    int selected = 0;
    for (int i = 0; i < menu->manual.tiled_level_count; i++) {
        if (menu->manual.zoom_level >= menu->manual.tiled_level_zoom[i]) {
            selected = i;
        }
    }
    return selected;
}

static void manual_apply_active_tiled_level (menu_t *menu) {
    if (!menu || menu->manual.tiled_level_count <= 0) {
        return;
    }

    int new_level = manual_get_tiled_selected_level(menu);
    if (new_level < 0) {
        new_level = 0;
    }
    if (new_level >= menu->manual.tiled_level_count) {
        new_level = menu->manual.tiled_level_count - 1;
    }

    int old_width = menu->manual.tiled_page_width;
    int old_height = menu->manual.tiled_page_height;
    if (menu->manual.tiled_active_level != new_level) {
        manual_free_tile_cache();
    }

    menu->manual.tiled_active_level = new_level;
    menu->manual.tiled_pages_directory = menu->manual.tiled_level_directories[new_level];
    menu->manual.tiled_page_width = menu->manual.tiled_level_page_width[new_level];
    menu->manual.tiled_page_height = menu->manual.tiled_level_page_height[new_level];
    menu->manual.tiled_tile_size = menu->manual.tiled_level_tile_size[new_level];
    menu->manual.tiled_rows = menu->manual.tiled_level_rows[new_level];
    menu->manual.tiled_cols = menu->manual.tiled_level_cols[new_level];

    if (old_width > 0 && old_height > 0
        && (old_width != menu->manual.tiled_page_width || old_height != menu->manual.tiled_page_height)) {
        menu->manual.pan_x = menu->manual.pan_x * ((float)menu->manual.tiled_page_width / (float)old_width);
        menu->manual.pan_y = menu->manual.pan_y * ((float)menu->manual.tiled_page_height / (float)old_height);
    }
}

static bool manual_current_tiled_level_uses_bundle (menu_t *menu) {
    if (!menu) {
        return false;
    }
    int level = menu->manual.tiled_active_level;
    if (level < 0 || level >= MANUAL_TILED_LEVEL_MAX) {
        return false;
    }
    return menu->manual.tiled_level_bundle_enabled[level];
}

static char *convert_error_message (png_err_t err) {
    switch (err) {
        case PNG_ERR_INT: return "Internal PNG decoder error";
        case PNG_ERR_BUSY: return "PNG decode already in process";
        case PNG_ERR_OUT_OF_MEM: return "Manual page decode failed due to insufficient memory";
        case PNG_ERR_NO_FILE: return "Manual page image not found";
        case PNG_ERR_BAD_FILE: return "Invalid manual page image";
        default: return "Unknown manual page decode error";
    }
}

static void manual_reset_pan (menu_t *menu) {
    menu->manual.pan_x = 0.0f;
    menu->manual.pan_y = 0.0f;
}

static void manual_free_current_image (menu_t *menu) {
    if (menu->manual.image) {
        surface_free(menu->manual.image);
        free(menu->manual.image);
        menu->manual.image = NULL;
    }
}

static void manual_free_prefetch_image (menu_t *menu) {
    if (menu->manual.prefetch_image) {
        surface_free(menu->manual.prefetch_image);
        free(menu->manual.prefetch_image);
        menu->manual.prefetch_image = NULL;
    }
    menu->manual.prefetched_page = -1;
    menu->manual.prefetch_loading = false;
    manual_prefetch_target_page = -1;
}

static void manual_reset_tile_window (void) {
    manual_tile_window_valid = false;
    manual_tile_window_level = -1;
    manual_tile_window_page = -1;
    manual_tile_window_row_start = -1;
    manual_tile_window_row_end = -1;
    manual_tile_window_col_start = -1;
    manual_tile_window_col_end = -1;
}

static void manual_abort_tile_loading (void) {
    if (manual_tile_loading) {
        png_decoder_abort();
        manual_tile_loading = false;
    }
}

static void manual_free_tile_cache (void) {
    manual_abort_tile_loading();
    manual_tile_queue_count = 0;
    manual_reset_tile_window();
    for (int i = 0; i < MANUAL_TILED_CACHE_SIZE; i++) {
        if (manual_tile_cache[i].image) {
            surface_free(manual_tile_cache[i].image);
            free(manual_tile_cache[i].image);
            manual_tile_cache[i].image = NULL;
        }
        manual_tile_cache[i].valid = false;
        manual_tile_cache[i].level = -1;
        manual_tile_cache[i].page = -1;
        manual_tile_cache[i].row = -1;
        manual_tile_cache[i].col = -1;
        manual_tile_cache[i].use_id = 0;
    }
    manual_tile_use_id = 0;
}

static void manual_deinit (menu_t *menu) {
    if (menu->manual.page_loading) {
        png_decoder_abort();
    }
    if (menu->manual.prefetch_loading) {
        png_decoder_abort();
    }
    manual_free_current_image(menu);
    manual_free_prefetch_image(menu);
    manual_free_tile_cache();
    if (menu->manual.pages_directory) {
        path_free(menu->manual.pages_directory);
        menu->manual.pages_directory = NULL;
    }
    if (menu->manual.zoom_pages_directory) {
        path_free(menu->manual.zoom_pages_directory);
        menu->manual.zoom_pages_directory = NULL;
    }
    if (menu->manual.tiled_preview_directory) {
        path_free(menu->manual.tiled_preview_directory);
        menu->manual.tiled_preview_directory = NULL;
    }
    if (menu->manual.tiled_pages_directory) {
        menu->manual.tiled_pages_directory = NULL;
    }
    for (int i = 0; i < MANUAL_TILED_LEVEL_MAX; i++) {
        if (menu->manual.tiled_level_directories[i]) {
            path_free(menu->manual.tiled_level_directories[i]);
            menu->manual.tiled_level_directories[i] = NULL;
        }
        menu->manual.tiled_level_zoom[i] = 0;
        menu->manual.tiled_level_page_width[i] = 0;
        menu->manual.tiled_level_page_height[i] = 0;
        menu->manual.tiled_level_tile_size[i] = 0;
        menu->manual.tiled_level_rows[i] = 0;
        menu->manual.tiled_level_cols[i] = 0;
        menu->manual.tiled_level_bundle_format[i] = MANUAL_TILE_BUNDLE_FORMAT_PNG;
    }
    if (menu->manual.directory) {
        path_free(menu->manual.directory);
        menu->manual.directory = NULL;
    }
    manual_free_tile_bundle_data(menu);
    menu->manual.page_count = 0;
    menu->manual.current_page = 0;
    menu->manual.zoom_level = 1;
    menu->manual.max_zoom_level = 1;
    menu->manual.loaded_page = -1;
    menu->manual.prefetched_page = -1;
    menu->manual.has_zoom_pages = false;
    menu->manual.loaded_zoom_asset = false;
    menu->manual.tiled_beta = false;
    menu->manual.page_loading = false;
    menu->manual.prefetch_loading = false;
    menu->manual.tiled_level_count = 0;
    menu->manual.tiled_active_level = 0;
    menu->manual.tiled_page_width = 0;
    menu->manual.tiled_page_height = 0;
    menu->manual.tiled_tile_size = 0;
    menu->manual.tiled_rows = 0;
    menu->manual.tiled_cols = 0;
    menu->manual.title[0] = '\0';
    manual_reset_pan(menu);
}

static void manual_free_tile_bundle_data (menu_t *menu) {
    if (!menu) {
        return;
    }
    for (int i = 0; i < MANUAL_TILED_LEVEL_MAX; i++) {
        manual_close_tile_bundle_stream(i);
        if (menu->manual.tiled_level_bundle_files[i]) {
            path_free(menu->manual.tiled_level_bundle_files[i]);
            menu->manual.tiled_level_bundle_files[i] = NULL;
        }
        if (menu->manual.tiled_level_bundle_entries[i]) {
            free(menu->manual.tiled_level_bundle_entries[i]);
            menu->manual.tiled_level_bundle_entries[i] = NULL;
        }
        menu->manual.tiled_level_bundle_entry_counts[i] = 0;
        menu->manual.tiled_level_bundle_enabled[i] = false;
        menu->manual.tiled_level_bundle_format[i] = MANUAL_TILE_BUNDLE_FORMAT_PNG;
    }
}

static bool manual_build_page_path (path_t *directory, int page_index, char *out, size_t out_len) {
    if (!directory || !out || out_len == 0 || page_index < 0) {
        return false;
    }

    path_t *page_path = path_clone(directory);
    if (!page_path) {
        return false;
    }

    char page_name[32];
    snprintf(page_name, sizeof(page_name), "%0*d.png", MANUAL_PAGE_FILENAME_DIGITS, page_index + 1);
    path_push(page_path, page_name);

    bool exists = native_image_sidecar_exists(path_get(page_path), MANUAL_NATIVE_SIDECAR);
    if (exists) {
        snprintf(out, out_len, "%s", path_get(page_path));
    }

    path_free(page_path);
    return exists;
}

static bool manual_build_tile_path (path_t *directory, int page_index, int row, int col, char *out, size_t out_len) {
    if (!directory || !out || out_len == 0 || page_index < 0 || row < 0 || col < 0) {
        return false;
    }

    path_t *tile_path = path_clone(directory);
    if (!tile_path) {
        return false;
    }

    char page_name[32];
    char tile_name[32];
    snprintf(page_name, sizeof(page_name), "%0*d", MANUAL_PAGE_FILENAME_DIGITS, page_index + 1);
    snprintf(tile_name, sizeof(tile_name), "r%03d_c%03d.png", row, col);
    path_push(tile_path, page_name);
    path_push(tile_path, tile_name);

    bool exists = file_exists(path_get(tile_path));
    if (exists) {
        snprintf(out, out_len, "%s", path_get(tile_path));
    }

    path_free(tile_path);
    return exists;
}

static manual_tile_bundle_entry_t *manual_find_bundle_entry (menu_t *menu, int level, int page, int row, int col) {
    if (!menu || level < 0 || level >= MANUAL_TILED_LEVEL_MAX || !menu->manual.tiled_level_bundle_entries[level]) {
        return NULL;
    }

    manual_tile_bundle_entry_t *entries = menu->manual.tiled_level_bundle_entries[level];
    int left = 0;
    int right = menu->manual.tiled_level_bundle_entry_counts[level] - 1;

    while (left <= right) {
        int mid = left + ((right - left) / 2);
        manual_tile_bundle_entry_t *entry = &entries[mid];

        if (entry->page == page && entry->row == row && entry->col == col) {
            return entry;
        }

        bool go_right = false;
        if (entry->page < page) {
            go_right = true;
        } else if (entry->page == page && entry->row < row) {
            go_right = true;
        } else if (entry->page == page && entry->row == row && entry->col < col) {
            go_right = true;
        }

        if (go_right) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    return NULL;
}

static bool manual_read_bundle_chunk (menu_t *menu, int level, manual_tile_bundle_entry_t *entry, uint8_t **out_data, size_t *out_size) {
    if (!menu || level < 0 || level >= MANUAL_TILED_LEVEL_MAX || !entry || !out_data || !out_size) {
        return false;
    }

    *out_data = NULL;
    *out_size = 0;

    FILE *file = manual_get_tile_bundle_stream(menu, level);
    if (!file) {
        return false;
    }

    if (fseek(file, (long)entry->offset, SEEK_SET) != 0) {
        return false;
    }

    uint8_t *buffer = malloc(entry->size);
    if (!buffer) {
        return false;
    }

    size_t read_size = fread(buffer, 1, entry->size, file);
    if (read_size != entry->size) {
        free(buffer);
        return false;
    }

    *out_data = buffer;
    *out_size = entry->size;
    return true;
}

static void manual_get_tile_dimensions (menu_t *menu, int level, int row, int col, int *out_width, int *out_height) {
    int tile_size = menu->manual.tiled_level_tile_size[level];
    int page_width = menu->manual.tiled_level_page_width[level];
    int page_height = menu->manual.tiled_level_page_height[level];
    int tile_x = col * tile_size;
    int tile_y = row * tile_size;

    int width = tile_size;
    int height = tile_size;
    if (tile_x + width > page_width) {
        width = page_width - tile_x;
    }
    if (tile_y + height > page_height) {
        height = page_height - tile_y;
    }
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }

    if (out_width) {
        *out_width = width;
    }
    if (out_height) {
        *out_height = height;
    }
}

static surface_t *manual_read_bundle_rgba16_surface (menu_t *menu, manual_tile_bundle_entry_t *entry, int level, int row, int col) {
    if (!menu || !entry || level < 0 || level >= MANUAL_TILED_LEVEL_MAX) {
        return NULL;
    }

    int tile_width = 0;
    int tile_height = 0;
    manual_get_tile_dimensions(menu, level, row, col, &tile_width, &tile_height);
    if (tile_width <= 0 || tile_height <= 0) {
        return NULL;
    }

    int tile_size = menu->manual.tiled_level_tile_size[level];
    size_t expected_size = (size_t)tile_width * (size_t)tile_height * 2;
    if (entry->size != expected_size) {
        return NULL;
    }

    FILE *file = manual_get_tile_bundle_stream(menu, level);
    if (!file) {
        return NULL;
    }
    if (fseek(file, (long)entry->offset, SEEK_SET) != 0) {
        return NULL;
    }

    surface_t *image = calloc(1, sizeof(surface_t));
    if (!image) {
        return NULL;
    }
    *image = surface_alloc(FMT_RGBA16, tile_size, tile_size);
    if (!image->buffer) {
        surface_free(image);
        free(image);
        return NULL;
    }
    memset(image->buffer, 0, (size_t)image->height * (size_t)image->stride);

    bool ok = true;
    for (int y = 0; y < tile_height; y++) {
        uint8_t *row_dst = (uint8_t *)image->buffer + ((size_t)y * (size_t)image->stride);
        size_t row_size = (size_t)tile_width * 2;
        if (fread(row_dst, 1, row_size, file) != row_size) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        surface_free(image);
        free(image);
        return NULL;
    }

    return image;
}

static bool manual_load_tile_bundle_index (path_t *index_path, manual_tile_bundle_entry_t **out_entries, int32_t *out_count) {
    if (!index_path || !out_entries || !out_count) {
        return false;
    }

    *out_entries = NULL;
    *out_count = 0;

    FILE *file = fopen(path_get(index_path), "rb");
    if (!file) {
        return false;
    }

    int capacity = 64;
    int count = 0;
    manual_tile_bundle_entry_t *entries = malloc(sizeof(manual_tile_bundle_entry_t) * capacity);
    if (!entries) {
        fclose(file);
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        manual_tile_bundle_entry_t entry = {0};
        long page = 0;
        long row = 0;
        long col = 0;
        unsigned long offset = 0;
        unsigned long size = 0;
        if (sscanf(line, "%ld\t%ld\t%ld\t%lu\t%lu", &page, &row, &col, &offset, &size) != 5) {
            continue;
        }
        entry.page = (int32_t)page;
        entry.row = (int32_t)row;
        entry.col = (int32_t)col;
        entry.offset = (uint32_t)offset;
        entry.size = (uint32_t)size;

        if (count >= capacity) {
            capacity *= 2;
            manual_tile_bundle_entry_t *grown = realloc(entries, sizeof(manual_tile_bundle_entry_t) * capacity);
            if (!grown) {
                free(entries);
                fclose(file);
                return false;
            }
            entries = grown;
        }
        entries[count++] = entry;
    }

    fclose(file);
    *out_entries = entries;
    *out_count = count;
    return true;
}

static bool manual_should_use_zoom_asset (menu_t *menu) {
    return !menu->manual.tiled_beta && menu->manual.has_zoom_pages && (menu->manual.zoom_level > 1);
}

static int manual_get_prefetch_candidate_page (menu_t *menu) {
    if (menu->manual.page_count <= 1) {
        return -1;
    }
    if (menu->manual.current_page + 1 < menu->manual.page_count) {
        return menu->manual.current_page + 1;
    }
    if (menu->manual.current_page - 1 >= 0) {
        return menu->manual.current_page - 1;
    }
    return -1;
}

static bool manual_should_use_tiled_page (menu_t *menu) {
    return menu->manual.tiled_beta
        && (menu->manual.zoom_level > 1)
        && ((menu->manual.tiled_pages_directory != NULL)
            || (menu->manual.tiled_active_level >= 0
                && menu->manual.tiled_active_level < MANUAL_TILED_LEVEL_MAX
                && menu->manual.tiled_level_bundle_enabled[menu->manual.tiled_active_level]))
        && (menu->manual.tiled_page_width > 0)
        && (menu->manual.tiled_page_height > 0)
        && (menu->manual.tiled_tile_size > 0);
}

static bool manual_tile_is_cached (int32_t level, int32_t page, int32_t row, int32_t col) {
    for (int i = 0; i < manual_get_effective_tile_cache_size(); i++) {
        if (manual_tile_cache[i].valid
            && manual_tile_cache[i].level == level
            && manual_tile_cache[i].page == page
            && manual_tile_cache[i].row == row
            && manual_tile_cache[i].col == col) {
            return true;
        }
    }
    return false;
}

static bool manual_get_logical_page_dimensions (menu_t *menu, int *width, int *height) {
    if (manual_should_use_tiled_page(menu)) {
        if (width) {
            *width = menu->manual.tiled_page_width;
        }
        if (height) {
            *height = menu->manual.tiled_page_height;
        }
        return true;
    }

    if (menu->manual.image) {
        if (width) {
            *width = menu->manual.image->width;
        }
        if (height) {
            *height = menu->manual.image->height;
        }
        return true;
    }

    return false;
}

static float manual_get_base_scale_for_size (int page_width, int page_height) {
    if (page_width <= 0 || page_height <= 0) {
        return 1.0f;
    }

    float base_scale = fminf((float)display_get_width() / (float)page_width,
                             (float)display_get_height() / (float)page_height);
    if (base_scale <= 0.0f) {
        return 1.0f;
    }
    return base_scale;
}

static void manual_complete_tile_load (menu_t *menu, surface_t *decoded_image) {
    if (!menu || !decoded_image) {
        return;
    }

    if (!manual_should_use_tiled_page(menu)
        || menu->manual.tiled_active_level != manual_tile_loading_level
        || menu->manual.current_page != manual_tile_loading_page) {
        surface_free(decoded_image);
        free(decoded_image);
        return;
    }

    manual_tile_cache_entry_t *entry = NULL;
    for (int i = 0; i < manual_get_effective_tile_cache_size(); i++) {
        if (manual_tile_cache[i].valid
            && manual_tile_cache[i].level == manual_tile_loading_level
            && manual_tile_cache[i].page == manual_tile_loading_page
            && manual_tile_cache[i].row == manual_tile_loading_row
            && manual_tile_cache[i].col == manual_tile_loading_col) {
            entry = &manual_tile_cache[i];
            break;
        }
    }
    if (!entry) {
        manual_tile_cache_entry_t *oldest = &manual_tile_cache[0];
        for (int i = 0; i < manual_get_effective_tile_cache_size(); i++) {
            if (!manual_tile_cache[i].valid) {
                oldest = &manual_tile_cache[i];
                break;
            }
            if (manual_tile_cache[i].use_id < oldest->use_id) {
                oldest = &manual_tile_cache[i];
            }
        }
        if (oldest->image) {
            surface_free(oldest->image);
            free(oldest->image);
        }
        entry = oldest;
    }

    entry->valid = true;
    entry->level = manual_tile_loading_level;
    entry->page = manual_tile_loading_page;
    entry->row = manual_tile_loading_row;
    entry->col = manual_tile_loading_col;
    entry->use_id = ++manual_tile_use_id;
    entry->image = decoded_image;

    if (menu->manual.image) {
        manual_free_current_image(menu);
    }
}

static void manual_tile_callback (png_err_t err, surface_t *decoded_image, void *callback_data) {
    menu_t *menu = (menu_t *)(callback_data);
    manual_tile_loading = false;

    if (err != PNG_OK) {
        if (decoded_image) {
            surface_free(decoded_image);
            free(decoded_image);
        }
        menu_show_error(menu, convert_error_message(err));
        return;
    }

    manual_complete_tile_load(menu, decoded_image);
}

static bool manual_start_page_load (menu_t *menu) {
    if (!menu->manual.pages_directory || menu->manual.page_count <= 0) {
        return false;
    }

    char page_path[512];
    bool use_zoom_asset = manual_should_use_zoom_asset(menu);
    bool found = false;

    if (use_zoom_asset) {
        found = manual_build_page_path(menu->manual.zoom_pages_directory, menu->manual.current_page, page_path, sizeof(page_path));
    }
    if (!found) {
        use_zoom_asset = false;
        found = manual_build_page_path(menu->manual.pages_directory, menu->manual.current_page, page_path, sizeof(page_path));
    }
    if (!found) {
        menu_show_error(menu, "Manual page image is missing");
        return false;
    }

    if (menu->manual.page_loading) {
        png_decoder_abort();
        menu->manual.page_loading = false;
    }
    if (menu->manual.prefetch_loading) {
        png_decoder_abort();
        menu->manual.prefetch_loading = false;
        manual_prefetch_decode_active = false;
    }

    manual_free_current_image(menu);
    menu->manual.loaded_page = -1;
    menu->manual.loaded_zoom_asset = use_zoom_asset;
    manual_free_prefetch_image(menu);

    surface_t *native_image = native_image_load_sidecar_rgba16(page_path, MANUAL_NATIVE_SIDECAR, MANUAL_MAX_PAGE_WIDTH, MANUAL_MAX_PAGE_HEIGHT);
    if (native_image) {
        menu->manual.image = native_image;
        menu->manual.loaded_page = menu->manual.current_page;
        menu->manual.page_loading = false;
        return true;
    }

    menu->manual.image = native_image_load_sidecar_rgba16(page_path, MANUAL_NATIVE_SIDECAR, MANUAL_MAX_PAGE_WIDTH, MANUAL_MAX_PAGE_HEIGHT);
    if (!menu->manual.image) {
        menu_show_error(menu, "Manual native page image is missing");
        return false;
    }

    menu->manual.loaded_page = menu->manual.current_page;
    menu->manual.page_loading = false;
    return true;
}

static void manual_maybe_start_prefetch (menu_t *menu) {
    if (!manual_allow_speculative_prefetch()) {
        return;
    }
    if (menu->manual.page_loading || menu->manual.prefetch_loading || png_decoder_is_busy()) {
        return;
    }
    if (!menu->manual.image || menu->manual.zoom_level > 1) {
        return;
    }

    int target_page = manual_get_prefetch_candidate_page(menu);
    if (target_page < 0 || target_page == menu->manual.current_page || target_page == menu->manual.prefetched_page) {
        return;
    }

    char page_path[512];
    if (!manual_build_page_path(menu->manual.pages_directory, target_page, page_path, sizeof(page_path))) {
        return;
    }

    surface_t *native_image = native_image_load_sidecar_rgba16(page_path, MANUAL_NATIVE_SIDECAR, MANUAL_MAX_PAGE_WIDTH, MANUAL_MAX_PAGE_HEIGHT);
    if (!native_image) {
        return;
    }

    manual_free_prefetch_image(menu);
    menu->manual.prefetch_image = native_image;
    menu->manual.prefetched_page = target_page;
    menu->manual.prefetch_loading = false;
    manual_prefetch_decode_active = false;
    manual_prefetch_target_page = -1;
}

static void manual_clamp_pan (menu_t *menu) {
    int page_width = 0;
    int page_height = 0;
    if (!manual_get_logical_page_dimensions(menu, &page_width, &page_height)) {
        manual_reset_pan(menu);
        return;
    }

    float total_scale = manual_get_base_scale_for_size(page_width, page_height) * (float)menu->manual.zoom_level;
    if (total_scale <= 0.0f) {
        manual_reset_pan(menu);
        return;
    }

    float src_w = (float)display_get_width() / total_scale;
    float src_h = (float)display_get_height() / total_scale;

    if (src_w >= page_width) {
        menu->manual.pan_x = 0.0f;
    } else {
        float max_x = (float)page_width - src_w;
        if (menu->manual.pan_x < 0.0f) {
            menu->manual.pan_x = 0.0f;
        } else if (menu->manual.pan_x > max_x) {
            menu->manual.pan_x = max_x;
        }
    }

    if (src_h >= page_height) {
        menu->manual.pan_y = 0.0f;
    } else {
        float max_y = (float)page_height - src_h;
        if (menu->manual.pan_y < 0.0f) {
            menu->manual.pan_y = 0.0f;
        } else if (menu->manual.pan_y > max_y) {
            menu->manual.pan_y = max_y;
        }
    }
}

static void manual_change_page (menu_t *menu, int delta) {
    if (menu->manual.page_count <= 0) {
        return;
    }

    int next_page = menu->manual.current_page + delta;
    if (next_page < 0) {
        next_page = 0;
    } else if (next_page >= menu->manual.page_count) {
        next_page = menu->manual.page_count - 1;
    }

    if (next_page == menu->manual.current_page) {
        return;
    }

    menu->manual.current_page = next_page;
    manual_free_tile_cache();
    manual_reset_pan(menu);
    if (menu->manual.zoom_level > 1) {
        menu->manual.zoom_level = 1;
    }
    if (menu->manual.prefetch_image
        && menu->manual.prefetched_page == menu->manual.current_page) {
        manual_free_current_image(menu);
        menu->manual.image = menu->manual.prefetch_image;
        menu->manual.prefetch_image = NULL;
        menu->manual.loaded_page = menu->manual.current_page;
        menu->manual.loaded_zoom_asset = false;
        menu->manual.prefetched_page = -1;
        menu->manual.prefetch_loading = false;
        manual_maybe_start_prefetch(menu);
    } else {
        manual_start_page_load(menu);
    }
    sound_play_effect(SFX_CURSOR);
}

static void manual_set_zoom_level (menu_t *menu, int zoom_level) {
    if (zoom_level < 1) {
        zoom_level = 1;
    }
    if (zoom_level > menu->manual.max_zoom_level) {
        zoom_level = menu->manual.max_zoom_level;
    }
    if (zoom_level == menu->manual.zoom_level) {
        return;
    }

    int old_zoom_level = menu->manual.zoom_level;
    bool old_zoom_asset = manual_should_use_zoom_asset(menu);
    menu->manual.zoom_level = zoom_level;
    bool new_zoom_asset = manual_should_use_zoom_asset(menu);
    if (menu->manual.tiled_beta) {
        if (old_zoom_level <= 1 && menu->manual.zoom_level > 1) {
            manual_reset_tile_window();
            manual_apply_active_tiled_level(menu);
        } else if (old_zoom_level > 1 && menu->manual.zoom_level <= 1) {
            manual_free_tile_cache();
            manual_start_page_load(menu);
        } else if (menu->manual.zoom_level > 1) {
            manual_apply_active_tiled_level(menu);
        }
    }
    manual_clamp_pan(menu);

    if ((new_zoom_asset != old_zoom_asset) && !menu->manual.tiled_beta) {
        manual_start_page_load(menu);
    } else if (!menu->manual.tiled_beta && menu->manual.zoom_level <= 1) {
        manual_maybe_start_prefetch(menu);
    }

    sound_play_effect(SFX_SETTING);
}

static void manual_pan (menu_t *menu, float dx, float dy) {
    if (menu->manual.zoom_level <= 1) {
        return;
    }
    if (!menu->manual.image && !manual_should_use_tiled_page(menu)) {
        return;
    }

    menu->manual.pan_x += dx;
    menu->manual.pan_y += dy;
    manual_clamp_pan(menu);
}

static bool manual_is_pan_input_active (menu_t *menu) {
    if (!menu || menu->manual.zoom_level <= 1) {
        return false;
    }
    return menu->actions.go_left
        || menu->actions.go_right
        || menu->actions.go_up
        || menu->actions.go_down;
}

static void process (menu_t *menu) {
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = menu->manual.return_mode;
        return;
    }

    if (menu->manual.page_loading) {
        return;
    }

    if (menu->actions.settings) {
        manual_show_ui = !manual_show_ui;
        sound_play_effect(SFX_SETTING);
        return;
    }

    if (menu->actions.options) {
        manual_set_zoom_level(menu, menu->manual.zoom_level + 1);
        return;
    }

    if (menu->actions.lz_context) {
        manual_set_zoom_level(menu, menu->manual.zoom_level - 1);
        return;
    }

    if (menu->actions.enter) {
        manual_change_page(menu, 1);
        return;
    }

    if (menu->actions.go_fast) {
        if (menu->actions.go_right) {
            manual_change_page(menu, 1);
            return;
        }
        if (menu->actions.go_left) {
            manual_change_page(menu, -1);
            return;
        }
        if (menu->actions.go_down) {
            manual_change_page(menu, 5);
            return;
        }
        if (menu->actions.go_up) {
            manual_change_page(menu, -5);
            return;
        }
    }

    if (menu->manual.zoom_level > 1) {
        float step = menu->actions.go_fast ? MANUAL_PAN_STEP_FAST_PIXELS : MANUAL_PAN_STEP_PIXELS;
        if (menu->actions.go_left) {
            manual_pan(menu, -step, 0.0f);
        } else if (menu->actions.go_right) {
            manual_pan(menu, step, 0.0f);
        } else if (menu->actions.go_up) {
            manual_pan(menu, 0.0f, -step);
        } else if (menu->actions.go_down) {
            manual_pan(menu, 0.0f, step);
        }
    } else {
        if (menu->actions.go_right) {
            manual_change_page(menu, 1);
        } else if (menu->actions.go_left) {
            manual_change_page(menu, -1);
        }
    }
}

static manual_tile_cache_entry_t *manual_find_tile_cache_entry (int level, int page, int row, int col) {
    for (int i = 0; i < MANUAL_TILED_CACHE_SIZE; i++) {
        if (manual_tile_cache[i].valid
            && manual_tile_cache[i].level == level
            && manual_tile_cache[i].page == page
            && manual_tile_cache[i].row == row
            && manual_tile_cache[i].col == col) {
            manual_tile_cache[i].use_id = ++manual_tile_use_id;
            return &manual_tile_cache[i];
        }
    }
    return NULL;
}

static void manual_tile_queue_clear (void) {
    manual_tile_queue_count = 0;
}

static void manual_tile_queue_push (int32_t level, int32_t page, int32_t row, int32_t col, int32_t priority) {
    if (manual_tile_queue_count >= MANUAL_TILED_QUEUE_SIZE) {
        return;
    }
    for (int i = 0; i < manual_tile_queue_count; i++) {
        if (manual_tile_queue[i].page == page
            && manual_tile_queue[i].level == level
            && manual_tile_queue[i].row == row
            && manual_tile_queue[i].col == col) {
            return;
        }
    }
    manual_tile_request_t request = {
        .level = level,
        .page = page,
        .row = row,
        .col = col,
        .priority = priority,
    };
    int insert_at = manual_tile_queue_count;
    while (insert_at > 0 && manual_tile_queue[insert_at - 1].priority > priority) {
        manual_tile_queue[insert_at] = manual_tile_queue[insert_at - 1];
        insert_at--;
    }
    manual_tile_queue[insert_at] = request;
    manual_tile_queue_count++;
}

static void manual_compute_visible_tile_window (
    menu_t *menu,
    surface_t *display,
    int *row_start,
    int *row_end,
    int *col_start,
    int *col_end
) {
    float total_scale = manual_get_base_scale_for_size(menu->manual.tiled_page_width, menu->manual.tiled_page_height)
        * (float)menu->manual.zoom_level;
    float src_w = fminf((float)menu->manual.tiled_page_width, (float)display->width / total_scale);
    float src_h = fminf((float)menu->manual.tiled_page_height, (float)display->height / total_scale);
    manual_clamp_pan(menu);

    int src_left = (int)floorf(menu->manual.pan_x);
    int src_top = (int)floorf(menu->manual.pan_y);
    int src_right = (int)ceilf(menu->manual.pan_x + src_w);
    int src_bottom = (int)ceilf(menu->manual.pan_y + src_h);

    *col_start = src_left / menu->manual.tiled_tile_size;
    *col_end = (src_right - 1) / menu->manual.tiled_tile_size;
    *row_start = src_top / menu->manual.tiled_tile_size;
    *row_end = (src_bottom - 1) / menu->manual.tiled_tile_size;

    if (*col_start < 0) {
        *col_start = 0;
    }
    if (*row_start < 0) {
        *row_start = 0;
    }
    if (*col_end >= menu->manual.tiled_cols) {
        *col_end = menu->manual.tiled_cols - 1;
    }
    if (*row_end >= menu->manual.tiled_rows) {
        *row_end = menu->manual.tiled_rows - 1;
    }
}

static bool manual_is_visible_tile_window_fully_cached (menu_t *menu, surface_t *display) {
    if (!manual_should_use_tiled_page(menu)) {
        return false;
    }

    int row_start = 0;
    int row_end = 0;
    int col_start = 0;
    int col_end = 0;
    manual_compute_visible_tile_window(menu, display, &row_start, &row_end, &col_start, &col_end);
    for (int row = row_start; row <= row_end; row++) {
        for (int col = col_start; col <= col_end; col++) {
            if (!manual_tile_is_cached(menu->manual.tiled_active_level, menu->manual.current_page, row, col)) {
                return false;
            }
        }
    }
    return true;
}

static void manual_update_tile_queue (menu_t *menu, surface_t *display) {
    if (!manual_should_use_tiled_page(menu)) {
        manual_tile_queue_clear();
        manual_reset_tile_window();
        return;
    }

    int row_start = 0;
    int row_end = 0;
    int col_start = 0;
    int col_end = 0;
    manual_compute_visible_tile_window(menu, display, &row_start, &row_end, &col_start, &col_end);
    int prefetch_margin = 0;
    if (!manual_is_pan_input_active(menu) && manual_allow_speculative_prefetch()) {
        prefetch_margin = MANUAL_TILED_PREFETCH_MARGIN;
    }
    int prefetch_row_start = row_start - prefetch_margin;
    int prefetch_row_end = row_end + prefetch_margin;
    int prefetch_col_start = col_start - prefetch_margin;
    int prefetch_col_end = col_end + prefetch_margin;
    if (prefetch_row_start < 0) {
        prefetch_row_start = 0;
    }
    if (prefetch_col_start < 0) {
        prefetch_col_start = 0;
    }
    if (prefetch_row_end >= menu->manual.tiled_rows) {
        prefetch_row_end = menu->manual.tiled_rows - 1;
    }
    if (prefetch_col_end >= menu->manual.tiled_cols) {
        prefetch_col_end = menu->manual.tiled_cols - 1;
    }

    if (manual_tile_window_valid
        && manual_tile_window_level == menu->manual.tiled_active_level
        && manual_tile_window_page == menu->manual.current_page
        && manual_tile_window_row_start == prefetch_row_start
        && manual_tile_window_row_end == prefetch_row_end
        && manual_tile_window_col_start == prefetch_col_start
        && manual_tile_window_col_end == prefetch_col_end) {
        return;
    }

    manual_tile_window_valid = true;
    manual_tile_window_level = menu->manual.tiled_active_level;
    manual_tile_window_page = menu->manual.current_page;
    manual_tile_window_row_start = prefetch_row_start;
    manual_tile_window_row_end = prefetch_row_end;
    manual_tile_window_col_start = prefetch_col_start;
    manual_tile_window_col_end = prefetch_col_end;

    manual_tile_queue_clear();

    int center_row = (row_start + row_end) / 2;
    int center_col = (col_start + col_end) / 2;
    for (int row = prefetch_row_start; row <= prefetch_row_end; row++) {
        for (int col = prefetch_col_start; col <= prefetch_col_end; col++) {
            if (manual_tile_is_cached(menu->manual.tiled_active_level, menu->manual.current_page, row, col)) {
                continue;
            }
            int visible_penalty = (row < row_start || row > row_end || col < col_start || col > col_end) ? 10 : 0;
            int priority = visible_penalty + abs(row - center_row) + abs(col - center_col);
            manual_tile_queue_push(menu->manual.tiled_active_level, menu->manual.current_page, row, col, priority);
        }
    }
}

static void manual_start_next_tile_load (menu_t *menu) {
    if (!manual_should_use_tiled_page(menu) || menu->manual.page_loading || manual_tile_loading || png_decoder_is_busy()) {
        return;
    }

    while (manual_tile_queue_count > 0) {
        manual_tile_request_t request = manual_tile_queue[0];
        for (int i = 1; i < manual_tile_queue_count; i++) {
            manual_tile_queue[i - 1] = manual_tile_queue[i];
        }
        manual_tile_queue_count--;

        if (request.level != menu->manual.tiled_active_level || request.page != menu->manual.current_page) {
            continue;
        }
        if (manual_tile_is_cached(request.level, request.page, request.row, request.col)) {
            continue;
        }

        png_err_t err = PNG_ERR_NO_FILE;
        if (manual_current_tiled_level_uses_bundle(menu)) {
            manual_tile_bundle_entry_t *bundle_entry = manual_find_bundle_entry(menu, request.level, request.page, request.row, request.col);
            if (!bundle_entry) {
                continue;
            }
            manual_tile_loading = true;
            manual_tile_loading_level = request.level;
            manual_tile_loading_page = request.page;
            manual_tile_loading_row = request.row;
            manual_tile_loading_col = request.col;

            if (menu->manual.tiled_level_bundle_format[request.level] == MANUAL_TILE_BUNDLE_FORMAT_RGBA16) {
                surface_t *tile_image = manual_read_bundle_rgba16_surface(
                    menu,
                    bundle_entry,
                    request.level,
                    request.row,
                    request.col
                );
                manual_tile_loading = false;
                if (!tile_image) {
                    menu_show_error(menu, "Manual tile bundle read failed");
                    return;
                }
                manual_complete_tile_load(menu, tile_image);
                return;
            }

            uint8_t *png_data = NULL;
            size_t png_size = 0;
            if (!manual_read_bundle_chunk(menu, request.level, bundle_entry, &png_data, &png_size)) {
                manual_tile_loading = false;
                continue;
            }
            err = png_decoder_start_buffer_owned(png_data, png_size, MANUAL_MAX_PAGE_WIDTH, MANUAL_MAX_PAGE_HEIGHT, manual_tile_callback, menu);
            if (err != PNG_OK) {
                free(png_data);
                manual_tile_loading = false;
            }
        } else {
            char tile_path[512];
            if (!manual_build_tile_path(menu->manual.tiled_pages_directory, request.page, request.row, request.col, tile_path, sizeof(tile_path))) {
                continue;
            }
            err = png_decoder_start(tile_path, MANUAL_MAX_PAGE_WIDTH, MANUAL_MAX_PAGE_HEIGHT, manual_tile_callback, menu);
        }
        if (err == PNG_ERR_BUSY) {
            return;
        }
        if (err != PNG_OK) {
            menu_show_error(menu, convert_error_message(err));
            return;
        }

        manual_tile_loading = true;
        manual_tile_loading_level = request.level;
        manual_tile_loading_page = request.page;
        manual_tile_loading_row = request.row;
        manual_tile_loading_col = request.col;
        return;
    }
}

static void draw_surface_region (surface_t *image, float draw_x, float draw_y, int src_x, int src_y, int src_width, int src_height, float total_scale) {
    if (!image || src_width <= 0 || src_height <= 0) {
        return;
    }

    if (src_x >= MANUAL_MAX_BLIT_COORD) {
        src_x = MANUAL_MAX_BLIT_COORD - 1;
    }
    if (src_y >= MANUAL_MAX_BLIT_COORD) {
        src_y = MANUAL_MAX_BLIT_COORD - 1;
    }
    if (src_x >= image->width) {
        src_x = image->width - 1;
    }
    if (src_y >= image->height) {
        src_y = image->height - 1;
    }
    if (src_width > (MANUAL_MAX_BLIT_COORD - src_x)) {
        src_width = MANUAL_MAX_BLIT_COORD - src_x;
    }
    if (src_height > (MANUAL_MAX_BLIT_COORD - src_y)) {
        src_height = MANUAL_MAX_BLIT_COORD - src_y;
    }
    if (src_width > (image->width - src_x)) {
        src_width = image->width - src_x;
    }
    if (src_height > (image->height - src_y)) {
        src_height = image->height - src_y;
    }
    if (src_width < 1) {
        src_width = 1;
    }
    if (src_height < 1) {
        src_height = 1;
    }

    int bytes_per_pixel = 2;
    int line_bytes = manual_align_up(src_width * bytes_per_pixel, 8);
    int max_strip_rows = MANUAL_TMEM_BYTES / line_bytes;
    if (max_strip_rows < 1) {
        max_strip_rows = 1;
    }

    int strip_y = src_y;
    int remaining_height = src_height;
    while (remaining_height > 0) {
        int strip_height = remaining_height;
        if (strip_height > max_strip_rows) {
            strip_height = max_strip_rows;
        }

        rdpq_tex_upload_sub(
            TILE0,
            image,
            NULL,
            src_x,
            strip_y,
            src_x + src_width,
            strip_y + strip_height
        );
        rdpq_texture_rectangle_scaled(
            TILE0,
            draw_x,
            draw_y + ((float)(strip_y - src_y) * total_scale),
            draw_x + ((float)src_width * total_scale),
            draw_y + ((float)((strip_y - src_y) + strip_height) * total_scale),
            src_x,
            strip_y,
            src_x + src_width,
            strip_y + strip_height
        );

        strip_y += strip_height;
        remaining_height -= strip_height;
    }
}

static void draw_full_page (menu_t *menu, surface_t *display) {
    if (!menu->manual.image) {
        return;
    }

    float base_scale = manual_get_base_scale_for_size(menu->manual.image->width, menu->manual.image->height);
    float total_scale = base_scale * (float)menu->manual.zoom_level;
    float src_w = fminf((float)menu->manual.image->width, (float)display->width / total_scale);
    float src_h = fminf((float)menu->manual.image->height, (float)display->height / total_scale);

    manual_clamp_pan(menu);

    int src_x = (int)menu->manual.pan_x;
    int src_y = (int)menu->manual.pan_y;
    int src_width = (int)src_w;
    int src_height = (int)src_h;
    if (src_width <= 0) {
        src_width = menu->manual.image->width;
    }
    if (src_height <= 0) {
        src_height = menu->manual.image->height;
    }

    float draw_w = (float)src_width * total_scale;
    float draw_h = (float)src_height * total_scale;
    float draw_x = ((float)display->width - draw_w) * 0.5f;
    float draw_y = ((float)display->height - draw_h) * 0.5f;

    rdpq_mode_push();
        rdpq_set_mode_copy(false);
        draw_surface_region(menu->manual.image, draw_x, draw_y, src_x, src_y, src_width, src_height, total_scale);
    rdpq_mode_pop();
}

static void draw_tiled_page (menu_t *menu, surface_t *display) {
    manual_apply_active_tiled_level(menu);

    if (menu->manual.image) {
        draw_full_page(menu, display);
    }

    float base_scale = manual_get_base_scale_for_size(menu->manual.tiled_page_width, menu->manual.tiled_page_height);
    float total_scale = base_scale * (float)menu->manual.zoom_level;
    float src_w = fminf((float)menu->manual.tiled_page_width, (float)display->width / total_scale);
    float src_h = fminf((float)menu->manual.tiled_page_height, (float)display->height / total_scale);

    manual_clamp_pan(menu);

    float src_left = menu->manual.pan_x;
    float src_top = menu->manual.pan_y;
    float draw_origin_x = ((float)display->width - (src_w * total_scale)) * 0.5f;
    float draw_origin_y = ((float)display->height - (src_h * total_scale)) * 0.5f;

    int src_right = (int)ceilf(src_left + src_w);
    int src_bottom = (int)ceilf(src_top + src_h);
    int col_start = (int)src_left / menu->manual.tiled_tile_size;
    int col_end = (src_right - 1) / menu->manual.tiled_tile_size;
    int row_start = (int)src_top / menu->manual.tiled_tile_size;
    int row_end = (src_bottom - 1) / menu->manual.tiled_tile_size;

    if (col_start < 0) {
        col_start = 0;
    }
    if (row_start < 0) {
        row_start = 0;
    }
    if (col_end >= menu->manual.tiled_cols) {
        col_end = menu->manual.tiled_cols - 1;
    }
    if (row_end >= menu->manual.tiled_rows) {
        row_end = menu->manual.tiled_rows - 1;
    }

    rdpq_mode_push();
        rdpq_set_mode_copy(false);
        for (int row = row_start; row <= row_end; row++) {
            for (int col = col_start; col <= col_end; col++) {
                manual_tile_cache_entry_t *tile = manual_find_tile_cache_entry(menu->manual.tiled_active_level, menu->manual.current_page, row, col);
                if (!tile || !tile->image) {
                    continue;
                }

                int tile_x = col * menu->manual.tiled_tile_size;
                int tile_y = row * menu->manual.tiled_tile_size;
                int tile_w = menu->manual.tiled_tile_size;
                int tile_h = menu->manual.tiled_tile_size;
                if ((tile_x + tile_w) > menu->manual.tiled_page_width) {
                    tile_w = menu->manual.tiled_page_width - tile_x;
                }
                if ((tile_y + tile_h) > menu->manual.tiled_page_height) {
                    tile_h = menu->manual.tiled_page_height - tile_y;
                }

                int inter_left = (int)fmaxf((float)tile_x, src_left);
                int inter_top = (int)fmaxf((float)tile_y, src_top);
                int inter_right = (int)fminf((float)(tile_x + tile_w), src_left + src_w);
                int inter_bottom = (int)fminf((float)(tile_y + tile_h), src_top + src_h);

                int inter_w = inter_right - inter_left;
                int inter_h = inter_bottom - inter_top;
                if (inter_w <= 0 || inter_h <= 0) {
                    continue;
                }

                float draw_x = draw_origin_x + ((float)inter_left - src_left) * total_scale;
                float draw_y = draw_origin_y + ((float)inter_top - src_top) * total_scale;
                int tile_src_x = inter_left - tile_x;
                int tile_src_y = inter_top - tile_y;

                draw_surface_region(tile->image, draw_x, draw_y, tile_src_x, tile_src_y, inter_w, inter_h, total_scale);
            }
        }
    rdpq_mode_pop();
}

static void draw_page (menu_t *menu, surface_t *display) {
    if (manual_should_use_tiled_page(menu)) {
        draw_tiled_page(menu, display);
        return;
    }
    draw_full_page(menu, display);
}

static void draw (menu_t *menu, surface_t *display) {
    rdpq_attach_clear(display, NULL);

    ui_components_background_draw();

    if (menu->manual.page_loading) {
        ui_components_loader_draw(png_decoder_get_progress(), "Loading manual page...");
    } else if (menu->manual.image || manual_should_use_tiled_page(menu)) {
        draw_page(menu, display);
    } else {
        ui_components_messagebox_draw("Manual page could not be loaded");
    }

    if (manual_show_ui) {
        char left[256];
        const char *asset_label = "";
        if (manual_should_use_tiled_page(menu)) {
            asset_label = " (tiled beta)";
        } else if (menu->manual.loaded_zoom_asset) {
            asset_label = " (hi-res)";
        }
        snprintf(left, sizeof(left), "%s\nPage %d / %d\nZoom %dx%s",
            menu->manual.title[0] ? menu->manual.title : "Manual",
            (int)(menu->manual.current_page + 1),
            (int)menu->manual.page_count,
            (int)menu->manual.zoom_level,
            asset_label);

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            left
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_RIGHT, VALIGN_TOP,
            "A: Next Page\n"
            "C-L/C-R: Prev/Next\n"
            "L/R: Zoom Out/In\n"
            "Stick/D-Pad: Pan\n"
            "Start: Toggle UI\n"
            "B: Back"
        );
    }

    rdpq_detach_show();
}

void view_manual_viewer_init (menu_t *menu) {
    manual_show_ui = true;
    manual_free_tile_cache();
    menu->manual.page_loading = false;
    menu->manual.image = NULL;
    menu->manual.loaded_page = -1;
    menu->manual.loaded_zoom_asset = false;
    menu->manual.has_zoom_pages = false;
    menu->manual.max_zoom_level = MANUAL_DEFAULT_MAX_ZOOM;
    menu->manual.zoom_level = 1;
    menu->manual.tiled_level_count = 0;
    menu->manual.tiled_active_level = 0;
    menu->manual.tiled_page_width = 0;
    menu->manual.tiled_page_height = 0;
    menu->manual.tiled_tile_size = 0;
    menu->manual.tiled_rows = 0;
    menu->manual.tiled_cols = 0;
    for (int i = 0; i < MANUAL_TILED_LEVEL_MAX; i++) {
        menu->manual.tiled_level_directories[i] = NULL;
        menu->manual.tiled_level_bundle_files[i] = NULL;
        menu->manual.tiled_level_zoom[i] = 0;
        menu->manual.tiled_level_page_width[i] = 0;
        menu->manual.tiled_level_page_height[i] = 0;
        menu->manual.tiled_level_tile_size[i] = 0;
        menu->manual.tiled_level_rows[i] = 0;
        menu->manual.tiled_level_cols[i] = 0;
        menu->manual.tiled_level_bundle_entry_counts[i] = 0;
        menu->manual.tiled_level_bundle_enabled[i] = false;
        menu->manual.tiled_level_bundle_format[i] = MANUAL_TILE_BUNDLE_FORMAT_PNG;
        menu->manual.tiled_level_bundle_entries[i] = NULL;
    }
    manual_reset_pan(menu);

    if (!menu->manual.directory) {
        menu_show_error(menu, "Manual directory is not set");
        return;
    }

    path_t *manifest_path = path_clone(menu->manual.directory);
    path_push(manifest_path, "manifest.ini");
    mini_t *ini = mini_try_load(path_get(manifest_path));
    path_free(manifest_path);
    if (!ini) {
        menu_show_error(menu, "Manual manifest is missing");
        return;
    }

    char pages_dir_name[64];
    char zoom_dir_name[64];
    char preview_dir_name[64];
    char tiled_pages_dir_name[64];
    char tiled_base_dir_name[64];

    menu->manual.page_count = mini_get_int(ini, "manual", "page_count", 0);
    menu->manual.current_page = mini_get_int(ini, "manual", "start_page", 1) - 1;
    menu->manual.max_zoom_level = mini_get_int(ini, "manual", "max_zoom", MANUAL_DEFAULT_MAX_ZOOM);
    snprintf(menu->manual.title, sizeof(menu->manual.title), "%s", mini_get_string(ini, "manual", "title", "Manual"));
    snprintf(pages_dir_name, sizeof(pages_dir_name), "%s", mini_get_string(ini, "manual", "pages_dir", "pages"));
    snprintf(zoom_dir_name, sizeof(zoom_dir_name), "%s", mini_get_string(ini, "manual", "zoom_dir", "zoom"));
    snprintf(preview_dir_name, sizeof(preview_dir_name), "%s", mini_get_string(ini, "tiled", "preview_dir", "preview"));
    snprintf(tiled_pages_dir_name, sizeof(tiled_pages_dir_name), "%s", mini_get_string(ini, "tiled", "tiles_dir", "pages"));
    snprintf(tiled_base_dir_name, sizeof(tiled_base_dir_name), "%s", tiled_pages_dir_name);
    menu->manual.tiled_level_count = mini_get_int(ini, "tiled", "level_count", 0);
    menu->manual.tiled_page_width = mini_get_int(ini, "tiled", "page_width", 0);
    menu->manual.tiled_page_height = mini_get_int(ini, "tiled", "page_height", 0);
    menu->manual.tiled_tile_size = mini_get_int(ini, "tiled", "tile_size", 0);
    menu->manual.tiled_rows = mini_get_int(ini, "tiled", "rows", 0);
    menu->manual.tiled_cols = mini_get_int(ini, "tiled", "cols", 0);

    if (menu->manual.tiled_level_count <= 0 && menu->manual.tiled_page_width > 0) {
        menu->manual.tiled_level_count = 1;
        menu->manual.tiled_level_zoom[0] = 2;
        menu->manual.tiled_level_page_width[0] = menu->manual.tiled_page_width;
        menu->manual.tiled_level_page_height[0] = menu->manual.tiled_page_height;
        menu->manual.tiled_level_tile_size[0] = menu->manual.tiled_tile_size;
        menu->manual.tiled_level_rows[0] = menu->manual.tiled_rows;
        menu->manual.tiled_level_cols[0] = menu->manual.tiled_cols;
    } else if (menu->manual.tiled_level_count > 0) {
        if (menu->manual.tiled_level_count > MANUAL_TILED_LEVEL_MAX) {
            menu->manual.tiled_level_count = MANUAL_TILED_LEVEL_MAX;
        }
        for (int i = 0; i < menu->manual.tiled_level_count; i++) {
            char section[32];
            char bundle_file_name[128];
            char bundle_index_name[128];
            char bundle_format_name[32];
            snprintf(section, sizeof(section), "tiled_level_%d", i + 1);
            menu->manual.tiled_level_zoom[i] = mini_get_int(ini, section, "zoom_trigger", i + 2);
            menu->manual.tiled_level_page_width[i] = mini_get_int(ini, section, "page_width", 0);
            menu->manual.tiled_level_page_height[i] = mini_get_int(ini, section, "page_height", 0);
            menu->manual.tiled_level_tile_size[i] = mini_get_int(ini, section, "tile_size", 0);
            menu->manual.tiled_level_rows[i] = mini_get_int(ini, section, "rows", 0);
            menu->manual.tiled_level_cols[i] = mini_get_int(ini, section, "cols", 0);
            snprintf(tiled_pages_dir_name, sizeof(tiled_pages_dir_name), "%s", mini_get_string(ini, section, "dir", ""));
            snprintf(bundle_file_name, sizeof(bundle_file_name), "%s", mini_get_string(ini, section, "bundle_file", ""));
            snprintf(bundle_index_name, sizeof(bundle_index_name), "%s", mini_get_string(ini, section, "bundle_index", ""));
            snprintf(bundle_format_name, sizeof(bundle_format_name), "%s", mini_get_string(ini, section, "bundle_format", "png"));
            if (tiled_pages_dir_name[0] != '\0') {
                menu->manual.tiled_level_directories[i] = path_clone(menu->manual.directory);
                path_push(menu->manual.tiled_level_directories[i], tiled_pages_dir_name);
            }
            if (strcasecmp(bundle_format_name, "rgba16") == 0) {
                menu->manual.tiled_level_bundle_format[i] = MANUAL_TILE_BUNDLE_FORMAT_RGBA16;
            } else {
                menu->manual.tiled_level_bundle_format[i] = MANUAL_TILE_BUNDLE_FORMAT_PNG;
            }
            if (bundle_file_name[0] != '\0' && bundle_index_name[0] != '\0') {
                path_t *bundle_file_path = path_clone(menu->manual.directory);
                path_push(bundle_file_path, bundle_file_name);
                path_t *bundle_index_path = path_clone(menu->manual.directory);
                path_push(bundle_index_path, bundle_index_name);
                if (file_exists(path_get(bundle_file_path)) && file_exists(path_get(bundle_index_path))) {
                    manual_tile_bundle_entry_t *entries = NULL;
                    int32_t entry_count = 0;
                    if (manual_load_tile_bundle_index(bundle_index_path, &entries, &entry_count)) {
                        menu->manual.tiled_level_bundle_enabled[i] = true;
                        menu->manual.tiled_level_bundle_files[i] = bundle_file_path;
                        menu->manual.tiled_level_bundle_entries[i] = entries;
                        menu->manual.tiled_level_bundle_entry_counts[i] = entry_count;
                        bundle_file_path = NULL;
                    }
                }
                if (bundle_file_path) {
                    path_free(bundle_file_path);
                }
                path_free(bundle_index_path);
            }
        }
    }

    mini_free(ini);

    if (menu->manual.page_count <= 0) {
        menu_show_error(menu, "Manual manifest has no pages");
        return;
    }
    if (menu->manual.current_page < 0) {
        menu->manual.current_page = 0;
    }
    if (menu->manual.current_page >= menu->manual.page_count) {
        menu->manual.current_page = menu->manual.page_count - 1;
    }
    if (menu->manual.max_zoom_level < 1) {
        menu->manual.max_zoom_level = 1;
    }
    if (menu->manual.tiled_beta && !is_memory_expanded() && menu->manual.max_zoom_level > 3) {
        menu->manual.max_zoom_level = 3;
    }

    if (menu->manual.tiled_beta) {
        if (menu->manual.tiled_level_count <= 0) {
            menu_show_error(menu, "Tiled manual manifest is incomplete");
            return;
        }

        menu->manual.tiled_preview_directory = path_clone(menu->manual.directory);
        path_push(menu->manual.tiled_preview_directory, (char *)preview_dir_name);
        if (!directory_exists(path_get(menu->manual.tiled_preview_directory))) {
            menu_show_error(menu, "Tiled manual preview directory is missing");
            return;
        }
        menu->manual.pages_directory = path_clone(menu->manual.tiled_preview_directory);
        for (int i = 0; i < menu->manual.tiled_level_count; i++) {
            if (!menu->manual.tiled_level_directories[i]) {
                if (!menu->manual.tiled_level_bundle_enabled[i] && i == 0) {
                    menu->manual.tiled_level_directories[i] = path_clone(menu->manual.directory);
                    path_push(menu->manual.tiled_level_directories[i], (char *)tiled_base_dir_name);
                } else if (!menu->manual.tiled_level_bundle_enabled[i]) {
                    menu_show_error(menu, "Tiled manual level directory is missing");
                    return;
                }
            }
            if (!menu->manual.tiled_level_bundle_enabled[i] && !directory_exists(path_get(menu->manual.tiled_level_directories[i]))) {
                menu_show_error(menu, "Tiled manual pages directory is missing");
                return;
            }
            if (menu->manual.tiled_level_page_width[i] <= 0
                || menu->manual.tiled_level_page_height[i] <= 0
                || menu->manual.tiled_level_tile_size[i] <= 0) {
                menu_show_error(menu, "Tiled manual level manifest is incomplete");
                return;
            }
            if (menu->manual.tiled_level_cols[i] <= 0) {
                menu->manual.tiled_level_cols[i] =
                    (menu->manual.tiled_level_page_width[i] + menu->manual.tiled_level_tile_size[i] - 1)
                    / menu->manual.tiled_level_tile_size[i];
            }
            if (menu->manual.tiled_level_rows[i] <= 0) {
                menu->manual.tiled_level_rows[i] =
                    (menu->manual.tiled_level_page_height[i] + menu->manual.tiled_level_tile_size[i] - 1)
                    / menu->manual.tiled_level_tile_size[i];
            }
        }

        manual_apply_active_tiled_level(menu);

        menu->manual.has_zoom_pages = false;
    } else {
        menu->manual.pages_directory = path_clone(menu->manual.directory);
        path_push(menu->manual.pages_directory, (char *)pages_dir_name);
        if (!directory_exists(path_get(menu->manual.pages_directory))) {
            menu_show_error(menu, "Manual pages directory is missing");
            return;
        }

        menu->manual.zoom_pages_directory = path_clone(menu->manual.directory);
        path_push(menu->manual.zoom_pages_directory, (char *)zoom_dir_name);
        menu->manual.has_zoom_pages = directory_exists(path_get(menu->manual.zoom_pages_directory));

        if (!menu->manual.has_zoom_pages) {
            path_free(menu->manual.zoom_pages_directory);
            menu->manual.zoom_pages_directory = NULL;
            menu->manual.max_zoom_level = 2;
        }
    }

    manual_start_page_load(menu);
}

void view_manual_viewer_display (menu_t *menu, surface_t *display) {
    process(menu);
    manual_update_tile_queue(menu, display);
    manual_start_next_tile_load(menu);
    manual_maybe_start_prefetch(menu);
    if (manual_should_use_tiled_page(menu)
        && menu->manual.image
        && manual_is_visible_tile_window_fully_cached(menu, display)) {
        manual_free_current_image(menu);
    }
    draw(menu, display);

    if (menu->next_mode != MENU_MODE_MANUAL_VIEWER) {
        manual_deinit(menu);
    }
}
