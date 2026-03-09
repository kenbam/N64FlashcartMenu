/**
 * @file rom_patch.c
 * @brief Optional ROM patch pipeline (MVP: IPS manifests).
 * @ingroup menu
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>

#include <mini.c/src/mini.h>
#include <libdragon.h>

#include "path.h"
#include "rom_info.h"
#include "rom_patch.h"
#include "utils/fs.h"
#include "utils/hash.h"

#define PATCHES_DIR "menu/patches"
#define PATCH_CACHE_DIR "menu/cache/patched"
#define PATCH_MANIFEST_NAME "default.ini"
#define PATCH_MAX_FILES 8
#define PATCH_MAX_MANIFESTS 32

typedef struct {
    char files[PATCH_MAX_FILES][256];
    int files_count;
    char prepatched_file[256];
    char type[16];
    uint64_t expected_check_code;
    bool has_expected_check_code;
    int64_t expected_rom_size;
    bool has_expected_rom_size;
    char expected_game_code[8];
} patch_manifest_t;

static void sanitize_token(const char *input, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    size_t j = 0;
    for (size_t i = 0; input && input[i] != '\0' && j + 1 < out_len; i++) {
        char c = input[i];
        if (isalnum((unsigned char)c)) {
            out[j++] = c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';
}

static bool read_u16_be(FILE *f, uint16_t *out) {
    uint8_t b[2];
    if (fread(b, 1, sizeof(b), f) != sizeof(b)) {
        return false;
    }
    *out = ((uint16_t)b[0] << 8) | ((uint16_t)b[1]);
    return true;
}

static bool read_u24_be(FILE *f, uint32_t *out) {
    uint8_t b[3];
    if (fread(b, 1, sizeof(b), f) != sizeof(b)) {
        return false;
    }
    *out = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | ((uint32_t)b[2]);
    return true;
}

static bool copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    uint8_t buf[16 * 1024];
    bool ok = true;
    while (1) {
        size_t r = fread(buf, 1, sizeof(buf), in);
        if (r > 0 && fwrite(buf, 1, r, out) != r) {
            ok = false;
            break;
        }
        if (r < sizeof(buf)) {
            if (ferror(in)) {
                ok = false;
            }
            break;
        }
    }

    if (fclose(out) != 0) {
        ok = false;
    }
    fclose(in);
    return ok;
}

static bool manifest_load(const char *manifest_path, patch_manifest_t *out) {
    mini_t *ini = mini_load((char *)manifest_path);
    if (!ini) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    const char *file = mini_get_string(ini, "patch", "file", NULL);
    if (file && file[0] != '\0') {
        snprintf(out->files[out->files_count++], sizeof(out->files[0]), "%s", file);
    }
    for (int i = 1; i <= PATCH_MAX_FILES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "file_%d", i);
        const char *f = mini_get_string(ini, "patch", key, NULL);
        if (f && f[0] != '\0') {
            if (out->files_count >= PATCH_MAX_FILES) {
                mini_free(ini);
                return false;
            }
            snprintf(out->files[out->files_count++], sizeof(out->files[0]), "%s", f);
        }
    }
    if (out->files_count <= 0) {
        mini_free(ini);
        return false;
    }

    snprintf(out->type, sizeof(out->type), "%s", mini_get_string(ini, "patch", "type", "ips"));
    snprintf(out->prepatched_file, sizeof(out->prepatched_file), "%s", mini_get_string(ini, "patch", "prepatched_file", ""));

    const char *check_code = mini_get_string(ini, "compatibility", "expected_check_code", NULL);
    if (check_code && check_code[0] != '\0') {
        char *endptr = NULL;
        errno = 0;
        uint64_t v = strtoull(check_code, &endptr, 0);
        if ((errno == 0) && endptr && (*endptr == '\0')) {
            out->expected_check_code = v;
            out->has_expected_check_code = true;
        }
    }

    const char *rom_size = mini_get_string(ini, "compatibility", "expected_rom_size", NULL);
    if (rom_size && rom_size[0] != '\0') {
        char *endptr = NULL;
        errno = 0;
        int64_t v = strtoll(rom_size, &endptr, 0);
        if ((errno == 0) && endptr && (*endptr == '\0') && (v > 0)) {
            out->expected_rom_size = v;
            out->has_expected_rom_size = true;
        }
    }

    snprintf(
        out->expected_game_code,
        sizeof(out->expected_game_code),
        "%s",
        mini_get_string(ini, "compatibility", "expected_game_code", "")
    );

    mini_free(ini);
    return true;
}

static bool apply_ips(const char *target_rom_path, const char *ips_path) {
    FILE *patch = fopen(ips_path, "rb");
    if (!patch) {
        return false;
    }
    FILE *rom = fopen(target_rom_path, "rb+");
    if (!rom) {
        fclose(patch);
        return false;
    }

    char magic[5];
    if (fread(magic, 1, sizeof(magic), patch) != sizeof(magic) || memcmp(magic, "PATCH", sizeof(magic)) != 0) {
        fclose(rom);
        fclose(patch);
        return false;
    }

    int64_t rom_size = file_get_size((char *)target_rom_path);
    bool ok = (rom_size > 0);
    while (ok) {
        uint32_t offset = 0;
        long pos_before = ftell(patch);
        if (!read_u24_be(patch, &offset)) {
            ok = false;
            break;
        }

        if (offset == 0x454F46) { // "EOF"
            break;
        }

        uint16_t size = 0;
        if (!read_u16_be(patch, &size)) {
            ok = false;
            break;
        }

        if (size == 0) {
            uint16_t rle_size = 0;
            uint8_t value = 0;
            if (!read_u16_be(patch, &rle_size) || fread(&value, 1, 1, patch) != 1) {
                ok = false;
                break;
            }
            if ((int64_t)offset + (int64_t)rle_size > rom_size) {
                debugf("ROM patch: IPS RLE write outside ROM bounds at %lu\n", (unsigned long)pos_before);
                ok = false;
                break;
            }
            if (fseek(rom, (long)offset, SEEK_SET) != 0) {
                ok = false;
                break;
            }
            uint8_t fill[256];
            memset(fill, value, sizeof(fill));
            uint32_t remaining = rle_size;
            while (remaining > 0) {
                uint32_t chunk = remaining > sizeof(fill) ? sizeof(fill) : remaining;
                if (fwrite(fill, 1, chunk, rom) != chunk) {
                    ok = false;
                    break;
                }
                remaining -= chunk;
            }
        } else {
            if ((int64_t)offset + (int64_t)size > rom_size) {
                debugf("ROM patch: IPS write outside ROM bounds at %lu\n", (unsigned long)pos_before);
                ok = false;
                break;
            }
            uint8_t *buf = malloc(size);
            if (!buf) {
                ok = false;
                break;
            }
            if (fread(buf, 1, size, patch) != size) {
                free(buf);
                ok = false;
                break;
            }
            if (fseek(rom, (long)offset, SEEK_SET) != 0 || fwrite(buf, 1, size, rom) != size) {
                free(buf);
                ok = false;
                break;
            }
            free(buf);
        }
    }

    if (fclose(rom) != 0) {
        ok = false;
    }
    fclose(patch);
    return ok;
}

static void build_patch_dirs(menu_t *menu, path_t **region_dir, path_t **global_dir) {
    char c0[2] = { menu->load.rom_info.game_code[0], '\0' };
    char c1[2] = { menu->load.rom_info.game_code[1], '\0' };
    char c2[2] = { menu->load.rom_info.game_code[2], '\0' };
    char c3[2] = { menu->load.rom_info.game_code[3], '\0' };

    *region_dir = path_init(menu->storage_prefix, PATCHES_DIR);
    path_push(*region_dir, c0);
    path_push(*region_dir, c1);
    path_push(*region_dir, c2);
    path_push(*region_dir, c3);

    *global_dir = path_clone(*region_dir);
    path_pop(*global_dir);
}

static void list_manifests(path_t *dir_path, char names[PATCH_MAX_MANIFESTS][128], int *count) {
    *count = 0;
    if (!dir_path || !directory_exists(path_get(dir_path))) {
        return;
    }

    dir_t info;
    int result = dir_findfirst(path_get(dir_path), &info);
    while (result == 0) {
        if ((info.d_type != DT_DIR) && file_has_extensions(info.d_name, (const char *[]){"ini", NULL})) {
            if (*count < PATCH_MAX_MANIFESTS) {
                snprintf(names[*count], 128, "%s", info.d_name);
                (*count)++;
            }
        }
        result = dir_findnext(path_get(dir_path), &info);
    }

    for (int i = 1; i < *count; i++) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "%s", names[i]);
        int j = i - 1;
        while (j >= 0 && strcasecmp(names[j], tmp) > 0) {
            snprintf(names[j + 1], 128, "%s", names[j]);
            j--;
        }
        snprintf(names[j + 1], 128, "%s", tmp);
    }
}

static bool resolve_manifest_path(menu_t *menu, char out_path[512], char out_name[128]) {
    path_t *region_dir = NULL;
    path_t *global_dir = NULL;
    build_patch_dirs(menu, &region_dir, &global_dir);

    const char *selected = menu->load.rom_info.settings.patch_profile;
    if (selected && selected[0] != '\0') {
        path_push(region_dir, (char *)selected);
        if (file_exists(path_get(region_dir))) {
            snprintf(out_path, 512, "%s", path_get(region_dir));
            snprintf(out_name, 128, "%s", selected);
            path_free(region_dir);
            path_free(global_dir);
            return true;
        }
        path_pop(region_dir);

        path_push(global_dir, (char *)selected);
        if (file_exists(path_get(global_dir))) {
            snprintf(out_path, 512, "%s", path_get(global_dir));
            snprintf(out_name, 128, "%s", selected);
            path_free(region_dir);
            path_free(global_dir);
            return true;
        }
        path_pop(global_dir);
    }

    path_push(region_dir, PATCH_MANIFEST_NAME);
    if (file_exists(path_get(region_dir))) {
        snprintf(out_path, 512, "%s", path_get(region_dir));
        snprintf(out_name, 128, "%s", PATCH_MANIFEST_NAME);
        path_free(region_dir);
        path_free(global_dir);
        return true;
    }
    path_pop(region_dir);

    path_push(global_dir, PATCH_MANIFEST_NAME);
    if (file_exists(path_get(global_dir))) {
        snprintf(out_path, 512, "%s", path_get(global_dir));
        snprintf(out_name, 128, "%s", PATCH_MANIFEST_NAME);
        path_free(region_dir);
        path_free(global_dir);
        return true;
    }
    path_pop(global_dir);

    char manifest_names[PATCH_MAX_MANIFESTS][128];
    int manifest_count = 0;

    list_manifests(region_dir, manifest_names, &manifest_count);
    if (manifest_count > 0) {
        path_push(region_dir, manifest_names[0]);
        snprintf(out_path, 512, "%s", path_get(region_dir));
        snprintf(out_name, 128, "%s", manifest_names[0]);
        path_free(region_dir);
        path_free(global_dir);
        return true;
    }

    list_manifests(global_dir, manifest_names, &manifest_count);
    if (manifest_count > 0) {
        path_push(global_dir, manifest_names[0]);
        snprintf(out_path, 512, "%s", path_get(global_dir));
        snprintf(out_name, 128, "%s", manifest_names[0]);
        path_free(region_dir);
        path_free(global_dir);
        return true;
    }

    path_free(region_dir);
    path_free(global_dir);
    return false;
}

static bool validate_manifest_compatibility(
    menu_t *menu,
    const patch_manifest_t *manifest,
    const char *source_rom_path
) {
    if (manifest->has_expected_check_code && manifest->expected_check_code != menu->load.rom_info.check_code) {
        debugf("ROM patch: check_code mismatch (rom=%016llX expected=%016llX)\n",
            (unsigned long long)menu->load.rom_info.check_code,
            (unsigned long long)manifest->expected_check_code);
        return false;
    }

    if (manifest->expected_game_code[0] != '\0') {
        char rom_game_code[5];
        memcpy(rom_game_code, menu->load.rom_info.game_code, 4);
        rom_game_code[4] = '\0';
        if (strcasecmp(manifest->expected_game_code, rom_game_code) != 0) {
            debugf("ROM patch: game code mismatch (rom=%s expected=%s)\n", rom_game_code, manifest->expected_game_code);
            return false;
        }
    }

    if (manifest->has_expected_rom_size) {
        int64_t rom_size = file_get_size((char *)source_rom_path);
        if (rom_size <= 0 || rom_size != manifest->expected_rom_size) {
            debugf("ROM patch: rom size mismatch (rom=%lld expected=%lld)\n",
                (long long)rom_size, (long long)manifest->expected_rom_size);
            return false;
        }
    }

    return true;
}

static uint32_t build_cache_key(
    const patch_manifest_t *manifest,
    const char *manifest_name,
    const char *manifest_path,
    const char patch_paths[PATCH_MAX_FILES][512]
) {
    uint32_t h = FNV1A_32_OFFSET_BASIS;
    h = fnv1a32_str(h, manifest_name);
    h = fnv1a32_str(h, manifest_path);
    h = fnv1a32_str(h, manifest->type);
    h = fnv1a32_u64(h, (uint64_t)manifest->has_expected_check_code);
    h = fnv1a32_u64(h, manifest->expected_check_code);
    h = fnv1a32_u64(h, (uint64_t)manifest->has_expected_rom_size);
    h = fnv1a32_u64(h, (uint64_t)manifest->expected_rom_size);
    h = fnv1a32_str(h, manifest->expected_game_code);

    for (int i = 0; i < manifest->files_count; i++) {
        h = fnv1a32_str(h, manifest->files[i]);
        h = fnv1a32_str(h, patch_paths[i]);
        int64_t sz = file_get_size((char *)patch_paths[i]);
        h = fnv1a32_u64(h, (uint64_t)sz);
    }
    return h;
}

rom_patch_result_t rom_patch_prepare_launch(
    menu_t *menu,
    const char *source_rom_path,
    char *out_rom_path,
    size_t out_rom_path_len
) {
    if (!menu || !source_rom_path || !out_rom_path || out_rom_path_len == 0) {
        return ROM_PATCH_IO_ERROR;
    }

    out_rom_path[0] = '\0';

    if (menu->load.rom_info.endianness != ENDIANNESS_BIG) {
        debugf("ROM patch: skipped, unsupported ROM endianness for safe IPS pipeline.\n");
        return ROM_PATCH_INCOMPATIBLE;
    }

    char manifest_path[512];
    char manifest_name[128];
    if (!resolve_manifest_path(menu, manifest_path, manifest_name)) {
        return ROM_PATCH_SKIPPED;
    }

    patch_manifest_t manifest;
    if (!manifest_load(manifest_path, &manifest)) {
        debugf("ROM patch: invalid manifest %s\n", manifest_path);
        return ROM_PATCH_FORMAT_ERROR;
    }

    bool patch_type_ips = (strcasecmp(manifest.type, "ips") == 0);
    bool patch_type_xdelta = (strcasecmp(manifest.type, "xdelta") == 0);
    if (!patch_type_ips && !patch_type_xdelta) {
        debugf("ROM patch: unsupported type '%s' in %s\n", manifest.type, manifest_path);
        return ROM_PATCH_INCOMPATIBLE;
    }

    if (!validate_manifest_compatibility(menu, &manifest, source_rom_path)) {
        return ROM_PATCH_INCOMPATIBLE;
    }

    path_t *manifest_dir = path_create(manifest_path);
    path_pop(manifest_dir);
    char patch_paths[PATCH_MAX_FILES][512];
    memset(patch_paths, 0, sizeof(patch_paths));
    for (int i = 0; i < manifest.files_count; i++) {
        path_push(manifest_dir, manifest.files[i]);
        snprintf(patch_paths[i], sizeof(patch_paths[i]), "%s", path_get(manifest_dir));
        path_pop(manifest_dir);
        if (!file_exists(patch_paths[i])) {
            debugf("ROM patch: patch file missing: %s\n", patch_paths[i]);
            path_free(manifest_dir);
            return ROM_PATCH_IO_ERROR;
        }
    }

    path_t *cache_dir = path_init(menu->storage_prefix, PATCH_CACHE_DIR);
    if (directory_create(path_get(cache_dir))) {
        path_free(cache_dir);
        path_free(manifest_dir);
        return ROM_PATCH_IO_ERROR;
    }

    char game_code[5];
    memcpy(game_code, menu->load.rom_info.game_code, 4);
    game_code[4] = '\0';

    char patch_token[96];
    sanitize_token(manifest_name, patch_token, sizeof(patch_token));
    if (patch_token[0] == '\0') {
        snprintf(patch_token, sizeof(patch_token), "patch");
    }

    uint32_t cache_key = build_cache_key(&manifest, manifest_name, manifest_path, patch_paths);
    char base_name[192];
    snprintf(base_name, sizeof(base_name), "%s_%016llX_%08lX_%s.z64",
        game_code,
        (unsigned long long)menu->load.rom_info.check_code,
        (unsigned long)cache_key,
        patch_token);
    path_push(cache_dir, base_name);
    char cache_rom_path[512];
    snprintf(cache_rom_path, sizeof(cache_rom_path), "%s", path_get(cache_dir));
    path_free(cache_dir);

    if (patch_type_ips) {
        if (!file_exists(cache_rom_path)) {
            if (!copy_file(source_rom_path, cache_rom_path)) {
                remove(cache_rom_path);
                path_free(manifest_dir);
                return ROM_PATCH_IO_ERROR;
            }
            for (int i = 0; i < manifest.files_count; i++) {
                if (!apply_ips(cache_rom_path, patch_paths[i])) {
                    remove(cache_rom_path);
                    path_free(manifest_dir);
                    return ROM_PATCH_FORMAT_ERROR;
                }
            }
        }
        snprintf(out_rom_path, out_rom_path_len, "%s", cache_rom_path);
        path_free(manifest_dir);
        return ROM_PATCH_OK;
    }

    // xdelta support uses pre-generated patched ROM file.
    if (manifest.prepatched_file[0] == '\0') {
        debugf("ROM patch: xdelta requires 'prepatched_file' in manifest (%s)\n", manifest_path);
        path_free(manifest_dir);
        return ROM_PATCH_INCOMPATIBLE;
    }
    path_push(manifest_dir, manifest.prepatched_file);
    char prepatched_path[512];
    snprintf(prepatched_path, sizeof(prepatched_path), "%s", path_get(manifest_dir));
    path_free(manifest_dir);
    if (!file_exists(prepatched_path)) {
        debugf("ROM patch: xdelta prepatched file missing: %s\n", prepatched_path);
        return ROM_PATCH_IO_ERROR;
    }
    snprintf(out_rom_path, out_rom_path_len, "%s", prepatched_path);
    return ROM_PATCH_OK;
}
