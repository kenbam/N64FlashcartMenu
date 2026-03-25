/**
 * @file rom_metadata.c
 * @brief ROM metadata loading from curated metadata directories
 * @ingroup menu
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libdragon.h>

#include "menu_paths.h"
#include "path.h"
#include "rom_info.h"
#include "rom_metadata.h"
#include "utils/fs.h"


static void metadata_copy_if_empty (char *destination, size_t destination_length, const char *source) {
    if ((destination == NULL) || (source == NULL) || (destination_length == 0)) {
        return;
    }

    if ((destination[0] != '\0') || (source[0] == '\0')) {
        return;
    }

    snprintf(destination, destination_length, "%s", source);
}

static void read_text_file_to_buffer (const char *path, char *buffer, size_t buffer_length) {
    if ((path == NULL) || (buffer == NULL) || (buffer_length == 0)) {
        return;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }

    size_t bytes_read = fread(buffer, 1, buffer_length - 1, file);
    buffer[bytes_read] = '\0';
    fclose(file);
}

/** Pre-scanned directory listing to avoid per-file stat() calls. */
#define DIR_LISTING_MAX 48
typedef struct {
    char names[DIR_LISTING_MAX][64];
    int count;
    bool scanned;
} dir_listing_t;

static void dir_listing_scan(dir_listing_t *listing, const char *dir_path) {
    listing->count = 0;
    listing->scanned = true;
    if (!dir_path) {
        return;
    }
    dir_t info;
    int result = dir_findfirst(dir_path, &info);
    while (result == 0 && listing->count < DIR_LISTING_MAX) {
        if (info.d_type != DT_DIR) {
            snprintf(listing->names[listing->count], sizeof(listing->names[0]), "%s", info.d_name);
            listing->count++;
        }
        result = dir_findnext(dir_path, &info);
    }
}

static bool dir_listing_contains(const dir_listing_t *listing, const char *filename) {
    for (int i = 0; i < listing->count; i++) {
        if (strcasecmp(listing->names[i], filename) == 0) {
            return true;
        }
    }
    return false;
}

static void read_metadata_text_file_if_missing(path_t *directory, const dir_listing_t *listing,
                                               bool enabled, const char *filename, char *buffer, size_t buffer_length) {
    if (!enabled || (directory == NULL) || (filename == NULL) || (buffer == NULL) || (buffer_length == 0) || (buffer[0] != '\0')) {
        return;
    }
    if (listing && listing->scanned && !dir_listing_contains(listing, filename)) {
        return;
    }

    path_t *text_path = path_clone(directory);
    path_push(text_path, (char *)filename);
    read_text_file_to_buffer(path_get(text_path), buffer, buffer_length);
    path_free(text_path);
}

static void read_metadata_mapped_text_if_missing(path_t *directory, const dir_listing_t *listing,
                                                 bool enabled, const char *mapped_filename,
                                                 const char *default_filename, char *buffer, size_t buffer_length) {
    const char *filename = ((mapped_filename != NULL) && (mapped_filename[0] != '\0')) ? mapped_filename : default_filename;
    read_metadata_text_file_if_missing(directory, listing, enabled, filename, buffer, buffer_length);
}

static char *trim_whitespace (char *string) {
    if (string == NULL) {
        return NULL;
    }

    while ((*string != '\0') && isspace((unsigned char)*string)) {
        string++;
    }

    size_t length = strlen(string);
    while ((length > 0) && isspace((unsigned char)string[length - 1])) {
        string[--length] = '\0';
    }

    return string;
}

static void lowercase_ascii (char *string) {
    if (string == NULL) {
        return;
    }
    for (; *string != '\0'; string++) {
        *string = (char)tolower((unsigned char)*string);
    }
}

static int32_t parse_release_year_from_value (const char *value) {
    if (value == NULL) {
        return -1;
    }

    size_t len = strlen(value);
    if (len < 4) {
        return -1;
    }
    for (size_t pos = 0; pos <= len - 4; pos++) {
        const char *cursor = &value[pos];
        if (!isdigit((unsigned char)cursor[0]) ||
            !isdigit((unsigned char)cursor[1]) ||
            !isdigit((unsigned char)cursor[2]) ||
            !isdigit((unsigned char)cursor[3])) {
            continue;
        }

        int32_t year = (int32_t)((cursor[0] - '0') * 1000 +
                                 (cursor[1] - '0') * 100 +
                                 (cursor[2] - '0') * 10 +
                                 (cursor[3] - '0'));
        if ((year >= 1980) && (year <= 2099)) {
            return year;
        }
    }

    return -1;
}

static bool parse_player_count_from_value(const char *value, int32_t *out_min, int32_t *out_max) {
    if (!value || !out_min || !out_max) {
        return false;
    }

    char normalized[128];
    snprintf(normalized, sizeof(normalized), "%s", value);
    lowercase_ascii(normalized);
    char *trimmed = trim_whitespace(normalized);
    if (trimmed[0] == '\0') {
        return false;
    }

    if (strncmp(trimmed, ">=", 2) == 0) {
        int32_t parsed = (int32_t)atoi(trimmed + 2);
        if (parsed > 0) {
            *out_min = parsed;
            *out_max = 99;
            return true;
        }
    }
    if (strncmp(trimmed, "<=", 2) == 0) {
        int32_t parsed = (int32_t)atoi(trimmed + 2);
        if (parsed > 0) {
            *out_min = 1;
            *out_max = parsed;
            return true;
        }
    }
    if (strncmp(trimmed, "up to ", 6) == 0) {
        int32_t parsed = (int32_t)atoi(trimmed + 6);
        if (parsed > 0) {
            *out_min = 1;
            *out_max = parsed;
            return true;
        }
    }

    int32_t values[2] = {0};
    int value_count = 0;
    for (char *cursor = trimmed; *cursor != '\0'; ) {
        if (!isdigit((unsigned char)*cursor)) {
            cursor++;
            continue;
        }

        char *end = cursor;
        long parsed = strtol(cursor, &end, 10);
        if ((end == cursor) || (parsed <= 0)) {
            break;
        }
        if (value_count < 2) {
            values[value_count++] = (int32_t)parsed;
        }
        cursor = end;
    }

    if (value_count == 0) {
        return false;
    }
    if (strchr(trimmed, '+') != NULL) {
        *out_min = values[0];
        *out_max = 99;
        return true;
    }
    if (value_count >= 2) {
        *out_min = values[0];
        *out_max = values[1];
        if (*out_min > *out_max) {
            int32_t swap = *out_min;
            *out_min = *out_max;
            *out_max = swap;
        }
        return true;
    }

    *out_min = values[0];
    *out_max = values[0];
    return true;
}

static int32_t parse_age_rating_from_value(const char *value) {
    if (!value || value[0] == '\0') {
        return -1;
    }

    while (*value == ' ' || *value == '\t') {
        value++;
    }

    char *parse_end = NULL;
    long parsed = strtol(value, &parse_end, 10);
    if (parse_end == value || parsed < 0 || parsed > 18) {
        return -1;
    }

    while (*parse_end == ' ' || *parse_end == '\t') {
        parse_end++;
    }

    if (*parse_end == '+') {
        parse_end++;
    }
    while (*parse_end == ' ' || *parse_end == '\t') {
        parse_end++;
    }

    return (*parse_end == '\0') ? (int32_t)parsed : -1;
}

static rom_esrb_age_rating_t parse_esrb_age_rating_from_value(const char *value) {
    if (!value || value[0] == '\0') {
        return ROM_ESRB_AGE_RATING_NONE;
    }

    while (*value == ' ' || *value == '\t') {
        value++;
    }

    char *parse_end = NULL;
    long parsed = strtol(value, &parse_end, 10);
    if (parse_end != value) {
        while (*parse_end == ' ' || *parse_end == '\t') {
            parse_end++;
        }
        if (*parse_end == '\0' && parsed >= ROM_ESRB_AGE_RATING_NONE && parsed <= ROM_ESRB_AGE_RATING_ADULT) {
            return (rom_esrb_age_rating_t)parsed;
        }
    }

    if (strcasecmp(value, "e") == 0 || strcasecmp(value, "everyone") == 0) {
        return ROM_ESRB_AGE_RATING_EVERYONE;
    }
    if (strcasecmp(value, "e10+") == 0 ||
        strcasecmp(value, "everyone 10+") == 0 ||
        strcasecmp(value, "everyone10+") == 0 ||
        strcasecmp(value, "everyone 10 plus") == 0) {
        return ROM_ESRB_AGE_RATING_EVERYONE_10_PLUS;
    }
    if (strcasecmp(value, "t") == 0 || strcasecmp(value, "teen") == 0) {
        return ROM_ESRB_AGE_RATING_TEEN;
    }
    if (strcasecmp(value, "m") == 0 || strcasecmp(value, "mature") == 0 || strcasecmp(value, "mature 17+") == 0) {
        return ROM_ESRB_AGE_RATING_MATURE;
    }
    if (strcasecmp(value, "ao") == 0 || strcasecmp(value, "adults only") == 0 || strcasecmp(value, "adults only 18+") == 0) {
        return ROM_ESRB_AGE_RATING_ADULT;
    }

    return ROM_ESRB_AGE_RATING_NONE;
}

static void load_rom_metadata_from_directory (path_t *directory, rom_info_t *rom_info, bool include_long_description) {
    if ((directory == NULL) || (rom_info == NULL)) {
        return;
    }

    path_t *metadata_path = path_clone(directory);
    path_push(metadata_path, "metadata.ini");
    FILE *metadata_file = fopen(path_get(metadata_path), "rb");
    path_free(metadata_path);
    if (metadata_file == NULL) {
        return;
    }

    bool in_meta_section = false;
    bool in_curated_section = false;
    char long_desc_file[128] = {0};
    char curated_description_file[128] = {0};
    char hook_file[128] = {0};
    char why_play_file[128] = {0};
    char vibe_file[128] = {0};
    char notable_file[128] = {0};
    char context_file[128] = {0};
    char play_curator_note_file[128] = {0};
    char tags_file[128] = {0};
    char warnings_file[128] = {0};
    char museum_card_file[128] = {0};
    char trivia_museum_file[128] = {0};
    char oddities_file[128] = {0};
    char design_quirks_file[128] = {0};
    char discovery_prompts_file[128] = {0};
    char curator_file[128] = {0};
    char museum_file[128] = {0};
    char trivia_file[128] = {0};
    char reception_file[128] = {0};
    char line[512];

    while (fgets(line, sizeof(line), metadata_file) != NULL) {
        char *cursor = trim_whitespace(line);
        if (*cursor == '\0' || *cursor == ';' || *cursor == '#') {
            continue;
        }

        // Handle UTF-8 BOM on first line.
        if ((unsigned char)cursor[0] == 0xEF && (unsigned char)cursor[1] == 0xBB && (unsigned char)cursor[2] == 0xBF) {
            cursor += 3;
            cursor = trim_whitespace(cursor);
        }

        if (*cursor == '[') {
            char *section_end = strchr(cursor, ']');
            if (section_end == NULL) {
                continue;
            }
            *section_end = '\0';
            char *section_name = trim_whitespace(cursor + 1);
            lowercase_ascii(section_name);
            in_meta_section = ((strcmp(section_name, "meta") == 0) || (strcmp(section_name, "metadata") == 0));
            in_curated_section = (strcmp(section_name, "curated") == 0);
            continue;
        }

        if (!in_meta_section && !in_curated_section) {
            continue;
        }

        char *equal_sign = strchr(cursor, '=');
        if (equal_sign == NULL) {
            continue;
        }

        *equal_sign = '\0';
        char *key = trim_whitespace(cursor);
        char *value = trim_whitespace(equal_sign + 1);
        lowercase_ascii(key);

        if (in_curated_section) {
            if ((strcmp(key, "hook") == 0) && (hook_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(hook_file, sizeof(hook_file), "%s", value);
            } else if ((strcmp(key, "why_play") == 0) && (why_play_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(why_play_file, sizeof(why_play_file), "%s", value);
            } else if ((strcmp(key, "vibe") == 0) && (vibe_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(vibe_file, sizeof(vibe_file), "%s", value);
            } else if ((strcmp(key, "notable") == 0) && (notable_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(notable_file, sizeof(notable_file), "%s", value);
            } else if ((strcmp(key, "context") == 0) && (context_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(context_file, sizeof(context_file), "%s", value);
            } else if ((strcmp(key, "play_curator_note") == 0) && (play_curator_note_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(play_curator_note_file, sizeof(play_curator_note_file), "%s", value);
            } else if ((strcmp(key, "tags") == 0) && (tags_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(tags_file, sizeof(tags_file), "%s", value);
            } else if ((strcmp(key, "warnings") == 0) && (warnings_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(warnings_file, sizeof(warnings_file), "%s", value);
            } else if ((strcmp(key, "museum_card") == 0) && (museum_card_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(museum_card_file, sizeof(museum_card_file), "%s", value);
            } else if ((strcmp(key, "trivia_museum") == 0) && (trivia_museum_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(trivia_museum_file, sizeof(trivia_museum_file), "%s", value);
            } else if ((strcmp(key, "oddities") == 0) && (oddities_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(oddities_file, sizeof(oddities_file), "%s", value);
            } else if ((strcmp(key, "design_quirks") == 0) && (design_quirks_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(design_quirks_file, sizeof(design_quirks_file), "%s", value);
            } else if ((strcmp(key, "discovery_prompts") == 0) && (discovery_prompts_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(discovery_prompts_file, sizeof(discovery_prompts_file), "%s", value);
            } else if ((strcmp(key, "curator") == 0) && (curator_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(curator_file, sizeof(curator_file), "%s", value);
            } else if ((strcmp(key, "museum") == 0) && (museum_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(museum_file, sizeof(museum_file), "%s", value);
            } else if ((strcmp(key, "trivia") == 0) && (trivia_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(trivia_file, sizeof(trivia_file), "%s", value);
            } else if ((strcmp(key, "reception") == 0) && (reception_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(reception_file, sizeof(reception_file), "%s", value);
            } else if (((strcmp(key, "full_description") == 0) || (strcmp(key, "description") == 0)) &&
                       (curated_description_file[0] == '\0') && (value[0] != '\0')) {
                snprintf(curated_description_file, sizeof(curated_description_file), "%s", value);
            }
            continue;
        }

        if ((strcmp(key, "name") == 0) || (strcmp(key, "title") == 0)) {
            metadata_copy_if_empty(rom_info->metadata.name, sizeof(rom_info->metadata.name), value);
        } else if ((strcmp(key, "author") == 0) || (strcmp(key, "publisher") == 0)) {
            metadata_copy_if_empty(rom_info->metadata.author, sizeof(rom_info->metadata.author), value);
        } else if ((strcmp(key, "developer") == 0) || (strcmp(key, "dev") == 0) || (strcmp(key, "studio") == 0)) {
            metadata_copy_if_empty(rom_info->metadata.developer, sizeof(rom_info->metadata.developer), value);
        } else if ((strcmp(key, "genre") == 0) || (strcmp(key, "genres") == 0) || (strcmp(key, "category") == 0)) {
            metadata_copy_if_empty(rom_info->metadata.genre, sizeof(rom_info->metadata.genre), value);
        } else if ((strcmp(key, "series") == 0) || (strcmp(key, "franchise") == 0)) {
            metadata_copy_if_empty(rom_info->metadata.series, sizeof(rom_info->metadata.series), value);
        } else if ((strcmp(key, "modes") == 0) || (strcmp(key, "mode") == 0) || (strcmp(key, "tags") == 0)) {
            metadata_copy_if_empty(rom_info->metadata.modes, sizeof(rom_info->metadata.modes), value);
        } else if (((strcmp(key, "players") == 0) ||
                    (strcmp(key, "player-count") == 0) ||
                    (strcmp(key, "player_count") == 0) ||
                    (strcmp(key, "playercount") == 0)) &&
                   (rom_info->metadata.players_max < 0)) {
            int32_t players_min = -1;
            int32_t players_max = -1;
            if (parse_player_count_from_value(value, &players_min, &players_max)) {
                rom_info->metadata.players_min = players_min;
                rom_info->metadata.players_max = players_max;
            }
        } else if (((strcmp(key, "players-min") == 0) || (strcmp(key, "players_min") == 0)) &&
                   (rom_info->metadata.players_min < 0)) {
            rom_info->metadata.players_min = (int32_t)atoi(value);
        } else if (((strcmp(key, "players-max") == 0) || (strcmp(key, "players_max") == 0)) &&
                   (rom_info->metadata.players_max < 0)) {
            rom_info->metadata.players_max = (int32_t)atoi(value);
        } else if ((strcmp(key, "short-desc") == 0) || (strcmp(key, "short_desc") == 0) || (strcmp(key, "summary") == 0)) {
            metadata_copy_if_empty(rom_info->metadata.short_desc, sizeof(rom_info->metadata.short_desc), value);
        } else if ((strcmp(key, "long-desc") == 0) && (long_desc_file[0] == '\0') && (value[0] != '\0')) {
            snprintf(long_desc_file, sizeof(long_desc_file), "%s", value);
        } else if (((strcmp(key, "age-rating") == 0) || (strcmp(key, "age_rating") == 0)) &&
                   (rom_info->metadata.age_rating < 0)) {
            int32_t parsed = parse_age_rating_from_value(value);
            if (parsed >= 0) {
                rom_info->metadata.age_rating = parsed;
            }
        } else if (((strcmp(key, "esrb") == 0) ||
                    (strcmp(key, "esrb-rating") == 0) ||
                    (strcmp(key, "esrb_rating") == 0) ||
                    (strcmp(key, "esrb-age-rating") == 0) ||
                    (strcmp(key, "esrb_age_rating") == 0)) &&
                   (rom_info->metadata.esrb_age_rating == ROM_ESRB_AGE_RATING_NONE)) {
            rom_esrb_age_rating_t parsed = parse_esrb_age_rating_from_value(value);
            if (parsed != ROM_ESRB_AGE_RATING_NONE) {
                rom_info->metadata.esrb_age_rating = parsed;
            }
        } else if (((strcmp(key, "release-date") == 0) ||
                    (strcmp(key, "release_date") == 0) ||
                    (strcmp(key, "releaseyear") == 0) ||
                    (strcmp(key, "release-year") == 0) ||
                    (strcmp(key, "year") == 0)) &&
                   (rom_info->metadata.release_year < 0)) {
            int32_t year = parse_release_year_from_value(value);
            if (year >= 0) {
                rom_info->metadata.release_year = year;
            }
        }
    }

    fclose(metadata_file);

    // Pre-scan the directory once to avoid per-file stat() calls.
    // One dir scan (~10-30ms) replaces ~18 individual fopen attempts (~90-180ms).
    dir_listing_t listing = { .count = 0, .scanned = false };
    if (include_long_description) {
        dir_listing_scan(&listing, path_get(directory));
    }

    if (include_long_description &&
        (rom_info->metadata.long_desc[0] == '\0') &&
        (long_desc_file[0] != '\0')) {
        if (!listing.scanned || dir_listing_contains(&listing, long_desc_file)) {
            path_t *description_path = path_clone(directory);
            path_push(description_path, long_desc_file);
            read_text_file_to_buffer(path_get(description_path), rom_info->metadata.long_desc,
                                     sizeof(rom_info->metadata.long_desc));
            path_free(description_path);
        }
    }

    // Common metadata fallback used by our generated sets.
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, curated_description_file, "description.txt",
                                         rom_info->metadata.long_desc, sizeof(rom_info->metadata.long_desc));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, hook_file, "hook.txt",
                                         rom_info->metadata.hook, sizeof(rom_info->metadata.hook));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, why_play_file, "why_play.txt",
                                         rom_info->metadata.why_play, sizeof(rom_info->metadata.why_play));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, vibe_file, "vibe.txt",
                                         rom_info->metadata.vibe, sizeof(rom_info->metadata.vibe));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, notable_file, "notable.txt",
                                         rom_info->metadata.notable, sizeof(rom_info->metadata.notable));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, context_file, "context.txt",
                                         rom_info->metadata.context, sizeof(rom_info->metadata.context));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, play_curator_note_file, "play_curator_note.txt",
                                         rom_info->metadata.play_curator_note, sizeof(rom_info->metadata.play_curator_note));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, tags_file, "tags.txt",
                                         rom_info->metadata.tags, sizeof(rom_info->metadata.tags));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, warnings_file, "warnings.txt",
                                         rom_info->metadata.warnings, sizeof(rom_info->metadata.warnings));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, museum_card_file, "museum_card.txt",
                                         rom_info->metadata.museum_card, sizeof(rom_info->metadata.museum_card));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, trivia_museum_file, "trivia_museum.txt",
                                         rom_info->metadata.trivia_museum, sizeof(rom_info->metadata.trivia_museum));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, oddities_file, "oddities.txt",
                                         rom_info->metadata.oddities, sizeof(rom_info->metadata.oddities));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, design_quirks_file, "design_quirks.txt",
                                         rom_info->metadata.design_quirks, sizeof(rom_info->metadata.design_quirks));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, discovery_prompts_file, "discovery_prompts.txt",
                                         rom_info->metadata.discovery_prompts, sizeof(rom_info->metadata.discovery_prompts));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, curator_file, "curator.txt",
                                         rom_info->metadata.curator, sizeof(rom_info->metadata.curator));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, museum_file, "museum.txt",
                                         rom_info->metadata.museum, sizeof(rom_info->metadata.museum));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, trivia_file, "trivia.txt",
                                         rom_info->metadata.trivia, sizeof(rom_info->metadata.trivia));
    read_metadata_mapped_text_if_missing(directory, &listing, include_long_description, reception_file, "reception.txt",
                                         rom_info->metadata.reception, sizeof(rom_info->metadata.reception));
}

void rom_metadata_load (path_t *rom_path, rom_info_t *rom_info, bool include_long_description) {
    if ((rom_path == NULL) || (rom_info == NULL)) {
        return;
    }

    char *full_path = path_get(rom_path);
    if (full_path == NULL) {
        return;
    }

    char *prefix_end = strstr(full_path, ":/");
    if (prefix_end == NULL) {
        return;
    }

    char prefix[16];
    size_t prefix_length = (size_t)(prefix_end - full_path) + 2;
    if (prefix_length >= sizeof(prefix)) {
        return;
    }

    memcpy(prefix, full_path, prefix_length);
    prefix[prefix_length] = '\0';

    path_t *metadata_directory = path_init(prefix, MENU_DIR_METADATA);
    for (size_t i = 0; i < 4; i++) {
        char component[2] = { rom_info->game_code[i], '\0' };
        if (component[0] == '\0') {
            path_free(metadata_directory);
            return;
        }
        path_push(metadata_directory, component);
    }

    load_rom_metadata_from_directory(metadata_directory, rom_info, include_long_description);

    // Region-agnostic fallback — skip if region-specific already populated primary fields.
    bool has_primary = (rom_info->metadata.name[0] != '\0') &&
                       (rom_info->metadata.short_desc[0] != '\0' || rom_info->metadata.long_desc[0] != '\0');
    if (!has_primary) {
        path_pop(metadata_directory);
        load_rom_metadata_from_directory(metadata_directory, rom_info, include_long_description);
    }

    path_free(metadata_directory);
}
