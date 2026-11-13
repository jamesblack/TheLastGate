#include <stdio.h>
#include <string.h>

#include "planner/combat_focus.h"
#include "planner/combat_shown.h"
#include "planner/skill_focus.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define SWORD_SLOT 8
#define TACTICS_SLOT 9
#define AXE_SLOT 10
#define SHIELD_SLOT 11

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

static void set_combat_skill(CombatFocusInput *input, const CombatSkill skill_id, const int raise_index,
                             const int server_total) {
    input->combat.known[skill_id] = 1;
    input->combat.current[skill_id] = server_total;
    input->combat.shown[skill_id] = server_total;
    input->combat_raise_index[skill_id] = raise_index;
    set_skill(&input->combat_skills[skill_id], server_total, ATTRIBUTE_AGILITY, ATTRIBUTE_AGILITY, ATTRIBUTE_STRENGTH);
}

static CombatFocusInput fixture(const CombatStat stat) {
    CombatFocusInput input;
    int attribute_id;
    int skill_id;
    int kind;

    memset(&input, 0, sizeof(input));
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        input.model.server_attributes[attribute_id] = 50;
        input.model.base_attributes[attribute_id] = 40;
    }
    input.model.divisor = 5;
    input.model.spell_multiplier = 100;
    input.model.spell_aptitude = 200;
    input.model.race_modifier = 100;
    input.model.server_weapon = 30;
    input.model.server_armor = 20;
    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
        set_skill(&input.model.spell_skills[kind], 0, -1, -1, -1);
        input.spell_raise_index[kind] = -1;
    }
    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) input.combat_raise_index[skill_id] = -1;

    input.combat.weapon = COMBAT_WEAPON_SWORD;
    input.combat.offhand = COMBAT_OFFHAND_NONE;
    input.combat.gear_class = COMBAT_GEAR_OTHER;
    input.combat.server_hit = 100;
    input.combat.server_parry = 90;
    input.stat = stat;
    return input;
}

static void add_candidate(CombatFocusInput *input, const int raise_index, const int max_points, const int cost) {
    BuffFocusCandidate *candidate = &input->candidates[input->candidate_count++];
    int step_index;

    candidate->raise_index = raise_index;
    candidate->max_points = max_points;
    for (step_index = 0; step_index < BUFF_FOCUS_MAX_POINTS; step_index++) {
        candidate->step_costs[step_index] = step_index < max_points ? cost : SKILL_FOCUS_NO_COST;
    }
}

static void test_cheap_weapon_skill_point_wins(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_HIT);
    CombatFocusResult result;

    set_combat_skill(&input, COMBAT_SKILL_SWORD, SWORD_SLOT, 60);
    add_candidate(&input, SWORD_SLOT, 1, 10);
    add_candidate(&input, ATTRIBUTE_AGILITY, 5, 10);
    add_candidate(&input, ATTRIBUTE_STRENGTH, 5, 10);
    result = combat_focus_search(&input);
    CHECK(result.current_value == 100);
    CHECK(result.best.available);
    CHECK(result.best.total_cost == 10);
    CHECK(result.best.raise_count == 1);
    CHECK(result.best.raises[0].raise_index == SWORD_SLOT);
    CHECK(result.best.raises[0].points == 1);
}

static void test_attribute_route_wins_when_cheaper(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_PARRY);
    CombatFocusResult result;

    /* Agility counts twice, so three points push the sum past the next multiple of five. */
    set_combat_skill(&input, COMBAT_SKILL_SWORD, SWORD_SLOT, 60);
    add_candidate(&input, SWORD_SLOT, 1, 100);
    add_candidate(&input, ATTRIBUTE_AGILITY, 5, 20);
    add_candidate(&input, ATTRIBUTE_STRENGTH, 5, 20);
    result = combat_focus_search(&input);
    CHECK(result.current_value == 90);
    CHECK(result.best.available);
    CHECK(result.best.total_cost == 60);
    CHECK(result.best.raise_count == 1);
    CHECK(result.best.raises[0].raise_index == ATTRIBUTE_AGILITY);
    CHECK(result.best.raises[0].points == 3);
}

static void test_equal_cost_prefers_fewer_points(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_HIT);
    CombatFocusResult result;

    set_combat_skill(&input, COMBAT_SKILL_SWORD, SWORD_SLOT, 60);
    add_candidate(&input, SWORD_SLOT, 1, 30);
    add_candidate(&input, ATTRIBUTE_AGILITY, 5, 10);
    result = combat_focus_search(&input);
    CHECK(result.best.available);
    CHECK(result.best.total_cost == 30);
    CHECK(result.best.raise_count == 1);
    CHECK(result.best.raises[0].raise_index == SWORD_SLOT);
}

static void test_tactics_needs_several_points(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_HIT);
    CombatFocusResult result;

    /* Tactics adds a sixth of its value, so 60 needs six more points to add one. */
    set_combat_skill(&input, COMBAT_SKILL_TACTICS, TACTICS_SLOT, 60);
    input.combat_skills[COMBAT_SKILL_TACTICS].attribute_ids[0] = -1;
    input.combat_skills[COMBAT_SKILL_TACTICS].attribute_ids[1] = -1;
    input.combat_skills[COMBAT_SKILL_TACTICS].attribute_ids[2] = -1;
    add_candidate(&input, TACTICS_SLOT, BUFF_FOCUS_MAX_POINTS, 5);
    result = combat_focus_search(&input);
    CHECK(result.best.available);
    CHECK(result.best.total_cost == 30);
    CHECK(result.best.raise_count == 1);
    CHECK(result.best.raises[0].points == 6);
}

static void test_capped_skill_has_no_route(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_HIT);
    CombatFocusResult result;

    set_combat_skill(&input, COMBAT_SKILL_SWORD, SWORD_SLOT, 60);
    add_candidate(&input, SWORD_SLOT, 0, 10);
    result = combat_focus_search(&input);
    CHECK(result.current_value == 100);
    CHECK(!result.best.available);
}

static void test_stat_it_cannot_move_has_no_route(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_ARMOR);
    CombatFocusResult result;

    set_combat_skill(&input, COMBAT_SKILL_SWORD, SWORD_SLOT, 60);
    add_candidate(&input, SWORD_SLOT, BUFF_FOCUS_MAX_POINTS, 10);
    add_candidate(&input, ATTRIBUTE_AGILITY, 5, 10);
    result = combat_focus_search(&input);
    CHECK(result.current_value == 20);
    CHECK(!result.best.available);
}

static TarotSet card_bit(const int card_index) {
    return (TarotSet) 1 << (unsigned int) card_index;
}

static CombatValues shown_values(const CombatFocusInput *input) {
    BuffModelResult buffed;

    buff_model_compute(&input->model, &buffed);
    return combat_shown_values(&input->model, &buffed, &input->combat, input->combat_skills);
}

static void test_best_weapon_skill_route(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_HIT);
    CombatFocusResult result;

    input.combat.best_weapon_skill = 1;
    set_combat_skill(&input, COMBAT_SKILL_SWORD, SWORD_SLOT, 60);
    set_combat_skill(&input, COMBAT_SKILL_AXE, AXE_SLOT, 58);
    add_candidate(&input, SWORD_SLOT, 1, 100);
    add_candidate(&input, AXE_SLOT, BUFF_FOCUS_MAX_POINTS, 10);
    result = combat_focus_search(&input);
    CHECK(result.best.total_cost == 30 && result.best.raises[0].raise_index == AXE_SLOT);
    CHECK(result.best.raises[0].points == 3);
}

static void test_parry_to_hit_lets_shield_points_raise_hit(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_HIT);
    CombatFocusResult result;

    input.combat.offhand = COMBAT_OFFHAND_SHIELD;
    input.model.effects.parry_to_hit_percent = 50;
    set_combat_skill(&input, COMBAT_SKILL_SWORD, SWORD_SLOT, 60);
    set_combat_skill(&input, COMBAT_SKILL_SHIELD, SHIELD_SLOT, 100);
    add_candidate(&input, SWORD_SLOT, 1, 100);
    add_candidate(&input, SHIELD_SLOT, BUFF_FOCUS_MAX_POINTS, 5);
    result = combat_focus_search(&input);
    CHECK(result.current_value == 100 && result.best.total_cost == 40);
    CHECK(result.best.raises[0].raise_index == SHIELD_SLOT && result.best.raises[0].points == 8);
}

static void test_shown_values_equal_server_with_empty_plan(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_HIT);
    CombatValues shown;

    input.model.effects.fists_of_heavens = 1;
    input.model.effects.hit_to_parry_percent = 3;
    input.model.effects.parry_to_hit_percent = 3;
    input.model.effects.armor_enchant = 1;
    input.model.tarot_cards = card_bit(TAROT_HERMIT);
    input.combat.best_weapon_skill = 1;
    set_combat_skill(&input, COMBAT_SKILL_SWORD, SWORD_SLOT, 60);
    shown = shown_values(&input);
    CHECK(shown.hit == 100 && shown.parry == 90 && shown.weapon == 30 && shown.armor == 20);
}

static void test_evergreen_weapon_value_goes_through_lovers_and_hermit(void) {
    CombatFocusInput input = fixture(COMBAT_STAT_WEAPON);
    CombatValues shown;

    /* Twenty more Agility gives Evergreen +2 WV; Lovers splits it and Hermit scales the armor half. */
    input.model.effects.evergreen = 1;
    input.model.tarot_cards = card_bit(TAROT_LOVERS) | card_bit(TAROT_HERMIT);
    input.model.server_weapon = 40;
    input.model.server_armor = 46;
    input.model.planned_attributes[ATTRIBUTE_AGILITY] = 20;
    shown = shown_values(&input);
    CHECK(shown.weapon == 41 && shown.armor == 47);
}

int main(void) {
    test_cheap_weapon_skill_point_wins();
    test_attribute_route_wins_when_cheaper();
    test_equal_cost_prefers_fewer_points();
    test_tactics_needs_several_points();
    test_capped_skill_has_no_route();
    test_stat_it_cannot_move_has_no_route();
    test_best_weapon_skill_route();
    test_parry_to_hit_lets_shield_points_raise_hit();
    test_shown_values_equal_server_with_empty_plan();
    test_evergreen_weapon_value_goes_through_lovers_and_hermit();

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("combat_focus_test: all checks passed\n");
    return 0;
}
