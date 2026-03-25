/**
 * @file browser_grid.h
 * @brief Playlist grid view subsystem for the file browser.
 * @ingroup menu
 *
 * Provides an experimental boxart grid layout for playlists.
 * The grid can be enabled per-playlist via the #EXTGRID directive
 * or toggled at runtime with the view-toggle button.
 */

#ifndef BROWSER_GRID_H__
#define BROWSER_GRID_H__

#include <stdbool.h>
#include <stddef.h>
#include "menu_state.h"

/**
 * @brief Prepare visible grid tiles for the current frame.
 *
 * Resolves boxart thumbnails for the current page.  When @p defer_work
 * is true only memory-cached thumbnails are used (no SD I/O).
 *
 * @param menu        Pointer to the menu structure.
 * @param defer_work  If true, skip heavy I/O this frame.
 */
void browser_grid_prepare(menu_t *menu, bool defer_work);

/**
 * @brief Draw the grid view.
 *
 * Renders the boxart grid, selection highlight, header caption
 * and page indicator.  Falls back to the file list when the
 * entry count is zero.
 *
 * @param menu  Pointer to the menu structure.
 */
void browser_grid_draw(menu_t *menu);

/**
 * @brief Background prewarm tick for grid meta-data.
 *
 * Reads a small number of ROM headers per frame so that boxart
 * directory lookups are ready before the user scrolls to them.
 * Call once per frame from the browser display function.
 *
 * @param menu  Pointer to the menu structure.
 */
void browser_grid_prewarm_tick(menu_t *menu);

/**
 * @brief Check whether the grid view is active for the current browser state.
 *
 * Returns true when the browser is inside a playlist that has grid mode
 * enabled (either by directive or runtime override) and no picker is active.
 *
 * @param menu  Pointer to the menu structure.
 * @return true if grid mode should be used.
 */
bool browser_grid_is_enabled(menu_t *menu);

/**
 * @brief Set the runtime grid/list override.
 *
 * @param override  -1 = use playlist default, 0 = force list, 1 = force grid.
 */
void browser_grid_set_override(int override);

/**
 * @brief Clear all grid state (thumbnail slots, meta index, prewarm).
 *
 * Called when the browser list changes, on directory navigation,
 * or when the view mode is toggled.
 */
void browser_grid_clear(void);

/**
 * @brief Rebuild the meta index for the current browser list.
 *
 * Invalidates and reallocates the per-entry ROM header cache
 * when the browser list pointer or entry count has changed.
 *
 * @param menu  Pointer to the menu structure.
 */
void browser_grid_reset_meta_index(menu_t *menu);

/**
 * @brief Format an entry name for grid display.
 *
 * Strips the file extension, replaces underscores with spaces,
 * and collapses consecutive spaces.
 *
 * @param name         Original file name.
 * @param buffer       Output buffer.
 * @param buffer_size  Size of @p buffer.
 * @return Pointer to @p buffer, or "" on invalid input.
 */
const char *browser_grid_display_name(const char *name, char *buffer, size_t buffer_size);

/**
 * @brief Set the playlist-level grid-view-enabled flag.
 *
 * Called by the playlist override system when a playlist specifies
 * a grid_view directive.
 *
 * @param enabled  true to enable grid view by default.
 */
void browser_grid_set_enabled(bool enabled);

/**
 * @brief Get the current playlist-level grid-view-enabled flag.
 * @return Current value of the grid view enabled flag.
 */
bool browser_grid_get_enabled(void);

#endif /* BROWSER_GRID_H__ */
