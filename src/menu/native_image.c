#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native_image.h"
#include "utils/fs.h"

#define NATIVE_IMAGE_MAGIC (0x4E494D47u)
#define NATIVE_IMAGE_MAX_DIM 1024

typedef struct {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t size;
} native_image_header_t;

static native_image_error_t g_native_image_last_error = NATIVE_IMAGE_OK;

static uint32_t native_image_read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24)
        | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8)
        | (uint32_t)p[3];
}

static void native_image_set_last_error(native_image_error_t err) {
    g_native_image_last_error = err;
}

native_image_error_t native_image_get_last_error(void) {
    return g_native_image_last_error;
}

const char *native_image_error_string(native_image_error_t err) {
    switch (err) {
        case NATIVE_IMAGE_OK:
            return "ok";
        case NATIVE_IMAGE_ERR_INVALID_ARGUMENT:
            return "invalid argument";
        case NATIVE_IMAGE_ERR_OUT_OF_MEMORY:
            return "out of memory";
        case NATIVE_IMAGE_ERR_SIDECAR_MISSING:
            return "native file missing";
        case NATIVE_IMAGE_ERR_OPEN_FAILED:
            return "open failed";
        case NATIVE_IMAGE_ERR_READ_HEADER_FAILED:
            return "header read failed";
        case NATIVE_IMAGE_ERR_INVALID_HEADER:
            return "invalid header";
        case NATIVE_IMAGE_ERR_BUFFER_ALLOC_FAILED:
            return "surface allocation failed";
        case NATIVE_IMAGE_ERR_SIZE_MISMATCH:
            return "payload size mismatch";
        case NATIVE_IMAGE_ERR_READ_DATA_FAILED:
            return "payload read failed";
        default:
            return "unknown error";
    }
}

static char *native_image_make_sidecar_path(const char *source_path, const char *sidecar_extension) {
    if (!source_path || !sidecar_extension || sidecar_extension[0] == '\0') {
        native_image_set_last_error(NATIVE_IMAGE_ERR_INVALID_ARGUMENT);
        return NULL;
    }

    size_t source_len = strlen(source_path);
    size_t ext_len = strlen(sidecar_extension);
    char *path = malloc(source_len + ext_len + 1);
    if (!path) {
        native_image_set_last_error(NATIVE_IMAGE_ERR_OUT_OF_MEMORY);
        return NULL;
    }

    memcpy(path, source_path, source_len);
    memcpy(path + source_len, sidecar_extension, ext_len);
    path[source_len + ext_len] = '\0';
    return path;
}

surface_t *native_image_load_rgba16_file(const char *path, int max_width, int max_height) {
    if (!path) {
        native_image_set_last_error(NATIVE_IMAGE_ERR_INVALID_ARGUMENT);
        return NULL;
    }

    native_image_set_last_error(NATIVE_IMAGE_OK);

    FILE *f = fopen(path, "rb");
    if (!f) {
        native_image_set_last_error(NATIVE_IMAGE_ERR_OPEN_FAILED);
        return NULL;
    }

    uint8_t raw_header[sizeof(native_image_header_t)];
    if (fread(raw_header, sizeof(raw_header), 1, f) != 1) {
        fclose(f);
        native_image_set_last_error(NATIVE_IMAGE_ERR_READ_HEADER_FAILED);
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
        || (header.width > NATIVE_IMAGE_MAX_DIM) || (header.height > NATIVE_IMAGE_MAX_DIM)
        || (max_width > 0 && (int)header.width > max_width)
        || (max_height > 0 && (int)header.height > max_height);
    if (invalid) {
        fclose(f);
        native_image_set_last_error(NATIVE_IMAGE_ERR_INVALID_HEADER);
        return NULL;
    }

    surface_t *image = calloc(1, sizeof(surface_t));
    if (!image) {
        fclose(f);
        native_image_set_last_error(NATIVE_IMAGE_ERR_OUT_OF_MEMORY);
        return NULL;
    }

    *image = surface_alloc(FMT_RGBA16, header.width, header.height);
    size_t expected_size = (size_t)image->height * (size_t)image->stride;
    if (!image->buffer || header.size != expected_size) {
        native_image_error_t err = !image->buffer ? NATIVE_IMAGE_ERR_BUFFER_ALLOC_FAILED : NATIVE_IMAGE_ERR_SIZE_MISMATCH;
        if (image->buffer) {
            surface_free(image);
        }
        free(image);
        fclose(f);
        native_image_set_last_error(err);
        return NULL;
    }

    if (fread(image->buffer, expected_size, 1, f) != 1) {
        surface_free(image);
        free(image);
        fclose(f);
        native_image_set_last_error(NATIVE_IMAGE_ERR_READ_DATA_FAILED);
        return NULL;
    }

    fclose(f);
    native_image_set_last_error(NATIVE_IMAGE_OK);
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
    } else {
        native_image_set_last_error(NATIVE_IMAGE_ERR_SIDECAR_MISSING);
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
