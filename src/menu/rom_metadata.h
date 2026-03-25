/**
 * @file rom_metadata.h
 * @brief ROM metadata loading from curated metadata directories
 * @ingroup menu
 */

#ifndef ROM_METADATA_H__
#define ROM_METADATA_H__

#include <stdbool.h>

#include "path.h"
#include "rom_info.h"

/**
 * @brief Load ROM metadata from the curated metadata directory tree.
 *
 * Resolves the metadata directory for the ROM's game code and loads
 * metadata.ini fields and associated text files.  Tries the region-specific
 * subdirectory first, then falls back to the region-agnostic parent.
 *
 * @param rom_path          Path to the ROM file (used to derive storage prefix)
 * @param rom_info          ROM info structure whose metadata fields are populated
 * @param include_long_description  Whether to load long description / curated text files
 */
void rom_metadata_load(path_t *rom_path, rom_info_t *rom_info, bool include_long_description);

#endif // ROM_METADATA_H__
