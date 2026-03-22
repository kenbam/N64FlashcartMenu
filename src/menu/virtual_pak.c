#include <errno.h>
#include <inttypes.h>
#include <libdragon.h>
#include <mini.c/src/mini.h>
#include <stdio.h>
#include <string.h>

#include "path.h"
#include "rom_info.h"
#include "virtual_pak.h"
#include "utils/hash.h"
#include "utils/cpakfs_utils.h"
#include "utils/fs.h"

#define VIRTUAL_PAK_DIRECTORY "/menu/virtual_paks"
#define VIRTUAL_PAK_SESSION_FILE "/menu/virtual_pak_session.ini"
#define VIRTUAL_PAK_CONTROLLER 0
#define VIRTUAL_PAK_BANK_SIZE 32768
#define VIRTUAL_PAK_LABEL_SIZE 32
#define VIRTUAL_PAK_SLOT_MIN 1
#define VIRTUAL_PAK_SLOT_MAX 4
typedef enum {
    VIRTUAL_PAK_PHASE_NONE = 0,
    VIRTUAL_PAK_PHASE_SLOT_LOADED = 1,
    VIRTUAL_PAK_PHASE_RECOVERY_NEEDED = 2,
} virtual_pak_phase_t;

typedef struct {
    bool active;
    int phase;
    int controller;
    uint8_t slot;
    char game_id[ROM_STABLE_ID_LENGTH];
    char rom_path[512];
    char pak_path[512];
    char backup_path[512];
    uint32_t session_token;
    char slot_label_hex[(VIRTUAL_PAK_LABEL_SIZE * 2) + 1];
} virtual_pak_session_t;

static char g_session_path[512];
static char g_root_path[512];

static void virtual_pak_refresh_accessory_state(void) {
    joypad_poll();
}

static int virtual_pak_read_raw(int controller, uint8_t bank, uint16_t address, void *buffer, size_t length) {
    int result = -1;
    for (int attempt = 0; attempt < 3; attempt++) {
        joypad_poll();
        result = cpak_read((joypad_port_t)controller, bank, address, buffer, length);
        if (result == (int)length) {
            return result;
        }
    }
    return result;
}

static int virtual_pak_write_raw(int controller, uint8_t bank, uint16_t address, const void *buffer, size_t length) {
    int result = -1;
    for (int attempt = 0; attempt < 3; attempt++) {
        joypad_poll();
        result = cpak_write((joypad_port_t)controller, bank, address, buffer, length);
        if (result == (int)length) {
            return result;
        }
    }
    return result;
}

static uint64_t virtual_pak_hash_update(uint64_t h, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV1A_64_PRIME;
    }
    return h;
}

static void virtual_pak_hex_encode(const uint8_t *data, size_t len, char *out, size_t out_len) {
    static const char hex[] = "0123456789ABCDEF";
    if (!out || out_len == 0) {
        return;
    }
    if (!data || out_len < (len * 2 + 1)) {
        out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[data[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static bool virtual_pak_hex_decode(const char *hex, uint8_t *out, size_t out_len) {
    if (!hex || !out) {
        return false;
    }
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2) {
        return false;
    }
    for (size_t i = 0; i < out_len; i++) {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        uint8_t v = 0;
        if (hi >= '0' && hi <= '9') v = (uint8_t)((hi - '0') << 4);
        else if (hi >= 'A' && hi <= 'F') v = (uint8_t)((hi - 'A' + 10) << 4);
        else if (hi >= 'a' && hi <= 'f') v = (uint8_t)((hi - 'a' + 10) << 4);
        else return false;
        if (lo >= '0' && lo <= '9') v |= (uint8_t)(lo - '0');
        else if (lo >= 'A' && lo <= 'F') v |= (uint8_t)(lo - 'A' + 10);
        else if (lo >= 'a' && lo <= 'f') v |= (uint8_t)(lo - 'a' + 10);
        else return false;
        out[i] = v;
    }
    return true;
}

static bool virtual_pak_read_label_block(int controller, uint8_t *out, size_t out_len) {
    if (!out || out_len < VIRTUAL_PAK_LABEL_SIZE || !has_cpak(controller)) {
        return false;
    }
    return virtual_pak_read_raw(controller, 0, 0, out, VIRTUAL_PAK_LABEL_SIZE) == VIRTUAL_PAK_LABEL_SIZE;
}

static bool virtual_pak_file_hash(const char *path, uint64_t *out_hash) {
    if (!path || !out_hash) {
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    uint8_t buffer[512];
    uint64_t h = FNV1A_64_OFFSET_BASIS;
    bool ok = true;
    while (true) {
        size_t n = fread(buffer, 1, sizeof(buffer), f);
        if (n > 0) {
            h = virtual_pak_hash_update(h, buffer, n);
        }
        if (n < sizeof(buffer)) {
            if (ferror(f)) {
                ok = false;
            }
            break;
        }
    }
    fclose(f);
    if (!ok) {
        return false;
    }
    *out_hash = h;
    return true;
}

static bool virtual_pak_controller_hash(int controller, uint64_t *out_hash) {
    if (!out_hash || !has_cpak(controller)) {
        return false;
    }
    int banks = cpak_probe_banks(controller);
    if (banks <= 0) {
        return false;
    }
    uint8_t *bankbuf = malloc(VIRTUAL_PAK_BANK_SIZE);
    if (!bankbuf) {
        return false;
    }
    uint64_t h = FNV1A_64_OFFSET_BASIS;
    bool ok = true;
    for (int bank = 0; bank < banks; bank++) {
        int rd = virtual_pak_read_raw(controller, (uint8_t)bank, 0, bankbuf, VIRTUAL_PAK_BANK_SIZE);
        if (rd != VIRTUAL_PAK_BANK_SIZE) {
            ok = false;
            break;
        }
        h = virtual_pak_hash_update(h, bankbuf, VIRTUAL_PAK_BANK_SIZE);
    }
    free(bankbuf);
    if (!ok) {
        return false;
    }
    *out_hash = h;
    return true;
}

static bool virtual_pak_verify_controller_matches_file(int controller, const char *path) {
    uint64_t file_hash = 0;
    uint64_t controller_hash = 0;
    return virtual_pak_file_hash(path, &file_hash) &&
        virtual_pak_controller_hash(controller, &controller_hash) &&
        (file_hash == controller_hash);
}

static void virtual_pak_session_init(virtual_pak_session_t *session) {
    if (!session) {
        return;
    }
    memset(session, 0, sizeof(*session));
    session->controller = VIRTUAL_PAK_CONTROLLER;
    session->slot = VIRTUAL_PAK_SLOT_MIN;
    session->phase = VIRTUAL_PAK_PHASE_NONE;
}

static void virtual_pak_session_load(virtual_pak_session_t *session) {
    virtual_pak_session_init(session);
    if (!session || !g_session_path[0] || !file_exists(g_session_path)) {
        return;
    }

    mini_t *ini = mini_try_load_safe(g_session_path);
    if (!ini) {
        return;
    }

    session->active = mini_get_bool(ini, "session", "active", false);
    session->phase = mini_get_int(ini, "session", "phase", VIRTUAL_PAK_PHASE_NONE);
    session->controller = mini_get_int(ini, "session", "controller", VIRTUAL_PAK_CONTROLLER);
    int slot = mini_get_int(ini, "session", "slot", VIRTUAL_PAK_SLOT_MIN);
    if (slot < VIRTUAL_PAK_SLOT_MIN) {
        slot = VIRTUAL_PAK_SLOT_MIN;
    } else if (slot > VIRTUAL_PAK_SLOT_MAX) {
        slot = VIRTUAL_PAK_SLOT_MAX;
    }
    session->slot = (uint8_t)slot;
    snprintf(session->game_id, sizeof(session->game_id), "%s", mini_get_string(ini, "session", "game_id", ""));
    snprintf(session->rom_path, sizeof(session->rom_path), "%s", mini_get_string(ini, "session", "rom_path", ""));
    snprintf(session->pak_path, sizeof(session->pak_path), "%s", mini_get_string(ini, "session", "pak_path", ""));
    snprintf(session->backup_path, sizeof(session->backup_path), "%s", mini_get_string(ini, "session", "backup_path", ""));
    session->session_token = (uint32_t)mini_get_int(ini, "session", "session_token", 0);
    snprintf(session->slot_label_hex, sizeof(session->slot_label_hex), "%s", mini_get_string(ini, "session", "slot_label_hex", ""));

    mini_free(ini);
}

static void virtual_pak_session_save(const virtual_pak_session_t *session) {
    if (!session || !g_session_path[0]) {
        return;
    }

    mini_t *ini = mini_create(g_session_path);
    mini_set_bool(ini, "session", "active", session->active);
    mini_set_int(ini, "session", "phase", session->phase);
    mini_set_int(ini, "session", "controller", session->controller);
    mini_set_int(ini, "session", "slot", session->slot);
    mini_set_string(ini, "session", "game_id", session->game_id);
    mini_set_string(ini, "session", "rom_path", session->rom_path);
    mini_set_string(ini, "session", "pak_path", session->pak_path);
    mini_set_string(ini, "session", "backup_path", session->backup_path);
    mini_set_int(ini, "session", "session_token", (int)session->session_token);
    mini_set_string(ini, "session", "slot_label_hex", session->slot_label_hex);
    mini_save_safe(ini, MINI_FLAGS_SKIP_EMPTY_GROUPS);
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

static bool virtual_pak_build_physical_backup_path(int controller, char *out, size_t out_len) {
    if (!out || out_len == 0 || controller < 0 || controller > 3 || !g_root_path[0]) {
        return false;
    }

    path_t *path = path_create(g_root_path);
    char filename[64];
    snprintf(filename, sizeof(filename), "_controller%d_physical_backup.pak", controller + 1);
    path_push(path, filename);
    snprintf(out, out_len, "%s", path_get(path));
    path_free(path);
    return true;
}

static bool virtual_pak_build_rescue_path(int controller, uint32_t session_token, char *out, size_t out_len) {
    if (!out || out_len == 0 || controller < 0 || controller > 3 || !g_root_path[0]) {
        return false;
    }

    path_t *path = path_create(g_root_path);
    char filename[64];
    snprintf(filename, sizeof(filename), "_controller%d_rescue_%08" PRIX32 ".pak", controller + 1, session_token);
    path_push(path, filename);
    snprintf(out, out_len, "%s", path_get(path));
    path_free(path);
    return true;
}

static void virtual_pak_status_init(virtual_pak_status_t *status) {
    if (!status) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->controller = VIRTUAL_PAK_CONTROLLER;
    status->slot = VIRTUAL_PAK_SLOT_MIN;
}

static bool virtual_pak_build_tmp_path(const char *path, const char *suffix, char *out, size_t out_len) {
    if (!path || !suffix || !out || out_len == 0) {
        return false;
    }
    int n = snprintf(out, out_len, "%s%s", path, suffix);
    return n > 0 && (size_t)n < out_len;
}

static bool virtual_pak_patch_file_label(const char *path, const uint8_t *label, size_t len) {
    if (!path || !label || len != VIRTUAL_PAK_LABEL_SIZE) {
        return false;
    }
    FILE *f = fopen(path, "rb+");
    if (!f) {
        return false;
    }
    bool ok = fwrite(label, 1, len, f) == len;
    if (ok) {
        fflush(f);
    }
    ok = ok && fclose(f) == 0;
    return ok;
}

static void virtual_pak_report_progress(virtual_pak_progress_callback_t progress, float start, float end,
                                        int index, int total, const char *message) {
    if (!progress) {
        return;
    }
    if (total <= 0) {
        progress(end, message);
        return;
    }
    float t = (float)index / (float)total;
    progress(start + ((end - start) * t), message);
}

static bool virtual_pak_dump_controller_to_file_ex(int controller, const char *out_path, char *error, size_t error_len,
                                                   virtual_pak_progress_callback_t progress, float progress_start,
                                                   float progress_end, const char *progress_message) {
    if (error && error_len > 0) {
        error[0] = '\0';
    }
    if (!out_path || !out_path[0]) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "Invalid backup path");
        }
        return false;
    }
    if (!has_cpak(controller)) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "Controller Pak is no longer detected on controller %d", controller + 1);
        }
        return false;
    }

    cpakfs_unmount(controller);

    int banks = cpak_probe_banks(controller);
    if (banks <= 0) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "Couldn't probe Controller Pak banks on controller %d", controller + 1);
        }
        return false;
    }
    if (!virtual_pak_ensure_parent_dir(out_path)) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "Couldn't create virtual pak directory");
        }
        return false;
    }

    size_t path_len = strlen(out_path);
    char *tmp_path = malloc(path_len + 5);
    if (!tmp_path) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "Out of memory creating backup path");
        }
        return false;
    }
    memcpy(tmp_path, out_path, path_len);
    memcpy(tmp_path + path_len, ".tmp", 5);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "Couldn't open backup file for writing");
        }
        free(tmp_path);
        return false;
    }

    uint8_t *bankbuf = malloc(VIRTUAL_PAK_BANK_SIZE);
    if (!bankbuf) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "Out of memory allocating Controller Pak buffer");
        }
        fclose(f);
        free(tmp_path);
        return false;
    }

    bool ok = true;
    for (int bank = 0; bank < banks; bank++) {
        virtual_pak_report_progress(progress, progress_start, progress_end, bank, banks, progress_message);
        int rd = virtual_pak_read_raw(controller, (uint8_t)bank, 0, bankbuf, VIRTUAL_PAK_BANK_SIZE);
        if (rd != VIRTUAL_PAK_BANK_SIZE) {
            if (error && error_len > 0) {
                snprintf(error, error_len, "Failed to read Controller Pak bank %d on controller %d (got %d bytes)", bank, controller + 1, rd);
            }
            ok = false;
            break;
        }
        if (fwrite(bankbuf, 1, VIRTUAL_PAK_BANK_SIZE, f) != VIRTUAL_PAK_BANK_SIZE) {
            if (error && error_len > 0) {
                snprintf(error, error_len, "Failed to write Controller Pak backup file");
            }
            ok = false;
            break;
        }
    }

    free(bankbuf);

    if (fclose(f)) {
        if (ok && error && error_len > 0) {
            snprintf(error, error_len, "Couldn't close Controller Pak backup file");
        }
        ok = false;
    }

    if (ok) {
        ok = file_rename(tmp_path, out_path);
        if (!ok && error && error_len > 0) {
            snprintf(error, error_len, "Couldn't finalize Controller Pak backup file");
        }
    }
    if (!ok) {
        remove(tmp_path);
    }
    free(tmp_path);
    if (ok) {
        virtual_pak_report_progress(progress, progress_start, progress_end, banks, banks, progress_message);
    }
    return ok;
}

static bool virtual_pak_dump_controller_to_file(int controller, const char *out_path) {
    return virtual_pak_dump_controller_to_file_ex(controller, out_path, NULL, 0, NULL, 0.0f, 0.0f, NULL);
}

static bool virtual_pak_restore_file_to_controller(int controller, const char *pak_path,
                                                   virtual_pak_progress_callback_t progress, float progress_start,
                                                   float progress_end, const char *progress_message) {
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

    uint8_t *bankbuf = malloc(VIRTUAL_PAK_BANK_SIZE);
    if (!bankbuf) {
        fclose(f);
        return false;
    }

    bool ok = true;
    for (int bank = 0; bank < total_banks; bank++) {
        virtual_pak_report_progress(progress, progress_start, progress_end, bank, total_banks, progress_message);
        size_t bytes_read = fread(bankbuf, 1, VIRTUAL_PAK_BANK_SIZE, f);
        if (bytes_read != VIRTUAL_PAK_BANK_SIZE) {
            ok = false;
            break;
        }
        int written = virtual_pak_write_raw(controller, (uint8_t)bank, 0, bankbuf, VIRTUAL_PAK_BANK_SIZE);
        if (written != VIRTUAL_PAK_BANK_SIZE) {
            ok = false;
            break;
        }
    }

    free(bankbuf);
    fclose(f);
    if (ok) {
        virtual_pak_report_progress(progress, progress_start, progress_end, total_banks, total_banks, progress_message);
    }
    return ok;
}

static bool virtual_pak_dump_controller_to_slot_file(int controller, const char *slot_path, const uint8_t *slot_label, size_t slot_label_len) {
    if (!slot_path || !slot_label || slot_label_len != VIRTUAL_PAK_LABEL_SIZE) {
        return false;
    }

    char tmp_path[576];
    if (!virtual_pak_build_tmp_path(slot_path, ".pending", tmp_path, sizeof(tmp_path))) {
        return false;
    }
    if (!virtual_pak_dump_controller_to_file(controller, tmp_path) ||
        !virtual_pak_verify_controller_matches_file(controller, tmp_path) ||
        !virtual_pak_patch_file_label(tmp_path, slot_label, slot_label_len) ||
        !file_rename(tmp_path, slot_path)) {
        remove(tmp_path);
        return false;
    }
    return true;
}

static bool virtual_pak_dump_controller_to_rescue(int controller, uint32_t session_token) {
    char rescue_path[512];
    if (!virtual_pak_build_rescue_path(controller, session_token, rescue_path, sizeof(rescue_path))) {
        return false;
    }
    return virtual_pak_dump_controller_to_file(controller, rescue_path);
}

static bool virtual_pak_create_blank_file(int controller, const char *pak_path) {
    if (!has_cpak(controller)) {
        return false;
    }
    cpakfs_unmount(controller);
    if (cpakfs_format(controller, false) < 0) {
        return false;
    }
    return virtual_pak_dump_controller_to_file(controller, pak_path);
}

static bool virtual_pak_build_file_path(const rom_info_t *rom_info, const char *rom_path, uint8_t slot, char *out, size_t out_len) {
    char game_id[ROM_STABLE_ID_LENGTH];
    if (!rom_info || !out || out_len == 0 || slot < VIRTUAL_PAK_SLOT_MIN || slot > VIRTUAL_PAK_SLOT_MAX) {
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

void virtual_pak_force_clear_pending(void) {
    virtual_pak_session_clear();
}

bool virtual_pak_get_status(virtual_pak_status_t *status) {
    if (!status) {
        return false;
    }

    virtual_pak_status_init(status);

    virtual_pak_session_t session;
    virtual_pak_session_load(&session);

    status->session_active = session.active;
    status->phase = session.phase;
    status->controller = session.controller;
    status->slot = session.slot;
    status->session_token = session.session_token;

    snprintf(status->game_id, sizeof(status->game_id), "%s", session.game_id);
    snprintf(status->rom_path, sizeof(status->rom_path), "%s", session.rom_path);
    snprintf(status->pak_path, sizeof(status->pak_path), "%s", session.pak_path);
    snprintf(status->backup_path, sizeof(status->backup_path), "%s", session.backup_path);

    if (session.active && session.controller >= 0 && session.controller <= 3 && session.session_token != 0) {
        virtual_pak_build_rescue_path(session.controller, session.session_token, status->rescue_path, sizeof(status->rescue_path));
    }

    status->slot_file_exists = session.pak_path[0] && file_exists(session.pak_path);
    status->backup_file_exists = session.backup_path[0] && file_exists(session.backup_path);
    status->rescue_file_exists = status->rescue_path[0] && file_exists(status->rescue_path);

    virtual_pak_refresh_accessory_state();
    status->has_physical_pak = has_cpak(status->controller);
    if (status->has_physical_pak) {
        status->physical_bank_count = cpak_probe_banks(status->controller);
        if (status->physical_bank_count < 0) {
            status->physical_bank_count = 0;
        }
    }

    return true;
}

void virtual_pak_try_sync_pending(void) {
    virtual_pak_session_t session;
    virtual_pak_session_load(&session);
    if (!session.active || !session.pak_path[0]) {
        return;
    }
    virtual_pak_refresh_accessory_state();
    if (!has_cpak(session.controller)) {
        return;
    }
    uint8_t slot_label[VIRTUAL_PAK_LABEL_SIZE];
    if (!virtual_pak_hex_decode(session.slot_label_hex, slot_label, sizeof(slot_label))) {
        return;
    }
    uint8_t current_label[VIRTUAL_PAK_LABEL_SIZE];
    if (!virtual_pak_read_label_block(session.controller, current_label, sizeof(current_label))) {
        return;
    }

    if (session.phase == VIRTUAL_PAK_PHASE_SLOT_LOADED) {
        if (session.backup_path[0] && file_exists(session.backup_path) &&
            virtual_pak_verify_controller_matches_file(session.controller, session.backup_path)) {
            virtual_pak_session_clear();
            return;
        }

        if (memcmp(current_label, slot_label, sizeof(slot_label)) == 0 &&
            virtual_pak_dump_controller_to_slot_file(session.controller, session.pak_path, slot_label, sizeof(slot_label))) {
            if (session.backup_path[0] && file_exists(session.backup_path) &&
                virtual_pak_restore_file_to_controller(session.controller, session.backup_path, NULL, 0.0f, 0.0f, NULL) &&
                virtual_pak_verify_controller_matches_file(session.controller, session.backup_path)) {
                virtual_pak_refresh_accessory_state();
                virtual_pak_session_clear();
            }
            return;
        }

        if (virtual_pak_dump_controller_to_rescue(session.controller, session.session_token)) {
            session.phase = VIRTUAL_PAK_PHASE_RECOVERY_NEEDED;
            virtual_pak_session_save(&session);
        }
        return;
    }

    if (session.phase == VIRTUAL_PAK_PHASE_RECOVERY_NEEDED) {
        virtual_pak_dump_controller_to_rescue(session.controller, session.session_token);
    }
}

bool virtual_pak_prepare_launch(menu_t *menu, char *error, size_t error_len, virtual_pak_progress_callback_t progress) {
    if (error && error_len > 0) {
        error[0] = '\0';
    }
    if (!menu || !menu->load.rom_path) {
        return true;
    }
    if (!menu->load.rom_info.features.controller_pak || !menu->load.rom_info.settings.virtual_pak_enabled) {
        return true;
    }

    if (progress) {
        progress(0.05f, "Checking Controller Pak...");
    }

    virtual_pak_try_sync_pending();
    if (virtual_pak_has_pending_sync()) {
        snprintf(error, error_len, "Previous virtual pak session still needs recovery. Reinsert the same physical Controller Pak in controller 1 and return to menu.");
        return false;
    }

    virtual_pak_refresh_accessory_state();
    if (!has_cpak(VIRTUAL_PAK_CONTROLLER)) {
        snprintf(error, error_len, "Virtual Controller Pak requires a real Controller Pak in controller 1. Leave it inserted while playing.");
        return false;
    }

    char pak_path[512];
    if (!virtual_pak_build_file_path(&menu->load.rom_info, path_get(menu->load.rom_path), menu->load.rom_info.settings.virtual_pak_slot, pak_path, sizeof(pak_path))) {
        snprintf(error, error_len, "Couldn't build virtual pak path");
        return false;
    }

    char backup_path[512];
    if (!virtual_pak_build_physical_backup_path(VIRTUAL_PAK_CONTROLLER, backup_path, sizeof(backup_path))) {
        snprintf(error, error_len, "Couldn't build Controller Pak backup path");
        return false;
    }
    if (progress) {
        progress(0.25f, "Backing up Controller Pak...");
    }
    if (!virtual_pak_dump_controller_to_file_ex(VIRTUAL_PAK_CONTROLLER, backup_path, error, error_len,
                                                progress, 0.10f, 0.45f, "Backing up Controller Pak...")) {
        return false;
    }
    if (!virtual_pak_verify_controller_matches_file(VIRTUAL_PAK_CONTROLLER, backup_path)) {
        snprintf(error, error_len, "Physical Controller Pak backup verification failed");
        return false;
    }

    if (progress) {
        progress(0.50f, file_exists(pak_path) ? "Loading virtual Pak slot..." : "Creating virtual Pak slot...");
    }
    if (!file_exists(pak_path) && !virtual_pak_create_blank_file(VIRTUAL_PAK_CONTROLLER, pak_path)) {
        virtual_pak_restore_file_to_controller(VIRTUAL_PAK_CONTROLLER, backup_path, NULL, 0.0f, 0.0f, NULL);
        snprintf(error, error_len, "Couldn't create virtual pak slot on the physical Controller Pak");
        return false;
    }

    if (progress) {
        progress(0.72f, "Writing virtual Pak to controller 1...");
    }
    if (!virtual_pak_restore_file_to_controller(VIRTUAL_PAK_CONTROLLER, pak_path, progress, 0.55f, 0.88f,
                                                "Writing virtual Pak to controller 1...")) {
        virtual_pak_restore_file_to_controller(VIRTUAL_PAK_CONTROLLER, backup_path, NULL, 0.0f, 0.0f, NULL);
        snprintf(error, error_len, "Couldn't load the virtual pak onto controller 1's physical Controller Pak");
        return false;
    }
    if (progress) {
        progress(0.90f, "Verifying virtual Pak...");
    }
    if (!virtual_pak_verify_controller_matches_file(VIRTUAL_PAK_CONTROLLER, pak_path)) {
        virtual_pak_restore_file_to_controller(VIRTUAL_PAK_CONTROLLER, backup_path, NULL, 0.0f, 0.0f, NULL);
        snprintf(error, error_len, "Virtual pak verification failed after loading controller 1");
        return false;
    }

    uint8_t slot_label[VIRTUAL_PAK_LABEL_SIZE];
    if (!virtual_pak_read_label_block(VIRTUAL_PAK_CONTROLLER, slot_label, sizeof(slot_label))) {
        virtual_pak_restore_file_to_controller(VIRTUAL_PAK_CONTROLLER, backup_path, NULL, 0.0f, 0.0f, NULL);
        snprintf(error, error_len, "Couldn't capture virtual pak recovery marker state");
        return false;
    }

    virtual_pak_session_t session;
    virtual_pak_session_init(&session);
    session.active = true;
    session.phase = VIRTUAL_PAK_PHASE_SLOT_LOADED;
    session.controller = VIRTUAL_PAK_CONTROLLER;
    session.slot = menu->load.rom_info.settings.virtual_pak_slot;
    session.session_token = (uint32_t)get_ticks_us() ^ (uint32_t)fnv1a64_str(path_get(menu->load.rom_path));
    snprintf(session.rom_path, sizeof(session.rom_path), "%s", path_get(menu->load.rom_path));
    snprintf(session.pak_path, sizeof(session.pak_path), "%s", pak_path);
    snprintf(session.backup_path, sizeof(session.backup_path), "%s", backup_path);
    rom_info_get_stable_id(&menu->load.rom_info, session.game_id, sizeof(session.game_id));
    virtual_pak_hex_encode(slot_label, sizeof(slot_label), session.slot_label_hex, sizeof(session.slot_label_hex));
    virtual_pak_session_save(&session);
    virtual_pak_refresh_accessory_state();
    if (progress) {
        progress(1.0f, "Virtual Pak ready");
    }
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

    if (virtual_pak_has_pending_sync()) {
        snprintf(out, out_len, "Needs sync (menu will try to recover)");
        return true;
    }

    char pak_path[512];
    if (virtual_pak_build_file_path(&menu->load.rom_info, menu->load.rom_path ? path_get(menu->load.rom_path) : NULL, menu->load.rom_info.settings.virtual_pak_slot, pak_path, sizeof(pak_path))) {
        snprintf(out, out_len, "Slot %u (%s, requires physical pak)", (unsigned)menu->load.rom_info.settings.virtual_pak_slot, file_exists(pak_path) ? "ready" : "new");
    } else {
        snprintf(out, out_len, "Slot %u (requires physical pak)", (unsigned)menu->load.rom_info.settings.virtual_pak_slot);
    }
    return true;
}
