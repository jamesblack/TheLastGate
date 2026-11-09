#pragma once

/*
 * Reading the server's reply to the "buffs" command, and deciding which buff
 * icon changes are worth asking for it again.
 *
 * The reply is a header line, a blank line, one "<Name> power of N ..." line
 * per buff (characters with spell knowledge also get " : +N <stat>" lines
 * under each), and a closing blank line. Only Bless, Enhance and Protect are
 * kept. Self-contained (no game headers) so it can be built into the unit tests.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define BUFF_ATTRIBUTE_COUNT 5
#define BUFF_LIST_HEADER "Your active buffs and debuffs:"
#define BUFF_SPRITE_PROTECT 86
#define BUFF_SPRITE_ENHANCE 87
#define BUFF_SPRITE_BLESS 88

typedef enum {
    BUFF_KIND_NONE = -1,
    BUFF_KIND_BLESS = 0,
    BUFF_KIND_ENHANCE = 1,
    BUFF_KIND_PROTECT = 2,
    BUFF_KIND_COUNT = 3
} BuffKind;

typedef struct {
    int known; /* a power line for this buff was read */
    int power;
    int has_exact; /* bonus lines followed the power line */
    int attribute_bonus[BUFF_ATTRIBUTE_COUNT]; /* Brv, Wil, Int, Agi, Str */
    int weapon_bonus;
    int armor_bonus;
} BuffReading;

typedef struct {
    BuffReading readings[BUFF_KIND_COUNT];
    int current_kind; /* buff the next bonus line belongs to; BUFF_KIND_NONE for other buffs */
    int blank_lines; /* blank lines seen since the header */
} BuffListParser;

BuffKind buff_list_sprite_kind(int sprite);

int buff_list_is_header(const char *text);

void buff_list_parser_start(BuffListParser *parser);

/* Feeds one line that came after the header. Returns 1 while the reply continues and 0 once the closing blank line is
 * consumed. */
int buff_list_parser_feed(BuffListParser *parser, const char *text);

/* 1 when a buff slot packet should trigger a fetch: one of the three buffs appeared, went, was refreshed or recast. */
int buff_list_slot_needs_fetch(int old_sprite, int old_active, int new_sprite, int new_active);

#ifdef __cplusplus
}
#endif
