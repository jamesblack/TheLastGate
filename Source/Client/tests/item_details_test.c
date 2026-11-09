/*
 * Unit tests for item_details_parse().
 *
 * Lines are produced with the same printf formats the server uses for its
 * stat table, so the tests check the parser against the real wire text.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "mods/item_details.h"

#define ITEM_INFO_MSG0 "%-16.16s    %+2d"
#define ITEM_INFO_MSG1 "%-16.16s   %+4d     %+4d     %3d"
#define ITEM_INFO_MSG2 "%-16.16s   %+4d     %+4d"
#define ITEM_INFO_MSG3 "%-16.16s   %+4d%%    %+4d%%"
#define ITEM_INFO_MSG4 "%-16.16s  %+2d.%1d%%   %+2d.%1d%%"
#define ITEM_INFO_MSG5 "%-16.16s  %+4d (%+4d/s)"
#define ITEM_INFO_MSG6 "%-16.16s   %4d seconds"
#define ITEM_INFO_MSG7 "%-16.16s   %4d"
#define ITEM_INFO_MSG8 "%-16.16s   %4d (%s)"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void add(TooltipBlock *block, const int font, const char *format, ...) {
    va_list args;
    TooltipLine *line = &block->lines[block->count++];

    line->font = (char) font;
    va_start(args, format);
    vsnprintf(line->text, sizeof(line->text), format, args);
    va_end(args);
}

static void test_weapon(void) {
    TooltipBlock block = {0};
    ItemDetails details;

    add(&block, CHAT_FONT_LIME, "Steel Sword:");
    add(&block, CHAT_FONT_YELLOW, "A finely crafted steel sword.");
    add(&block, CHAT_FONT_ORANGE, "Stat              Passiv   Active   Requi");
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG1, "Strength", 2, 4, 30);
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG1, "Sword", 0, 0, 25);
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG2, "Weapon Value", 24, 24);
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG3, "Crit Bonus", 5, 5);
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG4, "Dmg Bonus", 1, 5, 2, 5);
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG8, "Min. Rank", 5, "Sergeant");
    add(&block, CHAT_FONT_ORANGE, "Can be Whetstoned.");
    add(&block, CHAT_FONT_RED, "Cannot be Soulstoned.");
    add(&block, CHAT_FONT_VIOLET, "Has been Enchanted.");
    add(&block, CHAT_FONT_YELLOW, "It's fairly old.");
    add(&block, CHAT_FONT_LIME, "You estimate this could sell for 12G 34S.");

    item_details_parse(&block, &details);

    CHECK(strcmp(details.name, "Steel Sword") == 0);
    CHECK(details.description_line == 1);
    CHECK(!details.soulstone_table);
    CHECK(details.stat_count == 6);

    CHECK(strcmp(details.stats[0].name, "Strength") == 0);
    CHECK(details.stats[0].kind == ITEM_STAT_PLAIN);
    CHECK(details.stats[0].has_required);
    CHECK(details.stats[0].passive == 2 && details.stats[0].active == 4 && details.stats[0].required == 30);

    CHECK(details.stats[2].kind == ITEM_STAT_PLAIN && !details.stats[2].has_required);
    CHECK(details.stats[2].passive == 24 && details.stats[2].active == 24);

    CHECK(details.stats[3].kind == ITEM_STAT_PERCENT);
    CHECK(details.stats[3].passive == 50 && details.stats[3].active == 50);

    CHECK(details.stats[4].kind == ITEM_STAT_PERCENT);
    CHECK(details.stats[4].passive == 15 && details.stats[4].active == 25);

    CHECK(details.stats[5].kind == ITEM_STAT_RANK);
    CHECK(details.stats[5].passive == 5 && strcmp(details.stats[5].rank_name, "Sergeant") == 0);

    CHECK(details.mods_possible == (ITEM_CAN_WHETSTONE | ITEM_CANNOT_SOULSTONE));
    CHECK(details.mods_applied == ITEM_MOD_ENCHANTED);
    CHECK(details.condition == 2);
    CHECK(details.sell_silver == 1234);
    CHECK(details.effect_count == 0);
    CHECK(details.other_count == 0);
}

static void test_soulstone(void) {
    TooltipBlock block = {0};
    ItemDetails details;

    add(&block, CHAT_FONT_LIME, "Soulstone:");
    add(&block, CHAT_FONT_YELLOW, "A glowing stone.");
    add(&block, CHAT_FONT_ORANGE, "Stat               Adds");
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG0, "Dagger", 3);
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG2, "Armor Value", 2, 2);

    item_details_parse(&block, &details);

    CHECK(details.soulstone_table);
    CHECK(details.stat_count == 2);
    CHECK(details.stats[0].kind == ITEM_STAT_SOULSTONE && details.stats[0].passive == 3);
    CHECK(details.stats[1].kind == ITEM_STAT_PLAIN && details.stats[1].passive == 2);
}

static void test_potion(void) {
    TooltipBlock block = {0};
    ItemDetails details;

    add(&block, CHAT_FONT_LIME, "Healing Potion:");
    add(&block, CHAT_FONT_YELLOW, "Restores health over time.");
    add(&block, CHAT_FONT_ORANGE, "Stat              Passiv   Active   Requi");
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG5, "HP Regen", 120, 4);
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG6, "Duration", 30);
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG7, "Complexity", 12);

    item_details_parse(&block, &details);

    CHECK(details.stat_count == 3);
    CHECK(details.stats[0].kind == ITEM_STAT_REGEN && details.stats[0].passive == 120 && details.stats[0].active == 4);
    CHECK(details.stats[1].kind == ITEM_STAT_DURATION && details.stats[1].passive == 30);
    CHECK(details.stats[2].kind == ITEM_STAT_SINGLE && details.stats[2].passive == 12);
}

static void test_effects_and_unrecognised(void) {
    TooltipBlock block = {0};
    ItemDetails details;

    add(&block, CHAT_FONT_LIME, "Ring of Warmth:");
    add(&block, CHAT_FONT_YELLOW, "A warm ring.");
    add(&block, CHAT_FONT_VIOLET, "20%% more maximum Endurance.");
    add(&block, CHAT_FONT_ORANGE, "Stat              Passiv   Active   Requi");
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG2, "Armor Value", 1, 1);
    add(&block, CHAT_FONT_GREEN, "Level 3. Next level in 120 kills.");
    add(&block, CHAT_FONT_PINK, "Something the parser has never seen.");

    item_details_parse(&block, &details);

    CHECK(details.effect_count == 1 && details.effect_lines[0] == 2);
    CHECK(details.stat_count == 1);
    CHECK(details.level == 3 && details.kills_to_level == 120);
    CHECK(details.other_count == 1 && details.other_lines[0] == 6);
}

static void test_compare_section_dropped(void) {
    TooltipBlock block = {0};
    ItemDetails details;

    add(&block, CHAT_FONT_LIME, "Steel Sword:");
    add(&block, CHAT_FONT_YELLOW, "A sword.");
    add(&block, CHAT_FONT_YELLOW, " ");
    add(&block, CHAT_FONT_YELLOW, "You compare it with a Bronze Sword:");
    add(&block, CHAT_FONT_YELLOW, "A Steel Sword is the better weapon.");
    add(&block, CHAT_FONT_YELLOW, "No difference as armor.");
    add(&block, CHAT_FONT_ORANGE, "Stat              Passiv   Active   Requi");
    add(&block, CHAT_FONT_YELLOW, ITEM_INFO_MSG2, "Weapon Value", 24, 24);

    item_details_parse(&block, &details);

    CHECK(details.effect_count == 0);
    CHECK(details.other_count == 0);
    CHECK(details.stat_count == 1);
}

static void test_header_only(void) {
    TooltipBlock block = {0};
    ItemDetails details;

    add(&block, CHAT_FONT_LIME, "Talisman:");
    item_details_parse(&block, &details);

    CHECK(strcmp(details.name, "Talisman") == 0);
    CHECK(details.description_line == -1);
    CHECK(details.condition == -1 && details.level == -1 && details.sell_silver == -1);
}

static void test_reply_start(void) {
    CHECK(item_details_is_reply_start("Steel Sword:", CHAT_FONT_LIME));
    CHECK(item_details_is_reply_start("A contract. It reads:", CHAT_FONT_YELLOW));
    CHECK(!item_details_is_reply_start("A finely crafted sword:", CHAT_FONT_YELLOW));
    CHECK(!item_details_is_reply_start("Steel Sword", CHAT_FONT_LIME));
    CHECK(!item_details_is_reply_start(":", CHAT_FONT_LIME));
}

static void test_contract(void) {
    TooltipBlock block = {0};
    ItemDetails details;

    add(&block, CHAT_FONT_YELLOW, "A contract. It reads:");
    add(&block, CHAT_FONT_YELLOW, " ");
    add(&block, CHAT_FONT_GREEN, "Clear the area of all enemies.");
    add(&block, CHAT_FONT_YELLOW, " ");
    add(&block, CHAT_FONT_GREEN, "  + Enemies have more gold");
    add(&block, CHAT_FONT_GREEN, "A contract for a mercenary.");

    item_details_parse(&block, &details);

    CHECK(strcmp(details.name, "Contract") == 0);
    CHECK(details.stat_count == 0);
    CHECK(details.effect_count == 3 && details.effect_lines[0] == 2 && details.effect_lines[2] == 5);
}

int main(void) {
    test_weapon();
    test_soulstone();
    test_potion();
    test_effects_and_unrecognised();
    test_compare_section_dropped();
    test_header_only();
    test_reply_start();
    test_contract();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All item_details tests passed\n");
    return 0;
}
