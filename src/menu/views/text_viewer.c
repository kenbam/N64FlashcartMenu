/**
 * @file text_viewer.c
 * @brief Text Viewer component implementation
 * @ingroup ui_components
 */

#include <stdio.h>
#include <sys/stat.h>

#include "../ui_components/constants.h"
#include "../fonts.h"
#include "../sound.h"
#include "utils/utils.h"
#include "views.h"

#define MAX_FILE_SIZE KiB(128)
#define TEXT_VIEWER_CONTENT_WIDTH   (VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2))
#define TEXT_VIEWER_CONTENT_HEIGHT  (LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - (TEXT_MARGIN_VERTICAL * 2))
#define TEXT_VIEWER_LAYOUT_HEIGHT   (4096)
#define TEXT_VIEWER_SCROLL_STEP     (TEXT_VIEWER_CONTENT_HEIGHT / LIST_ENTRIES)

/** @brief Text file structure */
typedef struct {
    FILE *f; /**< File pointer */
    char *contents; /**< File contents */
    size_t length; /**< File length */
    int lines; /**< Approximate number of wrapped display lines */
    int current_line; /**< Current wrapped display line */
    int offset; /**< Pixel offset in the rendered paragraph */
    int max_offset; /**< Maximum pixel offset */
    bool vertical_scroll_possible; /**< Flag indicating if vertical scroll is possible */
    rdpq_paragraph_t *paragraph; /**< Prebuilt wrapped paragraph layout */
} text_file_t;

static text_file_t *text;

static void recalculate_scroll_state (void) {
    if (!text || !text->paragraph) {
        return;
    }

    int total_height = text->paragraph->bbox.y1 - text->paragraph->bbox.y0;
    if (total_height < TEXT_VIEWER_CONTENT_HEIGHT) {
        total_height = TEXT_VIEWER_CONTENT_HEIGHT;
    }

    text->max_offset = total_height - TEXT_VIEWER_CONTENT_HEIGHT;
    if (text->max_offset < 0) {
        text->max_offset = 0;
    }

    int step = TEXT_VIEWER_SCROLL_STEP > 0 ? TEXT_VIEWER_SCROLL_STEP : 1;
    text->lines = (total_height + step - 1) / step;
    if (text->lines < 1) {
        text->lines = 1;
    }
    text->vertical_scroll_possible = (text->max_offset > 0);

    if (text->offset < 0) {
        text->offset = 0;
    }
    if (text->offset > text->max_offset) {
        text->offset = text->max_offset;
    }
    text->current_line = text->offset / step;
}

/**
 * @brief Perform vertical scroll in the text file.
 * 
 * @param lines Number of wrapped display lines to scroll.
 */
static void perform_vertical_scroll (int lines) {
    if (!text || !text->vertical_scroll_possible) {
        return;
    }

    int step = TEXT_VIEWER_SCROLL_STEP > 0 ? TEXT_VIEWER_SCROLL_STEP : 1;
    int next_offset = text->offset + (lines * step);
    if (next_offset < 0) {
        next_offset = 0;
    }
    if (next_offset > text->max_offset) {
        next_offset = text->max_offset;
    }

    text->offset = next_offset;
    text->current_line = text->offset / step;
}

/**
 * @brief Process user actions for the text viewer.
 * 
 * @param menu Pointer to the menu structure.
 */
static void process (menu_t *menu) {
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    } else if (text) {
        if (menu->actions.go_up) {
            perform_vertical_scroll(menu->actions.go_fast ? -10 : -1);
        } else if (menu->actions.go_down) {
            perform_vertical_scroll(menu->actions.go_fast ? 10 : 1);
        }
    }
}

/**
 * @brief Draw the text viewer.
 * 
 * @param menu Pointer to the menu structure.
 * @param d Pointer to the display surface.
 */
static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    ui_components_layout_draw();

    if (text && text->paragraph) {
        int x = VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL;
        int y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL;
        int clip_x1 = x + TEXT_VIEWER_CONTENT_WIDTH;
        int clip_y1 = y + TEXT_VIEWER_CONTENT_HEIGHT;

        rdpq_set_scissor(x, y, clip_x1, clip_y1);
        rdpq_paragraph_render(text->paragraph, x, y - text->paragraph->bbox.y0 - text->offset);
        rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
    }

    ui_components_list_scrollbar_draw(text->current_line, text->lines, LIST_ENTRIES);

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "^%02XUp / Down: Scroll^00\n"
        "B: Back",
        text->vertical_scroll_possible ? STL_DEFAULT : STL_GRAY
    );

    rdpq_detach_show();
}

/**
 * @brief Deinitialize the text viewer.
 */
static void deinit (void) {
    if (text) {
        if (text->f) {
            fclose(text->f);
        }
        if (text->paragraph) {
            rdpq_paragraph_free(text->paragraph);
        }
        if (text->contents) {
            free(text->contents);
        }
        free(text);
        text = NULL;
    }
}

/**
 * @brief Initialize the text viewer.
 * 
 * @param menu Pointer to the menu structure.
 */
void view_text_viewer_init (menu_t *menu) {
    if ((text = calloc(1, sizeof(text_file_t))) == NULL) {
        return menu_show_error(menu, "Couldn't allocate memory for the text file");
    }

    path_t *path = NULL;
    if (menu->browser.entry && menu->browser.entry->path) {
        path = path_create(menu->browser.entry->path);
    } else {
        path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    }
    text->f = fopen(path_get(path), "r");
    path_free(path);

    if (text->f == NULL) {
        deinit();
        return menu_show_error(menu, "Couldn't open text file");
    }

    struct stat st;
    if (fstat(fileno(text->f), &st)) {
        deinit();
        return menu_show_error(menu, "Couldn't get text file size");
    }
    text->length = st.st_size;

    if (text->length <= 0) {
        deinit();
        return menu_show_error(menu, "Text file is empty");
    }

    if (text->length > MAX_FILE_SIZE) {
        deinit();
        return menu_show_error(menu, "Text file is too big to be displayed");
    }

    if ((text->contents = malloc((text->length + 1) * sizeof(char))) == NULL) {
        deinit();
        return menu_show_error(menu, "Couldn't allocate memory for the text file contents");
    }

    if (fread(text->contents, text->length, 1, text->f) != 1) {
        deinit();
        return menu_show_error(menu, "Couldn't read text file contents");
    }
    text->contents[text->length] = '\0';

    if (fclose(text->f)) {
        deinit();
        return menu_show_error(menu, "Couldn't close text file");
    }
    text->f = NULL;

    int paragraph_nbytes = (int)text->length;
    text->paragraph = rdpq_paragraph_build(&(rdpq_textparms_t) {
        .style_id = STL_DEFAULT,
        .width = TEXT_VIEWER_CONTENT_WIDTH,
        .height = TEXT_VIEWER_LAYOUT_HEIGHT,
        .align = ALIGN_LEFT,
        .valign = VALIGN_TOP,
        .wrap = WRAP_WORD,
        .line_spacing = TEXT_LINE_SPACING_ADJUST,
    }, FNT_DEFAULT, text->contents, &paragraph_nbytes);

    if (!text->paragraph) {
        deinit();
        return menu_show_error(menu, "Couldn't build text layout");
    }

    recalculate_scroll_state();
}

/**
 * @brief Display the text viewer.
 * 
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_text_viewer_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);

    if (menu->next_mode != MENU_MODE_TEXT_VIEWER) {
        deinit();
    }
}
