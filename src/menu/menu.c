/**
 * @file menu.c
 * @brief Menu system implementation
 * @ingroup menu
 */

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <GL/gl_integration.h>
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
#include "screensaver.h"
#include "settings.h"
#include "sound.h"
#include "usb_comm.h"
#include "virtual_pak.h"
#include "utils/fs.h"
#include "views/views.h"

#define MENU_DIRECTORY              "/menu"
#define MENU_SETTINGS_FILE          "config.ini"
#define MENU_CUSTOM_FONT_FILE       "custom.font64"
#define MENU_ROM_LOAD_HISTORY_FILE  "history.ini"
#define MENU_ROM_PLAYTIME_FILE      "playtime.ini"
#define MENU_BGM_MP3_FILE           "/menu/music/menu.mp3"
#define MENU_BGM_MP3_FILE_FALLBACK  "/menu/music/bgm.mp3"
#define MENU_BGM_WAV64_FILE         "/menu/music/menu.wav64"
#define MENU_BGM_WAV64_FILE_FALLBACK "/menu/music/bgm.wav64"
#define MENU_CACHE_DIRECTORY        "cache"
#define BACKGROUND_CACHE_FILE       "background.data"
#define BACKGROUND_IMAGES_DIRECTORY "backgrounds"

#define FPS_LIMIT                   (30.0f)

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
static bool menu_bgm_mp3_open = false;
typedef enum {
    MENU_BGM_BACKEND_NONE = 0,
    MENU_BGM_BACKEND_MP3,
    MENU_BGM_BACKEND_WAV64,
} menu_bgm_backend_t;
static menu_bgm_backend_t menu_bgm_backend = MENU_BGM_BACKEND_NONE;
static wav64_t menu_bgm_wav64;
static bool menu_bgm_wav64_open = false;
static bool menu_bgm_perf_pending = false;
typedef struct {
    waveform_t wave;
    waveform_t *inner_wave;
    WaveformRead inner_read;
    WaveformStart inner_start;
    void *inner_ctx;
} menu_bgm_wav64_meter_wrap_t;
static menu_bgm_wav64_meter_wrap_t menu_bgm_wav64_wrap;
static void menu_bgm_deinit (void);

static bool path_has_ext_ci(const char *path, const char *ext) {
    if (!path || !ext) {
        return false;
    }
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (path_len < ext_len) {
        return false;
    }
    return strcasecmp(path + (path_len - ext_len), ext) == 0;
}

static void menu_bgm_set_meter_from_pcm(const void *samples_ptr, int samples, int channels, int bits) {
    if (!sound_bgm_meter_enabled()) {
        return;
    }
    if (!samples_ptr || samples <= 0 || channels <= 0) {
        sound_bgm_meter_reset();
        return;
    }

    sound_bgm_meter_t meter = {0};
    if (bits == 16) {
        const int16_t *buffer = (const int16_t *)samples_ptr;
        uint32_t sum_abs_l = 0, sum_abs_r = 0;
        int16_t peak_l = 0, peak_r = 0;
        for (int i = 0; i < samples; i++) {
            int16_t sl = buffer[i * channels];
            int16_t al = (sl < 0) ? (int16_t)(-sl) : sl;
            if (al > peak_l) peak_l = al;
            sum_abs_l += (uint16_t)al;

            int16_t sr = (channels > 1) ? buffer[i * channels + 1] : sl;
            int16_t ar = (sr < 0) ? (int16_t)(-sr) : sr;
            if (ar > peak_r) peak_r = ar;
            sum_abs_r += (uint16_t)ar;
        }
        const float inv_max = 1.0f / 32768.0f;
        meter.peak_l = peak_l * inv_max;
        meter.peak_r = peak_r * inv_max;
        meter.avg_l = ((float)sum_abs_l / (float)samples) * inv_max;
        meter.avg_r = ((float)sum_abs_r / (float)samples) * inv_max;
        meter.valid = true;
    } else if (bits == 8) {
        const int8_t *buffer = (const int8_t *)samples_ptr;
        uint32_t sum_abs_l = 0, sum_abs_r = 0;
        int16_t peak_l = 0, peak_r = 0;
        for (int i = 0; i < samples; i++) {
            int16_t sl = (int16_t)buffer[i * channels];
            int16_t al = (sl < 0) ? (int16_t)(-sl) : sl;
            if (al > peak_l) peak_l = al;
            sum_abs_l += (uint16_t)al;
            int16_t sr = (channels > 1) ? (int16_t)buffer[i * channels + 1] : sl;
            int16_t ar = (sr < 0) ? (int16_t)(-sr) : sr;
            if (ar > peak_r) peak_r = ar;
            sum_abs_r += (uint16_t)ar;
        }
        const float inv_max = 1.0f / 128.0f;
        meter.peak_l = peak_l * inv_max;
        meter.peak_r = peak_r * inv_max;
        meter.avg_l = ((float)sum_abs_l / (float)samples) * inv_max;
        meter.avg_r = ((float)sum_abs_r / (float)samples) * inv_max;
        meter.valid = true;
    }

    sound_bgm_meter_set(&meter);
}

static void menu_bgm_wav64_meter_start(void *ctx, samplebuffer_t *sbuf) {
    menu_bgm_wav64_meter_wrap_t *w = (menu_bgm_wav64_meter_wrap_t *)ctx;
    sound_bgm_meter_reset();
    if (w && w->inner_start) {
        waveform_t *saved_wave = sbuf ? sbuf->wave : NULL;
        if (sbuf && w->inner_wave) {
            sbuf->wave = w->inner_wave;
        }
        w->inner_start(w->inner_ctx, sbuf);
        if (sbuf) {
            sbuf->wave = saved_wave;
        }
    }
}

static void menu_bgm_wav64_meter_read(void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking) {
    menu_bgm_wav64_meter_wrap_t *w = (menu_bgm_wav64_meter_wrap_t *)ctx;
    if (!w || !w->inner_read) {
        return;
    }

    int before_widx = sbuf->widx;
    waveform_t *saved_wave = sbuf->wave;
    if (w->inner_wave) {
        sbuf->wave = w->inner_wave;
    }
    w->inner_read(w->inner_ctx, sbuf, wpos, wlen, seeking);
    sbuf->wave = saved_wave;
    int after_widx = sbuf->widx;
    int appended = after_widx - before_widx;
    if (appended <= 0) {
        return;
    }

    int bps = 1 << SAMPLES_BPS_SHIFT(sbuf);
    int channels = w->wave.channels > 0 ? w->wave.channels : 2;
    int bits = (bps / (channels > 0 ? channels : 1)) * 8;
    if (bits != 8 && bits != 16) {
        return;
    }

    uint8_t *base = (uint8_t *)SAMPLES_PTR(sbuf);
    void *ptr = base + (before_widx * bps);
    menu_bgm_set_meter_from_pcm(ptr, appended, channels, bits);
}

static char *menu_bgm_resolve_path (menu_t *menu, const char *file_name) {
    if (!menu || !file_name || file_name[0] == '\0') {
        return NULL;
    }

    path_t *path = NULL;
    if (strstr(file_name, ":/") != NULL) {
        path = path_create((char *)file_name);
    } else {
        path = path_init(menu->storage_prefix, (char *)file_name);
    }
    if (!path) {
        return NULL;
    }

    char *resolved = strdup(path_get(path));
    path_free(path);
    return resolved;
}

static bool menu_bgm_is_playing (void) {
    switch (menu_bgm_backend) {
        case MENU_BGM_BACKEND_MP3:
            return mp3player_is_playing();
        case MENU_BGM_BACKEND_WAV64:
            return mixer_ch_playing(SOUND_MP3_PLAYER_CHANNEL);
        default:
            return false;
    }
}

static void menu_bgm_stop_playback (void) {
    switch (menu_bgm_backend) {
        case MENU_BGM_BACKEND_MP3:
            if (mp3player_is_playing()) {
                mp3player_stop();
            }
            break;
        case MENU_BGM_BACKEND_WAV64:
            mixer_ch_stop(SOUND_MP3_PLAYER_CHANNEL);
            break;
        default:
            break;
    }
}

static mp3player_err_t menu_bgm_load_mp3_file (menu_t *menu, const char *file_name) {
    if (!menu_bgm_mp3_open) {
        mp3player_err_t init_err = mp3player_init();
        if (init_err != MP3PLAYER_OK) {
            debugf("Menu BGM MP3 init failed (%d)\n", init_err);
            return init_err;
        }
        menu_bgm_mp3_open = true;
    }

    char *resolved = menu_bgm_resolve_path(menu, file_name);
    if (!resolved) {
        return MP3PLAYER_ERR_NO_FILE;
    }

    mp3player_err_t err = mp3player_load(resolved);
    free(resolved);
    if (err == MP3PLAYER_OK) {
        menu_bgm_backend = MENU_BGM_BACKEND_MP3;
    }
    return err;
}

static bool menu_bgm_load_wav64_file (menu_t *menu, const char *file_name) {
    char *resolved = menu_bgm_resolve_path(menu, file_name);
    if (!resolved) {
        return false;
    }

    wav64_open(&menu_bgm_wav64, resolved);
    if (menu_bgm_wav64.wave.read == NULL) {
        free(resolved);
        wav64_close(&menu_bgm_wav64);
        return false;
    }
    wav64_set_loop(&menu_bgm_wav64, true);
    memset(&menu_bgm_wav64_wrap, 0, sizeof(menu_bgm_wav64_wrap));
    menu_bgm_wav64_wrap.wave = menu_bgm_wav64.wave;
    menu_bgm_wav64_wrap.inner_wave = &menu_bgm_wav64.wave;
    menu_bgm_wav64_wrap.inner_read = menu_bgm_wav64.wave.read;
    menu_bgm_wav64_wrap.inner_start = menu_bgm_wav64.wave.start;
    menu_bgm_wav64_wrap.inner_ctx = menu_bgm_wav64.wave.ctx;
    menu_bgm_wav64_wrap.wave.start = menu_bgm_wav64_meter_start;
    menu_bgm_wav64_wrap.wave.read = menu_bgm_wav64_meter_read;
    menu_bgm_wav64_wrap.wave.ctx = &menu_bgm_wav64_wrap;
    menu_bgm_wav64_wrap.wave.__uuid = 0;
    menu_bgm_wav64_open = true;
    menu_bgm_backend = MENU_BGM_BACKEND_WAV64;
    sound_bgm_meter_reset();
    free(resolved);
    return true;
}

static mp3player_err_t menu_bgm_try_load_any (menu_t *menu, const char *file_name) {
    if (!file_name || !file_name[0]) {
        return MP3PLAYER_ERR_NO_FILE;
    }

    if (path_has_ext_ci(file_name, ".wav64")) {
        return menu_bgm_load_wav64_file(menu, file_name) ? MP3PLAYER_OK : MP3PLAYER_ERR_NO_FILE;
    }
    if (path_has_ext_ci(file_name, ".mp3")) {
        return menu_bgm_load_mp3_file(menu, file_name);
    }

    if (menu_bgm_load_wav64_file(menu, file_name)) {
        return MP3PLAYER_OK;
    }
    return menu_bgm_load_mp3_file(menu, file_name);
}

/** BGM init state machine — tries one fallback path per frame to avoid spikes. */
static int menu_bgm_init_step = -1;

static const char *menu_bgm_get_init_candidate(menu_t *menu, int step) {
    switch (step) {
        case 0:
            if (menu->runtime_bgm_override_file && menu->runtime_bgm_override_file[0] != '\0')
                return menu->runtime_bgm_override_file;
            if (menu->settings.bgm_file && menu->settings.bgm_file[0] != '\0')
                return menu->settings.bgm_file;
            return NULL;
        case 1: return MENU_BGM_WAV64_FILE;
        case 2: return MENU_BGM_MP3_FILE;
        case 3: return MENU_BGM_WAV64_FILE_FALLBACK;
        case 4: return MENU_BGM_MP3_FILE_FALLBACK;
        default: return NULL;
    }
}

#define MENU_BGM_INIT_STEPS 5

static void menu_bgm_init (menu_t *menu) {
    if (menu_bgm_loaded || menu_bgm_error) {
        menu_bgm_init_step = -1;
        return;
    }

    if (menu_bgm_init_step < 0) {
        return;
    }

    while (menu_bgm_init_step < MENU_BGM_INIT_STEPS) {
        const char *candidate = menu_bgm_get_init_candidate(menu, menu_bgm_init_step);
        menu_bgm_init_step++;

        if (!candidate) {
            continue;
        }

        mp3player_err_t err = menu_bgm_try_load_any(menu, candidate);
        if (err == MP3PLAYER_OK) {
            menu_bgm_loaded = true;
            menu_bgm_initialized = true;
            menu_bgm_init_step = -1;
            if (menu_bgm_perf_pending) {
                browser_playlist_perf_note_bgm_reload(0);
                menu_bgm_perf_pending = false;
            }
            return;
        }
        if (err != MP3PLAYER_ERR_NO_FILE) {
            menu_bgm_error = true;
            menu_bgm_initialized = true;
            menu_bgm_init_step = -1;
            debugf("Menu BGM disabled: failed to load BGM (%d)\n", err);
            return;
        }
        // Try next candidate next frame
        return;
    }

    // Exhausted all candidates
    menu_bgm_initialized = true;
    menu_bgm_init_step = -1;
    if (menu_bgm_perf_pending) {
        browser_playlist_perf_note_bgm_reload(0);
        menu_bgm_perf_pending = false;
    }
}

static void menu_bgm_poll (menu_t *menu) {
    if (menu->bgm_reload_requested) {
        menu_bgm_deinit();
        menu->bgm_reload_requested = false;
        menu_bgm_perf_pending = true;
        menu_bgm_init_step = 0;
    }

    if (!menu->settings.bgm_enabled) {
        if (menu_bgm_initialized) {
            menu_bgm_stop_playback();
        }
        sound_bgm_meter_reset();
        return;
    }

    bool decoder_busy = png_decoder_is_busy();
    bool loading_or_booting =
        (menu->mode == MENU_MODE_MUSIC_PLAYER) ||
        (menu->mode == MENU_MODE_MANUAL_VIEWER) ||
        decoder_busy ||
        (menu->mode == MENU_MODE_BOOT) ||
        (menu->next_mode == MENU_MODE_BOOT);

    if (loading_or_booting) {
        if (menu_bgm_initialized) {
            menu_bgm_stop_playback();
        }
        sound_bgm_meter_reset();
        return;
    }

    if (!menu_bgm_initialized && menu_bgm_init_step < 0) {
        menu_bgm_init_step = 0;
    }
    menu_bgm_init(menu);
    if (!menu_bgm_initialized || !menu_bgm_loaded || menu_bgm_error) {
        return;
    }

    if (menu_bgm_backend == MENU_BGM_BACKEND_WAV64) {
        if (!menu_bgm_is_playing()) {
            sound_init_default();
            mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &menu_bgm_wav64_wrap.wave);
            mixer_ch_set_vol(SOUND_MP3_PLAYER_CHANNEL, 0.8f, 0.8f);
        }
        return;
    }

    if (menu_bgm_backend == MENU_BGM_BACKEND_MP3) {
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
        } else {
            mp3player_meter_t m = {0};
            if (mp3player_get_meter(&m)) {
                sound_bgm_meter_t sm = {
                    .peak_l = m.peak_l, .peak_r = m.peak_r,
                    .avg_l = m.avg_l, .avg_r = m.avg_r,
                    .valid = m.valid,
                };
                sound_bgm_meter_set(&sm);
            }
        }
    }
}

static void menu_bgm_deinit (void) {
    if (!menu_bgm_initialized) {
        return;
    }

    menu_bgm_stop_playback();
    if (menu_bgm_mp3_open) {
        mp3player_deinit();
        menu_bgm_mp3_open = false;
    }
    if (menu_bgm_backend == MENU_BGM_BACKEND_WAV64 && menu_bgm_wav64_open) {
        wav64_close(&menu_bgm_wav64);
        menu_bgm_wav64_open = false;
    }
    menu_bgm_backend = MENU_BGM_BACKEND_NONE;
    sound_bgm_meter_reset();
    menu_bgm_initialized = false;
    menu_bgm_loaded = false;
    menu_bgm_error = false;
    menu_bgm_perf_pending = false;
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
    gl_init();
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

    virtual_pak_init(menu->storage_prefix);
  
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
    ui_components_background_set_visualizer(menu->settings.background_visualizer_enabled);
    ui_components_background_set_visualizer_style(menu->settings.background_visualizer_style);
    ui_components_background_set_visualizer_intensity(menu->settings.background_visualizer_intensity);
    sound_bgm_meter_enable(menu->settings.background_visualizer_enabled);
    ui_components_set_selected_row_shimmer(menu->settings.selected_row_shimmer_enabled);
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
    if (menu->settings.browser_random_mode < 0 || menu->settings.browser_random_mode > 4) {
        menu->settings.browser_random_mode = 0;
    }
    menu->browser.sort_mode = (browser_sort_t)menu->settings.browser_sort_mode;

    debugf("N64FlashcartMenu debugging...\n");
}

typedef const struct {
    menu_mode_t id;
    void (*init) (menu_t *menu);
    void (*show) (menu_t *menu, surface_t *display);
    void (*deinit) (menu_t *menu);
} view_t;

static view_t *menu_get_view (menu_mode_t id);

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

    screensaver_deinit();

    path_free(menu->load.disk_slots.primary.disk_path);
    path_free(menu->load.rom_path);
    free(menu->runtime_bgm_override_file);
    view_t *current = menu_get_view(menu->mode);
    if (current && current->deinit) {
        current->deinit(menu);
    }
    path_free(menu->browser.picker_root);
    path_free(menu->browser.directory);
    free(menu);

    display_close();

    sound_deinit();

    gl_close();
    rdpq_close();
    rspq_close();
    rtc_close();
    timer_close();
    joypad_close();

    flashcart_deinit();
}

static view_t menu_views[] = {
    { .id = MENU_MODE_STARTUP, .init = view_startup_init, .show = view_startup_display },
    { .id = MENU_MODE_BROWSER, .init = view_browser_init, .show = view_browser_display, .deinit = view_browser_deinit },
    { .id = MENU_MODE_FILE_INFO, .init = view_file_info_init, .show = view_file_info_display },
    { .id = MENU_MODE_SYSTEM_INFO, .init = view_system_info_init, .show = view_system_info_display },
    { .id = MENU_MODE_IMAGE_VIEWER, .init = view_image_viewer_init, .show = view_image_viewer_display },
    { .id = MENU_MODE_TEXT_VIEWER, .init = view_text_viewer_init, .show = view_text_viewer_display },
    { .id = MENU_MODE_MANUAL_VIEWER, .init = view_manual_viewer_init, .show = view_manual_viewer_display },
    { .id = MENU_MODE_MUSIC_PLAYER, .init = view_music_player_init, .show = view_music_player_display },
    { .id = MENU_MODE_CREDITS, .init = view_credits_init, .show = view_credits_display },
    { .id = MENU_MODE_SETTINGS_EDITOR, .init = view_settings_init, .show = view_settings_display },
    { .id = MENU_MODE_RTC, .init = view_rtc_init, .show = view_rtc_display },
    { .id = MENU_MODE_CONTROLLER_PAKFS, .init = view_controller_pakfs_init, .show = view_controller_pakfs_display },
    { .id = MENU_MODE_VIRTUAL_PAK_CENTER, .init = view_virtual_pak_center_init, .show = view_virtual_pak_center_display },
    { .id = MENU_MODE_CONTROLLER_PAK_DUMP_INFO, .init = view_controller_pak_dump_info_init, .show = view_controller_pak_dump_info_display },
    { .id = MENU_MODE_CONTROLLER_PAK_DUMP_NOTE_INFO, .init = view_controller_pak_note_dump_info_init, .show = view_controller_pak_note_dump_info_display },
    { .id = MENU_MODE_FLASHCART, .init = view_flashcart_info_init, .show = view_flashcart_info_display },
    { .id = MENU_MODE_LOAD_ROM, .init = view_load_rom_init, .show = view_load_rom_display },
    { .id = MENU_MODE_LOAD_DISK, .init = view_load_disk_init, .show = view_load_disk_display },
    { .id = MENU_MODE_LOAD_EMULATOR, .init = view_load_emulator_init, .show = view_load_emulator_display },
    { .id = MENU_MODE_ERROR, .init = view_error_init, .show = view_error_display },
    { .id = MENU_MODE_FAULT, .init = view_fault_init, .show = view_fault_display },
    { .id = MENU_MODE_FAVORITE, .init = view_favorite_init, .show = view_favorite_display },
    { .id = MENU_MODE_HISTORY, .init = view_history_init, .show = view_history_display },
    { .id = MENU_MODE_PLAYTIME, .init = view_playtime_init, .show = view_playtime_display },
    { .id = MENU_MODE_DATEL_CODE_EDITOR, .init = view_datel_code_editor_init, .show = view_datel_code_editor_display },
    { .id = MENU_MODE_EXTRACT_FILE, .init = view_extract_file_init, .show = view_extract_file_display },
};

/**
 * @brief Get the view structure for the specified menu mode.
 * 
 * @param id The menu mode ID.
 * @return view_t* Pointer to the view structure.
 */
static view_t *menu_get_view (menu_mode_t id) {
    for (size_t i = 0; i < sizeof(menu_views) / sizeof(view_t); i++) {
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

    static uint32_t menu_frame_counter = 0;
    while (true) {
        menu_frame_counter++;
        surface_t *display = display_try_get();

        if (display != NULL) {
            actions_update(menu);
            screensaver_update_state(menu);
            screensaver_apply_fps_limit(menu);

            if (screensaver_is_active()) {
                screensaver_draw(menu, display);
                if ((menu_frame_counter & 31) == 0) time(&menu->current_time);
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
                view_t *prev_view = menu_get_view(menu->mode);
                if (prev_view && prev_view->deinit) {
                    prev_view->deinit(menu);
                }
                menu->mode = menu->next_mode;

                view_t *next_view = menu_get_view(menu->next_mode);
                if (next_view && next_view->init) {
                    next_view->init(menu);
                }
            }

            if ((menu_frame_counter & 31) == 0) time(&menu->current_time);
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
