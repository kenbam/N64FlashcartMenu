#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libdragon.h>

#include "fonts.h"
#include "native_image.h"
#include "path.h"
#include "png_decoder.h"
#include "screensaver_dvd.h"
#include "ui_components.h"
#include "utils/fs.h"
#include "views/views.h"

#define SCREENSAVER_NATIVE_SIDECAR    ".nimg"
#define SCREENSAVER_LOGO_WIDTH        (96)
#define SCREENSAVER_LOGO_HEIGHT       (28)
#define SCREENSAVER_LOGO_FILE         "/menu/DVD_video_logo.png"
#define SCREENSAVER_LOGO_FILE_ALT     "/menu/screensavers/DVD_video_logo.png"
#define SCREENSAVER_LOGO_FILE_DEFAULT "/menu/screensavers/dvd-logo.png"
#define SCREENSAVER_SPEED_X           (70.0f)
#define SCREENSAVER_SPEED_Y           (58.0f)
#define SCREENSAVER_DEBUG_BOUNDS      (0)

static uint32_t screensaver_dvd_rand_u32(screensaver_dvd_state_t *state) {
    if (state->rng == 0) {
        state->rng = (uint32_t)get_ticks_us() ^ 0xA511E9B3u;
    }
    state->rng = (state->rng * 1664525u) + 1013904223u;
    return state->rng;
}

static int screensaver_dvd_rand_int(screensaver_dvd_state_t *state, int limit) {
    if (limit <= 0) {
        return 0;
    }
    return (int)(screensaver_dvd_rand_u32(state) % (uint32_t)limit);
}

static float screensaver_dvd_rand_speed(screensaver_dvd_state_t *state, float base, int variance) {
    int offset = screensaver_dvd_rand_int(state, variance + variance + 1) - variance;
    return base + (float)offset;
}

static void screensaver_dvd_randomize_motion(
    menu_t *menu,
    screensaver_dvd_state_t *state,
    int min_x,
    int max_x,
    int min_y,
    int max_y
) {
    state->x = (float)(min_x + screensaver_dvd_rand_int(state, (max_x - min_x) + 1));
    state->y = (float)(min_y + screensaver_dvd_rand_int(state, (max_y - min_y) + 1));
    state->prev_x = state->x;
    state->prev_y = state->y;

    float vx = screensaver_dvd_rand_speed(state, SCREENSAVER_SPEED_X, 7);
    float vy = screensaver_dvd_rand_speed(state, SCREENSAVER_SPEED_Y, 7);
    while (fabsf(vx - vy) < 8.0f) {
        vy = screensaver_dvd_rand_speed(state, SCREENSAVER_SPEED_Y, 7);
    }

    state->vx = screensaver_dvd_rand_int(state, 2) ? vx : -vx;
    state->vy = screensaver_dvd_rand_int(state, 2) ? vy : -vy;
    state->color_index = (uint8_t)screensaver_dvd_rand_int(state, 8);

    if (menu && menu->settings.screensaver_smooth_mode) {
        state->vx *= 1.04f;
        state->vy *= 1.04f;
    }
}

static void screensaver_dvd_logo_free(screensaver_dvd_state_t *state) {
    if (!state) {
        return;
    }
    if (state->logo_loading) {
        png_decoder_abort();
        state->logo_loading = false;
    }
    if (state->logo_image) {
        surface_free(state->logo_image);
        free(state->logo_image);
        state->logo_image = NULL;
    }
}

static void screensaver_dvd_logo_callback(png_err_t err, surface_t *decoded_image, void *callback_data) {
    screensaver_dvd_state_t *state = (screensaver_dvd_state_t *)callback_data;
    if (!state) {
        if (decoded_image) {
            surface_free(decoded_image);
            free(decoded_image);
        }
        return;
    }

    state->logo_loading = false;
    if (err == PNG_OK) {
        state->logo_image = decoded_image;
    } else if (decoded_image) {
        surface_free(decoded_image);
        free(decoded_image);
    }
}

static void screensaver_dvd_logo_try_load_path(menu_t *menu, screensaver_dvd_state_t *state, const char *logo_file) {
    if (!menu || !state || state->logo_loading || state->logo_image || png_decoder_is_busy()) {
        return;
    }
    if (!logo_file || logo_file[0] == '\0') {
        return;
    }

    path_t *logo_path = path_init(menu->storage_prefix, (char *)logo_file);
    if (file_exists(path_get(logo_path))) {
        state->logo_image = native_image_load_sidecar_rgba16(path_get(logo_path), SCREENSAVER_NATIVE_SIDECAR, 1024, 1024);
        if (state->logo_image) {
            path_free(logo_path);
            return;
        }

        png_err_t png_err = png_decoder_start(path_get(logo_path), 1024, 1024, screensaver_dvd_logo_callback, state);
        if (png_err == PNG_OK) {
            state->logo_loading = true;
        }
    }
    path_free(logo_path);
}

static void screensaver_dvd_get_logo_size(const screensaver_dvd_state_t *state, int *width, int *height) {
    if (state && state->logo_image) {
        *width = state->logo_image->width;
        *height = state->logo_image->height;
        return;
    }
    *width = SCREENSAVER_LOGO_WIDTH;
    *height = SCREENSAVER_LOGO_HEIGHT;
}

static void screensaver_dvd_get_logo_dimensions(const screensaver_dvd_state_t *state, int *logo_width, int *logo_height) {
    screensaver_dvd_get_logo_size(state, logo_width, logo_height);
    int screen_w = display_get_width();
    int screen_h = display_get_height();
    if (*logo_width > screen_w) {
        *logo_width = screen_w;
    }
    if (*logo_height > screen_h) {
        *logo_height = screen_h;
    }
}

static void screensaver_dvd_cycle_color(screensaver_dvd_state_t *state) {
    if (!state) {
        return;
    }
    state->color_index = (state->color_index + 1) & 7;
}

static bool screensaver_dvd_simulate_step(
    screensaver_dvd_state_t *state,
    int min_x_px,
    int max_x_px,
    int min_y_px,
    int max_y_px,
    float dt
) {
    float min_x = (float)min_x_px;
    float min_y = (float)min_y_px;
    float max_x = (float)max_x_px;
    float max_y = (float)max_y_px;
    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;

    float next_x = state->x + (state->vx * dt);
    float next_y = state->y + (state->vy * dt);
    bool bounced_x = false;
    bool bounced_y = false;

    if (state->vx > 0.0f && next_x >= max_x) {
        next_x = max_x;
        state->vx = -fabsf(state->vx);
        bounced_x = true;
    } else if (state->vx < 0.0f && next_x <= min_x) {
        next_x = min_x;
        state->vx = fabsf(state->vx);
        bounced_x = true;
    }

    if (state->vy > 0.0f && next_y >= max_y) {
        next_y = max_y;
        state->vy = -fabsf(state->vy);
        bounced_y = true;
    } else if (state->vy < 0.0f && next_y <= min_y) {
        next_y = min_y;
        state->vy = fabsf(state->vy);
        bounced_y = true;
    }

    if (bounced_x || bounced_y) {
        screensaver_dvd_cycle_color(state);
    }

    state->prev_x = state->x;
    state->prev_y = state->y;
    state->x = next_x;
    state->y = next_y;

    return bounced_x || bounced_y;
}

void screensaver_dvd_init_state(screensaver_dvd_state_t *state) {
    if (!state) {
        return;
    }
    state->prev_x = 64.0f;
    state->prev_y = 64.0f;
    state->x = 64.0f;
    state->y = 64.0f;
    state->vx = SCREENSAVER_SPEED_X;
    state->vy = SCREENSAVER_SPEED_Y;
    state->accumulator_s = 0.0f;
    state->color_index = 0;
    state->rng = 0;
    state->logo_image = NULL;
    state->logo_loading = false;
    state->logo_search_done = false;
}

void screensaver_dvd_reset(screensaver_dvd_state_t *state) {
    if (!state) {
        return;
    }
    state->accumulator_s = 0.0f;
}

void screensaver_dvd_deinit(screensaver_dvd_state_t *state) {
    screensaver_dvd_logo_free(state);
}

void screensaver_dvd_logo_try_load(menu_t *menu, screensaver_dvd_state_t *state) {
    if (!menu || !state || state->logo_loading || state->logo_image || state->logo_search_done) {
        return;
    }

    if (menu->settings.screensaver_logo_file && menu->settings.screensaver_logo_file[0] != '\0') {
        screensaver_dvd_logo_try_load_path(menu, state, menu->settings.screensaver_logo_file);
    }
    if (!state->logo_loading && !state->logo_image) {
        screensaver_dvd_logo_try_load_path(menu, state, SCREENSAVER_LOGO_FILE_DEFAULT);
    }
    if (!state->logo_loading && !state->logo_image) {
        screensaver_dvd_logo_try_load_path(menu, state, SCREENSAVER_LOGO_FILE_ALT);
    }
    if (!state->logo_loading && !state->logo_image) {
        screensaver_dvd_logo_try_load_path(menu, state, SCREENSAVER_LOGO_FILE);
    }

    if (!state->logo_loading) {
        state->logo_search_done = true;
    }
}

void screensaver_dvd_logo_reload(menu_t *menu, screensaver_dvd_state_t *state) {
    uint64_t start_us = get_ticks_us();
    screensaver_dvd_logo_free(state);
    if (state) {
        state->logo_search_done = false;
    }
    screensaver_dvd_logo_try_load(menu, state);
    browser_playlist_perf_note_screensaver_logo_reload((uint32_t)((get_ticks_us() - start_us) / 1000ULL));
}

void screensaver_dvd_activate(menu_t *menu, screensaver_dvd_state_t *state) {
    if (!menu || !state) {
        return;
    }

    int logo_width, logo_height;
    screensaver_dvd_get_logo_dimensions(state, &logo_width, &logo_height);
    int screen_w = display_get_width();
    int screen_h = display_get_height();
    int min_x = menu->settings.screensaver_margin_left;
    int min_y = menu->settings.screensaver_margin_top;
    int max_x = screen_w - menu->settings.screensaver_margin_right - logo_width;
    int max_y = screen_h - menu->settings.screensaver_margin_bottom - logo_height;
    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;

    screensaver_dvd_randomize_motion(menu, state, min_x, max_x, min_y, max_y);
    state->accumulator_s = 0.0f;
}

void screensaver_dvd_draw(menu_t *menu, screensaver_dvd_state_t *state, surface_t *display, float dt) {
    if (!menu || !state || !display) {
        return;
    }

    int logo_width, logo_height;
    screensaver_dvd_get_logo_dimensions(state, &logo_width, &logo_height);

    int screen_w = display_get_width();
    int screen_h = display_get_height();
    int min_x = menu->settings.screensaver_margin_left;
    int min_y = menu->settings.screensaver_margin_top;
    int max_x_px = screen_w - menu->settings.screensaver_margin_right - logo_width;
    int max_y_px = screen_h - menu->settings.screensaver_margin_bottom - logo_height;
    if (max_x_px < min_x) max_x_px = min_x;
    if (max_y_px < min_y) max_y_px = min_y;

    float sim_dt = menu->settings.screensaver_smooth_mode ? (1.0f / 120.0f) : (1.0f / 60.0f);
    state->accumulator_s += dt;
    if (state->accumulator_s > 0.25f) {
        state->accumulator_s = 0.25f;
    }

    int sim_steps = 0;
    bool bounced_this_frame = false;
    while (state->accumulator_s >= sim_dt && sim_steps < 16) {
        if (screensaver_dvd_simulate_step(state, min_x, max_x_px, min_y, max_y_px, sim_dt)) {
            bounced_this_frame = true;
        }
        state->accumulator_s -= sim_dt;
        sim_steps++;
    }
    if (sim_steps == 0) {
        state->prev_x = state->x;
        state->prev_y = state->y;
    }

    float alpha = state->accumulator_s / sim_dt;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    if (bounced_this_frame) {
        alpha = 1.0f;
    }

    int draw_x = (int)floorf(state->prev_x + ((state->x - state->prev_x) * alpha));
    int draw_y = (int)floorf(state->prev_y + ((state->y - state->prev_y) * alpha));
    if (draw_x < min_x) draw_x = min_x;
    if (draw_y < min_y) draw_y = min_y;
    if (draw_x > max_x_px) draw_x = max_x_px;
    if (draw_y > max_y_px) draw_y = max_y_px;

    if (state->logo_image) {
        rdpq_mode_push();
            rdpq_set_mode_copy(true);
            rdpq_set_scissor(0, 0, screen_w, screen_h);
            rdpq_tex_blit(state->logo_image, draw_x, draw_y, &(rdpq_blitparms_t){
                .width = logo_width,
                .height = logo_height,
                .filtering = false,
            });
        rdpq_mode_pop();
    } else {
        rdpq_set_scissor(0, 0, screen_w, screen_h);
        ui_components_box_draw(draw_x, draw_y, draw_x + SCREENSAVER_LOGO_WIDTH, draw_y + SCREENSAVER_LOGO_HEIGHT, RGBA32(0x00, 0x00, 0x00, 0xFF));
        ui_components_border_draw(draw_x, draw_y, draw_x + SCREENSAVER_LOGO_WIDTH, draw_y + SCREENSAVER_LOGO_HEIGHT);
        rdpq_text_print(
            &(rdpq_textparms_t){
                .style_id = STL_DEFAULT,
                .width = SCREENSAVER_LOGO_WIDTH,
                .height = SCREENSAVER_LOGO_HEIGHT,
                .align = ALIGN_CENTER,
                .valign = VALIGN_CENTER,
            },
            FNT_DEFAULT,
            draw_x,
            draw_y,
            "DVD"
        );
    }

#if SCREENSAVER_DEBUG_BOUNDS
    ui_components_border_draw(0, 0, screen_w, screen_h);
    ui_components_box_draw(0, 0, screen_w, 1, RGBA32(0x40, 0xFF, 0x40, 0xFF));
    ui_components_box_draw(0, screen_h - 1, screen_w, screen_h, RGBA32(0x40, 0xFF, 0x40, 0xFF));
    ui_components_box_draw(0, 0, 1, screen_h, RGBA32(0x40, 0xFF, 0x40, 0xFF));
    ui_components_box_draw(screen_w - 1, 0, screen_w, screen_h, RGBA32(0x40, 0xFF, 0x40, 0xFF));
    ui_components_box_draw(draw_x, draw_y, draw_x + logo_width, draw_y + 1, RGBA32(0xFF, 0x40, 0x40, 0xFF));
    ui_components_box_draw(draw_x, draw_y + logo_height - 1, draw_x + logo_width, draw_y + logo_height, RGBA32(0xFF, 0x40, 0x40, 0xFF));
    ui_components_box_draw(draw_x, draw_y, draw_x + 1, draw_y + logo_height, RGBA32(0xFF, 0x40, 0x40, 0xFF));
    ui_components_box_draw(draw_x + logo_width - 1, draw_y, draw_x + logo_width, draw_y + logo_height, RGBA32(0xFF, 0x40, 0x40, 0xFF));
#endif
}
