#ifndef MENU_SCREENSAVER_MYSTIFY_GL_H__
#define MENU_SCREENSAVER_MYSTIFY_GL_H__

#include <libdragon.h>

#define SCREENSAVER_MYSTIFY_VERTEX_COUNT (4)
#define SCREENSAVER_MYSTIFY_TRAIL_COUNT  (14)

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
} screensaver_mystify_vertex_t;

typedef struct {
    float x;
    float y;
} screensaver_mystify_point_t;

typedef struct {
    uint32_t rng;
    float time_s;
    int history_count;
    screensaver_mystify_vertex_t vertices[SCREENSAVER_MYSTIFY_VERTEX_COUNT];
    screensaver_mystify_point_t history[SCREENSAVER_MYSTIFY_TRAIL_COUNT][SCREENSAVER_MYSTIFY_VERTEX_COUNT];
} screensaver_mystify_state_t;

void screensaver_mystify_reset(screensaver_mystify_state_t *state);
void screensaver_mystify_activate(screensaver_mystify_state_t *state);
void screensaver_mystify_step(screensaver_mystify_state_t *state, float dt);
void screensaver_mystify_draw(surface_t *display, const screensaver_mystify_state_t *state);

#endif
