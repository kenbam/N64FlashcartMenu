#ifndef MENU_SCREENSAVER_GRADIENT_H__
#define MENU_SCREENSAVER_GRADIENT_H__

#include "menu_state.h"

#define SCREENSAVER_GRADIENT_POINT_COUNT (2)

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
    float radius;
    float phase;
} screensaver_gradient_point_t;

typedef struct {
    uint32_t rng;
    float time_s;
    screensaver_gradient_point_t points[SCREENSAVER_GRADIENT_POINT_COUNT];
} screensaver_gradient_state_t;

void screensaver_gradient_init_state(screensaver_gradient_state_t *state);
void screensaver_gradient_reset(screensaver_gradient_state_t *state);
void screensaver_gradient_activate(screensaver_gradient_state_t *state);
void screensaver_gradient_step(screensaver_gradient_state_t *state, float dt);
void screensaver_gradient_draw(surface_t *display, const screensaver_gradient_state_t *state);

#endif
