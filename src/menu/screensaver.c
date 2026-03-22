#include <stdbool.h>

#include <libdragon.h>

#include "screensaver.h"
#include "screensaver_attract.h"
#include "screensaver_dvd.h"
#include "screensaver_gradient.h"
#include "screensaver_pipes_gl_render.h"
#include "screensaver_pipes_render.h"
#include "screensaver_pipes_state.h"

#define SCREENSAVER_FPS_LIMIT    (30.0f)

typedef struct {
    bool active;
    int idle_frames;
    uint64_t last_ticks_us;
    screensaver_style_t active_style;
    screensaver_attract_state_t attract;
    screensaver_dvd_state_t dvd;
    screensaver_gradient_state_t gradient;
    screensaver_pipes_state_t pipes;
} screensaver_state_t;

static int screensaver_fps_mode_applied = -1;
static screensaver_state_t screensaver = {
    .active = false,
    .idle_frames = 0,
    .last_ticks_us = 0,
    .active_style = SCREENSAVER_STYLE_DVD,
};

static screensaver_style_t screensaver_pick_random_style(void) {
    switch ((get_ticks_us() / 1000ULL) % 5ULL) {
        case 1:
            return SCREENSAVER_STYLE_PIPES;
        case 2:
            return SCREENSAVER_STYLE_GRADIENT;
        case 3:
            return SCREENSAVER_STYLE_ATTRACT;
        case 4:
            return SCREENSAVER_STYLE_PIPES_GL;
        case 0:
        default:
            return SCREENSAVER_STYLE_DVD;
    }
}

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
    if (menu->settings.screensaver_style == SCREENSAVER_STYLE_RANDOM) {
        return screensaver.active ? screensaver.active_style : screensaver_pick_random_style();
    }
    if (menu->settings.screensaver_style == SCREENSAVER_STYLE_PIPES) {
        return SCREENSAVER_STYLE_PIPES;
    }
    if (menu->settings.screensaver_style == SCREENSAVER_STYLE_GRADIENT) {
        return SCREENSAVER_STYLE_GRADIENT;
    }
    if (menu->settings.screensaver_style == SCREENSAVER_STYLE_ATTRACT) {
        return SCREENSAVER_STYLE_ATTRACT;
    }
    if (menu->settings.screensaver_style == SCREENSAVER_STYLE_PIPES_GL) {
        return SCREENSAVER_STYLE_PIPES_GL;
    }
    return SCREENSAVER_STYLE_DVD;
}

static int screensaver_get_idle_seconds(const menu_t *menu) {
    if (!menu) {
        return 30;
    }
    int seconds = menu->settings.screensaver_wait_seconds;
    if (seconds < 5) {
        seconds = 5;
    } else if (seconds > 300) {
        seconds = 300;
    }
    return seconds;
}

static void screensaver_reset(menu_t *menu) {
    bool was_active = screensaver.active;
    screensaver.active = false;
    screensaver.idle_frames = 0;
    screensaver.last_ticks_us = 0;
    screensaver.active_style = SCREENSAVER_STYLE_DVD;
    screensaver_attract_reset(&screensaver.attract);
    screensaver_dvd_reset(&screensaver.dvd);
    screensaver_gradient_reset(&screensaver.gradient);
    screensaver_pipes_reset(&screensaver.pipes);
    if (was_active) {
        screensaver_apply_fps_limit(menu);
    }
}

static void screensaver_activate(menu_t *menu) {
    screensaver_style_t style = screensaver_get_style(menu);
    screensaver.active = true;
    screensaver.idle_frames = 0;
    screensaver.last_ticks_us = get_ticks_us();
    screensaver.active_style = style;
    switch (style) {
        case SCREENSAVER_STYLE_PIPES:
        case SCREENSAVER_STYLE_PIPES_GL:
            screensaver_pipes_activate(&screensaver.pipes);
            break;
        case SCREENSAVER_STYLE_GRADIENT:
            screensaver_gradient_activate(&screensaver.gradient);
            break;
        case SCREENSAVER_STYLE_ATTRACT:
            screensaver_attract_activate(menu, &screensaver.attract);
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

    if (screensaver.active &&
        screensaver_get_style(menu) == SCREENSAVER_STYLE_ATTRACT &&
        menu->actions.enter) {
        screensaver_attract_open_current(menu, &screensaver.attract);
        screensaver_reset(menu);
        return;
    }

    if (screensaver.active && screensaver_get_style(menu) == SCREENSAVER_STYLE_ATTRACT) {
        if (menu->actions.go_left) {
            screensaver_attract_cycle_current(menu, &screensaver.attract, -1);
            return;
        }
        if (menu->actions.go_right) {
            screensaver_attract_cycle_current(menu, &screensaver.attract, 1);
            return;
        }
    }

    if (menu_has_any_input(menu)) {
        screensaver_reset(menu);
        return;
    }

    if (!screensaver.active) {
        screensaver.idle_frames++;
        if (screensaver.idle_frames >= (screensaver_get_idle_seconds(menu) * (int)SCREENSAVER_FPS_LIMIT)) {
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

    screensaver_style_t style = screensaver_get_style(menu);

    if (style == SCREENSAVER_STYLE_PIPES_GL) {
        rdpq_attach(display, display_get_zbuf());
        screensaver_pipes_step(&screensaver.pipes, dt * 1.35f);
        screensaver_pipes_gl_draw(display, &screensaver.pipes);
        rdpq_detach_show();
        return;
    }

    rdpq_attach_clear(display, NULL);

    switch (style) {
        case SCREENSAVER_STYLE_PIPES:
            screensaver_pipes_step(&screensaver.pipes, dt);
            screensaver_pipes_draw(display, &screensaver.pipes);
            break;
        case SCREENSAVER_STYLE_GRADIENT:
            screensaver_gradient_step(&screensaver.gradient, dt);
            screensaver_gradient_draw(display, &screensaver.gradient);
            break;
        case SCREENSAVER_STYLE_ATTRACT:
            screensaver_attract_step(menu, &screensaver.attract, dt);
            screensaver_attract_draw(menu, display, &screensaver.attract);
            break;
        case SCREENSAVER_STYLE_DVD:
        default:
            screensaver_dvd_draw(menu, &screensaver.dvd, display, dt);
            break;
    }

    rdpq_detach_show();
}

void screensaver_deinit(void) {
    screensaver_attract_deinit(&screensaver.attract);
    screensaver_dvd_deinit(&screensaver.dvd);
    screensaver_gradient_reset(&screensaver.gradient);
    screensaver_pipes_reset(&screensaver.pipes);
}
