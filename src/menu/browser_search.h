/**
 * @file browser_search.h
 * @brief Local search overlay for the file browser.
 * @ingroup menu
 *
 * Provides an on-screen keyboard and filtered result list that operates
 * as a modal overlay on top of the browser view.
 */

#ifndef BROWSER_SEARCH_H__
#define BROWSER_SEARCH_H__

#include <stdbool.h>
#include "menu_state.h"

/**
 * @brief Check whether the search overlay is currently active.
 * @return true if the search overlay is visible and consuming input.
 */
bool browser_search_is_active(void);

/**
 * @brief Open the search overlay.
 *
 * Compatible with the context-menu action signature so it can be used
 * directly as a menu item action callback.
 *
 * @param menu  Pointer to the menu structure.
 * @param arg   Unused (for context-menu compatibility).
 */
void browser_search_open(menu_t *menu, void *arg);

/**
 * @brief Close the search overlay without resetting the browser selection.
 */
void browser_search_close(void);

/**
 * @brief Reset all search state (query, matches, keyboard cursor).
 *
 * Called when the browser directory changes or the browser is torn down
 * so that stale match indices are not kept around.
 */
void browser_search_reset_state(void);

/**
 * @brief Process input while the search overlay is active.
 *
 * @param menu  Pointer to the menu structure.
 * @return true if the search overlay consumed the input this frame
 *         (caller should skip normal browser input handling).
 */
bool browser_search_process(menu_t *menu);

/**
 * @brief Draw the search overlay (keyboard + filtered results).
 *
 * Should only be called when browser_search_is_active() is true.
 *
 * @param menu  Pointer to the menu structure.
 */
void browser_search_draw(menu_t *menu);

#endif /* BROWSER_SEARCH_H__ */
