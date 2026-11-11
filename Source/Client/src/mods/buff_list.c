#include "buff_list.h"

#include <string.h>

#include "util/string_util.h"

#define POWER_MARKER " power of "
#define BONUS_PREFIX " : "

static const char *const BUFF_NAMES[BUFF_KIND_COUNT] = {"Bless", "Enhance", "Protect"};

BuffKind buff_list_sprite_kind(const int sprite) {
    if (sprite == BUFF_SPRITE_BLESS) return BUFF_KIND_BLESS;
    if (sprite == BUFF_SPRITE_ENHANCE) return BUFF_KIND_ENHANCE;
    if (sprite == BUFF_SPRITE_PROTECT) return BUFF_KIND_PROTECT;
    return BUFF_KIND_NONE;
}

int buff_list_is_header(const char *text) {
    return strcmp(text, BUFF_LIST_HEADER) == 0;
}

void buff_list_parser_start(BuffListParser *parser) {
    memset(parser, 0, sizeof(*parser));
    parser->current_kind = BUFF_KIND_NONE;
}

/* "<Name> power of N": sets the current buff, or clears it for a buff that isn't kept. */
static int read_power_line(BuffListParser *parser, const char *text) {
    const char *marker = strstr(text, POWER_MARKER);
    const size_t name_length = marker != NULL ? (size_t) (marker - text) : 0;
    int kind;

    if (marker == NULL) return 0;
    parser->current_kind = BUFF_KIND_NONE;
    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
        if (strlen(BUFF_NAMES[kind]) == name_length && strncmp(text, BUFF_NAMES[kind], name_length) == 0) {
            BuffReading *reading = &parser->readings[kind];
            const char *cursor = marker + strlen(POWER_MARKER);

            memset(reading, 0, sizeof(*reading));
            reading->known = 1;
            read_int(&cursor, &reading->power);
            parser->current_kind = kind;
        }
    }
    return 1;
}

/* " : +N <stat>" under the current buff. */
static void read_bonus_line(BuffListParser *parser, const char *text) {
    BuffReading *reading;
    const char *cursor;
    const char *stat_name;
    int amount;
    int attribute_id;

    if (parser->current_kind == BUFF_KIND_NONE) return;
    if (!starts_with(text, BONUS_PREFIX)) return;
    cursor = text + strlen(BONUS_PREFIX);
    if (!read_int(&cursor, &amount) || *cursor != ' ') return;
    stat_name = cursor + 1;

    reading = &parser->readings[parser->current_kind];
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        if (strcmp(stat_name, attribute_names[attribute_id]) == 0) {
            reading->attribute_bonus[attribute_id] = amount;
            reading->has_exact = 1;
            return;
        }
    }
    if (strcmp(stat_name, "Weapon Value") == 0) {
        reading->weapon_bonus = amount;
        reading->has_exact = 1;
    } else if (strcmp(stat_name, "Armor Value") == 0) {
        reading->armor_bonus = amount;
        reading->has_exact = 1;
    }
}

int buff_list_parser_feed(BuffListParser *parser, const char *text) {
    if (is_blank(text)) {
        parser->blank_lines++;
        return parser->blank_lines < 2;
    }
    if (!read_power_line(parser, text)) read_bonus_line(parser, text);
    return 1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int buff_list_slot_needs_fetch(const int old_sprite, const int old_active, const int new_sprite, const int new_active) {
    if (buff_list_sprite_kind(old_sprite) == BUFF_KIND_NONE && buff_list_sprite_kind(new_sprite) == BUFF_KIND_NONE) {
        return 0;
    }
    if (old_sprite != new_sprite) return 1;
    return new_active >= old_active;
}
