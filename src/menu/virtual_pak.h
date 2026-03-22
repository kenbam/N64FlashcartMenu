#ifndef VIRTUAL_PAK_H__
#define VIRTUAL_PAK_H__

#include <stdbool.h>
#include <stddef.h>

#include "menu_state.h"

typedef void (*virtual_pak_progress_callback_t)(float progress, const char *message);

typedef struct {
    bool session_active;
    int phase;
    int controller;
    uint8_t slot;
    uint32_t session_token;
    bool has_physical_pak;
    int physical_bank_count;
    bool slot_file_exists;
    bool backup_file_exists;
    bool rescue_file_exists;
    char game_id[ROM_STABLE_ID_LENGTH];
    char rom_path[512];
    char pak_path[512];
    char backup_path[512];
    char rescue_path[512];
} virtual_pak_status_t;

void virtual_pak_init(const char *storage_prefix);
void virtual_pak_try_sync_pending(void);
bool virtual_pak_has_pending_sync(void);
void virtual_pak_force_clear_pending(void);
bool virtual_pak_get_status(virtual_pak_status_t *status);
bool virtual_pak_prepare_launch(menu_t *menu, char *error, size_t error_len, virtual_pak_progress_callback_t progress);
bool virtual_pak_describe(menu_t *menu, char *out, size_t out_len);

#endif
