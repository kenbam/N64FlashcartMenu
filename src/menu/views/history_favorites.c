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

#define PLAYTIME_LEADERBOARD_MAX 10
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

    if (playtime_ranked_count > PLAYTIME_LEADERBOARD_MAX) {
        playtime_ranked_count = PLAYTIME_LEADERBOARD_MAX;
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

static void item_move_next() {
    int last = selected_item;
    do {
        selected_item++;
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
            if ((uint16_t)selected_item >= playtime_ranked_count) {
                selected_item = last;
                break;
            }
            sound_play_effect(SFX_CURSOR);
            break;
        }
        if (selected_item >= item_max) {
            selected_item = last;
            break;
        } else if (item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            sound_play_effect(SFX_CURSOR);
            break;
        }
    } while (true);
}

static void item_move_previous() {
    int last = selected_item;
    do {
        selected_item--;
        if (selected_item < 0) {
            selected_item = last;
            break;
        }
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
            sound_play_effect(SFX_CURSOR);
            break;
        }
        if (item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            sound_play_effect(SFX_CURSOR);
            break;
        }
    } while (true);
}

static void process(menu_t *menu) {
    if (menu->actions.go_down) {
        item_move_next();
    } else if (menu->actions.go_up) {
        item_move_previous();
    } else if (menu->actions.enter && item_index_valid(selected_item)) {
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_PLAYTIME) {
            playtime_entry_t *entry = playtime_ranked[selected_item];
            if (entry && entry->path) {
                if (menu->browser.select_file) {
                    path_free(menu->browser.select_file);
                }
                menu->browser.select_file = path_create(entry->path);
                menu->next_mode = MENU_MODE_BROWSER;
                sound_play_effect(SFX_ENTER);
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
    if (selected_item != -1) {
        float highlight_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (selected_item * 38);
        ui_components_box_draw(
            VISIBLE_AREA_X0,
            highlight_y,
            VISIBLE_AREA_X0 + FILE_LIST_HIGHLIGHT_WIDTH + LIST_SCROLLBAR_WIDTH,
            highlight_y + 38,
            FILE_LIST_HIGHLIGHT_COLOR
        );
    }

    char buffer[BOOKKEEPING_BUFFER_LEN];
    int cursor = 0;
    buffer[0] = '\0';

    for (uint16_t i = 0; i < item_max; i++) {
        if (path_has_value(item_list[i].primary_path)) {
            buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "%d  : %s\n", (i + 1), path_last_get(item_list[i].primary_path));
        } else {
            buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "%d  : \n", (i + 1));
        }

        if (path_has_value(item_list[i].secondary_path)) {
            buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "     %s\n", path_last_get(item_list[i].secondary_path));
        } else {
            buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "\n");
        }

        if (cursor >= (BOOKKEEPING_BUFFER_LEN - 64)) {
            break;
        }
    }

    int nbytes = strlen(buffer);
    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - (TEXT_MARGIN_VERTICAL * 2),
            .align = ALIGN_LEFT,
            .valign = VALIGN_TOP,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_OFFSET_VERTICAL,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL,
        buffer,
        nbytes
    );
}

static void draw_playtime_leaderboard(void) {
    if (selected_item != -1) {
        int row_height = 19;
        float highlight_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (selected_item * row_height);
        ui_components_box_draw(
            VISIBLE_AREA_X0,
            highlight_y,
            VISIBLE_AREA_X0 + FILE_LIST_HIGHLIGHT_WIDTH + LIST_SCROLLBAR_WIDTH,
            highlight_y + row_height,
            FILE_LIST_HIGHLIGHT_COLOR
        );
    }

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

    char buffer[BOOKKEEPING_BUFFER_LEN];
    int cursor = 0;
    buffer[0] = '\0';

    for (uint16_t i = 0; i < playtime_ranked_count; i++) {
        playtime_entry_t *entry = playtime_ranked[i];
        if (!entry || !entry->path) {
            continue;
        }
        char duration[64];
        format_duration(duration, sizeof(duration), entry->total_seconds);
        const char *name = file_basename(strip_fs_prefix(entry->path));
        if (!name || name[0] == '\0') {
            name = entry->path;
        }
        buffer_appendf(buffer, BOOKKEEPING_BUFFER_LEN, &cursor, "%2d. %-42.42s  %s\n", (int)(i + 1), name, duration);
        if (cursor >= (BOOKKEEPING_BUFFER_LEN - 64)) {
            break;
        }
    }

    int nbytes = strlen(buffer);
    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - (TEXT_MARGIN_VERTICAL * 2),
            .align = ALIGN_LEFT,
            .valign = VALIGN_TOP,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_OFFSET_VERTICAL,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL,
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
        playtime_list_free();
    }
}
