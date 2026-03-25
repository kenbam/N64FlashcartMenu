/**
 * @file browser_search.c
 * @brief Local search overlay for the file browser.
 * @ingroup menu
 */

#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "browser_search.h"
#include "fonts.h"
#include "sound.h"
#include "ui_components.h"
#include "ui_components/constants.h"

typedef struct {
    const char *label;
    const char *text;
} browser_search_key_t;

static bool search_active = false;
static char search_query[32];
static int *search_matches = NULL;
static int search_match_count = 0;
static int search_selected = 0;
static int search_key_row = 0;
static int search_key_col = 0;
static bool search_focus_results = false;

#define BROWSER_SEARCH_HEADER_X0   (VISIBLE_AREA_X0 + 10)
#define BROWSER_SEARCH_HEADER_X1   (VISIBLE_AREA_X1 - 10)
#define BROWSER_SEARCH_HEADER_Y0   (VISIBLE_AREA_Y0 + TAB_HEIGHT + 8)
#define BROWSER_SEARCH_HEADER_Y1   (BROWSER_SEARCH_HEADER_Y0 + 228)
#define BROWSER_SEARCH_RESULTS_Y0  (BROWSER_SEARCH_HEADER_Y1 + 8)
#define BROWSER_SEARCH_RESULTS_Y1  (LAYOUT_ACTIONS_SEPARATOR_Y - 8)
#define BROWSER_SEARCH_RESULTS_X0  (VISIBLE_AREA_X0 + 8)
#define BROWSER_SEARCH_RESULTS_X1  (VISIBLE_AREA_X1 - 8)
#define BROWSER_SEARCH_LIST_X      (BROWSER_SEARCH_RESULTS_X0 + 10)
#define BROWSER_SEARCH_LIST_Y0     (BROWSER_SEARCH_RESULTS_Y0 + 24)
#define BROWSER_SEARCH_LIST_Y1     (BROWSER_SEARCH_RESULTS_Y1 - 8)
#define BROWSER_SEARCH_RESULT_ROW_H (19)

static const browser_search_key_t search_keyboard[][10] = {
    {
        {"A", "A"}, {"B", "B"}, {"C", "C"}, {"D", "D"}, {"E", "E"},
        {"F", "F"}, {"G", "G"}, {"H", "H"}, {"I", "I"}, {"J", "J"},
    },
    {
        {"K", "K"}, {"L", "L"}, {"M", "M"}, {"N", "N"}, {"O", "O"},
        {"P", "P"}, {"Q", "Q"}, {"R", "R"}, {"S", "S"}, {"T", "T"},
    },
    {
        {"U", "U"}, {"V", "V"}, {"W", "W"}, {"X", "X"}, {"Y", "Y"},
        {"Z", "Z"}, {"0", "0"}, {"1", "1"}, {"2", "2"}, {"3", "3"},
    },
    {
        {"4", "4"}, {"5", "5"}, {"6", "6"}, {"7", "7"}, {"8", "8"},
        {"9", "9"}, {"-", "-"}, {"_", "_"}, {".", "."}, {"'", "'"},
    },
    {
        {"Space", " "}, {"Del", "\b"}, {"Clear", "\f"}, {"Done", "\r"},
        {"", ""}, {"", ""}, {"", ""}, {"", ""}, {"", ""}, {"", ""},
    },
};

static const int search_keyboard_row_lengths[] = { 10, 10, 10, 10, 4 };

#define KEYBOARD_ROWS ((int)(sizeof(search_keyboard_row_lengths) / sizeof(search_keyboard_row_lengths[0])))

static void search_clear_matches(void) {
    free(search_matches);
    search_matches = NULL;
    search_match_count = 0;
    search_selected = 0;
}

bool browser_search_is_active(void) {
    return search_active;
}

void browser_search_reset_state(void) {
    search_active = false;
    search_query[0] = '\0';
    search_key_row = 0;
    search_key_col = 0;
    search_focus_results = false;
    search_clear_matches();
}

static char search_fold_char(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static bool search_name_matches(const char *name, const char *query) {
    if (!query || query[0] == '\0') {
        return true;
    }
    if (!name) {
        return false;
    }

    size_t query_len = strlen(query);
    for (size_t start = 0; name[start] != '\0'; start++) {
        size_t i = 0;
        while (i < query_len && name[start + i] != '\0' &&
               search_fold_char(name[start + i]) == search_fold_char(query[i])) {
            i++;
        }
        if (i == query_len) {
            return true;
        }
    }
    return false;
}

static void search_rebuild(menu_t *menu) {
    search_clear_matches();

    if (!menu || menu->browser.entries <= 0 || !menu->browser.list) {
        return;
    }

    search_matches = malloc((size_t)menu->browser.entries * sizeof(int));
    if (!search_matches) {
        return;
    }

    for (int i = 0; i < menu->browser.entries; i++) {
        if (search_name_matches(menu->browser.list[i].name, search_query)) {
            search_matches[search_match_count++] = i;
        }
    }

    if (search_match_count <= 0) {
        search_selected = 0;
        return;
    }

    int current_selected = menu->browser.selected;
    for (int i = 0; i < search_match_count; i++) {
        if (search_matches[i] == current_selected) {
            search_selected = i;
            return;
        }
    }
    search_selected = 0;
}

static void search_sync_selection(menu_t *menu) {
    if (!menu || search_match_count <= 0 || !search_matches) {
        return;
    }

    if (search_selected < 0) {
        search_selected = 0;
    } else if (search_selected >= search_match_count) {
        search_selected = search_match_count - 1;
    }

    menu->browser.selected = search_matches[search_selected];
    if (menu->browser.selected >= 0 && menu->browser.selected < menu->browser.entries) {
        menu->browser.entry = &menu->browser.list[menu->browser.selected];
    }
}

void browser_search_open(menu_t *menu, void *arg) {
    (void)arg;
    search_active = true;
    search_query[0] = '\0';
    search_key_row = 0;
    search_key_col = 0;
    search_focus_results = false;
    search_rebuild(menu);
    search_sync_selection(menu);
}

void browser_search_close(void) {
    search_active = false;
    search_focus_results = false;
}

static void search_append_text(menu_t *menu, const char *text) {
    if (!text || text[0] == '\0') {
        return;
    }

    size_t len = strlen(search_query);
    size_t add_len = strlen(text);
    if ((len + add_len) >= sizeof(search_query)) {
        return;
    }

    memcpy(search_query + len, text, add_len + 1);
    search_rebuild(menu);
    search_sync_selection(menu);
}

static void search_backspace(menu_t *menu) {
    size_t len = strlen(search_query);
    if (len == 0) {
        return;
    }
    search_query[len - 1] = '\0';
    search_rebuild(menu);
    search_sync_selection(menu);
}

static void search_clear_query(menu_t *menu) {
    search_query[0] = '\0';
    search_rebuild(menu);
    search_sync_selection(menu);
}

static void search_activate_key(menu_t *menu) {
    const browser_search_key_t *key = &search_keyboard[search_key_row][search_key_col];
    if (!key->label || key->label[0] == '\0') {
        return;
    }
    if (strcmp(key->text, "\b") == 0) {
        search_backspace(menu);
    } else if (strcmp(key->text, "\f") == 0) {
        search_clear_query(menu);
    } else if (strcmp(key->text, "\r") == 0) {
        browser_search_close();
    } else {
        search_append_text(menu, key->text);
    }
}

bool browser_search_process(menu_t *menu) {
    if (!search_active) {
        return false;
    }

    if (menu->actions.settings) {
        browser_search_close();
        sound_play_effect(SFX_EXIT);
        return true;
    }

    if (menu->actions.back) {
        browser_search_close();
        sound_play_effect(SFX_EXIT);
        return true;
    }

    if (menu->actions.options) {
        search_focus_results = !search_focus_results;
        sound_play_effect(SFX_SETTING);
        return true;
    }

    if (search_focus_results) {
        if (menu->actions.go_up) {
            search_selected -= (menu->actions.go_fast ? 10 : 1);
            search_sync_selection(menu);
            sound_play_effect(SFX_CURSOR);
            return true;
        }
        if (menu->actions.go_down) {
            search_selected += (menu->actions.go_fast ? 10 : 1);
            search_sync_selection(menu);
            sound_play_effect(SFX_CURSOR);
            return true;
        }
        if (menu->actions.enter) {
            browser_search_close();
            sound_play_effect(SFX_ENTER);
            return false;
        }
        return true;
    }

    if (menu->actions.go_left) {
        search_key_col--;
        if (search_key_col < 0) {
            search_key_col = search_keyboard_row_lengths[search_key_row] - 1;
        }
        sound_play_effect(SFX_CURSOR);
        return true;
    }
    if (menu->actions.go_right) {
        search_key_col++;
        if (search_key_col >= search_keyboard_row_lengths[search_key_row]) {
            search_key_col = 0;
        }
        sound_play_effect(SFX_CURSOR);
        return true;
    }
    if (menu->actions.go_up) {
        search_key_row--;
        if (search_key_row < 0) {
            search_key_row = KEYBOARD_ROWS - 1;
        }
        if (search_key_col >= search_keyboard_row_lengths[search_key_row]) {
            search_key_col = search_keyboard_row_lengths[search_key_row] - 1;
        }
        sound_play_effect(SFX_CURSOR);
        return true;
    }
    if (menu->actions.go_down) {
        search_key_row++;
        if (search_key_row >= KEYBOARD_ROWS) {
            search_key_row = 0;
        }
        if (search_key_col >= search_keyboard_row_lengths[search_key_row]) {
            search_key_col = search_keyboard_row_lengths[search_key_row] - 1;
        }
        sound_play_effect(SFX_CURSOR);
        return true;
    }
    if (menu->actions.enter) {
        search_activate_key(menu);
        sound_play_effect(SFX_ENTER);
        return true;
    }

    return true;
}

void browser_search_draw(menu_t *menu) {
    (void)menu;
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
    char header_text[1536];
    char results_text[2048];
    size_t header_used = 0;
    size_t results_used = 0;
    ui_region_t header_region = {
        .x = BROWSER_SEARCH_HEADER_X0 + 12,
        .y = BROWSER_SEARCH_HEADER_Y0 + 8,
        .width = (BROWSER_SEARCH_HEADER_X1 - BROWSER_SEARCH_HEADER_X0) - 24,
        .height = (BROWSER_SEARCH_HEADER_Y1 - BROWSER_SEARCH_HEADER_Y0) - 16,
    };
    ui_region_t results_region = {
        .x = BROWSER_SEARCH_LIST_X,
        .y = BROWSER_SEARCH_LIST_Y0,
        .width = (BROWSER_SEARCH_RESULTS_X1 - BROWSER_SEARCH_RESULTS_X0) - 20,
        .height = BROWSER_SEARCH_LIST_Y1 - BROWSER_SEARCH_LIST_Y0,
    };

    ui_components_box_draw(BROWSER_SEARCH_HEADER_X0, BROWSER_SEARCH_HEADER_Y0, BROWSER_SEARCH_HEADER_X1, BROWSER_SEARCH_HEADER_Y1, RGBA32(0x08, 0x10, 0x1C, 0xC8));
    ui_components_border_draw(BROWSER_SEARCH_HEADER_X0, BROWSER_SEARCH_HEADER_Y0, BROWSER_SEARCH_HEADER_X1, BROWSER_SEARCH_HEADER_Y1);

    header_used += (size_t)snprintf(
        header_text + header_used,
        header_used < sizeof(header_text) ? sizeof(header_text) - header_used : 0,
        "^%02XSearch^00\n"
        "%s\n"
        "^%02X%d result%s^00  R: %s  B/Start: Close\n"
        "^%02X%s^00\n\n",
        STL_GREEN,
        search_query[0] != '\0' ? search_query : "Type to filter titles",
        search_match_count > 0 ? STL_DEFAULT : STL_ORANGE,
        search_match_count,
        search_match_count == 1 ? "" : "s",
        search_focus_results ? "Results" : "Keyboard",
        STL_YELLOW,
        search_focus_results ? "Results: Up/Down browse, A accept" : "Keyboard: D-pad move, A type"
    );

    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        int row_len = search_keyboard_row_lengths[row];
        for (int col = 0; col < row_len; col++) {
            const browser_search_key_t *key = &search_keyboard[row][col];
            bool selected = !search_focus_results && row == search_key_row && col == search_key_col;
            header_used += (size_t)snprintf(
                header_text + header_used,
                header_used < sizeof(header_text) ? sizeof(header_text) - header_used : 0,
                "%s^%02X%s^00",
                col == 0 ? "" : "  ",
                selected ? STL_YELLOW : STL_GRAY,
                key->label
            );
        }
        header_used += (size_t)snprintf(
            header_text + header_used,
            header_used < sizeof(header_text) ? sizeof(header_text) - header_used : 0,
            "\n"
        );
    }

    ui_components_text_draw_in_region(&header_region, STL_DEFAULT, "%s", header_text);

    ui_components_box_draw(BROWSER_SEARCH_RESULTS_X0, BROWSER_SEARCH_RESULTS_Y0, BROWSER_SEARCH_RESULTS_X1, BROWSER_SEARCH_RESULTS_Y1, RGBA32(0x05, 0x08, 0x12, 0xA8));
    ui_components_border_draw(BROWSER_SEARCH_RESULTS_X0, BROWSER_SEARCH_RESULTS_Y0, BROWSER_SEARCH_RESULTS_X1, BROWSER_SEARCH_RESULTS_Y1);
    rdpq_text_printf(
        &(rdpq_textparms_t){ .width = VISIBLE_AREA_WIDTH - 32, .height = 16, .wrap = WRAP_NONE },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + 16,
        BROWSER_SEARCH_RESULTS_Y0 + 4,
        "^%02XFiltered Results^00",
        STL_YELLOW
    );

    int visible_rows = (BROWSER_SEARCH_LIST_Y1 - BROWSER_SEARCH_LIST_Y0) / BROWSER_SEARCH_RESULT_ROW_H;
    if (visible_rows < 1) {
        visible_rows = 1;
    }

    if (search_match_count <= 0) {
        ui_components_text_draw_in_region(&results_region, STL_ORANGE, "No matching entries in this list.");
        return;
    }

    int start = 0;
    if (search_selected >= visible_rows / 2) {
        start = search_selected - (visible_rows / 2);
        if (start > search_match_count - visible_rows) {
            start = search_match_count - visible_rows;
        }
    }
    if (start < 0) {
        start = 0;
    }

    int end = start + visible_rows;
    if (end > search_match_count) {
        end = search_match_count;
    }

    for (int i = start; i < end; i++) {
        int source_index = search_matches[i];
        bool selected = (i == search_selected);
        results_used += (size_t)snprintf(
            results_text + results_used,
            results_used < sizeof(results_text) ? sizeof(results_text) - results_used : 0,
            "%s^%02X%s^00\n",
            selected ? "> " : "  ",
            selected ? STL_YELLOW : STL_DEFAULT,
            menu->browser.list[source_index].name ? menu->browser.list[source_index].name : "(unnamed)"
        );
    }

    ui_components_text_draw_in_region(&results_region, STL_DEFAULT, "%s", results_text);
}
