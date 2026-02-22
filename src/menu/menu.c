/**
 * @file menu.c
 * @brief Menu system implementation
 * @ingroup menu
 */

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include <libdragon.h>

#include "actions.h"
#include "boot/boot.h"
#include "flashcart/flashcart.h"
#include "fonts.h"
#include "hdmi.h"
#include "menu_state.h"
#include "menu.h"
#include "mp3_player.h"
#include "playtime.h"
#include "png_decoder.h"
#include "settings.h"
#include "sound.h"
#include "usb_comm.h"
#include "utils/fs.h"
#include "views/views.h"

#define MENU_DIRECTORY              "/menu"
#define MENU_SETTINGS_FILE          "config.ini"
#define MENU_CUSTOM_FONT_FILE       "custom.font64"
#define MENU_ROM_LOAD_HISTORY_FILE  "history.ini"
#define MENU_ROM_PLAYTIME_FILE      "playtime.ini"
#define MENU_BGM_MP3_FILE           "/menu/music/menu.mp3"
#define MENU_BGM_MP3_FILE_FALLBACK  "/menu/music/bgm.mp3"

#define MENU_CACHE_DIRECTORY        "cache"
#define BACKGROUND_CACHE_FILE       "background.data"
#define BACKGROUND_IMAGES_DIRECTORY "backgrounds"

#define FPS_LIMIT                   (30.0f)
#define SCREENSAVER_IDLE_SECONDS    (30)
#define SCREENSAVER_LOGO_WIDTH      (96)
#define SCREENSAVER_LOGO_HEIGHT     (28)
#define SCREENSAVER_LOGO_FILE       "/menu/DVD_video_logo.png"
#define SCREENSAVER_LOGO_FILE_ALT   "/menu/screensavers/DVD_video_logo.png"
#define SCREENSAVER_LOGO_FILE_DEFAULT "/menu/screensavers/dvd-logo.png"
#define SCREENSAVER_LOGO_MAX_WIDTH  (180)
#define SCREENSAVER_LOGO_MAX_HEIGHT (96)
#define SCREENSAVER_DEBUG_BOUNDS    (0)

static menu_t *menu;

/** FIXME: These are used for overriding libdragon's global variables for TV type to allow PAL60 compatibility
 *  with hardware mods that don't really understand the VI output.
 **/
static tv_type_t tv_type;
extern int __boot_tvtype;
/* -- */

static bool interlaced = true;
static bool menu_bgm_initialized = false;
static bool menu_bgm_loaded = false;
static bool menu_bgm_error = false;
static int screensaver_fps_mode_applied = -1;
static struct {
    bool active;
    int idle_frames;
    float prev_x;
    float prev_y;
    float x;
    float y;
    float vx;
    float vy;
    float accumulator_s;
    uint64_t last_ticks_us;
    uint8_t color_index;
} screensaver = {
    .active = false,
    .idle_frames = 0,
    .prev_x = 64.0f,
    .prev_y = 64.0f,
    .x = 64.0f,
    .y = 64.0f,
    .vx = 60.0f,
    .vy = 60.0f,
    .accumulator_s = 0.0f,
    .last_ticks_us = 0,
    .color_index = 0,
};

static const color_t screensaver_palette[] = {
    RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    RGBA32(0xFF, 0x5E, 0x5E, 0xFF),
    RGBA32(0x55, 0xE8, 0xFF, 0xFF),
    RGBA32(0xFF, 0xD3, 0x55, 0xFF),
    RGBA32(0x7B, 0xFF, 0x83, 0xFF),
    RGBA32(0xFF, 0x7B, 0xF1, 0xFF),
};

static surface_t *screensaver_logo_image = NULL;
static bool screensaver_logo_loading = false;
static void menu_bgm_deinit (void);
static void screensaver_logo_reload(menu_t *menu);
static void screensaver_logo_try_load(menu_t *menu);

static void screensaver_logo_callback(png_err_t err, surface_t *decoded_image, void *callback_data) {
    (void)callback_data;
    screensaver_logo_loading = false;
    if (err == PNG_OK) {
        screensaver_logo_image = decoded_image;
    } else if (decoded_image) {
        surface_free(decoded_image);
        free(decoded_image);
    }
}

static void screensaver_logo_free (void) {
    if (screensaver_logo_loading) {
        png_decoder_abort();
        screensaver_logo_loading = false;
    }
    if (screensaver_logo_image) {
        surface_free(screensaver_logo_image);
        free(screensaver_logo_image);
        screensaver_logo_image = NULL;
    }
}

static void screensaver_logo_try_load_path(menu_t *menu, const char *logo_file) {
    if (screensaver_logo_loading || screensaver_logo_image || png_decoder_is_busy()) {
        return;
    }
    if (!logo_file || logo_file[0] == '\0') {
        return;
    }

    path_t *logo_path = path_init(menu->storage_prefix, (char *)logo_file);
    if (file_exists(path_get(logo_path))) {
        // Use a larger decode target for compatibility with logo PNGs that fail
        // when decoded directly to a very small target size.
        png_err_t png_err = png_decoder_start(path_get(logo_path), 1024, 1024, screensaver_logo_callback, NULL);
        if (png_err == PNG_OK) {
            screensaver_logo_loading = true;
        }
    }
    path_free(logo_path);
}

static void screensaver_logo_try_load(menu_t *menu) {
    if (!menu || screensaver_logo_loading || screensaver_logo_image) {
        return;
    }

    if (menu->settings.screensaver_logo_file && menu->settings.screensaver_logo_file[0] != '\0') {
        screensaver_logo_try_load_path(menu, menu->settings.screensaver_logo_file);
    }
    if (!screensaver_logo_loading && !screensaver_logo_image) {
        screensaver_logo_try_load_path(menu, SCREENSAVER_LOGO_FILE_DEFAULT);
    }
    if (!screensaver_logo_loading && !screensaver_logo_image) {
        screensaver_logo_try_load_path(menu, SCREENSAVER_LOGO_FILE_ALT);
    }
    if (!screensaver_logo_loading && !screensaver_logo_image) {
        screensaver_logo_try_load_path(menu, SCREENSAVER_LOGO_FILE);
    }
}

static void screensaver_logo_reload(menu_t *menu) {
    screensaver_logo_free();
    screensaver_logo_try_load(menu);
}

static void screensaver_get_logo_size(int *width, int *height) {
    if (screensaver_logo_image) {
        *width = screensaver_logo_image->width;
        *height = screensaver_logo_image->height;
        return;
    }
    *width = SCREENSAVER_LOGO_WIDTH;
    *height = SCREENSAVER_LOGO_HEIGHT;
}

static void screensaver_get_logo_dimensions(int *logo_width, int *logo_height) {
    screensaver_get_logo_size(logo_width, logo_height);
    int screen_w = display_get_width();
    int screen_h = display_get_height();
    if (*logo_width > screen_w) {
        *logo_width = screen_w;
    }
    if (*logo_height > screen_h) {
        *logo_height = screen_h;
    }
}

static bool menu_has_any_input (menu_t *menu) {
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

static bool screensaver_mode_allowed (menu_mode_t mode) {
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

static void screensaver_cycle_color (void) {
    uint8_t count = (uint8_t)(sizeof(screensaver_palette) / sizeof(screensaver_palette[0]));
    screensaver.color_index = (screensaver.color_index + 1) % count;
}

static bool screensaver_simulate_step (int logo_width, int logo_height, int min_x_px, int max_x_px, int min_y_px, int max_y_px, float dt) {
    float min_x = (float)min_x_px;
    float min_y = (float)min_y_px;
    float max_x = (float)max_x_px;
    float max_y = (float)max_y_px;
    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;

    float next_x = screensaver.x + (screensaver.vx * dt);
    float next_y = screensaver.y + (screensaver.vy * dt);
    bool bounced_x = false;
    bool bounced_y = false;

    // Simple directional edge checks:
    // moving right -> compare right edge, moving left -> compare left edge.
    if (screensaver.vx > 0.0f && next_x >= max_x) {
        next_x = max_x;
        screensaver.vx = -fabsf(screensaver.vx);
        bounced_x = true;
    } else if (screensaver.vx < 0.0f && next_x <= min_x) {
        next_x = min_x;
        screensaver.vx = fabsf(screensaver.vx);
        bounced_x = true;
    }

    // moving down -> compare bottom edge, moving up -> compare top edge.
    if (screensaver.vy > 0.0f && next_y >= max_y) {
        next_y = max_y;
        screensaver.vy = -fabsf(screensaver.vy);
        bounced_y = true;
    } else if (screensaver.vy < 0.0f && next_y <= min_y) {
        next_y = min_y;
        screensaver.vy = fabsf(screensaver.vy);
        bounced_y = true;
    }

    if (next_x < min_x) next_x = min_x;
    if (next_x > max_x) next_x = max_x;
    if (next_y < min_y) next_y = min_y;
    if (next_y > max_y) next_y = max_y;

    if (bounced_x || bounced_y) {
        screensaver_cycle_color();
    }

    screensaver.prev_x = screensaver.x;
    screensaver.prev_y = screensaver.y;
    screensaver.x = next_x;
    screensaver.y = next_y;

    return bounced_x || bounced_y;
}

static void screensaver_apply_fps_limit(menu_t *menu) {
    int desired_mode = 0; // normal
    if (menu && screensaver.active && menu->settings.screensaver_smooth_mode) {
        desired_mode = 1; // smooth screensaver
    }
    if (desired_mode == screensaver_fps_mode_applied) {
        return;
    }

    screensaver_fps_mode_applied = desired_mode;
    display_set_fps_limit(desired_mode ? 60.0f : FPS_LIMIT);
}

static void screensaver_reset (menu_t *menu) {
    bool was_active = screensaver.active;
    screensaver.active = false;
    screensaver.idle_frames = 0;
    screensaver.accumulator_s = 0.0f;
    screensaver.last_ticks_us = 0;
    if (was_active) {
        screensaver_apply_fps_limit(menu);
    }
}

static void screensaver_activate (menu_t *menu) {
    int logo_width, logo_height;
    screensaver_get_logo_dimensions(&logo_width, &logo_height);
    int screen_w = display_get_width();
    int screen_h = display_get_height();
    int left = menu ? menu->settings.screensaver_margin_left : 0;
    int right = menu ? menu->settings.screensaver_margin_right : 0;
    int top = menu ? menu->settings.screensaver_margin_top : 0;
    int bottom = menu ? menu->settings.screensaver_margin_bottom : 0;
    int min_x = left;
    int min_y = top;
    int max_x = screen_w - right - logo_width;
    int max_y = screen_h - bottom - logo_height;
    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;

    screensaver.active = true;
    screensaver.idle_frames = 0;
    screensaver.x = (min_x + max_x) / 2.0f;
    screensaver.y = (min_y + max_y) / 2.0f;
    screensaver.prev_x = screensaver.x;
    screensaver.prev_y = screensaver.y;
    screensaver.vx = screensaver.vx < 0 ? -60.0f : 60.0f;
    screensaver.vy = screensaver.vy < 0 ? -60.0f : 60.0f;
    screensaver.accumulator_s = 0.0f;
    screensaver.last_ticks_us = get_ticks_us();
    screensaver_apply_fps_limit(menu);
}

static void screensaver_update_state (menu_t *menu) {
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
        if (screensaver.idle_frames >= (SCREENSAVER_IDLE_SECONDS * (int)FPS_LIMIT)) {
            screensaver_activate(menu);
        }
    }
}

static void screensaver_draw (surface_t *display) {
    int logo_width, logo_height;
    screensaver_get_logo_dimensions(&logo_width, &logo_height);

    rdpq_attach_clear(display, NULL);

    uint64_t now_us = get_ticks_us();
    bool smooth_mode = screensaver.active && menu && menu->settings.screensaver_smooth_mode;
    float target_dt = smooth_mode ? (1.0f / 60.0f) : (1.0f / FPS_LIMIT);
    float dt = target_dt;
    if (screensaver.last_ticks_us != 0 && now_us > screensaver.last_ticks_us) {
        uint64_t delta_us = now_us - screensaver.last_ticks_us;
        if (delta_us > 100000) delta_us = 100000;
        float measured_dt = (float)delta_us / 1000000.0f;
        // Reduce visible jitter from timer noise by snapping to the target frame time
        // when the measured delta is close enough.
        if (measured_dt > (target_dt * 0.75f) && measured_dt < (target_dt * 1.25f)) {
            dt = target_dt;
        } else {
            dt = measured_dt;
        }
    }
    screensaver.last_ticks_us = now_us;

    int screen_w = display_get_width();
    int screen_h = display_get_height();
    int margin_left = menu ? menu->settings.screensaver_margin_left : 0;
    int margin_right = menu ? menu->settings.screensaver_margin_right : 0;
    int margin_top = menu ? menu->settings.screensaver_margin_top : 0;
    int margin_bottom = menu ? menu->settings.screensaver_margin_bottom : 0;
    int min_x = margin_left;
    int min_y = margin_top;
    int max_x_px = screen_w - margin_right - logo_width;
    int max_y_px = screen_h - margin_bottom - logo_height;
    if (max_x_px < min_x) max_x_px = min_x;
    if (max_y_px < min_y) max_y_px = min_y;
    float sim_dt = smooth_mode ? (1.0f / 120.0f) : (1.0f / 60.0f);
    screensaver.accumulator_s += dt;
    if (screensaver.accumulator_s > 0.25f) {
        screensaver.accumulator_s = 0.25f;
    }
    int sim_steps = 0;
    bool bounced_this_frame = false;
    while (screensaver.accumulator_s >= sim_dt && sim_steps < 16) {
        if (screensaver_simulate_step(logo_width, logo_height, min_x, max_x_px, min_y, max_y_px, sim_dt)) {
            bounced_this_frame = true;
        }
        screensaver.accumulator_s -= sim_dt;
        sim_steps++;
    }
    if (sim_steps == 0) {
        screensaver.prev_x = screensaver.x;
        screensaver.prev_y = screensaver.y;
    }

    float alpha = screensaver.accumulator_s / sim_dt;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    if (bounced_this_frame) {
        // Interpolating across a reflection can visually pass through the wall.
        alpha = 1.0f;
    }
    float render_x_f = screensaver.prev_x + (screensaver.x - screensaver.prev_x) * alpha;
    float render_y_f = screensaver.prev_y + (screensaver.y - screensaver.prev_y) * alpha;

    int draw_x = (int)floorf(render_x_f);
    int draw_y = (int)floorf(render_y_f);
    if (draw_x < min_x) draw_x = min_x;
    if (draw_y < min_y) draw_y = min_y;
    if (draw_x > max_x_px) draw_x = max_x_px;
    if (draw_y > max_y_px) draw_y = max_y_px;

    if (screensaver_logo_image) {
        rdpq_mode_push();
            // Use copy mode for exact 1:1 blit footprint (no filtering expansion).
            rdpq_set_mode_copy(false);
            rdpq_set_scissor(0, 0, screen_w, screen_h);
            rdpq_tex_blit(screensaver_logo_image, draw_x, draw_y, &(rdpq_blitparms_t){
                .width = logo_width,
                .height = logo_height,
                .filtering = false,
            });
            rdpq_set_scissor(0, 0, screen_w, screen_h);
        rdpq_mode_pop();
    } else {
        rdpq_set_scissor(0, 0, screen_w, screen_h);
        ui_components_box_draw(
            draw_x,
            draw_y,
            draw_x + SCREENSAVER_LOGO_WIDTH,
            draw_y + SCREENSAVER_LOGO_HEIGHT,
            RGBA32(0x00, 0x00, 0x00, 0xFF)
        );
        ui_components_border_draw(
            draw_x,
            draw_y,
            draw_x + SCREENSAVER_LOGO_WIDTH,
            draw_y + SCREENSAVER_LOGO_HEIGHT
        );
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
        rdpq_set_scissor(0, 0, screen_w, screen_h);
    }

#if SCREENSAVER_DEBUG_BOUNDS
    // Framebuffer edge (green) and collision/draw rect (red) to compare visible crop vs logic.
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

    rdpq_detach_show();
}

static mp3player_err_t menu_bgm_load_file (menu_t *menu, const char *file_name) {
    path_t *path = path_init(menu->storage_prefix, (char *)file_name);

    if (!file_exists(path_get(path))) {
        path_free(path);
        return MP3PLAYER_ERR_NO_FILE;
    }

    mp3player_err_t err = mp3player_load(path_get(path));
    path_free(path);
    return err;
}

static void menu_bgm_init (menu_t *menu) {
    if (menu_bgm_initialized || menu_bgm_error) {
        return;
    }

    mp3player_err_t err = mp3player_init();
    if (err != MP3PLAYER_OK) {
        menu_bgm_error = true;
        debugf("Menu BGM disabled: mp3 init failed (%d)\n", err);
        return;
    }

    menu_bgm_initialized = true;

    if (menu->settings.bgm_file && menu->settings.bgm_file[0] != '\0') {
        err = menu_bgm_load_file(menu, menu->settings.bgm_file);
    } else {
        err = MP3PLAYER_ERR_NO_FILE;
    }

    if (err == MP3PLAYER_ERR_NO_FILE) {
        err = menu_bgm_load_file(menu, MENU_BGM_MP3_FILE);
    }
    if (err == MP3PLAYER_ERR_NO_FILE) {
        err = menu_bgm_load_file(menu, MENU_BGM_MP3_FILE_FALLBACK);
    }

    if (err == MP3PLAYER_OK) {
        menu_bgm_loaded = true;
    } else if (err != MP3PLAYER_ERR_NO_FILE) {
        menu_bgm_error = true;
        debugf("Menu BGM disabled: failed to load mp3 (%d)\n", err);
    }
}

static void menu_bgm_poll (menu_t *menu) {
    if (menu->bgm_reload_requested) {
        menu_bgm_deinit();
        menu->bgm_reload_requested = false;
    }

    if (!menu->settings.bgm_enabled) {
        if (menu_bgm_initialized && mp3player_is_playing()) {
            mp3player_stop();
        }
        return;
    }

    bool decoder_busy = png_decoder_is_busy();
    bool loading_or_booting =
        (menu->mode == MENU_MODE_MUSIC_PLAYER) ||
        decoder_busy ||
        (menu->mode == MENU_MODE_BOOT) ||
        (menu->next_mode == MENU_MODE_BOOT);

    if (loading_or_booting) {
        if (menu_bgm_initialized && mp3player_is_playing()) {
            mp3player_stop();
        }
        return;
    }

    menu_bgm_init(menu);
    if (!menu_bgm_initialized || !menu_bgm_loaded || menu_bgm_error) {
        return;
    }

    if (!mp3player_is_playing()) {
        sound_init_mp3_playback();
        mp3player_mute(false);
        mp3player_err_t err = mp3player_play();
        if (err != MP3PLAYER_OK) {
            menu_bgm_error = true;
            debugf("Menu BGM disabled: failed to start playback (%d)\n", err);
            return;
        }
    }

    mp3player_err_t err = mp3player_process();
    if (err != MP3PLAYER_OK) {
        menu_bgm_error = true;
        debugf("Menu BGM disabled: playback error (%d)\n", err);
    }
}

static void menu_bgm_deinit (void) {
    if (!menu_bgm_initialized) {
        return;
    }

    mp3player_deinit();
    menu_bgm_initialized = false;
    menu_bgm_loaded = false;
    menu_bgm_error = false;
}

/**
 * @brief Initialize the menu system.
 * 
 * @param boot_params Pointer to the boot parameters structure.
 */
static void menu_init (boot_params_t *boot_params) {    
    menu = calloc(1, sizeof(menu_t));
    assert(menu != NULL);

    menu->boot_params = boot_params;

    menu->mode = MENU_MODE_NONE;
    menu->next_mode = MENU_MODE_STARTUP;

    menu->flashcart_err = flashcart_init(&menu->storage_prefix);
    if (menu->flashcart_err != FLASHCART_OK) {
        menu->next_mode = MENU_MODE_FAULT;
    }

    joypad_init();
    timer_init();
    rtc_init();
    rspq_init();
    rdpq_init();
    dfs_init(DFS_DEFAULT_LOCATION);

    actions_init();
    sound_init_default();
    sound_init_sfx();

    hdmi_clear_game_id();

    path_t *path = path_init(menu->storage_prefix, MENU_DIRECTORY);

    directory_create(path_get(path));

    path_push(path, MENU_SETTINGS_FILE);
    settings_init(path_get(path));
    settings_load(&menu->settings);
    int max_theme = ui_components_theme_count() - 1;
    if (menu->settings.ui_theme < 0 || menu->settings.ui_theme > max_theme) {
        menu->settings.ui_theme = 0;
    }
    ui_components_set_theme(menu->settings.ui_theme);
    ui_components_set_text_panel(menu->settings.text_panel_enabled, menu->settings.text_panel_alpha);
    path_pop(path);

    path_push(path, MENU_ROM_LOAD_HISTORY_FILE);
    bookkeeping_init(path_get(path));
    bookkeeping_load(&menu->bookkeeping);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    path_pop(path);

    path_push(path, MENU_ROM_PLAYTIME_FILE);
    playtime_init(path_get(path));
    playtime_load(&menu->playtime);
    time(&menu->current_time);
    playtime_finalize_active(&menu->playtime, menu->current_time);
    path_pop(path);
  
    if (menu->settings.pal60_compatibility_mode) { // hardware VI mods that dont really understand the output
        tv_type = get_tv_type();
        if (tv_type == TV_PAL && menu->settings.pal60_enabled) {
            // HACK: Set TV type to NTSC, so PAL console would output 60 Hz signal instead.
            __boot_tvtype = (int)TV_NTSC;
        }
    }

    // Force interlacing off in VI settings for TVs and other devices that struggle with interlaced video input.
    interlaced = !menu->settings.force_progressive_scan;

    resolution_t resolution = {
        .width = 640,
        .height = 480,
        .interlaced = interlaced ? INTERLACE_HALF : INTERLACE_OFF,
        .pal60 = menu->settings.pal60_enabled, // this may be overridden by the PAL60 compatibility mode.
    };

    display_init(resolution, DEPTH_16_BPP, 2, GAMMA_NONE, interlaced ? FILTERS_DISABLED : FILTERS_RESAMPLE);
    display_set_fps_limit(FPS_LIMIT);

    path_push(path, MENU_CUSTOM_FONT_FILE);
    fonts_init(path_get(path));
    path_pop(path);

    path_push(path, MENU_CACHE_DIRECTORY);
    directory_create(path_get(path));
    path_push(path, BACKGROUND_CACHE_FILE);
    ui_components_background_init(path_get(path));
    path_pop(path);

    screensaver_logo_try_load(menu);
    path_pop(path);

    path_push(path, BACKGROUND_IMAGES_DIRECTORY);
    directory_create(path_get(path));

    path_free(path);

    sound_use_sfx(menu->settings.soundfx_enabled);

    menu->browser.directory = path_init(menu->storage_prefix, menu->settings.default_directory);
    if (!directory_exists(path_get(menu->browser.directory))) {
        path_free(menu->browser.directory);
        menu->browser.directory = path_init(menu->storage_prefix, "/");
    }
    if (menu->settings.browser_sort_mode < BROWSER_SORT_CUSTOM || menu->settings.browser_sort_mode > BROWSER_SORT_ZA) {
        menu->settings.browser_sort_mode = BROWSER_SORT_AZ;
    }
    if (menu->settings.browser_random_mode < 0 || menu->settings.browser_random_mode > 3) {
        menu->settings.browser_random_mode = 0;
    }
    menu->browser.sort_mode = (browser_sort_t)menu->settings.browser_sort_mode;

    debugf("N64FlashcartMenu debugging...\n");
}

/**
 * @brief Deinitialize the menu system.
 * 
 * @param menu Pointer to the menu structure.
 */
static void menu_deinit (menu_t *menu) {
    hdmi_send_game_id(menu->boot_params);

    ui_components_background_free();

    menu_bgm_deinit();

    playtime_save(&menu->playtime);
    playtime_free(&menu->playtime);

    screensaver_logo_free();

    path_free(menu->load.disk_slots.primary.disk_path);
    path_free(menu->load.rom_path);
    for (int i = 0; i < menu->browser.entries; i++) {
        free(menu->browser.list[i].name);
    }
    free(menu->browser.list);
    path_free(menu->browser.directory);
    free(menu);

    display_close();

    sound_deinit();

    rdpq_close();
    rspq_close();
    rtc_close();
    timer_close();
    joypad_close();

    flashcart_deinit();
}

/**
 * @brief View structure containing initialization and display functions.
 */
typedef const struct {
    menu_mode_t id; /**< View ID */
    void (*init) (menu_t *menu); /**< Initialization function */
    void (*show) (menu_t *menu, surface_t *display); /**< Display function */
} view_t;

static view_t menu_views[] = {
    { MENU_MODE_STARTUP, view_startup_init, view_startup_display },
    { MENU_MODE_BROWSER, view_browser_init, view_browser_display },
    { MENU_MODE_FILE_INFO, view_file_info_init, view_file_info_display },
    { MENU_MODE_SYSTEM_INFO, view_system_info_init, view_system_info_display },
    { MENU_MODE_IMAGE_VIEWER, view_image_viewer_init, view_image_viewer_display },
    { MENU_MODE_TEXT_VIEWER, view_text_viewer_init, view_text_viewer_display },
    { MENU_MODE_MUSIC_PLAYER, view_music_player_init, view_music_player_display },
    { MENU_MODE_CREDITS, view_credits_init, view_credits_display },
    { MENU_MODE_SETTINGS_EDITOR, view_settings_init, view_settings_display },
    { MENU_MODE_RTC, view_rtc_init, view_rtc_display },
    { MENU_MODE_CONTROLLER_PAKFS, view_controller_pakfs_init, view_controller_pakfs_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_INFO, view_controller_pak_dump_info_init, view_controller_pak_dump_info_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_NOTE_INFO, view_controller_pak_note_dump_info_init, view_controller_pak_note_dump_info_display },
    { MENU_MODE_FLASHCART, view_flashcart_info_init, view_flashcart_info_display },
    { MENU_MODE_LOAD_ROM, view_load_rom_init, view_load_rom_display },
    { MENU_MODE_LOAD_DISK, view_load_disk_init, view_load_disk_display },
    { MENU_MODE_LOAD_EMULATOR, view_load_emulator_init, view_load_emulator_display },
    { MENU_MODE_ERROR, view_error_init, view_error_display },
    { MENU_MODE_FAULT, view_fault_init, view_fault_display },
    { MENU_MODE_FAVORITE, view_favorite_init, view_favorite_display },
    { MENU_MODE_HISTORY, view_history_init, view_history_display },
    { MENU_MODE_PLAYTIME, view_playtime_init, view_playtime_display },
    { MENU_MODE_DATEL_CODE_EDITOR, view_datel_code_editor_init, view_datel_code_editor_display },
    { MENU_MODE_EXTRACT_FILE, view_extract_file_init, view_extract_file_display }
};

/**
 * @brief Get the view structure for the specified menu mode.
 * 
 * @param id The menu mode ID.
 * @return view_t* Pointer to the view structure.
 */
static view_t *menu_get_view (menu_mode_t id) {
    for (int i = 0; i < sizeof(menu_views) / sizeof(view_t); i++) {
        if (menu_views[i].id == id) {
            return &menu_views[i];
        }
    }
    return NULL;
}

/**
 * @brief Run the menu system.
 * 
 * @param boot_params Pointer to the boot parameters structure.
 */
void menu_run (boot_params_t *boot_params) {
    menu_init(boot_params);

    while (true) {
        surface_t *display = display_try_get();

        if (display != NULL) {
            actions_update(menu);
            screensaver_update_state(menu);
            screensaver_apply_fps_limit(menu);

            if (screensaver.active) {
                screensaver_draw(display);
                time(&menu->current_time);
                menu_bgm_poll(menu);
                sound_poll();
                png_decoder_poll();
                usb_comm_poll(menu);
                continue;
            }

            view_t *view = menu_get_view(menu->mode);
            if (view && view->show) {
                view->show(menu, display);
            } else {
                rdpq_attach_clear(display, NULL);
                rdpq_detach_wait();
                display_show(display);
            }

            if (menu->mode == MENU_MODE_BOOT) {
                break;
            }

            while (menu->mode != menu->next_mode) {
                menu->mode = menu->next_mode;

                view_t *next_view = menu_get_view(menu->next_mode);
                if (next_view && next_view->init) {
                    next_view->init(menu);
                }
            }

            time(&menu->current_time);
        }

        if (menu->screensaver_logo_reload_requested) {
            screensaver_logo_reload(menu);
            menu->screensaver_logo_reload_requested = false;
        } else {
            screensaver_logo_try_load(menu);
        }

        menu_bgm_poll(menu);

        sound_poll();

        png_decoder_poll();

        usb_comm_poll(menu);
    }

    menu_deinit(menu);

    while (exception_reset_time() > 0) {
        // Do nothing if reset button was pressed
    }
}
