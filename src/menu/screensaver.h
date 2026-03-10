#ifndef MENU_SCREENSAVER_H__
#define MENU_SCREENSAVER_H__

#include <stdbool.h>

#include "menu_state.h"

void screensaver_logo_try_load(menu_t *menu);
void screensaver_logo_reload(menu_t *menu);
void screensaver_update_state(menu_t *menu);
void screensaver_apply_fps_limit(menu_t *menu);
bool screensaver_is_active(void);
void screensaver_draw(menu_t *menu, surface_t *display);
void screensaver_deinit(void);

#endif
