/**
 * @file settings.h
 * @brief Menu Settings
 * @ingroup menu 
 */

#ifndef SETTINGS_H__
#define SETTINGS_H__

#include <stdint.h>


/** @brief Settings Structure */
typedef struct {
    /** @brief Settings version */
    int schema_revision;

    /** @brief First run of the menu */
    bool first_run;

    /** @brief Use 60 Hz refresh rate on a PAL console */
    bool pal60_enabled;
    
    /** @brief Use 60 Hz refresh rate on a PAL console with certain mods that do not properly the video output */
    bool pal60_compatibility_mode;

    /** @brief Direct the VI to force progressive scan output at 240p. Meant for TVs and other devices which struggle to display interlaced video. */
    bool force_progressive_scan;

    /** @brief Show files/directories that are filtered in the browser */
    bool show_protected_entries;

    /** @brief Default directory to navigate to when menu loads */
    char *default_directory;

    /** @brief Put saves into separate directory */
    bool use_saves_folder;

    /** @brief Show saves folder in file browser */ 
    bool show_saves_folder;

    /** @brief Browser sort mode (0=custom, 1=A-Z, 2=Z-A) */
    int browser_sort_mode;

    /** @brief Smart random mode (0=any game, 1=unplayed, 2=underplayed, 3=favorites) */
    int browser_random_mode;

    /** @brief UI theme preset (see ui_components_theme_name()) */
    int ui_theme;

    /** @brief Draw semi-transparent panel behind main content text areas */
    bool text_panel_enabled;

    /** @brief Alpha for text panel (0-255) */
    uint8_t text_panel_alpha;

    /** @brief Hide rom file extensions */    
    bool show_browser_file_extensions;

    /** @brief Hide rom tags */  
    bool show_browser_rom_tags;

    /** @brief Enable Background music */
    bool bgm_enabled;

    /** @brief Absolute SD path to selected menu music file (empty = auto default) */
    char *bgm_file;

    /** @brief Absolute SD path to selected screensaver logo PNG (empty = auto default) */
    char *screensaver_logo_file;

    /** @brief Use 60 FPS while screensaver is active for smoother animation */
    bool screensaver_smooth_mode;

    /** @brief Screensaver visible-area compensation margins (pixels) */
    uint8_t screensaver_margin_left;
    uint8_t screensaver_margin_right;
    uint8_t screensaver_margin_top;
    uint8_t screensaver_margin_bottom;

    /** @brief Enable Sound effects within the menu */
    bool soundfx_enabled;

    /** @brief Enable rumble feedback within the menu */
    bool rumble_enabled;

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    /** @brief Show progress bar when loading a ROM */
    bool loading_progress_bar_enabled;

    /** @brief Enable the ability to bypass the menu and instantly load a ROM on power and reset button */
    bool rom_autoload_enabled;
#else
    /** @brief Enable the ability to bypass the menu and instantly load a ROM on reset button */
    bool rom_fast_reboot_enabled;
#endif

    /** @brief A path to the autoloaded ROM */
    char *rom_autoload_path;

    /** @brief A filename of the autoloaded ROM */
    char *rom_autoload_filename;

} settings_t;


/** @brief Init settings path */
void settings_init (char *path);
/** @brief The settings to load */
void settings_load (settings_t *settings);
/** @brief The settings to save */
void settings_save (settings_t *settings);
/** @brief Reset settings to defaults */
void settings_reset_to_defaults();

#endif
