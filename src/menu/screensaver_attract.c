#include <libdragon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fonts.h"
#include "path.h"
#include "sound.h"
#include "screensaver_attract.h"
#include "ui_components/constants.h"
#include "utils/fs.h"

#define SCREENSAVER_ATTRACT_ROTATE_SECONDS   (30.0f)
#define SCREENSAVER_ATTRACT_SCAN_DIRS_FRAME  (2)
#define SCREENSAVER_ATTRACT_SCROLL_HOLD_S    (1.4f)
#define SCREENSAVER_ATTRACT_SCROLL_PX_PER_S  (18.0f)

static const char *attract_rom_extensions[] = { "z64", "n64", "v64", "rom", NULL };
static const char *attract_prompt_icon_paths[] = {
    "rom:/attract_a_button.sprite",
    NULL,
};
static const char *attract_library_roots[] = {
    "/N64 - USA",
    "/N64 - EU",
    "/N64 - JP",
    "/N64 - Translations",
    "/N64 - Aftermarket",
    "/N64 - N64DD",
    NULL,
};

static uint32_t attract_rng_next(screensaver_attract_state_t *state) {
    state->rng = (state->rng * 1664525u) + 1013904223u;
    return state->rng;
}

static const char *attract_display_name(const screensaver_attract_state_t *state) {
    if (state->current_rom_info.metadata.name[0] != '\0') {
        return state->current_rom_info.metadata.name;
    }
    return state->current_rom_info.title;
}

static const char *attract_description(const screensaver_attract_state_t *state) {
    if (state->current_rom_info.metadata.long_desc[0] != '\0') {
        return state->current_rom_info.metadata.long_desc;
    }
    if (state->current_rom_info.metadata.short_desc[0] != '\0') {
        return state->current_rom_info.metadata.short_desc;
    }
    return "No description available.";
}

static const char *attract_publisher(const screensaver_attract_state_t *state) {
    if (state->current_rom_info.metadata.author[0] != '\0') {
        return state->current_rom_info.metadata.author;
    }
    if (state->current_rom_info.metadata.developer[0] != '\0') {
        return state->current_rom_info.metadata.developer;
    }
    return "Unknown";
}

static float attract_description_scroll_offset(float featured_time_s, float scrollable_height) {
    if (scrollable_height <= 0.0f) {
        return 0.0f;
    }

    float travel_time = scrollable_height / SCREENSAVER_ATTRACT_SCROLL_PX_PER_S;
    if (travel_time < 0.01f) {
        return 0.0f;
    }

    float cycle_duration = (SCREENSAVER_ATTRACT_SCROLL_HOLD_S * 2.0f) + (travel_time * 2.0f);
    float t = fmodf(featured_time_s, cycle_duration);

    if (t < SCREENSAVER_ATTRACT_SCROLL_HOLD_S) {
        return 0.0f;
    }
    t -= SCREENSAVER_ATTRACT_SCROLL_HOLD_S;

    if (t < travel_time) {
        return scrollable_height * (t / travel_time);
    }
    t -= travel_time;

    if (t < SCREENSAVER_ATTRACT_SCROLL_HOLD_S) {
        return scrollable_height;
    }
    t -= SCREENSAVER_ATTRACT_SCROLL_HOLD_S;

    if (t < travel_time) {
        return scrollable_height * (1.0f - (t / travel_time));
    }

    return 0.0f;
}

static void attract_format_last_played(char *out, size_t out_len, int64_t ts) {
    if (!out || out_len == 0) {
        return;
    }
    if (ts <= 0) {
        snprintf(out, out_len, "Never played");
        return;
    }
    time_t t = (time_t)ts;
    char *s = ctime(&t);
    if (!s) {
        snprintf(out, out_len, "Played before");
        return;
    }
    size_t len = strnlen(s, out_len - 1);
    if (len > 0 && s[len - 1] == '\n') {
        len--;
    }
    snprintf(out, out_len, "Last played: %.*s", (int)len, s);
}

static void attract_format_players(const rom_info_t *rom_info, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    if (!rom_info) {
        out[0] = '\0';
        return;
    }
    if ((rom_info->metadata.players_min > 0) && (rom_info->metadata.players_max > 0)) {
        if (rom_info->metadata.players_min == rom_info->metadata.players_max) {
            snprintf(out, out_len, "%d player", (int)rom_info->metadata.players_max);
            return;
        }
        snprintf(out, out_len, "%d-%d players",
            (int)rom_info->metadata.players_min,
            (int)rom_info->metadata.players_max);
        return;
    }
    if (rom_info->metadata.players_max > 0) {
        snprintf(out, out_len, "%d player", (int)rom_info->metadata.players_max);
        return;
    }
    out[0] = '\0';
}

static bool attract_is_low_signal_feature(const rom_info_t *rom_info) {
    if (!rom_info) {
        return true;
    }
    const char *title = rom_info->metadata.name[0] != '\0' ? rom_info->metadata.name : rom_info->title;
    bool has_desc = rom_info->metadata.short_desc[0] != '\0' || rom_info->metadata.long_desc[0] != '\0';
    bool has_publisher = rom_info->metadata.author[0] != '\0' || rom_info->metadata.developer[0] != '\0';
    bool has_context = rom_info->metadata.release_year >= 0 ||
        rom_info->metadata.genre[0] != '\0' ||
        rom_info->metadata.players_max > 0;

    if (strcmp(title, "64DD") == 0 || strcmp(title, "IPL4") == 0) {
        return true;
    }

    return !has_desc && !has_publisher && !has_context;
}

static void attract_paragraph_add_text(const char *text) {
    const char *start = text;
    const char *p = text;
    while (*p) {
        if (*p == '\n') {
            if (p > start) {
                rdpq_paragraph_builder_span(start, (size_t)(p - start));
            }
            rdpq_paragraph_builder_newline();
            p++;
            start = p;
            continue;
        }
        p++;
    }
    if (p > start) {
        rdpq_paragraph_builder_span(start, (size_t)(p - start));
    }
}

static bool attract_should_skip_dir(const char *name) {
    if (!name || name[0] == '\0' || name[0] == '.') {
        return true;
    }
    return strcmp(name, "menu") == 0 ||
        strcmp(name, "ED64") == 0 ||
        strcmp(name, "ED64P") == 0 ||
        strcmp(name, "System Volume Information") == 0 ||
        strcmp(name, "__MACOSX") == 0;
}

static bool attract_should_skip_root_file(const char *name) {
    if (!name || name[0] == '\0') {
        return true;
    }
    return strcmp(name, "menu.bin") == 0 ||
        strcmp(name, "N64FlashcartMenu.n64") == 0 ||
        strcmp(name, "sc64menu.n64") == 0;
}

static bool attract_push_scan_dir(screensaver_attract_state_t *state, const char *path) {
    if (!state || !path || path[0] == '\0') {
        return false;
    }
    if (state->scan_stack_count >= state->scan_stack_capacity) {
        int next_capacity = state->scan_stack_capacity > 0 ? (state->scan_stack_capacity * 2) : 32;
        char **next = realloc(state->scan_stack, (size_t)next_capacity * sizeof(char *));
        if (!next) {
            return false;
        }
        state->scan_stack = next;
        state->scan_stack_capacity = next_capacity;
    }
    state->scan_stack[state->scan_stack_count] = strdup(path);
    if (!state->scan_stack[state->scan_stack_count]) {
        return false;
    }
    state->scan_stack_count++;
    return true;
}

static char *attract_pop_scan_dir(screensaver_attract_state_t *state) {
    if (!state || state->scan_stack_count <= 0) {
        return NULL;
    }
    state->scan_stack_count--;
    char *path = state->scan_stack[state->scan_stack_count];
    state->scan_stack[state->scan_stack_count] = NULL;
    return path;
}

static void attract_remove_pool_entry(screensaver_attract_state_t *state, int index) {
    if (!state || index < 0 || index >= state->pool_count) {
        return;
    }
    free(state->pool[index]);
    for (int i = index; i < (state->pool_count - 1); i++) {
        state->pool[i] = state->pool[i + 1];
    }
    state->pool_count--;
    if (state->current_index == index) {
        state->current_index = -1;
    } else if (state->current_index > index) {
        state->current_index--;
    }
}

static void attract_add_rom_path(screensaver_attract_state_t *state, const char *path) {
    if (!state || !path || path[0] == '\0') {
        return;
    }

    if (state->pool_count >= state->pool_capacity) {
        int next_capacity = state->pool_capacity > 0 ? (state->pool_capacity * 2) : 256;
        char **next = realloc(state->pool, (size_t)next_capacity * sizeof(char *));
        if (!next) {
            return;
        }
        state->pool = next;
        state->pool_capacity = next_capacity;
    }

    state->pool[state->pool_count] = strdup(path);
    if (!state->pool[state->pool_count]) {
        return;
    }
    state->pool_count++;
    state->scanned_game_count++;
}

static void attract_begin_scan(menu_t *menu, screensaver_attract_state_t *state) {
    if (!menu || !state || state->scan_started) {
        return;
    }

    bool pushed_any = false;
    for (int i = 0; attract_library_roots[i] != NULL; i++) {
        path_t *root = path_init(menu->storage_prefix, (char *)attract_library_roots[i]);
        if (!root) {
            continue;
        }
        if (directory_exists(path_get(root)) && attract_push_scan_dir(state, path_get(root))) {
            pushed_any = true;
        }
        path_free(root);
    }

    if (!pushed_any) {
        path_t *root = path_init(menu->storage_prefix, "/");
        if (!root) {
            return;
        }
        pushed_any = attract_push_scan_dir(state, path_get(root));
        path_free(root);
    }

    state->scan_started = pushed_any;
}

static void attract_scan_one_dir(screensaver_attract_state_t *state) {
    char *dir_string = attract_pop_scan_dir(state);
    if (!dir_string) {
        state->scan_complete = true;
        return;
    }

    path_t *dir_path = path_create(dir_string);
    free(dir_string);
    if (!dir_path || !directory_exists(path_get(dir_path))) {
        path_free(dir_path);
        if (state->scan_stack_count <= 0) {
            state->scan_complete = true;
        }
        return;
    }

    bool at_root = path_is_root(dir_path);
    dir_t info;
    int result = dir_findfirst(path_get(dir_path), &info);
    while (result == 0) {
        if (info.d_type == DT_DIR) {
            if (!attract_should_skip_dir(info.d_name)) {
                path_t *child = path_clone_push(dir_path, info.d_name);
                if (child) {
                    attract_push_scan_dir(state, path_get(child));
                    path_free(child);
                }
            }
        } else if (file_has_extensions(info.d_name, attract_rom_extensions)) {
            if (!(at_root && attract_should_skip_root_file(info.d_name))) {
                path_t *candidate = path_clone_push(dir_path, info.d_name);
                if (candidate) {
                    attract_add_rom_path(state, path_get(candidate));
                    path_free(candidate);
                }
            }
        }
        result = dir_findnext(path_get(dir_path), &info);
    }

    path_free(dir_path);
    if (state->scan_stack_count <= 0) {
        state->scan_complete = true;
    }
}

static void attract_clear_feature(screensaver_attract_state_t *state) {
    if (!state) {
        return;
    }
    if (state->boxart) {
        ui_components_boxart_free(state->boxart);
        state->boxart = NULL;
    }
    memset(&state->current_rom_info, 0, sizeof(state->current_rom_info));
    state->current_index = -1;
    state->featured_time_s = 0.0f;
}

static void attract_draw_boxart(const screensaver_attract_state_t *state) {
    int fallback_x = BOXART_X - 8;
    int fallback_y = BOXART_Y - 6;
    int fallback_w = BOXART_WIDTH + 16;
    int fallback_h = BOXART_HEIGHT + 12;

    if (state && state->boxart && state->boxart->image) {
        ui_components_box_draw(
            BOXART_X - 6,
            BOXART_Y - 6,
            BOXART_X + BOXART_WIDTH + 6,
            BOXART_Y + BOXART_HEIGHT + 6,
            RGBA32(0x04, 0x08, 0x0E, 0x88)
        );
        ui_components_boxart_draw(state->boxart);
        return;
    }

    ui_components_box_draw(
        fallback_x,
        fallback_y,
        fallback_x + fallback_w,
        fallback_y + fallback_h,
        RGBA32(0x10, 0x16, 0x22, 0xB0)
    );
}

static void attract_ensure_prompt_icon(screensaver_attract_state_t *state) {
    if (!state || state->prompt_icon || state->prompt_icon_attempted) {
        return;
    }
    state->prompt_icon_attempted = true;
    for (int i = 0; attract_prompt_icon_paths[i] != NULL; i++) {
        sprite_t *image = sprite_load(attract_prompt_icon_paths[i]);
        if (image != NULL) {
            state->prompt_icon = image;
            return;
        }
    }
}

static bool attract_load_feature(menu_t *menu, screensaver_attract_state_t *state, int index) {
    if (!menu || !state || index < 0 || index >= state->pool_count) {
        return false;
    }

    path_t *rom_path = path_create(state->pool[index]);
    if (!rom_path) {
        attract_remove_pool_entry(state, index);
        return false;
    }

    rom_info_t rom_info = {0};
    rom_load_options_t options = {
        .include_config = false,
        .include_long_description = true,
    };
    rom_err_t err = rom_config_load_ex(rom_path, &rom_info, &options);
    path_free(rom_path);
    if (err != ROM_OK) {
        attract_remove_pool_entry(state, index);
        return false;
    }
    if (attract_is_low_signal_feature(&rom_info)) {
        attract_remove_pool_entry(state, index);
        return false;
    }

    component_boxart_t *boxart = ui_components_boxart_init(
        menu->storage_prefix,
        rom_info.game_code,
        rom_info.title,
        IMAGE_BOXART_FRONT
    );

    attract_clear_feature(state);
    state->current_rom_info = rom_info;
    state->current_index = index;
    state->boxart = boxart;
    return true;
}

static void attract_pick_next_feature(menu_t *menu, screensaver_attract_state_t *state) {
    if (!menu || !state || state->pool_count <= 0) {
        attract_clear_feature(state);
        return;
    }

    int start = (int)(attract_rng_next(state) % (uint32_t)state->pool_count);
    for (int offset = 0; offset < state->pool_count; offset++) {
        int index = (start + offset) % state->pool_count;
        if (state->pool_count > 1 && index == state->current_index) {
            continue;
        }
        if (attract_load_feature(menu, state, index)) {
            return;
        }
        if (state->pool_count <= 0) {
            break;
        }
        if (index >= state->pool_count) {
            index = 0;
        }
    }

    if (state->current_index < 0 && state->pool_count > 0) {
        attract_load_feature(menu, state, 0);
    }
}

void screensaver_attract_init_state(screensaver_attract_state_t *state) {
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->rng = 0xA57A47C3u;
    state->current_index = -1;
}

void screensaver_attract_reset(screensaver_attract_state_t *state) {
    if (!state) {
        return;
    }
    attract_clear_feature(state);
}

void screensaver_attract_deinit(screensaver_attract_state_t *state) {
    if (!state) {
        return;
    }
    attract_clear_feature(state);
    for (int i = 0; i < state->pool_count; i++) {
        free(state->pool[i]);
    }
    free(state->pool);
    state->pool = NULL;
    state->pool_count = 0;
    state->pool_capacity = 0;
    for (int i = 0; i < state->scan_stack_count; i++) {
        free(state->scan_stack[i]);
    }
    free(state->scan_stack);
    state->scan_stack = NULL;
    state->scan_stack_count = 0;
    state->scan_stack_capacity = 0;
    state->scan_started = false;
    state->scan_complete = false;
    state->scanned_game_count = 0;
    if (state->prompt_icon) {
        sprite_free(state->prompt_icon);
        state->prompt_icon = NULL;
    }
    state->prompt_icon_attempted = false;
}

void screensaver_attract_activate(menu_t *menu, screensaver_attract_state_t *state) {
    if (!menu || !state) {
        return;
    }
    state->rng ^= (uint32_t)get_ticks_us();
    attract_rng_next(state);
    attract_ensure_prompt_icon(state);
    if (!state->scan_started) {
        attract_begin_scan(menu, state);
    }
    if (state->scan_complete && state->pool_count > 0) {
        attract_pick_next_feature(menu, state);
    }
}

void screensaver_attract_step(menu_t *menu, screensaver_attract_state_t *state, float dt) {
    if (!menu || !state) {
        return;
    }

    if (!state->scan_started) {
        attract_begin_scan(menu, state);
    }

    if (!state->scan_complete) {
        for (int i = 0; i < SCREENSAVER_ATTRACT_SCAN_DIRS_FRAME; i++) {
            if (state->scan_complete) {
                break;
            }
            attract_scan_one_dir(state);
        }
    }

    if (state->current_index < 0) {
        if (state->scan_complete && state->pool_count > 0) {
            attract_pick_next_feature(menu, state);
        }
        return;
    }

    state->featured_time_s += dt;
    if (state->featured_time_s >= SCREENSAVER_ATTRACT_ROTATE_SECONDS) {
        attract_pick_next_feature(menu, state);
    }
}

bool screensaver_attract_open_current(menu_t *menu, screensaver_attract_state_t *state) {
    if (!menu || !state || state->current_index < 0 || state->current_index >= state->pool_count) {
        return false;
    }

    if (menu->load.rom_path) {
        path_free(menu->load.rom_path);
    }
    menu->load.rom_path = path_create(state->pool[state->current_index]);
    if (!menu->load.rom_path) {
        return false;
    }
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    menu->load.back_mode = menu->mode;
    menu->next_mode = MENU_MODE_LOAD_ROM;
    sound_play_effect(SFX_ENTER);
    return true;
}

void screensaver_attract_draw(menu_t *menu, surface_t *display, screensaver_attract_state_t *state) {
    (void)display;

    ui_components_background_draw();

    if (!state || state->current_index < 0 || state->current_index >= state->pool_count) {
        ui_components_box_draw(
            VISIBLE_AREA_X0 + 12,
            VISIBLE_AREA_Y0 + 18,
            BOXART_X - 16,
            LAYOUT_ACTIONS_SEPARATOR_Y - 18,
            RGBA32(0x03, 0x07, 0x0D, 0xA8)
        );
        ui_components_box_draw(
            VISIBLE_AREA_X0 + 12,
            LAYOUT_ACTIONS_SEPARATOR_Y + 10,
            VISIBLE_AREA_X1 - 12,
            VISIBLE_AREA_Y1 - 10,
            RGBA32(0x02, 0x04, 0x08, 0x98)
        );
        rdpq_text_print(
            &(rdpq_textparms_t){
                .style_id = STL_DEFAULT,
                .width = BOXART_X - VISIBLE_AREA_X0 - 60,
                .height = 80,
                .wrap = WRAP_WORD,
            },
            FNT_DEFAULT,
            VISIBLE_AREA_X0 + 28,
            VISIBLE_AREA_Y0 + 38,
            "Scanning your game library..."
        );
        rdpq_text_printf(
            &(rdpq_textparms_t){
                .style_id = STL_BLUE,
                .width = BOXART_X - VISIBLE_AREA_X0 - 60,
                .height = 80,
                .wrap = WRAP_WORD,
            },
            FNT_DEFAULT,
            VISIBLE_AREA_X0 + 28,
            VISIBLE_AREA_Y0 + 82,
            "%lu games indexed so far",
            (unsigned long)state->scanned_game_count
        );
        rdpq_text_print(
            STL_DEFAULT,
            FNT_DEFAULT,
            VISIBLE_AREA_X0 + 28,
            LAYOUT_ACTIONS_SEPARATOR_Y + 30,
            "Give it a moment on the first run."
        );
        return;
    }

    const char *title = attract_display_name(state);
    const char *publisher = attract_publisher(state);
    const char *description = attract_description(state);

    ui_components_box_draw(
        VISIBLE_AREA_X0 + 12,
        VISIBLE_AREA_Y0 + 18,
        BOXART_X - 16,
        LAYOUT_ACTIONS_SEPARATOR_Y - 18,
        RGBA32(0x03, 0x07, 0x0D, 0xA8)
    );
    ui_components_box_draw(
        VISIBLE_AREA_X0 + 12,
        LAYOUT_ACTIONS_SEPARATOR_Y + 10,
        VISIBLE_AREA_X1 - 12,
        VISIBLE_AREA_Y1 - 10,
        RGBA32(0x02, 0x04, 0x08, 0x98)
    );
    rdpq_text_print(
        &(rdpq_textparms_t){
            .style_id = STL_DEFAULT,
            .width = BOXART_X - VISIBLE_AREA_X0 - 60,
            .height = 40,
            .wrap = WRAP_WORD,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + 28,
        VISIBLE_AREA_Y0 + 36,
        title
    );
    rdpq_text_printf(
        &(rdpq_textparms_t){
            .style_id = STL_BLUE,
            .width = BOXART_X - VISIBLE_AREA_X0 - 60,
            .height = 20,
            .wrap = WRAP_WORD,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + 28,
        VISIBLE_AREA_Y0 + 84,
        "Publisher: %s",
        publisher
    );

    char players[32];
    attract_format_players(&state->current_rom_info, players, sizeof(players));
    char meta_summary[192];
    meta_summary[0] = '\0';
    if (state->current_rom_info.metadata.release_year >= 0) {
        snprintf(meta_summary + strlen(meta_summary), sizeof(meta_summary) - strlen(meta_summary), "%d", (int)state->current_rom_info.metadata.release_year);
    }
    if (state->current_rom_info.metadata.genre[0] != '\0') {
        snprintf(
            meta_summary + strlen(meta_summary),
            sizeof(meta_summary) - strlen(meta_summary),
            "%s%s",
            meta_summary[0] != '\0' ? "  ·  " : "",
            state->current_rom_info.metadata.genre
        );
    }
    if (players[0] != '\0') {
        snprintf(
            meta_summary + strlen(meta_summary),
            sizeof(meta_summary) - strlen(meta_summary),
            "%s%s",
            meta_summary[0] != '\0' ? "  ·  " : "",
            players
        );
    }
    if (meta_summary[0] != '\0') {
        rdpq_text_print(
            &(rdpq_textparms_t){
                .style_id = STL_GRAY,
                .width = BOXART_X - VISIBLE_AREA_X0 - 60,
                .height = 18,
                .wrap = WRAP_WORD,
            },
            FNT_DEFAULT,
            VISIBLE_AREA_X0 + 28,
            VISIBLE_AREA_Y0 + 104,
            meta_summary
        );
    }

    char play_line[80];
    playtime_entry_t *playtime = playtime_get_if_cached(&menu->playtime, state->pool[state->current_index]);
    attract_format_last_played(play_line, sizeof(play_line), playtime ? playtime->last_played : 0);
    rdpq_text_print(
        &(rdpq_textparms_t){
            .style_id = playtime && playtime->last_played > 0 ? STL_GRAY : STL_YELLOW,
            .width = BOXART_X - VISIBLE_AREA_X0 - 60,
            .height = 18,
            .wrap = WRAP_WORD,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + 28,
        VISIBLE_AREA_Y0 + 124,
        play_line
    );

    int base_x = VISIBLE_AREA_X0 + 28;
    int base_y = VISIBLE_AREA_Y0 + 150;
    int text_right_limit = BOXART_X - 12;
    int text_width = text_right_limit - base_x;
    if (text_width < 180) {
        text_width = 180;
    }

    int clip_y0 = base_y;
    int clip_y1 = LAYOUT_ACTIONS_SEPARATOR_Y - TEXT_MARGIN_VERTICAL;

    char description_block[2048];
    snprintf(
        description_block,
        sizeof(description_block),
        "Description:\n%s",
        description
    );

    rdpq_paragraph_builder_begin(
        &(rdpq_textparms_t) {
            .width = text_width,
            .height = 10000,
            .wrap = WRAP_WORD,
            .line_spacing = TEXT_LINE_SPACING_ADJUST - 1,
        },
        FNT_DEFAULT,
        NULL
    );
    rdpq_paragraph_builder_style(STL_DEFAULT);
    attract_paragraph_add_text(description_block);
    rdpq_paragraph_t *layout = rdpq_paragraph_builder_end();

    rdpq_set_scissor(base_x, clip_y0, base_x + text_width, clip_y1);
    int visible_height = clip_y1 - clip_y0;
    float total_height = layout->bbox.y1 - layout->bbox.y0;
    float scrollable_height = total_height - (float)visible_height;
    float scroll_offset = attract_description_scroll_offset(state->featured_time_s, scrollable_height);
    rdpq_paragraph_render(layout, base_x, base_y - layout->bbox.y0 - scroll_offset);
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
    rdpq_paragraph_free(layout);

    int icon_x = VISIBLE_AREA_X0 + 22;
    int icon_y = LAYOUT_ACTIONS_SEPARATOR_Y + 20;
    rdpq_text_print(
        &(rdpq_textparms_t){
            .style_id = STL_DEFAULT,
            .width = 240,
            .height = 28,
            .valign = VALIGN_CENTER,
        },
        FNT_DEFAULT,
        icon_x + 38,
        icon_y,
        "Open Game"
    );
    rdpq_text_print(
        &(rdpq_textparms_t){
            .style_id = STL_YELLOW,
            .width = 320,
            .height = 18,
        },
        FNT_DEFAULT,
        icon_x + 38,
        icon_y + 19,
        "Any other input exits"
    );
    if (state->prompt_icon) {
        rdpq_mode_push();
            rdpq_set_mode_standard();
            rdpq_sprite_blit(state->prompt_icon, (float)icon_x, (float)icon_y, NULL);
        rdpq_mode_pop();
    } else {
        ui_components_box_draw(
            icon_x,
            icon_y,
            icon_x + 28,
            icon_y + 28,
            RGBA32(0x1E, 0x78, 0xD7, 0xE0)
        );
        rdpq_text_print(
            &(rdpq_textparms_t){
                .style_id = STL_DEFAULT,
                .width = 18,
                .height = 18,
                .align = ALIGN_CENTER,
                .valign = VALIGN_TOP,
            },
            FNT_DEFAULT,
            icon_x + 5,
            icon_y + 4,
            "A"
        );
    }

    attract_draw_boxart(state);

}
