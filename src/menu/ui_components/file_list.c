/**
 * @file file_list.c
 * @brief Implementation of the file list UI component.
 * @ingroup ui_components
 */

#include <stdlib.h>

#include "../ui_components.h"
#include "../fonts.h"
#include "constants.h"

/**
 * @brief Icon string for directory entries in the file list.
 */
static const char *directory_icon = "[DIR] ";
// static const char *archive_icon = "[Zip] ";
// static const char *rom_icon = "[Rom] ";
// static const char *disk_icon = "[Disk] ";
// static const char *music_icon = "[Mp3] ";
// static const char *text_icon = "[Txt] ";
// static const char *image_icon = "[Png] ";
// static const char *save_icon = "[Save] ";
// static const char *other_icon = "[?] ";

static bool selected_row_shimmer_enabled = true;
static playtime_db_t *file_list_playtime_db = NULL;
static time_t file_list_now = 0;
static int file_list_top_inset = 0;

static menu_font_style_t selected_shimmer_style(void) {
    static const menu_font_style_t cycle[] = {
        STL_RED, STL_ORANGE, STL_YELLOW, STL_GREEN, STL_BLUE, STL_DEFAULT
    };
    static uint32_t tick = 0;
    if (!selected_row_shimmer_enabled) {
        return STL_DEFAULT;
    }
    tick++;
    return cycle[(tick / 4) % (sizeof(cycle) / sizeof(cycle[0]))];
}

void ui_components_set_selected_row_shimmer(bool enabled) {
    selected_row_shimmer_enabled = enabled;
}

void ui_components_set_file_list_last_played_context(playtime_db_t *db, time_t now) {
    file_list_playtime_db = db;
    file_list_now = now;
}

void ui_components_set_file_list_top_inset(int inset_pixels) {
    if (inset_pixels < 0) {
        inset_pixels = 0;
    }
    if (inset_pixels > 120) {
        inset_pixels = 120;
    }
    file_list_top_inset = inset_pixels;
}

/**
 * @brief Format the file size into a human-readable string.
 *
 * @param buffer Buffer to store the formatted string.
 * @param size Size of the file in bytes.
 * @return Number of characters written to the buffer.
 */
static int format_file_size(char *buffer, int64_t size) {
    if (size < 0) {
        return sprintf(buffer, "unknown");
    } else if (size == 0) {
        return sprintf(buffer, "empty");
    } else if (size < 8 * 1024) {
        return sprintf(buffer, "%lld B", size);
    } else if (size < 4 * 1024 * 1024) {
        return sprintf(buffer, "%lld kB", size / 1024);
    } else if (size < 1 * 1024 * 1024 * 1024) {
        return sprintf(buffer, "%lld MB", size / 1024 / 1024);
    } else {
        return sprintf(buffer, "%lld GB", size / 1024 / 1024 / 1024);
    }
}

static int format_last_played(char *buffer, const entry_t *entry) {
    if (!buffer || !entry) {
        return sprintf(buffer, "--");
    }

    if (entry->type != ENTRY_TYPE_ROM && entry->type != ENTRY_TYPE_DISK && entry->type != ENTRY_TYPE_EMULATOR) {
        return sprintf(buffer, "--");
    }

    if (entry->last_played <= 0) {
        buffer[0] = '\0';
        return 0;
    }
    if (file_list_now <= 0) {
        return sprintf(buffer, "Played");
    }

    int64_t delta = (int64_t)file_list_now - (int64_t)entry->last_played;
    if (delta < 0) {
        delta = 0;
    }
    if (delta < 86400) {
        return sprintf(buffer, "Today");
    }
    if (delta < (86400 * 7)) {
        return sprintf(buffer, "%lldd ago", (long long)(delta / 86400));
    }
    if (delta < (86400 * 30)) {
        return sprintf(buffer, "%lldw ago", (long long)(delta / (86400 * 7)));
    }
    return sprintf(buffer, "%lldmo ago", (long long)(delta / (86400 * 30)));
}

/**
 * @brief Draw the file list UI component.
 *
 * @param list Pointer to the list of file entries.
 * @param entries Number of entries in the list.
 * @param selected Index of the currently selected entry.
 */
void ui_components_file_list_draw(entry_t *list, int entries, int selected) {
    menu_font_style_t shimmer_style = selected_shimmer_style();

    const int nominal_row_height = 19;
    int starting_position = 0;
    int list_x = VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL;
    int list_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + file_list_top_inset;
    int list_bottom = LAYOUT_ACTIONS_SEPARATOR_Y - TEXT_MARGIN_VERTICAL;
    int list_height = list_bottom - list_y;
    if (list_height < 0) {
        list_height = 0;
    }

    if (selected < 0) {
        selected = 0;
    } else if (selected >= entries && entries > 0) {
        selected = entries - 1;
    }

    int max_visible_entries = list_height / nominal_row_height;
    if (max_visible_entries < 1) {
        max_visible_entries = 1;
    } else if (max_visible_entries > LIST_ENTRIES) {
        max_visible_entries = LIST_ENTRIES;
    }

    if (entries > max_visible_entries && selected >= (max_visible_entries / 2)) {
        starting_position = selected - (max_visible_entries / 2);
        if (starting_position >= entries - max_visible_entries) {
            starting_position = entries - max_visible_entries;
        }
    }

    int visible_entries = entries - starting_position;
    if (visible_entries > max_visible_entries) {
        visible_entries = max_visible_entries;
    }
    if (visible_entries < 0) {
        visible_entries = 0;
    }

    ui_components_list_scrollbar_draw(selected, entries, max_visible_entries);

    if (entries == 0) {
        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n"
            "^%02X** empty directory **",
            STL_GRAY
        );
    } else {
        rdpq_paragraph_t *file_list_layout;
        rdpq_paragraph_t *layout;

        size_t name_lengths[LIST_ENTRIES];
        size_t total_length = 1;

        for (int i = 0; i < LIST_ENTRIES; i++) {
            int entry_index = starting_position + i;

            if (i >= visible_entries || entry_index >= entries) {
                name_lengths[i] = 0;
            } else {
                size_t length = strlen(list[entry_index].name);
                name_lengths[i] = length;
                total_length += length;
            }
        }

        file_list_layout = malloc(sizeof(rdpq_paragraph_t) + (sizeof(rdpq_paragraph_char_t) * total_length));
        memset(file_list_layout, 0, sizeof(rdpq_paragraph_t));
        file_list_layout->capacity = total_length;

        rdpq_paragraph_builder_begin(
            &(rdpq_textparms_t) {
                .width = FILE_LIST_MAX_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
                .height = list_height,
                .wrap = WRAP_ELLIPSES,
                .line_spacing = TEXT_LINE_SPACING_ADJUST,
            },
            FNT_DEFAULT,
            file_list_layout
        );

        for (int i = 0; i < visible_entries; i++) {
            int entry_index = starting_position + i;
            entry_t *entry = &list[entry_index];

            menu_font_style_t style;

            switch (entry->type) {
                case ENTRY_TYPE_DIR: style = STL_YELLOW; break;
                case ENTRY_TYPE_ROM: style = STL_DEFAULT; break;
                case ENTRY_TYPE_DISK: style = STL_DEFAULT; break;
                case ENTRY_TYPE_EMULATOR: style = STL_DEFAULT; break;
                case ENTRY_TYPE_SAVE: style = STL_GREEN; break;
                case ENTRY_TYPE_IMAGE: style = STL_BLUE; break;
                case ENTRY_TYPE_MUSIC: style = STL_BLUE; break;
                case ENTRY_TYPE_TEXT: style = STL_ORANGE; break;
                case ENTRY_TYPE_PLAYLIST: style = STL_ORANGE; break;
                case ENTRY_TYPE_OTHER: style = STL_GRAY; break;
                case ENTRY_TYPE_ARCHIVE: style = STL_ORANGE; break;
                case ENTRY_TYPE_ARCHIVED: style = STL_DEFAULT; break;
                default: style = STL_GRAY; break;
            }

            if (entry_index == selected) {
                rdpq_paragraph_builder_style(shimmer_style);
            } else {
                rdpq_paragraph_builder_style(style);
            }

            rdpq_paragraph_builder_span(entry->name, name_lengths[i]);

            if ((i + 1) >= visible_entries) {
                break;
            }

            rdpq_paragraph_builder_newline();
        }

        layout = rdpq_paragraph_builder_end();

        int lines = layout->nlines > 0 ? layout->nlines : visible_entries;
        if (lines < 1) {
            lines = 1;
        }
        int highlight_height = (layout->bbox.y1 - layout->bbox.y0) / lines;
        if (highlight_height < 1) {
            highlight_height = nominal_row_height;
        }
        int selected_row = selected - starting_position;
        if (selected_row < 0) {
            selected_row = 0;
        } else if (selected_row >= lines) {
            selected_row = lines - 1;
        }
        int highlight_y = list_y + (selected_row * highlight_height);

        rdpq_set_scissor(list_x, list_y, VISIBLE_AREA_X1 - TEXT_MARGIN_HORIZONTAL, list_bottom);
        ui_components_box_draw(
            FILE_LIST_HIGHLIGHT_X,
            highlight_y,
            FILE_LIST_HIGHLIGHT_X + FILE_LIST_HIGHLIGHT_WIDTH,
            highlight_y + highlight_height,
            ui_components_file_list_highlight_color()
        );

        rdpq_paragraph_render(
            layout,
            list_x,
            list_y
        );

        rdpq_paragraph_free(layout);

        rdpq_paragraph_builder_begin(
            &(rdpq_textparms_t) {
                .width = VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
                .height = list_height,
                .align = ALIGN_RIGHT,
                .wrap = WRAP_ELLIPSES,
                .line_spacing = TEXT_LINE_SPACING_ADJUST,
            },
            FNT_DEFAULT,
            NULL
        );

        char second_col[20];

        for (int i = 0; i < visible_entries; i++) {
            int entry_index = starting_position + i;
            entry_t *entry = &list[entry_index];

            if (entry_index == selected) {
                rdpq_paragraph_builder_style(shimmer_style);
            } else {
                rdpq_paragraph_builder_style(STL_DEFAULT);
            }

            if (entry->type != ENTRY_TYPE_DIR) {
                // Playlists typically have unknown entry sizes; last-played is higher-value.
                rdpq_paragraph_builder_span(second_col, format_last_played(second_col, entry));
            }
            else {
                rdpq_paragraph_builder_span(directory_icon, 5);
            }

            if ((i + 1) >= visible_entries) {
                break;
            }

            rdpq_paragraph_builder_newline();
        }

        layout = rdpq_paragraph_builder_end();

        rdpq_paragraph_render(layout, list_x, list_y);
        rdpq_set_scissor(0, 0, display_get_width(), display_get_height());

        rdpq_paragraph_free(layout);
    }
}
