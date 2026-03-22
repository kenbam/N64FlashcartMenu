#include <math.h>

#include <GL/gl.h>
#include <GL/gl_integration.h>
#include <libdragon.h>

#include "screensaver_mystify_gl.h"

#define MYSTIFY_MIN_X         (0.08f)
#define MYSTIFY_MAX_X         (0.92f)
#define MYSTIFY_MIN_Y         (0.10f)
#define MYSTIFY_MAX_Y         (0.90f)
#define MYSTIFY_MIN_SPEED     (0.16f)
#define MYSTIFY_MAX_SPEED     (0.36f)
#define MYSTIFY_BASE_ALPHA    (0.10f)
#define MYSTIFY_HEAD_ALPHA    (0.80f)

typedef struct {
    float r;
    float g;
    float b;
} mystify_rgb_t;

static float mystify_clampf(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static uint32_t mystify_rand_u32(screensaver_mystify_state_t *state) {
    if (state->rng == 0) {
        state->rng = (uint32_t)get_ticks_us() ^ 0x9E3779B9u;
    }
    state->rng = (state->rng * 1664525u) + 1013904223u;
    return state->rng;
}

static float mystify_rand_unit(screensaver_mystify_state_t *state) {
    return (float)(mystify_rand_u32(state) & 0xFFFFu) / 65535.0f;
}

static float mystify_rand_range(screensaver_mystify_state_t *state, float min_value, float max_value) {
    return min_value + ((max_value - min_value) * mystify_rand_unit(state));
}

static mystify_rgb_t mystify_hsv_to_rgb(float h, float s, float v) {
    float hue = h - floorf(h);
    float scaled = hue * 6.0f;
    int sector = (int)scaled;
    float f = scaled - (float)sector;
    float p = v * (1.0f - s);
    float q = v * (1.0f - (s * f));
    float t = v * (1.0f - (s * (1.0f - f)));

    switch (sector % 6) {
        case 0: return (mystify_rgb_t){ v, t, p };
        case 1: return (mystify_rgb_t){ q, v, p };
        case 2: return (mystify_rgb_t){ p, v, t };
        case 3: return (mystify_rgb_t){ p, q, v };
        case 4: return (mystify_rgb_t){ t, p, v };
        case 5:
        default: return (mystify_rgb_t){ v, p, q };
    }
}

static void mystify_randomize_vertices(screensaver_mystify_state_t *state) {
    for (int i = 0; i < SCREENSAVER_MYSTIFY_VERTEX_COUNT; i++) {
        screensaver_mystify_vertex_t *vertex = &state->vertices[i];
        vertex->x = mystify_rand_range(state, MYSTIFY_MIN_X, MYSTIFY_MAX_X);
        vertex->y = mystify_rand_range(state, MYSTIFY_MIN_Y, MYSTIFY_MAX_Y);
        vertex->vx = mystify_rand_range(state, MYSTIFY_MIN_SPEED, MYSTIFY_MAX_SPEED);
        vertex->vy = mystify_rand_range(state, MYSTIFY_MIN_SPEED, MYSTIFY_MAX_SPEED);
        if (mystify_rand_u32(state) & 1u) {
            vertex->vx = -vertex->vx;
        }
        if (mystify_rand_u32(state) & 1u) {
            vertex->vy = -vertex->vy;
        }
    }
}

static void mystify_push_history(screensaver_mystify_state_t *state) {
    for (int i = SCREENSAVER_MYSTIFY_TRAIL_COUNT - 1; i > 0; i--) {
        for (int v = 0; v < SCREENSAVER_MYSTIFY_VERTEX_COUNT; v++) {
            state->history[i][v] = state->history[i - 1][v];
        }
    }
    for (int v = 0; v < SCREENSAVER_MYSTIFY_VERTEX_COUNT; v++) {
        state->history[0][v].x = state->vertices[v].x;
        state->history[0][v].y = state->vertices[v].y;
    }
    if (state->history_count < SCREENSAVER_MYSTIFY_TRAIL_COUNT) {
        state->history_count++;
    }
}

void screensaver_mystify_reset(screensaver_mystify_state_t *state) {
    if (!state) {
        return;
    }
    state->time_s = 0.0f;
    state->history_count = 0;
}

void screensaver_mystify_activate(screensaver_mystify_state_t *state) {
    if (!state) {
        return;
    }

    state->time_s = 0.0f;
    state->history_count = 0;
    mystify_randomize_vertices(state);
    for (int i = 0; i < SCREENSAVER_MYSTIFY_TRAIL_COUNT; i++) {
        mystify_push_history(state);
    }
}

void screensaver_mystify_step(screensaver_mystify_state_t *state, float dt) {
    if (!state) {
        return;
    }

    float clamped_dt = mystify_clampf(dt, 0.0f, 0.05f);
    state->time_s += clamped_dt;

    for (int i = 0; i < SCREENSAVER_MYSTIFY_VERTEX_COUNT; i++) {
        screensaver_mystify_vertex_t *vertex = &state->vertices[i];
        vertex->x += vertex->vx * clamped_dt;
        vertex->y += vertex->vy * clamped_dt;

        if (vertex->x <= MYSTIFY_MIN_X) {
            vertex->x = MYSTIFY_MIN_X;
            vertex->vx = fabsf(vertex->vx);
        } else if (vertex->x >= MYSTIFY_MAX_X) {
            vertex->x = MYSTIFY_MAX_X;
            vertex->vx = -fabsf(vertex->vx);
        }

        if (vertex->y <= MYSTIFY_MIN_Y) {
            vertex->y = MYSTIFY_MIN_Y;
            vertex->vy = fabsf(vertex->vy);
        } else if (vertex->y >= MYSTIFY_MAX_Y) {
            vertex->y = MYSTIFY_MAX_Y;
            vertex->vy = -fabsf(vertex->vy);
        }
    }

    mystify_push_history(state);
}

void screensaver_mystify_draw(surface_t *display, const screensaver_mystify_state_t *state) {
    if (!display || !state) {
        return;
    }

    float aspect = 4.0f / 3.0f;
    if (display_get_height() > 0) {
        aspect = (float)display_get_width() / (float)display_get_height();
    }

    gl_context_begin();

    glViewport(0, 0, display_get_width(), display_get_height());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.01f, 0.01f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glShadeModel(GL_SMOOTH);
    glDitherModeN64(DITHER_BAYER_BAYER);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glBegin(GL_QUADS);
        glColor4f(0.00f, 0.01f, 0.05f, 1.0f);
        glVertex2f(-aspect, -1.0f);
        glVertex2f(aspect, -1.0f);
        glColor4f(0.01f, 0.03f, 0.12f, 1.0f);
        glVertex2f(aspect, 1.0f);
        glVertex2f(-aspect, 1.0f);
    glEnd();

    for (int trail = state->history_count - 1; trail >= 0; trail--) {
        float trail_t = (float)trail / (float)(SCREENSAVER_MYSTIFY_TRAIL_COUNT - 1);
        float hue = state->time_s * 0.07f + ((1.0f - trail_t) * 0.22f);
        mystify_rgb_t color = mystify_hsv_to_rgb(hue, 0.85f, 1.0f);
        float alpha = MYSTIFY_BASE_ALPHA + ((1.0f - trail_t) * (MYSTIFY_HEAD_ALPHA - MYSTIFY_BASE_ALPHA));
        float offset = trail_t * 0.02f;
        float skew = sinf((state->time_s * 0.8f) + (trail_t * 3.0f)) * 0.015f;

        glColor4f(color.r, color.g, color.b, alpha);
        glBegin(GL_LINE_LOOP);
            for (int v = 0; v < SCREENSAVER_MYSTIFY_VERTEX_COUNT; v++) {
                float x = ((state->history[trail][v].x * 2.0f) - 1.0f) * aspect;
                float y = (state->history[trail][v].y * 2.0f) - 1.0f;
                glVertex2f(x + offset, y + skew);
            }
        glEnd();
    }

    glDisable(GL_BLEND);
    gl_context_end();
}
