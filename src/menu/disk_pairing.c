/**
 * @file disk_pairing.c
 * @brief 64DD combo ROM and disk pairing helpers
 * @ingroup menu
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "disk_pairing.h"
#include "path.h"
#include "utils/fs.h"

static bool disk_pairing_is_expansion_code(char code) {
    return (code >= 'E') && (code <= 'Z');
}

static bool disk_pairing_expected_region_for_rom(const rom_info_t *rom_info, disk_region_t *out_region) {
    if (!rom_info) {
        return false;
    }

    switch (rom_info->destination_code) {
        case MARKET_JAPANESE:
            *out_region = DISK_REGION_JAPANESE;
            return true;
        case MARKET_JAPANESE_MULTI:
        case MARKET_NORTH_AMERICA:
        case MARKET_AUSTRALIAN:
            *out_region = DISK_REGION_USA;
            return true;
        default:
            return false;
    }
}

bool disk_pairing_rom_is_combo(const rom_info_t *rom_info) {
    return rom_info && rom_info->features.combo_rom_disk_game;
}

bool disk_pairing_region_matches_rom(const rom_info_t *rom_info, const disk_info_t *disk_info) {
    disk_region_t expected_region;

    if (!disk_info || !disk_pairing_expected_region_for_rom(rom_info, &expected_region)) {
        return false;
    }

    return disk_info->region == expected_region;
}

bool disk_pairing_disk_id_matches_rom_game_code(const char rom_game_code[4], const char disk_id[4]) {
    if (!rom_game_code || !disk_id) {
        return false;
    }

    if (rom_game_code[0] != 'C') {
        return false;
    }

    if ((rom_game_code[0] == 'C') &&
        (rom_game_code[1] == 'D') &&
        (rom_game_code[2] == 'Z') &&
        (disk_id[0] == 'D') &&
        (disk_id[1] == 'E') &&
        (disk_id[2] == 'Z') &&
        (disk_id[3] == 'A')) {
        return true;
    }

    if ((rom_game_code[1] == 'P' && rom_game_code[2] == 'S' && rom_game_code[3] == 'J') ||
        (rom_game_code[1] == 'P' && rom_game_code[2] == '2' && rom_game_code[3] == 'J')) {
        return disk_pairing_is_expansion_code(disk_id[0]) &&
            (disk_id[1] == rom_game_code[1]) &&
            (disk_id[2] == rom_game_code[2]) &&
            (disk_id[3] == rom_game_code[3]);
    }

    return (disk_id[0] == 'E') &&
        (disk_id[1] == rom_game_code[1]) &&
        (disk_id[2] == rom_game_code[2]) &&
        (disk_id[3] == rom_game_code[3]);
}

bool disk_pairing_disk_matches_rom(const rom_info_t *rom_info, const disk_info_t *disk_info) {
    if (!disk_pairing_rom_is_combo(rom_info) || !disk_info) {
        return false;
    }

    return disk_pairing_region_matches_rom(rom_info, disk_info) &&
        disk_pairing_disk_id_matches_rom_game_code(rom_info->game_code, disk_info->id);
}

bool disk_pairing_resolve_configured_path(
    const char *storage_prefix,
    const char *configured_path,
    char *out,
    size_t out_len
) {
    if (!configured_path || configured_path[0] == '\0' || !out || out_len == 0) {
        return false;
    }

    if (file_exists((char *)configured_path)) {
        snprintf(out, out_len, "%s", configured_path);
        return true;
    }

    if (storage_prefix && configured_path[0] == '/') {
        path_t *prefixed = path_init(storage_prefix, (char *)configured_path);
        if (prefixed && file_exists(path_get(prefixed))) {
            snprintf(out, out_len, "%s", path_get(prefixed));
            path_free(prefixed);
            return true;
        }
        path_free(prefixed);
    }

    return false;
}
