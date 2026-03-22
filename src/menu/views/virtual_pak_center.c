#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <dir.h>

#include "../path.h"
#include "../rom_info.h"
#include "../sound.h"
#include "../ui_components/constants.h"
#include "utils/fs.h"
#include "../virtual_pak.h"
#include "views.h"

typedef enum {
    VIRTUAL_PAK_CENTER_TAB_STATUS = 0,
    VIRTUAL_PAK_CENTER_TAB_RECOVERY = 1,
    VIRTUAL_PAK_CENTER_TAB_ROM_SLOT = 2,
    VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS = 3,
} virtual_pak_center_tab_t;

#define VIRTUAL_PAK_CENTER_MAX_SLOTS 64

typedef struct {
    char game_id[ROM_STABLE_ID_LENGTH];
    char file_path[512];
    char owner_path[512];
    char display_title[ROM_METADATA_NAME_LENGTH];
    uint8_t slot;
    bool owner_checked;
    bool owner_exists;
    bool title_checked;
    int64_t size_bytes;
} virtual_pak_slot_entry_t;

static virtual_pak_center_tab_t current_tab = VIRTUAL_PAK_CENTER_TAB_STATUS;
static virtual_pak_status_t current_status;
static char status_message[128];
static virtual_pak_slot_entry_t slot_entries[VIRTUAL_PAK_CENTER_MAX_SLOTS];
static int slot_entry_count = 0;
static int slot_selected_index = 0;
static int slot_scroll_offset = 0;
static bool slot_delete_armed = false;
static int status_refresh_cooldown = 0;

#define VIRTUAL_PAK_LIST_ROWS     (8)

static const char *virtual_pak_phase_label(int phase) {
    switch (phase) {
        case 1: return "Slot loaded";
        case 2: return "Recovery needed";
        default: return "Idle";
    }
}

static const char *virtual_pak_yes_no(bool value) {
    return value ? "Yes" : "No";
}

static ui_region_t virtual_pak_center_body_region(void) {
    return ui_components_content_region_get(true, 8, 8);
}

static void virtual_pak_center_draw_body_text(menu_font_style_t style, const char *text) {
    ui_region_t region = virtual_pak_center_body_region();
    ui_components_text_draw_in_region(&region, style, "%s", text ? text : "");
}

static void virtual_pak_center_refresh(void) {
    virtual_pak_get_status(&current_status);
    status_refresh_cooldown = 20;
}

static void virtual_pak_center_set_message(const char *message) {
    snprintf(status_message, sizeof(status_message), "%s", message ? message : "");
}

static void virtual_pak_center_cycle_tab(int delta) {
    int next = (int)current_tab + delta;
    if (next < 0) {
        next = VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS;
    } else if (next > VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS) {
        next = VIRTUAL_PAK_CENTER_TAB_STATUS;
    }
    current_tab = (virtual_pak_center_tab_t)next;
    slot_delete_armed = false;
    status_message[0] = '\0';
}

static bool virtual_pak_center_has_rom(menu_t *menu) {
    return menu && menu->load.rom_path;
}

static void virtual_pak_center_toggle_current_rom(menu_t *menu) {
    if (!virtual_pak_center_has_rom(menu)) {
        virtual_pak_center_set_message("Open this from ROM details to manage a specific game's slot.");
        sound_play_effect(SFX_ERROR);
        return;
    }
    if (!menu->load.rom_info.features.controller_pak) {
        virtual_pak_center_set_message("This ROM does not use a Controller Pak.");
        sound_play_effect(SFX_ERROR);
        return;
    }
    bool next_enabled = !menu->load.rom_info.settings.virtual_pak_enabled;
    rom_err_t err = rom_config_setting_set_virtual_pak_enabled(menu->load.rom_path, &menu->load.rom_info, next_enabled);
    if (err != ROM_OK) {
        virtual_pak_center_set_message("Couldn't save virtual pak setting.");
        sound_play_effect(SFX_ERROR);
        return;
    }
    sound_play_effect(SFX_SETTING);
    virtual_pak_center_set_message(next_enabled ? "Enabled virtual pak for this ROM." : "Disabled virtual pak for this ROM.");
}

static void virtual_pak_center_adjust_slot(menu_t *menu, int delta) {
    if (!virtual_pak_center_has_rom(menu)) {
        return;
    }
    if (!menu->load.rom_info.features.controller_pak) {
        virtual_pak_center_set_message("This ROM does not use a Controller Pak.");
        sound_play_effect(SFX_ERROR);
        return;
    }

    int slot = (int)menu->load.rom_info.settings.virtual_pak_slot + delta;
    if (slot < 1) {
        slot = 4;
    } else if (slot > 4) {
        slot = 1;
    }

    rom_err_t err = rom_config_setting_set_virtual_pak_slot(menu->load.rom_path, &menu->load.rom_info, (uint8_t)slot);
    if (err != ROM_OK) {
        virtual_pak_center_set_message("Couldn't save virtual pak slot.");
        sound_play_effect(SFX_ERROR);
        return;
    }
    sound_play_effect(SFX_SETTING);
    virtual_pak_center_set_message("Updated virtual pak slot for this ROM.");
}

static void virtual_pak_center_resolve_slot_owner(menu_t *menu, virtual_pak_slot_entry_t *entry) {
    if (!menu || !entry || entry->owner_checked) {
        return;
    }
    entry->owner_checked = true;
    entry->owner_exists = rom_info_resolve_stable_id_path(
        menu->storage_prefix,
        entry->game_id,
        NULL,
        entry->owner_path,
        sizeof(entry->owner_path)
    );
}

static void virtual_pak_center_fill_slot_title(menu_t *menu, virtual_pak_slot_entry_t *entry) {
    if (!entry || entry->title_checked) {
        return;
    }
    entry->title_checked = true;
    entry->display_title[0] = '\0';
    virtual_pak_center_resolve_slot_owner(menu, entry);
    if (entry->owner_exists && entry->owner_path[0]) {
        path_t *rom_path = path_create(entry->owner_path);
        if (rom_path) {
            rom_info_t rom_info = {0};
            if (rom_config_load(rom_path, &rom_info) == ROM_OK) {
                const char *title = rom_info.metadata.name[0] ? rom_info.metadata.name : rom_info.title;
                snprintf(entry->display_title, sizeof(entry->display_title), "%s", title);
            }
            path_free(rom_path);
        }
    }

    if (!entry->display_title[0]) {
        snprintf(entry->display_title, sizeof(entry->display_title), "%s", entry->game_id);
    }
}

static int virtual_pak_center_compare_slots(const void *lhs, const void *rhs) {
    const virtual_pak_slot_entry_t *a = (const virtual_pak_slot_entry_t *)lhs;
    const virtual_pak_slot_entry_t *b = (const virtual_pak_slot_entry_t *)rhs;

    int game_cmp = strcmp(a->game_id, b->game_id);
    if (game_cmp != 0) {
        return game_cmp;
    }

    return (int)a->slot - (int)b->slot;
}

static void virtual_pak_center_scan_slots(menu_t *menu) {
    slot_entry_count = 0;
    slot_selected_index = 0;
    slot_scroll_offset = 0;
    slot_delete_armed = false;

    if (!menu || !menu->storage_prefix) {
        return;
    }

    path_t *root = path_init(menu->storage_prefix, "/menu/virtual_paks");
    if (!root || !directory_exists(path_get(root))) {
        path_free(root);
        return;
    }

    dir_t game_dir_info;
    int result = dir_findfirst(path_get(root), &game_dir_info);
    while (result == 0 && slot_entry_count < VIRTUAL_PAK_CENTER_MAX_SLOTS) {
        if (game_dir_info.d_type == DT_DIR) {
            path_t *game_dir = path_clone_push(root, game_dir_info.d_name);
            if (game_dir) {
                dir_t slot_info;
                int slot_result = dir_findfirst(path_get(game_dir), &slot_info);
                while (slot_result == 0 && slot_entry_count < VIRTUAL_PAK_CENTER_MAX_SLOTS) {
                    if (slot_info.d_type != DT_DIR) {
                        path_t *slot_path = path_clone_push(game_dir, slot_info.d_name);
                        if (slot_path && file_has_extensions(slot_info.d_name, (const char *[]){"pak", NULL})) {
                            virtual_pak_slot_entry_t *entry = &slot_entries[slot_entry_count++];
                            memset(entry, 0, sizeof(*entry));
                            snprintf(entry->game_id, sizeof(entry->game_id), "%s", game_dir_info.d_name);
                            snprintf(entry->file_path, sizeof(entry->file_path), "%s", path_get(slot_path));
                            entry->size_bytes = file_get_size(path_get(slot_path));
                            snprintf(entry->display_title, sizeof(entry->display_title), "%s", entry->game_id);

                            const char *basename = file_basename(slot_info.d_name);
                            unsigned int parsed_slot = 0;
                            if (sscanf(basename, "slot%2u.pak", &parsed_slot) == 1) {
                                entry->slot = (uint8_t)parsed_slot;
                            } else {
                                entry->slot = 0;
                            }
                        }
                        path_free(slot_path);
                    }
                    slot_result = dir_findnext(path_get(game_dir), &slot_info);
                }
                path_free(game_dir);
            }
        }
        result = dir_findnext(path_get(root), &game_dir_info);
    }

    if (slot_entry_count > 1) {
        qsort(slot_entries, (size_t)slot_entry_count, sizeof(slot_entries[0]), virtual_pak_center_compare_slots);
    }

    path_free(root);
}

static void virtual_pak_center_move_slot_selection(int delta) {
    if (slot_entry_count <= 0) {
        return;
    }
    slot_delete_armed = false;
    slot_selected_index += delta;
    if (slot_selected_index < 0) {
        slot_selected_index = slot_entry_count - 1;
    } else if (slot_selected_index >= slot_entry_count) {
        slot_selected_index = 0;
    }

    if (slot_selected_index < slot_scroll_offset) {
        slot_scroll_offset = slot_selected_index;
    } else if (slot_selected_index >= (slot_scroll_offset + VIRTUAL_PAK_LIST_ROWS)) {
        slot_scroll_offset = slot_selected_index - (VIRTUAL_PAK_LIST_ROWS - 1);
    }
}

static void virtual_pak_center_open_selected_slot_rom(menu_t *menu) {
    if (!menu || slot_entry_count <= 0 || slot_selected_index < 0 || slot_selected_index >= slot_entry_count) {
        return;
    }

    virtual_pak_slot_entry_t *entry = &slot_entries[slot_selected_index];
    virtual_pak_center_resolve_slot_owner(menu, entry);
    if (!entry->owner_exists) {
        virtual_pak_center_set_message("Owning ROM could not be found for this slot.");
        sound_play_effect(SFX_ERROR);
        return;
    }

    if (menu->load.rom_path) {
        path_free(menu->load.rom_path);
    }
    menu->load.rom_path = path_create(entry->owner_path);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    menu->load.back_mode = MENU_MODE_VIRTUAL_PAK_CENTER;
    menu->next_mode = MENU_MODE_LOAD_ROM;
    sound_play_effect(SFX_ENTER);
}

static void virtual_pak_center_delete_selected_slot(menu_t *menu) {
    (void)menu;
    if (slot_entry_count <= 0 || slot_selected_index < 0 || slot_selected_index >= slot_entry_count) {
        return;
    }

    virtual_pak_slot_entry_t *entry = &slot_entries[slot_selected_index];
    if (!slot_delete_armed) {
        slot_delete_armed = true;
        virtual_pak_center_set_message("Press Start again to delete the selected slot file.");
        sound_play_effect(SFX_ERROR);
        return;
    }

    if (remove(entry->file_path) != 0) {
        virtual_pak_center_set_message("Failed to delete selected slot file.");
        slot_delete_armed = false;
        sound_play_effect(SFX_ERROR);
        return;
    }

    slot_delete_armed = false;
    sound_play_effect(SFX_EXIT);
    virtual_pak_center_set_message("Deleted selected slot file.");
    virtual_pak_center_scan_slots(menu);
}

static void virtual_pak_center_prime_visible_slots(menu_t *menu) {
    if (!menu || current_tab != VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS || slot_entry_count <= 0) {
        return;
    }

    int start = slot_scroll_offset;
    int end = slot_scroll_offset + VIRTUAL_PAK_LIST_ROWS;
    if (slot_selected_index < start) {
        start = slot_selected_index;
    }
    if (slot_selected_index >= end) {
        end = slot_selected_index + 1;
    }
    if (end > slot_entry_count) {
        end = slot_entry_count;
    }

    for (int index = start; index < end; index++) {
        virtual_pak_center_fill_slot_title(menu, &slot_entries[index]);
    }
}

static void process(menu_t *menu) {
    if (current_tab == VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS) {
        virtual_pak_center_prime_visible_slots(menu);
    } else if (status_refresh_cooldown <= 0) {
        virtual_pak_center_refresh();
    } else {
        status_refresh_cooldown--;
    }

    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = menu->utility_return_mode ? menu->utility_return_mode : MENU_MODE_BROWSER;
        return;
    }

    if (menu->actions.go_left) {
        virtual_pak_center_cycle_tab(-1);
        if (current_tab == VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS) {
            virtual_pak_center_scan_slots(menu);
        }
        sound_play_effect(SFX_CURSOR);
        return;
    }

    if (menu->actions.go_right) {
        virtual_pak_center_cycle_tab(1);
        if (current_tab == VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS) {
            virtual_pak_center_scan_slots(menu);
        }
        sound_play_effect(SFX_CURSOR);
        return;
    }

    if (current_tab == VIRTUAL_PAK_CENTER_TAB_ROM_SLOT) {
        if (menu->actions.go_up) {
            virtual_pak_center_adjust_slot(menu, 1);
            slot_delete_armed = false;
            return;
        }
        if (menu->actions.go_down) {
            virtual_pak_center_adjust_slot(menu, -1);
            slot_delete_armed = false;
            return;
        }
    } else if (current_tab == VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS) {
        if (menu->actions.go_up) {
            virtual_pak_center_move_slot_selection(-1);
            sound_play_effect(SFX_CURSOR);
            return;
        }
        if (menu->actions.go_down) {
            virtual_pak_center_move_slot_selection(1);
            sound_play_effect(SFX_CURSOR);
            return;
        }
    }

    if (menu->actions.options) {
        sound_play_effect(SFX_ENTER);
        menu->next_mode = MENU_MODE_CONTROLLER_PAKFS;
        return;
    }

    if (menu->actions.settings) {
        if (current_tab == VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS) {
            virtual_pak_center_delete_selected_slot(menu);
            return;
        }
        if (!current_status.session_active) {
            virtual_pak_center_set_message("No pending session to clear.");
            sound_play_effect(SFX_ERROR);
            return;
        }
        sound_play_effect(SFX_ERROR);
        virtual_pak_force_clear_pending();
        virtual_pak_center_refresh();
        virtual_pak_center_set_message("Cleared pending virtual pak session.");
        return;
    }

    if (menu->actions.enter) {
        if (current_tab == VIRTUAL_PAK_CENTER_TAB_ROM_SLOT) {
            virtual_pak_center_toggle_current_rom(menu);
            return;
        }
        if (current_tab == VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS) {
            virtual_pak_center_open_selected_slot_rom(menu);
            return;
        }
        if (current_tab == VIRTUAL_PAK_CENTER_TAB_STATUS) {
            virtual_pak_center_refresh();
            virtual_pak_center_set_message("Refreshed virtual pak status.");
            sound_play_effect(SFX_ENTER);
            return;
        }
        sound_play_effect(SFX_ENTER);
        virtual_pak_try_sync_pending();
        virtual_pak_center_refresh();
        if (!current_status.session_active) {
            virtual_pak_center_set_message("Recovery completed. Session is clear.");
        } else if (!current_status.has_physical_pak) {
            virtual_pak_center_set_message("Insert the same physical Controller Pak in controller 1.");
        } else {
            virtual_pak_center_set_message("Recovery still pending. Inspect the pak or try again.");
        }
    }
}

static void draw_status_tab(void) {
    char body[2048];
    snprintf(
        body,
        sizeof(body),
        "Session Active       : %s\n"
        "Phase                : %s\n"
        "Controller           : %d\n"
        "Assigned Slot        : %u\n"
        "Physical Pak Present : %s\n"
        "Detected Banks       : %d\n"
        "Slot File Present    : %s\n"
        "Backup File Present  : %s\n"
        "Rescue File Present  : %s\n"
        "Game ID              : %s\n"
        "Session Token        : %08lX\n"
        "\n"
        "ROM Path             : %s\n"
        "\n"
        "Slot File            : %s\n"
        "\n"
        "Backup File          : %s\n",
        virtual_pak_yes_no(current_status.session_active),
        virtual_pak_phase_label(current_status.phase),
        current_status.controller + 1,
        (unsigned)current_status.slot,
        virtual_pak_yes_no(current_status.has_physical_pak),
        current_status.physical_bank_count,
        virtual_pak_yes_no(current_status.slot_file_exists),
        virtual_pak_yes_no(current_status.backup_file_exists),
        virtual_pak_yes_no(current_status.rescue_file_exists),
        current_status.game_id[0] ? current_status.game_id : "(none)",
        (unsigned long)current_status.session_token,
        current_status.rom_path[0] ? current_status.rom_path : "(none)",
        current_status.pak_path[0] ? current_status.pak_path : "(none)",
        current_status.backup_path[0] ? current_status.backup_path : "(none)"
    );
    virtual_pak_center_draw_body_text(STL_DEFAULT, body);
}

static void draw_recovery_tab(void) {
    const char *guidance = current_status.session_active
        ? "Keep the same physical Controller Pak inserted in controller 1 before retrying recovery."
        : "No pending recovery session. Launch a virtual-pak game to populate this screen.";

    char body[1536];
    snprintf(
        body,
        sizeof(body),
        "%s\n"
        "\n"
        "Recovery Phase       : %s\n"
        "Physical Pak Present : %s\n"
        "Backup File          : %s\n"
        "Rescue File          : %s\n"
        "\n"
        "Rescue Path          : %s\n"
        "\n"
        "A retries recovery now.\n"
        "R opens the physical pak manager.\n"
        "Start clears the pending session.\n",
        guidance,
        virtual_pak_phase_label(current_status.phase),
        virtual_pak_yes_no(current_status.has_physical_pak),
        current_status.backup_file_exists ? "Present" : "Missing",
        current_status.rescue_file_exists ? "Present" : "Missing",
        current_status.rescue_path[0] ? current_status.rescue_path : "(none)"
    );
    virtual_pak_center_draw_body_text(current_status.session_active ? STL_ORANGE : STL_DEFAULT, body);
}

static void draw_rom_slot_tab(menu_t *menu) {
    if (!virtual_pak_center_has_rom(menu)) {
        virtual_pak_center_draw_body_text(
            STL_DEFAULT,
            "No ROM is currently selected.\n"
            "\n"
            "Open this center from a ROM details screen to manage a specific game's virtual pak settings.\n"
        );
        return;
    }

    const char *supports_cpak = menu->load.rom_info.features.controller_pak ? "Yes" : "No";
    const char *enabled = menu->load.rom_info.settings.virtual_pak_enabled ? "Yes" : "No";

    char body[1024];
    snprintf(
        body,
        sizeof(body),
        "ROM Title              : %s\n"
        "Supports Controller Pak: %s\n"
        "Virtual Pak Enabled    : %s\n"
        "Assigned Slot          : %u\n"
        "\n"
        "A toggles virtual pak for this ROM.\n"
        "Up/Down changes the assigned slot.\n"
        "\n"
        "This updates the per-ROM config used at launch.\n",
        menu->load.rom_info.title,
        supports_cpak,
        enabled,
        (unsigned)menu->load.rom_info.settings.virtual_pak_slot
    );
    virtual_pak_center_draw_body_text(menu->load.rom_info.features.controller_pak ? STL_DEFAULT : STL_ORANGE, body);
}

static void draw_all_slots_tab(void) {
    char body[4096];
    ui_region_t region = virtual_pak_center_body_region();
    size_t used = (size_t)snprintf(
        body,
        sizeof(body),
        "Slots Stored: %d\n"
        "\n",
        slot_entry_count
    );
    if (slot_entry_count <= 0) {
        rdpq_text_printf(
            &(rdpq_textparms_t){ .width = region.width, .height = region.height, .wrap = WRAP_WORD },
            FNT_DEFAULT,
            region.x,
            region.y,
            "No virtual pak slot files were found in /menu/virtual_paks."
        );
        return;
    }

    for (int i = 0; i < VIRTUAL_PAK_LIST_ROWS; i++) {
        int index = slot_scroll_offset + i;
        if (index >= slot_entry_count) {
            break;
        }
        virtual_pak_slot_entry_t *entry = &slot_entries[index];
        used += (size_t)snprintf(
            body + used,
            used < sizeof(body) ? sizeof(body) - used : 0,
            "%s%2u  %-22.22s  %-4lld KB  %s\n",
            (index == slot_selected_index) ? ">" : " ",
            (unsigned)entry->slot,
            entry->display_title,
            (long long)((entry->size_bytes + 1023) / 1024),
            entry->owner_exists ? "ROM OK" : "ROM missing"
        );
    }

    if (slot_selected_index >= 0 && slot_selected_index < slot_entry_count) {
        virtual_pak_slot_entry_t *selected = &slot_entries[slot_selected_index];
        used += (size_t)snprintf(
            body + used,
            used < sizeof(body) ? sizeof(body) - used : 0,
            "\n"
            "Selected Title       : %s\n"
            "Selected Game ID     : %s\n"
            "Slot File            : %s\n"
            "Owning ROM           : %s\n",
            selected->display_title,
            selected->game_id,
            selected->file_path,
            selected->owner_exists ? selected->owner_path : "(not found)"
        );
    }

    rdpq_text_printf(
        &(rdpq_textparms_t){ .width = region.width, .height = region.height, .wrap = WRAP_WORD },
        FNT_DEFAULT,
        region.x,
        region.y,
        "%s",
        body
    );
}

static void draw(menu_t *menu, surface_t *display) {
    const char *tab_labels[] = {"Status", "Recovery", "ROM Slot", "All Slots"};
    const int tab_count = 4;
    float tab_width = (VISIBLE_AREA_X1 - VISIBLE_AREA_X0 - 8.0f) / (tab_count + 0.5f);

    rdpq_attach(display, NULL);

    ui_components_background_draw();
    ui_components_layout_draw_tabbed();
    ui_components_tabs_draw(
        tab_labels,
        tab_count,
        (int)current_tab,
        tab_width
    );

    if (current_tab == VIRTUAL_PAK_CENTER_TAB_STATUS) {
        draw_status_tab();
    } else if (current_tab == VIRTUAL_PAK_CENTER_TAB_RECOVERY) {
        draw_recovery_tab();
    } else if (current_tab == VIRTUAL_PAK_CENTER_TAB_ROM_SLOT) {
        draw_rom_slot_tab(menu);
    } else {
        draw_all_slots_tab();
    }

    if (current_tab == VIRTUAL_PAK_CENTER_TAB_ROM_SLOT) {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Toggle for ROM\n"
            "B: Back\n"
            "Up/Down: Change slot\n"
            "R: Pak manager"
        );
    } else if (current_tab == VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS) {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Open owning ROM\n"
            "B: Back\n"
            "Up/Down: Select slot\n"
            "Start: Arm delete"
        );
    } else {
        if (current_tab == VIRTUAL_PAK_CENTER_TAB_STATUS) {
            ui_components_actions_bar_text_draw(
                STL_DEFAULT,
                ALIGN_LEFT, VALIGN_TOP,
                "A: Refresh status\n"
                "B: Back\n"
                "R: Pak manager\n"
                "Start: Force clear"
            );
        } else {
            ui_components_actions_bar_text_draw(
                STL_DEFAULT,
                ALIGN_LEFT, VALIGN_TOP,
                "A: Retry recovery\n"
                "B: Back\n"
                "R: Pak manager\n"
                "Start: Force clear"
            );
        }
    }

    if (current_tab == VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS && slot_delete_armed) {
        ui_components_actions_bar_text_draw(
            STL_ORANGE,
            ALIGN_RIGHT, VALIGN_TOP,
            "Start again: Confirm delete\n"
            "B: Back\n"
            "Up/Down: Cancel arm"
        );
    } else {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_RIGHT, VALIGN_TOP,
            "Left/Right: Switch tab"
        );
    }

    if (status_message[0]) {
        ui_components_messagebox_draw("%s", status_message);
    }

    rdpq_detach_show();
}

void view_virtual_pak_center_init(menu_t *menu) {
    virtual_pak_center_refresh();
    virtual_pak_center_scan_slots(menu);
    if (menu->mode == MENU_MODE_LOAD_ROM && menu->load.back_mode == MENU_MODE_VIRTUAL_PAK_CENTER) {
        current_tab = VIRTUAL_PAK_CENTER_TAB_ALL_SLOTS;
    } else if (menu->utility_return_mode == MENU_MODE_LOAD_ROM) {
        current_tab = VIRTUAL_PAK_CENTER_TAB_ROM_SLOT;
    } else {
        current_tab = current_status.session_active ? VIRTUAL_PAK_CENTER_TAB_RECOVERY : VIRTUAL_PAK_CENTER_TAB_STATUS;
    }
    status_message[0] = '\0';
    slot_delete_armed = false;
    status_refresh_cooldown = 20;
}

void view_virtual_pak_center_display(menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}
