/**
 * @file combo_disk_flow.c
 * @brief Combo ROM + 64DD disk launch flow helpers
 * @ingroup menu
 */

#include <stdio.h>

#include <libdragon.h>

#include "combo_disk_flow.h"
#include "disk_pairing.h"
#include "path.h"
#include "rom_info.h"
#include "utils/fs.h"
#include "views/views.h"

static bool combo_disk_flow_find_single_compatible_recursive(
    menu_t *menu,
    path_t *directory,
    char *resolved_path,
    size_t resolved_len,
    int *match_count
);

static path_t *combo_disk_flow_create_preferred_root(menu_t *menu) {
    if (!menu) {
        return NULL;
    }
    return path_init(menu->storage_prefix, "/N64 - N64DD");
}

static path_t *combo_disk_flow_create_browser_root(menu_t *menu) {
    path_t *preferred_root = combo_disk_flow_create_preferred_root(menu);
    if (preferred_root && directory_exists(path_get(preferred_root))) {
        return preferred_root;
    }

    path_free(preferred_root);
    return path_init(menu->storage_prefix, "/");
}

static bool combo_disk_flow_resolve_default(menu_t *menu, char *resolved_path, size_t resolved_len) {
    if (!combo_disk_flow_is_applicable(menu)) {
        return false;
    }

    return disk_pairing_resolve_configured_path(
        menu->storage_prefix,
        menu->load.rom_info.settings.default_disk_path,
        resolved_path,
        resolved_len
    );
}

static bool combo_disk_flow_scan_matches(
    menu_t *menu,
    char *resolved_path,
    size_t resolved_len,
    int *match_count
) {
    if (resolved_path && resolved_len > 0) {
        resolved_path[0] = '\0';
    }
    *match_count = 0;

    path_t *preferred_root = combo_disk_flow_create_preferred_root(menu);
    if (!preferred_root || !directory_exists(path_get(preferred_root))) {
        path_free(preferred_root);
        return true;
    }

    bool scanned = combo_disk_flow_find_single_compatible_recursive(
        menu,
        preferred_root,
        resolved_path,
        resolved_len,
        match_count
    );
    path_free(preferred_root);
    return scanned;
}

static void combo_disk_flow_open_picker(menu_t *menu, browser_picker_t picker) {
    path_t *disk_root = combo_disk_flow_create_browser_root(menu);
    if (!disk_root) {
        menu_show_error(menu, "Couldn't open 64DD disk browser");
        return;
    }

    if (menu->browser.directory) {
        path_free(menu->browser.directory);
    }
    menu->browser.directory = disk_root;
    menu->browser.valid = false;
    menu->browser.reload = false;
    menu->browser.picker = picker;
    if (menu->browser.picker_root) {
        path_free(menu->browser.picker_root);
    }
    menu->browser.picker_root = path_clone(disk_root);
    menu->browser.picker_return_mode = MENU_MODE_LOAD_ROM;

    if (menu->browser.select_file) {
        path_free(menu->browser.select_file);
        menu->browser.select_file = NULL;
    }

    char resolved_disk_path[ROM_CONFIG_PATH_LENGTH];
    if (combo_disk_flow_resolve_default(menu, resolved_disk_path, sizeof(resolved_disk_path))) {
        menu->browser.select_file = path_create(resolved_disk_path);
    }

    menu->next_mode = MENU_MODE_BROWSER;
}

static bool combo_disk_flow_try_launch_path(menu_t *menu, const char *disk_path_string) {
    path_t *disk_path = path_create(disk_path_string);
    if (!disk_path) {
        return false;
    }

    if (!disk_pairing_path_matches_rom(&menu->load.rom_info, disk_path)) {
        path_free(disk_path);
        return false;
    }

    path_free(menu->load.disk_slots.primary.disk_path);
    menu->load.disk_slots.primary.disk_path = disk_path;
    menu->load.combined_disk_rom = true;
    menu->load.back_mode = MENU_MODE_LOAD_ROM;
    menu->load_pending.disk_file = true;
    menu->next_mode = MENU_MODE_LOAD_DISK;
    return true;
}

static bool combo_disk_flow_try_launch_default(menu_t *menu) {
    char resolved_disk_path[ROM_CONFIG_PATH_LENGTH];

    if (!combo_disk_flow_resolve_default(menu, resolved_disk_path, sizeof(resolved_disk_path))) {
        return false;
    }

    return combo_disk_flow_try_launch_path(menu, resolved_disk_path);
}

static bool combo_disk_flow_find_single_compatible_recursive(
    menu_t *menu,
    path_t *directory,
    char *resolved_path,
    size_t resolved_len,
    int *match_count
) {
    dir_t info;
    int result = dir_findfirst(path_get(directory), &info);
    if (result < -1) {
        return false;
    }

    while (result == 0) {
        path_t *candidate = path_clone_push(directory, info.d_name);
        if (!candidate) {
            return false;
        }

        if (info.d_type == DT_DIR) {
            if (!combo_disk_flow_find_single_compatible_recursive(menu, candidate, resolved_path, resolved_len, match_count)) {
                path_free(candidate);
                return false;
            }
        } else if (disk_pairing_path_matches_rom(&menu->load.rom_info, candidate)) {
            if (*match_count == 0) {
                snprintf(resolved_path, resolved_len, "%s", path_get(candidate));
            }
            (*match_count)++;
        }

        path_free(candidate);

        if (*match_count > 1) {
            break;
        }

        result = dir_findnext(path_get(directory), &info);
    }

    return result >= -1;
}

bool combo_disk_flow_is_applicable(const menu_t *menu) {
    return menu && disk_pairing_rom_is_combo(&menu->load.rom_info);
}

combo_disk_flow_result_t combo_disk_flow_launch(menu_t *menu) {
    if (!combo_disk_flow_is_applicable(menu)) {
        menu_show_error(menu, "This ROM doesn't use a companion 64DD disk");
        return COMBO_DISK_FLOW_NONE;
    }

    if (combo_disk_flow_try_launch_default(menu)) {
        return COMBO_DISK_FLOW_LAUNCHED_DISK;
    }

    char resolved_disk_path[ROM_CONFIG_PATH_LENGTH] = {0};
    int match_count = 0;
    if (!combo_disk_flow_scan_matches(menu, resolved_disk_path, sizeof(resolved_disk_path), &match_count)) {
        menu_show_error(menu, "Couldn't scan 64DD disk library");
        return COMBO_DISK_FLOW_NONE;
    }

    if (match_count == 0) {
        return COMBO_DISK_FLOW_NO_MATCH;
    }

    if (match_count == 1) {
        rom_config_setting_set_default_disk_path(menu->load.rom_path, &menu->load.rom_info, strip_fs_prefix(resolved_disk_path));
        return combo_disk_flow_try_launch_default(menu) ? COMBO_DISK_FLOW_LAUNCHED_DISK : COMBO_DISK_FLOW_NONE;
    }

    combo_disk_flow_open_picker(menu, BROWSER_PICKER_64DD_DISK_LAUNCH);
    return COMBO_DISK_FLOW_OPENED_PICKER;
}

combo_disk_flow_result_t combo_disk_flow_launch_required(menu_t *menu) {
    if (!combo_disk_flow_is_applicable(menu)) {
        menu_show_error(menu, "This ROM doesn't use a companion 64DD disk");
        return COMBO_DISK_FLOW_NONE;
    }

    combo_disk_flow_result_t result = combo_disk_flow_launch(menu);
    if (result == COMBO_DISK_FLOW_NO_MATCH) {
        menu_show_error(menu, "No compatible 64DD disk found for this ROM");
    }
    return result;
}

void combo_disk_flow_set_default(menu_t *menu) {
    if (!combo_disk_flow_is_applicable(menu)) {
        menu_show_error(menu, "This ROM doesn't use a companion 64DD disk");
        return;
    }
    combo_disk_flow_open_picker(menu, BROWSER_PICKER_64DD_DISK_DEFAULT);
}

void combo_disk_flow_clear_default(menu_t *menu) {
    if (!combo_disk_flow_is_applicable(menu)) {
        menu_show_error(menu, "This ROM doesn't use a companion 64DD disk");
        return;
    }

    rom_config_setting_set_default_disk_path(menu->load.rom_path, &menu->load.rom_info, "");
}
