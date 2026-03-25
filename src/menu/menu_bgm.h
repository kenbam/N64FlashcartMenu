/**
 * @file menu_bgm.h
 * @brief Background music subsystem
 * @ingroup menu
 */

#ifndef MENU_BGM_H__
#define MENU_BGM_H__

#include "menu_state.h"


/**
 * @brief Poll the BGM subsystem (call every frame from the main loop).
 *
 * Handles reload requests, the init state machine, and playback control.
 *
 * @param menu Pointer to the menu structure.
 */
void menu_bgm_poll (menu_t *menu);

/**
 * @brief Shut down the BGM subsystem and release resources.
 */
void menu_bgm_deinit (void);

/**
 * @brief Request a BGM reload (restarts the init state machine).
 */
void menu_bgm_request_reload (void);


#endif /* MENU_BGM_H__ */
