#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "../bookkeeping.h"
#include "../fonts.h"
#include "../playtime.h"
#include "../sound.h"
#include "../ui_components/constants.h"
#include "utils/fs.h"
#include "views.h"

#define BOOKKEEPING_BUFFER_LEN 3072

typedef enum {
    BOOKKEEPING_TAB_CONTEXT_HISTORY,
    BOOKKEEPING_TAB_CONTEXT_FAVORITE,
    BOOKKEEPING_TAB_CONTEXT_PLAYTIME,
    BOOKKEEPING_TAB_CONTEXT_NONE
} bookkeeping_tab_context_t;

static bookkeeping_tab_context_t tab_context = BOOKKEEPING_TAB_CONTEXT_NONE;
static int selected_item = -1;
static bookkeeping_item_t *item_list;
static uint16_t item_max = 0;
static playtime_entry_t **playtime_ranked = NULL;
static uint16_t playtime_ranked_count = 0;

static int bookkeeping_count_visible_items(void) {
    if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
        return (int)playtime_ranked_count;
    }

    int count = 0;
    for (uint16_t i = 0; i < item_max; i++) {
        if (item_list[i].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            count++;
        }
    }
    return count;
}

static int bookkeeping_logical_index_for_item(int selected) {
    if (selected < 0 || tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
        return selected;
    }

    int logical_index = 0;
    for (uint16_t i = 0; i < item_max; i++) {
        if (item_list[i].bookkeeping_type == BOOKKEEPING_TYPE_EMPTY) {
            continue;
        }
        if ((int)i == selected) {
            return logical_index;
        }
        logical_index++;
    }

    return -1;
}

static int bookkeeping_compute_start_index(int selected_logical, int visible_entries) {
    int total_entries = bookkeeping_count_visible_items();
    if (visible_entries <= 0 || total_entries <= visible_entries || selected_logical < 0) {
        return 0;
    }

    int start = selected_logical - (visible_entries / 2);
    int max_start = total_entries - visible_entries;
    if (start < 0) {
        start = 0;
    } else if (start > max_start) {
        start = max_start;
    }
    return start;
}

static bool resolve_existing_rom_path(const char *storage_prefix, const char *current_path, char *out, size_t out_len) {
    if (!current_path || current_path[0] == '\0' || !out || out_len == 0) {
        return false;
    }
    if (file_exists((char *)current_path)) {
        snprintf(out, out_len, "%s", current_path);
        return true;
    }
    if (storage_prefix && current_path[0] == '/') {
        path_t *prefixed = path_init(storage_prefix, (char *)current_path);
        if (prefixed && file_exists(path_get(prefixed))) {
            snprintf(out, out_len, "%s", path_get(prefixed));
            path_free(prefixed);
            return true;
        }
        path_free(prefixed);
    }
    return false;
}

static bool resolve_playtime_entry_path(menu_t *menu, playtime_entry_t *entry) {
    if (!menu || !entry) {
        return false;
    }

    char resolved_path[512];
    if (resolve_existing_rom_path(menu->storage_prefix, entry->path, resolved_path, sizeof(resolved_path))) {
        if (!entry->path || strcmp(entry->path, resolved_path) != 0) {
            free(entry->path);
            entry->path = strdup(resolved_path);
            menu->playtime.dirty = true;
        }
        return true;
    }
    if (entry->game_id[0] == '\0') {
        return false;
    }

    if (!rom_info_resolve_stable_id_path(menu->storage_prefix, entry->game_id, entry->path, resolved_path, sizeof(resolved_path))) {
        return false;
    }

    free(entry->path);
    entry->path = strdup(resolved_path);
    menu->playtime.dirty = true;
    return (entry->path != NULL);
}

static void buffer_appendf(char *buffer, size_t length, int *cursor, const char *fmt, ...) {
    if (!buffer || !cursor || *cursor < 0 || (size_t)(*cursor) >= length) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + *cursor, length - (size_t)(*cursor), fmt, args);
    va_end(args);

    if (written <= 0) {
        return;
    }

    *cursor += written;
    if ((size_t)(*cursor) >= length) {
        *cursor = (int)length - 1;
        buffer[*cursor] = '\0';
    }
}

static void playtime_list_free(void) {
    free(playtime_ranked);
    playtime_ranked = NULL;
    playtime_ranked_count = 0;
}

static int playtime_compare(const void *a, const void *b) {
    const playtime_entry_t *lhs = *(const playtime_entry_t * const *)a;
    const playtime_entry_t *rhs = *(const playtime_entry_t * const *)b;

    if (lhs->total_seconds < rhs->total_seconds) return 1;
    if (lhs->total_seconds > rhs->total_seconds) return -1;
    if (lhs->last_played < rhs->last_played) return 1;
    if (lhs->last_played > rhs->last_played) return -1;
    return 0;
}

static void playtime_list_rebuild(menu_t *menu) {
    playtime_list_free();

    for (uint32_t i = 0; i < menu->playtime.count; i++) {
        playtime_entry_t *entry = &menu->playtime.entries[i];
        if (entry->total_seconds == 0) {
            continue;
        }
        playtime_entry_t **next = realloc(playtime_ranked, (playtime_ranked_count + 1) * sizeof(playtime_entry_t *));
        if (!next) {
            break;
        }
        playtime_ranked = next;
        playtime_ranked[playtime_ranked_count++] = entry;
    }

    if (playtime_ranked_count > 1) {
        qsort(playtime_ranked, playtime_ranked_count, sizeof(playtime_entry_t *), playtime_compare);
    }

}

static void format_duration(char *out, size_t out_len, uint64_t seconds) {
    uint64_t hrs = seconds / 3600;
    uint64_t mins = (seconds % 3600) / 60;
    uint64_t secs = seconds % 60;
    if (hrs > 0) {
        snprintf(out, out_len, "%lluh %llum %llus", (unsigned long long)hrs, (unsigned long long)mins, (unsigned long long)secs);
    } else if (mins > 0) {
        snprintf(out, out_len, "%llum %llus", (unsigned long long)mins, (unsigned long long)secs);
    } else {
        snprintf(out, out_len, "%llus", (unsigned long long)secs);
    }
}

static bool item_index_valid(int index) {
    if (index < 0) {
        return false;
    }
    if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
        return ((uint16_t)index < playtime_ranked_count);
    }
    return ((uint16_t)index < item_max && item_list[index].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY);
}

static void item_reset_selected(menu_t *menu) {
    (void)menu;
    selected_item = -1;

    if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
        if (playtime_ranked_count > 0) {
            selected_item = 0;
        }
        return;
    }

    for (uint16_t i = 0; i < item_max; i++) {
        if (item_list[i].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            selected_item = i;
            break;
        }
    }
}

static bool item_move_next() {
    int last = selected_item;
    do {
        selected_item++;
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
            if ((uint16_t)selected_item >= playtime_ranked_count) {
                selected_item = last;
                break;
            }
            return true;
        }
        if (selected_item >= item_max) {
            selected_item = last;
            break;
        } else if (item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            return true;
        }
    } while (true);
    return false;
}

static bool item_move_previous() {
    int last = selected_item;
    do {
        selected_item--;
        if (selected_item < 0) {
            selected_item = last;
            break;
        }
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
            return true;
        }
        if (item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            return true;
        }
    } while (true);
    return false;
}

static void process(menu_t *menu) {
    if (menu->actions.go_down) {
        int steps = 1;
        if (menu->actions.go_fast) {
            steps = 10;
        }
        bool moved = false;
        for (int i = 0; i < steps; i++) {
            if (!item_move_next()) {
                break;
            }
            moved = true;
        }
        if (moved) {
            sound_play_effect(SFX_CURSOR);
        }
    } else if (menu->actions.go_up) {
        int steps = 1;
        if (menu->actions.go_fast) {
            steps = 10;
        }
        bool moved = false;
        for (int i = 0; i < steps; i++) {
            if (!item_move_previous()) {
                break;
            }
            moved = true;
        }
        if (moved) {
            sound_play_effect(SFX_CURSOR);
        }
    } else if (menu->actions.enter && item_index_valid(selected_item)) {
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
            playtime_entry_t *entry = playtime_ranked[selected_item];
            if (entry && resolve_playtime_entry_path(menu, entry) && entry->path) {
                if (menu->load.rom_path) {
                    path_free(menu->load.rom_path);
                }
                menu->load.rom_path = path_create(entry->path);
                menu->load.load_history_id = -1;
                menu->load.load_favorite_id = -1;
                menu->load.back_mode = MENU_MODE_PLAYTIME;
                menu->next_mode = MENU_MODE_LOAD_ROM;
                sound_play_effect(SFX_ENTER);
            } else {
                menu_show_error(menu, "Couldn't locate ROM");
            }
            return;
        }

        if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->load.load_favorite_id = selected_item;
            menu->load.load_history_id = -1;
        } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->load.load_history_id = selected_item;
            menu->load.load_favorite_id = -1;
        }

        menu->load.back_mode = MENU_MODE_HISTORY;
        if (item_list[selected_item].bookkeeping_type == BOOKKEEPING_TYPE_DISK) {
            menu->next_mode = MENU_MODE_LOAD_DISK;
            sound_play_effect(SFX_ENTER);
        } else if (item_list[selected_item].bookkeeping_type == BOOKKEEPING_TYPE_ROM) {
            menu->next_mode = MENU_MODE_LOAD_ROM;
            sound_play_effect(SFX_ENTER);
        }
    } else if (menu->actions.go_left) {
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
            menu->next_mode = MENU_MODE_FAVORITE;
        } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->next_mode = MENU_MODE_HISTORY;
        } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->next_mode = MENU_MODE_BROWSER;
        }
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_right) {
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->next_mode = MENU_MODE_FAVORITE;
        } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->next_mode = MENU_MODE_PLAYTIME;
        } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
            menu->next_mode = MENU_MODE_BROWSER;
        }
        sound_play_effect(SFX_CURSOR);
    } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE && menu->actions.options && selected_item != -1) {
        bookkeeping_favorite_remove(&menu->bookkeeping, selected_item);
        item_reset_selected(menu);
        sound_play_effect(SFX_SETTING);
    }
}

static void draw_bookkeeping_list(void) {
    const int row_height = 38;
    const int list_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL;
    const int list_bottom = LAYOUT_ACTIONS_SEPARATOR_Y - TEXT_MARGIN_VERTICAL;
    int list_height = list_bottom - list_y;
    if (list_height < row_height) {
        list_height = row_height;
    }

    int max_visible_entries = list_height / row_height;
    if (max_visible_entries < 1) {
        max_visible_entries = 1;
    }

    int total_entries = bookkeeping_count_visible_items();
    int selected_logical = bookkeeping_logical_index_for_item(selected_item);
    int starting_position = bookkeeping_compute_start_index(selected_logical, max_visible_entries);

    if (total_entries > max_visible_entries) {
        ui_components_list_scrollbar_draw(selected_logical, total_entries, max_visible_entries);
    }

    if (selected_logical != -1 && selected_logical >= starting_position && selected_logical < (starting_position + max_visible_entries)) {
        float highlight_y = (float)(list_y + ((selected_logical - starting_position) * row_height));
        ui_components_box_draw(
            VISIBLE_AREA_X0,
            highlight_y,
            VISIBLE_AREA_X0 + FILE_LIST_HIGHLIGHT_WIDTH + LIST_SCROLLBAR_WIDTH,
            highlight_y + row_height,
            FILE_LIST_HIGHLIGHT_COLOR
        );
    }

    char buffer[BOOKKEEPING_BUFFER_LEN];
    int cursor = 0;
    buffer[0] = '\0';

    int logical_index = 0;
    int ending_position = starting_position + max_visible_entries;
    for (uint16_t i = 0; i < item_max; i++) {
        if (item_list[i].bookkeeping_type == BOOKKEEPING_TYPE_EMPTY) {
            continue;
        }
        if (logical_index < starting_position) {
            logical_index++;
            continue;
        }
        if (logical_index >= ending_position) {
            break;
        }

        if (path_has_value(item_list[i].primary_path)) {
            buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "%d  : %s\n", (logical_index + 1), path_last_get(item_list[i].primary_path));
        } else {
            buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "%d  : \n", (logical_index + 1));
        }

        if (path_has_value(item_list[i].secondary_path)) {
            buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "     %s\n", path_last_get(item_list[i].secondary_path));
        } else {
            buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "\n");
        }

        logical_index++;
        if (cursor >= (BOOKKEEPING_BUFFER_LEN - 64)) {
            break;
        }
    }

    int nbytes = strlen(buffer);
    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = list_height,
            .align = ALIGN_LEFT,
            .valign = VALIGN_TOP,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_OFFSET_VERTICAL,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        list_y,
        buffer,
        nbytes
    );
}

static void draw_playtime_leaderboard(void) {
    if (playtime_ranked_count == 0) {
        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n"
            "^%02XNo playtime data yet",
            STL_GRAY
        );
        return;
    }

    const int row_height = 19;
    const int list_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL;
    const int list_bottom = LAYOUT_ACTIONS_SEPARATOR_Y - TEXT_MARGIN_VERTICAL;
    int list_height = list_bottom - list_y;
    if (list_height < row_height) {
        list_height = row_height;
    }

    int max_visible_entries = list_height / row_height;
    if (max_visible_entries < 1) {
        max_visible_entries = 1;
    } else if (max_visible_entries > LIST_ENTRIES) {
        max_visible_entries = LIST_ENTRIES;
    }

    int starting_position = 0;
    if ((int)playtime_ranked_count > max_visible_entries && selected_item >= (max_visible_entries / 2)) {
        starting_position = selected_item - (max_visible_entries / 2);
        int max_start = (int)playtime_ranked_count - max_visible_entries;
        if (starting_position > max_start) {
            starting_position = max_start;
        }
    }

    int visible_entries = (int)playtime_ranked_count - starting_position;
    if (visible_entries > max_visible_entries) {
        visible_entries = max_visible_entries;
    }
    if (visible_entries < 0) {
        visible_entries = 0;
    }

    ui_components_list_scrollbar_draw(selected_item, playtime_ranked_count, max_visible_entries);

    if ((selected_item >= starting_position) && (selected_item < (starting_position + visible_entries))) {
        float highlight_y = (float)(list_y + (selected_item - starting_position) * row_height);
        ui_components_box_draw(
            VISIBLE_AREA_X0,
            highlight_y,
            VISIBLE_AREA_X0 + FILE_LIST_HIGHLIGHT_WIDTH + LIST_SCROLLBAR_WIDTH,
            highlight_y + row_height,
            FILE_LIST_HIGHLIGHT_COLOR
        );
    }

    char buffer[BOOKKEEPING_BUFFER_LEN];
    int cursor = 0;
    buffer[0] = '\0';

    for (int i = 0; i < visible_entries; i++) {
        int absolute_index = starting_position + i;
        playtime_entry_t *entry = playtime_ranked[absolute_index];
        if (!entry || !entry->path) {
            continue;
        }
        char duration[64];
        format_duration(duration, sizeof(duration), entry->total_seconds);
        const char *name = file_basename(strip_fs_prefix(entry->path));
        if (!name || name[0] == '\0') {
            name = entry->path;
        }
        buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "%2d. %-42.42s  %s\n", absolute_index + 1, name, duration);
        if (cursor >= (BOOKKEEPING_BUFFER_LEN - 64)) {
            break;
        }
    }

    int nbytes = strlen(buffer);
    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = list_height,
            .align = ALIGN_LEFT,
            .valign = VALIGN_TOP,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_OFFSET_VERTICAL,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        list_y,
        buffer,
        nbytes
    );
}

static void draw(menu_t *menu, surface_t *display) {
    rdpq_attach(display, NULL);
    ui_components_background_draw();

    if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
        ui_components_tabs_common_draw(3);
    } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
        ui_components_tabs_common_draw(2);
    } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
        ui_components_tabs_common_draw(1);
    }

    ui_components_layout_draw_tabbed();

    if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
        draw_playtime_leaderboard();
    } else {
        draw_bookkeeping_list();
    }

    if (item_index_valid(selected_item)) {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Open Game\n\n"
        );

        if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            ui_components_actions_bar_text_draw(
                STL_DEFAULT,
                ALIGN_RIGHT, VALIGN_TOP,
                "R: Remove item\n\n"
            );
        }
    }

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "◀ Change Tab ▶\n\n"
    );

    rdpq_detach_show();
}

void view_favorite_init(menu_t *menu) {
    tab_context = BOOKKEEPING_TAB_CONTEXT_FAVORITE;
    item_list = menu->bookkeeping.favorite_items;
    item_max = FAVORITES_COUNT;
    item_reset_selected(menu);
}

void view_favorite_display(menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}

void view_history_init(menu_t *menu) {
    tab_context = BOOKKEEPING_TAB_CONTEXT_HISTORY;
    item_list = menu->bookkeeping.history_items;
    item_max = HISTORY_COUNT;
    item_reset_selected(menu);
}

void view_history_display(menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}

void view_playtime_init(menu_t *menu) {
    tab_context = BOOKKEEPING_TAB_CONTEXT_PLAYTIME;
    playtime_list_rebuild(menu);
    item_reset_selected(menu);
}

void view_playtime_display(menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);

    if (menu->next_mode != MENU_MODE_PLAYTIME) {
        playtime_save_if_dirty(&menu->playtime);
        playtime_list_free();
    }
}
