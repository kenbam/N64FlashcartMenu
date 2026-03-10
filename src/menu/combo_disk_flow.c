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

static const char *combo_disk_extensions[] = { "ndd", NULL };

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

static void combo_disk_flow_open_picker(menu_t *menu, browser_picker_t picker) {
    path_t *disk_root = path_init(menu->storage_prefix, "/N64 - N64DD");
    if (!disk_root || !directory_exists(path_get(disk_root))) {
        path_free(disk_root);
        disk_root = path_init(menu->storage_prefix, "/");
    }
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

    disk_info_t disk_info;
    disk_err_t err = disk_info_load(disk_path, &disk_info);
    if (err != DISK_OK || !disk_pairing_disk_matches_rom(&menu->load.rom_info, &disk_info)) {
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
        } else if (file_has_extensions(info.d_name, combo_disk_extensions)) {
            disk_info_t disk_info;
            if (disk_info_load(candidate, &disk_info) == DISK_OK &&
                disk_pairing_disk_matches_rom(&menu->load.rom_info, &disk_info)) {
                if (*match_count == 0) {
                    snprintf(resolved_path, resolved_len, "%s", path_get(candidate));
                }
                (*match_count)++;
            }
        }

        path_free(candidate);

        if (*match_count > 1) {
            break;
        }

        result = dir_findnext(path_get(directory), &info);
    }

    return result >= -1;
}

static bool combo_disk_flow_try_launch_single_match(menu_t *menu) {
    path_t *disk_root = path_init(menu->storage_prefix, "/N64 - N64DD");
    if (!disk_root || !directory_exists(path_get(disk_root))) {
        path_free(disk_root);
        return false;
    }

    char resolved_disk_path[ROM_CONFIG_PATH_LENGTH] = {0};
    int match_count = 0;
    bool scanned = combo_disk_flow_find_single_compatible_recursive(
        menu,
        disk_root,
        resolved_disk_path,
        sizeof(resolved_disk_path),
        &match_count
    );
    path_free(disk_root);

    if (!scanned || match_count != 1) {
        return false;
    }

    rom_config_setting_set_default_disk_path(menu->load.rom_path, &menu->load.rom_info, strip_fs_prefix(resolved_disk_path));
    return combo_disk_flow_try_launch_default(menu);
}

bool combo_disk_flow_is_applicable(const menu_t *menu) {
    return menu && disk_pairing_rom_is_combo(&menu->load.rom_info);
}

void combo_disk_flow_launch(menu_t *menu) {
    if (!combo_disk_flow_is_applicable(menu)) {
        menu_show_error(menu, "This ROM doesn't use a companion 64DD disk");
        return;
    }

    if (!combo_disk_flow_try_launch_default(menu) &&
        !combo_disk_flow_try_launch_single_match(menu)) {
        combo_disk_flow_open_picker(menu, BROWSER_PICKER_64DD_DISK_LAUNCH);
    }
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
