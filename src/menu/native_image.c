#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native_image.h"
#include "utils/fs.h"

#define NATIVE_IMAGE_MAGIC (0x4E494D47u)

typedef struct {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t size;
} native_image_header_t;

static uint32_t native_image_read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24)
        | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8)
        | (uint32_t)p[3];
}

static char *native_image_make_sidecar_path(const char *source_path, const char *sidecar_extension) {
    if (!source_path || !sidecar_extension || sidecar_extension[0] == '\0') {
        return NULL;
    }

    size_t source_len = strlen(source_path);
    size_t ext_len = strlen(sidecar_extension);
    char *path = malloc(source_len + ext_len + 1);
    if (!path) {
        return NULL;
    }

    memcpy(path, source_path, source_len);
    memcpy(path + source_len, sidecar_extension, ext_len);
    path[source_len + ext_len] = '\0';
    return path;
}

surface_t *native_image_load_rgba16_file(const char *path, int max_width, int max_height) {
    if (!path) {
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    uint8_t raw_header[sizeof(native_image_header_t)];
    if (fread(raw_header, sizeof(raw_header), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    native_image_header_t header = {
        .magic = native_image_read_be32(&raw_header[0]),
        .width = native_image_read_be32(&raw_header[4]),
        .height = native_image_read_be32(&raw_header[8]),
        .size = native_image_read_be32(&raw_header[12]),
    };

    bool invalid = (header.magic != NATIVE_IMAGE_MAGIC)
        || (header.width == 0) || (header.height == 0)
        || (max_width > 0 && (int)header.width > max_width)
        || (max_height > 0 && (int)header.height > max_height);
    if (invalid) {
        fclose(f);
        return NULL;
    }

    surface_t *image = calloc(1, sizeof(surface_t));
    if (!image) {
        fclose(f);
        return NULL;
    }

    *image = surface_alloc(FMT_RGBA16, header.width, header.height);
    size_t expected_size = (size_t)image->height * (size_t)image->stride;
    if (!image->buffer || header.size != expected_size) {
        if (image->buffer) {
            surface_free(image);
        }
        free(image);
        fclose(f);
        return NULL;
    }

    if (fread(image->buffer, expected_size, 1, f) != 1) {
        surface_free(image);
        free(image);
        fclose(f);
        return NULL;
    }

    fclose(f);
    return image;
}

surface_t *native_image_load_sidecar_rgba16(const char *source_path, const char *sidecar_extension, int max_width, int max_height) {
    char *sidecar_path = native_image_make_sidecar_path(source_path, sidecar_extension);
    if (!sidecar_path) {
        return NULL;
    }

    surface_t *image = NULL;
    if (file_exists(sidecar_path)) {
        image = native_image_load_rgba16_file(sidecar_path, max_width, max_height);
    }

    free(sidecar_path);
    return image;
}

bool native_image_sidecar_exists(const char *source_path, const char *sidecar_extension) {
    char *sidecar_path = native_image_make_sidecar_path(source_path, sidecar_extension);
    if (!sidecar_path) {
        return false;
    }

    bool exists = file_exists(sidecar_path);
    free(sidecar_path);
    return exists;
}
