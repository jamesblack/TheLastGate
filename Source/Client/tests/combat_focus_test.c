#include <stdio.h>
#include <string.h>

#include "planner/combat_focus.h"
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

static void set_skill(BuffModelSkill *skill, const int server_total, const int first_id, const int second_id,
                      const int third_id) {
    skill->server_total = server_total;
    skill->planned = 0;
    skill->attribute_ids[0] = first_id;
    skill->attribute_ids[1] = second_id;
    skill->attribute_ids[2] = third_id;
    skill->scale_kind = BUFF_SKILL_SCALE_NONE;
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

int main(void) {
    test_cheap_weapon_skill_point_wins();
    test_attribute_route_wins_when_cheaper();
    test_equal_cost_prefers_fewer_points();
    test_tactics_needs_several_points();
    test_capped_skill_has_no_route();
    test_stat_it_cannot_move_has_no_route();

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("combat_focus_test: all checks passed\n");
    return 0;
}
