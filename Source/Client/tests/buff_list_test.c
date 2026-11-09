/*
 * Unit tests for reading the buffs reply and deciding when to fetch it.
 */

#include <stdio.h>

#include "mods/buff_list.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static BuffListParser parser;

static void start_reply(void) {
    buff_list_parser_start(&parser);
    buff_list_parser_feed(&parser, " ");
}

static void test_header_recognised(void) {
    CHECK(buff_list_is_header("Your active buffs and debuffs:") == 1);
    CHECK(buff_list_is_header("Your companion's active buffs and debuffs:") == 0);
}

static void test_bless_power_line(void) {
    start_reply();
    buff_list_parser_feed(&parser, "Bless power of 142 for 4m 10s");
    CHECK(parser.readings[BUFF_KIND_BLESS].known == 1);
    CHECK(parser.readings[BUFF_KIND_BLESS].power == 142);
    CHECK(parser.readings[BUFF_KIND_BLESS].has_exact == 0);
    CHECK(parser.readings[BUFF_KIND_ENHANCE].known == 0);
}

static void test_exact_attribute_lines(void) {
    const BuffReading *bless = &parser.readings[BUFF_KIND_BLESS];

    start_reply();
    buff_list_parser_feed(&parser, "Bless power of 142 for 4m 10s");
    buff_list_parser_feed(&parser, " : +31 Braveness");
    buff_list_parser_feed(&parser, " : +30 Willpower");
    buff_list_parser_feed(&parser, " : +30 Intuition");
    buff_list_parser_feed(&parser, " : +30 Agility");
    buff_list_parser_feed(&parser, " : +29 Strength");
    CHECK(bless->has_exact == 1);
    CHECK(bless->attribute_bonus[0] == 31);
    CHECK(bless->attribute_bonus[1] == 30);
    CHECK(bless->attribute_bonus[2] == 30);
    CHECK(bless->attribute_bonus[3] == 30);
    CHECK(bless->attribute_bonus[4] == 29);
}

static void test_enhance_and_protect_lines(void) {
    start_reply();
    buff_list_parser_feed(&parser, "Enhance power of 80 for 9m 0s");
    buff_list_parser_feed(&parser, " : +24 Weapon Value");
    buff_list_parser_feed(&parser, "Protect power of 60 for 9m 0s");
    buff_list_parser_feed(&parser, " : +19 Armor Value");
    CHECK(parser.readings[BUFF_KIND_ENHANCE].power == 80);
    CHECK(parser.readings[BUFF_KIND_ENHANCE].weapon_bonus == 24);
    CHECK(parser.readings[BUFF_KIND_PROTECT].power == 60);
    CHECK(parser.readings[BUFF_KIND_PROTECT].armor_bonus == 19);
    CHECK(parser.readings[BUFF_KIND_ENHANCE].armor_bonus == 0);
}

static void test_other_buff_lines_ignored(void) {
    const BuffReading *bless = &parser.readings[BUFF_KIND_BLESS];

    start_reply();
    buff_list_parser_feed(&parser, "Bless power of 50 for 1m 0s");
    buff_list_parser_feed(&parser, "Curse power of 70 for 1m 0s");
    buff_list_parser_feed(&parser, " : -17 Braveness");
    buff_list_parser_feed(&parser, "Magic Shield power of 50 / 12");
    CHECK(bless->power == 50);
    CHECK(bless->has_exact == 0);
    CHECK(bless->attribute_bonus[0] == 0);
}

static void test_none_reply(void) {
    int kind;

    buff_list_parser_start(&parser);
    CHECK(buff_list_parser_feed(&parser, " ") == 1);
    CHECK(buff_list_parser_feed(&parser, "None.") == 1);
    CHECK(buff_list_parser_feed(&parser, " ") == 0);
    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) CHECK(parser.readings[kind].known == 0);
}

static void test_reply_ends_at_second_blank(void) {
    buff_list_parser_start(&parser);
    CHECK(buff_list_parser_feed(&parser, " ") == 1);
    CHECK(buff_list_parser_feed(&parser, "Bless power of 9 for 0m 5s") == 1);
    CHECK(buff_list_parser_feed(&parser, " ") == 0);
}

static void test_fetch_rule(void) {
    CHECK(buff_list_slot_needs_fetch(0, 0, 88, 16) == 1);
    CHECK(buff_list_slot_needs_fetch(88, 16, 0, 0) == 1);
    CHECK(buff_list_slot_needs_fetch(88, 10, 88, 16) == 1);
    CHECK(buff_list_slot_needs_fetch(88, 16, 88, 16) == 1);
    CHECK(buff_list_slot_needs_fetch(88, 10, 87, 16) == 1);
    CHECK(buff_list_slot_needs_fetch(88, 16, 88, 15) == 0);
    CHECK(buff_list_slot_needs_fetch(500, 4, 500, 4) == 0);
    CHECK(buff_list_slot_needs_fetch(0, 0, 500, 16) == 0);
    CHECK(buff_list_slot_needs_fetch(0, 0, 0, 0) == 0);
}

int main(void) {
    test_header_recognised();
    test_bless_power_line();
    test_exact_attribute_lines();
    test_enhance_and_protect_lines();
    test_other_buff_lines_ignored();
    test_none_reply();
    test_reply_ends_at_second_blank();
    test_fetch_rule();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All buff_list tests passed\n");
    return 0;
}
