/*
 * Unit tests for the planner's buff model: double-cast Bless, Enhance and
 * Protect, and taking buffs already on the player out of the totals.
 */

#include <stdio.h>
#include <string.h>

#include "planner/buff_model.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void set_skill(BuffModelSkill *skill, const int server_total, const int first_id, const int second_id,
                      const int third_id) {
    skill->server_total = server_total;
    skill->planned = 0;
    skill->attribute_ids[0] = first_id;
    skill->attribute_ids[1] = second_id;
    skill->attribute_ids[2] = third_id;
    skill->scale_kind = BUFF_SKILL_SCALE_NONE;
}

static void set_all_attributes(int attributes[ATTRIBUTE_COUNT], const int value) {
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) attributes[attribute_id] = value;
}

static BuffModelInput fixture(void) {
    BuffModelInput input;

    memset(&input, 0, sizeof(input));
    set_all_attributes(input.server_attributes, 50);
    set_all_attributes(input.base_attributes, 40);
    input.divisor = 5;
    input.spell_multiplier = 100;
    input.spell_bonus = 0;
    input.spell_aptitude = 200;
    input.race_modifier = 100;
    input.server_weapon = 30;
    input.server_armor = 30;
    input.spell_known[BUFF_KIND_BLESS] = 1;
    set_skill(&input.spell_skills[BUFF_KIND_BLESS], 60, 0, 1, 1);
    set_skill(&input.spell_skills[BUFF_KIND_ENHANCE], 0, -1, -1, -1);
    set_skill(&input.spell_skills[BUFF_KIND_PROTECT], 0, -1, -1, -1);
    input.show_buffed[BUFF_KIND_BLESS] = 1;
    return input;
}

static int attributes_equal(const BuffModelResult *result, const int brv, const int wil, const int intu, const int agi,
                            const int str) {
    return result->attributes[0] == brv && result->attributes[1] == wil && result->attributes[2] == intu &&
           result->attributes[3] == agi && result->attributes[4] == str;
}

static void test_bless_bonus_formula(void) {
    CHECK(buff_model_bless_bonus(60, 0) == 11);
    CHECK(buff_model_bless_bonus(60, 1) == 10);
    CHECK(buff_model_bless_bonus(0, 0) == 0);
    CHECK(buff_model_item_bonus(50, 0) == 16);
    CHECK(buff_model_item_bonus(50, 1) == 11);
    CHECK(buff_model_item_bonus(1000, 0) == 127);
}

static void test_bless_double_cast(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    buff_model_compute(&input, &result);
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 66);
    CHECK(result.effective_power[BUFF_KIND_BLESS] == 66);
    CHECK(attributes_equal(&result, 61, 61, 61, 61, 61));
}

static void test_bless_off_shows_unbuffed(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    input.show_buffed[BUFF_KIND_BLESS] = 0;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 50, 50, 50, 50, 50));
    CHECK(result.effective_power[BUFF_KIND_BLESS] == 0);
}

static void test_aptitude_caps_power(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    input.spell_aptitude = 20;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 58, 58, 57, 57, 57));
}

static void test_multiplier_and_bonus(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    input.spell_multiplier = 150;
    input.spell_bonus = 10;
    buff_model_compute(&input, &result);
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 118);
    CHECK(attributes_equal(&result, 68, 68, 68, 68, 67));
}

static void test_planned_willpower_feeds_bless(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    input.planned_attributes[1] = 5;
    buff_model_compute(&input, &result);
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 68);
    CHECK(attributes_equal(&result, 62, 66, 61, 61, 61));
}

static void test_planned_intuition_raises_aptitude(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    input.spell_aptitude = 20;
    input.planned_attributes[2] = 10;
    buff_model_compute(&input, &result);
    CHECK(result.spell_aptitude == 30);

    input.race_modifier = 115;
    buff_model_compute(&input, &result);
    CHECK(result.spell_aptitude == 31);
}

static void test_active_bless_taken_out(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    set_all_attributes(input.server_attributes, 61);
    input.spell_skills[BUFF_KIND_BLESS].server_total = 66;
    input.active[BUFF_KIND_BLESS].known = 1;
    input.active[BUFF_KIND_BLESS].power = 66;

    input.show_buffed[BUFF_KIND_BLESS] = 0;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 50, 50, 50, 50, 50));

    input.show_buffed[BUFF_KIND_BLESS] = 1;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 61, 61, 61, 61, 61));
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 66);
}

static void test_stronger_active_bless_kept(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    input.server_attributes[0] = 66;
    input.server_attributes[1] = 66;
    input.server_attributes[2] = 65;
    input.server_attributes[3] = 65;
    input.server_attributes[4] = 65;
    input.spell_skills[BUFF_KIND_BLESS].server_total = 69;
    input.active[BUFF_KIND_BLESS].known = 1;
    input.active[BUFF_KIND_BLESS].power = 100;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 66, 66, 65, 65, 65));
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 66);
    CHECK(result.effective_power[BUFF_KIND_BLESS] == 100);
}

static void test_exact_bonus_preferred(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;
    BuffReading *active = &input.active[BUFF_KIND_BLESS];

    input.server_attributes[0] = 62;
    input.server_attributes[1] = 61;
    input.server_attributes[2] = 61;
    input.server_attributes[3] = 61;
    input.server_attributes[4] = 61;
    active->known = 1;
    active->power = 66;
    active->has_exact = 1;
    active->attribute_bonus[0] = 12;
    active->attribute_bonus[1] = 11;
    active->attribute_bonus[2] = 11;
    active->attribute_bonus[3] = 11;
    active->attribute_bonus[4] = 11;
    input.show_buffed[BUFF_KIND_BLESS] = 0;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 50, 50, 50, 50, 50));
}

static BuffModelInput enhance_fixture(void) {
    BuffModelInput input = fixture();

    input.show_buffed[BUFF_KIND_BLESS] = 0;
    input.spell_known[BUFF_KIND_ENHANCE] = 1;
    set_skill(&input.spell_skills[BUFF_KIND_ENHANCE], 50, 0, 1, 2);
    input.show_buffed[BUFF_KIND_ENHANCE] = 1;
    return input;
}

static void test_enhance_weapon(void) {
    BuffModelInput input = enhance_fixture();
    BuffModelResult result;

    buff_model_compute(&input, &result);
    CHECK(result.weapon == 46);
    CHECK(result.armor == 30);

    input.reduced_item_buffs = 1;
    buff_model_compute(&input, &result);
    CHECK(result.weapon == 41);

    input = enhance_fixture();
    input.active[BUFF_KIND_ENHANCE].known = 1;
    input.active[BUFF_KIND_ENHANCE].power = 40;
    input.server_weapon = 44;
    input.show_buffed[BUFF_KIND_ENHANCE] = 0;
    buff_model_compute(&input, &result);
    CHECK(result.weapon == 30);
}

static void test_enhance_uses_blessed_attributes(void) {
    BuffModelInput input = enhance_fixture();
    BuffModelResult result;

    input.show_buffed[BUFF_KIND_BLESS] = 1;
    buff_model_compute(&input, &result);
    CHECK(result.weapon == 48);
}

static BuffModelInput fool_reversed_fixture(void) {
    BuffModelInput input = fixture();

    set_all_attributes(input.server_attributes, 105);
    input.spell_known[BUFF_KIND_BLESS] = 0;
    input.show_buffed[BUFF_KIND_BLESS] = 0;
    input.tarot_cards = (TarotSet) 1 << (unsigned int) TAROT_FOOL_REVERSED;
    return input;
}

static void test_fool_reversed_spreads_planned_points(void) {
    BuffModelInput input = fool_reversed_fixture();
    BuffModelResult result;

    input.planned_attributes[0] = 5;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 106, 106, 106, 106, 106));
}

static void test_fool_reversed_four_points_change_nothing(void) {
    BuffModelInput input = fool_reversed_fixture();
    BuffModelResult result;

    input.planned_attributes[0] = 4;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 105, 105, 105, 105, 105));
}

static void test_fool_reversed_removing_active_bless_drops_whole_breakpoints(void) {
    BuffModelInput input = fool_reversed_fixture();
    BuffModelResult result;

    input.active[BUFF_KIND_BLESS].known = 1;
    input.active[BUFF_KIND_BLESS].power = 10;
    input.active[BUFF_KIND_BLESS].has_exact = 1;
    input.active[BUFF_KIND_BLESS].attribute_bonus[0] = 2;
    input.active[BUFF_KIND_BLESS].attribute_bonus[1] = 1;
    input.active[BUFF_KIND_BLESS].attribute_bonus[2] = 1;
    input.active[BUFF_KIND_BLESS].attribute_bonus[3] = 1;
    input.active[BUFF_KIND_BLESS].attribute_bonus[4] = 1;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 102, 102, 102, 102, 102));
}

static void test_magician_uses_strength_base_for_aptitude(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    input.show_buffed[BUFF_KIND_BLESS] = 0;
    input.server_attributes[4] = 60;
    input.planned_attributes[4] = 10;
    buff_model_compute(&input, &result);
    CHECK(result.spell_aptitude == 200);

    input.tarot_cards = (TarotSet) 1 << (unsigned int) TAROT_MAGICIAN;
    buff_model_compute(&input, &result);
    CHECK(result.spell_aptitude == 210);
}

static void test_star_reversed_drops_spell_bonus_from_cast_power(void) {
    BuffModelInput input = fixture();
    BuffModelResult without_bonus;
    BuffModelResult star_reversed;

    buff_model_compute(&input, &without_bonus);
    input.spell_bonus = 20;
    input.tarot_cards = (TarotSet) 1 << (unsigned int) TAROT_STAR_REVERSED;
    buff_model_compute(&input, &star_reversed);
    CHECK(star_reversed.cast_power[BUFF_KIND_BLESS] == without_bonus.cast_power[BUFF_KIND_BLESS]);
}

static void test_hermit_scales_resist_skill_change(void) {
    BuffModelInput input = fixture();
    BuffModelSkill skill;

    set_skill(&skill, 80, -1, -1, -1);
    skill.planned = 10;
    skill.scale_kind = BUFF_SKILL_SCALE_RESIST;
    CHECK(buff_model_skill_value(&input, &skill, input.server_attributes) == 90);
    input.tarot_cards = (TarotSet) 1 << (unsigned int) TAROT_HERMIT;
    CHECK(buff_model_skill_value(&input, &skill, input.server_attributes) == 88);
}

static void test_moon_reversed_scales_tactics_with_mana(void) {
    BuffModelInput input = fixture();
    BuffModelSkill skill;

    set_skill(&skill, 110, -1, -1, -1);
    skill.planned = 10;
    skill.scale_kind = BUFF_SKILL_SCALE_TACTICS;
    input.tarot_cards = (TarotSet) 1 << (unsigned int) TAROT_MOON_REVERSED;
    input.uncapped_mana = 500;
    CHECK(buff_model_skill_value(&input, &skill, input.server_attributes) == 121);
    input.planned_mana = 500;
    CHECK(buff_model_skill_value(&input, &skill, input.server_attributes) == 132);
}

int main(void) {
    test_bless_bonus_formula();
    test_bless_double_cast();
    test_bless_off_shows_unbuffed();
    test_aptitude_caps_power();
    test_multiplier_and_bonus();
    test_planned_willpower_feeds_bless();
    test_planned_intuition_raises_aptitude();
    test_active_bless_taken_out();
    test_stronger_active_bless_kept();
    test_exact_bonus_preferred();
    test_enhance_weapon();
    test_enhance_uses_blessed_attributes();
    test_fool_reversed_spreads_planned_points();
    test_fool_reversed_four_points_change_nothing();
    test_fool_reversed_removing_active_bless_drops_whole_breakpoints();
    test_magician_uses_strength_base_for_aptitude();
    test_star_reversed_drops_spell_bonus_from_cast_power();
    test_hermit_scales_resist_skill_change();
    test_moon_reversed_scales_tactics_with_mana();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All buff_model tests passed\n");
    return 0;
}
