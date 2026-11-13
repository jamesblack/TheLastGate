/*
 * Unit tests for saving the planner's Weapon and Off-hand picks per character.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "planner/combat_model.h"
#include "planner/item_effects.h"
#include "planner/planner_choices.h"
#include "planner/tree_effects.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define TEST_PATH "planner_choices_test.dat"

static PlannerChoices make_choices(const int weapon, const int offhand) {
    PlannerChoices choices;
    choices.weapon = weapon;
    choices.offhand = offhand;
    choices.spell_toggles = 0;
    choices.tarot_cards = 0;
    choices.corruptor_ticks = 0;
    memset(choices.corruptor_counts, 0, sizeof(choices.corruptor_counts));
    choices.enchant_ticks = 0;
    memset(choices.enchant_counts, 0, sizeof(choices.enchant_counts));
    choices.gear_ticks = 0;
    return choices;
}

static void write_raw_file(const void *bytes, const size_t byte_count) {
    FILE *file = fopen(TEST_PATH, "wb");
    if (file == NULL) return;
    fwrite(bytes, 1, byte_count, file);
    fclose(file);
}

static void test_missing_file_has_no_choices(void) {
    PlannerChoices loaded;

    remove(TEST_PATH);
    loaded = planner_choices_load(TEST_PATH, 1234);
    CHECK(loaded.weapon == PLANNER_CHOICE_NONE);
    CHECK(loaded.offhand == PLANNER_CHOICE_NONE);
}

static void test_saved_choices_load_back(void) {
    PlannerChoices loaded;

    remove(TEST_PATH);
    CHECK(planner_choices_save(TEST_PATH, 1234, make_choices(COMBAT_WEAPON_AXE, COMBAT_OFFHAND_SHIELD)) == 1);
    loaded = planner_choices_load(TEST_PATH, 1234);
    CHECK(loaded.weapon == COMBAT_WEAPON_AXE);
    CHECK(loaded.offhand == COMBAT_OFFHAND_SHIELD);
}

static void test_characters_are_kept_apart(void) {
    PlannerChoices first;
    PlannerChoices second;

    remove(TEST_PATH);
    planner_choices_save(TEST_PATH, 11, make_choices(COMBAT_WEAPON_DAGGER, COMBAT_OFFHAND_DUAL));
    planner_choices_save(TEST_PATH, 22, make_choices(COMBAT_WEAPON_STAFF, COMBAT_OFFHAND_NONE));
    planner_choices_save(TEST_PATH, 11, make_choices(COMBAT_WEAPON_SWORD, COMBAT_OFFHAND_SHIELD));

    first = planner_choices_load(TEST_PATH, 11);
    second = planner_choices_load(TEST_PATH, 22);
    CHECK(first.weapon == COMBAT_WEAPON_SWORD);
    CHECK(first.offhand == COMBAT_OFFHAND_SHIELD);
    CHECK(second.weapon == COMBAT_WEAPON_STAFF);
    CHECK(second.offhand == COMBAT_OFFHAND_NONE);
    CHECK(planner_choices_load(TEST_PATH, 33).weapon == PLANNER_CHOICE_NONE);
}

static void test_out_of_range_values_are_ignored(void) {
    const uint32_t file_words[] = {1, 1, 1234, (uint32_t) COMBAT_WEAPON_COUNT, (uint32_t) -5};
    PlannerChoices loaded;

    write_raw_file(file_words, sizeof(file_words));
    loaded = planner_choices_load(TEST_PATH, 1234);
    CHECK(loaded.weapon == PLANNER_CHOICE_NONE);
    CHECK(loaded.offhand == PLANNER_CHOICE_NONE);
}

static void test_damaged_file_is_replaced(void) {
    const char garbage[] = "not a planner file";
    PlannerChoices loaded;

    write_raw_file(garbage, sizeof(garbage));
    CHECK(planner_choices_load(TEST_PATH, 1234).weapon == PLANNER_CHOICE_NONE);
    CHECK(planner_choices_save(TEST_PATH, 1234, make_choices(COMBAT_WEAPON_SPEAR, COMBAT_OFFHAND_NONE)) == 1);
    loaded = planner_choices_load(TEST_PATH, 1234);
    CHECK(loaded.weapon == COMBAT_WEAPON_SPEAR);
    CHECK(loaded.offhand == COMBAT_OFFHAND_NONE);
}

static void test_uncreated_character_is_not_saved(void) {
    remove(TEST_PATH);
    CHECK(planner_choices_save(TEST_PATH, 0, make_choices(COMBAT_WEAPON_AXE, COMBAT_OFFHAND_NONE)) == 0);
    CHECK(planner_choices_load(TEST_PATH, 0).weapon == PLANNER_CHOICE_NONE);
}

static void test_spells_and_cards_round_trip(void) {
    PlannerChoices saved = make_choices(COMBAT_WEAPON_SWORD, COMBAT_OFFHAND_SHIELD);
    PlannerChoices loaded;

    saved.spell_toggles = 0x5;
    saved.tarot_cards = ((TarotSet) 1 << 22U) | ((TarotSet) 1 << 43U);
    remove(TEST_PATH);
    CHECK(planner_choices_save(TEST_PATH, 7, saved) == 1);
    loaded = planner_choices_load(TEST_PATH, 7);
    CHECK(loaded.weapon == COMBAT_WEAPON_SWORD);
    CHECK(loaded.offhand == COMBAT_OFFHAND_SHIELD);
    CHECK(loaded.spell_toggles == 0x5);
    CHECK(loaded.tarot_cards == saved.tarot_cards);
}

static void write_format_one_file(void) {
    const uint32_t file_words[] = {1, 1, 7, (uint32_t) COMBAT_WEAPON_SWORD, (uint32_t) COMBAT_OFFHAND_SHIELD};

    write_raw_file(file_words, sizeof(file_words));
}

static void test_format_one_file_keeps_weapon_and_offhand(void) {
    PlannerChoices loaded;

    write_format_one_file();
    loaded = planner_choices_load(TEST_PATH, 7);
    CHECK(loaded.weapon == COMBAT_WEAPON_SWORD);
    CHECK(loaded.offhand == COMBAT_OFFHAND_SHIELD);
    CHECK(loaded.spell_toggles == 0);
    CHECK(loaded.tarot_cards == 0);
}

static void test_saving_over_format_one_upgrades(void) {
    PlannerChoices loaded;

    write_format_one_file();
    CHECK(planner_choices_save(TEST_PATH, 9, make_choices(COMBAT_WEAPON_AXE, COMBAT_OFFHAND_NONE)) == 1);
    loaded = planner_choices_load(TEST_PATH, 7);
    CHECK(loaded.weapon == COMBAT_WEAPON_SWORD);
    CHECK(loaded.offhand == COMBAT_OFFHAND_SHIELD);
    CHECK(planner_choices_load(TEST_PATH, 9).weapon == COMBAT_WEAPON_AXE);
}

static void test_corruptors_round_trip(void) {
    PlannerChoices saved = make_choices(COMBAT_WEAPON_SWORD, COMBAT_OFFHAND_SHIELD);
    PlannerChoices loaded;

    saved.corruptor_ticks = 0x5;
    saved.corruptor_counts[0] = 2;
    saved.corruptor_counts[1] = 4;
    saved.corruptor_counts[2] = 7;
    saved.corruptor_counts[CORRUPTOR_COUNT - 1] = 20;
    remove(TEST_PATH);
    CHECK(planner_choices_save(TEST_PATH, 7, saved) == 1);
    loaded = planner_choices_load(TEST_PATH, 7);
    CHECK(loaded.corruptor_ticks == 0x5);
    CHECK(loaded.corruptor_counts[0] == 2);
    CHECK(loaded.corruptor_counts[1] == 4);
    CHECK(loaded.corruptor_counts[2] == 7);
    CHECK(loaded.corruptor_counts[3] == 0);
    CHECK(loaded.corruptor_counts[CORRUPTOR_COUNT - 1] == 20);
}

static void test_format_two_loads_without_corruptors(void) {
    const uint32_t file_words[] = {
        2, 1, 7, (uint32_t) COMBAT_WEAPON_SWORD, (uint32_t) COMBAT_OFFHAND_SHIELD, 0x5, 0x400000, 0x800};
    PlannerChoices loaded;
    int corruptor;

    write_raw_file(file_words, sizeof(file_words));
    loaded = planner_choices_load(TEST_PATH, 7);
    CHECK(loaded.weapon == COMBAT_WEAPON_SWORD);
    CHECK(loaded.offhand == COMBAT_OFFHAND_SHIELD);
    CHECK(loaded.spell_toggles == 0x5);
    CHECK(loaded.tarot_cards == (((TarotSet) 0x800 << 32U) | 0x400000U));
    CHECK(loaded.corruptor_ticks == 0);
    for (corruptor = 0; corruptor < CORRUPTOR_COUNT; corruptor++) {
        CHECK(loaded.corruptor_counts[corruptor] == 0);
    }
}

static void test_counts_out_of_range_are_clamped(void) {
    PlannerChoices saved = make_choices(COMBAT_WEAPON_SWORD, COMBAT_OFFHAND_SHIELD);
    PlannerChoices loaded;

    saved.corruptor_ticks = 0x7;
    saved.corruptor_counts[0] = 0;
    saved.corruptor_counts[1] = 21;
    saved.corruptor_counts[2] = 5;
    saved.corruptor_counts[3] = 30;
    remove(TEST_PATH);
    CHECK(planner_choices_save(TEST_PATH, 7, saved) == 1);
    loaded = planner_choices_load(TEST_PATH, 7);
    CHECK(loaded.corruptor_counts[0] == 1);
    CHECK(loaded.corruptor_counts[1] == 20);
    CHECK(loaded.corruptor_counts[2] == 5);
    CHECK(loaded.corruptor_counts[3] == 20);
}

static void test_items_round_trip(void) {
    PlannerChoices saved = make_choices(COMBAT_WEAPON_SWORD, COMBAT_OFFHAND_SHIELD);
    PlannerChoices loaded;

    saved.enchant_ticks = 0x41;
    saved.enchant_counts[0] = 2;
    saved.enchant_counts[6] = 5;
    saved.gear_ticks = 0x100001;
    remove(TEST_PATH);
    CHECK(planner_choices_save(TEST_PATH, 7, saved) == 1);
    loaded = planner_choices_load(TEST_PATH, 7);
    CHECK(loaded.enchant_ticks == 0x41);
    CHECK(loaded.enchant_counts[0] == 2);
    CHECK(loaded.enchant_counts[6] == 5);
    CHECK(loaded.enchant_counts[1] == 0);
    CHECK(loaded.gear_ticks == 0x100001);
}

static void test_format_three_loads_without_items(void) {
    const uint32_t file_words[] = {3,
                                   1,
                                   7,
                                   (uint32_t) COMBAT_WEAPON_SWORD,
                                   (uint32_t) COMBAT_OFFHAND_SHIELD,
                                   0x5,
                                   0x400000,
                                   0x800,
                                   0x3,
                                   0x0402,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0,
                                   0};
    PlannerChoices loaded;
    int enchant;

    write_raw_file(file_words, sizeof(file_words));
    loaded = planner_choices_load(TEST_PATH, 7);
    CHECK(loaded.weapon == COMBAT_WEAPON_SWORD);
    CHECK(loaded.spell_toggles == 0x5);
    CHECK(loaded.corruptor_ticks == 0x3);
    CHECK(loaded.corruptor_counts[0] == 2);
    CHECK(loaded.corruptor_counts[1] == 4);
    CHECK(loaded.enchant_ticks == 0);
    CHECK(loaded.gear_ticks == 0);
    for (enchant = 0; enchant < ENCHANT_COUNT; enchant++) {
        CHECK(loaded.enchant_counts[enchant] == 0);
    }
}

static void test_enchant_counts_out_of_range_are_clamped(void) {
    PlannerChoices saved = make_choices(COMBAT_WEAPON_SWORD, COMBAT_OFFHAND_SHIELD);
    PlannerChoices loaded;

    saved.enchant_ticks = 0x7;
    saved.enchant_counts[0] = 0;
    saved.enchant_counts[1] = 21;
    saved.enchant_counts[2] = 5;
    saved.enchant_counts[3] = 30;
    remove(TEST_PATH);
    CHECK(planner_choices_save(TEST_PATH, 7, saved) == 1);
    loaded = planner_choices_load(TEST_PATH, 7);
    CHECK(loaded.enchant_counts[0] == 1);
    CHECK(loaded.enchant_counts[1] == 20);
    CHECK(loaded.enchant_counts[2] == 5);
    CHECK(loaded.enchant_counts[3] == 20);
}

int main(void) {
    test_missing_file_has_no_choices();
    test_saved_choices_load_back();
    test_characters_are_kept_apart();
    test_out_of_range_values_are_ignored();
    test_damaged_file_is_replaced();
    test_uncreated_character_is_not_saved();
    test_spells_and_cards_round_trip();
    test_format_one_file_keeps_weapon_and_offhand();
    test_saving_over_format_one_upgrades();
    test_corruptors_round_trip();
    test_format_two_loads_without_corruptors();
    test_counts_out_of_range_are_clamped();
    test_items_round_trip();
    test_format_three_loads_without_items();
    test_enchant_counts_out_of_range_are_clamped();
    remove(TEST_PATH);

    if (failures == 0) printf("planner_choices: all tests passed\n");
    return failures == 0 ? 0 : 1;
}
