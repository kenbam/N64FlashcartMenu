/**
 * @file rom_patch.h
 * @brief Optional ROM patch pipeline (MVP: IPS manifests).
 * @ingroup menu
 */

#ifndef ROM_PATCH_H__
#define ROM_PATCH_H__

#include <stddef.h>
#include <stdbool.h>

#include "menu_state.h"

typedef enum {
    ROM_PATCH_OK = 0,
    ROM_PATCH_SKIPPED,
    ROM_PATCH_INCOMPATIBLE,
    ROM_PATCH_IO_ERROR,
    ROM_PATCH_FORMAT_ERROR,
} rom_patch_result_t;

/**
 * @brief Prepare a patched ROM for launch.
 *
 * Safe/opt-in behavior:
 * - Returns ROM_PATCH_SKIPPED when no patch manifest is found.
 * - Returns ROM_PATCH_INCOMPATIBLE when manifest constraints do not match.
 * - Returns ROM_PATCH_OK and writes `out_rom_path` when a cached/generated patch is ready.
 */
rom_patch_result_t rom_patch_prepare_launch(
    menu_t *menu,
    const char *source_rom_path,
    char *out_rom_path,
    size_t out_rom_path_len
);

#endif
