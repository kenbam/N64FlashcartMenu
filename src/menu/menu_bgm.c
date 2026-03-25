/**
 * @file menu_bgm.c
 * @brief Background music subsystem
 * @ingroup menu
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "menu_bgm.h"
#include "menu_state.h"
#include "mp3_player.h"
#include "png_decoder.h"
#include "sound.h"
#include "utils/fs.h"
#include "views/views.h"

#define MENU_BGM_MP3_FILE           "/menu/music/menu.mp3"
#define MENU_BGM_MP3_FILE_FALLBACK  "/menu/music/bgm.mp3"
#define MENU_BGM_WAV64_FILE         "/menu/music/menu.wav64"
#define MENU_BGM_WAV64_FILE_FALLBACK "/menu/music/bgm.wav64"

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

void menu_bgm_poll (menu_t *menu) {
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

void menu_bgm_deinit (void) {
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

void menu_bgm_request_reload (void) {
    menu_bgm_init_step = 0;
}
