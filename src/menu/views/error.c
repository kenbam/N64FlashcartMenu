#include "views.h"
#include "../sound.h"
#include <stdio.h>
#include <string.h>

static char error_message_buffer[512];

static void process (menu_t *menu) {
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    if (menu->error_message) {
        ui_components_messagebox_draw(menu->error_message);
    } else {
        ui_components_messagebox_draw("Unspecified error");
    }

    rdpq_detach_show();
}

static void deinit (menu_t *menu) {
    menu->error_message = NULL;
    error_message_buffer[0] = '\0';
    menu->flashcart_err = FLASHCART_OK;
}


void view_error_init (menu_t *menu) {
    if (menu->flashcart_err != FLASHCART_OK) {
        debugf(
            "Flashcart error [%d]: %s\n",
            menu->flashcart_err,
            flashcart_convert_error_message(menu->flashcart_err)
        );
    }
}

void view_error_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);

    if (menu->next_mode != MENU_MODE_ERROR) {
        deinit(menu);
    }
}

void menu_show_error (menu_t *menu, char *error_message) {
    sound_play_effect(SFX_ERROR);
    menu->next_mode = MENU_MODE_ERROR;
    if (error_message && error_message[0] != '\0') {
        snprintf(error_message_buffer, sizeof(error_message_buffer), "%s", error_message);
        menu->error_message = error_message_buffer;
    } else {
        snprintf(error_message_buffer, sizeof(error_message_buffer), "%s", "Unspecified error");
        menu->error_message = error_message_buffer;
    }
}
