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
#include "menu_bgm.h"
#include "menu_state.h"
#include "menu.h"
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
