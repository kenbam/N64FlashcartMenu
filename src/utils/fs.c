
/**
 * @file fs.c
 * @brief Implementation of file system utility functions.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fatfs/ff.h>
#include <mini.c/src/mini.h>

#include "fs.h"
#include "utils.h"

/**
 * @brief Strip the file system prefix from a path.
 *
 * Removes the file system prefix (such as ":/") from the provided path string.
 *
 * @param path The path from which to strip the prefix.
 * @return A pointer to the path without the prefix.
 */
char *strip_fs_prefix(char *path) {
    const char *prefix = ":/";
    char *found = strstr(path, prefix);
    if (found) {
        return (found + strlen(prefix) - 1);
    }
    return path;
}

/**
 * @brief Get the basename of a path.
 *
 * Returns a pointer to the basename (the final component) of the provided path.
 *
 * @param path The path from which to get the basename.
 * @return A pointer to the basename of the path.
 */
char *file_basename(char *path) {
    char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

/**
 * @brief Check if a file exists at the given path.
 *
 * Checks if a file exists at the specified path.
 *
 * @param path The path to the file.
 * @return true if the file exists, false otherwise.
 */
bool file_exists(char *path) {
    struct stat st;
    int error = stat(path, &st);
    return ((error == 0) && S_ISREG(st.st_mode));
}

/**
 * @brief Get the size of a file at the given path.
 *
 * Returns the size of the file at the specified path in bytes.
 *
 * @param path The path to the file.
 * @return The size of the file in bytes, or -1 if the file does not exist or an error occurs.
 */
int64_t file_get_size(char *path) {
    struct stat st;
    if (stat(path, &st)) {
        return -1;
    }
    return (int64_t)(st.st_size);
}

/**
 * @brief Allocate a file of the specified size at the given path.
 *
 * Creates a file of the specified size at the provided path. The file is filled with zeros.
 *
 * @param path The path to the file.
 * @param size The size of the file to create in bytes.
 * @return true if the file was successfully created, false otherwise.
 */
bool file_allocate(char *path, size_t size) {
    FILE *f;
    if ((f = fopen(path, "wb")) == NULL) {
        return true;
    }
    if (fseek(f, size, SEEK_SET)) {
        fclose(f);
        return true;
    }
    if ((size_t)(ftell(f)) != size) {
        fclose(f);
        return true;
    }
    if (fclose(f)) {
        return true;
    }
    return false;
}

/**
 * @brief Fill a file with the specified value.
 *
 * Fills the file at the given path with the specified byte value.
 *
 * @param path The path to the file.
 * @param value The value to fill the file with (byte).
 * @return true if the file was successfully filled, false otherwise.
 */
bool file_fill(char *path, uint8_t value) {
    FILE *f;
    bool error = false;
    uint8_t buffer[FS_SECTOR_SIZE * 8];
    size_t bytes_to_write;

    for (size_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = value;
    }

    if ((f = fopen(path, "rb+")) == NULL) {
        return true;
    }

    setbuf(f, NULL);

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    for (size_t i = 0; i < size; i += sizeof(buffer)) {
        bytes_to_write = MIN(size - ftell(f), sizeof(buffer));
        if (fwrite(buffer, 1, bytes_to_write, f) != bytes_to_write) {
            error = true;
            break;
        }
    }

    if (fclose(f)) {
        error = true;
    }

    return error;
}

/**
 * @brief Check if a file has one of the specified extensions.
 *
 * Checks if the file at the given path has one of the specified extensions.
 *
 * @param path The path to the file.
 * @param extensions An array of extensions to check (NULL-terminated).
 * @return true if the file has one of the specified extensions, false otherwise.
 */
bool file_has_extensions(char *path, const char *extensions[]) {
    char *ext = strrchr(path, '.');

    if (ext == NULL) {
        return false;
    }

    while (*extensions != NULL) {
        if (strcasecmp(ext + 1, *extensions) == 0) {
            return true;
        }
        extensions++;
    }

    return false;
}

/**
 * @brief Check if a directory exists at the given path.
 *
 * Checks if a directory exists at the specified path.
 *
 * @param path The path to the directory.
 * @return true if the directory exists, false otherwise.
 */
bool directory_exists(char *path) {
    struct stat st;
    int error = stat(path, &st);
    return ((error == 0) && S_ISDIR(st.st_mode));
}

/**
 * @brief Create a directory at the given path.
 *
 * Creates a directory at the specified path, including any necessary parent directories.
 *
 * @param path The path to the directory.
 * @return false if the directory was successfully created, true if there was an error.
 */
bool directory_create(char *path) {
    bool error = false;

    if (directory_exists(path)) {
        return false;
    }

    char *directory = strdup(path);
    char *separator = strip_fs_prefix(directory);

    if (separator != directory) {
        separator++;
    }

    do {
        separator = strchr(separator, '/');

        if (separator != NULL) {
            *separator++ = '\0';
        }

        if (directory[0] != '\0') {
            if (mkdir(directory, 0777) && (errno != EEXIST)) {
                error = true;
                break;
            }
        }

        if (separator != NULL) {
            *(separator - 1) = '/';
        }
    } while (separator != NULL);

    free(directory);

    return error;
}

bool file_rename(const char *old_path, const char *new_path) {
    const char *old_fat = strip_fs_prefix((char *)old_path);
    const char *new_fat = strip_fs_prefix((char *)new_path);
    f_unlink(new_fat);
    return (f_rename(old_fat, new_fat) == FR_OK);
}

/* NOTE: Coupled to mini_t struct layout (accesses ini->path directly). */
int mini_save_safe(void *opaque, int flags) {
    mini_t *ini = (mini_t *)opaque;
    if (!ini->path || strlen(ini->path) < 1) {
        return MINI_INVALID_PATH;
    }

    size_t path_len = strlen(ini->path);
    char *tmp_path = malloc(path_len + 5);
    if (!tmp_path) {
        /* OOM fallback: direct write */
        return mini_save(ini, flags);
    }
    memcpy(tmp_path, ini->path, path_len);
    memcpy(tmp_path + path_len, ".tmp", 5);

    /* Write to .tmp first */
    char *orig_path = ini->path;
    ini->path = tmp_path;
    int result = mini_save(ini, flags);
    ini->path = orig_path;

    if (result != MINI_OK) {
        /* .tmp write failed — fall back to direct write */
        free(tmp_path);
        return mini_save(ini, flags);
    }

    /* .tmp is good — atomic rename over the real file */
    if (file_rename(tmp_path, ini->path)) {
        free(tmp_path);
        return MINI_OK;
    }

    /* Rename failed — fall back to direct write, clean up .tmp */
    f_unlink(strip_fs_prefix(tmp_path));
    free(tmp_path);
    return mini_save(ini, flags);
}

/* NOTE: Coupled to mini_t struct layout (sets ini->path on recovery). */
void *mini_try_load_safe(const char *path) {
    mini_t *ini = mini_try_load(path);

    /* If the file was empty/missing, check for orphaned .tmp from interrupted save */
    if (ini && ini->head && !ini->head->head) {
        size_t path_len = strlen(path);
        char *tmp_path = malloc(path_len + 5);
        if (tmp_path) {
            memcpy(tmp_path, path, path_len);
            memcpy(tmp_path + path_len, ".tmp", 5);
            if (file_exists(tmp_path)) {
                mini_t *recovered = mini_try_load(tmp_path);
                if (recovered && recovered->head && recovered->head->head) {
                    /* .tmp has data — use it and rename over the real file */
                    free(recovered->path);
                    recovered->path = strdup(path);
                    mini_free(ini);
                    file_rename(tmp_path, path);
                    free(tmp_path);
                    return recovered;
                }
                mini_free(recovered);
            }
            free(tmp_path);
        }
    }

    return ini;
}
