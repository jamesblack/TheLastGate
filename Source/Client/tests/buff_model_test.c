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
    skill->skill_groups = 0;
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

static void test_no_effects_matches_old_model(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;
    BuffModelSkill immunity;

    input.spell_skills[BUFF_KIND_BLESS].skill_groups =
            SKILL_GROUP_PASSIVE | SKILL_GROUP_ACTIVE_SPELL | SKILL_GROUP_ACTIVE_MELEE;
    input.planned_attributes[1] = 5;
    buff_model_compute(&input, &result);
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 68);
    CHECK(attributes_equal(&result, 62, 66, 61, 61, 61));

    set_skill(&immunity, 80, -1, -1, -1);
    immunity.planned = 10;
    immunity.scale_kind = BUFF_SKILL_SCALE_IMMUNITY;
    CHECK(buff_model_skill_value(&input, &immunity, input.server_attributes) == 90);
}

static BuffModelInput unbuffed_fixture(void) {
    BuffModelInput input = fixture();

    input.spell_known[BUFF_KIND_BLESS] = 0;
    input.show_buffed[BUFF_KIND_BLESS] = 0;
    return input;
}

static void test_attribute_percent_scales_planned_points(void) {
    BuffModelInput input = unbuffed_fixture();
    BuffModelResult result;

    input.server_attributes[ATTRIBUTE_STRENGTH] = 100;
    input.effects.attribute_percent[ATTRIBUTE_STRENGTH] = 6;
    input.planned_attributes[ATTRIBUTE_STRENGTH] = 10;
    buff_model_compute(&input, &result);
    CHECK(result.attributes[ATTRIBUTE_STRENGTH] == 111);
    CHECK(result.attributes[ATTRIBUTE_AGILITY] == 50);
}

static void test_attribute_percent_rounds_like_server(void) {
    BuffModelInput input = unbuffed_fixture();
    BuffModelResult result;

    input.server_attributes[ATTRIBUTE_STRENGTH] = 106;
    input.effects.attribute_percent[ATTRIBUTE_STRENGTH] = 6;
    buff_model_compute(&input, &result);
    CHECK(result.attributes[ATTRIBUTE_STRENGTH] == 106);

    input.planned_attributes[ATTRIBUTE_STRENGTH] = 1;
    buff_model_compute(&input, &result);
    CHECK(result.attributes[ATTRIBUTE_STRENGTH] == 107);
}

static int strength_raise_value(const int skill_groups) {
    BuffModelInput input = fixture();
    BuffModelSkill skill;
    int planned[ATTRIBUTE_COUNT];

    input.server_attributes[ATTRIBUTE_AGILITY] = 52;
    input.server_attributes[ATTRIBUTE_STRENGTH] = 100;
    input.effects.skill_extra_percent[ATTRIBUTE_STRENGTH] = 20;
    set_skill(&skill, 60, ATTRIBUTE_AGILITY, ATTRIBUTE_AGILITY, ATTRIBUTE_STRENGTH);
    skill.skill_groups = skill_groups;
    memcpy(planned, input.server_attributes, sizeof(planned));
    planned[ATTRIBUTE_STRENGTH] += 5;
    return buff_model_skill_value(&input, &skill, planned);
}

static void test_skill_extra_percent(void) {
    /* (52 + 52 + 105 + 2100 / 100) / 5 = 46 against (52 + 52 + 100 + 2000 / 100) / 5 = 44. */
    CHECK(strength_raise_value(SKILL_GROUP_ACTIVE_MELEE) == 62);
}

static void test_extra_percent_ignores_other_groups(void) {
    /* 209 / 5 = 41 against 204 / 5 = 40, as without the effect. */
    CHECK(strength_raise_value(SKILL_GROUP_PASSIVE) == 61);
}

static int immunity_with_bravery_raise(const TarotSet tarot_cards) {
    BuffModelInput input = fixture();
    BuffModelSkill skill;
    int planned[ATTRIBUTE_COUNT];

    input.tarot_cards = tarot_cards;
    input.effects.immunity_percent = 10;
    set_skill(&skill, 110, ATTRIBUTE_BRAVENESS, ATTRIBUTE_WILLPOWER, ATTRIBUTE_INTUITION);
    skill.scale_kind = BUFF_SKILL_SCALE_IMMUNITY;
    memcpy(planned, input.server_attributes, sizeof(planned));
    planned[ATTRIBUTE_BRAVENESS] += 50;
    return buff_model_skill_value(&input, &skill, planned);
}

static void test_immunity_percent(void) {
    CHECK(immunity_with_bravery_raise(0) == 121);
}

static void test_immunity_percent_with_hermit(void) {
    /* Raw 110 x 500 / 440 = 125 shows 109 and 135 shows 118, so the change is 9. */
    CHECK(immunity_with_bravery_raise((TarotSet) 1 << (unsigned int) TAROT_HERMIT) == 119);
}

static void test_immunity_percent_and_hermit_round_in_two_steps(void) {
    BuffModelInput input = fixture();
    BuffModelSkill skill;

    input.tarot_cards = (TarotSet) 1 << (unsigned int) TAROT_HERMIT;
    input.effects.immunity_percent = 10;
    set_skill(&skill, 88, -1, -1, -1);
    skill.planned = 29;
    skill.scale_kind = BUFF_SKILL_SCALE_IMMUNITY;
    /* Raw 100 becomes 129: 129 x 110 / 100 = 141, then 141 x 4 / 5 = 112 (one fraction would give 113). */
    CHECK(buff_model_skill_value(&input, &skill, input.server_attributes) == 112);
}

static void test_fool_reversed_averages_before_attribute_percent(void) {
    BuffModelInput input = fool_reversed_fixture();
    BuffModelResult result;

    /* Strength 50 is 40 before +25%, where the next Reverse Fool step adds two, so 40 becomes 42. */
    input.server_attributes[ATTRIBUTE_STRENGTH] = 50;
    input.effects.attribute_percent[ATTRIBUTE_STRENGTH] = 25;
    input.planned_attributes[ATTRIBUTE_BRAVENESS] = 5;
    buff_model_compute(&input, &result);
    CHECK(attributes_equal(&result, 106, 106, 106, 106, 52));
}

static void test_aptitude_percent(void) {
    BuffModelInput input = unbuffed_fixture();
    BuffModelResult result;

    /* Raw 110 x 100 / 110 = 100 becomes 110, which shows 121. */
    input.spell_aptitude = 110;
    input.effects.aptitude_percent = 10;
    input.planned_attributes[ATTRIBUTE_WILLPOWER] = 10;
    buff_model_compute(&input, &result);
    CHECK(result.spell_aptitude == 121);
}

static void test_attribute_percent_skips_aptitude(void) {
    BuffModelInput input = unbuffed_fixture();
    BuffModelResult result;

    input.spell_aptitude = 110;
    input.effects.aptitude_percent = 10;
    input.effects.attribute_percent[ATTRIBUTE_WILLPOWER] = 10;
    input.planned_attributes[ATTRIBUTE_WILLPOWER] = 10;
    buff_model_compute(&input, &result);
    CHECK(result.attributes[ATTRIBUTE_WILLPOWER] == 61);
    CHECK(result.spell_aptitude == 121);
}

static BuffModelInput brilliance_fixture(void) {
    BuffModelInput input = unbuffed_fixture();

    set_all_attributes(input.server_attributes, 90);
    input.server_attributes[ATTRIBUTE_STRENGTH] = 89;
    input.spell_bonus = 30;
    input.effects.brilliance = 1;
    return input;
}

static void test_brilliance_follows_attribute_sum(void) {
    BuffModelInput input = brilliance_fixture();
    BuffModelResult result;

    buff_model_compute(&input, &result);
    CHECK(result.spell_bonus == 30);

    input.planned_attributes[ATTRIBUTE_STRENGTH] = 1;
    buff_model_compute(&input, &result);
    CHECK(result.spell_bonus == 31);
}

static void test_brilliance_with_magician(void) {
    BuffModelInput input = unbuffed_fixture();
    BuffModelResult result;

    input.server_attributes[ATTRIBUTE_STRENGTH] = 100;
    input.spell_bonus = 10;
    input.effects.brilliance = 1;
    input.planned_attributes[ATTRIBUTE_STRENGTH] = 50;
    buff_model_compute(&input, &result);
    CHECK(result.spell_bonus == 10);

    /* Strength 150 also stands in for Intuition 50, so the sum is 450 against 350. */
    input.tarot_cards = (TarotSet) 1 << (unsigned int) TAROT_MAGICIAN;
    buff_model_compute(&input, &result);
    CHECK(result.spell_bonus == 11);
}

static void test_introspection_follows_aptitude(void) {
    BuffModelInput input = unbuffed_fixture();
    BuffModelResult result;

    input.spell_aptitude = 99;
    input.spell_bonus = 40;
    input.effects.introspection = 2;
    input.planned_attributes[ATTRIBUTE_WILLPOWER] = 1;
    buff_model_compute(&input, &result);
    CHECK(result.spell_aptitude == 100);
    CHECK(result.spell_bonus == 41);
}

static void test_spell_bonus_percent(void) {
    BuffModelInput input = unbuffed_fixture();
    BuffModelResult result;

    /* Introspection adds 5 x 300 / 50 - 5 x 200 / 50 = 10 to raw 50, which shows 72. */
    input.spell_bonus = 60;
    input.effects.spell_bonus_percent = 20;
    input.effects.introspection = 5;
    input.planned_attributes[ATTRIBUTE_WILLPOWER] = 100;
    buff_model_compute(&input, &result);
    CHECK(result.spell_aptitude == 300);
    CHECK(result.spell_bonus == 72);
}

static void test_planned_spell_bonus_raises_cast_power(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    /* The first cast lifts the attribute sum from 250 to 301, past 300, so the second cast gets one more bonus. */
    input.effects.brilliance = 1;
    buff_model_compute(&input, &result);
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 67);
    CHECK(result.spell_bonus == 1);

    input.spell_aptitude = 33;
    buff_model_compute(&input, &result);
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 66);
}

static void test_guardian_angel(void) {
    BuffModelInput input = fixture();
    BuffModelResult result;

    /* Skill 250 is capped at 200. The first cast sees Braveness 70 and gives 207, which lifts Braveness to 100. */
    input.server_attributes[ATTRIBUTE_BRAVENESS] = 70;
    input.spell_aptitude = 100;
    set_skill(&input.spell_skills[BUFF_KIND_BLESS], 250, -1, -1, -1);
    buff_model_compute(&input, &result);
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 200);

    input.effects.guardian_angel = 1;
    buff_model_compute(&input, &result);
    CHECK(result.cast_power[BUFF_KIND_BLESS] == 210);
    CHECK(result.attributes[ATTRIBUTE_BRAVENESS] == 101);
}

static void test_determination(void) {
    BuffModelInput input = brilliance_fixture();
    BuffModelResult result;

    input.effects.brilliance = 0;
    input.effects.determination = 2;
    buff_model_compute(&input, &result);
    CHECK(result.weapon == 30);
    CHECK(result.armor == 30);

    /* 2 x 450 / 150 = 6 against 2 x 449 / 150 = 5. */
    input.planned_attributes[ATTRIBUTE_STRENGTH] = 1;
    buff_model_compute(&input, &result);
    CHECK(result.weapon == 31);
    CHECK(result.armor == 31);

    /* 2 x 525 / 150 = 7. */
    input.planned_attributes[ATTRIBUTE_STRENGTH] = 76;
    buff_model_compute(&input, &result);
    CHECK(result.weapon == 32);
    CHECK(result.armor == 32);
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
    test_no_effects_matches_old_model();
    test_attribute_percent_scales_planned_points();
    test_attribute_percent_rounds_like_server();
    test_skill_extra_percent();
    test_extra_percent_ignores_other_groups();
    test_immunity_percent();
    test_immunity_percent_with_hermit();
    test_immunity_percent_and_hermit_round_in_two_steps();
    test_fool_reversed_averages_before_attribute_percent();
    test_aptitude_percent();
    test_attribute_percent_skips_aptitude();
    test_brilliance_follows_attribute_sum();
    test_brilliance_with_magician();
    test_introspection_follows_aptitude();
    test_spell_bonus_percent();
    test_planned_spell_bonus_raises_cast_power();
    test_guardian_angel();
    test_determination();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All buff_model tests passed\n");
    return 0;
}
