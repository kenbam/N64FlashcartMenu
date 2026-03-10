#include <stdbool.h>

#include <libdragon.h>

#include "screensaver.h"
#include "screensaver_dvd.h"
#include "screensaver_gradient.h"
#include "screensaver_pipes_render.h"
#include "screensaver_pipes_state.h"

#define SCREENSAVER_IDLE_SECONDS (30)
#define SCREENSAVER_FPS_LIMIT    (30.0f)

typedef struct {
    bool active;
    int idle_frames;
    uint64_t last_ticks_us;
    screensaver_dvd_state_t dvd;
    screensaver_gradient_state_t gradient;
    screensaver_pipes_state_t pipes;
} screensaver_state_t;

static int screensaver_fps_mode_applied = -1;
static screensaver_state_t screensaver = {
    .active = false,
    .idle_frames = 0,
    .last_ticks_us = 0,
};

static bool menu_has_any_input(menu_t *menu) {
    return menu->actions.go_up ||
        menu->actions.go_down ||
        menu->actions.go_left ||
        menu->actions.go_right ||
        menu->actions.go_fast ||
        menu->actions.enter ||
        menu->actions.back ||
        menu->actions.options ||
        menu->actions.settings ||
        menu->actions.lz_context;
}

static bool screensaver_mode_allowed(menu_mode_t mode) {
    switch (mode) {
        case MENU_MODE_BROWSER:
        case MENU_MODE_HISTORY:
        case MENU_MODE_FAVORITE:
        case MENU_MODE_PLAYTIME:
        case MENU_MODE_SETTINGS_EDITOR:
        case MENU_MODE_SYSTEM_INFO:
        case MENU_MODE_FLASHCART:
        case MENU_MODE_CREDITS:
        case MENU_MODE_CONTROLLER_PAKFS:
            return true;
        default:
            return false;
    }
}

static screensaver_style_t screensaver_get_style(const menu_t *menu) {
    if (!menu) {
        return SCREENSAVER_STYLE_DVD;
    }
    if (menu->settings.screensaver_style == SCREENSAVER_STYLE_PIPES) {
        return SCREENSAVER_STYLE_PIPES;
    }
    if (menu->settings.screensaver_style == SCREENSAVER_STYLE_GRADIENT) {
        return SCREENSAVER_STYLE_GRADIENT;
    }
    return SCREENSAVER_STYLE_DVD;
}

static void screensaver_reset(menu_t *menu) {
    bool was_active = screensaver.active;
    screensaver.active = false;
    screensaver.idle_frames = 0;
    screensaver.last_ticks_us = 0;
    screensaver_dvd_reset(&screensaver.dvd);
    screensaver_gradient_reset(&screensaver.gradient);
    screensaver_pipes_reset(&screensaver.pipes);
    if (was_active) {
        screensaver_apply_fps_limit(menu);
    }
}

static void screensaver_activate(menu_t *menu) {
    screensaver.active = true;
    screensaver.idle_frames = 0;
    screensaver.last_ticks_us = get_ticks_us();
    switch (screensaver_get_style(menu)) {
        case SCREENSAVER_STYLE_PIPES:
            screensaver_pipes_activate(&screensaver.pipes);
            break;
        case SCREENSAVER_STYLE_GRADIENT:
            screensaver_gradient_activate(&screensaver.gradient);
            break;
        case SCREENSAVER_STYLE_DVD:
        default:
            screensaver_dvd_activate(menu, &screensaver.dvd);
            break;
    }
}

void screensaver_logo_try_load(menu_t *menu) {
    screensaver_dvd_logo_try_load(menu, &screensaver.dvd);
}

void screensaver_logo_reload(menu_t *menu) {
    screensaver_dvd_logo_reload(menu, &screensaver.dvd);
}

void screensaver_update_state(menu_t *menu) {
    if (!screensaver_mode_allowed(menu->mode) || (menu->next_mode != menu->mode)) {
        screensaver_reset(menu);
        return;
    }

    if (menu_has_any_input(menu)) {
        screensaver_reset(menu);
        return;
    }

    if (!screensaver.active) {
        screensaver.idle_frames++;
        if (screensaver.idle_frames >= (SCREENSAVER_IDLE_SECONDS * (int)SCREENSAVER_FPS_LIMIT)) {
            screensaver_activate(menu);
        }
    }
}

void screensaver_apply_fps_limit(menu_t *menu) {
    int desired_mode = 0;
    if (menu && screensaver.active && menu->settings.screensaver_smooth_mode) {
        desired_mode = 1;
    }
    if (desired_mode == screensaver_fps_mode_applied) {
        return;
    }

    screensaver_fps_mode_applied = desired_mode;
    display_set_fps_limit(desired_mode ? 60.0f : SCREENSAVER_FPS_LIMIT);
}

bool screensaver_is_active(void) {
    return screensaver.active;
}

void screensaver_draw(menu_t *menu, surface_t *display) {
    rdpq_attach_clear(display, NULL);

    uint64_t now_us = get_ticks_us();
    float target_dt = (menu && menu->settings.screensaver_smooth_mode) ? (1.0f / 60.0f) : (1.0f / SCREENSAVER_FPS_LIMIT);
    float dt = target_dt;
    if (screensaver.last_ticks_us != 0 && now_us > screensaver.last_ticks_us) {
        uint64_t delta_us = now_us - screensaver.last_ticks_us;
        if (delta_us > 100000) {
            delta_us = 100000;
        }
        float measured_dt = (float)delta_us / 1000000.0f;
        if (measured_dt > (target_dt * 0.75f) && measured_dt < (target_dt * 1.25f)) {
            dt = target_dt;
        } else {
            dt = measured_dt;
        }
    }
    screensaver.last_ticks_us = now_us;

    switch (screensaver_get_style(menu)) {
        case SCREENSAVER_STYLE_PIPES:
            screensaver_pipes_step(&screensaver.pipes, dt);
            screensaver_pipes_draw(display, &screensaver.pipes);
            break;
        case SCREENSAVER_STYLE_GRADIENT:
            screensaver_gradient_step(&screensaver.gradient, dt);
            screensaver_gradient_draw(display, &screensaver.gradient);
            break;
        case SCREENSAVER_STYLE_DVD:
        default:
            screensaver_dvd_draw(menu, &screensaver.dvd, display, dt);
            break;
    }

    rdpq_detach_show();
}

void screensaver_deinit(void) {
    screensaver_dvd_deinit(&screensaver.dvd);
    screensaver_gradient_reset(&screensaver.gradient);
    screensaver_pipes_reset(&screensaver.pipes);
}
