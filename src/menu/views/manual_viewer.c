#include <libdragon.h>
#include <mini.c/src/mini.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#include "views.h"
#include "utils/fs.h"
#include "../fonts.h"
#include "../native_image.h"
#include "../png_decoder.h"
#include "../sound.h"
#include "../ui_components/constants.h"

#define MANUAL_PAGE_FILENAME_DIGITS     4
#define MANUAL_MAX_PAGE_WIDTH           1024
#define MANUAL_MAX_PAGE_HEIGHT          1024
#define MANUAL_DEFAULT_MAX_ZOOM         3
#define MANUAL_PAN_STEP_PIXELS          24.0f
#define MANUAL_PAN_STEP_FAST_PIXELS     64.0f
#define MANUAL_MAX_BLIT_COORD           1023
#define MANUAL_NATIVE_SIDECAR           ".nimg"
#define MANUAL_TMEM_BYTES               4096

static int manual_align_up (int value, int alignment) {
    if (alignment <= 1) {
        return value;
    }
    return ((value + alignment - 1) / alignment) * alignment;
}

static bool manual_show_ui;

static void manual_reset_pan (menu_t *menu) {
    menu->manual.pan_x = 0.0f;
    menu->manual.pan_y = 0.0f;
}

static void manual_free_current_image (menu_t *menu) {
    if (menu->manual.image) {
        surface_free(menu->manual.image);
        free(menu->manual.image);
        menu->manual.image = NULL;
    }
}

static void manual_free_prefetch_image (menu_t *menu) {
    if (menu->manual.prefetch_image) {
        surface_free(menu->manual.prefetch_image);
        free(menu->manual.prefetch_image);
        menu->manual.prefetch_image = NULL;
    }
    menu->manual.prefetched_page = -1;
    menu->manual.prefetch_loading = false;
}

static void manual_deinit (menu_t *menu) {
    if (menu->manual.page_loading) {
        png_decoder_abort();
    }
    if (menu->manual.prefetch_loading) {
        png_decoder_abort();
    }
    manual_free_current_image(menu);
    manual_free_prefetch_image(menu);
    if (menu->manual.pages_directory) {
        path_free(menu->manual.pages_directory);
        menu->manual.pages_directory = NULL;
    }
    if (menu->manual.zoom_pages_directory) {
        path_free(menu->manual.zoom_pages_directory);
        menu->manual.zoom_pages_directory = NULL;
    }
    if (menu->manual.directory) {
        path_free(menu->manual.directory);
        menu->manual.directory = NULL;
    }
    menu->manual.page_count = 0;
    menu->manual.current_page = 0;
    menu->manual.zoom_level = 1;
    menu->manual.max_zoom_level = 1;
    menu->manual.loaded_page = -1;
    menu->manual.prefetched_page = -1;
    menu->manual.has_zoom_pages = false;
    menu->manual.loaded_zoom_asset = false;
    menu->manual.page_loading = false;
    menu->manual.prefetch_loading = false;
    menu->manual.title[0] = '\0';
    manual_reset_pan(menu);
}

static bool manual_build_page_path (path_t *directory, int page_index, char *out, size_t out_len) {
    if (!directory || !out || out_len == 0 || page_index < 0) {
        return false;
    }

    path_t *page_path = path_clone(directory);
    if (!page_path) {
        return false;
    }

    char page_name[32];
    snprintf(page_name, sizeof(page_name), "%0*d.png", MANUAL_PAGE_FILENAME_DIGITS, page_index + 1);
    path_push(page_path, page_name);

    bool exists = native_image_sidecar_exists(path_get(page_path), MANUAL_NATIVE_SIDECAR);
    if (exists) {
        snprintf(out, out_len, "%s", path_get(page_path));
    }

    path_free(page_path);
    return exists;
}

static bool manual_should_use_zoom_asset (menu_t *menu) {
    return menu->manual.has_zoom_pages && (menu->manual.zoom_level > 1);
}

static int manual_get_prefetch_candidate_page (menu_t *menu) {
    if (menu->manual.page_count <= 1) {
        return -1;
    }
    if (menu->manual.current_page + 1 < menu->manual.page_count) {
        return menu->manual.current_page + 1;
    }
    if (menu->manual.current_page - 1 >= 0) {
        return menu->manual.current_page - 1;
    }
    return -1;
}

static bool manual_get_logical_page_dimensions (menu_t *menu, int *width, int *height) {
    if (menu->manual.image) {
        if (width) {
            *width = menu->manual.image->width;
        }
        if (height) {
            *height = menu->manual.image->height;
        }
        return true;
    }

    return false;
}

static float manual_get_base_scale_for_size (int page_width, int page_height) {
    if (page_width <= 0 || page_height <= 0) {
        return 1.0f;
    }

    float base_scale = fminf((float)display_get_width() / (float)page_width,
                             (float)display_get_height() / (float)page_height);
    if (base_scale <= 0.0f) {
        return 1.0f;
    }
    return base_scale;
}

static bool manual_start_page_load (menu_t *menu) {
    if (!menu->manual.pages_directory || menu->manual.page_count <= 0) {
        return false;
    }

    char page_path[512];
    bool use_zoom_asset = manual_should_use_zoom_asset(menu);
    bool found = false;

    if (use_zoom_asset) {
        found = manual_build_page_path(menu->manual.zoom_pages_directory, menu->manual.current_page, page_path, sizeof(page_path));
    }
    if (!found) {
        use_zoom_asset = false;
        found = manual_build_page_path(menu->manual.pages_directory, menu->manual.current_page, page_path, sizeof(page_path));
    }
    if (!found) {
        menu_show_error(menu, "Manual page image is missing");
        return false;
    }

    if (menu->manual.page_loading) {
        png_decoder_abort();
        menu->manual.page_loading = false;
    }
    if (menu->manual.prefetch_loading) {
        png_decoder_abort();
        menu->manual.prefetch_loading = false;
    }

    manual_free_current_image(menu);
    menu->manual.loaded_page = -1;
    menu->manual.loaded_zoom_asset = use_zoom_asset;
    manual_free_prefetch_image(menu);

    menu->manual.image = native_image_load_sidecar_rgba16(page_path, MANUAL_NATIVE_SIDECAR, MANUAL_MAX_PAGE_WIDTH, MANUAL_MAX_PAGE_HEIGHT);
    if (!menu->manual.image) {
        native_image_error_t native_err = native_image_get_last_error();
        char errbuf[96];
        snprintf(errbuf, sizeof(errbuf), "Manual page load failed: %s", native_image_error_string(native_err));
        debugf("manual: native load failed for %s: %s\n", page_path, native_image_error_string(native_err));
        menu_show_error(menu, errbuf);
        return false;
    }

    menu->manual.loaded_page = menu->manual.current_page;
    menu->manual.page_loading = false;
    return true;
}

static bool manual_allow_speculative_prefetch (void) {
    return is_memory_expanded();
}

static void manual_maybe_start_prefetch (menu_t *menu) {
    if (!manual_allow_speculative_prefetch()) {
        return;
    }
    if (menu->manual.page_loading || menu->manual.prefetch_loading || png_decoder_is_busy()) {
        return;
    }
    if (!menu->manual.image || menu->manual.zoom_level > 1) {
        return;
    }

    int target_page = manual_get_prefetch_candidate_page(menu);
    if (target_page < 0 || target_page == menu->manual.current_page || target_page == menu->manual.prefetched_page) {
        return;
    }

    char page_path[512];
    if (!manual_build_page_path(menu->manual.pages_directory, target_page, page_path, sizeof(page_path))) {
        return;
    }

    surface_t *native_image = native_image_load_sidecar_rgba16(page_path, MANUAL_NATIVE_SIDECAR, MANUAL_MAX_PAGE_WIDTH, MANUAL_MAX_PAGE_HEIGHT);
    if (!native_image) {
        return;
    }

    manual_free_prefetch_image(menu);
    menu->manual.prefetch_image = native_image;
    menu->manual.prefetched_page = target_page;
    menu->manual.prefetch_loading = false;
}

static void manual_clamp_pan (menu_t *menu) {
    int page_width = 0;
    int page_height = 0;
    if (!manual_get_logical_page_dimensions(menu, &page_width, &page_height)) {
        manual_reset_pan(menu);
        return;
    }

    float total_scale = manual_get_base_scale_for_size(page_width, page_height) * (float)menu->manual.zoom_level;
    if (total_scale <= 0.0f) {
        manual_reset_pan(menu);
        return;
    }

    float src_w = (float)display_get_width() / total_scale;
    float src_h = (float)display_get_height() / total_scale;

    if (src_w >= page_width) {
        menu->manual.pan_x = 0.0f;
    } else {
        float max_x = (float)page_width - src_w;
        if (menu->manual.pan_x < 0.0f) {
            menu->manual.pan_x = 0.0f;
        } else if (menu->manual.pan_x > max_x) {
            menu->manual.pan_x = max_x;
        }
    }

    if (src_h >= page_height) {
        menu->manual.pan_y = 0.0f;
    } else {
        float max_y = (float)page_height - src_h;
        if (menu->manual.pan_y < 0.0f) {
            menu->manual.pan_y = 0.0f;
        } else if (menu->manual.pan_y > max_y) {
            menu->manual.pan_y = max_y;
        }
    }
}

static void manual_change_page (menu_t *menu, int delta) {
    if (menu->manual.page_count <= 0) {
        return;
    }

    int next_page = menu->manual.current_page + delta;
    if (next_page < 0) {
        next_page = 0;
    } else if (next_page >= menu->manual.page_count) {
        next_page = menu->manual.page_count - 1;
    }

    if (next_page == menu->manual.current_page) {
        return;
    }

    menu->manual.current_page = next_page;
    manual_reset_pan(menu);
    if (menu->manual.zoom_level > 1) {
        menu->manual.zoom_level = 1;
    }
    if (menu->manual.prefetch_image
        && menu->manual.prefetched_page == menu->manual.current_page) {
        manual_free_current_image(menu);
        menu->manual.image = menu->manual.prefetch_image;
        menu->manual.prefetch_image = NULL;
        menu->manual.loaded_page = menu->manual.current_page;
        menu->manual.loaded_zoom_asset = false;
        menu->manual.prefetched_page = -1;
        menu->manual.prefetch_loading = false;
        manual_maybe_start_prefetch(menu);
    } else {
        manual_start_page_load(menu);
    }
    sound_play_effect(SFX_CURSOR);
}

static void manual_set_zoom_level (menu_t *menu, int zoom_level) {
    if (zoom_level < 1) {
        zoom_level = 1;
    }
    if (zoom_level > menu->manual.max_zoom_level) {
        zoom_level = menu->manual.max_zoom_level;
    }
    if (zoom_level == menu->manual.zoom_level) {
        return;
    }

    bool old_zoom_asset = manual_should_use_zoom_asset(menu);
    menu->manual.zoom_level = zoom_level;
    bool new_zoom_asset = manual_should_use_zoom_asset(menu);
    manual_clamp_pan(menu);

    if (new_zoom_asset != old_zoom_asset) {
        manual_start_page_load(menu);
    } else if (menu->manual.zoom_level <= 1) {
        manual_maybe_start_prefetch(menu);
    }

    sound_play_effect(SFX_SETTING);
}

static void manual_pan (menu_t *menu, float dx, float dy) {
    if (menu->manual.zoom_level <= 1) {
        return;
    }
    if (!menu->manual.image) {
        return;
    }

    menu->manual.pan_x += dx;
    menu->manual.pan_y += dy;
    manual_clamp_pan(menu);
}

static void process (menu_t *menu) {
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = menu->manual.return_mode;
        return;
    }

    if (menu->manual.page_loading) {
        return;
    }

    if (menu->actions.settings) {
        manual_show_ui = !manual_show_ui;
        sound_play_effect(SFX_SETTING);
        return;
    }

    if (menu->actions.options) {
        manual_set_zoom_level(menu, menu->manual.zoom_level + 1);
        return;
    }

    if (menu->actions.lz_context) {
        manual_set_zoom_level(menu, menu->manual.zoom_level - 1);
        return;
    }

    if (menu->actions.enter) {
        manual_change_page(menu, 1);
        return;
    }

    if (menu->actions.go_fast) {
        if (menu->actions.go_right) {
            manual_change_page(menu, 1);
            return;
        }
        if (menu->actions.go_left) {
            manual_change_page(menu, -1);
            return;
        }
        if (menu->actions.go_down) {
            manual_change_page(menu, 5);
            return;
        }
        if (menu->actions.go_up) {
            manual_change_page(menu, -5);
            return;
        }
    }

    if (menu->manual.zoom_level > 1) {
        float step = menu->actions.go_fast ? MANUAL_PAN_STEP_FAST_PIXELS : MANUAL_PAN_STEP_PIXELS;
        if (menu->actions.go_left) {
            manual_pan(menu, -step, 0.0f);
        } else if (menu->actions.go_right) {
            manual_pan(menu, step, 0.0f);
        } else if (menu->actions.go_up) {
            manual_pan(menu, 0.0f, -step);
        } else if (menu->actions.go_down) {
            manual_pan(menu, 0.0f, step);
        }
    } else {
        if (menu->actions.go_right) {
            manual_change_page(menu, 1);
        } else if (menu->actions.go_left) {
            manual_change_page(menu, -1);
        }
    }
}

static void draw_surface_region (surface_t *image, float draw_x, float draw_y, int src_x, int src_y, int src_width, int src_height, float total_scale) {
    if (!image || src_width <= 0 || src_height <= 0) {
        return;
    }

    if (src_x >= MANUAL_MAX_BLIT_COORD) {
        src_x = MANUAL_MAX_BLIT_COORD - 1;
    }
    if (src_y >= MANUAL_MAX_BLIT_COORD) {
        src_y = MANUAL_MAX_BLIT_COORD - 1;
    }
    if (src_x >= image->width) {
        src_x = image->width - 1;
    }
    if (src_y >= image->height) {
        src_y = image->height - 1;
    }
    if (src_width > (MANUAL_MAX_BLIT_COORD - src_x)) {
        src_width = MANUAL_MAX_BLIT_COORD - src_x;
    }
    if (src_height > (MANUAL_MAX_BLIT_COORD - src_y)) {
        src_height = MANUAL_MAX_BLIT_COORD - src_y;
    }
    if (src_width > (image->width - src_x)) {
        src_width = image->width - src_x;
    }
    if (src_height > (image->height - src_y)) {
        src_height = image->height - src_y;
    }
    if (src_width < 1) {
        src_width = 1;
    }
    if (src_height < 1) {
        src_height = 1;
    }

    int bytes_per_pixel = 2;
    int line_bytes = manual_align_up(src_width * bytes_per_pixel, 8);
    int max_strip_rows = MANUAL_TMEM_BYTES / line_bytes;
    if (max_strip_rows < 1) {
        max_strip_rows = 1;
    }

    int strip_y = src_y;
    int remaining_height = src_height;
    while (remaining_height > 0) {
        int strip_height = remaining_height;
        if (strip_height > max_strip_rows) {
            strip_height = max_strip_rows;
        }

        rdpq_tex_upload_sub(
            TILE0,
            image,
            NULL,
            src_x,
            strip_y,
            src_x + src_width,
            strip_y + strip_height
        );
        rdpq_texture_rectangle_scaled(
            TILE0,
            draw_x,
            draw_y + ((float)(strip_y - src_y) * total_scale),
            draw_x + ((float)src_width * total_scale),
            draw_y + ((float)((strip_y - src_y) + strip_height) * total_scale),
            src_x,
            strip_y,
            src_x + src_width,
            strip_y + strip_height
        );

        strip_y += strip_height;
        remaining_height -= strip_height;
    }
}

static void draw_full_page (menu_t *menu, surface_t *display) {
    if (!menu->manual.image) {
        return;
    }

    float base_scale = manual_get_base_scale_for_size(menu->manual.image->width, menu->manual.image->height);
    float total_scale = base_scale * (float)menu->manual.zoom_level;
    float src_w = fminf((float)menu->manual.image->width, (float)display->width / total_scale);
    float src_h = fminf((float)menu->manual.image->height, (float)display->height / total_scale);

    manual_clamp_pan(menu);

    int src_x = (int)menu->manual.pan_x;
    int src_y = (int)menu->manual.pan_y;
    int src_width = (int)src_w;
    int src_height = (int)src_h;
    if (src_width <= 0) {
        src_width = menu->manual.image->width;
    }
    if (src_height <= 0) {
        src_height = menu->manual.image->height;
    }

    float draw_w = (float)src_width * total_scale;
    float draw_h = (float)src_height * total_scale;
    float draw_x = ((float)display->width - draw_w) * 0.5f;
    float draw_y = ((float)display->height - draw_h) * 0.5f;

    rdpq_mode_push();
        rdpq_set_mode_copy(false);
        draw_surface_region(menu->manual.image, draw_x, draw_y, src_x, src_y, src_width, src_height, total_scale);
    rdpq_mode_pop();
}

static void draw (menu_t *menu, surface_t *display) {
    rdpq_attach_clear(display, NULL);

    ui_components_background_draw();

    if (menu->manual.page_loading) {
        ui_components_loader_draw(png_decoder_get_progress(), "Loading manual page...");
    } else if (menu->manual.image) {
        draw_full_page(menu, display);
    } else {
        ui_components_messagebox_draw("Manual page could not be loaded");
    }

    if (manual_show_ui) {
        char left[256];
        const char *asset_label = "";
        if (menu->manual.loaded_zoom_asset) {
            asset_label = " (hi-res)";
        }
        snprintf(left, sizeof(left), "%s\nPage %d / %d\nZoom %dx%s",
            menu->manual.title[0] ? menu->manual.title : "Manual",
            (int)(menu->manual.current_page + 1),
            (int)menu->manual.page_count,
            (int)menu->manual.zoom_level,
            asset_label);

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            left
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_RIGHT, VALIGN_TOP,
            "A: Next Page\n"
            "C-L/C-R: Prev/Next\n"
            "L/R: Zoom Out/In\n"
            "Stick/D-Pad: Pan\n"
            "Start: Toggle UI\n"
            "B: Back"
        );
    }

    rdpq_detach_show();
}

void view_manual_viewer_init (menu_t *menu) {
    manual_show_ui = true;
    menu->manual.page_loading = false;
    menu->manual.image = NULL;
    menu->manual.loaded_page = -1;
    menu->manual.loaded_zoom_asset = false;
    menu->manual.has_zoom_pages = false;
    menu->manual.max_zoom_level = MANUAL_DEFAULT_MAX_ZOOM;
    menu->manual.zoom_level = 1;
    manual_reset_pan(menu);

    if (!menu->manual.directory) {
        menu_show_error(menu, "Manual directory is not set");
        return;
    }

    path_t *manifest_path = path_clone(menu->manual.directory);
    path_push(manifest_path, "manifest.ini");
    mini_t *ini = mini_try_load(path_get(manifest_path));
    path_free(manifest_path);
    if (!ini) {
        menu_show_error(menu, "Manual manifest is missing");
        return;
    }

    char pages_dir_name[64];
    char zoom_dir_name[64];

    menu->manual.page_count = mini_get_int(ini, "manual", "page_count", 0);
    menu->manual.current_page = mini_get_int(ini, "manual", "start_page", 1) - 1;
    menu->manual.max_zoom_level = mini_get_int(ini, "manual", "max_zoom", MANUAL_DEFAULT_MAX_ZOOM);
    snprintf(menu->manual.title, sizeof(menu->manual.title), "%s", mini_get_string(ini, "manual", "title", "Manual"));
    snprintf(pages_dir_name, sizeof(pages_dir_name), "%s", mini_get_string(ini, "manual", "pages_dir", "pages"));
    snprintf(zoom_dir_name, sizeof(zoom_dir_name), "%s", mini_get_string(ini, "manual", "zoom_dir", "zoom"));

    mini_free(ini);

    if (menu->manual.page_count <= 0) {
        menu_show_error(menu, "Manual manifest has no pages");
        return;
    }
    if (menu->manual.current_page < 0) {
        menu->manual.current_page = 0;
    }
    if (menu->manual.current_page >= menu->manual.page_count) {
        menu->manual.current_page = menu->manual.page_count - 1;
    }
    if (menu->manual.max_zoom_level < 1) {
        menu->manual.max_zoom_level = 1;
    }

    menu->manual.pages_directory = path_clone(menu->manual.directory);
    path_push(menu->manual.pages_directory, (char *)pages_dir_name);
    if (!directory_exists(path_get(menu->manual.pages_directory))) {
        menu_show_error(menu, "Manual pages directory is missing");
        return;
    }

    menu->manual.zoom_pages_directory = path_clone(menu->manual.directory);
    path_push(menu->manual.zoom_pages_directory, (char *)zoom_dir_name);
    menu->manual.has_zoom_pages = directory_exists(path_get(menu->manual.zoom_pages_directory));

    if (!menu->manual.has_zoom_pages) {
        path_free(menu->manual.zoom_pages_directory);
        menu->manual.zoom_pages_directory = NULL;
        menu->manual.max_zoom_level = 2;
    }

    manual_start_page_load(menu);
}

void view_manual_viewer_display (menu_t *menu, surface_t *display) {
    process(menu);
    manual_maybe_start_prefetch(menu);
    draw(menu, display);

    if (menu->next_mode != MENU_MODE_MANUAL_VIEWER) {
        manual_deinit(menu);
    }
}
