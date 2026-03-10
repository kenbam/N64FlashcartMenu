/**
 * @file combo_disk_flow.h
 * @brief Combo ROM + 64DD disk launch flow helpers
 * @ingroup menu
 */

#ifndef COMBO_DISK_FLOW_H__
#define COMBO_DISK_FLOW_H__

#include <stdbool.h>

#include "menu_state.h"

/**
 * @brief Returns true when the current ROM uses the combo ROM+disk flow.
 *
 * @param menu Menu state
 * @return true when the loaded ROM expects a companion disk
 */
bool combo_disk_flow_is_applicable(const menu_t *menu);

/**
 * @brief Try to launch the current combo ROM with a compatible 64DD disk.
 *
 * Uses the saved default when valid, otherwise auto-launches a single
 * compatible match under `/N64 - N64DD`, otherwise opens the disk picker.
 *
 * @param menu Menu state
 */
void combo_disk_flow_launch(menu_t *menu);

/**
 * @brief Open the 64DD picker to set the ROM's default companion disk.
 *
 * @param menu Menu state
 */
void combo_disk_flow_set_default(menu_t *menu);

/**
 * @brief Clear the ROM's saved default companion disk.
 *
 * @param menu Menu state
 */
void combo_disk_flow_clear_default(menu_t *menu);

#endif /* COMBO_DISK_FLOW_H__ */
