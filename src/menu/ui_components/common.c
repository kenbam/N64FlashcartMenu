/**
 * @file common.c
 * @brief Common UI components implementation
 * @ingroup ui_components
 */

#include <stdarg.h>

#include "../ui_components.h"
#include "../fonts.h"
#include "constants.h"

static bool text_panel_enabled = true;
static uint8_t text_panel_alpha = 112;

typedef struct {
    color_t border;
    color_t progress_done;
    color_t progress_bg;
    color_t scrollbar_bg;
    color_t scrollbar_inactive;
    color_t scrollbar_position;
    color_t dialog_bg;
    color_t file_list_highlight;
    color_t context_menu_highlight;
    color_t tab_inactive_border;
    color_t tab_active_border;
    color_t tab_inactive_bg;
    color_t tab_active_bg;
} ui_theme_palette_t;

static const ui_theme_palette_t ui_theme_palettes[] = {
    {
        .border = BORDER_COLOR,
        .progress_done = PROGRESSBAR_DONE_COLOR,
        .progress_bg = PROGRESSBAR_BG_COLOR,
        .scrollbar_bg = SCROLLBAR_BG_COLOR,
        .scrollbar_inactive = SCROLLBAR_INACTIVE_COLOR,
        .scrollbar_position = SCROLLBAR_POSITION_COLOR,
        .dialog_bg = DIALOG_BG_COLOR,
        .file_list_highlight = FILE_LIST_HIGHLIGHT_COLOR,
        .context_menu_highlight = CONTEXT_MENU_HIGHLIGHT_COLOR,
        .tab_inactive_border = TAB_INACTIVE_BORDER_COLOR,
        .tab_active_border = TAB_ACTIVE_BORDER_COLOR,
        .tab_inactive_bg = TAB_INACTIVE_BACKGROUND_COLOR,
        .tab_active_bg = TAB_ACTIVE_BACKGROUND_COLOR,
    },
    {
        // Solarized Dark-inspired palette
        .border = RGBA32(0x93, 0xA1, 0xA1, 0xFF),
        .progress_done = RGBA32(0x2A, 0xA1, 0x98, 0xFF),
        .progress_bg = RGBA32(0x00, 0x2B, 0x36, 0xFF),
        .scrollbar_bg = RGBA32(0x07, 0x36, 0x42, 0xFF),
        .scrollbar_inactive = RGBA32(0x58, 0x6E, 0x75, 0xFF),
        .scrollbar_position = RGBA32(0x26, 0x8B, 0xD2, 0xFF),
        .dialog_bg = RGBA32(0x00, 0x2B, 0x36, 0xFF),
        .file_list_highlight = RGBA32(0x07, 0x36, 0x42, 0xFF),
        .context_menu_highlight = RGBA32(0x07, 0x36, 0x42, 0xFF),
        .tab_inactive_border = RGBA32(0x58, 0x6E, 0x75, 0xFF),
        .tab_active_border = RGBA32(0x93, 0xA1, 0xA1, 0xFF),
        .tab_inactive_bg = RGBA32(0x00, 0x2B, 0x36, 0xFF),
        .tab_active_bg = RGBA32(0x07, 0x36, 0x42, 0xFF),
    },
    {
        // Gruvbox dark-inspired palette
        .border = RGBA32(0xD5, 0xC4, 0xA1, 0xFF),
        .progress_done = RGBA32(0x98, 0x97, 0x1A, 0xFF),
        .progress_bg = RGBA32(0x28, 0x28, 0x28, 0xFF),
        .scrollbar_bg = RGBA32(0x3C, 0x38, 0x36, 0xFF),
        .scrollbar_inactive = RGBA32(0x50, 0x49, 0x45, 0xFF),
        .scrollbar_position = RGBA32(0xD7, 0x99, 0x21, 0xFF),
        .dialog_bg = RGBA32(0x1D, 0x20, 0x21, 0xFF),
        .file_list_highlight = RGBA32(0x45, 0x3B, 0x2C, 0xFF),
        .context_menu_highlight = RGBA32(0x45, 0x3B, 0x2C, 0xFF),
        .tab_inactive_border = RGBA32(0x66, 0x5C, 0x54, 0xFF),
        .tab_active_border = RGBA32(0xD5, 0xC4, 0xA1, 0xFF),
        .tab_inactive_bg = RGBA32(0x3C, 0x38, 0x36, 0xFF),
        .tab_active_bg = RGBA32(0x50, 0x49, 0x45, 0xFF),
    },
    {
        // CRT terminal-inspired green palette
        .border = RGBA32(0x9C, 0xFF, 0x9C, 0xFF),
        .progress_done = RGBA32(0x4D, 0xFF, 0x66, 0xFF),
        .progress_bg = RGBA32(0x00, 0x10, 0x00, 0xFF),
        .scrollbar_bg = RGBA32(0x00, 0x18, 0x00, 0xFF),
        .scrollbar_inactive = RGBA32(0x00, 0x26, 0x00, 0xFF),
        .scrollbar_position = RGBA32(0x57, 0xFF, 0x57, 0xFF),
        .dialog_bg = RGBA32(0x00, 0x08, 0x00, 0xFF),
        .file_list_highlight = RGBA32(0x00, 0x22, 0x00, 0xFF),
        .context_menu_highlight = RGBA32(0x00, 0x22, 0x00, 0xFF),
        .tab_inactive_border = RGBA32(0x33, 0x88, 0x33, 0xFF),
        .tab_active_border = RGBA32(0x9C, 0xFF, 0x9C, 0xFF),
        .tab_inactive_bg = RGBA32(0x00, 0x14, 0x00, 0xFF),
        .tab_active_bg = RGBA32(0x00, 0x2A, 0x00, 0xFF),
    },
    {
        // Retrowave-inspired neon palette
        .border = RGBA32(0xFF, 0x6B, 0xC8, 0xFF),
        .progress_done = RGBA32(0x00, 0xE5, 0xFF, 0xFF),
        .progress_bg = RGBA32(0x16, 0x08, 0x24, 0xFF),
        .scrollbar_bg = RGBA32(0x23, 0x10, 0x38, 0xFF),
        .scrollbar_inactive = RGBA32(0x2E, 0x18, 0x4A, 0xFF),
        .scrollbar_position = RGBA32(0xFF, 0x9D, 0x00, 0xFF),
        .dialog_bg = RGBA32(0x0F, 0x05, 0x19, 0xFF),
        .file_list_highlight = RGBA32(0x31, 0x16, 0x4E, 0xFF),
        .context_menu_highlight = RGBA32(0x31, 0x16, 0x4E, 0xFF),
        .tab_inactive_border = RGBA32(0x77, 0x32, 0xA6, 0xFF),
        .tab_active_border = RGBA32(0xFF, 0x6B, 0xC8, 0xFF),
        .tab_inactive_bg = RGBA32(0x23, 0x10, 0x38, 0xFF),
        .tab_active_bg = RGBA32(0x42, 0x1D, 0x66, 0xFF),
    },
};

static const char *ui_theme_names[] = {
    "Classic",
    "Solarized",
    "Gruvbox",
    "CRT Green",
    "Retrowave",
};

static int active_theme_id = 0;
static ui_theme_palette_t active_theme = {
    .border = BORDER_COLOR,
    .progress_done = PROGRESSBAR_DONE_COLOR,
    .progress_bg = PROGRESSBAR_BG_COLOR,
    .scrollbar_bg = SCROLLBAR_BG_COLOR,
    .scrollbar_inactive = SCROLLBAR_INACTIVE_COLOR,
    .scrollbar_position = SCROLLBAR_POSITION_COLOR,
    .dialog_bg = DIALOG_BG_COLOR,
    .file_list_highlight = FILE_LIST_HIGHLIGHT_COLOR,
    .context_menu_highlight = CONTEXT_MENU_HIGHLIGHT_COLOR,
    .tab_inactive_border = TAB_INACTIVE_BORDER_COLOR,
    .tab_active_border = TAB_ACTIVE_BORDER_COLOR,
    .tab_inactive_bg = TAB_INACTIVE_BACKGROUND_COLOR,
    .tab_active_bg = TAB_ACTIVE_BACKGROUND_COLOR,
};

void ui_components_set_theme(int theme_id) {
    int max_theme = (int)(sizeof(ui_theme_palettes) / sizeof(ui_theme_palettes[0])) - 1;
    if (theme_id < 0 || theme_id > max_theme) {
        theme_id = 0;
    }
    active_theme_id = theme_id;
    active_theme = ui_theme_palettes[theme_id];
    fonts_set_theme(theme_id);
}

int ui_components_get_theme(void) {
    return active_theme_id;
}

const char *ui_components_theme_name(int theme_id) {
    int max_theme = (int)(sizeof(ui_theme_names) / sizeof(ui_theme_names[0])) - 1;
    if (theme_id < 0 || theme_id > max_theme) {
        return ui_theme_names[0];
    }
    return ui_theme_names[theme_id];
}

int ui_components_theme_count(void) {
    return (int)(sizeof(ui_theme_names) / sizeof(ui_theme_names[0]));
}

color_t ui_components_file_list_highlight_color(void) {
    return active_theme.file_list_highlight;
}

color_t ui_components_context_menu_highlight_color(void) {
    return active_theme.context_menu_highlight;
}

/**
 * @brief Draw a box with the specified color.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 * @param color The color of the box.
 */
void ui_components_box_draw (int x0, int y0, int x1, int y1, color_t color) {
    rdpq_mode_push();
        rdpq_set_mode_fill(color);
        rdpq_fill_rectangle(x0, y0, x1, y1);
    rdpq_mode_pop();
}

/**
 * @brief Draw a border with the specified color.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 * @param color The color of the border.
 */
static void ui_components_border_draw_internal (int x0, int y0, int x1, int y1, color_t color) {
    rdpq_mode_push();
        rdpq_set_mode_fill(color);
        rdpq_fill_rectangle(x0 - BORDER_THICKNESS, y0 - BORDER_THICKNESS, x1 + BORDER_THICKNESS, y0);
        rdpq_fill_rectangle(x0 - BORDER_THICKNESS, y1, x1 + BORDER_THICKNESS, y1 + BORDER_THICKNESS);
        rdpq_fill_rectangle(x0 - BORDER_THICKNESS, y0, x0, y1);
        rdpq_fill_rectangle(x1, y0, x1 + BORDER_THICKNESS, y1);
    rdpq_mode_pop();
}

static void ui_components_text_panel_draw (int y0) {
    if (!text_panel_enabled || text_panel_alpha == 0) {
        return;
    }

    int x0 = VISIBLE_AREA_X0;
    int x1 = VISIBLE_AREA_X1;
    int y1 = LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS;
    if (y0 >= y1) {
        return;
    }

    // Fill mode ignores alpha blending. Use standard mode + blender for proper translucency.
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0x00, 0x00, 0x00, text_panel_alpha));
        rdpq_fill_rectangle(x0, y0, x1, y1);
    rdpq_mode_pop();
}

void ui_components_set_text_panel (bool enabled, uint8_t alpha) {
    text_panel_enabled = enabled;
    text_panel_alpha = alpha;
}

/**
 * @brief Draw a border with the default border color.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 */
void ui_components_border_draw (int x0, int y0, int x1, int y1) {
    ui_components_border_draw_internal(x0, y0, x1, y1, active_theme.border);
}

/**
 * @brief Draw the layout with tabs.
 */
void ui_components_layout_draw_tabbed (void) {
    ui_components_text_panel_draw(VISIBLE_AREA_Y0 + TAB_HEIGHT + BORDER_THICKNESS);

    ui_components_border_draw(
        VISIBLE_AREA_X0,
        VISIBLE_AREA_Y0 + TAB_HEIGHT + BORDER_THICKNESS,
        VISIBLE_AREA_X1,
        VISIBLE_AREA_Y1
    );

    ui_components_box_draw(
        VISIBLE_AREA_X0,
        LAYOUT_ACTIONS_SEPARATOR_Y,
        VISIBLE_AREA_X1,
        LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS,
        active_theme.border
    );
}

/**
 * @brief Draw the layout.
 */
void ui_components_layout_draw (void) {
    ui_components_text_panel_draw(VISIBLE_AREA_Y0 + BORDER_THICKNESS);

    ui_components_border_draw(
        VISIBLE_AREA_X0,
        VISIBLE_AREA_Y0,
        VISIBLE_AREA_X1,
        VISIBLE_AREA_Y1
    );
    ui_components_box_draw(
        VISIBLE_AREA_X0,
        LAYOUT_ACTIONS_SEPARATOR_Y,
        VISIBLE_AREA_X1,
        LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS,
        active_theme.border
    );
}

/**
 * @brief Draw a progress bar.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 * @param progress The progress value (0.0 to 1.0).
 */
void ui_components_progressbar_draw (int x0, int y0, int x1, int y1, float progress) {    
    float progress_width = progress * (x1 - x0);

    ui_components_box_draw(x0, y0, x0 + progress_width, y1, active_theme.progress_done);
    ui_components_box_draw(x0 + progress_width, y0, x1, y1, active_theme.progress_bg);
}

/**
 * @brief Draw a seek bar.
 * 
 * @param position The position value (0.0 to 1.0).
 */
void ui_components_seekbar_draw (float position) {
    int x0 = SEEKBAR_X;
    int y0 = SEEKBAR_Y;
    int x1 = SEEKBAR_X + SEEKBAR_WIDTH;
    int y1 = SEEKBAR_Y + SEEKBAR_HEIGHT;

    ui_components_border_draw(x0, y0, x1, y1);
    ui_components_progressbar_draw(x0, y0, x1, y1, position);
}

/**
 * @brief Draw a loader.
 * 
 * @param progress The progress value (0.0 to 1.0).
 * @param msg The message to display truncated to 30 characters.
 */
void ui_components_loader_draw (float progress, const char *msg) {
    int x0 = LOADER_X;
    int y0 = LOADER_Y;
    int x1 = LOADER_X + LOADER_WIDTH;
    int y1 = LOADER_Y + LOADER_HEIGHT;

    ui_components_border_draw(x0, y0, x1, y1);
    ui_components_progressbar_draw(x0, y0, x1, y1, progress);

    if (msg != NULL) {
        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_CENTER,
            "\n%.30s",
            msg
        );
    }
}

/**
 * @brief Draw a scrollbar.
 * 
 * @param x The x-coordinate of the top-left corner.
 * @param y The y-coordinate of the top-left corner.
 * @param width The width of the scrollbar.
 * @param height The height of the scrollbar.
 * @param position The current position.
 * @param items The total number of items.
 * @param visible_items The number of visible items.
 */
void ui_components_scrollbar_draw (int x, int y, int width, int height, int position, int items, int visible_items) {
    if (items <= 1 || items <= visible_items) {
        ui_components_box_draw(x, y, x + width, y + height, active_theme.scrollbar_inactive);
    } else {
        int scroll_height = (int) ((visible_items / (float) (items)) * height);
        float scroll_position = ((position / (float) (items - 1)) * (height - scroll_height));

        ui_components_box_draw(x, y, x + width, y + height, active_theme.scrollbar_bg);
        ui_components_box_draw(x, y + scroll_position, x + width, y + scroll_position + scroll_height, active_theme.scrollbar_position);
    }
}

/**
 * @brief Draw a list scrollbar.
 * 
 * @param position The current position.
 * @param items The total number of items.
 * @param visible_items The number of visible items.
 */
void ui_components_list_scrollbar_draw (int position, int items, int visible_items) {
    ui_components_scrollbar_draw(
        LIST_SCROLLBAR_X,
        LIST_SCROLLBAR_Y,
        LIST_SCROLLBAR_WIDTH,
        LIST_SCROLLBAR_HEIGHT,
        position,
        items,
        visible_items
    );
}

/**
 * @brief Draw a dialog box.
 * 
 * @param width The width of the dialog box.
 * @param height The height of the dialog box.
 */
void ui_components_dialog_draw (int width, int height) {
    int x0 = DISPLAY_CENTER_X - (width / 2);
    int y0 = DISPLAY_CENTER_Y - (height / 2);
    int x1 = DISPLAY_CENTER_X + (width / 2);
    int y1 = DISPLAY_CENTER_Y + (height / 2);

    ui_components_border_draw(x0, y0, x1, y1);
    ui_components_box_draw(x0, y0, x1, y1, active_theme.dialog_bg);
}

/**
 * @brief Draw a message box with formatted text.
 * 
 * @param fmt The format string.
 * @param ... The format arguments.
 */
void ui_components_messagebox_draw (char *fmt, ...) {
    char buffer[512];
    size_t nbytes = sizeof(buffer);

    va_list va;
    va_start(va, fmt);
    char *formatted = vasnprintf(buffer, &nbytes, fmt, va);
    va_end(va);

    int paragraph_nbytes = nbytes;

    rdpq_paragraph_t *paragraph = rdpq_paragraph_build(&(rdpq_textparms_t) {
        .width = MESSAGEBOX_MAX_WIDTH,
        .height = VISIBLE_AREA_HEIGHT,
        .align = ALIGN_CENTER,
        .valign = VALIGN_CENTER,
        .wrap = WRAP_WORD,
        .line_spacing = TEXT_LINE_SPACING_ADJUST,
    }, FNT_DEFAULT, formatted, &paragraph_nbytes);

    if (formatted != buffer) {
        free(formatted);
    }

    ui_components_dialog_draw(
        paragraph->bbox.x1 - paragraph->bbox.x0 + MESSAGEBOX_MARGIN,
        paragraph->bbox.y1 - paragraph->bbox.y0 + MESSAGEBOX_MARGIN
    );

    rdpq_paragraph_render(paragraph, DISPLAY_CENTER_X - (MESSAGEBOX_MAX_WIDTH / 2), VISIBLE_AREA_Y0);

    rdpq_paragraph_free(paragraph);
}

/**
 * @brief Draw the main text with formatted content.
 * 
 * @param style The font style.
 * @param align The horizontal alignment.
 * @param valign The vertical alignment.
 * @param fmt The format string.
 * @param ... The format arguments.
 */
void ui_components_main_text_draw (menu_font_type_t style, rdpq_align_t align, rdpq_valign_t valign, char *fmt, ...) {
    char buffer[1024];
    size_t nbytes = sizeof(buffer);

    va_list va;
    va_start(va, fmt);
    char *formatted = vasnprintf(buffer, &nbytes, fmt, va);
    va_end(va);

    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = style,
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - (TEXT_MARGIN_VERTICAL * 2),
            .align = align,
            .valign = valign,
            .wrap = WRAP_WORD,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL,
        formatted,
        nbytes
    );

    if (formatted != buffer) {
        free(formatted);
    }
}

/**
 * @brief Draw the actions bar text with formatted content.
 * 
 * @param style The font style.
 * @param align The horizontal alignment.
 * @param valign The vertical alignment.
 * @param fmt The format string.
 * @param ... The format arguments.
 */
void ui_components_actions_bar_text_draw (menu_font_type_t style, rdpq_align_t align, rdpq_valign_t valign, char *fmt, ...) {
    char buffer[256];
    size_t nbytes = sizeof(buffer);

    va_list va;
    va_start(va, fmt);
    char *formatted = vasnprintf(buffer, &nbytes, fmt, va);
    va_end(va);

    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = style,
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = VISIBLE_AREA_Y1 - LAYOUT_ACTIONS_SEPARATOR_Y - BORDER_THICKNESS - (TEXT_MARGIN_VERTICAL * 2),
            .align = align,
            .valign = valign,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL,
        formatted,
        nbytes
    );

    if (formatted != buffer) {
        free(formatted);
    }
}

/**
 * @brief Draw the tabs.
 * 
 * @param text Array of tab text.
 * @param count Number of tabs.
 * @param selected Index of the selected tab.
 * @param width Width of each tab.
 */
void ui_components_tabs_draw(const char **text, int count, int selected, float width ) {
    float starting_x = VISIBLE_AREA_X0;

    float x = starting_x;
    float y = OVERSCAN_HEIGHT;    
    float height = TAB_HEIGHT;

    // first draw the tabs that are not selected
    for(int i=0;i< count;i++) {
        if(i != selected) {
            ui_components_box_draw(
                x,
                y,
                x + width,
                y + height,
                active_theme.tab_inactive_bg
            );

            ui_components_border_draw_internal(
                x,
                y,
                x + width,
                y + height,
                active_theme.tab_inactive_border
            );
        }
        x += width;
    }
    
    // draw the selected tab (so it shows up on top of the others)
    if(selected >= 0 && selected < count) {
        x = starting_x + (width * selected);

        ui_components_box_draw(
            x,
            y,
            x + width,
            y + height,
            active_theme.tab_active_bg
        );

        ui_components_border_draw_internal(
            x,
            y,
            x + width,
            y + height,
            active_theme.tab_active_border
        );
    }

    // write the text on the tabs
    rdpq_textparms_t tab_textparms = {
        .width = width,
        .height = 24,
        .align = ALIGN_CENTER,
        .wrap = WRAP_NONE
    };
    x = starting_x;
    for(int i=0;i< count;i++) {
        rdpq_text_print(
            &tab_textparms,
            FNT_DEFAULT,
            x,
            y,
            text[i]
        );
        x += width;
    }
}

void ui_component_value_editor(const char **header_text, const char **value_text, int count, int selected, float width_adjustment ) {
    float field_width = (VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2)) / width_adjustment;
    float starting_x = DISPLAY_CENTER_X - (field_width * count / 2.0f);

    float x = starting_x;
    float y = DISPLAY_CENTER_Y;    
    float height = TAB_HEIGHT;

    // first draw the values that are not selected
    for(int i=0;i< count;i++) {
        if(i != selected) {
            ui_components_box_draw(
                x,
                y,
                x + field_width,
                y + height + 24,
                active_theme.tab_inactive_bg
            );
        }
        x += field_width;
    }
    
    // draw the selected value (so it shows up on top of the others)
    if(selected >= 0 && selected < count) {
        x = starting_x + (field_width * selected);

        ui_components_box_draw(
            x,
            y,
            x + field_width,
            y + height + 24,
            active_theme.tab_active_bg
        );
    }

    // write the text on the value boxes
    rdpq_textparms_t value_textparms = {
        .width = field_width,
        .height = 24,
        .align = ALIGN_CENTER,
        .wrap = WRAP_NONE
    };
    x = starting_x;
    for(int i=0;i< count;i++) {
        rdpq_text_print(
            &value_textparms,
            FNT_DEFAULT,
            x,
            y,
            header_text[i]
        );

        rdpq_text_print(
            &value_textparms,
            FNT_DEFAULT,
            x,
            y + 24,
            value_text[i]
        );
        x += field_width;
    }

    // draw the border around the value boxes
    ui_components_border_draw (starting_x, y, x, y + height + 24);
}
