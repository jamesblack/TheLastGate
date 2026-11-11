#include "item_details.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "util/array_util.h"
#include "util/string_util.h"

#define TENTHS_PER_PERCENT 10.0F
#define SILVER_PER_GOLD 100
#define COMPARE_SECTION_LINES 2 /* lines that follow "You compare it with ..." */
#define CONTRACT_OPENING "A contract. It reads:"
#define CONTRACT_NAME "Contract"
#define MAX_STAT_VALUES 3 /* passive, active, required */

typedef struct {
    const char *text;
    unsigned int flag;
} FlagLine;

typedef struct {
    int seen_table;
    int in_table;
    int skip_compare;
} ParseState;

static const FlagLine applied_lines[] = {
    {"Has been Whetstoned.", ITEM_MOD_WHETSTONED},
    {"Has been Soulstoned.", ITEM_MOD_SOULSTONED},
    {"Has been Catalyzed.", ITEM_MOD_CATALYZED},
    {"Has been Enchanted.", ITEM_MOD_ENCHANTED},
    {"Has been Focused.", ITEM_MOD_FOCUSED},
    {"Has been Augmented.", ITEM_MOD_AUGMENTED},
    {"Has been made easier to use.", ITEM_MOD_EASIER},
    {"Has been Corrupted.", ITEM_MOD_CORRUPTED},
    {"Legacy item.", ITEM_MOD_LEGACY},
};

static const FlagLine possible_lines[] = {
    {"Can be Whetstoned.", ITEM_CAN_WHETSTONE},       {"Can be Soulstoned.", ITEM_CAN_SOULSTONE},
    {"Cannot be Soulstoned.", ITEM_CANNOT_SOULSTONE}, {"Can be Enchanted.", ITEM_CAN_ENCHANT},
    {"Cannot be Enchanted.", ITEM_CANNOT_ENCHANT},    {"May be further enhanced by Osiris.", ITEM_CAN_OSIRIS},
};

static const char *condition_lines[] = {
    "It's in perfect condition.",   "It's showing signs of age.", "It's fairly old.", "It is old.",
    "It is very old and battered.",
};

/* Matches `literal` at the cursor, ignoring spaces in front of either, and moves past it. */
static int expect(const char **cursor, const char *literal) {
    const char *input = skip_spaces(*cursor);
    const char *wanted = skip_spaces(literal);
    const size_t length = strlen(wanted);

    if (strncmp(input, wanted, length) != 0) return 0;
    *cursor = input + length;
    return 1;
}

/* Reads a value like "+1.5%" as tenths of a percent. */
static int read_percent(const char **cursor, int *tenths) {
    char *end = NULL;

    errno = 0;
    const float parsed = strtof(*cursor, &end);
    if (end == *cursor || errno == ERANGE || *end != '%') return 0;

    *tenths = (int) lroundf(parsed * TENTHS_PER_PERCENT);
    *cursor = end + 1;
    return 1;
}

/* Reads the text up to the closing bracket of "(<rank name>)". */
static int read_rank_name(const char **cursor, char *rank_name) {
    const char *close = strchr(*cursor, ')');
    if (close == NULL) return 0;

    const size_t length = (size_t) (close - *cursor);
    if (length == 0 || length >= ITEM_RANK_NAME_SIZE) return 0;

    memcpy(rank_name, *cursor, length);
    rank_name[length] = 0;
    *cursor = close + 1;
    return 1;
}

static int match_flag(const char *text, const FlagLine *table, const int count, unsigned int *flags) {
    for (int entry = 0; entry < count; entry++) {
        if (strcmp(text, table[entry].text) == 0) {
            *flags |= table[entry].flag;
            return 1;
        }
    }
    return 0;
}

/* One to three plain numbers: a single value, passive and active, or passive, active and required. */
static int parse_plain_values(const char *cursor, const int soulstone_table, ItemStat *stat) {
    int values[MAX_STAT_VALUES] = {0};
    int count = 0;

    while (count < MAX_STAT_VALUES && read_int(&cursor, &values[count])) count++;
    if (count == 0 || !is_blank(cursor)) return 0;

    stat->passive = values[0];
    stat->active = values[1];
    stat->required = values[2];
    if (count == MAX_STAT_VALUES) {
        stat->kind = ITEM_STAT_PLAIN;
        stat->has_required = 1;
    } else if (count == 2) {
        stat->kind = ITEM_STAT_PLAIN;
    } else {
        stat->kind = soulstone_table ? ITEM_STAT_SOULSTONE : ITEM_STAT_SINGLE;
    }
    return 1;
}

/* Parses everything after the name column of a stat row. */
static int parse_stat_values(const char *values, const int soulstone_table, ItemStat *stat) {
    const char *cursor = values;

    if (strstr(values, "seconds") != NULL) {
        stat->kind = ITEM_STAT_DURATION;
        return read_int(&cursor, &stat->passive) && expect(&cursor, "seconds") && is_blank(cursor);
    }
    if (strstr(values, "/s)") != NULL) {
        stat->kind = ITEM_STAT_REGEN;
        return read_int(&cursor, &stat->passive) && expect(&cursor, "(") && read_int(&cursor, &stat->active) &&
               expect(&cursor, "/s)") && is_blank(cursor);
    }
    if (strchr(values, '(') != NULL) {
        stat->kind = ITEM_STAT_RANK;
        return read_int(&cursor, &stat->passive) && expect(&cursor, "(") && read_rank_name(&cursor, stat->rank_name) &&
               is_blank(cursor);
    }
    if (strchr(values, '%') != NULL) {
        stat->kind = ITEM_STAT_PERCENT;
        return read_percent(&cursor, &stat->passive) && read_percent(&cursor, &stat->active) && is_blank(cursor);
    }
    return parse_plain_values(cursor, soulstone_table, stat);
}

/* Parses one row of the stat table; the name fills the first ITEM_STAT_NAME_WIDTH columns. */
static int parse_stat(const char *text, const int soulstone_table, ItemStat *stat) {
    char name[ITEM_STAT_NAME_WIDTH + 1];

    if (strlen(text) <= ITEM_STAT_NAME_WIDTH) return 0;

    memcpy(name, text, ITEM_STAT_NAME_WIDTH);
    name[ITEM_STAT_NAME_WIDTH] = 0;
    for (int column = ITEM_STAT_NAME_WIDTH - 1; column >= 0 && name[column] == ' '; column--) name[column] = 0;
    if (!name[0]) return 0;

    memset(stat, 0, sizeof(*stat));
    memcpy(stat->name, name, sizeof(name));
    return parse_stat_values(text + ITEM_STAT_NAME_WIDTH, soulstone_table, stat);
}

/* "Level 3. Next level in 120 kills." */
static int parse_level(const char *text, ItemDetails *details) {
    const char *cursor = text;
    int level = 0;
    int kills = 0;

    if (!expect(&cursor, "Level ") || !read_int(&cursor, &level) || !expect(&cursor, ". Next level in ") ||
        !read_int(&cursor, &kills) || !expect(&cursor, " kills.") || *cursor != 0) {
        return 0;
    }
    details->level = level;
    details->kills_to_level = kills;
    return 1;
}

/* "2 more blessings. Next in 40 kills." or "1 more blessing. ..." */
static int parse_blessings(const char *text, ItemDetails *details) {
    const char *cursor = text;
    int blessings = 0;
    int kills = 0;

    if (!read_int(&cursor, &blessings) || !expect(&cursor, " more blessing")) return 0;
    if (*cursor == 's') cursor++;
    if (!expect(&cursor, ". Next in ") || !read_int(&cursor, &kills) || !expect(&cursor, " kills.") || *cursor != 0) {
        return 0;
    }
    details->blessings = blessings;
    details->kills_to_blessing = kills;
    return 1;
}

/* "You estimate this could sell for 12G 34S." */
static int parse_appraisal(const char *text, ItemDetails *details) {
    const char *cursor = text;
    int gold = 0;
    int silver = 0;

    if (!expect(&cursor, "You estimate this could sell for ") || !read_int(&cursor, &gold) || !expect(&cursor, "G") ||
        !read_int(&cursor, &silver) || !expect(&cursor, "S.") || *cursor != 0) {
        return 0;
    }
    details->sell_silver = (gold * SILVER_PER_GOLD) + silver;
    return 1;
}

static int match_condition(const char *text, ItemDetails *details) {
    for (int condition = 0; condition < ARRAY_SIZE(condition_lines); condition++) {
        if (strcmp(text, condition_lines[condition]) == 0) {
            details->condition = condition;
            return 1;
        }
    }
    return 0;
}

static void reset_details(ItemDetails *details) {
    memset(details, 0, sizeof(*details));
    details->description_line = -1;
    details->condition = -1;
    details->level = -1;
    details->blessings = -1;
    details->sell_silver = -1;
}

int item_details_is_reply_start(const char *text, const int font) {
    const size_t text_length = strlen(text);

    if (strcmp(text, CONTRACT_OPENING) == 0) return 1;
    return font == CHAT_FONT_LIME && text_length >= 2 && text[text_length - 1] == ':';
}

/* Header "<name>:", or a contract's opening line, which names no item. */
static void parse_header(const TooltipBlock *block, ItemDetails *details) {
    const char *header = strcmp(block->lines[0].text, CONTRACT_OPENING) == 0 ? CONTRACT_NAME : block->lines[0].text;

    strncpy(details->name, header, TT_LINE_LEN - 1);
    const size_t name_length = strlen(details->name);
    if (name_length > 0 && details->name[name_length - 1] == ':') details->name[name_length - 1] = 0;

    if (block->count > 1) details->description_line = 1;
}

/* Comparison with the item on the cursor describes that item, not this one. */
static int skip_comparison(ParseState *state, const char *text) {
    if (starts_with(text, "You compare it with ")) {
        state->skip_compare = COMPARE_SECTION_LINES;
        return 1;
    }
    if (state->skip_compare > 0) {
        state->skip_compare--;
        return 1;
    }
    return 0;
}

/* The stat table header and its rows; returns 1 if the line belonged to the table. */
static int parse_table_line(ParseState *state, const TooltipLine *line, ItemDetails *details) {
    if (line->font == CHAT_FONT_ORANGE && starts_with(line->text, "Stat ")) {
        state->in_table = 1;
        state->seen_table = 1;
        details->soulstone_table = strstr(line->text, "Adds") != NULL;
        return 1;
    }
    if (!state->in_table) return 0;

    if (details->stat_count < ITEM_DETAILS_MAX_STATS &&
        parse_stat(line->text, details->soulstone_table, &details->stats[details->stat_count])) {
        details->stat_count++;
        return 1;
    }
    state->in_table = 0;
    return 0;
}

static int parse_known_line(const char *text, ItemDetails *details) {
    return match_flag(text, applied_lines, ARRAY_SIZE(applied_lines), &details->mods_applied) ||
           match_flag(text, possible_lines, ARRAY_SIZE(possible_lines), &details->mods_possible) ||
           match_condition(text, details) || parse_level(text, details) || parse_blessings(text, details) ||
           parse_appraisal(text, details);
}

/* Lines nothing recognised: effects before the stat table, anything else after it. */
static void keep_unrecognised(ItemDetails *details, const ParseState *state, const int line_index) {
    if (!state->seen_table) {
        if (details->effect_count < ITEM_DETAILS_MAX_REFS) {
            details->effect_lines[details->effect_count++] = (unsigned char) line_index;
        }
    } else if (details->other_count < ITEM_DETAILS_MAX_REFS) {
        details->other_lines[details->other_count++] = (unsigned char) line_index;
    }
}

void item_details_parse(const TooltipBlock *block, ItemDetails *details) {
    ParseState state = {0};

    reset_details(details);
    if (!block || block->count == 0) return;

    parse_header(block, details);

    for (int line_index = 2; line_index < block->count; line_index++) {
        const TooltipLine *line = &block->lines[line_index];

        if (skip_comparison(&state, line->text) || is_blank(line->text)) continue;
        if (parse_table_line(&state, line, details) || parse_known_line(line->text, details)) continue;
        keep_unrecognised(details, &state, line_index);
    }
}
