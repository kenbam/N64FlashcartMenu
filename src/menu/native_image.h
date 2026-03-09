#ifndef MENU_NATIVE_IMAGE_H__
#define MENU_NATIVE_IMAGE_H__

#include <libdragon.h>
#include <stdbool.h>

surface_t *native_image_load_rgba16_file(const char *path, int max_width, int max_height);
surface_t *native_image_load_sidecar_rgba16(const char *source_path, const char *sidecar_extension, int max_width, int max_height);
bool native_image_sidecar_exists(const char *source_path, const char *sidecar_extension);

#endif
