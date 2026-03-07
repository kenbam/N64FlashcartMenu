#include <errno.h>
#include <libdragon.h>
#include <mini.c/src/mini.h>
#include <stdio.h>
#include <string.h>

#include "path.h"
#include "rom_info.h"
#include "virtual_pak.h"
#include "utils/cpakfs_utils.h"
#include "utils/fs.h"

#define VIRTUAL_PAK_DIRECTORY "/menu/virtual_paks"
#define VIRTUAL_PAK_SESSION_FILE "/menu/virtual_pak_session.ini"
#define VIRTUAL_PAK_CONTROLLER 0
#define VIRTUAL_PAK_BANK_SIZE 32768

typedef struct {
    bool active;
    int controller;
    uint8_t slot;
    char game_id[ROM_STABLE_ID_LENGTH];
    char rom_path[512];
    char pak_path[512];
} virtual_pak_session_t;

static char g_session_path[512];
static char g_root_path[512];

static void virtual_pak_session_init(virtual_pak_session_t *session) {
    if (!session) {
        return;
    }
    memset(session, 0, sizeof(*session));
    session->controller = VIRTUAL_PAK_CONTROLLER;
    session->slot = 1;
}

static void virtual_pak_session_load(virtual_pak_session_t *session) {
    virtual_pak_session_init(session);
    if (!session || !g_session_path[0] || !file_exists(g_session_path)) {
        return;
    }

    mini_t *ini = mini_try_load(g_session_path);
    if (!ini) {
        return;
    }

    session->active = mini_get_bool(ini, "session", "active", false);
    session->controller = mini_get_int(ini, "session", "controller", VIRTUAL_PAK_CONTROLLER);
    int slot = mini_get_int(ini, "session", "slot", 1);
    if (slot < 1) {
        slot = 1;
    } else if (slot > 8) {
        slot = 8;
    }
    session->slot = (uint8_t)slot;
    snprintf(session->game_id, sizeof(session->game_id), "%s", mini_get_string(ini, "session", "game_id", ""));
    snprintf(session->rom_path, sizeof(session->rom_path), "%s", mini_get_string(ini, "session", "rom_path", ""));
    snprintf(session->pak_path, sizeof(session->pak_path), "%s", mini_get_string(ini, "session", "pak_path", ""));

    mini_free(ini);
}

static void virtual_pak_session_save(const virtual_pak_session_t *session) {
    if (!session || !g_session_path[0]) {
        return;
    }

    mini_t *ini = mini_create(g_session_path);
    mini_set_bool(ini, "session", "active", session->active);
    mini_set_int(ini, "session", "controller", session->controller);
    mini_set_int(ini, "session", "slot", session->slot);
    mini_set_string(ini, "session", "game_id", session->game_id);
    mini_set_string(ini, "session", "rom_path", session->rom_path);
    mini_set_string(ini, "session", "pak_path", session->pak_path);
    mini_save(ini, MINI_FLAGS_SKIP_EMPTY_GROUPS);
    mini_free(ini);
}

static void virtual_pak_session_clear(void) {
    virtual_pak_session_t session;
    virtual_pak_session_init(&session);
    virtual_pak_session_save(&session);
}

static bool virtual_pak_ensure_parent_dir(const char *path) {
    if (!path || !path[0]) {
        return false;
    }
    path_t *dir = path_create(path);
    path_pop(dir);
    bool error = directory_create(path_get(dir));
    path_free(dir);
    return !error;
}

static bool virtual_pak_dump_controller_to_file(int controller, const char *out_path) {
    if (!out_path || !out_path[0]) {
        return false;
    }
    if (!has_cpak(controller)) {
        return false;
    }

    cpakfs_unmount(controller);

    int banks = cpak_probe_banks(controller);
    if (banks <= 0) {
        return false;
    }
    if (!virtual_pak_ensure_parent_dir(out_path)) {
        return false;
    }

    char temp_path[576];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", out_path);

    FILE *f = fopen(temp_path, "wb");
    if (!f) {
        return false;
    }

    uint8_t bankbuf[VIRTUAL_PAK_BANK_SIZE];
    bool ok = true;
    for (int bank = 0; bank < banks; bank++) {
        int rd = cpak_read((joypad_port_t)controller, (uint8_t)bank, 0, bankbuf, sizeof(bankbuf));
        if (rd != 0 || fwrite(bankbuf, 1, sizeof(bankbuf), f) != sizeof(bankbuf)) {
            ok = false;
            break;
        }
    }

    if (fclose(f)) {
        ok = false;
    }
    if (!ok) {
        remove(temp_path);
        return false;
    }
    remove((char *)out_path);
    if (rename(temp_path, out_path) != 0) {
        remove(temp_path);
        return false;
    }
    return true;
}

static bool virtual_pak_restore_file_to_controller(int controller, const char *pak_path) {
    if (!pak_path || !pak_path[0] || !file_exists((char *)pak_path) || !has_cpak(controller)) {
        return false;
    }

    cpakfs_unmount(controller);

    FILE *f = fopen(pak_path, "rb");
    if (!f) {
        return false;
    }

    int banks_on_device = cpak_probe_banks(controller);
    if (banks_on_device <= 0) {
        fclose(f);
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long filesize = ftell(f);
    rewind(f);

    if (filesize <= 0 || (filesize % VIRTUAL_PAK_BANK_SIZE) != 0) {
        fclose(f);
        return false;
    }

    int total_banks = (int)(filesize / VIRTUAL_PAK_BANK_SIZE);
    if (total_banks > banks_on_device) {
        fclose(f);
        return false;
    }

    uint8_t bankbuf[VIRTUAL_PAK_BANK_SIZE];
    bool ok = true;
    for (int bank = 0; bank < total_banks; bank++) {
        size_t bytes_read = fread(bankbuf, 1, sizeof(bankbuf), f);
        if (bytes_read != sizeof(bankbuf)) {
            ok = false;
            break;
        }
        int written = cpak_write((joypad_port_t)controller, (uint8_t)bank, 0, bankbuf, sizeof(bankbuf));
        if (written != 0) {
            ok = false;
            break;
        }
    }

    fclose(f);
    return ok;
}

static bool virtual_pak_create_blank_file(int controller, const char *pak_path) {
    if (!has_cpak(controller)) {
        return false;
    }
    if (cpakfs_format(controller, false) < 0) {
        return false;
    }
    return virtual_pak_dump_controller_to_file(controller, pak_path);
}

static bool virtual_pak_build_file_path(const rom_info_t *rom_info, const char *rom_path, uint8_t slot, char *out, size_t out_len) {
    char game_id[ROM_STABLE_ID_LENGTH];
    if (!rom_info || !out || out_len == 0 || slot < 1) {
        return false;
    }
    if (!rom_info_get_stable_id(rom_info, game_id, sizeof(game_id))) {
        if (!rom_path || !rom_info_get_stable_id_for_path(rom_path, game_id, sizeof(game_id))) {
            return false;
        }
    }
    path_t *path = path_create(g_root_path);
    path_push(path, game_id);
    char filename[32];
    snprintf(filename, sizeof(filename), "slot%02u.pak", (unsigned)slot);
    path_push(path, filename);
    snprintf(out, out_len, "%s", path_get(path));
    path_free(path);
    return true;
}

void virtual_pak_init(const char *storage_prefix) {
    snprintf(g_root_path, sizeof(g_root_path), "%s", VIRTUAL_PAK_DIRECTORY);
    path_t *session_path = path_init(storage_prefix, (char *)VIRTUAL_PAK_SESSION_FILE);
    path_t *root_path = path_init(storage_prefix, (char *)VIRTUAL_PAK_DIRECTORY);
    snprintf(g_session_path, sizeof(g_session_path), "%s", path_get(session_path));
    snprintf(g_root_path, sizeof(g_root_path), "%s", path_get(root_path));
    directory_create(path_get(root_path));
    path_free(session_path);
    path_free(root_path);
}

bool virtual_pak_has_pending_sync(void) {
    virtual_pak_session_t session;
    virtual_pak_session_load(&session);
    return session.active;
}

void virtual_pak_try_sync_pending(void) {
    virtual_pak_session_t session;
    virtual_pak_session_load(&session);
    if (!session.active || !session.pak_path[0]) {
        return;
    }
    if (!has_cpak(session.controller)) {
        return;
    }
    if (!virtual_pak_dump_controller_to_file(session.controller, session.pak_path)) {
        return;
    }
    virtual_pak_session_clear();
}

bool virtual_pak_prepare_launch(menu_t *menu, char *error, size_t error_len) {
    if (error && error_len > 0) {
        error[0] = '\0';
    }
    if (!menu || !menu->load.rom_path) {
        return true;
    }
    if (!menu->load.rom_info.features.controller_pak || !menu->load.rom_info.settings.virtual_pak_enabled) {
        return true;
    }

    virtual_pak_try_sync_pending();
    if (virtual_pak_has_pending_sync()) {
        snprintf(error, error_len, "Previous virtual pak session still needs backup");
        return false;
    }

    if (!has_cpak(VIRTUAL_PAK_CONTROLLER)) {
        snprintf(error, error_len, "Insert a Controller Pak in controller 1");
        return false;
    }

    char pak_path[512];
    if (!virtual_pak_build_file_path(&menu->load.rom_info, path_get(menu->load.rom_path), menu->load.rom_info.settings.virtual_pak_slot, pak_path, sizeof(pak_path))) {
        snprintf(error, error_len, "Couldn't build virtual pak path");
        return false;
    }

    if (!file_exists(pak_path) && !virtual_pak_create_blank_file(VIRTUAL_PAK_CONTROLLER, pak_path)) {
        snprintf(error, error_len, "Couldn't create virtual pak slot");
        return false;
    }

    if (!virtual_pak_restore_file_to_controller(VIRTUAL_PAK_CONTROLLER, pak_path)) {
        snprintf(error, error_len, "Couldn't restore virtual pak to controller 1");
        return false;
    }

    virtual_pak_session_t session;
    virtual_pak_session_init(&session);
    session.active = true;
    session.controller = VIRTUAL_PAK_CONTROLLER;
    session.slot = menu->load.rom_info.settings.virtual_pak_slot;
    snprintf(session.rom_path, sizeof(session.rom_path), "%s", path_get(menu->load.rom_path));
    snprintf(session.pak_path, sizeof(session.pak_path), "%s", pak_path);
    rom_info_get_stable_id(&menu->load.rom_info, session.game_id, sizeof(session.game_id));
    virtual_pak_session_save(&session);
    return true;
}

bool virtual_pak_describe(menu_t *menu, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!menu) {
        return false;
    }
    if (!menu->load.rom_info.features.controller_pak) {
        snprintf(out, out_len, "Not applicable");
        return true;
    }
    if (!menu->load.rom_info.settings.virtual_pak_enabled) {
        snprintf(out, out_len, "Off");
        return true;
    }

    char pak_path[512];
    if (virtual_pak_build_file_path(&menu->load.rom_info, menu->load.rom_path ? path_get(menu->load.rom_path) : NULL, menu->load.rom_info.settings.virtual_pak_slot, pak_path, sizeof(pak_path))) {
        snprintf(out, out_len, "Slot %u (%s)", (unsigned)menu->load.rom_info.settings.virtual_pak_slot, file_exists(pak_path) ? "ready" : "new");
    } else {
        snprintf(out, out_len, "Slot %u", (unsigned)menu->load.rom_info.settings.virtual_pak_slot);
    }
    return true;
}
