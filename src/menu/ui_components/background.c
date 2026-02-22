/**
 * @file background.c
 * @brief Implementation of the background UI component.
 * @ingroup ui_components
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "../ui_components.h"
#include "../sound.h"
#include "constants.h"
#include "utils/fs.h"

#define CACHE_METADATA_MAGIC    (0x424B4731)

/**
 * @brief Structure representing the background component.
 */
typedef struct {
    char *cache_location;      /**< Path to the cache file location. */
    surface_t *image;          /**< Pointer to the loaded image surface. */
    rspq_block_t *image_display_list; /**< Display list for rendering the image. */
    bool visualizer_enabled;   /**< Draw animated visualizer instead of image. */
    int visualizer_style;      /**< Visualizer style enum. */
    int visualizer_intensity;  /**< 0=subtle,1=normal,2=full */
    float vis_bars[14];        /**< Smoothed visualizer bar values. */
    float vis_trail_1[14];     /**< First trail buffer (recent). */
    float vis_caps[14];        /**< Peak hold caps. */
    uint32_t vis_tick;         /**< Animation tick counter. */
} component_background_t;

/**
 * @brief Structure for background image cache metadata.
 */
typedef struct {
    uint32_t magic;    /**< Magic number for cache validation. */
    uint32_t width;    /**< Image width in pixels. */
    uint32_t height;   /**< Image height in pixels. */
    uint32_t size;     /**< Image buffer size in bytes. */
} cache_metadata_t;

static component_background_t *background = NULL;

static void visualizer_reset_state(component_background_t *c) {
    if (!c) return;
    memset(c->vis_bars, 0, sizeof(c->vis_bars));
    memset(c->vis_trail_1, 0, sizeof(c->vis_trail_1));
    memset(c->vis_caps, 0, sizeof(c->vis_caps));
}

static uint64_t fnv1a64_str(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    if (!s) return h;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static char *make_variant_cache_path(const char *base_cache_location, const char *source_path) {
    if (!base_cache_location || !source_path) {
        return NULL;
    }

    const char *slash = strrchr(base_cache_location, '/');
    size_t dir_len = slash ? (size_t)(slash - base_cache_location) : 0;
    uint64_t hash = fnv1a64_str(source_path);

    size_t out_len = (dir_len ? dir_len : 1) + 1 + 3 + 16 + 6 + 1; // dir + / + bg_ + hash + .cache + NUL
    char *out = malloc(out_len);
    if (!out) {
        return NULL;
    }

    if (dir_len > 0) {
        memcpy(out, base_cache_location, dir_len);
        out[dir_len] = '\0';
    } else {
        strcpy(out, ".");
        dir_len = 1;
    }

    snprintf(out + dir_len, out_len - dir_len, "/bg_%016" PRIx64 ".cache", hash);
    return out;
}

static surface_t *load_surface_from_cache_file(const char *cache_path) {
    if (!cache_path) {
        return NULL;
    }

    FILE *f = fopen(cache_path, "rb");
    if (!f) {
        return NULL;
    }

    cache_metadata_t cache_metadata;
    if (fread(&cache_metadata, sizeof(cache_metadata), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (cache_metadata.magic != CACHE_METADATA_MAGIC || cache_metadata.width > DISPLAY_WIDTH || cache_metadata.height > DISPLAY_HEIGHT) {
        fclose(f);
        return NULL;
    }

    surface_t *image = calloc(1, sizeof(surface_t));
    if (!image) {
        fclose(f);
        return NULL;
    }
    *image = surface_alloc(FMT_RGBA16, cache_metadata.width, cache_metadata.height);

    if (image->buffer == NULL || cache_metadata.size != (image->height * image->stride)) {
        if (image->buffer) {
            surface_free(image);
        }
        free(image);
        fclose(f);
        return NULL;
    }

    if (fread(image->buffer, cache_metadata.size, 1, f) != 1) {
        surface_free(image);
        free(image);
        image = NULL;
    }

    fclose(f);
    return image;
}

static void save_surface_to_cache_file(const char *cache_path, surface_t *image) {
    if (!cache_path || !image) {
        return;
    }

    FILE *f = fopen(cache_path, "wb");
    if (!f) {
        return;
    }

    cache_metadata_t cache_metadata = {
        .magic = CACHE_METADATA_MAGIC,
        .width = image->width,
        .height = image->height,
        .size = (image->height * image->stride),
    };

    fwrite(&cache_metadata, sizeof(cache_metadata), 1, f);
    fwrite(image->buffer, cache_metadata.size, 1, f);
    fclose(f);
}

static uint8_t u8_clamp(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static color_t hsv_to_rgba(float h, float s, float v, uint8_t a) {
    while (h < 0.0f) h += 1.0f;
    while (h >= 1.0f) h -= 1.0f;

    float r = v, g = v, b = v;
    if (s > 0.0f) {
        float hf = h * 6.0f;
        int i = (int)hf;
        float f = hf - (float)i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - s * f);
        float t = v * (1.0f - s * (1.0f - f));

        switch (i % 6) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
    }

    return RGBA32(u8_clamp((int)(r * 255.0f)),
                  u8_clamp((int)(g * 255.0f)),
                  u8_clamp((int)(b * 255.0f)),
                  a);
}

static uint8_t vis_alpha_scale(component_background_t *c, int alpha) {
    static const float k[3] = {0.55f, 0.85f, 1.10f};
    int idx = (c && c->visualizer_intensity >= 0 && c->visualizer_intensity <= 2) ? c->visualizer_intensity : 1;
    int out = (int)(alpha * k[idx]);
    if (out > 255) out = 255;
    if (out < 0) out = 0;
    return (uint8_t)out;
}

static bool visualizer_get_meter(component_background_t *c, float *out_base, float *out_peak) {
    (void)c;
    sound_bgm_meter_t meter = {0};
    bool have_meter = sound_bgm_meter_get(&meter);
    float base = 0.0f;
    float peak = 0.0f;
    if (have_meter) {
        base = (meter.avg_l + meter.avg_r) * 0.5f;
        peak = (meter.peak_l + meter.peak_r) * 0.5f;
    }
    if (out_base) *out_base = base;
    if (out_peak) *out_peak = peak;
    return have_meter;
}

static void draw_visualizer_bars_overlay(component_background_t *c) {
    if (!c) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
        return;
    }

    if (!c->image_display_list) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }

    float base = 0.0f;
    float peak = 0.0f;
    bool have_meter = visualizer_get_meter(c, &base, &peak);

    c->vis_tick++;

    const int bars = (int)(sizeof(c->vis_bars) / sizeof(c->vis_bars[0]));
    const int bar_w = 14;
    const int gap = 6;
    const int total_w = bars * (bar_w + gap) - gap;
    const int x0 = (DISPLAY_WIDTH - total_w) / 2;
    const int bottom = DISPLAY_HEIGHT - 34;
    const int panel_top = bottom - 84;
    const int max_h = bottom - panel_top - 8;

    // Keep playlist/background image visible; darken only the visualizer strip.
    ui_components_box_draw(0, panel_top - 3, DISPLAY_WIDTH, bottom + 3, RGBA32(0x05, 0x08, 0x0C, vis_alpha_scale(c, 0x72)));
    ui_components_box_draw(0, panel_top - 4, DISPLAY_WIDTH, panel_top - 3, RGBA32(0xC8, 0xD8, 0xFF, vis_alpha_scale(c, 0x20)));
    for (int y = panel_top; y < bottom; y += 10) {
        ui_components_box_draw(0, y, DISPLAY_WIDTH, y + 1, RGBA32(0x0D, 0x13, 0x18, vis_alpha_scale(c, 0x20)));
    }

    for (int i = 0; i < bars; i++) {
        // Cheap deterministic motion so the visualizer still animates gently when music is quiet.
        float pulse = (float)(((c->vis_tick * 3) + (i * 11)) % 64) / 63.0f;
        if (pulse > 0.5f) {
            pulse = 1.0f - pulse;
        }
        pulse *= 2.0f;

        float center = (float)(i < bars / 2 ? i : (bars - 1 - i)) / (float)(bars / 2);
        float center_weight = 0.88f + center * 0.18f;
        float band_shape = 0.92f + 0.12f * (float)(((i * 7) + (int)c->vis_tick) % 9) / 8.0f;
        // Bias heavily toward instantaneous peaks to reduce perceived lag.
        float peak_drive = peak * (0.85f + 0.35f * pulse);
        float body_drive = base * (0.06f + 0.10f * band_shape);
        float target = ((peak_drive * center_weight) + body_drive) * band_shape;
        if (!have_meter) {
            target = 0.03f + pulse * 0.05f;
        }
        if (target > 1.0f) {
            target = 1.0f;
        }

        // Immediate rise and quick release for minimal visual lag.
        if (target > c->vis_bars[i]) {
            c->vis_bars[i] = target;
        } else {
            c->vis_bars[i] = (c->vis_bars[i] * 0.58f) + (target * 0.42f);
        }

        // Trail buffers for a cheap WMP/Winamp-style persistence effect.
        c->vis_trail_1[i] = (c->vis_trail_1[i] * 0.70f) + (c->vis_bars[i] * 0.30f);
        // Peak hold cap with gradual fall.
        if (c->vis_bars[i] >= c->vis_caps[i]) {
            c->vis_caps[i] = c->vis_bars[i];
        } else {
            c->vis_caps[i] -= 0.018f;
            if (c->vis_caps[i] < c->vis_bars[i]) {
                c->vis_caps[i] = c->vis_bars[i];
            }
            if (c->vis_caps[i] < 0.0f) {
                c->vis_caps[i] = 0.0f;
            }
        }

        int h = (int)(c->vis_bars[i] * max_h);
        if (h < 2) {
            h = 2;
        }
        int h_t1 = (int)(c->vis_trail_1[i] * max_h);
        if (h_t1 < 1) h_t1 = 1;
        int x = x0 + i * (bar_w + gap);
        int y = bottom - h;
        int y_t1 = bottom - h_t1;

        float hue = (float)((c->vis_tick * 2) + (i * 10)) / 256.0f;
        color_t trail1_col = hsv_to_rgba(hue + 0.01f, 0.85f, 0.72f, vis_alpha_scale(c, 0x40));
        color_t fill_lo = hsv_to_rgba(hue + 0.08f, 0.90f, 0.70f, vis_alpha_scale(c, 0xCC));
        color_t fill_mid = hsv_to_rgba(hue + 0.03f, 0.92f, 0.85f, vis_alpha_scale(c, 0xD8));
        color_t fill_hi = hsv_to_rgba(hue, 0.95f, 1.00f, vis_alpha_scale(c, 0xE0));
        color_t cap_col = hsv_to_rgba(hue + 0.15f, 0.55f, 1.00f, vis_alpha_scale(c, 0xF0));

        // Trail for persistence.
        ui_components_box_draw(x, y_t1, x + bar_w, bottom, trail1_col);

        // Outer bar body
        ui_components_box_draw(x, y, x + bar_w, bottom, RGBA32(0x14, 0x1D, 0x28, 0xD8));

        // Segmented rainbow fill.
        int fill_h = h - 2;
        int fill_top = bottom - 1 - fill_h;
        int split1 = fill_top + (fill_h / 2);
        int split2 = fill_top + ((fill_h * 4) / 5);
        ui_components_box_draw(x + 1, split1, x + bar_w - 1, bottom - 1, fill_hi);
        ui_components_box_draw(x + 1, split2, x + bar_w - 1, split1, fill_mid);
        ui_components_box_draw(x + 1, fill_top, x + bar_w - 1, split2, fill_lo);

        // Peak cap (per-bar hold, not global peak).
        int cap_y = bottom - 2 - (int)(c->vis_caps[i] * max_h);
        if (cap_y < y) {
            cap_y = y;
        }
        ui_components_box_draw(x, cap_y - 1, x + bar_w, cap_y + 2, RGBA32(0x00, 0x00, 0x00, vis_alpha_scale(c, 0x50)));
        ui_components_box_draw(x + 1, cap_y, x + bar_w - 1, cap_y + 1, cap_col);
    }
}

static void draw_visualizer_pulse_wash(component_background_t *c) {
    if (!c) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
        return;
    }
    if (!c->image_display_list) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }

    float base = 0.0f, peak = 0.0f;
    bool have_meter = visualizer_get_meter(c, &base, &peak);
    c->vis_tick++;

    float energy = (peak * 0.75f) + (base * 0.25f);
    if (!have_meter) {
        energy = 0.08f + 0.04f * (float)((c->vis_tick % 60)) / 59.0f;
    }
    if (energy > 1.0f) energy = 1.0f;

    float hue = (float)((c->vis_tick * 2) % 512) / 512.0f;
    color_t wash_a = hsv_to_rgba(hue, 0.85f, 0.75f + 0.20f * energy, vis_alpha_scale(c, 28 + (int)(energy * 50.0f)));
    color_t wash_b = hsv_to_rgba(hue + 0.33f, 0.65f, 0.55f + 0.20f * energy, vis_alpha_scale(c, 20 + (int)(energy * 36.0f)));
    color_t wash_c = hsv_to_rgba(hue + 0.66f, 0.75f, 0.45f + 0.25f * energy, vis_alpha_scale(c, 16 + (int)(energy * 30.0f)));

    // Full-screen layered pulse wash (few large quads = cheap).
    ui_components_box_draw(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, RGBA32(0x03, 0x05, 0x08, vis_alpha_scale(c, 0x30)));
    ui_components_box_draw(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT / 2, wash_a);
    ui_components_box_draw(0, DISPLAY_HEIGHT / 2, DISPLAY_WIDTH, DISPLAY_HEIGHT, wash_b);

    int cx = DISPLAY_WIDTH / 2;
    int cy = DISPLAY_HEIGHT / 2;
    int spread = 70 + (int)(energy * 120.0f);
    ui_components_box_draw(cx - spread, cy - (18 + spread / 5), cx + spread, cy + (18 + spread / 5), wash_c);
    ui_components_box_draw(cx - (12 + spread / 4), cy - spread, cx + (12 + spread / 4), cy + spread, wash_a);

    // Moving color bands.
    for (int i = 0; i < 4; i++) {
        int band_h = 16 + (i * 6);
        int y = (int)((c->vis_tick * (2 + i) + i * 53) % (DISPLAY_HEIGHT + band_h)) - band_h;
        color_t band = hsv_to_rgba(hue + (float)i * 0.12f, 0.9f, 0.9f, vis_alpha_scale(c, 14 + (int)(energy * 24.0f)));
        ui_components_box_draw(0, y, DISPLAY_WIDTH, y + band_h, band);
    }
}

static void draw_visualizer_sunburst(component_background_t *c) {
    if (!c) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
        return;
    }
    if (!c->image_display_list) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }

    float base = 0.0f, peak = 0.0f;
    bool have_meter = visualizer_get_meter(c, &base, &peak);
    c->vis_tick++;

    float energy = (peak * 0.8f) + (base * 0.2f);
    if (!have_meter) {
        energy = 0.10f;
    }
    if (energy > 1.0f) energy = 1.0f;

    int cx = DISPLAY_WIDTH / 2;
    int cy = (DISPLAY_HEIGHT / 2) - 8;
    float hue = (float)((c->vis_tick * 3) % 512) / 512.0f;

    // Dim center field to help rays pop without killing background visibility.
    ui_components_box_draw(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, RGBA32(0x02, 0x03, 0x06, vis_alpha_scale(c, 0x16)));

    // Expanding pulse rings (axis-aligned rectangles).
    for (int r = 0; r < 3; r++) {
        int phase = ((int)c->vis_tick * (3 + r)) + (r * 41);
        int radius = 20 + (phase % 180) + (int)(energy * 30.0f);
        int thick = 2 + r;
        color_t ring = hsv_to_rgba(hue + 0.08f * r, 0.85f, 0.95f, vis_alpha_scale(c, 18 + (int)(energy * 28.0f)));
        ui_components_box_draw(cx - radius, cy - radius, cx + radius, cy - radius + thick, ring);
        ui_components_box_draw(cx - radius, cy + radius - thick, cx + radius, cy + radius, ring);
        ui_components_box_draw(cx - radius, cy - radius, cx - radius + thick, cy + radius, ring);
        ui_components_box_draw(cx + radius - thick, cy - radius, cx + radius, cy + radius, ring);
    }

    // Cardinal and diagonal "rays" approximated with rectangles / stepped quads.
    int ray = 50 + (int)(energy * 170.0f);
    color_t ray1 = hsv_to_rgba(hue, 0.95f, 1.0f, vis_alpha_scale(c, 22 + (int)(energy * 40.0f)));
    color_t ray2 = hsv_to_rgba(hue + 0.18f, 0.90f, 0.95f, vis_alpha_scale(c, 18 + (int)(energy * 30.0f)));
    color_t core = hsv_to_rgba(hue + 0.08f, 0.35f, 1.0f, vis_alpha_scale(c, 44 + (int)(energy * 70.0f)));

    ui_components_box_draw(cx - 10, cy - ray, cx + 10, cy + ray, ray1);
    ui_components_box_draw(cx - ray, cy - 8, cx + ray, cy + 8, ray1);

    for (int s = 0; s < 5; s++) {
        int step = 12 + s * 14;
        int width = 10 - s;
        if (width < 3) width = 3;
        int len = ray - (s * 18);
        if (len < 12) len = 12;
        ui_components_box_draw(cx + step, cy - step - width, cx + step + len/4, cy - step + width, ray2);
        ui_components_box_draw(cx - step - len/4, cy - step - width, cx - step, cy - step + width, ray2);
        ui_components_box_draw(cx + step, cy + step - width, cx + step + len/4, cy + step + width, ray2);
        ui_components_box_draw(cx - step - len/4, cy + step - width, cx - step, cy + step + width, ray2);
    }

    // Center orb pulse.
    int core_r = 12 + (int)(energy * 22.0f);
    ui_components_box_draw(cx - core_r - 2, cy - core_r - 2, cx + core_r + 2, cy + core_r + 2, RGBA32(0x00, 0x00, 0x00, vis_alpha_scale(c, 0x30)));
    ui_components_box_draw(cx - core_r, cy - core_r, cx + core_r, cy + core_r, core);
}

static void draw_visualizer_oscilloscope(component_background_t *c) {
    if (!c) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
        return;
    }
    if (!c->image_display_list) {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }

    float base = 0.0f, peak = 0.0f;
    bool have_meter = visualizer_get_meter(c, &base, &peak);
    c->vis_tick++;

    const int left = 26;
    const int right = DISPLAY_WIDTH - 26;
    const int center_y = DISPLAY_HEIGHT - 72;
    const int amp = 14 + (int)(peak * 42.0f);
    const int width = right - left;

    ui_components_box_draw(0, center_y - 42, DISPLAY_WIDTH, center_y + 42, RGBA32(0x03, 0x06, 0x0A, vis_alpha_scale(c, 0x58)));
    ui_components_box_draw(left, center_y, right, center_y + 1, RGBA32(0xB0, 0xD8, 0xFF, vis_alpha_scale(c, 0x26)));

    float energy = peak * 0.75f + base * 0.25f;
    if (!have_meter) energy = 0.10f;
    for (int lane = 0; lane < 3; lane++) {
        int prev_x = left;
        float phase = (float)(c->vis_tick * (2 + lane)) * 0.05f;
        float freq = 0.025f + (0.010f * lane) + (0.03f * energy);
        color_t col = hsv_to_rgba(((float)c->vis_tick / 180.0f) + lane * 0.08f, 0.9f, 1.0f, vis_alpha_scale(c, 0x30 + lane * 0x18));
        int prev_y = center_y;
        for (int x = 0; x < width; x += 4) {
            float xf = (float)x;
            float yv = sinf((xf * freq) + phase) * (0.45f + 0.25f * lane)
                     + sinf((xf * (freq * 0.47f)) - (phase * 1.7f)) * (0.25f + 0.10f * energy);
            int y = center_y + (int)(yv * (float)amp);
            int draw_x = left + x;
            int minx = prev_x < draw_x ? prev_x : draw_x;
            int maxx = prev_x > draw_x ? prev_x : draw_x;
            int miny = prev_y < y ? prev_y : y;
            int maxy = prev_y > y ? prev_y : y;
            ui_components_box_draw(minx, miny, maxx + 2, maxy + 2, col);
            prev_x = draw_x;
            prev_y = y;
        }
    }

    // Bright core trace on top.
    int prev_x = left;
    int prev_y = center_y;
    color_t core = hsv_to_rgba((float)c->vis_tick / 160.0f, 0.35f, 1.0f, vis_alpha_scale(c, 0xCC));
    for (int x = 0; x < width; x += 3) {
        float xf = (float)x;
        float yv = sinf((xf * (0.03f + 0.05f * energy)) + ((float)c->vis_tick * 0.12f))
                 * (0.65f + 0.30f * peak);
        int y = center_y + (int)(yv * (float)amp);
        int draw_x = left + x;
        int minx = prev_x < draw_x ? prev_x : draw_x;
        int maxx = prev_x > draw_x ? prev_x : draw_x;
        int miny = prev_y < y ? prev_y : y;
        int maxy = prev_y > y ? prev_y : y;
        ui_components_box_draw(minx, miny, maxx + 2, maxy + 2, core);
        prev_x = draw_x;
        prev_y = y;
    }
}

/**
 * @brief Load background image from cache file if available.
 *
 * @param c Pointer to the background component structure.
 */
static void load_from_cache(component_background_t *c) {
    if (!c->cache_location) {
        return;
    }
    c->image = load_surface_from_cache_file(c->cache_location);
}

/**
 * @brief Save background image to cache file.
 *
 * @param c Pointer to the background component structure.
 */
static void save_to_cache(component_background_t *c) {
    if (!c->cache_location || !c->image) {
        return;
    }
    save_surface_to_cache_file(c->cache_location, c->image);
}

/**
 * @brief Prepare the background image for display (darken and center).
 *
 * @param c Pointer to the background component structure.
 */
static void prepare_background(component_background_t *c) {
    if (!c->image || c->image->width == 0 || c->image->height == 0) {
        return;
    }

    // Darken the image
    rdpq_attach(c->image, NULL);
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_set_prim_color(BACKGROUND_OVERLAY_COLOR);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_fill_rectangle(0, 0, c->image->width, c->image->height);
    rdpq_mode_pop();
    rdpq_detach();

    uint16_t image_center_x = (c->image->width / 2);
    uint16_t image_center_y = (c->image->height / 2);

    // Prepare display list
    rspq_block_begin();
    rdpq_mode_push();
        if ((c->image->width != DISPLAY_WIDTH) || (c->image->height != DISPLAY_HEIGHT)) {
            rdpq_set_mode_fill(BACKGROUND_EMPTY_COLOR);
        }
        if (c->image->width != DISPLAY_WIDTH) {
            rdpq_fill_rectangle(
                0,
                DISPLAY_CENTER_Y - image_center_y,
                DISPLAY_CENTER_X - image_center_x,
                DISPLAY_CENTER_Y + image_center_y
            );
            rdpq_fill_rectangle(
                DISPLAY_CENTER_X + image_center_x - (c->image->width % 2),
                DISPLAY_CENTER_Y - image_center_y,
                DISPLAY_WIDTH,
                DISPLAY_CENTER_Y + image_center_y
            );
        }
        if (c->image->height != DISPLAY_HEIGHT) {
            rdpq_fill_rectangle(
                0,
                0,
                DISPLAY_WIDTH,
                DISPLAY_CENTER_Y - image_center_y
            );
            rdpq_fill_rectangle(
                0,
                DISPLAY_CENTER_Y + image_center_y - (c->image->height % 2),
                DISPLAY_WIDTH,
                DISPLAY_HEIGHT
            );
        }
        rdpq_set_mode_copy(false);
        rdpq_tex_blit(c->image, DISPLAY_CENTER_X - image_center_x, DISPLAY_CENTER_Y - image_center_y, NULL);
    rdpq_mode_pop();
    c->image_display_list = rspq_block_end();
}

/**
 * @brief Free the display list for the background image.
 *
 * @param arg Pointer to the display list (rspq_block_t *).
 */
static void display_list_free(void *arg) {
    rspq_block_free((rspq_block_t *) (arg));
}

/**
 * @brief Initialize the background component and load from cache.
 *
 * @param cache_location Path to the cache file location.
 */
void ui_components_background_init(char *cache_location) {
    if (!background) {
        background = calloc(1, sizeof(component_background_t));
        background->cache_location = strdup(cache_location);
        load_from_cache(background);
        prepare_background(background);
    }
}

/**
 * @brief Free the background component and its resources.
 */
void ui_components_background_free(void) {
    if (background) {
        if (background->image) {
            surface_free(background->image);
            free(background->image);
            background->image = NULL;
        }
        if (background->image_display_list) {
            rdpq_call_deferred(display_list_free, background->image_display_list);
            background->image_display_list = NULL;
        }
        if (background->cache_location) {
            free(background->cache_location);
        }
        free(background);
        background = NULL;
    }
}

/**
 * @brief Replace the background image and update cache/display list.
 *
 * @param image Pointer to the new background image surface.
 */
void ui_components_background_replace_image(surface_t *image) {
    if (!background) {
        return;
    }

    if (background->image) {
        surface_free(background->image);
        free(background->image);
        background->image = NULL;
    }

    if (background->image_display_list) {
        rdpq_call_deferred(display_list_free, background->image_display_list);
        background->image_display_list = NULL;
    }

    background->image = image;
    save_to_cache(background);
    prepare_background(background);
}

void ui_components_background_replace_image_temporary(surface_t *image) {
    if (!background) {
        return;
    }

    if (background->image) {
        surface_free(background->image);
        free(background->image);
        background->image = NULL;
    }

    if (background->image_display_list) {
        rdpq_call_deferred(display_list_free, background->image_display_list);
        background->image_display_list = NULL;
    }

    background->image = image;
    prepare_background(background);
}

bool ui_components_background_load_temporary_cached(const char *source_path) {
    if (!background || !background->cache_location || !source_path) {
        return false;
    }

    char *cache_path = make_variant_cache_path(background->cache_location, source_path);
    if (!cache_path) {
        return false;
    }

    surface_t *image = load_surface_from_cache_file(cache_path);
    free(cache_path);
    if (!image) {
        return false;
    }

    ui_components_background_replace_image_temporary(image);
    return true;
}

void ui_components_background_save_temporary_cache(const char *source_path) {
    if (!background || !background->cache_location || !source_path || !background->image) {
        return;
    }

    char *cache_path = make_variant_cache_path(background->cache_location, source_path);
    if (!cache_path) {
        return;
    }
    save_surface_to_cache_file(cache_path, background->image);
    free(cache_path);
}

void ui_components_background_reload_cache(void) {
    if (!background) {
        return;
    }

    if (background->image) {
        surface_free(background->image);
        free(background->image);
        background->image = NULL;
    }

    if (background->image_display_list) {
        rdpq_call_deferred(display_list_free, background->image_display_list);
        background->image_display_list = NULL;
    }

    load_from_cache(background);
    prepare_background(background);
}

void ui_components_background_set_visualizer(bool enabled) {
    if (!background) {
        return;
    }
    background->visualizer_enabled = enabled;
    if (enabled) {
        visualizer_reset_state(background);
    }
}

void ui_components_background_set_visualizer_style(int style) {
    if (!background) {
        return;
    }
    if (style < UI_BACKGROUND_VISUALIZER_BARS || style > UI_BACKGROUND_VISUALIZER_OSCILLOSCOPE) {
        style = UI_BACKGROUND_VISUALIZER_BARS;
    }
    if (background->visualizer_style != style) {
        background->visualizer_style = style;
        visualizer_reset_state(background);
    }
}

void ui_components_background_set_visualizer_intensity(int intensity) {
    if (!background) {
        return;
    }
    if (intensity < 0) intensity = 0;
    if (intensity > 2) intensity = 2;
    background->visualizer_intensity = intensity;
}

/**
 * @brief Draw the background image or clear the screen if not available.
 */
void ui_components_background_draw(void) {
    if (background && background->image_display_list) {
        rspq_block_run(background->image_display_list);
        if (background->visualizer_enabled) {
            switch (background->visualizer_style) {
                case UI_BACKGROUND_VISUALIZER_PULSE_WASH:
                    draw_visualizer_pulse_wash(background);
                    break;
                case UI_BACKGROUND_VISUALIZER_SUNBURST:
                    draw_visualizer_sunburst(background);
                    break;
                case UI_BACKGROUND_VISUALIZER_OSCILLOSCOPE:
                    draw_visualizer_oscilloscope(background);
                    break;
                case UI_BACKGROUND_VISUALIZER_BARS:
                default:
                    draw_visualizer_bars_overlay(background);
                    break;
            }
        }
    } else if (background && background->visualizer_enabled) {
        switch (background->visualizer_style) {
            case UI_BACKGROUND_VISUALIZER_PULSE_WASH:
                draw_visualizer_pulse_wash(background);
                break;
            case UI_BACKGROUND_VISUALIZER_SUNBURST:
                draw_visualizer_sunburst(background);
                break;
            case UI_BACKGROUND_VISUALIZER_OSCILLOSCOPE:
                draw_visualizer_oscilloscope(background);
                break;
            case UI_BACKGROUND_VISUALIZER_BARS:
            default:
                draw_visualizer_bars_overlay(background);
                break;
        }
    } else {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }
}
