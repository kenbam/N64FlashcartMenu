#ifndef VIRTUAL_PAK_H__
#define VIRTUAL_PAK_H__

#include <stdbool.h>
#include <stddef.h>

#include "menu_state.h"

void virtual_pak_init(const char *storage_prefix);
void virtual_pak_try_sync_pending(void);
bool virtual_pak_has_pending_sync(void);
bool virtual_pak_prepare_launch(menu_t *menu, char *error, size_t error_len);
bool virtual_pak_describe(menu_t *menu, char *out, size_t out_len);

#endif
