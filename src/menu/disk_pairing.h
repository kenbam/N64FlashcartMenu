/**
 * @file disk_pairing.h
 * @brief 64DD combo ROM and disk pairing helpers
 * @ingroup menu
 */

#ifndef DISK_PAIRING_H__
#define DISK_PAIRING_H__

#include <stdbool.h>
#include <stddef.h>

#include "disk_info.h"
#include "path.h"
#include "rom_info.h"

/**
 * @brief Returns true when the ROM uses the combo ROM+disk flow.
 *
 * @param rom_info Loaded ROM information
 * @return true when the ROM expects a companion 64DD disk
 */
bool disk_pairing_rom_is_combo(const rom_info_t *rom_info);

/**
 * @brief Returns true when the disk region is compatible with the ROM region.
 *
 * @param rom_info Loaded ROM information
 * @param disk_info Loaded disk information
 * @return true when the regions are compatible
 */
bool disk_pairing_region_matches_rom(const rom_info_t *rom_info, const disk_info_t *disk_info);

/**
 * @brief Returns true when the disk ID matches a known combo-ROM mapping.
 *
 * @param rom_game_code 4-byte ROM game code
 * @param disk_id 4-byte 64DD disk ID
 * @return true when the IDs are a compatible pair
 */
bool disk_pairing_disk_id_matches_rom_game_code(const char rom_game_code[4], const char disk_id[4]);

/**
 * @brief Returns true when a 64DD disk is compatible with the ROM.
 *
 * @param rom_info Loaded ROM information
 * @param disk_info Loaded disk information
 * @return true when the disk can be paired with the ROM
 */
bool disk_pairing_disk_matches_rom(const rom_info_t *rom_info, const disk_info_t *disk_info);

/**
 * @brief Load a disk file and check whether it matches the ROM.
 *
 * @param rom_info Loaded ROM information
 * @param disk_path Disk file path
 * @return true when the disk file is compatible with the ROM
 */
bool disk_pairing_path_matches_rom(const rom_info_t *rom_info, path_t *disk_path);

/**
 * @brief Returns true when a directory tree contains any compatible disk.
 *
 * @param rom_info Loaded ROM information
 * @param directory Directory to scan recursively
 * @return true when at least one compatible disk exists under the directory
 */
bool disk_pairing_directory_has_match_recursive(const rom_info_t *rom_info, path_t *directory);

/**
 * @brief Resolve a stored disk path against the current storage root.
 *
 * Stored paths may be absolute SD-relative paths such as `/N64 - N64DD/...`
 * or already-prefixed filesystem paths.
 *
 * @param storage_prefix Storage prefix such as `sd:`
 * @param configured_path Stored disk path
 * @param out Output buffer for the resolved path
 * @param out_len Output buffer length
 * @return true when the resolved path exists
 */
bool disk_pairing_resolve_configured_path(
    const char *storage_prefix,
    const char *configured_path,
    char *out,
    size_t out_len
);

#endif /* DISK_PAIRING_H__ */
