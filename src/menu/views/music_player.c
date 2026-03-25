#include <string.h>
#include <strings.h>

#include <libdragon.h>

#include "../mp3_player.h"
#include "../sound.h"
#include "views.h"


#define SEEK_SECONDS        (5)
#define SEEK_SECONDS_FAST   (60)

typedef enum {
    MUSIC_BACKEND_NONE,
    MUSIC_BACKEND_MP3,
    MUSIC_BACKEND_WAV64,
} music_backend_t;

static music_backend_t backend = MUSIC_BACKEND_NONE;
static wav64_t wav64_file;
static bool wav64_playing = false;
static bool wav64_open_ok = false;

static bool path_has_ext_ci(const char *path, const char *ext) {
    if (!path || !ext) return false;
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return false;
    return strcasecmp(path + (plen - elen), ext) == 0;
}

static char *convert_error_message (mp3player_err_t err) {
    switch (err) {
        case MP3PLAYER_ERR_OUT_OF_MEM: return "Music player: insufficient memory";
        case MP3PLAYER_ERR_IO: return "I/O error during playback";
        case MP3PLAYER_ERR_NO_FILE: return "No music file is loaded";
        case MP3PLAYER_ERR_INVALID_FILE: return "Invalid music file";
        default: return "Unknown music player error";
    }
}

static void format_elapsed_duration (char *buffer, size_t buf_size, float elapsed, float duration) {
    size_t off = 0;
    buffer[0] = '\0';

    if (duration >= 3600) {
        off += snprintf(buffer + off, buf_size - off, "%02d:", (int) (elapsed) / 3600);
    }
    off += snprintf(buffer + off, buf_size - off, "%02d:%02d", ((int) (elapsed) % 3600) / 60, (int) (elapsed) % 60);

    off += snprintf(buffer + off, buf_size - off, " / ");

    if (duration >= 3600) {
        off += snprintf(buffer + off, buf_size - off, "%02d:", (int) (duration) / 3600);
    }
    snprintf(buffer + off, buf_size - off, "%02d:%02d", ((int) (duration) % 3600) / 60, (int) (duration) % 60);
}

static bool is_playing(void) {
    switch (backend) {
        case MUSIC_BACKEND_MP3: return mp3player_is_playing();
        case MUSIC_BACKEND_WAV64: return wav64_playing && mixer_ch_playing(SOUND_MP3_PLAYER_CHANNEL);
        default: return false;
    }
}

static bool is_finished(void) {
    switch (backend) {
        case MUSIC_BACKEND_MP3: return mp3player_is_finished();
        case MUSIC_BACKEND_WAV64: return wav64_playing && !mixer_ch_playing(SOUND_MP3_PLAYER_CHANNEL);
        default: return true;
    }
}

static void process (menu_t *menu) {
    if (backend == MUSIC_BACKEND_MP3) {
        mp3player_err_t err = mp3player_process();
        if (err != MP3PLAYER_OK) {
            menu_show_error(menu, convert_error_message(err));
            return;
        }
    }

    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    } else if (menu->actions.enter) {
        if (backend == MUSIC_BACKEND_MP3) {
            mp3player_err_t err = mp3player_toggle();
            if (err != MP3PLAYER_OK) {
                menu_show_error(menu, convert_error_message(err));
            }
        } else if (backend == MUSIC_BACKEND_WAV64) {
            if (wav64_playing && mixer_ch_playing(SOUND_MP3_PLAYER_CHANNEL)) {
                mixer_ch_stop(SOUND_MP3_PLAYER_CHANNEL);
                wav64_playing = false;
            } else if (wav64_open_ok) {
                wav64_playing = true;
                mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &wav64_file.wave);
            }
        }
        sound_play_effect(SFX_ENTER);
    } else if (menu->actions.go_left || menu->actions.go_right) {
        if (backend == MUSIC_BACKEND_MP3) {
            int seconds = menu->actions.go_fast ? SEEK_SECONDS_FAST : SEEK_SECONDS;
            mp3player_err_t err = mp3player_seek(menu->actions.go_left ? (-seconds) : seconds);
            if (err != MP3PLAYER_OK) {
                menu_show_error(menu, convert_error_message(err));
            }
        }
        // WAV64 does not support seeking
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    ui_components_layout_draw();

    if (backend == MUSIC_BACKEND_MP3) {
        ui_components_seekbar_draw(mp3player_get_progress());
    }

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "MUSIC PLAYER\n"
        "\n"
        "%s",
        menu->browser.entry->name
    );

    if (backend == MUSIC_BACKEND_MP3) {
        char formatted_track_elapsed_length[64];
        format_elapsed_duration(
            formatted_track_elapsed_length,
            sizeof(formatted_track_elapsed_length),
            mp3player_get_duration() * mp3player_get_progress(),
            mp3player_get_duration()
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n\n\n\n"
            " Track elapsed / length:\n"
            "  %s\n"
            "\n"
            " Average bitrate:\n"
            "  %.0f kbps\n"
            "\n"
            " Samplerate:\n"
            "  %d Hz",
            formatted_track_elapsed_length,
            mp3player_get_bitrate() / 1000,
            mp3player_get_samplerate()
        );
    } else if (backend == MUSIC_BACKEND_WAV64) {
        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n\n\n\n"
            " Format: WAV64\n"
            " Samplerate: %d Hz\n"
            " Channels: %d",
            wav64_file.wave.frequency,
            wav64_file.wave.channels
        );
    }

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "A: %s\n"
        "B: Back\n",
        is_playing() ? "Pause" : is_finished() ? "Play again" : "Play"
    );

    if (backend == MUSIC_BACKEND_MP3) {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "◀ Rewind | Fast forward ▶\n"
        );
    }

    rdpq_detach_show();
}

static void deinit (void) {
    if (backend == MUSIC_BACKEND_MP3) {
        sound_init_default();
        mp3player_deinit();
    } else if (backend == MUSIC_BACKEND_WAV64) {
        mixer_ch_stop(SOUND_MP3_PLAYER_CHANNEL);
        if (wav64_open_ok) {
            wav64_close(&wav64_file);
            wav64_open_ok = false;
        }
        wav64_playing = false;
        sound_init_default();
    }
    backend = MUSIC_BACKEND_NONE;
}


void view_music_player_init (menu_t *menu) {
    backend = MUSIC_BACKEND_NONE;
    wav64_playing = false;
    wav64_open_ok = false;

    path_t *path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    const char *file_path = path_get(path);

    if (path_has_ext_ci(file_path, ".wav64")) {
        wav64_open(&wav64_file, file_path);
        if (wav64_file.wave.read == NULL) {
            wav64_close(&wav64_file);
            menu_show_error(menu, "Failed to open WAV64 file");
            path_free(path);
            return;
        }
        wav64_open_ok = true;
        backend = MUSIC_BACKEND_WAV64;

        sound_init_default();
        mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &wav64_file.wave);
        mixer_ch_set_vol(SOUND_MP3_PLAYER_CHANNEL, 0.8f, 0.8f);
        wav64_playing = true;
    } else {
        // MP3 path (original behavior)
        mp3player_err_t err = mp3player_init();
        if (err != MP3PLAYER_OK) {
            menu_show_error(menu, convert_error_message(err));
            mp3player_deinit();
            path_free(path);
            return;
        }

        err = mp3player_load((char *)file_path);
        if (err != MP3PLAYER_OK) {
            menu_show_error(menu, convert_error_message(err));
            mp3player_deinit();
        } else {
            backend = MUSIC_BACKEND_MP3;
            sound_init_mp3_playback();
            mp3player_mute(false);
            err = mp3player_play();
            if (err != MP3PLAYER_OK) {
                menu_show_error(menu, convert_error_message(err));
                mp3player_deinit();
                backend = MUSIC_BACKEND_NONE;
            }
        }
    }

    path_free(path);
}

void view_music_player_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);

    if (menu->next_mode != MENU_MODE_MUSIC_PLAYER) {
        deinit();
    }
}
