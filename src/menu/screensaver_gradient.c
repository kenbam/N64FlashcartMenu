#include <math.h>

#include <libdragon.h>

#include "screensaver_gradient.h"

#define SCREENSAVER_GRADIENT_BLOB_TEX_SIZE (40)

typedef struct {
    float r;
    float g;
    float b;
} gradient_rgb_t;

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

static uint8_t gradient_blob_texels[SCREENSAVER_GRADIENT_BLOB_TEX_SIZE * SCREENSAVER_GRADIENT_BLOB_TEX_SIZE * 2] __attribute__((aligned(8)));
static surface_t gradient_blob_surface;
static bool gradient_blob_ready = false;

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

static float gradient_bayer_sample(int x, int y) {
    return (gradient_bayer8[y & 7][x & 7] / 63.0f) - 0.5f;
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

static gradient_rgb_t gradient_rgb_add(gradient_rgb_t a, gradient_rgb_t b) {
    gradient_rgb_t out = { a.r + b.r, a.g + b.g, a.b + b.b };
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

static color_t gradient_rgb_to_color(gradient_rgb_t color, uint8_t alpha) {
    return RGBA32(
        gradient_u8(gradient_clampf(color.r, 0.0f, 1.0f)),
        gradient_u8(gradient_clampf(color.g, 0.0f, 1.0f)),
        gradient_u8(gradient_clampf(color.b, 0.0f, 1.0f)),
        alpha
    );
}

static void gradient_prepare_blob_texture(void) {
    if (gradient_blob_ready) {
        return;
    }

    gradient_blob_surface = surface_make_linear(
        gradient_blob_texels,
        FMT_IA16,
        SCREENSAVER_GRADIENT_BLOB_TEX_SIZE,
        SCREENSAVER_GRADIENT_BLOB_TEX_SIZE
    );

    for (int y = 0; y < SCREENSAVER_GRADIENT_BLOB_TEX_SIZE; y++) {
        for (int x = 0; x < SCREENSAVER_GRADIENT_BLOB_TEX_SIZE; x++) {
            float nx = (((float)x + 0.5f) / (float)SCREENSAVER_GRADIENT_BLOB_TEX_SIZE * 2.0f) - 1.0f;
            float ny = (((float)y + 0.5f) / (float)SCREENSAVER_GRADIENT_BLOB_TEX_SIZE * 2.0f) - 1.0f;
            float dist = sqrtf((nx * nx) + (ny * ny));
            float outer = gradient_clampf(1.0f - dist, 0.0f, 1.0f);
            float base = outer * outer * (3.0f - (2.0f * outer));
            float edge = powf(base, 1.55f);
            float core = powf(base, 0.82f);
            float dither = gradient_bayer_sample(x, y) * 0.035f;
            uint8_t intensity = gradient_u8(gradient_clampf(core + dither, 0.0f, 1.0f));
            uint8_t alpha = gradient_u8(gradient_clampf(edge + (dither * 0.75f), 0.0f, 1.0f));
            int offset = ((y * SCREENSAVER_GRADIENT_BLOB_TEX_SIZE) + x) * 2;
            gradient_blob_texels[offset + 0] = intensity;
            gradient_blob_texels[offset + 1] = alpha;
        }
    }

    gradient_blob_ready = true;
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

static gradient_rgb_t gradient_background_color(const screensaver_gradient_state_t *state) {
    gradient_rgb_t bg = {0.010f, 0.012f, 0.020f};
    gradient_rgb_t sum = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < SCREENSAVER_GRADIENT_POINT_COUNT; i++) {
        sum = gradient_rgb_add(sum, gradient_point_color(state, i));
    }

    sum = gradient_rgb_scale(sum, 1.0f / (float)SCREENSAVER_GRADIENT_POINT_COUNT);
    sum = gradient_rgb_saturate(sum, 1.10f);
    return gradient_rgb_lerp(bg, gradient_rgb_scale(sum, 0.17f), 0.60f);
}

static void gradient_draw_blob(
    const screensaver_gradient_state_t *state,
    int screen_w,
    int screen_h,
    int point_index,
    float radius_scale,
    uint8_t alpha
) {
    const screensaver_gradient_point_t *point = &state->points[point_index];
    gradient_rgb_t color = gradient_point_color(state, point_index);
    float cx = point->x * (float)screen_w;
    float cy = point->y * (float)screen_h;
    float half_w = point->radius * radius_scale * (float)screen_w;
    float half_h = point->radius * radius_scale * (float)screen_h;

    rdpq_set_prim_color(gradient_rgb_to_color(color, alpha));
    rdpq_texture_rectangle_scaled(
        TILE0,
        cx - half_w,
        cy - half_h,
        cx + half_w,
        cy + half_h,
        0.0f,
        0.0f,
        (float)SCREENSAVER_GRADIENT_BLOB_TEX_SIZE,
        (float)SCREENSAVER_GRADIENT_BLOB_TEX_SIZE
    );
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

    gradient_prepare_blob_texture();
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

    gradient_prepare_blob_texture();
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

    gradient_prepare_blob_texture();

    const int screen_w = display_get_width();
    const int screen_h = display_get_height();
    gradient_rgb_t bg = gradient_background_color(state);

    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_set_prim_color(gradient_rgb_to_color(bg, 0xFF));
        rdpq_fill_rectangle(0, 0, screen_w, screen_h);

        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_dithering(DITHER_BAYER_BAYER);
        rdpq_tex_upload(TILE0, &gradient_blob_surface, NULL);

        for (int i = 0; i < SCREENSAVER_GRADIENT_POINT_COUNT; i++) {
            gradient_draw_blob(state, screen_w, screen_h, i, 1.34f, 156);
        }
        for (int i = 0; i < SCREENSAVER_GRADIENT_POINT_COUNT; i++) {
            gradient_draw_blob(state, screen_w, screen_h, i, 0.72f, 122);
        }
    rdpq_mode_pop();
}
