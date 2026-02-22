#include <libdragon.h>

#include "fonts.h"
#include "utils/fs.h"

typedef struct {
    color_t style[7];
} font_theme_t;

static rdpq_font_t *default_font = NULL;

static const font_theme_t font_themes[] = {
    {
        .style = {
            RGBA32(0xFF, 0xFF, 0xFF, 0xFF), // default
            RGBA32(0x70, 0xFF, 0x70, 0xFF), // green
            RGBA32(0x70, 0xBC, 0xFF, 0xFF), // blue
            RGBA32(0xFF, 0xFF, 0x70, 0xFF), // yellow
            RGBA32(0xFF, 0x99, 0x00, 0xFF), // orange
            RGBA32(0xFF, 0x40, 0x40, 0xFF), // red
            RGBA32(0xA0, 0xA0, 0xA0, 0xFF), // gray
        },
    },
    {
        // Solarized-inspired
        .style = {
            RGBA32(0xEE, 0xE8, 0xD5, 0xFF),
            RGBA32(0x85, 0x99, 0x00, 0xFF),
            RGBA32(0x26, 0x8B, 0xD2, 0xFF),
            RGBA32(0xB5, 0x89, 0x00, 0xFF),
            RGBA32(0xCB, 0x4B, 0x16, 0xFF),
            RGBA32(0xDC, 0x32, 0x2F, 0xFF),
            RGBA32(0x93, 0xA1, 0xA1, 0xFF),
        },
    },
    {
        // Gruvbox-inspired
        .style = {
            RGBA32(0xEB, 0xDB, 0xB2, 0xFF),
            RGBA32(0xB8, 0xBB, 0x26, 0xFF),
            RGBA32(0x83, 0xA5, 0x98, 0xFF),
            RGBA32(0xFA, 0xBD, 0x2F, 0xFF),
            RGBA32(0xFE, 0x80, 0x19, 0xFF),
            RGBA32(0xFB, 0x49, 0x34, 0xFF),
            RGBA32(0xA8, 0x99, 0x84, 0xFF),
        },
    },
    {
        // CRT Green
        .style = {
            RGBA32(0x9C, 0xFF, 0x9C, 0xFF),
            RGBA32(0x57, 0xFF, 0x57, 0xFF),
            RGBA32(0x7A, 0xFF, 0x7A, 0xFF),
            RGBA32(0xD0, 0xFF, 0x7A, 0xFF),
            RGBA32(0x9C, 0xFF, 0x57, 0xFF),
            RGBA32(0xFF, 0x7A, 0x7A, 0xFF),
            RGBA32(0x66, 0xAA, 0x66, 0xFF),
        },
    },
    {
        // Retrowave
        .style = {
            RGBA32(0xFF, 0xD6, 0xF3, 0xFF),
            RGBA32(0x6E, 0xFF, 0xB8, 0xFF),
            RGBA32(0x61, 0xDA, 0xFF, 0xFF),
            RGBA32(0xFF, 0xE0, 0x66, 0xFF),
            RGBA32(0xFF, 0x9D, 0x00, 0xFF),
            RGBA32(0xFF, 0x69, 0xA8, 0xFF),
            RGBA32(0xB6, 0x9A, 0xD8, 0xFF),
        },
    },
};

void fonts_set_theme(int theme_id) {
    if (!default_font) {
        return;
    }

    int max_theme = (int)(sizeof(font_themes) / sizeof(font_themes[0])) - 1;
    if (theme_id < 0 || theme_id > max_theme) {
        theme_id = 0;
    }

    for (int i = 0; i <= STL_GRAY; i++) {
        rdpq_font_style(default_font, i, &((rdpq_fontstyle_t){ .color = font_themes[theme_id].style[i] }));
    }
}

static void load_default_font (char *custom_font_path) {
    char *font_path = "rom:/Firple-Bold.font64";

    if (custom_font_path && file_exists(custom_font_path)) {
        font_path = custom_font_path;
    }

    default_font = rdpq_font_load(font_path);
    fonts_set_theme(0);

    rdpq_text_register_font(FNT_DEFAULT, default_font);
}


void fonts_init (char *custom_font_path) {
    load_default_font(custom_font_path);
}
