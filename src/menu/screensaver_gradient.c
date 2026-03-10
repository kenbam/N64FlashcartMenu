#include <math.h>

#include <libdragon.h>

#include "screensaver_gradient.h"

#define SCREENSAVER_GRADIENT_GRID_X (26)
#define SCREENSAVER_GRADIENT_GRID_Y (15)

typedef struct {
    float r;
    float g;
    float b;
} gradient_rgb_t;

typedef struct {
    float x;
    float y;
    color_t color;
} gradient_vertex_t;

static const gradient_rgb_t gradient_themes[][SCREENSAVER_GRADIENT_POINT_COUNT] = {
    {
        {0.97f, 0.16f, 0.40f},
        {0.10f, 0.74f, 1.00f},
    },
    {
        {1.00f, 0.44f, 0.10f},
        {0.86f, 0.22f, 0.96f},
    },
    {
        {0.18f, 0.88f, 0.42f},
        {0.18f, 0.40f, 1.00f},
    },
};

static uint32_t gradient_rand_u32(screensaver_gradient_state_t *state) {
    if (state->rng == 0) {
        state->rng = (uint32_t)get_ticks_us() ^ 0x6C8E9CF5u;
    }
    state->rng = (state->rng * 1664525u) + 1013904223u;
    return state->rng;
}

static float gradient_rand_unit(screensaver_gradient_state_t *state) {
    return (float)(gradient_rand_u32(state) & 0xFFFFu) / 65535.0f;
}

static float gradient_clampf(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static uint8_t gradient_u8(float value) {
    int scaled = (int)(value * 255.0f);
    if (scaled < 0) return 0;
    if (scaled > 255) return 255;
    return (uint8_t)scaled;
}

static const float gradient_bayer8[8][8] = {
    { 0.0f, 48.0f, 12.0f, 60.0f,  3.0f, 51.0f, 15.0f, 63.0f },
    {32.0f, 16.0f, 44.0f, 28.0f, 35.0f, 19.0f, 47.0f, 31.0f },
    { 8.0f, 56.0f,  4.0f, 52.0f, 11.0f, 59.0f,  7.0f, 55.0f },
    {40.0f, 24.0f, 36.0f, 20.0f, 43.0f, 27.0f, 39.0f, 23.0f },
    { 2.0f, 50.0f, 14.0f, 62.0f,  1.0f, 49.0f, 13.0f, 61.0f },
    {34.0f, 18.0f, 46.0f, 30.0f, 33.0f, 17.0f, 45.0f, 29.0f },
    {10.0f, 58.0f,  6.0f, 54.0f,  9.0f, 57.0f,  5.0f, 53.0f },
    {42.0f, 26.0f, 38.0f, 22.0f, 41.0f, 25.0f, 37.0f, 21.0f },
};

static float gradient_bayer_sample(int x, int y, int phase_x, int phase_y) {
    return (gradient_bayer8[(y + phase_y) & 7][(x + phase_x) & 7] / 63.0f) - 0.5f;
}

static float gradient_dither(int x, int y, float time_s, int channel) {
    int phase = ((int)(time_s * 6.0f)) & 7;
    return gradient_bayer_sample(x, y, phase + (channel * 3), (phase >> 1) + (channel * 5));
}

static void gradient_sample_jitter(int x, int y, float time_s, float *jitter_u, float *jitter_v) {
    int phase = ((int)(time_s * 4.0f)) & 7;
    float primary = gradient_bayer_sample(x, y, phase, phase >> 1);
    float secondary = gradient_bayer_sample(x, y, phase + 3, phase + 5);
    *jitter_u = (primary * 0.18f) + (secondary * 0.07f);
    *jitter_v = (secondary * 0.16f) - (primary * 0.06f);
}

static gradient_rgb_t gradient_rgb_scale(gradient_rgb_t color, float scale) {
    gradient_rgb_t out = { color.r * scale, color.g * scale, color.b * scale };
    return out;
}

static gradient_rgb_t gradient_rgb_lerp(gradient_rgb_t a, gradient_rgb_t b, float t) {
    gradient_rgb_t out = {
        a.r + ((b.r - a.r) * t),
        a.g + ((b.g - a.g) * t),
        a.b + ((b.b - a.b) * t),
    };
    return out;
}

static gradient_rgb_t gradient_rgb_saturate(gradient_rgb_t color, float amount) {
    float luma = (color.r * 0.30f) + (color.g * 0.59f) + (color.b * 0.11f);
    gradient_rgb_t out = {
        luma + ((color.r - luma) * amount),
        luma + ((color.g - luma) * amount),
        luma + ((color.b - luma) * amount),
    };
    return out;
}

static color_t gradient_to_color(gradient_rgb_t color, int grid_x, int grid_y, float time_s) {
    float dither_r = gradient_dither(grid_x, grid_y, time_s, 0) * 0.020f;
    float dither_g = gradient_dither(grid_x, grid_y, time_s, 1) * 0.017f;
    float dither_b = gradient_dither(grid_x, grid_y, time_s, 2) * 0.020f;
    return RGBA32(
        gradient_u8(gradient_clampf(color.r + dither_r, 0.0f, 1.0f)),
        gradient_u8(gradient_clampf(color.g + dither_g, 0.0f, 1.0f)),
        gradient_u8(gradient_clampf(color.b + dither_b, 0.0f, 1.0f)),
        0xFF
    );
}

static void gradient_make_vertex(float *out, gradient_vertex_t vertex) {
    out[0] = vertex.x;
    out[1] = vertex.y;
    out[2] = (float)vertex.color.r / 255.0f;
    out[3] = (float)vertex.color.g / 255.0f;
    out[4] = (float)vertex.color.b / 255.0f;
    out[5] = 1.0f;
}

static void gradient_draw_triangle(gradient_vertex_t a, gradient_vertex_t b, gradient_vertex_t c) {
    float va[6];
    float vb[6];
    float vc[6];
    gradient_make_vertex(va, a);
    gradient_make_vertex(vb, b);
    gradient_make_vertex(vc, c);
    rdpq_triangle(&TRIFMT_SHADE, va, vb, vc);
}

static gradient_rgb_t gradient_point_color(const screensaver_gradient_state_t *state, int point_index) {
    float theme_time = state ? (state->time_s * 0.065f) : 0.0f;
    int theme_count = (int)(sizeof(gradient_themes) / sizeof(gradient_themes[0]));
    int theme_index = ((int)floorf(theme_time)) % theme_count;
    int next_theme_index = (theme_index + 1) % theme_count;
    float blend = theme_time - floorf(theme_time);
    blend = blend * blend * (3.0f - (2.0f * blend));

    gradient_rgb_t base = gradient_rgb_lerp(
        gradient_themes[theme_index][point_index % SCREENSAVER_GRADIENT_POINT_COUNT],
        gradient_themes[next_theme_index][point_index % SCREENSAVER_GRADIENT_POINT_COUNT],
        blend
    );
    float pulse = 0.84f + (0.08f * sinf((state->time_s * 0.7f) + state->points[point_index].phase));
    return gradient_rgb_scale(base, pulse);
}

static gradient_rgb_t gradient_sample(const screensaver_gradient_state_t *state, float u, float v) {
    gradient_rgb_t color = {0.012f, 0.016f, 0.026f};
    float center_dx = u - 0.5f;
    float center_dy = v - 0.5f;
    float vignette = 1.0f - gradient_clampf((center_dx * center_dx) + (center_dy * center_dy), 0.0f, 0.34f);
    color = gradient_rgb_scale(color, 0.68f + (vignette * 0.34f));

    gradient_rgb_t point_colors[SCREENSAVER_GRADIENT_POINT_COUNT];
    gradient_rgb_t hue_mix = {0.0f, 0.0f, 0.0f};
    float total_weight = 0.0f;
    float max_weight = 0.0f;

    for (int i = 0; i < SCREENSAVER_GRADIENT_POINT_COUNT; i++) {
        float dx = u - state->points[i].x;
        float dy = v - state->points[i].y;
        float dist2 = (dx * dx) + (dy * dy);
        float radius = state->points[i].radius;
        float weight = (radius * radius) / (dist2 + (radius * radius * 0.12f));
        weight = gradient_clampf(weight, 0.0f, 1.8f);
        weight = weight * weight * 1.15f;
        point_colors[i] = gradient_point_color(state, i);
        hue_mix.r += point_colors[i].r * weight;
        hue_mix.g += point_colors[i].g * weight;
        hue_mix.b += point_colors[i].b * weight;
        total_weight += weight;
        if (weight > max_weight) {
            max_weight = weight;
        }
    }

    if (total_weight > 0.0001f) {
        float inv_weight = 1.0f / total_weight;
        hue_mix = gradient_rgb_scale(hue_mix, inv_weight);
        hue_mix = gradient_rgb_saturate(hue_mix, 1.42f);

        float center_focus = gradient_clampf(max_weight / 1.45f, 0.0f, 1.0f);
        float hue_zone = gradient_clampf((powf(total_weight, 0.70f) * 0.12f) + (powf(max_weight, 0.78f) * 0.30f), 0.0f, 0.44f);
        float saturation_zone = powf(center_focus, 1.75f);
        float hot_core = powf(center_focus, 3.10f);
        float burst = 0.90f + (0.10f * sinf((state->time_s * 0.72f) + (u * 5.6f) + (v * 4.8f)));

        color = gradient_rgb_lerp(color, hue_mix, hue_zone);
        color = gradient_rgb_saturate(color, 1.12f + (saturation_zone * 0.32f));
        color = gradient_rgb_lerp(color, gradient_rgb_scale(hue_mix, 1.04f), gradient_clampf(hot_core * burst * 0.16f, 0.0f, 0.16f));
    }

    return gradient_rgb_saturate(color, 1.10f);
}

static void gradient_randomize_point(screensaver_gradient_state_t *state, screensaver_gradient_point_t *point, int index) {
    point->x = 0.17f + (gradient_rand_unit(state) * 0.66f);
    point->y = 0.15f + (gradient_rand_unit(state) * 0.70f);
    point->vx = (gradient_rand_unit(state) * 0.15f) - 0.075f;
    point->vy = (gradient_rand_unit(state) * 0.13f) - 0.065f;
    point->radius = 0.31f + (gradient_rand_unit(state) * 0.17f) + (index == 0 ? 0.05f : 0.01f);
    point->phase = (float)index * 1.4f + (gradient_rand_unit(state) * 2.5f);
}

void screensaver_gradient_init_state(screensaver_gradient_state_t *state) {
    if (!state) {
        return;
    }
    state->rng = 0;
    state->time_s = 0.0f;
    for (int i = 0; i < SCREENSAVER_GRADIENT_POINT_COUNT; i++) {
        gradient_randomize_point(state, &state->points[i], i);
    }
}

void screensaver_gradient_reset(screensaver_gradient_state_t *state) {
    if (!state) {
        return;
    }
    state->time_s = 0.0f;
}

void screensaver_gradient_activate(screensaver_gradient_state_t *state) {
    if (!state) {
        return;
    }
    state->time_s = 0.0f;
    for (int i = 0; i < SCREENSAVER_GRADIENT_POINT_COUNT; i++) {
        gradient_randomize_point(state, &state->points[i], i);
    }
}

void screensaver_gradient_step(screensaver_gradient_state_t *state, float dt) {
    if (!state) {
        return;
    }

    state->time_s += dt;
    for (int i = 0; i < SCREENSAVER_GRADIENT_POINT_COUNT; i++) {
        screensaver_gradient_point_t *point = &state->points[i];
        point->x += point->vx * dt;
        point->y += point->vy * dt;

        if (point->x < 0.04f) {
            point->x = 0.04f;
            point->vx = fabsf(point->vx);
        } else if (point->x > 0.96f) {
            point->x = 0.96f;
            point->vx = -fabsf(point->vx);
        }

        if (point->y < 0.04f) {
            point->y = 0.04f;
            point->vy = fabsf(point->vy);
        } else if (point->y > 0.96f) {
            point->y = 0.96f;
            point->vy = -fabsf(point->vy);
        }
    }
}

void screensaver_gradient_draw(surface_t *display, const screensaver_gradient_state_t *state) {
    if (!display || !state) {
        return;
    }

    const int screen_w = display_get_width();
    const int screen_h = display_get_height();
    gradient_vertex_t vertices[SCREENSAVER_GRADIENT_GRID_Y + 1][SCREENSAVER_GRADIENT_GRID_X + 1];

    for (int y = 0; y <= SCREENSAVER_GRADIENT_GRID_Y; y++) {
        float v = (float)y / (float)SCREENSAVER_GRADIENT_GRID_Y;
        for (int x = 0; x <= SCREENSAVER_GRADIENT_GRID_X; x++) {
            float u = (float)x / (float)SCREENSAVER_GRADIENT_GRID_X;
            float jitter_u = 0.0f;
            float jitter_v = 0.0f;
            gradient_sample_jitter(x, y, state->time_s, &jitter_u, &jitter_v);
            float sample_u = gradient_clampf(
                u + (jitter_u / (float)SCREENSAVER_GRADIENT_GRID_X),
                0.0f,
                1.0f
            );
            float sample_v = gradient_clampf(
                v + (jitter_v / (float)SCREENSAVER_GRADIENT_GRID_Y),
                0.0f,
                1.0f
            );
            vertices[y][x].x = u * (float)screen_w;
            vertices[y][x].y = v * (float)screen_h;
            vertices[y][x].color = gradient_to_color(gradient_sample(state, sample_u, sample_v), x, y, state->time_s);
        }
    }

    rdpq_set_scissor(0, 0, screen_w, screen_h);
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        for (int y = 0; y < SCREENSAVER_GRADIENT_GRID_Y; y++) {
            for (int x = 0; x < SCREENSAVER_GRADIENT_GRID_X; x++) {
                gradient_draw_triangle(vertices[y][x], vertices[y][x + 1], vertices[y + 1][x + 1]);
                gradient_draw_triangle(vertices[y][x], vertices[y + 1][x + 1], vertices[y + 1][x]);
            }
        }
    rdpq_mode_pop();
}
