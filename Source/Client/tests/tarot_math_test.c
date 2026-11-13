#include <stdio.h>
#include <string.h>

#include "planner/tarot_math.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static TarotSet card_bit(const int card_index) {
    return (TarotSet) 1 << (unsigned int) card_index;
}

static CombatValues combat_values(const int hit, const int parry, const int weapon, const int armor) {
    CombatValues values;
    values.hit = hit;
    values.parry = parry;
    values.weapon = weapon;
    values.armor = armor;
    return values;
}

static PlannerEffects no_effects(void) {
    PlannerEffects effects;

    memset(&effects, 0, sizeof(effects));
    return effects;
}

static void test_set_has(void) {
    const TarotSet cards = card_bit(TAROT_HERMIT) | card_bit(43);

    CHECK(tarot_set_has(cards, TAROT_HERMIT));
    CHECK(tarot_set_has(cards, 43));
    CHECK(!tarot_set_has(cards, TAROT_LOVERS));
    CHECK(!tarot_set_has(cards, -1));
    CHECK(!tarot_set_has(cards, TAROT_CARD_COUNT));
}

static void test_floor_divide(void) {
    CHECK(tarot_floor_divide(7, 5) == 1);
    CHECK(tarot_floor_divide(-1, 5) == -1);
    CHECK(tarot_floor_divide(-5, 5) == -1);
    CHECK(tarot_floor_divide(-6, 5) == -2);
}

static void test_fool_reversed_breakpoints(void) {
    CHECK(tarot_fool_reversed_total(105, 0) == 105);
    CHECK(tarot_fool_reversed_total(105, 4) == 105);
    CHECK(tarot_fool_reversed_total(105, 5) == 106);
    CHECK(tarot_fool_reversed_total(105, 20) == 109);
    CHECK(tarot_fool_reversed_total(124, 5) == 126);
}

static void test_fool_reversed_negative_change(void) {
    CHECK(tarot_fool_reversed_total(105, -1) == 103);
    CHECK(tarot_fool_reversed_total(105, -5) == 103);
    CHECK(tarot_fool_reversed_total(105, -6) == 102);
}

static void test_scaled_change(void) {
    CHECK(tarot_scaled_change(80, 10, 4, 5) == 8);
    CHECK(tarot_scaled_change(80, 0, 4, 5) == 0);
    CHECK(tarot_scaled_change(115, 20, 23, 20) == 23);
}

static void test_combat_no_cards_passes_changes_through(void) {
    const PlannerEffects effects = no_effects();
    const CombatValues shown =
            tarot_combat_shown(0, &effects, combat_values(100, 90, 60, 50), combat_values(3, -2, 5, 7));

    CHECK(shown.hit == 103);
    CHECK(shown.parry == 88);
    CHECK(shown.weapon == 65);
    CHECK(shown.armor == 57);
}

static void test_combat_lovers_averages_weapon_and_armor(void) {
    const PlannerEffects effects = no_effects();
    const CombatValues shown = tarot_combat_shown(card_bit(TAROT_LOVERS), &effects, combat_values(0, 0, 50, 50),
                                                  combat_values(0, 0, 4, 0));

    CHECK(shown.weapon == 52);
    CHECK(shown.armor == 52);
}

static void test_combat_lovers_reversed_averages_hit_and_parry(void) {
    const PlannerEffects effects = no_effects();
    const CombatValues shown = tarot_combat_shown(card_bit(TAROT_LOVERS_REVERSED), &effects,
                                                  combat_values(100, 100, 0, 0), combat_values(6, 0, 0, 0));

    CHECK(shown.hit == 103);
    CHECK(shown.parry == 103);
}

static void test_combat_strength_reversed(void) {
    const PlannerEffects effects = no_effects();
    const CombatValues shown = tarot_combat_shown(card_bit(TAROT_STRENGTH_REVERSED), &effects,
                                                  combat_values(100, 100, 60, 0), combat_values(10, 0, 10, 0));

    CHECK(shown.weapon == 72);
    CHECK(shown.hit == 108);
    CHECK(shown.parry == 100);
}

static void test_combat_hermit_armor(void) {
    const PlannerEffects effects = no_effects();
    const CombatValues shown = tarot_combat_shown(card_bit(TAROT_HERMIT), &effects, combat_values(0, 0, 0, 115),
                                                  combat_values(0, 0, 0, 20));

    CHECK(shown.armor == 138);
}

static void test_combat_temperance_reversed_uses_four_stacks(void) {
    const PlannerEffects effects = no_effects();
    const CombatValues shown = tarot_combat_shown(card_bit(TAROT_TEMPERANCE_REVERSED), &effects,
                                                  combat_values(0, 0, 140, 0), combat_values(0, 0, 10, 0));

    CHECK(TAROT_TEMPERANCE_SICKNESS_STACKS == 4);
    CHECK(shown.weapon == 154);
}

static void test_combat_lovers_odd_and_negative_changes_round_down(void) {
    const TarotSet lovers = card_bit(TAROT_LOVERS);
    const PlannerEffects effects = no_effects();
    const CombatValues server = combat_values(0, 0, 50, 50);

    CHECK(tarot_combat_shown(lovers, &effects, server, combat_values(0, 0, 3, 0)).weapon == 51);
    CHECK(tarot_combat_shown(lovers, &effects, server, combat_values(0, 0, -3, 0)).weapon == 48);
    CHECK(tarot_combat_shown(lovers, &effects, server, combat_values(0, 0, 0, 0)).weapon == 50);
}

static void test_hit_percent_after_lovers_reversed_before_strength_reversed(void) {
    PlannerEffects effects = no_effects();
    CombatValues shown;

    effects.hit_percent = 4;
    shown = tarot_combat_shown(card_bit(TAROT_LOVERS_REVERSED) | card_bit(TAROT_STRENGTH_REVERSED), &effects,
                               combat_values(100, 100, 0, 0), combat_values(10, 0, 0, 0));

    CHECK(shown.hit == 104);
    CHECK(shown.parry == 105);
}

static void test_weapon_armor_percent_last(void) {
    PlannerEffects effects = no_effects();
    CombatValues shown;

    effects.armor_percent = 10;
    shown = tarot_combat_shown(card_bit(TAROT_HERMIT), &effects, combat_values(0, 0, 0, 115),
                               combat_values(0, 0, 0, 20));

    CHECK(shown.armor == 140);
}

static void test_kwai_hit_to_parry(void) {
    PlannerEffects effects = no_effects();
    CombatValues shown;

    effects.hit_to_parry_percent = 3;
    shown = tarot_combat_shown(0, &effects, combat_values(200, 106, 0, 0), combat_values(100, 0, 0, 0));

    CHECK(shown.hit == 300 && shown.parry == 109);
}

static void test_kwai_parry_to_hit_uses_raised_parry(void) {
    PlannerEffects effects = no_effects();
    CombatValues shown;

    effects.hit_to_parry_percent = 3;
    effects.parry_to_hit_percent = 3;
    shown = tarot_combat_shown(0, &effects, combat_values(203, 106, 0, 0), combat_values(0, 100, 0, 0));

    CHECK(shown.hit == 206 && shown.parry == 206);
}

static void test_kwai_after_strength_reversed(void) {
    PlannerEffects effects = no_effects();
    CombatValues shown;

    effects.hit_to_parry_percent = 3;
    shown = tarot_combat_shown(card_bit(TAROT_STRENGTH_REVERSED), &effects, combat_values(80, 102, 0, 0),
                               combat_values(25, 0, 0, 0));

    CHECK(shown.hit == 100 && shown.parry == 103);
}

static void test_armor_enchant_after_hermit_before_percent(void) {
    PlannerEffects effects = no_effects();
    CombatValues shown;

    effects.armor_enchant = 1;
    effects.armor_percent = 10;
    shown = tarot_combat_shown(card_bit(TAROT_HERMIT), &effects, combat_values(0, 0, 0, 138),
                               combat_values(0, 0, 0, 20));

    /* Hermit makes the 20 into 23, the enchant makes that 25 and the percent makes it 28. */
    CHECK(shown.armor == 166);
}

static void test_kwai_and_armor_enchant_keep_server_values(void) {
    PlannerEffects effects = no_effects();
    CombatValues shown;

    effects.hit_to_parry_percent = 3;
    effects.parry_to_hit_percent = 3;
    effects.armor_enchant = 1;
    shown = tarot_combat_shown(card_bit(TAROT_HERMIT) | card_bit(TAROT_STRENGTH_REVERSED), &effects,
                               combat_values(203, 106, 50, 60), combat_values(0, 0, 0, 0));

    CHECK(shown.hit == 203 && shown.parry == 106 && shown.weapon == 50 && shown.armor == 60);
}

int main(void) {
    test_set_has();
    test_floor_divide();
    test_fool_reversed_breakpoints();
    test_fool_reversed_negative_change();
    test_scaled_change();
    test_combat_no_cards_passes_changes_through();
    test_combat_lovers_averages_weapon_and_armor();
    test_combat_lovers_reversed_averages_hit_and_parry();
    test_combat_strength_reversed();
    test_combat_hermit_armor();
    test_combat_temperance_reversed_uses_four_stacks();
    test_combat_lovers_odd_and_negative_changes_round_down();
    test_hit_percent_after_lovers_reversed_before_strength_reversed();
    test_weapon_armor_percent_last();
    test_kwai_hit_to_parry();
    test_kwai_parry_to_hit_uses_raised_parry();
    test_kwai_after_strength_reversed();
    test_armor_enchant_after_hermit_before_percent();
    test_kwai_and_armor_enchant_keep_server_values();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All tarot math tests passed\n");
    return 0;
}
