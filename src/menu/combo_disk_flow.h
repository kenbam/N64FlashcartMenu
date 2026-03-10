/**
 * @file combo_disk_flow.h
 * @brief Combo ROM + 64DD disk launch flow helpers
 * @ingroup menu
 */

#ifndef COMBO_DISK_FLOW_H__
#define COMBO_DISK_FLOW_H__

#include <stdbool.h>

#include "menu_state.h"

typedef enum {
    COMBO_DISK_FLOW_NONE = 0,
    COMBO_DISK_FLOW_LAUNCHED_DISK,
    COMBO_DISK_FLOW_OPENED_PICKER,
    COMBO_DISK_FLOW_NO_MATCH,
} combo_disk_flow_result_t;

/**
 * @brief Returns true when the current ROM uses the combo ROM+disk flow.
 *
 * @param menu Menu state
 * @return true when the loaded ROM expects a companion disk
 */
bool combo_disk_flow_is_applicable(const menu_t *menu);

/**
 * @brief Try to use a compatible 64DD disk for the current combo ROM.
 *
 * Uses the saved default when valid, otherwise auto-launches a single
 * compatible match under `/N64 - N64DD`, otherwise opens the disk picker
 * when multiple matches exist. If no compatible disk exists, returns
 * `COMBO_DISK_FLOW_NO_MATCH`.
 *
 * @param menu Menu state
 * @return Flow result
 */
combo_disk_flow_result_t combo_disk_flow_launch(menu_t *menu);

/**
 * @brief Require a compatible 64DD disk for the current combo ROM.
 *
 * Behaves like `combo_disk_flow_launch`, but shows an error instead of
 * falling back when no compatible disk exists.
 *
 * @param menu Menu state
 * @return Flow result
 */
combo_disk_flow_result_t combo_disk_flow_launch_required(menu_t *menu);

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
