#ifndef MENU_SCREENSAVER_ATTRACT_H__
#define MENU_SCREENSAVER_ATTRACT_H__

#include "menu_state.h"
#include "rom_info.h"
#include "ui_components.h"

typedef struct {
    uint32_t rng;
    float featured_time_s;
    bool scan_started;
    bool scan_complete;
    bool pool_finalized;
    int finalize_index;
    int finalize_kept_count;
    char **finalize_paths;
    char (*finalize_keys)[96];
    int *finalize_ranks;
    bool *finalize_deduped;
    uint32_t scanned_game_count;
    int current_index;
    int current_shuffle_pos;
    int pool_count;
    int pool_capacity;
    char **pool;
    int *shuffle_order;
    int scan_stack_count;
    int scan_stack_capacity;
    char **scan_stack;
    rom_info_t current_rom_info;
    component_boxart_t *boxart;
    sprite_t *prompt_icon;
    bool prompt_icon_attempted;
} screensaver_attract_state_t;

void screensaver_attract_init_state(screensaver_attract_state_t *state);
void screensaver_attract_reset(screensaver_attract_state_t *state);
void screensaver_attract_deinit(screensaver_attract_state_t *state);
void screensaver_attract_activate(menu_t *menu, screensaver_attract_state_t *state);
void screensaver_attract_step(menu_t *menu, screensaver_attract_state_t *state, float dt);
void screensaver_attract_draw(menu_t *menu, surface_t *display, screensaver_attract_state_t *state);
bool screensaver_attract_open_current(menu_t *menu, screensaver_attract_state_t *state);
bool screensaver_attract_cycle_current(menu_t *menu, screensaver_attract_state_t *state, int direction);

#endif
