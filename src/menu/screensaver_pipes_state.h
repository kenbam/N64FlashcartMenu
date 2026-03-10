#ifndef MENU_SCREENSAVER_PIPES_STATE_H__
#define MENU_SCREENSAVER_PIPES_STATE_H__

#include "menu_state.h"

#define SCREENSAVER_PIPES_MAX_SEGMENTS (64)

typedef struct {
    uint8_t color_index;
    int ax;
    int ay;
    int az;
    int bx;
    int by;
    int bz;
} screensaver_pipe_segment_t;

typedef struct {
    uint32_t rng;
    int head_x;
    int head_y;
    int head_z;
    int dir;
    float segment_progress;
    float reset_delay_s;
    uint8_t active_color_index;
    uint8_t straight_run_remaining;
    uint32_t frame_tick;
    uint16_t traversed_cells;
    int segment_count;
    screensaver_pipe_segment_t segments[SCREENSAVER_PIPES_MAX_SEGMENTS];
} screensaver_pipes_state_t;

void screensaver_pipes_init_state(screensaver_pipes_state_t *state);
void screensaver_pipes_reset(screensaver_pipes_state_t *state);
void screensaver_pipes_activate(screensaver_pipes_state_t *state);
void screensaver_pipes_step(screensaver_pipes_state_t *state, float dt);

#endif
