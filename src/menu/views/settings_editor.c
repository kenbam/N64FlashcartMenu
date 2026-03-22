#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "../sound.h"
#include "../settings.h"
#include "../path.h"
#include "utils/fs.h"
#include "views.h"

static bool show_message_reset_settings = false;

static component_context_menu_t set_screensaver_style_context_menu;
static component_context_menu_t set_screensaver_smooth_mode_context_menu;
static component_context_menu_t set_screensaver_wait_context_menu;

static const char *format_switch (bool state) {
    switch (state) {
        case true: return "On";
        case false: return "Off";
    }
}

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_loading_progress_bar_enabled_type (menu_t *menu, void *arg) {
    menu->settings.loading_progress_bar_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}
#endif

static void set_protected_entries_type (menu_t *menu, void *arg) {
    menu->settings.show_protected_entries = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}

static void set_use_saves_folder_type (menu_t *menu, void *arg) {
    menu->settings.use_saves_folder = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_show_saves_folder_type (menu_t *menu, void *arg) {
    menu->settings.show_saves_folder = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}

static void set_soundfx_enabled_type (menu_t *menu, void *arg) {
    menu->settings.soundfx_enabled = (bool)(uintptr_t)(arg);
    sound_use_sfx(menu->settings.soundfx_enabled);
    settings_save(&menu->settings);
}

static void set_bgm_enabled_type (menu_t *menu, void *arg) {
    menu->settings.bgm_enabled = (bool)(uintptr_t)(arg);
    menu->bgm_reload_requested = true;
    settings_save(&menu->settings);
}

static void set_menu_music_file_auto (menu_t *menu, void *arg) {
    (void)arg;

    if (menu->settings.bgm_file) {
        free(menu->settings.bgm_file);
    }
    menu->settings.bgm_file = strdup("");
    menu->bgm_reload_requested = true;
    settings_save(&menu->settings);
}

static void open_menu_music_picker (menu_t *menu, void *arg) {
    (void)arg;

    path_t *music_dir = path_init(menu->storage_prefix, "/menu/music");
    directory_create(path_get(music_dir));

    if (menu->browser.directory) {
        path_free(menu->browser.directory);
    }
    menu->browser.directory = music_dir;
    menu->browser.valid = false;
    menu->browser.reload = false;
    menu->browser.picker = BROWSER_PICKER_MENU_BGM;
    if (menu->browser.picker_root) {
        path_free(menu->browser.picker_root);
    }
    menu->browser.picker_root = path_clone(music_dir);
    menu->browser.picker_return_mode = MENU_MODE_SETTINGS_EDITOR;

    if (menu->browser.select_file) {
        path_free(menu->browser.select_file);
        menu->browser.select_file = NULL;
    }

    menu->next_mode = MENU_MODE_BROWSER;
}

static void set_screensaver_logo_file_auto (menu_t *menu, void *arg) {
    (void)arg;

    if (menu->settings.screensaver_logo_file) {
        free(menu->settings.screensaver_logo_file);
    }
    menu->settings.screensaver_logo_file = strdup("");
    menu->screensaver_logo_reload_requested = true;
    settings_save(&menu->settings);
}

static void set_screensaver_style_type (menu_t *menu, void *arg) {
    menu->settings.screensaver_style = (int)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_screensaver_smooth_mode_type (menu_t *menu, void *arg) {
    menu->settings.screensaver_smooth_mode = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_screensaver_wait_seconds_type (menu_t *menu, void *arg) {
    menu->settings.screensaver_wait_seconds = (uint8_t)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_screensaver_margin_left_type (menu_t *menu, void *arg) {
    menu->settings.screensaver_margin_left = (uint8_t)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_screensaver_margin_right_type (menu_t *menu, void *arg) {
    menu->settings.screensaver_margin_right = (uint8_t)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_screensaver_margin_top_type (menu_t *menu, void *arg) {
    menu->settings.screensaver_margin_top = (uint8_t)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_screensaver_margin_bottom_type (menu_t *menu, void *arg) {
    menu->settings.screensaver_margin_bottom = (uint8_t)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void open_screensaver_logo_picker (menu_t *menu, void *arg) {
    (void)arg;

    path_t *logos_dir = path_init(menu->storage_prefix, "/menu/screensavers");
    directory_create(path_get(logos_dir));

    if (menu->browser.directory) {
        path_free(menu->browser.directory);
    }
    menu->browser.directory = logos_dir;
    menu->browser.valid = false;
    menu->browser.reload = false;
    menu->browser.picker = BROWSER_PICKER_SCREENSAVER_LOGO;
    if (menu->browser.picker_root) {
        path_free(menu->browser.picker_root);
    }
    menu->browser.picker_root = path_clone(logos_dir);
    menu->browser.picker_return_mode = MENU_MODE_SETTINGS_EDITOR;

    if (menu->browser.select_file) {
        path_free(menu->browser.select_file);
        menu->browser.select_file = NULL;
    }

    menu->next_mode = MENU_MODE_BROWSER;
}

static void set_text_panel_enabled_type (menu_t *menu, void *arg) {
    menu->settings.text_panel_enabled = (bool)(uintptr_t)(arg);
    ui_components_set_text_panel(menu->settings.text_panel_enabled, menu->settings.text_panel_alpha);
    settings_save(&menu->settings);
}

static void set_text_panel_alpha_type (menu_t *menu, void *arg) {
    menu->settings.text_panel_alpha = (uint8_t)(uintptr_t)(arg);
    ui_components_set_text_panel(menu->settings.text_panel_enabled, menu->settings.text_panel_alpha);
    settings_save(&menu->settings);
}

static void set_ui_theme_type(menu_t *menu, void *arg) {
    int theme = (int)(uintptr_t)arg;
    int max_theme = ui_components_theme_count() - 1;
    if (theme < 0 || theme > max_theme) {
        theme = 0;
    }
    menu->settings.ui_theme = theme;
    ui_components_set_theme(menu->settings.ui_theme);
    settings_save(&menu->settings);
}

static void open_background_picker (menu_t *menu, void *arg) {
    (void)arg;

    path_t *backgrounds_dir = path_init(menu->storage_prefix, "/menu/backgrounds");
    directory_create(path_get(backgrounds_dir));

    if (menu->browser.directory) {
        path_free(menu->browser.directory);
    }
    menu->browser.directory = backgrounds_dir;
    menu->browser.valid = false;
    menu->browser.reload = false;

    if (menu->browser.select_file) {
        path_free(menu->browser.select_file);
        menu->browser.select_file = NULL;
    }

    menu->next_mode = MENU_MODE_BROWSER;
}

#ifndef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_use_rom_fast_reboot_enabled_type (menu_t *menu, void *arg) {
    menu->settings.rom_fast_reboot_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}
#endif

static void set_background_visualizer_enabled_type (menu_t *menu, void *arg) {
    menu->settings.background_visualizer_enabled = (bool)(uintptr_t)(arg);
    ui_components_background_set_visualizer(menu->settings.background_visualizer_enabled);
    sound_bgm_meter_enable(menu->settings.background_visualizer_enabled);
    settings_save(&menu->settings);
}

static void set_background_visualizer_style_type (menu_t *menu, void *arg) {
    menu->settings.background_visualizer_style = (int)(uintptr_t)(arg);
    ui_components_background_set_visualizer_style(menu->settings.background_visualizer_style);
    settings_save(&menu->settings);
}

static void set_background_visualizer_intensity_type (menu_t *menu, void *arg) {
    menu->settings.background_visualizer_intensity = (int)(uintptr_t)(arg);
    ui_components_background_set_visualizer_intensity(menu->settings.background_visualizer_intensity);
    settings_save(&menu->settings);
}

static void set_selected_row_shimmer_enabled_type (menu_t *menu, void *arg) {
    menu->settings.selected_row_shimmer_enabled = (bool)(uintptr_t)(arg);
    ui_components_set_selected_row_shimmer(menu->settings.selected_row_shimmer_enabled);
    settings_save(&menu->settings);
}

#ifdef BETA_SETTINGS
static void set_pal60_type (menu_t *menu, void *arg) {
    menu->settings.pal60_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_mod_pal60_compatibility_type (menu_t *menu, void *arg) {
    menu->settings.pal60_compatibility_mode = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_show_browser_file_extensions_type(menu_t *menu, void *arg) {
    menu->settings.show_browser_file_extensions = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
    menu->browser.reload = true;
}

static void set_show_browser_rom_tags_type (menu_t *menu, void *arg) {
    menu->settings.show_browser_rom_tags = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}
static void set_rumble_enabled_type (menu_t *menu, void *arg) {
    menu->settings.rumble_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

// static void set_use_default_settings (menu_t *menu, void *arg) {
//     // FIXME: add implementation
//     menu->browser.reload = true;
// }
#endif

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static int get_loading_progress_bar_enabled_current_selection (menu_t *menu) {
    return menu->settings.loading_progress_bar_enabled ? 0 : 1;
}

static component_context_menu_t set_loading_progress_bar_enabled_context_menu = {
    .get_default_selection = get_loading_progress_bar_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_loading_progress_bar_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_loading_progress_bar_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static int get_protected_entries_current_selection (menu_t *menu) {
    return menu->settings.show_protected_entries ? 0 : 1;
}

static component_context_menu_t set_protected_entries_type_context_menu = {
    .get_default_selection = get_protected_entries_current_selection,
    .list = {
        {.text = "On", .action = set_protected_entries_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_protected_entries_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_soundfx_enabled_current_selection (menu_t *menu) {
    return menu->settings.soundfx_enabled ? 0 : 1;
}

static component_context_menu_t set_soundfx_enabled_type_context_menu = {
    .get_default_selection = get_soundfx_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_soundfx_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_soundfx_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_bgm_enabled_current_selection (menu_t *menu) {
    return menu->settings.bgm_enabled ? 0 : 1;
}

static component_context_menu_t set_bgm_enabled_type_context_menu = {
    .get_default_selection = get_bgm_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_bgm_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_bgm_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_menu_music_file_context_menu = {
    .list = {
        {.text = "Auto (menu.wav64/menu.mp3/bgm.wav64/bgm.mp3)", .action = set_menu_music_file_auto },
        {.text = "Pick from /menu/music", .action = open_menu_music_picker },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_screensaver_logo_file_context_menu = {
    .list = {
        {.text = "Auto (DVD logo)", .action = set_screensaver_logo_file_auto },
        {.text = "Pick from /menu/screensavers", .action = open_screensaver_logo_picker },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t screensaver_general_context_menu = {
    .list = {
        {.text = "Type", .submenu = &set_screensaver_style_context_menu },
        {.text = "Logo", .submenu = &set_screensaver_logo_file_context_menu },
        {.text = "Wait", .submenu = &set_screensaver_wait_context_menu },
        {.text = "Smooth", .submenu = &set_screensaver_smooth_mode_context_menu },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_screensaver_style_current_selection (menu_t *menu) {
    switch (menu->settings.screensaver_style) {
        case SCREENSAVER_STYLE_PIPES:
            return 1;
        case SCREENSAVER_STYLE_GRADIENT:
            return 2;
        case SCREENSAVER_STYLE_ATTRACT:
            return 3;
        case SCREENSAVER_STYLE_PIPES_GL:
            return 4;
        case SCREENSAVER_STYLE_RANDOM:
            return 5;
        case SCREENSAVER_STYLE_DVD:
        default:
            return 0;
    }
}

static component_context_menu_t set_screensaver_style_context_menu = {
    .get_default_selection = get_screensaver_style_current_selection,
    .list = {
        {.text = "DVD Logo", .action = set_screensaver_style_type, .arg = (void *)(uintptr_t)(SCREENSAVER_STYLE_DVD) },
        {.text = "3D Pipes", .action = set_screensaver_style_type, .arg = (void *)(uintptr_t)(SCREENSAVER_STYLE_PIPES) },
        {.text = "Living Gradient", .action = set_screensaver_style_type, .arg = (void *)(uintptr_t)(SCREENSAVER_STYLE_GRADIENT) },
        {.text = "Attract Mode", .action = set_screensaver_style_type, .arg = (void *)(uintptr_t)(SCREENSAVER_STYLE_ATTRACT) },
        {.text = "GL Pipes (Experimental)", .action = set_screensaver_style_type, .arg = (void *)(uintptr_t)(SCREENSAVER_STYLE_PIPES_GL) },
        {.text = "Random", .action = set_screensaver_style_type, .arg = (void *)(uintptr_t)(SCREENSAVER_STYLE_RANDOM) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_screensaver_smooth_mode_current_selection (menu_t *menu) {
    return menu->settings.screensaver_smooth_mode ? 0 : 1;
}

static component_context_menu_t set_screensaver_smooth_mode_context_menu = {
    .get_default_selection = get_screensaver_smooth_mode_current_selection,
    .list = {
        {.text = "On (60 FPS)", .action = set_screensaver_smooth_mode_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off (30 FPS)", .action = set_screensaver_smooth_mode_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_screensaver_wait_current_selection(menu_t *menu) {
    int seconds = menu->settings.screensaver_wait_seconds;
    if (seconds <= 5) return 0;
    if (seconds <= 10) return 1;
    if (seconds <= 15) return 2;
    if (seconds <= 30) return 3;
    if (seconds <= 45) return 4;
    if (seconds <= 60) return 5;
    if (seconds <= 120) return 6;
    return 7;
}

static component_context_menu_t set_screensaver_wait_context_menu = {
    .get_default_selection = get_screensaver_wait_current_selection,
    .list = {
        {.text = "5 sec", .action = set_screensaver_wait_seconds_type, .arg = (void *)(uintptr_t)(5) },
        {.text = "10 sec", .action = set_screensaver_wait_seconds_type, .arg = (void *)(uintptr_t)(10) },
        {.text = "15 sec", .action = set_screensaver_wait_seconds_type, .arg = (void *)(uintptr_t)(15) },
        {.text = "30 sec", .action = set_screensaver_wait_seconds_type, .arg = (void *)(uintptr_t)(30) },
        {.text = "45 sec", .action = set_screensaver_wait_seconds_type, .arg = (void *)(uintptr_t)(45) },
        {.text = "60 sec", .action = set_screensaver_wait_seconds_type, .arg = (void *)(uintptr_t)(60) },
        {.text = "2 min", .action = set_screensaver_wait_seconds_type, .arg = (void *)(uintptr_t)(120) },
        {.text = "5 min", .action = set_screensaver_wait_seconds_type, .arg = (void *)(uintptr_t)(300) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_screensaver_margin_selection(int v) {
    if (v <= 0) return 0;
    if (v <= 2) return 1;
    if (v <= 4) return 2;
    if (v <= 8) return 3;
    if (v <= 12) return 4;
    if (v <= 16) return 5;
    if (v <= 24) return 6;
    return 7;
}
static int get_screensaver_margin_left_current_selection (menu_t *menu) {
    return get_screensaver_margin_selection(menu->settings.screensaver_margin_left);
}
static int get_screensaver_margin_right_current_selection (menu_t *menu) {
    return get_screensaver_margin_selection(menu->settings.screensaver_margin_right);
}
static int get_screensaver_margin_top_current_selection (menu_t *menu) {
    return get_screensaver_margin_selection(menu->settings.screensaver_margin_top);
}
static int get_screensaver_margin_bottom_current_selection (menu_t *menu) {
    return get_screensaver_margin_selection(menu->settings.screensaver_margin_bottom);
}

#define SCREEN_MARGIN_MENU(name, getter, setter) \
static component_context_menu_t name = { \
    .get_default_selection = getter, \
    .list = { \
        {.text = "0 px", .action = setter, .arg = (void *)(uintptr_t)(0) }, \
        {.text = "2 px", .action = setter, .arg = (void *)(uintptr_t)(2) }, \
        {.text = "4 px", .action = setter, .arg = (void *)(uintptr_t)(4) }, \
        {.text = "8 px", .action = setter, .arg = (void *)(uintptr_t)(8) }, \
        {.text = "12 px", .action = setter, .arg = (void *)(uintptr_t)(12) }, \
        {.text = "16 px", .action = setter, .arg = (void *)(uintptr_t)(16) }, \
        {.text = "24 px", .action = setter, .arg = (void *)(uintptr_t)(24) }, \
        {.text = "32 px", .action = setter, .arg = (void *)(uintptr_t)(32) }, \
    COMPONENT_CONTEXT_MENU_LIST_END, \
}};

SCREEN_MARGIN_MENU(set_screensaver_margin_left_context_menu, get_screensaver_margin_left_current_selection, set_screensaver_margin_left_type)
SCREEN_MARGIN_MENU(set_screensaver_margin_right_context_menu, get_screensaver_margin_right_current_selection, set_screensaver_margin_right_type)
SCREEN_MARGIN_MENU(set_screensaver_margin_top_context_menu, get_screensaver_margin_top_current_selection, set_screensaver_margin_top_type)
SCREEN_MARGIN_MENU(set_screensaver_margin_bottom_context_menu, get_screensaver_margin_bottom_current_selection, set_screensaver_margin_bottom_type)

static component_context_menu_t screensaver_dvd_bounds_context_menu = {
    .list = {
        {.text = "Left Margin", .submenu = &set_screensaver_margin_left_context_menu },
        {.text = "Right Margin", .submenu = &set_screensaver_margin_right_context_menu },
        {.text = "Top Margin", .submenu = &set_screensaver_margin_top_context_menu },
        {.text = "Bottom Margin", .submenu = &set_screensaver_margin_bottom_context_menu },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#undef SCREEN_MARGIN_MENU

static int get_text_panel_enabled_current_selection (menu_t *menu) {
    return menu->settings.text_panel_enabled ? 0 : 1;
}

static component_context_menu_t set_text_panel_enabled_type_context_menu = {
    .get_default_selection = get_text_panel_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_text_panel_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_text_panel_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_text_panel_alpha_current_selection (menu_t *menu) {
    if (menu->settings.text_panel_alpha <= 48) return 0;
    if (menu->settings.text_panel_alpha <= 80) return 1;
    if (menu->settings.text_panel_alpha <= 112) return 2;
    if (menu->settings.text_panel_alpha <= 144) return 3;
    return 4;
}

static component_context_menu_t set_text_panel_alpha_context_menu = {
    .get_default_selection = get_text_panel_alpha_current_selection,
    .list = {
        {.text = "Very Low", .action = set_text_panel_alpha_type, .arg = (void *)(uintptr_t)(48) },
        {.text = "Low", .action = set_text_panel_alpha_type, .arg = (void *)(uintptr_t)(80) },
        {.text = "Medium", .action = set_text_panel_alpha_type, .arg = (void *)(uintptr_t)(112) },
        {.text = "High", .action = set_text_panel_alpha_type, .arg = (void *)(uintptr_t)(144) },
        {.text = "Very High", .action = set_text_panel_alpha_type, .arg = (void *)(uintptr_t)(176) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_ui_theme_current_selection(menu_t *menu) {
    int max_theme = ui_components_theme_count() - 1;
    if (menu->settings.ui_theme < 0 || menu->settings.ui_theme > max_theme) {
        return 0;
    }
    return menu->settings.ui_theme;
}

static component_context_menu_t set_ui_theme_context_menu = {
    .get_default_selection = get_ui_theme_current_selection,
    .list = {
        {.text = "Classic", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(0) },
        {.text = "Solarized", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(1) },
        {.text = "Gruvbox", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(2) },
        {.text = "CRT Green", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(3) },
        {.text = "Retrowave", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(4) },
        {.text = "Nord", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(5) },
        {.text = "Dracula", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(6) },
        {.text = "Amber Terminal", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(7) },
        {.text = "Ocean Teal", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(8) },
        {.text = "Monokai", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(9) },
        {.text = "One Dark", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(10) },
        {.text = "Tokyo Night", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(11) },
        {.text = "Catppuccin", .action = set_ui_theme_type, .arg = (void *)(uintptr_t)(12) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_use_saves_folder_current_selection (menu_t *menu) {
    return menu->settings.use_saves_folder ? 0 : 1;
}

static component_context_menu_t set_use_saves_folder_type_context_menu = {
    .get_default_selection = get_use_saves_folder_current_selection,
    .list = {
        {.text = "On", .action = set_use_saves_folder_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_use_saves_folder_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_saves_folder_current_selection (menu_t *menu) {
    return menu->settings.show_saves_folder ? 0 : 1;
}

static component_context_menu_t set_show_saves_folder_type_context_menu = {
    .get_default_selection = get_show_saves_folder_current_selection,
    .list = {
        {.text = "On", .action = set_show_saves_folder_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_saves_folder_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

#ifndef FEATURE_AUTOLOAD_ROM_ENABLED
static int get_use_rom_fast_reboot_current_selection (menu_t *menu) {
    return menu->settings.rom_fast_reboot_enabled ? 0 : 1;
}

static component_context_menu_t set_use_rom_fast_reboot_context_menu = {
    .get_default_selection = get_use_rom_fast_reboot_current_selection,
    .list = {
        {.text = "On", .action = set_use_rom_fast_reboot_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_use_rom_fast_reboot_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

#ifdef BETA_SETTINGS
static int get_pal60_current_selection (menu_t *menu) {
    return menu->settings.pal60_enabled ? 0 : 1;
}

static component_context_menu_t set_pal60_type_context_menu = {
    .get_default_selection = get_pal60_current_selection,
    .list = {
        {.text = "On", .action = set_pal60_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_pal60_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_pal60_mod_compatibility_current_selection (menu_t *menu) {
    return menu->settings.pal60_compatibility_mode ? 0 : 1;
}

static component_context_menu_t set_pal60_mod_compatibility_type_context_menu = {
    .get_default_selection = get_pal60_mod_compatibility_current_selection,
    .list = {
        {.text = "On", .action = set_mod_pal60_compatibility_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_mod_pal60_compatibility_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_browser_file_extensions_current_selection (menu_t *menu) {
    return menu->settings.show_browser_file_extensions ? 0 : 1;
}

static component_context_menu_t set_show_browser_file_extensions_context_menu = {
    .get_default_selection = get_show_browser_file_extensions_current_selection,
    .list = {
        { .text = "On", .action = set_show_browser_file_extensions_type, .arg = (void *)(uintptr_t)(true) },
        { .text = "Off", .action = set_show_browser_file_extensions_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_browser_rom_tags_current_selection (menu_t *menu) {
    return menu->settings.show_browser_rom_tags ? 0 : 1;
}

static component_context_menu_t set_show_browser_rom_tags_context_menu = {
    .get_default_selection = get_show_browser_rom_tags_current_selection,
    .list = {
        {.text = "On", .action = set_show_browser_rom_tags_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_browser_rom_tags_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static int get_background_visualizer_enabled_current_selection (menu_t *menu) {
    return menu->settings.background_visualizer_enabled ? 0 : 1;
}

static component_context_menu_t set_background_visualizer_enabled_type_context_menu = {
    .get_default_selection = get_background_visualizer_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_background_visualizer_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_background_visualizer_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_background_visualizer_style_current_selection (menu_t *menu) {
    int style = menu->settings.background_visualizer_style;
    if (style < 0 || style > 2) {
        return 0;
    }
    return style;
}

static component_context_menu_t set_background_visualizer_style_context_menu = {
    .get_default_selection = get_background_visualizer_style_current_selection,
    .list = {
        {.text = "Bars", .action = set_background_visualizer_style_type, .arg = (void *)(uintptr_t)(0) },
        {.text = "Pulse Wash", .action = set_background_visualizer_style_type, .arg = (void *)(uintptr_t)(1) },
        {.text = "Sunburst", .action = set_background_visualizer_style_type, .arg = (void *)(uintptr_t)(2) },
        {.text = "Oscilloscope", .action = set_background_visualizer_style_type, .arg = (void *)(uintptr_t)(3) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_background_visualizer_intensity_current_selection (menu_t *menu) {
    int v = menu->settings.background_visualizer_intensity;
    if (v < 0 || v > 2) return 1;
    return v;
}

static component_context_menu_t set_background_visualizer_intensity_context_menu = {
    .get_default_selection = get_background_visualizer_intensity_current_selection,
    .list = {
        {.text = "Subtle", .action = set_background_visualizer_intensity_type, .arg = (void *)(uintptr_t)(0) },
        {.text = "Normal", .action = set_background_visualizer_intensity_type, .arg = (void *)(uintptr_t)(1) },
        {.text = "Full", .action = set_background_visualizer_intensity_type, .arg = (void *)(uintptr_t)(2) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_selected_row_shimmer_enabled_current_selection (menu_t *menu) {
    return menu->settings.selected_row_shimmer_enabled ? 0 : 1;
}

static component_context_menu_t set_selected_row_shimmer_enabled_context_menu = {
    .get_default_selection = get_selected_row_shimmer_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_selected_row_shimmer_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_selected_row_shimmer_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

#ifdef BETA_SETTINGS
static int get_rumble_enabled_current_selection (menu_t *menu) {
    return menu->settings.rumble_enabled ? 0 : 1;
}

static component_context_menu_t set_rumble_enabled_type_context_menu = {
    .get_default_selection = get_rumble_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_rumble_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_rumble_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static component_context_menu_t options_context_menu = { .list = {
    { .text = "Show Hidden Files", .submenu = &set_protected_entries_type_context_menu },
    { .text = "Sound Effects", .submenu = &set_soundfx_enabled_type_context_menu },
    { .text = "Background Music", .submenu = &set_bgm_enabled_type_context_menu },
    { .text = "Menu Music File", .submenu = &set_menu_music_file_context_menu },
    { .text = "Screensaver", .submenu = &screensaver_general_context_menu },
    { .text = "DVD Logo Bounds", .submenu = &screensaver_dvd_bounds_context_menu },
    { .text = "Use Saves Folder", .submenu = &set_use_saves_folder_type_context_menu },
    { .text = "Show Saves Folder", .submenu = &set_show_saves_folder_type_context_menu },
    { .text = "Text Panel Overlay", .submenu = &set_text_panel_enabled_type_context_menu },
    { .text = "Text Panel Strength", .submenu = &set_text_panel_alpha_context_menu },
    { .text = "Theme Preset", .submenu = &set_ui_theme_context_menu },
    { .text = "Visualizer Background", .submenu = &set_background_visualizer_enabled_type_context_menu },
    { .text = "Visualizer Style", .submenu = &set_background_visualizer_style_context_menu },
    { .text = "Visualizer Intensity", .submenu = &set_background_visualizer_intensity_context_menu },
    { .text = "Selected Row Shimmer", .submenu = &set_selected_row_shimmer_enabled_context_menu },
    { .text = "Pick Background Image", .action = open_background_picker },
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { .text = "ROM Loading Bar", .submenu = &set_loading_progress_bar_enabled_context_menu },
#else
    { .text = "Fast Reboot ROM", .submenu = &set_use_rom_fast_reboot_context_menu },
#endif
#ifdef BETA_SETTINGS
    { .text = "PAL60 Mode", .submenu = &set_pal60_type_context_menu },
    { .text = "PAL60 Compatibility", .submenu = &set_pal60_mod_compatibility_type_context_menu },
    { .text = "Hide ROM Extensions", .submenu = &set_show_browser_file_extensions_context_menu },
    { .text = "Hide ROM Tags", .submenu = &set_show_browser_rom_tags_context_menu },
    { .text = "Rumble Feedback", .submenu = &set_rumble_enabled_type_context_menu },
    // { .text = "Restore Defaults", .action = set_use_default_settings },
#endif

    COMPONENT_CONTEXT_MENU_LIST_END,
}};


static void process (menu_t *menu) {
    if (ui_components_context_menu_process(menu, &options_context_menu)) {
        return;
    }

    if (menu->actions.enter) {
        if (show_message_reset_settings) {
            settings_reset_to_defaults();
            menu_show_error(menu, "Reboot N64 to take effect!");
            show_message_reset_settings = false;
        } else {
            ui_components_context_menu_show(&options_context_menu);
        }
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.back) {
        if (show_message_reset_settings) {
            show_message_reset_settings = false;
        } else {
            menu->next_mode = MENU_MODE_BROWSER;
        }
        sound_play_effect(SFX_EXIT);
    } else if (menu->actions.options){
        show_message_reset_settings = true;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    const char *bgm_file_label = "Auto";
    if (menu->settings.bgm_file && menu->settings.bgm_file[0] != '\0') {
        bgm_file_label = file_basename(menu->settings.bgm_file);
    }
    const char *screensaver_logo_label = "Auto";
    if (menu->settings.screensaver_logo_file && menu->settings.screensaver_logo_file[0] != '\0') {
        screensaver_logo_label = file_basename(menu->settings.screensaver_logo_file);
    }
    const char *screensaver_style_label = "DVD Logo";
    switch (menu->settings.screensaver_style) {
        case SCREENSAVER_STYLE_PIPES:
            screensaver_style_label = "3D Pipes";
            break;
        case SCREENSAVER_STYLE_GRADIENT:
            screensaver_style_label = "Living Gradient";
            break;
        case SCREENSAVER_STYLE_ATTRACT:
            screensaver_style_label = "Attract Mode";
            break;
        case SCREENSAVER_STYLE_PIPES_GL:
            screensaver_style_label = "GL Pipes (Exp)";
            break;
        case SCREENSAVER_STYLE_RANDOM:
            screensaver_style_label = "Random";
            break;
        default:
            break;
    }
    const char *screensaver_smooth_label = menu->settings.screensaver_smooth_mode ? "On (60)" : "Off (30)";
    char screensaver_wait_label[16];
    int wait_seconds = menu->settings.screensaver_wait_seconds;
    if (wait_seconds >= 60 && (wait_seconds % 60) == 0) {
        snprintf(screensaver_wait_label, sizeof(screensaver_wait_label), "%d min", wait_seconds / 60);
    } else {
        snprintf(screensaver_wait_label, sizeof(screensaver_wait_label), "%d sec", wait_seconds);
    }
    const char *visualizer_style_label = "Bars";
    switch (menu->settings.background_visualizer_style) {
        case 1: visualizer_style_label = "Pulse Wash"; break;
        case 2: visualizer_style_label = "Sunburst"; break;
        case 3: visualizer_style_label = "Oscilloscope"; break;
        default: break;
    }
    const char *visualizer_intensity_label = "Normal";
    switch (menu->settings.background_visualizer_intensity) {
        case 0: visualizer_intensity_label = "Subtle"; break;
        case 2: visualizer_intensity_label = "Full"; break;
        default: break;
    }

    rdpq_attach(d, NULL);

    ui_components_background_draw();

    ui_components_layout_draw();

	ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "MENU SETTINGS EDITOR\n"
        "\n"
    );

ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n\n"
        "  Default Directory : %s\n\n"
        "To change the following menu settings, press 'A':\n"
        "     Show Hidden Files : %s\n"
        "     Sound Effects     : %s\n"
        "     Background Music  : %s\n"
        "     Menu Music File   : %s\n"
        "     Screensaver       : %s\n"
        "     Saver Logo        : %s\n"
        "     Saver Wait        : %s\n"
        "     Saver Smooth      : %s\n"
        "     DVD Bounds L/R    : %d / %d\n"
        "     DVD Bounds T/B    : %d / %d\n"
        "     Use Saves folder  : %s\n"
        "     Show Saves folder : %s\n"
        "     Text Panel Overlay: %s\n"
        "     Text Panel Str    : %d\n"
        "     Theme Preset      : %s\n"
        "     Visualizer BG     : %s\n"
        "     Visualizer Style  : %s\n"
        "     Visualizer Int    : %s\n"
        "     Row Shimmer       : %s\n"
        "     Background Picker : Use A menu\n"
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
        "  Autoload ROM      : %s\n\n"
        "    ROM Loading Bar   : %s\n"
#else
        "     Fast Reboot ROM   : %s\n"
#endif
#ifdef BETA_SETTINGS
        "*    PAL60 Mode        : %s\n"
        "*    PAL60 Mod Compat  : %s\n"
        "     Hide ROM Extension: %s\n"
        "     Hide ROM Tags     : %s\n"
        "     Rumble Feedback   : %s\n"
        "\n\n"
        "Note: Certain settings have the following caveats:\n"
        "*    Requires rebooting the N64 Console.\n"
#endif
        ,
        menu->settings.default_directory,
        format_switch(menu->settings.show_protected_entries),
        format_switch(menu->settings.soundfx_enabled),
        format_switch(menu->settings.bgm_enabled),
        bgm_file_label,
        screensaver_style_label,
        screensaver_logo_label,
        screensaver_wait_label,
        screensaver_smooth_label,
        (int)menu->settings.screensaver_margin_left,
        (int)menu->settings.screensaver_margin_right,
        (int)menu->settings.screensaver_margin_top,
        (int)menu->settings.screensaver_margin_bottom,
        format_switch(menu->settings.use_saves_folder),
        format_switch(menu->settings.show_saves_folder),
        format_switch(menu->settings.text_panel_enabled),
        (int)menu->settings.text_panel_alpha,
        ui_components_theme_name(menu->settings.ui_theme),
        format_switch(menu->settings.background_visualizer_enabled),
        visualizer_style_label,
        visualizer_intensity_label,
        format_switch(menu->settings.selected_row_shimmer_enabled),
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
        format_switch(menu->settings.rom_autoload_enabled),
        format_switch(menu->settings.loading_progress_bar_enabled)
#else
        format_switch(menu->settings.rom_fast_reboot_enabled)
#endif
#ifdef BETA_SETTINGS
        ,
        format_switch(menu->settings.pal60_enabled),
        format_switch(menu->settings.pal60_compatibility_mode),
        format_switch(menu->settings.show_browser_file_extensions),
        format_switch(menu->settings.show_browser_rom_tags),
        format_switch(menu->settings.rumble_enabled)
#endif
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "A: Change\n"
        "B: Back"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_RIGHT, VALIGN_TOP,
        "R: Reset settings\n"
        "\n"
    );

    ui_components_context_menu_draw(&options_context_menu);

    if (show_message_reset_settings) {
        ui_components_messagebox_draw(
            "Reset settings?\n\n"
            "A: Yes, B: Back"
        );
    }

    rdpq_detach_show();
}


void view_settings_init (menu_t *menu) {
    ui_components_context_menu_init(&options_context_menu);

}

void view_settings_display (menu_t *menu, surface_t *display) {
    process(menu);
    
    draw(menu, display);
}
