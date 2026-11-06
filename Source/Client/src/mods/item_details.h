#pragma once

/*
 * Structured view of an item's look text.
 *
 * Self-contained (no game headers) so it can be built into the unit tests.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define TT_MAX_LINES 32
#define TT_LINE_LEN 256

/* One chat line as the server sent it. */
typedef struct {
    char font;
    char text[TT_LINE_LEN];
} TooltipLine;

/* The full reply to one look request; line 0 is always the "<name>:" header. */
typedef struct {
    int count;
    TooltipLine lines[TT_MAX_LINES];
} TooltipBlock;

/* Server chat fonts, also used for tooltip colours. */
enum {
    CHAT_FONT_RED = 0,
    CHAT_FONT_YELLOW = 1,
    CHAT_FONT_GREEN = 2,
    CHAT_FONT_BLUE = 3,
    CHAT_FONT_PINK = 4,
    CHAT_FONT_ORANGE = 5,
    CHAT_FONT_LIME = 6,
    CHAT_FONT_SKYBLUE = 7,
    CHAT_FONT_VIOLET = 8,
    CHAT_FONT_WHITE = 9,
};

typedef enum {
    ITEM_STAT_PLAIN, /* passive, active, optional required */
    ITEM_STAT_PERCENT, /* passive, active in tenths of a percent */
    ITEM_STAT_SOULSTONE, /* passive only: what the soulstone adds */
    ITEM_STAT_REGEN, /* passive = total, active = per second */
    ITEM_STAT_DURATION, /* passive = seconds */
    ITEM_STAT_SINGLE, /* passive only (Complexity) */
    ITEM_STAT_RANK, /* passive = rank number, rank_name set */
} ItemStatKind;

#define ITEM_STAT_NAME_WIDTH 16 /* the stat table pads names to this many columns */
#define ITEM_RANK_NAME_SIZE 24

typedef struct {
    char name[ITEM_STAT_NAME_WIDTH + 1];
    unsigned char kind;
    unsigned char has_required;
    int passive;
    int active;
    int required;
    char rank_name[ITEM_RANK_NAME_SIZE];
} ItemStat;

/* Modifications already applied to the item. */
#define ITEM_MOD_WHETSTONED (1U << 0U)
#define ITEM_MOD_SOULSTONED (1U << 1U)
#define ITEM_MOD_CATALYZED (1U << 2U)
#define ITEM_MOD_ENCHANTED (1U << 3U)
#define ITEM_MOD_FOCUSED (1U << 4U)
#define ITEM_MOD_AUGMENTED (1U << 5U)
#define ITEM_MOD_EASIER (1U << 6U)
#define ITEM_MOD_CORRUPTED (1U << 7U)
#define ITEM_MOD_LEGACY (1U << 8U)

/* Modifications the item can or cannot still take. */
#define ITEM_CAN_WHETSTONE (1U << 0U)
#define ITEM_CAN_SOULSTONE (1U << 1U)
#define ITEM_CANNOT_SOULSTONE (1U << 2U)
#define ITEM_CAN_ENCHANT (1U << 3U)
#define ITEM_CANNOT_ENCHANT (1U << 4U)
#define ITEM_CAN_OSIRIS (1U << 5U)

#define ITEM_DETAILS_MAX_STATS 32
#define ITEM_DETAILS_MAX_REFS TT_MAX_LINES

typedef struct {
    char name[TT_LINE_LEN];
    int description_line; /* index into the block, -1 if none */

    int soulstone_table; /* stats use the "Stat / Adds" layout */
    int stat_count;
    ItemStat stats[ITEM_DETAILS_MAX_STATS];

    unsigned int mods_applied; /* ITEM_MOD_* */
    unsigned int mods_possible; /* ITEM_CAN_* / ITEM_CANNOT_* */

    int condition; /* 0 perfect .. 4 very old, -1 if not shown */

    int level; /* god weapons, -1 if not shown */
    int kills_to_level;
    int blessings; /* Osiris weapons, -1 if not shown */
    int kills_to_blessing;

    int sell_silver; /* appraisal, -1 if not shown */

    /* Text shown before the stats: special effects, enchantments, tarot text. */
    int effect_count;
    unsigned char effect_lines[ITEM_DETAILS_MAX_REFS];

    /* Lines nothing recognised; still shown so no information is lost. */
    int other_count;
    unsigned char other_lines[ITEM_DETAILS_MAX_REFS];
} ItemDetails;

/* Fill `details` from a captured look reply. */
void item_details_parse(const TooltipBlock *block, ItemDetails *details);

#ifdef __cplusplus
}
#endif
