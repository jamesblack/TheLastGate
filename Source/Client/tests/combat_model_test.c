/*
 * Unit tests for the planner's combat model: how planned skill values move
 * Hit, Parry, Weapon Value and Armor Value, and the default weapon and off-hand.
 */

#include <stdio.h>
#include <string.h>

#include "planner/combat_model.h"

static int failures = 0;

#define WEAPON_SKILL_REACH 16

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static CombatModelInput fixture(void) {
    CombatModelInput input;
    int skill_id;

    memset(&input, 0, sizeof(input));
    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) input.known[skill_id] = 1;
    input.weapon = COMBAT_WEAPON_SWORD;
    input.offhand = COMBAT_OFFHAND_NONE;
    input.gear_class = COMBAT_GEAR_OTHER;
    input.server_hit = 100;
    input.server_parry = 90;
    input.base_weapon = 30;
    input.base_armor = 20;
    return input;
}

static void set_skill(CombatModelInput *input, const CombatSkill skill_id, const int current, const int shown) {
    input->current[skill_id] = current;
    input->shown[skill_id] = shown;
}

static CombatValues compute(const CombatModelInput *input) {
    CombatValues result;

    combat_model_compute(input, &result);
    return result;
}

static void test_default_weapon_highest_known(void) {
    CombatModelInput input = fixture();

    input.current[COMBAT_SKILL_DAGGER] = 50;
    input.current[COMBAT_SKILL_SWORD] = 80;
    input.current[COMBAT_SKILL_AXE] = 80;
    input.current[COMBAT_SKILL_STAFF] = 90;
    input.known[COMBAT_SKILL_STAFF] = 0;
    CHECK(combat_model_default_weapon(input.known, input.current) == COMBAT_WEAPON_SWORD);
}

static void test_default_weapon_none_known(void) {
    CombatModelInput input = fixture();

    input.known[COMBAT_SKILL_HAND] = 0;
    input.known[COMBAT_SKILL_DAGGER] = 0;
    input.known[COMBAT_SKILL_SWORD] = 0;
    input.known[COMBAT_SKILL_AXE] = 0;
    input.known[COMBAT_SKILL_STAFF] = 0;
    input.known[COMBAT_SKILL_TWOHAND] = 0;
    input.current[COMBAT_SKILL_SWORD] = 80;
    CHECK(combat_model_default_weapon(input.known, input.current) == COMBAT_WEAPON_UNARMED);
}

static void test_default_offhand(void) {
    CombatModelInput input = fixture();

    input.current[COMBAT_SKILL_SHIELD] = 40;
    input.current[COMBAT_SKILL_DUAL] = 60;
    CHECK(combat_model_default_offhand(input.known, input.current) == COMBAT_OFFHAND_DUAL);
    input.known[COMBAT_SKILL_DUAL] = 0;
    CHECK(combat_model_default_offhand(input.known, input.current) == COMBAT_OFFHAND_SHIELD);
    input.known[COMBAT_SKILL_SHIELD] = 0;
    CHECK(combat_model_default_offhand(input.known, input.current) == COMBAT_OFFHAND_NONE);
    input.known[COMBAT_SKILL_SHIELD] = 1;
    input.known[COMBAT_SKILL_DUAL] = 1;
    input.current[COMBAT_SKILL_SHIELD] = 50;
    input.current[COMBAT_SKILL_DUAL] = 50;
    CHECK(combat_model_default_offhand(input.known, input.current) == COMBAT_OFFHAND_SHIELD);
}

static void test_weapon_skill_moves_hit_parry(void) {
    CombatModelInput input = fixture();
    CombatValues result;

    set_skill(&input, COMBAT_SKILL_SWORD, 80, 90);
    result = compute(&input);
    CHECK(result.hit == 110);
    CHECK(result.parry == 100);
    CHECK(result.weapon == 30);
    CHECK(result.armor == 20);
}

static void test_weapon_skill_capped(void) {
    CombatModelInput input = fixture();

    set_skill(&input, COMBAT_SKILL_SWORD, 295, 310);
    CHECK(compute(&input).hit == 105);
}

static void test_spear_best_of_two(void) {
    CombatModelInput input = fixture();

    input.weapon = COMBAT_WEAPON_SPEAR;
    set_skill(&input, COMBAT_SKILL_DAGGER, 50, 50);
    set_skill(&input, COMBAT_SKILL_STAFF, 60, 70);
    CHECK(compute(&input).hit == 110);
    set_skill(&input, COMBAT_SKILL_DAGGER, 50, 80);
    CHECK(compute(&input).hit == 120);
}

static void test_dual_wield_adds_hit(void) {
    CombatModelInput input = fixture();
    CombatValues result;

    input.offhand = COMBAT_OFFHAND_DUAL;
    set_skill(&input, COMBAT_SKILL_DUAL, 40, 48);
    result = compute(&input);
    CHECK(result.hit == 102);
    CHECK(result.parry == 90);
}

static void test_shield_adds_parry(void) {
    CombatModelInput input = fixture();

    input.offhand = COMBAT_OFFHAND_SHIELD;
    set_skill(&input, COMBAT_SKILL_SHIELD, 40, 48);
    CHECK(compute(&input).parry == 92);
    CHECK(compute(&input).hit == 100);
    input.rampart = 1;
    CHECK(compute(&input).parry == 93);
}

static void test_offhand_skills_capped(void) {
    CombatModelInput input = fixture();

    input.offhand = COMBAT_OFFHAND_SHIELD;
    set_skill(&input, COMBAT_SKILL_SHIELD, 300, 312);
    CHECK(compute(&input).parry == 90);
    input.offhand = COMBAT_OFFHAND_DUAL;
    set_skill(&input, COMBAT_SKILL_DUAL, 292, 312);
    CHECK(compute(&input).hit == 102);
}

static void test_tactics_both(void) {
    CombatModelInput input = fixture();
    CombatValues result;

    set_skill(&input, COMBAT_SKILL_TACTICS, 60, 66);
    result = compute(&input);
    CHECK(result.hit == 101);
    CHECK(result.parry == 91);
}

static void test_unknown_skill_adds_nothing(void) {
    CombatModelInput input = fixture();
    CombatValues result;

    input.known[COMBAT_SKILL_TACTICS] = 0;
    set_skill(&input, COMBAT_SKILL_TACTICS, 60, 66);
    result = compute(&input);
    CHECK(result.hit == 100);
    CHECK(result.parry == 90);
}

static void check_gear_mastery(const CombatGearClass gear_class, const int weapon, const int armor) {
    CombatModelInput input = fixture();
    CombatValues result;

    input.gear_class = gear_class;
    set_skill(&input, COMBAT_SKILL_GEARMAST, 100, 150);
    result = compute(&input);
    CHECK(result.weapon == weapon);
    CHECK(result.armor == armor);
}

static void test_gear_mastery_divisors(void) {
    check_gear_mastery(COMBAT_GEAR_OTHER, 35, 25);
    check_gear_mastery(COMBAT_GEAR_TEMPLAR_OR_BRAVER, 40, 30);
    check_gear_mastery(COMBAT_GEAR_ARCHTEMPLAR, 42, 37);
    check_gear_mastery(COMBAT_GEAR_SKALD, 47, 32);
}

static void test_hand_unarmed_weapon_value(void) {
    CombatModelInput input = fixture();
    CombatValues result;

    input.weapon = COMBAT_WEAPON_UNARMED;
    set_skill(&input, COMBAT_SKILL_HAND, 80, 100);
    result = compute(&input);
    CHECK(result.hit == 120);
    CHECK(result.weapon == 35);
    input.weapon = COMBAT_WEAPON_CLAW;
    result = compute(&input);
    CHECK(result.hit == 120);
    CHECK(result.weapon == 30);
}

static void test_ferocity(void) {
    CombatModelInput input = fixture();
    CombatValues result;

    input.lycan = 1;
    input.empty_slots = 4;
    set_skill(&input, COMBAT_SKILL_FEROCITY, 40, 60);
    result = compute(&input);
    CHECK(result.weapon == 34);
    CHECK(result.armor == 24);
    input.lycan = 0;
    result = compute(&input);
    CHECK(result.weapon == 30);
    CHECK(result.armor == 20);
}

static void test_skills_affecting_hit_and_parry(void) {
    CombatModelInput input = fixture();
    int affects[COMBAT_SKILL_COUNT];

    input.weapon = COMBAT_WEAPON_SPEAR;
    input.offhand = COMBAT_OFFHAND_DUAL;
    combat_model_skills_affecting(&input, COMBAT_STAT_HIT, WEAPON_SKILL_REACH, affects);
    CHECK(affects[COMBAT_SKILL_DAGGER] && affects[COMBAT_SKILL_STAFF] && affects[COMBAT_SKILL_TACTICS]);
    CHECK(affects[COMBAT_SKILL_DUAL]);
    CHECK(!affects[COMBAT_SKILL_SWORD] && !affects[COMBAT_SKILL_SHIELD] && !affects[COMBAT_SKILL_GEARMAST]);

    combat_model_skills_affecting(&input, COMBAT_STAT_PARRY, WEAPON_SKILL_REACH, affects);
    CHECK(!affects[COMBAT_SKILL_DUAL]);

    input.offhand = COMBAT_OFFHAND_SHIELD;
    combat_model_skills_affecting(&input, COMBAT_STAT_PARRY, WEAPON_SKILL_REACH, affects);
    CHECK(affects[COMBAT_SKILL_SHIELD]);

    input.known[COMBAT_SKILL_TACTICS] = 0;
    combat_model_skills_affecting(&input, COMBAT_STAT_PARRY, WEAPON_SKILL_REACH, affects);
    CHECK(!affects[COMBAT_SKILL_TACTICS]);
}

static void test_skills_affecting_weapon_and_armor(void) {
    CombatModelInput input = fixture();
    int affects[COMBAT_SKILL_COUNT];

    input.weapon = COMBAT_WEAPON_UNARMED;
    combat_model_skills_affecting(&input, COMBAT_STAT_WEAPON, WEAPON_SKILL_REACH, affects);
    CHECK(affects[COMBAT_SKILL_GEARMAST] && affects[COMBAT_SKILL_HAND]);
    CHECK(!affects[COMBAT_SKILL_FEROCITY] && !affects[COMBAT_SKILL_TACTICS]);

    input.lycan = 1;
    combat_model_skills_affecting(&input, COMBAT_STAT_ARMOR, WEAPON_SKILL_REACH, affects);
    CHECK(affects[COMBAT_SKILL_GEARMAST] && affects[COMBAT_SKILL_FEROCITY]);
    CHECK(!affects[COMBAT_SKILL_HAND]);
}

static void test_best_weapon_skill_uses_highest(void) {
    CombatModelInput input = fixture();
    CombatValues result;

    input.best_weapon_skill = 1;
    set_skill(&input, COMBAT_SKILL_HAND, 50, 50);
    set_skill(&input, COMBAT_SKILL_SWORD, 80, 80);
    set_skill(&input, COMBAT_SKILL_AXE, 70, 90);
    result = compute(&input);
    CHECK(result.hit == 110 && result.parry == 100);
}

static void test_best_weapon_skill_has_no_unarmed_weapon_value(void) {
    CombatModelInput input = fixture();
    CombatValues result;

    input.best_weapon_skill = 1;
    input.weapon = COMBAT_WEAPON_UNARMED;
    set_skill(&input, COMBAT_SKILL_HAND, 40, 80);
    result = compute(&input);
    CHECK(result.weapon == 30 && result.hit == 140);
}

static void test_best_weapon_skill_marks_skills_within_reach(void) {
    CombatModelInput input = fixture();
    int affects[COMBAT_SKILL_COUNT];

    input.best_weapon_skill = 1;
    input.known[COMBAT_SKILL_TWOHAND] = 0;
    set_skill(&input, COMBAT_SKILL_HAND, 50, 50);
    set_skill(&input, COMBAT_SKILL_SWORD, 80, 80);
    set_skill(&input, COMBAT_SKILL_AXE, 70, 65);
    set_skill(&input, COMBAT_SKILL_TWOHAND, 79, 79);
    combat_model_skills_affecting(&input, COMBAT_STAT_HIT, WEAPON_SKILL_REACH, affects);
    CHECK(affects[COMBAT_SKILL_SWORD] && affects[COMBAT_SKILL_AXE] && !affects[COMBAT_SKILL_HAND]);
    CHECK(!affects[COMBAT_SKILL_DAGGER] && !affects[COMBAT_SKILL_STAFF] && !affects[COMBAT_SKILL_TWOHAND]);
    CHECK(affects[COMBAT_SKILL_TACTICS] && !affects[COMBAT_SKILL_GEARMAST]);

    set_skill(&input, COMBAT_SKILL_AXE, 70, 64);
    combat_model_skills_affecting(&input, COMBAT_STAT_HIT, WEAPON_SKILL_REACH, affects);
    CHECK(!affects[COMBAT_SKILL_AXE]);

    combat_model_skills_affecting(&input, COMBAT_STAT_PARRY, 31, affects);
    CHECK(affects[COMBAT_SKILL_HAND] && affects[COMBAT_SKILL_AXE] && !affects[COMBAT_SKILL_DAGGER]);
}

static void test_parry_feeds_hit_marks_shield_for_hit(void) {
    CombatModelInput input = fixture();
    int affects[COMBAT_SKILL_COUNT];

    input.offhand = COMBAT_OFFHAND_SHIELD;
    combat_model_skills_affecting(&input, COMBAT_STAT_HIT, WEAPON_SKILL_REACH, affects);
    CHECK(!affects[COMBAT_SKILL_SHIELD]);

    input.parry_feeds_hit = 1;
    combat_model_skills_affecting(&input, COMBAT_STAT_HIT, WEAPON_SKILL_REACH, affects);
    CHECK(affects[COMBAT_SKILL_SHIELD] && affects[COMBAT_SKILL_SWORD] && !affects[COMBAT_SKILL_DUAL]);

    input.offhand = COMBAT_OFFHAND_DUAL;
    combat_model_skills_affecting(&input, COMBAT_STAT_HIT, WEAPON_SKILL_REACH, affects);
    CHECK(!affects[COMBAT_SKILL_SHIELD] && affects[COMBAT_SKILL_DUAL]);
}

static void test_hit_feeds_parry_marks_dual_for_parry(void) {
    CombatModelInput input = fixture();
    int affects[COMBAT_SKILL_COUNT];

    input.offhand = COMBAT_OFFHAND_DUAL;
    combat_model_skills_affecting(&input, COMBAT_STAT_PARRY, WEAPON_SKILL_REACH, affects);
    CHECK(!affects[COMBAT_SKILL_DUAL]);

    input.hit_feeds_parry = 1;
    combat_model_skills_affecting(&input, COMBAT_STAT_PARRY, WEAPON_SKILL_REACH, affects);
    CHECK(affects[COMBAT_SKILL_DUAL] && affects[COMBAT_SKILL_SWORD] && !affects[COMBAT_SKILL_SHIELD]);

    input.known[COMBAT_SKILL_DUAL] = 0;
    combat_model_skills_affecting(&input, COMBAT_STAT_PARRY, WEAPON_SKILL_REACH, affects);
    CHECK(!affects[COMBAT_SKILL_DUAL]);
}

int main(void) {
    test_default_weapon_highest_known();
    test_default_weapon_none_known();
    test_default_offhand();
    test_weapon_skill_moves_hit_parry();
    test_weapon_skill_capped();
    test_spear_best_of_two();
    test_dual_wield_adds_hit();
    test_shield_adds_parry();
    test_offhand_skills_capped();
    test_tactics_both();
    test_unknown_skill_adds_nothing();
    test_gear_mastery_divisors();
    test_hand_unarmed_weapon_value();
    test_ferocity();
    test_skills_affecting_hit_and_parry();
    test_skills_affecting_weapon_and_armor();
    test_best_weapon_skill_uses_highest();
    test_best_weapon_skill_has_no_unarmed_weapon_value();
    test_best_weapon_skill_marks_skills_within_reach();
    test_parry_feeds_hit_marks_shield_for_hit();
    test_hit_feeds_parry_marks_dual_for_parry();

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("combat_model_test: all checks passed\n");
    return 0;
}
