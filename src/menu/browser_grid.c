/**
 * @file browser_grid.c
 * @brief Playlist grid view subsystem for the file browser.
 * @ingroup menu
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "browser_grid.h"
#include "fonts.h"
#include "png_decoder.h"
#include "rom_info.h"
#include "ui_components.h"
#include "ui_components/constants.h"

/* ======================================================================== */
/* Thumbnail slot pool                                                       */
/* ======================================================================== */

typedef struct {
    int entry_index;
    char *entry_path;
    component_boxart_t *boxart;
    bool boxart_resolved;
    uint32_t last_used_frame;
} grid_thumb_slot_t;

#define GRID_VISIBLE_SLOTS 12
#define GRID_THUMB_SLOTS   25
#define GRID_MAX_ENTRIES   64

static grid_thumb_slot_t grid_slots[GRID_THUMB_SLOTS];
static int8_t grid_entry_to_slot[GRID_MAX_ENTRIES];
static entry_t *grid_slots_list = NULL;
static bool grid_page_mem_warm_done = false;
static uint32_t grid_frame_counter = 0;

/* ======================================================================== */
/* Per-entry ROM header meta cache (game_code + title for boxart lookup)     */
/* ======================================================================== */

typedef struct {
    bool attempted;
    bool loaded;
    char game_code[4];
    char rom_title[21];
} grid_meta_index_entry_t;

static grid_meta_index_entry_t *grid_meta_index = NULL;
static int grid_meta_index_count = 0;
static entry_t *grid_meta_index_list = NULL;

/* ======================================================================== */
/* Incremental prewarm state                                                 */
/* ======================================================================== */

static int prewarm_next_index = -1;
static int prewarm_total = 0;
static int prewarm_cooldown = 0;

/* ======================================================================== */
/* View enabled / runtime override                                           */
/* ======================================================================== */

static bool grid_view_enabled = false;
static int grid_runtime_override = -1; /* -1=playlist/default, 0=list, 1=grid */

/* ======================================================================== */
/* Internal helpers                                                          */
/* ======================================================================== */

static void grid_slots_clear(void) {
    for (int i = 0; i < GRID_THUMB_SLOTS; i++) {
        if (grid_slots[i].boxart) {
            ui_components_boxart_free(grid_slots[i].boxart);
        }
        free(grid_slots[i].entry_path);
        memset(&grid_slots[i], 0, sizeof(grid_slots[i]));
        grid_slots[i].entry_index = -1;
    }
    memset(grid_entry_to_slot, -1, sizeof(grid_entry_to_slot));
    grid_slots_list = NULL;
    grid_page_mem_warm_done = false;
    grid_frame_counter = 0;
}

static int grid_slot_find(int entry_index) {
    if (entry_index < 0 || entry_index >= GRID_MAX_ENTRIES) {
        return -1;
    }
    return grid_entry_to_slot[entry_index];
}

static int grid_slot_alloc(void) {
    int best = -1;
    uint32_t best_frame = UINT32_MAX;
    for (int i = 0; i < GRID_THUMB_SLOTS; i++) {
        if (grid_slots[i].entry_index < 0) {
            return i;
        }
        if (grid_slots[i].last_used_frame < best_frame) {
            best_frame = grid_slots[i].last_used_frame;
            best = i;
        }
    }
    if (best >= 0) {
        int old_ei = grid_slots[best].entry_index;
        if (old_ei >= 0 && old_ei < GRID_MAX_ENTRIES) {
            grid_entry_to_slot[old_ei] = -1;
        }
        if (grid_slots[best].boxart) {
            ui_components_boxart_free(grid_slots[best].boxart);
        }
        free(grid_slots[best].entry_path);
        memset(&grid_slots[best], 0, sizeof(grid_slots[best]));
        grid_slots[best].entry_index = -1;
    }
    return best;
}

static void grid_slot_register(int entry_index, int slot_index) {
    if (entry_index >= 0 && entry_index < GRID_MAX_ENTRIES) {
        grid_entry_to_slot[entry_index] = (int8_t)slot_index;
    }
}

static void grid_meta_index_clear(void) {
    free(grid_meta_index);
    grid_meta_index = NULL;
    grid_meta_index_count = 0;
    grid_meta_index_list = NULL;
}

static void grid_meta_index_reset_for_current_list(menu_t *menu) {
    if (!menu) {
        grid_meta_index_clear();
        return;
    }
    if (grid_meta_index_list == menu->browser.list &&
        grid_meta_index_count == menu->browser.entries) {
        return;
    }
    grid_meta_index_clear();
    if (!menu->browser.playlist || menu->browser.entries <= 0 || !menu->browser.list) {
        return;
    }
    grid_meta_index = calloc((size_t)menu->browser.entries, sizeof(*grid_meta_index));
    if (!grid_meta_index) {
        return;
    }
    grid_meta_index_count = menu->browser.entries;
    grid_meta_index_list = menu->browser.list;
}

static bool grid_get_boxart_meta_by_index(menu_t *menu, int entry_index, char game_code_out[4], char rom_title_out[21]) {
    if (!menu || entry_index < 0 || entry_index >= menu->browser.entries || !menu->browser.list) {
        return false;
    }

    grid_meta_index_reset_for_current_list(menu);
    if (!grid_meta_index || entry_index >= grid_meta_index_count) {
        return false;
    }

    grid_meta_index_entry_t *idx = &grid_meta_index[entry_index];
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

static bool grid_get_boxart_meta_by_index_cached_only(menu_t *menu, int entry_index, char game_code_out[4], char rom_title_out[21]) {
    if (!menu || entry_index < 0 || entry_index >= menu->browser.entries) {
        return false;
    }
    grid_meta_index_reset_for_current_list(menu);
    if (!grid_meta_index || entry_index >= grid_meta_index_count) {
        return false;
    }

    grid_meta_index_entry_t *idx = &grid_meta_index[entry_index];
    if (!idx->attempted || !idx->loaded) {
        return false;
    }

    memcpy(game_code_out, idx->game_code, 4);
    memcpy(rom_title_out, idx->rom_title, 21);
    return true;
}

static int grid_slot_prepare(menu_t *menu, int entry_index, bool memory_cache_only) {
    if (!menu || entry_index < 0 || entry_index >= menu->browser.entries ||
        entry_index >= GRID_MAX_ENTRIES) {
        return -1;
    }

    entry_t *entry = &menu->browser.list[entry_index];
    if (entry->type != ENTRY_TYPE_ROM || !entry->path) {
        return -1;
    }

    int si = grid_slot_find(entry_index);
    if (si >= 0) {
        grid_thumb_slot_t *slot = &grid_slots[si];
        slot->last_used_frame = grid_frame_counter;
        if (slot->boxart_resolved || memory_cache_only) {
            return si;
        }
    } else {
        if (memory_cache_only) {
            char game_code[4];
            char safe_title[21];
            if (!grid_get_boxart_meta_by_index_cached_only(menu, entry_index, game_code, safe_title)) {
                return -1;
            }
            si = grid_slot_alloc();
            if (si < 0) return -1;
            grid_thumb_slot_t *slot = &grid_slots[si];
            slot->entry_index = entry_index;
            slot->entry_path = strdup(entry->path);
            slot->last_used_frame = grid_frame_counter;
            slot->boxart = ui_components_boxart_init_grid_memory_cached(menu->storage_prefix, game_code, safe_title);
            grid_slot_register(entry_index, si);
            return si;
        }
        si = grid_slot_alloc();
        if (si < 0) return -1;
        grid_thumb_slot_t *slot = &grid_slots[si];
        slot->entry_index = entry_index;
        slot->entry_path = strdup(entry->path);
        slot->last_used_frame = grid_frame_counter;
        grid_slot_register(entry_index, si);
    }

    grid_thumb_slot_t *slot = &grid_slots[si];
    char game_code[4];
    char safe_title[21];
    bool have_meta = grid_get_boxart_meta_by_index(menu, entry_index, game_code, safe_title);
    if (have_meta) {
        if (slot->boxart) {
            ui_components_boxart_free(slot->boxart);
        }
        slot->boxart = ui_components_boxart_init_grid(menu->storage_prefix, game_code, safe_title);
    }
    slot->boxart_resolved = true;
    return si;
}

static void grid_prewarm_start(menu_t *menu) {
    prewarm_next_index = -1;
    prewarm_total = 0;
    if (!menu || !menu->browser.playlist || menu->browser.entries <= 0) {
        return;
    }
    grid_meta_index_reset_for_current_list(menu);
    if (!grid_meta_index) {
        return;
    }
    prewarm_next_index = 0;
    prewarm_total = menu->browser.entries;
    prewarm_cooldown = 0;
}

/* ======================================================================== */
/* Public API                                                                */
/* ======================================================================== */

const char *browser_grid_display_name(const char *name, char *buffer, size_t buffer_size) {
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

    for (char *p = buffer; *p; p++) {
        if (*p == ' ' && p[1] == ' ') {
            memmove(p, p + 1, strlen(p));
            p--;
        }
    }
    return buffer;
}

bool browser_grid_is_enabled(menu_t *menu) {
    if (!menu) {
        return false;
    }
    if (!menu->browser.playlist) {
        return false;
    }
    if (menu->browser.picker != BROWSER_PICKER_NONE) {
        return false;
    }
    if (grid_runtime_override == 0 || grid_runtime_override == 1) {
        return (grid_runtime_override == 1);
    }
    return grid_view_enabled;
}

void browser_grid_set_override(int override) {
    grid_runtime_override = override;
}

void browser_grid_set_enabled(bool enabled) {
    grid_view_enabled = enabled;
}

bool browser_grid_get_enabled(void) {
    return grid_view_enabled;
}

void browser_grid_clear(void) {
    grid_slots_clear();
    grid_meta_index_clear();
    prewarm_next_index = -1;
    prewarm_total = 0;
    prewarm_cooldown = 0;
}

void browser_grid_reset_meta_index(menu_t *menu) {
    grid_meta_index_clear();
    grid_slots_clear();
    if (menu) {
        grid_prewarm_start(menu);
    }
}

void browser_grid_prewarm_tick(menu_t *menu) {
    if (prewarm_next_index < 0 || prewarm_next_index >= prewarm_total) {
        return;
    }
    if (png_decoder_is_busy()) {
        return;
    }
    if (prewarm_cooldown > 0) {
        prewarm_cooldown--;
        return;
    }
    char gc[4], title[21];
    if (grid_get_boxart_meta_by_index(menu, prewarm_next_index, gc, title)) {
        ui_components_boxart_prewarm_dir(menu->storage_prefix, gc, title);
    }
    prewarm_next_index++;
    prewarm_cooldown = 2;
}

void browser_grid_draw(menu_t *menu) {
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

    if (grid_slots_list != menu->browser.list) {
        grid_slots_clear();
        grid_slots_list = menu->browser.list;
    }

    for (int i = 0; i < visible; i++) {
        int entry_index = page_start + i;
        int col = i % cols;
        int row = i / cols;
        int x0 = grid_x + col * (tile_w + gap_x);
        int y0 = grid_y + row * (tile_h + gap_y);
        int x1 = x0 + tile_w;
        bool is_selected = (entry_index == selected);

        int si = grid_slot_find(entry_index);
        grid_thumb_slot_t *slot = (si >= 0) ? &grid_slots[si] : NULL;
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
                rdpq_set_mode_fill(ui_components_get_highlight_color());
                rdpq_fill_rectangle(x0 - 2, y0 - 2, x1 + 2, y0);
                rdpq_fill_rectangle(x0 - 2, sy1, x1 + 2, sy1 + 2);
                rdpq_fill_rectangle(x0 - 2, y0, x0, sy1);
                rdpq_fill_rectangle(x1, y0, x1 + 2, sy1);
            rdpq_mode_pop();
        }
    }

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

void browser_grid_prepare(menu_t *menu, bool defer_work) {
    if (!menu || !browser_grid_is_enabled(menu)) {
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

    grid_frame_counter++;

    if (grid_slots_list != menu->browser.list) {
        grid_slots_clear();
        grid_slots_list = menu->browser.list;
    }

    for (int i = 0; i < visible; i++) {
        int ei = page_start + i;
        int si = grid_slot_find(ei);
        if (si >= 0) {
            grid_slots[si].last_used_frame = grid_frame_counter;
        } else {
            grid_slot_prepare(menu, ei, true);
        }
    }

    if (defer_work) {
        return;
    }

    int resolved = 0;
    int sel_si = grid_slot_find(selected);
    if (sel_si < 0 || !grid_slots[sel_si].boxart_resolved) {
        grid_slot_prepare(menu, selected, false);
        resolved++;
    }
    for (int i = 0; i < visible && resolved < 2; i++) {
        int ei = page_start + i;
        if (ei == selected) continue;
        int si = grid_slot_find(ei);
        if (si < 0 || !grid_slots[si].boxart_resolved) {
            grid_slot_prepare(menu, ei, false);
            resolved++;
        }
    }
    if (resolved > 0) return;
    int next_page_start = page_start + page_size;
    if (next_page_start < entries) {
        int next_visible = entries - next_page_start;
        if (next_visible > page_size) next_visible = page_size;
        for (int i = 0; i < next_visible; i++) {
            int ei = next_page_start + i;
            int si = grid_slot_find(ei);
            if (si < 0 || !grid_slots[si].boxart_resolved) {
                grid_slot_prepare(menu, ei, false);
                return;
            }
        }
    }
}
