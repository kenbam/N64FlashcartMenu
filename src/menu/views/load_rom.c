#include "../bookkeeping.h"
#include "../cart_load.h"
#include "../combo_disk_flow.h"
#include "../datel_codes.h"
#include "../playtime.h"
#include "../rom_info.h"
#include "../sound.h"
#include "../virtual_pak.h"
#include "boot/boot.h"
#include "utils/fs.h"
#include "views.h"
#include "../ui_components/constants.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include <libdragon.h>

#include "../native_image.h"

static bool show_extra_info_message = false;
static component_boxart_t *boxart;
static char *rom_filename = NULL;
static int details_scroll = 0;
static int details_max_scroll = 0;
static rdpq_paragraph_t *details_layout = NULL;
static bool boxart_retry_pending = false;
static bool metadata_directory_cached = false;
static bool metadata_directory_available = false;
static char cached_metadata_directory[64];

static int16_t current_metadata_image_index = 0;
static const file_image_type_t metadata_image_filename_cache[] = {
    IMAGE_BOXART_FRONT,
    IMAGE_BOXART_BACK,
    IMAGE_BOXART_LEFT,
    IMAGE_BOXART_RIGHT,
    IMAGE_BOXART_TOP,
    IMAGE_BOXART_BOTTOM,
    IMAGE_GAMEPAK_FRONT,
    IMAGE_GAMEPAK_BACK
};
static const uint16_t metadata_image_filename_cache_length = sizeof(metadata_image_filename_cache) / sizeof(metadata_image_filename_cache[0]);
static bool metadata_image_available[sizeof(metadata_image_filename_cache) / sizeof(metadata_image_filename_cache[0])] = {false};
static bool metadata_images_scanned = false;

// Pre-computed per-ROM display data (avoid per-frame I/O).
static bool rom_display_data_valid = false;
static char cached_total_buf[64];
static char cached_last_session_buf[64];
static char cached_last_played_buf[64];
static char cached_recent_sessions_buf[512];
static char cached_save_health_buf[128];
static char cached_save_modified_buf[64];
static bool cached_has_manual = false;

static void refresh_display_cache(menu_t *menu);

static void invalidate_metadata_directory_cache(void) {
    metadata_directory_cached = false;
    metadata_directory_available = false;
    cached_metadata_directory[0] = '\0';
}

static bool resolve_metadata_directory_for_rom (path_t *path, const char game_code[4], char *resolved, size_t resolved_size) {
    if ((path == NULL) || (game_code == NULL)) {
        return false;
    }

    char candidate[32];

    snprintf(candidate, sizeof(candidate), "%c/%c/%c/%c", game_code[0], game_code[1], game_code[2], game_code[3]);
    path_push(path, candidate);
    if (directory_exists(path_get(path))) {
        if (resolved && resolved_size > 0) {
            snprintf(resolved, resolved_size, "%s", candidate);
        }
        return true;
    }
    path_pop(path);

    const char fallback_regions[] = { 'E', 'P', 'J', 'U', 'A', '\0' };
    for (size_t i = 0; fallback_regions[i] != '\0'; i++) {
        if (fallback_regions[i] == game_code[3]) {
            continue;
        }
        snprintf(candidate, sizeof(candidate), "%c/%c/%c/%c", game_code[0], game_code[1], game_code[2], fallback_regions[i]);
        path_push(path, candidate);
        if (directory_exists(path_get(path))) {
            if (resolved && resolved_size > 0) {
                snprintf(resolved, resolved_size, "%s", candidate);
            }
            return true;
        }
        path_pop(path);
    }

    snprintf(candidate, sizeof(candidate), "%c/%c/%c", game_code[0], game_code[1], game_code[2]);
    path_push(path, candidate);
    if (directory_exists(path_get(path))) {
        if (resolved && resolved_size > 0) {
            snprintf(resolved, resolved_size, "%s", candidate);
        }
        return true;
    }
    path_pop(path);

    return false;
}

static bool resolve_metadata_directory_for_current_rom(menu_t *menu, char *resolved, size_t resolved_size) {
    if (!menu) {
        return false;
    }

    if (!metadata_directory_cached) {
        cached_metadata_directory[0] = '\0';
        metadata_directory_available = false;

        path_t *path = path_init(menu->storage_prefix, "menu/metadata");
        if (path != NULL) {
            char game_code_path[64] = {0};

            if (menu->load.rom_info.game_code[1] == 'E' && menu->load.rom_info.game_code[2] == 'D') {
                char safe_title[21];
                memcpy(safe_title, menu->load.rom_info.title, 20);
                safe_title[20] = '\0';
                for (char *p = safe_title; *p; p++) {
                    if (*p == '/' || *p == '\\' || *p < 0x20) {
                        *p = '_';
                    }
                }

                snprintf(game_code_path, sizeof(game_code_path), "homebrew/%s", safe_title);
                path_push(path, game_code_path);
                metadata_directory_available = directory_exists(path_get(path));
                path_pop(path);
            } else {
                metadata_directory_available = resolve_metadata_directory_for_rom(
                    path,
                    menu->load.rom_info.game_code,
                    game_code_path,
                    sizeof(game_code_path)
                );
                if (metadata_directory_available) {
                    path_pop(path);
                }
            }

            if (metadata_directory_available) {
                snprintf(cached_metadata_directory, sizeof(cached_metadata_directory), "%s", game_code_path);
            }

            path_free(path);
        }

        metadata_directory_cached = true;
    }

    if (!metadata_directory_available) {
        return false;
    }

    if (resolved && resolved_size > 0) {
        snprintf(resolved, resolved_size, "%s", cached_metadata_directory);
    }

    return true;
}

static bool resolve_existing_rom_path(const char *storage_prefix, const char *current_path, char *out, size_t out_len) {
    if (!current_path || current_path[0] == '\0' || !out || out_len == 0) {
        return false;
    }
    if (file_exists((char *)current_path)) {
        snprintf(out, out_len, "%s", current_path);
        return true;
    }
    if (storage_prefix && current_path[0] == '/') {
        path_t *prefixed = path_init(storage_prefix, (char *)current_path);
        if (prefixed && file_exists(path_get(prefixed))) {
            snprintf(out, out_len, "%s", path_get(prefixed));
            path_free(prefixed);
            return true;
        }
        path_free(prefixed);
    }
    return false;
}

static bool resolve_manual_directory_for_current_rom (menu_t *menu, path_t **out_manual_directory, const char *subdirectory) {
    if (!menu || !out_manual_directory) {
        return false;
    }

    path_t *metadata_directory = path_init(menu->storage_prefix, "menu/metadata");
    if (!metadata_directory) {
        return false;
    }

    char metadata_rel[64];
    if (!resolve_metadata_directory_for_current_rom(menu, metadata_rel, sizeof(metadata_rel))) {
        path_free(metadata_directory);
        return false;
    }
    path_push(metadata_directory, metadata_rel);

    path_push(metadata_directory, "manual");
    if (subdirectory && subdirectory[0] != '\0') {
        path_push(metadata_directory, (char *)subdirectory);
    }
    if (!directory_exists(path_get(metadata_directory))) {
        path_free(metadata_directory);
        return false;
    }

    path_t *manifest_path = path_clone(metadata_directory);
    path_push(manifest_path, "manifest.ini");
    bool ok = file_exists(path_get(manifest_path));
    path_free(manifest_path);
    if (!ok) {
        path_free(metadata_directory);
        return false;
    }

    *out_manual_directory = metadata_directory;
    return true;
}

static bool resolve_bookkeeping_rom_path(menu_t *menu, bookkeeping_item_t *item) {
    if (!menu || !item) {
        return false;
    }

    const char *current_path = item->primary_path ? path_get(item->primary_path) : NULL;
    char resolved_path[512];
    if (resolve_existing_rom_path(menu->storage_prefix, current_path, resolved_path, sizeof(resolved_path))) {
        if (!item->primary_path || strcmp(path_get(item->primary_path), resolved_path) != 0) {
            if (item->primary_path) {
                path_free(item->primary_path);
            }
            item->primary_path = path_create(resolved_path);
            bookkeeping_save(&menu->bookkeeping);
        }
        return true;
    }
    if (item->game_id[0] == '\0') {
        return false;
    }

    if (!rom_info_resolve_stable_id_path(menu->storage_prefix, item->game_id, current_path, resolved_path, sizeof(resolved_path))) {
        return false;
    }

    if (item->primary_path) {
        path_free(item->primary_path);
    }
    item->primary_path = path_create(resolved_path);
    bookkeeping_save(&menu->bookkeeping);
    return true;
}

static void scan_metadata_images(menu_t *menu) {
    if (metadata_images_scanned) {
        return;
    }

    path_t *path = path_init(menu->storage_prefix, "menu/metadata"); // should be METADATA_BASE_DIRECTORY
    char game_code_path[64] = {0};
    bool dir_exists = (path != NULL) &&
        resolve_metadata_directory_for_current_rom(menu, game_code_path, sizeof(game_code_path));

    if (dir_exists) {
        path_push(path, game_code_path);
    }

    if (dir_exists) {
        // Filenames array matches metadata_image_filename_cache order for indexed access
        // Note: This mapping is also present in boxart.c but duplicated here
        // for efficient scanning without calling into the component layer
        char *filenames[] = {
            "boxart_front.png",
            "boxart_back.png",
            "boxart_left.png",
            "boxart_right.png",
            "boxart_top.png",
            "boxart_bottom.png",
            "gamepak_front.png",
            "gamepak_back.png"
        };

        for (uint16_t i = 0; i < metadata_image_filename_cache_length; i++) {
            path_push(path, filenames[i]);
            metadata_image_available[i] = file_exists(path_get(path))
                || native_image_sidecar_exists(path_get(path), ".nimg");
            path_pop(path);
        }
    } else {
        // No directory exists, mark all images as unavailable
        for (uint16_t i = 0; i < metadata_image_filename_cache_length; i++) {
            metadata_image_available[i] = false;
        }
    }

    debugf("Metadata: Scanned metadata for ROM ID %s. \n", game_code_path);

    path_free(path);
    metadata_images_scanned = true;
}

static void retry_boxart_load (menu_t *menu) {
    if (!boxart_retry_pending || boxart != NULL) {
        return;
    }

    scan_metadata_images(menu);

    if (!metadata_image_available[current_metadata_image_index]) {
        for (uint16_t i = 0; i < metadata_image_filename_cache_length; i++) {
            if (metadata_image_available[i]) {
                current_metadata_image_index = i;
                break;
            }
        }
    }

    if (!metadata_image_available[current_metadata_image_index]) {
        boxart_retry_pending = false;
        return;
    }

    boxart = ui_components_boxart_init_memory_cached(
        menu->storage_prefix,
        menu->load.rom_info.game_code,
        menu->load.rom_info.title,
        metadata_image_filename_cache[current_metadata_image_index]
    );
    if (boxart == NULL) {
        boxart = ui_components_boxart_init_async(
            menu->storage_prefix,
            menu->load.rom_info.game_code,
            menu->load.rom_info.title,
            metadata_image_filename_cache[current_metadata_image_index]
        );
    }

    if (boxart != NULL) {
        boxart_retry_pending = false;
    }
}

static const char *format_rom_description(menu_t *menu) {
    if (menu->load.rom_info.metadata.long_desc[0] != '\0') {
        return menu->load.rom_info.metadata.long_desc;
    }

    if (menu->load.rom_info.metadata.short_desc[0] != '\0') {
        return menu->load.rom_info.metadata.short_desc;
    }

    return "No description available.";
}

static void append_detail_section(char *buffer, size_t buffer_length, const char *title, const char *body) {
    if ((buffer == NULL) || (buffer_length == 0) || (title == NULL) || (body == NULL) || (body[0] == '\0')) {
        return;
    }

    size_t used = strlen(buffer);
    if (used >= (buffer_length - 1)) {
        return;
    }

    snprintf(buffer + used, buffer_length - used, "%s:\n\t%s\n\n", title, body);
}

static char *convert_error_message (rom_err_t err) {
    switch (err) {
        case ROM_ERR_LOAD_IO: return "I/O error during loading ROM information and/or options";
        case ROM_ERR_SAVE_IO: return "I/O error during storing ROM options";
        case ROM_ERR_NO_FILE: return "Couldn't open ROM file";
        default: return "Unknown ROM info load error";
    }
}

static const char *format_rom_endianness (rom_endianness_t endianness) {
    switch (endianness) {
        case ENDIANNESS_BIG: return "Big (default)";
        case ENDIANNESS_LITTLE: return "Little (unsupported)";
        case ENDIANNESS_BYTE_SWAP: return "Byte swapped";
        default: return "Unknown";
    }
}

static const char *format_rom_media_type (rom_category_type_t media_type) {
    switch (media_type) {
        case N64_CART: return "Cartridge";
        case N64_DISK: return "Disk";
        case N64_CART_EXPANDABLE: return "Cartridge (Expandable)";
        case N64_DISK_EXPANDABLE: return "Disk (Expandable)";
        case N64_ALECK64: return "Aleck64";
        default: return "Unknown";
    }
}

static const char *format_rom_destination_market (rom_destination_type_t market_type) {
    // TODO: These are all assumptions and should be corrected if required.
    // From http://n64devkit.square7.ch/info/submission/pal/01-01.html
    switch (market_type) {
        case MARKET_JAPANESE_MULTI: return "Japanese & English"; // 1080 Snowboarding JPN
        case MARKET_BRAZILIAN: return "Brazilian (Portuguese)";
        case MARKET_CHINESE: return "Chinese";
        case MARKET_GERMAN: return "German";
        case MARKET_NORTH_AMERICA: return "American English";
        case MARKET_FRENCH: return "French";
        case MARKET_DUTCH: return "Dutch";
        case MARKET_ITALIAN: return "Italian";
        case MARKET_JAPANESE: return "Japanese";
        case MARKET_KOREAN: return "Korean";
        case MARKET_CANADIAN: return "Canadaian (English & French)";
        case MARKET_SPANISH: return "Spanish";
        case MARKET_AUSTRALIAN: return "Australian (English)";
        case MARKET_SCANDINAVIAN: return "Scandinavian";
        case MARKET_GATEWAY64_NTSC: return "LodgeNet/Gateway (NTSC)";
        case MARKET_GATEWAY64_PAL: return "LodgeNet/Gateway (PAL)";
        case MARKET_EUROPEAN_BASIC: return "PAL (includes English)"; // Mostly EU but is used on some Australian ROMs
        case MARKET_OTHER_X: return "Regional (non specific)"; // FIXME: AUS HSV Racing ROM's and Asia Top Gear Rally use this so not only EUR
        case MARKET_OTHER_Y: return "European (non specific)";
        case MARKET_OTHER_Z: return "Regional (unknown)";
        default: return "Unknown";
    }
}

static const char *format_rom_save_type (rom_save_type_t save_type, bool supports_cpak) {
    switch (save_type) {
        case SAVE_TYPE_NONE: return supports_cpak ? "Controller PAK" : "None";
        case SAVE_TYPE_EEPROM_4KBIT: return supports_cpak ?   "EEPROM 4kbit | Controller PAK" : "EEPROM 4kbit";
        case SAVE_TYPE_EEPROM_16KBIT: return supports_cpak ?  "EEPROM 16kbit | Controller PAK" : "EEPROM 16kbit";
        case SAVE_TYPE_SRAM_256KBIT: return supports_cpak ?   "SRAM 256kbit | Controller PAK" : "SRAM 256kbit";
        case SAVE_TYPE_SRAM_BANKED: return supports_cpak ?    "SRAM 768kbit / 3 banks | Controller PAK" : "SRAM 768kbit / 3 banks";
        case SAVE_TYPE_SRAM_1MBIT: return supports_cpak ?     "SRAM 1Mbit | Controller PAK" : "SRAM 1Mbit";
        case SAVE_TYPE_FLASHRAM_1MBIT: return supports_cpak ? "FlashRAM 1Mbit | Controller PAK" : "FlashRAM 1Mbit";
        case SAVE_TYPE_FLASHRAM_PKST2: return supports_cpak ? "FlashRAM (Pokemon Stadium 2) | Controller PAK" : "FlashRAM (Pokemon Stadium 2)";
        default: return "Unknown";
    }
}

static const char *format_rom_tv_type (rom_tv_type_t tv_type) {
    switch (tv_type) {
        case ROM_TV_TYPE_PAL: return "PAL";
        case ROM_TV_TYPE_NTSC: return "NTSC";
        case ROM_TV_TYPE_MPAL: return "MPAL";
        default: return "Unknown";
    }
}

static const char *format_rom_expansion_pak_info (rom_expansion_pak_t expansion_pak_info) {
    switch (expansion_pak_info) {
        case EXPANSION_PAK_REQUIRED: return "Required";
        case EXPANSION_PAK_RECOMMENDED: return "Recommended";
        case EXPANSION_PAK_SUGGESTED: return "Suggested";
        case EXPANSION_PAK_FAULTY: return "May require ROM patch";
        default: return "Not required";
    }
}

static const char *format_rom_pak_feature_info (bool pak_feature_info) {
    if (pak_feature_info) {
        return "Supported";
    } else {
        return "Not used";
    }
}

static const char *format_cic_type (rom_cic_type_t cic_type) {
    switch (cic_type) {
        case ROM_CIC_TYPE_5101: return "5101";
        case ROM_CIC_TYPE_5167: return "5167";
        case ROM_CIC_TYPE_6101: return "6101";
        case ROM_CIC_TYPE_7102: return "7102";
        case ROM_CIC_TYPE_x102: return "6102 / 7101";
        case ROM_CIC_TYPE_x103: return "6103 / 7103";
        case ROM_CIC_TYPE_x105: return "6105 / 7105";
        case ROM_CIC_TYPE_x106: return "6106 / 7106";
        case ROM_CIC_TYPE_8301: return "8301";
        case ROM_CIC_TYPE_8302: return "8302";
        case ROM_CIC_TYPE_8303: return "8303";
        case ROM_CIC_TYPE_8401: return "8401";
        case ROM_CIC_TYPE_8501: return "8501";
        default: return "Unknown";
    }
}

static const char *format_esrb_age_rating (rom_esrb_age_rating_t esrb_age_rating) {
    switch (esrb_age_rating) {
        case ROM_ESRB_AGE_RATING_NONE: return "None";
        case ROM_ESRB_AGE_RATING_EVERYONE: return "Everyone";
        case ROM_ESRB_AGE_RATING_EVERYONE_10_PLUS: return "Everyone 10+";
        case ROM_ESRB_AGE_RATING_TEEN: return "Teen";
        case ROM_ESRB_AGE_RATING_MATURE: return "Mature";
        case ROM_ESRB_AGE_RATING_ADULT: return "Adults Only";
        default: return "Unknown";
    }
}

static inline const char *format_boolean_type (bool bool_value) {
    return bool_value ? "On" : "Off";
}

static void set_cic_type (menu_t *menu, void *arg) {
    rom_cic_type_t cic_type = (rom_cic_type_t) (arg);
    rom_err_t err = rom_config_override_cic_type(menu->load.rom_path, &menu->load.rom_info, cic_type);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
    }
    refresh_display_cache(menu);
    menu->browser.reload = true;
}

static void set_save_type (menu_t *menu, void *arg) {
    rom_save_type_t save_type = (rom_save_type_t) (arg);
    rom_err_t err = rom_config_override_save_type(menu->load.rom_path, &menu->load.rom_info, save_type);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
    }
    refresh_display_cache(menu);
    menu->browser.reload = true;
}

static void set_tv_type (menu_t *menu, void *arg) {
    rom_tv_type_t tv_type = (rom_tv_type_t) (arg);
    rom_err_t err = rom_config_override_tv_type(menu->load.rom_path, &menu->load.rom_info, tv_type);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
    }
    refresh_display_cache(menu);
    menu->browser.reload = true;
}
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_autoload_type (menu_t *menu, void *arg) {
    free(menu->settings.rom_autoload_path);
    menu->settings.rom_autoload_path = strdup(strip_fs_prefix(path_get(menu->browser.directory)));
    free(menu->settings.rom_autoload_filename);
    menu->settings.rom_autoload_filename = strdup(menu->browser.entry->name);
    // FIXME: add a confirmation box here! (press start on reboot)
    menu->settings.rom_autoload_enabled = true;
    settings_save(&menu->settings);
    menu->browser.reload = true;
}
#endif

static void set_cheat_option(menu_t *menu, void *arg) {
    debugf("Load Rom: setting cheat option to %d\n", (int)arg);
    if (!is_memory_expanded()) {
        // If the Expansion pak is not installed, we cannot use cheats, and force it to off (just incase).
        rom_config_setting_set_cheats(menu->load.rom_path, &menu->load.rom_info, false);
        refresh_display_cache(menu);
        menu->browser.reload = true;
    }
    else {
        bool enabled = (bool)arg;
        rom_config_setting_set_cheats(menu->load.rom_path, &menu->load.rom_info, enabled);
        refresh_display_cache(menu);
        menu->browser.reload = true;
    }
}

#ifdef FEATURE_PATCHER_GUI_ENABLED
static void set_patcher_option(menu_t *menu, void *arg) {
    bool enabled = (bool)arg;
    rom_config_setting_set_patches(menu->load.rom_path, &menu->load.rom_info, enabled);
    refresh_display_cache(menu);
    menu->browser.reload = true;
}

static void append_manifest_name_unique(char names[][128], int *count, const char *name) {
    if (!name || !name[0] || !names || !count) {
        return;
    }
    for (int i = 0; i < *count; i++) {
        if (strcasecmp(names[i], name) == 0) {
            return;
        }
    }
    if (*count >= 32) {
        return;
    }
    snprintf(names[*count], 128, "%s", name);
    (*count)++;
}

static void scan_patch_profiles_in_dir(path_t *dir_path, char names[][128], int *count) {
    if (!dir_path || !directory_exists(path_get(dir_path))) {
        return;
    }
    dir_t info;
    int result = dir_findfirst(path_get(dir_path), &info);
    while (result == 0) {
        if ((info.d_type != DT_DIR) && file_has_extensions(info.d_name, (const char *[]){"ini", NULL})) {
            append_manifest_name_unique(names, count, info.d_name);
        }
        result = dir_findnext(path_get(dir_path), &info);
    }
}

static bool find_next_patch_profile(menu_t *menu, char *out, size_t out_len) {
    char c0[2] = { menu->load.rom_info.game_code[0], '\0' };
    char c1[2] = { menu->load.rom_info.game_code[1], '\0' };
    char c2[2] = { menu->load.rom_info.game_code[2], '\0' };
    char c3[2] = { menu->load.rom_info.game_code[3], '\0' };

    char names[32][128];
    int count = 0;

    path_t *region_dir = path_init(menu->storage_prefix, "menu/patches");
    path_push(region_dir, c0);
    path_push(region_dir, c1);
    path_push(region_dir, c2);
    path_push(region_dir, c3);
    scan_patch_profiles_in_dir(region_dir, names, &count);

    path_t *global_dir = path_clone(region_dir);
    path_pop(global_dir);
    scan_patch_profiles_in_dir(global_dir, names, &count);

    path_free(region_dir);
    path_free(global_dir);

    if (count <= 0) {
        return false;
    }

    for (int i = 1; i < count; i++) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "%s", names[i]);
        int j = i - 1;
        while (j >= 0 && strcasecmp(names[j], tmp) > 0) {
            snprintf(names[j + 1], sizeof(names[j + 1]), "%s", names[j]);
            j--;
        }
        snprintf(names[j + 1], sizeof(names[j + 1]), "%s", tmp);
    }

    const char *current = menu->load.rom_info.settings.patch_profile;
    int current_idx = -1;
    for (int i = 0; i < count; i++) {
        if (current && current[0] && strcasecmp(names[i], current) == 0) {
            current_idx = i;
            break;
        }
    }

    int next_idx = (current_idx + 1) % count;
    snprintf(out, out_len, "%s", names[next_idx]);
    return true;
}

static void set_next_patch_profile(menu_t *menu, void *arg) {
    (void)arg;
    char next_profile[128];
    if (!find_next_patch_profile(menu, next_profile, sizeof(next_profile))) {
        menu_show_error(menu, "No patch profiles found");
        return;
    }
    rom_err_t err = rom_config_setting_set_patch_profile(menu->load.rom_path, &menu->load.rom_info, next_profile);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
        return;
    }
    sound_play_effect(SFX_SETTING);
    refresh_display_cache(menu);
    menu->browser.reload = true;
}
#endif

static void add_favorite (menu_t *menu, void *arg) {
    bookkeeping_favorite_add(&menu->bookkeeping, menu->load.rom_path, NULL, BOOKKEEPING_TYPE_ROM);
}

static void set_virtual_pak_enabled(menu_t *menu, void *arg) {
    bool enabled = (arg != NULL);
    if (!menu->load.rom_info.features.controller_pak) {
        menu_show_error(menu, "This game doesn't use a Controller Pak");
        return;
    }
    rom_err_t err = rom_config_setting_set_virtual_pak_enabled(menu->load.rom_path, &menu->load.rom_info, enabled);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
        return;
    }
    sound_play_effect(SFX_SETTING);
    refresh_display_cache(menu);
}

static void set_next_virtual_pak_slot(menu_t *menu, void *arg) {
    (void)arg;
    if (!menu->load.rom_info.features.controller_pak) {
        menu_show_error(menu, "This game doesn't use a Controller Pak");
        return;
    }
    uint8_t next_slot = (uint8_t)(menu->load.rom_info.settings.virtual_pak_slot + 1);
    if (next_slot < 1 || next_slot > 4) {
        next_slot = 1;
    }
    rom_err_t err = rom_config_setting_set_virtual_pak_slot(menu->load.rom_path, &menu->load.rom_info, next_slot);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
        return;
    }
    sound_play_effect(SFX_SETTING);
    refresh_display_cache(menu);
}

static void set_previous_virtual_pak_slot(menu_t *menu, void *arg) {
    (void)arg;
    if (!menu->load.rom_info.features.controller_pak) {
        menu_show_error(menu, "This game doesn't use a Controller Pak");
        return;
    }
    uint8_t prev_slot = (menu->load.rom_info.settings.virtual_pak_slot <= 1)
        ? 4
        : (uint8_t)(menu->load.rom_info.settings.virtual_pak_slot - 1);
    rom_err_t err = rom_config_setting_set_virtual_pak_slot(menu->load.rom_path, &menu->load.rom_info, prev_slot);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
        return;
    }
    sound_play_effect(SFX_SETTING);
    refresh_display_cache(menu);
}

static void iterate_metadata_image(menu_t *menu, int direction) {
    scan_metadata_images(menu);

    // Transverse to next/previous available image based on direction (1 = next, -1 = previous)
    int16_t start_metadata_image_index = current_metadata_image_index;
    int16_t new_metadata_image_index = (current_metadata_image_index + direction + metadata_image_filename_cache_length) % metadata_image_filename_cache_length;

    // Find next available image from our cached list
    while (new_metadata_image_index != start_metadata_image_index) {
        if (metadata_image_available[new_metadata_image_index]) {
            // Use async variant to avoid blocking SD I/O during carousel switch.
            component_boxart_t *new_boxart = ui_components_boxart_init_async(
                menu->storage_prefix,
                menu->load.rom_info.game_code,
                menu->load.rom_info.title,
                metadata_image_filename_cache[new_metadata_image_index]
            );

            if (new_boxart != NULL) {
                // Only free old boxart after successful new allocation
                ui_components_boxart_free(boxart);
                boxart = new_boxart;
                current_metadata_image_index = new_metadata_image_index;
                sound_play_effect(SFX_SETTING);
                break;
            }
        }
        new_metadata_image_index = (new_metadata_image_index + direction + metadata_image_filename_cache_length) % metadata_image_filename_cache_length;
    }
}

static component_context_menu_t set_cic_type_context_menu = { .list = {
    {.text = "Automatic", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_AUTOMATIC) },
    {.text = "CIC-6101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_6101) },
    {.text = "CIC-7102", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_7102) },
    {.text = "CIC-6102 / CIC-7101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x102) },
    {.text = "CIC-6103 / CIC-7103", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x103) },
    {.text = "CIC-6105 / CIC-7105", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x105) },
    {.text = "CIC-6106 / CIC-7106", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x106) },
    {.text = "Aleck64 CIC-5101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_5101) },
    {.text = "64DD ROM conversion CIC-5167", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_5167) },
    {.text = "NDDJ0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8301) },
    {.text = "NDDJ1 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8302) },
    {.text = "NDDJ2 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8303) },
    {.text = "NDXJ0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8401) },
    {.text = "NDDE0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8501) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_save_type_context_menu = { .list = {
    { .text = "Automatic", .action = set_save_type, .arg = (void *) (SAVE_TYPE_AUTOMATIC) },
    { .text = "None", .action = set_save_type, .arg = (void *) (SAVE_TYPE_NONE) },
    { .text = "EEPROM 4kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_EEPROM_4KBIT) },
    { .text = "EEPROM 16kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_EEPROM_16KBIT) },
    { .text = "SRAM 256kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_256KBIT) },
    { .text = "SRAM 768kbit / 3 banks", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_BANKED) },
    { .text = "SRAM 1Mbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_1MBIT) },
    { .text = "FlashRAM 1Mbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_FLASHRAM_1MBIT) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_tv_type_context_menu = { .list = {
    { .text = "Automatic", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_AUTOMATIC) },
    { .text = "PAL", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_PAL) },
    { .text = "NTSC", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_NTSC) },
    { .text = "MPAL", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_MPAL) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_cheat_options_menu = { .list = {
    { .text = "Enable", .action = set_cheat_option, .arg = (void *) (true)},
    { .text = "Disable", .action = set_cheat_option, .arg = (void *) (false)},
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_virtual_pak_options_menu = { .list = {
    { .text = "Enable", .action = set_virtual_pak_enabled, .arg = (void *) (true)},
    { .text = "Disable", .action = set_virtual_pak_enabled, .arg = (void *) (false)},
    { .text = "Previous Slot", .action = set_previous_virtual_pak_slot },
    { .text = "Next Slot", .action = set_next_virtual_pak_slot },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

#ifdef FEATURE_PATCHER_GUI_ENABLED
static component_context_menu_t set_patcher_options_menu = { .list = {
    { .text = "Enable", .action = set_patcher_option, .arg = (void *) (true)},
    { .text = "Disable", .action = set_patcher_option, .arg = (void *) (false)},
    { .text = "Next Profile", .action = set_next_patch_profile },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static void set_menu_next_mode (menu_t *menu, void *arg) {
    menu_mode_t next_mode = (menu_mode_t) (arg);
    menu->next_mode = next_mode;
}

static void open_virtual_pak_center(menu_t *menu, void *arg) {
    (void)arg;
    menu->utility_return_mode = MENU_MODE_LOAD_ROM;
    menu->next_mode = MENU_MODE_VIRTUAL_PAK_CENTER;
}

static void manual_prepare_launch (menu_t *menu, path_t *manual_directory) {
    if (menu->manual.directory) {
        path_free(menu->manual.directory);
    }
    if (menu->manual.pages_directory) {
        path_free(menu->manual.pages_directory);
        menu->manual.pages_directory = NULL;
    }
    if (menu->manual.zoom_pages_directory) {
        path_free(menu->manual.zoom_pages_directory);
        menu->manual.zoom_pages_directory = NULL;
    }

    menu->manual.directory = manual_directory;
    menu->manual.return_mode = MENU_MODE_LOAD_ROM;
    menu->next_mode = MENU_MODE_MANUAL_VIEWER;
}

static void open_manual (menu_t *menu, void *arg) {
    (void)arg;

    path_t *manual_directory = NULL;
    if (!resolve_manual_directory_for_current_rom(menu, &manual_directory, NULL)) {
        menu_show_error(menu, "No manual package found for this ROM");
        return;
    }

    manual_prepare_launch(menu, manual_directory);
}

static void load_rom_only(menu_t *menu, void *arg) {
    (void)arg;
    menu->load_pending.rom_file = true;
}

static void launch_with_64dd_disk(menu_t *menu, void *arg) {
    (void)arg;
    combo_disk_flow_launch_required(menu);
}

static void set_default_64dd_disk(menu_t *menu, void *arg) {
    (void)arg;
    combo_disk_flow_set_default(menu);
}

static void clear_default_64dd_disk(menu_t *menu, void *arg) {
    (void)arg;
    combo_disk_flow_clear_default(menu);
}

static component_context_menu_t set_64dd_disk_context_menu = { .list = {
    { .text = "Load ROM Only", .action = load_rom_only },
    { .text = "Launch with 64DD Disk...", .action = launch_with_64dd_disk },
    { .text = "Set Default 64DD Disk...", .action = set_default_64dd_disk },
    { .text = "Clear Default 64DD Disk", .action = clear_default_64dd_disk },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t options_context_menu = { .list = {
    { .text = "Set CIC Type", .submenu = &set_cic_type_context_menu },
    { .text = "Set Save Type", .submenu = &set_save_type_context_menu },
    { .text = "Set TV Type", .submenu = &set_tv_type_context_menu },
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { .text = "Set ROM to autoload", .action = set_autoload_type },
#endif
    { .text = "Use Cheats", .submenu = &set_cheat_options_menu },
    { .text = "Virtual Controller Pak", .submenu = &set_virtual_pak_options_menu },
    { .text = "Virtual Pak Center", .action = open_virtual_pak_center },
    { .text = "Datel Code Editor", .action = set_menu_next_mode, .arg = (void *) (MENU_MODE_DATEL_CODE_EDITOR) },
    { .text = "64DD Disk", .submenu = &set_64dd_disk_context_menu },
    { .text = "Open Manual", .action = open_manual },
#ifdef FEATURE_PATCHER_GUI_ENABLED
    { .text = "Use Patches", .submenu = &set_patcher_options_menu },
#endif
    { .text = "Add to favorites", .action = add_favorite },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static void format_duration (char *out, size_t out_len, uint64_t seconds) {
    uint64_t hrs = seconds / 3600;
    uint64_t mins = (seconds % 3600) / 60;
    uint64_t secs = seconds % 60;
    if (hrs > 0) {
        snprintf(out, out_len, "%lluh %llum %llus", (unsigned long long)hrs, (unsigned long long)mins, (unsigned long long)secs);
    } else if (mins > 0) {
        snprintf(out, out_len, "%llum %llus", (unsigned long long)mins, (unsigned long long)secs);
    } else {
        snprintf(out, out_len, "%llus", (unsigned long long)secs);
    }
}

static void format_last_played (char *out, size_t out_len, int64_t ts) {
    if (ts <= 0) {
        snprintf(out, out_len, "Never");
        return;
    }
    time_t t = (time_t)ts;
    char *s = ctime(&t);
    if (!s) {
        snprintf(out, out_len, "Unknown");
        return;
    }
    // ctime adds trailing newline
    size_t len = strnlen(s, out_len - 1);
    if (len > 0 && s[len - 1] == '\n') {
        len--;
    }
    snprintf(out, out_len, "%.*s", (int)len, s);
}

static size_t get_expected_save_size(rom_save_type_t save_type) {
    switch (save_type) {
        case SAVE_TYPE_EEPROM_4KBIT: return 512;
        case SAVE_TYPE_EEPROM_16KBIT: return 2 * 1024;
        case SAVE_TYPE_SRAM_256KBIT: return 32 * 1024;
        case SAVE_TYPE_SRAM_BANKED: return 96 * 1024;
        case SAVE_TYPE_SRAM_1MBIT: return 128 * 1024;
        case SAVE_TYPE_FLASHRAM_1MBIT: return 128 * 1024;
        case SAVE_TYPE_FLASHRAM_PKST2: return 128 * 1024;
        default: return 0;
    }
}

static const char *format_save_backend(rom_save_type_t save_type, bool supports_cpak) {
    if (save_type == SAVE_TYPE_NONE) {
        return supports_cpak ? "Controller Pak only (game-managed)" : "No persistent save";
    }
    return "SD-backed .sav via flashcart";
}

static const char *format_save_writeback_status(bool save_expected) {
    if (!save_expected) {
        return "N/A";
    }
    return flashcart_has_feature(FLASHCART_FEATURE_SAVE_WRITEBACK) ? "Enabled" : "Unavailable";
}

static bool get_save_file_path(menu_t *menu, char *out, size_t out_len) {
    if (!menu || !menu->load.rom_path || !out || out_len == 0) {
        return false;
    }

    path_t *save_path = path_clone(menu->load.rom_path);
    path_ext_replace(save_path, "sav");
    if (menu->settings.use_saves_folder) {
        path_push_subdir(save_path, SAVE_DIRECTORY_NAME);
    }

    snprintf(out, out_len, "%s", strip_fs_prefix(path_get(save_path)));
    path_free(save_path);
    return true;
}

static void format_save_health(char *out, size_t out_len, const char *save_path, size_t expected_size) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';

    if (!save_path || save_path[0] == '\0') {
        snprintf(out, out_len, "Unknown");
        return;
    }

    path_t *path = path_create(save_path);
    char *full_path = path_get(path);

    if (!file_exists(full_path)) {
        snprintf(out, out_len, "Missing (created on first load)");
        path_free(path);
        return;
    }

    int64_t actual_size = file_get_size(full_path);
    if (actual_size < 0) {
        snprintf(out, out_len, "Present (size unavailable)");
        path_free(path);
        return;
    }

    if (expected_size > 0 && (size_t)actual_size != expected_size) {
        snprintf(out, out_len, "Size mismatch (%lld vs %u bytes)", (long long)actual_size, (unsigned int)expected_size);
    } else {
        snprintf(out, out_len, "OK");
    }

    path_free(path);
}

static void format_save_last_modified(char *out, size_t out_len, const char *save_path) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';

    if (!save_path || save_path[0] == '\0') {
        snprintf(out, out_len, "Unknown");
        return;
    }

    struct stat st;
    path_t *path = path_create(save_path);
    int err = stat(path_get(path), &st);
    path_free(path);
    if (err != 0) {
        snprintf(out, out_len, "Not created");
        return;
    }

    time_t modified = st.st_mtime;
    struct tm *time_info = localtime(&modified);
    if (!time_info || strftime(out, out_len, "%m/%d %I:%M %p", time_info) == 0) {
        snprintf(out, out_len, "Unknown");
    }
}

static void format_recent_sessions (char *out, size_t out_len, const playtime_entry_t *pt) {
    if (out_len == 0) {
        return;
    }

    out[0] = '\0';

    if (!pt || pt->recent_sessions_count == 0) {
        snprintf(out, out_len, "\tNone");
        return;
    }

    for (uint32_t i = 0; i < pt->recent_sessions_count; i++) {
        char when_buf[64];
        char dur_buf[64];

        format_last_played(when_buf, sizeof(when_buf), pt->recent_sessions[i].ended_at);
        format_duration(dur_buf, sizeof(dur_buf), pt->recent_sessions[i].duration_seconds);

        size_t used = strlen(out);
        if (used >= out_len - 1) {
            break;
        }

        snprintf(out + used, out_len - used, "\t- %s (%s)%s",
            when_buf,
            dur_buf,
            (i + 1 < pt->recent_sessions_count) ? "\n" : "");
    }
}

static void paragraph_builder_add_text(const char *text) {
    const char *start = text;
    const char *p = text;
    while (*p) {
        if (*p == '\n') {
            if (p > start) {
                rdpq_paragraph_builder_span(start, (size_t)(p - start));
            }
            rdpq_paragraph_builder_newline();
            p++;
            start = p;
            continue;
        }
        p++;
    }
    if (p > start) {
        rdpq_paragraph_builder_span(start, (size_t)(p - start));
    }
}

static void free_details_layout(void) {
    if (details_layout != NULL) {
        rdpq_paragraph_free(details_layout);
        details_layout = NULL;
    }
}

static int get_details_text_width(void) {
    int base_x = VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL;
    int text_right_limit = BOXART_X - 12;
    int text_width = text_right_limit - base_x;
    if (text_width < 180) {
        text_width = 180;
    }
    return text_width;
}

static int get_details_visible_height(void) {
    int base_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL + 18;
    int clip_y0 = base_y;
    int clip_y1 = LAYOUT_ACTIONS_SEPARATOR_Y - TEXT_MARGIN_VERTICAL;
    int visible_height = clip_y1 - clip_y0;
    if (visible_height < 0) {
        visible_height = 0;
    }
    return visible_height;
}

static void rebuild_details_layout(menu_t *menu) {
    if (!menu) {
        return;
    }

    free_details_layout();

    char details[13824];
    const char *display_name = (menu->load.rom_info.metadata.name[0] != '\0') ? menu->load.rom_info.metadata.name : rom_filename;
    const char *publisher = (menu->load.rom_info.metadata.author[0] != '\0') ? menu->load.rom_info.metadata.author : "Unknown";
    const char *developer = (menu->load.rom_info.metadata.developer[0] != '\0') ? menu->load.rom_info.metadata.developer : "Unknown";
    const char *genre = (menu->load.rom_info.metadata.genre[0] != '\0') ? menu->load.rom_info.metadata.genre : "Unknown";
    const char *series = (menu->load.rom_info.metadata.series[0] != '\0') ? menu->load.rom_info.metadata.series : "Unknown";
    const char *modes = (menu->load.rom_info.metadata.modes[0] != '\0') ? menu->load.rom_info.metadata.modes : "Unknown";
    char virtual_pak[128];
    char age_rating[16];
    char release_year[16];
    char players[24];
    char save_path[512];

    virtual_pak_describe(menu, virtual_pak, sizeof(virtual_pak));

    if (menu->load.rom_info.metadata.age_rating >= 0) {
        snprintf(age_rating, sizeof(age_rating), "%d+", (int)menu->load.rom_info.metadata.age_rating);
    } else {
        snprintf(age_rating, sizeof(age_rating), "Unknown");
    }

    if (menu->load.rom_info.metadata.release_year >= 0) {
        snprintf(release_year, sizeof(release_year), "%d", (int)menu->load.rom_info.metadata.release_year);
    } else {
        snprintf(release_year, sizeof(release_year), "Unknown");
    }

    if ((menu->load.rom_info.metadata.players_min > 0) && (menu->load.rom_info.metadata.players_max > 0)) {
        if (menu->load.rom_info.metadata.players_min == menu->load.rom_info.metadata.players_max) {
            snprintf(players, sizeof(players), "%d", (int)menu->load.rom_info.metadata.players_max);
        } else if (menu->load.rom_info.metadata.players_max >= 99) {
            snprintf(players, sizeof(players), "%d+", (int)menu->load.rom_info.metadata.players_min);
        } else {
            snprintf(players, sizeof(players), "%d-%d",
                     (int)menu->load.rom_info.metadata.players_min,
                     (int)menu->load.rom_info.metadata.players_max);
        }
    } else if (menu->load.rom_info.metadata.players_max > 0) {
        snprintf(players, sizeof(players), "%d", (int)menu->load.rom_info.metadata.players_max);
    } else {
        snprintf(players, sizeof(players), "Unknown");
    }

    rom_save_type_t effective_save_type = rom_info_get_save_type(&menu->load.rom_info);
    bool save_expected = (effective_save_type != SAVE_TYPE_NONE);
    bool supports_cpak = menu->load.rom_info.features.controller_pak;
    if (save_expected && get_save_file_path(menu, save_path, sizeof(save_path))) {
        // Path resolved for display text only.
    } else {
        snprintf(save_path, sizeof(save_path), "N/A");
    }

    snprintf(details, sizeof(details),
        "Title:\t\t\t%s\n"
        "Publisher:\t\t%s\n"
        "Developer:\t\t%s\n"
        "Year:\t\t\t%s\n"
        "Genre:\t\t\t%s\n"
        "Series:\t\t\t%s\n"
        "Players:\t\t\t%s\n"
        "Modes:\t\t\t%s\n"
        "Virtual PAK:\t\t%s\n"
        "ESRB Rating:\t\t%s\n"
        "Age Rating:\t\t%s\n\n"
        "Manual:\t\t\t%s\n",
        display_name,
        publisher,
        developer,
        release_year,
        genre,
        series,
        players,
        modes,
        virtual_pak,
        format_esrb_age_rating(menu->load.rom_info.metadata.esrb_age_rating),
        age_rating,
        cached_has_manual ? "Available" : "Not found"
    );
    append_detail_section(details, sizeof(details), "Description", format_rom_description(menu));
    append_detail_section(details, sizeof(details), "Hook", menu->load.rom_info.metadata.hook);
    append_detail_section(details, sizeof(details), "Why Play", menu->load.rom_info.metadata.why_play);
    append_detail_section(details, sizeof(details), "Vibe", menu->load.rom_info.metadata.vibe);
    append_detail_section(details, sizeof(details), "Notable", menu->load.rom_info.metadata.notable);
    append_detail_section(details, sizeof(details), "Context", menu->load.rom_info.metadata.context);
    append_detail_section(details, sizeof(details), "Play Curator Note", menu->load.rom_info.metadata.play_curator_note);
    append_detail_section(details, sizeof(details), "Tags", menu->load.rom_info.metadata.tags);
    append_detail_section(details, sizeof(details), "Warnings", menu->load.rom_info.metadata.warnings);
    append_detail_section(details, sizeof(details), "Museum Card", menu->load.rom_info.metadata.museum_card);
    append_detail_section(details, sizeof(details), "Museum Trivia", menu->load.rom_info.metadata.trivia_museum);
    append_detail_section(details, sizeof(details), "Oddities", menu->load.rom_info.metadata.oddities);
    if (menu->load.rom_info.features.controller_pak && menu->load.rom_info.settings.virtual_pak_enabled) {
        append_detail_section(details, sizeof(details), "Virtual Pak Note",
            "Requires one real Controller Pak in controller 1. The menu swaps pak contents onto that physical pak before launch, so do not remove it while playing.");
    }
    append_detail_section(details, sizeof(details), "Design Quirks", menu->load.rom_info.metadata.design_quirks);
    append_detail_section(details, sizeof(details), "Discovery Prompts", menu->load.rom_info.metadata.discovery_prompts);
    append_detail_section(details, sizeof(details), "Curator", menu->load.rom_info.metadata.curator);
    append_detail_section(details, sizeof(details), "Museum", menu->load.rom_info.metadata.museum);
    append_detail_section(details, sizeof(details), "Trivia", menu->load.rom_info.metadata.trivia);
    append_detail_section(details, sizeof(details), "Reception", menu->load.rom_info.metadata.reception);
    snprintf(details + strlen(details), sizeof(details) - strlen(details),
        "Datel Cheats:\t\t%s\n"
        "Patches:\t\t\t%s\n"
        "Patch profile:\t\t%s\n"
        "TV region:\t\t%s\n"
        "Expansion PAK:\t%s\n"
        "Rumble PAK:\t\t%s\n"
        "Transfer PAK:\t\t%s\n"
        "Save type:\t\t%s\n"
        "Save backend:\t\t%s\n"
        "Save writeback:\t%s\n"
        "Save file:\t\t%s\n"
        "Save health:\t\t%s\n"
        "Save modified:\t\t%s\n"
        "Playtime:\t\t%s\n"
        "Last session:\t\t%s\n"
        "Last played:\t\t%s\n"
        "Recent sessions:\n%s\n",
        format_boolean_type(menu->load.rom_info.settings.cheats_enabled),
        format_boolean_type(menu->load.rom_info.settings.patches_enabled),
        (menu->load.rom_info.settings.patch_profile[0] != '\0') ? menu->load.rom_info.settings.patch_profile : "auto/default",
        format_rom_tv_type(rom_info_get_tv_type(&menu->load.rom_info)),
        format_rom_expansion_pak_info(menu->load.rom_info.features.expansion_pak),
        format_rom_pak_feature_info(menu->load.rom_info.features.rumble_pak),
        format_rom_pak_feature_info(menu->load.rom_info.features.transfer_pak),
        format_rom_save_type(effective_save_type, supports_cpak),
        format_save_backend(effective_save_type, supports_cpak),
        format_save_writeback_status(save_expected),
        save_path,
        cached_save_health_buf,
        cached_save_modified_buf,
        cached_total_buf,
        cached_last_session_buf,
        cached_last_played_buf,
        cached_recent_sessions_buf
    );

    rdpq_paragraph_builder_begin(
        &(rdpq_textparms_t) {
            .width = get_details_text_width(),
            .height = 2040,
            .wrap = WRAP_WORD,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        NULL
    );
    rdpq_paragraph_builder_style(STL_DEFAULT);
    paragraph_builder_add_text(details);
    details_layout = rdpq_paragraph_builder_end();

    if (details_layout != NULL) {
        int total_height = details_layout->bbox.y1 - details_layout->bbox.y0;
        int visible_height = get_details_visible_height();
        details_max_scroll = total_height > visible_height ? (total_height - visible_height) : 0;
        if (details_scroll > details_max_scroll) {
            details_scroll = details_max_scroll;
        }
        if (details_scroll < 0) {
            details_scroll = 0;
        }
    } else {
        details_max_scroll = 0;
        details_scroll = 0;
    }
}

static void refresh_display_cache(menu_t *menu) {
    if (!menu) {
        return;
    }

    playtime_entry_t *pt = playtime_get_if_cached(&menu->playtime, path_get(menu->load.rom_path));
    if (pt) {
        format_duration(cached_total_buf, sizeof(cached_total_buf), pt->total_seconds);
        format_duration(cached_last_session_buf, sizeof(cached_last_session_buf), pt->last_session_seconds);
        format_last_played(cached_last_played_buf, sizeof(cached_last_played_buf), pt->last_played);
        format_recent_sessions(cached_recent_sessions_buf, sizeof(cached_recent_sessions_buf), pt);
    } else {
        snprintf(cached_total_buf, sizeof(cached_total_buf), "0s");
        snprintf(cached_last_session_buf, sizeof(cached_last_session_buf), "0s");
        snprintf(cached_last_played_buf, sizeof(cached_last_played_buf), "Never");
        snprintf(cached_recent_sessions_buf, sizeof(cached_recent_sessions_buf), "\tNone");
    }

    rom_save_type_t init_save_type = rom_info_get_save_type(&menu->load.rom_info);
    bool init_save_expected = (init_save_type != SAVE_TYPE_NONE);
    char init_save_path[512];
    if (init_save_expected && get_save_file_path(menu, init_save_path, sizeof(init_save_path))) {
        size_t init_expected_size = get_expected_save_size(init_save_type);
        format_save_health(cached_save_health_buf, sizeof(cached_save_health_buf), init_save_path, init_expected_size);
        format_save_last_modified(cached_save_modified_buf, sizeof(cached_save_modified_buf), init_save_path);
    } else {
        snprintf(cached_save_health_buf, sizeof(cached_save_health_buf), "N/A");
        snprintf(cached_save_modified_buf, sizeof(cached_save_modified_buf), "N/A");
    }

    {
        path_t *manual_dir = NULL;
        cached_has_manual = resolve_manual_directory_for_current_rom(menu, &manual_dir, NULL);
        path_free(manual_dir);
    }

    rebuild_details_layout(menu);
    rom_display_data_valid = true;
}

static void process (menu_t *menu) {
    if (ui_components_context_menu_process(menu, &options_context_menu)) {
        return;
    }

    if (menu->actions.enter) {
        if (combo_disk_flow_is_applicable(menu)) {
            if (combo_disk_flow_launch(menu) == COMBO_DISK_FLOW_NO_MATCH) {
                menu->load_pending.rom_file = true;
            }
        } else {
            menu->load_pending.rom_file = true;
        }
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = menu->load.back_mode ? menu->load.back_mode : MENU_MODE_BROWSER;
    } else if (menu->actions.options) {
        ui_components_context_menu_show(&options_context_menu);
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.lz_context) {
        if (show_extra_info_message) {
            show_extra_info_message = false;
        } else {
            show_extra_info_message = true;
        }
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.go_right) {
        iterate_metadata_image(menu, 1);
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_left) {
        iterate_metadata_image(menu, -1);
        sound_play_effect(SFX_CURSOR);
    } else if (!show_extra_info_message && (menu->actions.go_down || menu->actions.go_up)) {
        int step = menu->actions.go_fast ? 48 : 12;
        if (menu->actions.go_down) {
            details_scroll += step;
            if (details_scroll > details_max_scroll) {
                details_scroll = details_max_scroll;
            }
        } else if (menu->actions.go_up) {
            details_scroll -= step;
            if (details_scroll < 0) {
                details_scroll = 0;
            }
        }
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (menu->load_pending.rom_file && menu->settings.loading_progress_bar_enabled) {
        ui_components_loader_draw(0.0f, NULL);
    } else {
#endif
        ui_components_layout_draw();

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "%s\n",
            rom_filename
        );

        if (!rom_display_data_valid || (details_layout == NULL)) {
            refresh_display_cache(menu);
        }

        int base_x = VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL;
        int base_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL + 18;
        int text_width = get_details_text_width();

        int clip_y0 = base_y;
        int clip_y1 = LAYOUT_ACTIONS_SEPARATOR_Y - TEXT_MARGIN_VERTICAL;

        rdpq_set_scissor(base_x, clip_y0, base_x + text_width, clip_y1);
        if (details_layout != NULL) {
            rdpq_paragraph_render(details_layout, base_x, base_y - details_scroll);
        }
        rdpq_set_scissor(0, 0, display_get_width(), display_get_height());

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "%s\n"
            "B: Back\n"
            ,
            combo_disk_flow_is_applicable(menu) ? "A: Load ROM / 64DD" : "A: Load and run ROM"
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_RIGHT, VALIGN_TOP,
            "L|Z: Extra Info\n"
            "R: Adv. Options\n"
        );

        if (boxart != NULL) {
            ui_components_boxart_draw(boxart);
        }

        if (show_extra_info_message) {
            ui_components_messagebox_draw(
                "EXTRA ROM INFO\n"
                "\n"
                "Endianness: %s\n"
                "Title: %.20s\n"
                "Game code: %c%c%c%c\n"
                "Media type: %s\n"
                "Variant: %s\n"
                "Version: %hhu\n"
                "ESRB Age Rating: %s\n"
                "Check code: 0x%016llX\n"
                "CIC: %s\n"
                "Boot address: 0x%08lX\n"
                "SDK version: %.1f%c\n"
                "Clock Rate: %.2fMHz\n\n\n"
                "Press L|Z to return.\n",
                format_rom_endianness(menu->load.rom_info.endianness),
                menu->load.rom_info.title,
                menu->load.rom_info.game_code[0], menu->load.rom_info.game_code[1], menu->load.rom_info.game_code[2], menu->load.rom_info.game_code[3],
                format_rom_media_type(menu->load.rom_info.category_code),
                format_rom_destination_market(menu->load.rom_info.destination_code),
                menu->load.rom_info.version,
                format_esrb_age_rating(menu->load.rom_info.metadata.esrb_age_rating),
                menu->load.rom_info.check_code,
                format_cic_type(rom_info_get_cic_type(&menu->load.rom_info)),
                menu->load.rom_info.boot_address,
                (menu->load.rom_info.libultra.version / 10.0f), menu->load.rom_info.libultra.revision,
                menu->load.rom_info.clock_rate
            );
        }

        ui_components_context_menu_draw(&options_context_menu);
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    }
#endif

    rdpq_detach_show();
}

static void draw_loader_progress(float progress, const char *message) {
    surface_t *d = display_get();

    if (d) {
        rdpq_attach(d, NULL);

        ui_components_background_draw();

        ui_components_loader_draw(progress, message ? message : "Loading ROM...");

        rdpq_detach_show();
    }
}

static void draw_progress (float progress) {
    draw_loader_progress(progress, "Loading ROM...");
}

static void load (menu_t *menu) {
    debugf("Load ROM: load function called\n");
    cart_load_err_t err;
    char virtual_pak_error[128];
    virtual_pak_progress_callback_t virtual_pak_progress = draw_loader_progress;
    if (!virtual_pak_prepare_launch(menu, virtual_pak_error, sizeof(virtual_pak_error), virtual_pak_progress)) {
        menu_show_error(menu, virtual_pak_error);
        return;
    }
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (!menu->settings.loading_progress_bar_enabled) {
        err = cart_load_n64_rom_and_save(menu, NULL);
    } else  {
        err = cart_load_n64_rom_and_save(menu, draw_progress);
    }
#else
    err = cart_load_n64_rom_and_save(menu, draw_progress);
#endif

    if (err != CART_LOAD_OK) {
        menu_show_error(menu, cart_load_convert_error_message(err));
        return;
    }

    playtime_start_session(&menu->playtime, path_get(menu->load.rom_path), menu->current_time);

    bookkeeping_history_add(&menu->bookkeeping, menu->load.rom_path, NULL, BOOKKEEPING_TYPE_ROM);

    menu->next_mode = MENU_MODE_BOOT;

    menu->boot_params->device_type = BOOT_DEVICE_TYPE_ROM;
    menu->boot_params->detect_cic_seed = rom_info_get_cic_seed(&menu->load.rom_info, &menu->boot_params->cic_seed);
    switch (rom_info_get_tv_type(&menu->load.rom_info)) {
        case ROM_TV_TYPE_PAL: menu->boot_params->tv_type = BOOT_TV_TYPE_PAL; break;
        case ROM_TV_TYPE_NTSC: menu->boot_params->tv_type = BOOT_TV_TYPE_NTSC; break;
        case ROM_TV_TYPE_MPAL: menu->boot_params->tv_type = BOOT_TV_TYPE_MPAL; break;
        default: menu->boot_params->tv_type = BOOT_TV_TYPE_PASSTHROUGH; break;
    }

    // Handle cheat codes only if Expansion Pak is present and cheats are enabled
    if (is_memory_expanded() && menu->load.rom_info.settings.cheats_enabled) {
        uint32_t tmp_cheats[MAX_CHEAT_CODE_ARRAYLIST_SIZE];
        size_t cheat_item_count = generate_enabled_cheats_array(get_cheat_codes(), tmp_cheats);

        if (cheat_item_count > 2) { // account for at least one valid cheat code (address and value), excluding the last two 0s
            // Allocate memory for the cheats array
            uint32_t *cheats = malloc(cheat_item_count * sizeof(uint32_t));
            if (cheats) {
                memcpy(cheats, tmp_cheats, cheat_item_count * sizeof(uint32_t));
                for (size_t i = 0; i + 1 < cheat_item_count; i += 2) {
                    debugf("Cheat %u: Address: 0x%08lX, Value: 0x%08lX\n", i / 2, cheats[i], cheats[i + 1]);
                }
                debugf("Cheats enabled, %u cheats found\n", cheat_item_count / 2);
                menu->boot_params->cheat_list = cheats;
            } else {
                debugf("Failed to allocate memory for cheat list\n");
                menu->boot_params->cheat_list = NULL;
            }
        } else {
            debugf("Cheats enabled, but no cheats found\n");
            menu->boot_params->cheat_list = NULL;
        }
    } else {
        debugf("Cheats disabled or Expansion Pak not present\n");
        menu->boot_params->cheat_list = NULL;
    }
}

static void deinit (void) {
    ui_components_boxart_free(boxart);
    boxart = NULL;
    free_details_layout();
    invalidate_metadata_directory_cache();
    current_metadata_image_index = 0;
    metadata_images_scanned = false;
    details_scroll = 0;
    details_max_scroll = 0;
    boxart_retry_pending = false;

    // Clear availability cache
    for (uint16_t i = 0; i < metadata_image_filename_cache_length; i++) {
        metadata_image_available[i] = false;
    }

    rom_display_data_valid = false;
    cached_has_manual = false;
}


void view_load_rom_init (menu_t *menu) {
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (!menu->settings.rom_autoload_enabled) {
#endif
        invalidate_metadata_directory_cache();
        if(menu->load.load_history_id != -1) {
            path_free(menu->load.rom_path);
            bookkeeping_item_t *item = &menu->bookkeeping.history_items[menu->load.load_history_id];
            resolve_bookkeeping_rom_path(menu, item);
            menu->load.rom_path = item->primary_path ? path_clone(item->primary_path) : NULL;
        } else if(menu->load.load_favorite_id != -1) {
            path_free(menu->load.rom_path);
            bookkeeping_item_t *item = &menu->bookkeeping.favorite_items[menu->load.load_favorite_id];
            resolve_bookkeeping_rom_path(menu, item);
            menu->load.rom_path = item->primary_path ? path_clone(item->primary_path) : NULL;
        } else if(menu->load.rom_path && path_has_value(menu->load.rom_path)) {
            // rom_path pre-set by caller (e.g. playtime view) — use it as-is.
        } else {
            path_free(menu->load.rom_path);
            if (menu->browser.entry && menu->browser.entry->path) {
                menu->load.rom_path = path_create(menu->browser.entry->path);
            } else {
                menu->load.rom_path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
            }
        }

        rom_filename = menu->load.rom_path ? path_last_get(menu->load.rom_path) : NULL;
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    }
#endif 

    if (show_extra_info_message) {
        show_extra_info_message = false;
    }

    if (!menu->load.rom_path || !path_has_value(menu->load.rom_path)) {
        menu_show_error(menu, "Couldn't locate ROM");
        return;
    }

    debugf("Load ROM: loading ROM info from %s\n", path_get(menu->load.rom_path));
    rom_err_t err = rom_config_load(menu->load.rom_path, &menu->load.rom_info);
    if (err != ROM_OK) {
        path_free(menu->load.rom_path);
        menu->load.rom_path = NULL;
        menu_show_error(menu, convert_error_message(err));
        return;
    }
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (!menu->settings.rom_autoload_enabled) {
#endif
        current_metadata_image_index = 0;
        scan_metadata_images(menu);
        boxart = ui_components_boxart_init_memory_cached(
            menu->storage_prefix,
            menu->load.rom_info.game_code,
            menu->load.rom_info.title,
            IMAGE_BOXART_FRONT
        );
        if (boxart == NULL) {
            boxart = ui_components_boxart_init_async(
                menu->storage_prefix,
                menu->load.rom_info.game_code,
                menu->load.rom_info.title,
                IMAGE_BOXART_FRONT
            );
        }
        boxart_retry_pending = (boxart == NULL);
        ui_components_context_menu_init(&options_context_menu);
        refresh_display_cache(menu);
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    }
#endif

}

void view_load_rom_display (menu_t *menu, surface_t *display) {
    process(menu);
    retry_boxart_load(menu);

    draw(menu, display);

    if (menu->load_pending.rom_file) {
        menu->load_pending.rom_file = false;
        load(menu);
    }

    if (menu->next_mode != MENU_MODE_LOAD_ROM && menu->next_mode != MENU_MODE_DATEL_CODE_EDITOR) {
        menu->load.load_history_id = -1;
        menu->load.load_favorite_id = -1;
        deinit();
    }
}
