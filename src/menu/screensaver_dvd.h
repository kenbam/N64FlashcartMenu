#ifndef MENU_SCREENSAVER_DVD_H__
#define MENU_SCREENSAVER_DVD_H__

#include "menu_state.h"

typedef struct {
    float prev_x;
    float prev_y;
    float x;
    float y;
    float vx;
    float vy;
    float accumulator_s;
    uint8_t color_index;
    uint32_t rng;
    surface_t *logo_image;
    bool logo_loading;
    bool logo_search_done;
} screensaver_dvd_state_t;

void screensaver_dvd_init_state(screensaver_dvd_state_t *state);
void screensaver_dvd_reset(screensaver_dvd_state_t *state);
void screensaver_dvd_deinit(screensaver_dvd_state_t *state);
void screensaver_dvd_logo_try_load(menu_t *menu, screensaver_dvd_state_t *state);
void screensaver_dvd_logo_reload(menu_t *menu, screensaver_dvd_state_t *state);
void screensaver_dvd_activate(menu_t *menu, screensaver_dvd_state_t *state);
void screensaver_dvd_draw(menu_t *menu, screensaver_dvd_state_t *state, surface_t *display, float dt);

#endif
