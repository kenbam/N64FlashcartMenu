#ifndef MENU_NATIVE_IMAGE_H__
#define MENU_NATIVE_IMAGE_H__

#include <libdragon.h>
#include <stdbool.h>

typedef enum {
    NATIVE_IMAGE_OK = 0,
    NATIVE_IMAGE_ERR_INVALID_ARGUMENT,
    NATIVE_IMAGE_ERR_OUT_OF_MEMORY,
    NATIVE_IMAGE_ERR_SIDECAR_MISSING,
    NATIVE_IMAGE_ERR_OPEN_FAILED,
    NATIVE_IMAGE_ERR_READ_HEADER_FAILED,
    NATIVE_IMAGE_ERR_INVALID_HEADER,
    NATIVE_IMAGE_ERR_BUFFER_ALLOC_FAILED,
    NATIVE_IMAGE_ERR_SIZE_MISMATCH,
    NATIVE_IMAGE_ERR_READ_DATA_FAILED,
} native_image_error_t;

surface_t *native_image_load_rgba16_file(const char *path, int max_width, int max_height);
surface_t *native_image_load_sidecar_rgba16(const char *source_path, const char *sidecar_extension, int max_width, int max_height);
bool native_image_sidecar_exists(const char *source_path, const char *sidecar_extension);
native_image_error_t native_image_get_last_error(void);
const char *native_image_error_string(native_image_error_t err);

#endif
