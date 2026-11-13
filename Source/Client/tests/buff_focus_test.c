/*
 * Unit tests for the cheapest +1 search that can use Bless as a lever.
 */

#include <stdio.h>
#include <string.h>

#include "planner/buff_focus.h"
#include "planner/skill_focus.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define FOCUSED_SLOT 8
#define BLESS_SLOT 9
#define GEARMAST_SLOT 12
#define FIRST_INERT_SLOT 20 /* raise slots the model never reads, so points there change nothing */

static void set_skill(BuffModelSkill *skill, const int server_total, const int first_id, const int second_id,
                      const int third_id) {
    skill->server_total = server_total;
    skill->planned = 0;
    skill->attribute_ids[0] = first_id;
    skill->attribute_ids[1] = second_id;
    skill->attribute_ids[2] = third_id;
    skill->skill_groups = 0;
}

static void set_model(BuffModelInput *model) {
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        model->server_attributes[attribute_id] = 50;
        model->base_attributes[attribute_id] = 40;
    }
    model->divisor = 5;
    model->spell_multiplier = 100;
    model->spell_aptitude = 200;
    model->race_modifier = 100;
    model->server_weapon = 30;
    model->server_armor = 30;
    model->spell_known[BUFF_KIND_BLESS] = 1;
    set_skill(&model->spell_skills[BUFF_KIND_BLESS], 60, 0, 1, 1);
    set_skill(&model->spell_skills[BUFF_KIND_ENHANCE], 0, -1, -1, -1);
    set_skill(&model->spell_skills[BUFF_KIND_PROTECT], 0, -1, -1, -1);
    model->show_buffed[BUFF_KIND_BLESS] = 1;
}

static void add_candidate(BuffFocusInput *input, const int raise_index, const int max_points, const int cost) {
    BuffFocusCandidate *candidate = &input->candidates[input->candidate_count++];
    int step_index;

    candidate->raise_index = raise_index;
    candidate->max_points = max_points;
    for (step_index = 0; step_index < BUFF_FOCUS_MAX_POINTS; step_index++) candidate->step_costs[step_index] = cost;
}

/* Focused skill uses Agi, Str, Str; Bless is the lever. */
static BuffFocusInput fixture(void) {
    BuffFocusInput input;

    memset(&input, 0, sizeof(input));
    set_model(&input.model);
    set_skill(&input.focused, 100, 3, 4, 4);
    input.focused_raise_index = FOCUSED_SLOT;
    input.spell_raise_index[BUFF_KIND_BLESS] = BLESS_SLOT;
    input.spell_raise_index[BUFF_KIND_ENHANCE] = -1;
    input.spell_raise_index[BUFF_KIND_PROTECT] = -1;
    add_candidate(&input, FOCUSED_SLOT, 1, 1000);
    add_candidate(&input, 0, 6, 300);
    add_candidate(&input, 1, 6, 300);
    add_candidate(&input, 2, 6, 300);
    add_candidate(&input, 3, 6, 500);
    add_candidate(&input, 4, 6, 500);
    add_candidate(&input, BLESS_SLOT, 16, 20);
    return input;
}

static int route_is_single(const BuffFocusRoute *route, const int raise_index, const int points, const int cost) {
    return route->available && route->raise_count == 1 && route->total_cost == cost &&
           route->raises[0].raise_index == raise_index && route->raises[0].points == points &&
           route->raises[0].cost == cost;
}

static void test_bless_points_win(void) {
    const BuffFocusInput input = fixture();
    const BuffFocusResult result = buff_focus_search(&input);

    CHECK(result.current_value == 106);
    CHECK(route_is_single(&result.best, BLESS_SLOT, 8, 160));
    CHECK(route_is_single(&result.direct, FOCUSED_SLOT, 1, 1000));
    CHECK(route_is_single(&result.through_attributes, 4, 1, 500));
    CHECK(route_is_single(&result.through_bless, BLESS_SLOT, 8, 160));
}

static void test_direct_when_cheap(void) {
    BuffFocusInput input = fixture();
    BuffFocusResult result;

    input.candidates[0].step_costs[0] = 10;
    result = buff_focus_search(&input);
    CHECK(route_is_single(&result.best, FOCUSED_SLOT, 1, 10));
    CHECK(route_is_single(&result.direct, FOCUSED_SLOT, 1, 10));
    CHECK(route_is_single(&result.through_attributes, 4, 1, 500));
    CHECK(route_is_single(&result.through_bless, BLESS_SLOT, 8, 160));
}

static void test_capped_bless_skipped(void) {
    BuffFocusInput input = fixture();
    BuffFocusResult result;
    int step_index;

    for (step_index = 4; step_index < BUFF_FOCUS_MAX_POINTS; step_index++) {
        input.candidates[6].step_costs[step_index] = SKILL_FOCUS_NO_COST;
    }
    result = buff_focus_search(&input);
    CHECK(route_is_single(&result.best, 4, 1, 500));
    CHECK(route_is_single(&result.through_attributes, 4, 1, 500));
    /* A Bless point that the route would succeed without does not make it a Bless route. */
    CHECK(result.through_bless.available && result.through_bless.total_cost == 1780);
}

static void test_nothing_available(void) {
    BuffFocusInput input = fixture();
    BuffFocusResult result;
    int candidate_index;
    int step_index;

    for (candidate_index = 0; candidate_index < input.candidate_count; candidate_index++) {
        for (step_index = 0; step_index < BUFF_FOCUS_MAX_POINTS; step_index++) {
            input.candidates[candidate_index].step_costs[step_index] = SKILL_FOCUS_NO_COST;
        }
    }
    result = buff_focus_search(&input);
    CHECK(result.best.available == 0);
    CHECK(result.direct.available == 0);
    CHECK(result.through_attributes.available == 0);
    CHECK(result.through_bless.available == 0);
}

static void test_focus_on_bless_itself(void) {
    BuffFocusInput input;
    BuffFocusResult result;

    memset(&input, 0, sizeof(input));
    set_model(&input.model);
    set_skill(&input.focused, 60, 0, 1, 1);
    input.focused_raise_index = BLESS_SLOT;
    input.spell_raise_index[BUFF_KIND_BLESS] = BLESS_SLOT;
    input.spell_raise_index[BUFF_KIND_ENHANCE] = -1;
    input.spell_raise_index[BUFF_KIND_PROTECT] = -1;
    add_candidate(&input, BLESS_SLOT, 16, 20);
    add_candidate(&input, 0, 6, 300);
    result = buff_focus_search(&input);
    CHECK(result.current_value == 66);
    CHECK(route_is_single(&result.best, BLESS_SLOT, 1, 20));
    /* Focusing Bless makes a Bless point the direct raise, not a route through Bless. */
    CHECK(route_is_single(&result.direct, BLESS_SLOT, 1, 20));
    CHECK(result.through_bless.available == 0);
}

static void test_fool_reversed_uses_attribute_pool(void) {
    BuffFocusInput input;
    BuffFocusResult result;
    int attribute_id;

    memset(&input, 0, sizeof(input));
    set_model(&input.model);
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        input.model.server_attributes[attribute_id] = 105;
    }
    input.model.spell_known[BUFF_KIND_BLESS] = 0;
    input.model.show_buffed[BUFF_KIND_BLESS] = 0;
    input.model.tarot_cards = (TarotSet) 1 << (unsigned int) TAROT_FOOL_REVERSED;
    set_skill(&input.focused, 100, 0, 1, 2);
    input.focused_raise_index = FOCUSED_SLOT;
    input.spell_raise_index[BUFF_KIND_BLESS] = -1;
    input.spell_raise_index[BUFF_KIND_ENHANCE] = -1;
    input.spell_raise_index[BUFF_KIND_PROTECT] = -1;
    add_candidate(&input, FOCUSED_SLOT, 1, 1000);
    add_candidate(&input, BUFF_FOCUS_ATTRIBUTE_POOL, 10, 10);

    result = buff_focus_search(&input);
    CHECK(result.best.available);
    CHECK(result.best.raise_count == 1);
    CHECK(result.best.raises[0].raise_index == BUFF_FOCUS_ATTRIBUTE_POOL);
    CHECK(result.best.raises[0].points == 10);
    CHECK(result.best.total_cost == 100);
}

/* A Resistance that gains a tenth of the shown AV; the focused point costs far more than any other route. */
static BuffFocusInput resist_fixture(void) {
    BuffFocusInput input;
    int kind;
    int skill_id;

    memset(&input, 0, sizeof(input));
    set_model(&input.model);
    input.model.show_buffed[BUFF_KIND_BLESS] = 0;
    input.model.server_armor = 109;
    input.model.effects.armor_to_resist = 1;
    set_skill(&input.focused, 70, -1, -1, -1);
    input.focused.scale_kind = BUFF_SKILL_SCALE_RESIST;
    input.focused_raise_index = FOCUSED_SLOT;
    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) input.spell_raise_index[kind] = -1;
    input.armor_feeds_focused = 1;
    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) input.combat_raise_index[skill_id] = -1;
    add_candidate(&input, FOCUSED_SLOT, 1, 100);
    return input;
}

static void test_armor_feeds_resistance_through_gear_mastery(void) {
    BuffFocusInput input = resist_fixture();
    BuffFocusResult result;

    /* 59 to 60 Gear Mastery adds 1 AV, which takes the shown AV from 109 to 110 and its tenth from 10 to 11. */
    input.combat.known[COMBAT_SKILL_GEARMAST] = 1;
    input.combat.current[COMBAT_SKILL_GEARMAST] = 59;
    input.combat.shown[COMBAT_SKILL_GEARMAST] = 59;
    input.combat.gear_class = COMBAT_GEAR_OTHER;
    input.combat_raise_index[COMBAT_SKILL_GEARMAST] = GEARMAST_SLOT;
    set_skill(&input.combat_skills[COMBAT_SKILL_GEARMAST], 59, -1, -1, -1);
    add_candidate(&input, GEARMAST_SLOT, 16, 5);
    result = buff_focus_search(&input);
    CHECK(result.current_value == 70);
    CHECK(route_is_single(&result.best, GEARMAST_SLOT, 1, 5));
}

static void test_hitpoint_raise_reaches_resistance_through_odachi(void) {
    BuffFocusInput input = resist_fixture();
    BuffFocusResult result;

    /* 499 to 500 Hitpoints adds 1 Spell Bonus, which the Odachi adds to AV. */
    input.model.effects.spell_bonus_to_armor = 1;
    input.model.effects.hitpoints_to_spell_bonus = 1;
    input.model.uncapped_hitpoints = 499;
    add_candidate(&input, BUFF_FOCUS_HITPOINTS_RAISE, 16, 3);
    result = buff_focus_search(&input);
    CHECK(result.current_value == 70);
    CHECK(route_is_single(&result.best, BUFF_FOCUS_HITPOINTS_RAISE, 1, 3));
}

static void test_many_candidates_still_find_a_cheap_route(void) {
    BuffFocusInput input = fixture();
    BuffFocusResult result;
    int inert_index;

    for (inert_index = 0; input.candidate_count < BUFF_FOCUS_MAX_CANDIDATES; inert_index++) {
        add_candidate(&input, FIRST_INERT_SLOT + inert_index, BUFF_FOCUS_MAX_POINTS, 1000);
    }
    result = buff_focus_search(&input);
    CHECK(input.candidate_count == BUFF_FOCUS_MAX_CANDIDATES);
    CHECK(route_is_single(&result.best, BLESS_SLOT, 8, 160));
    CHECK(route_is_single(&result.direct, FOCUSED_SLOT, 1, 1000));
    CHECK(route_is_single(&result.through_attributes, 4, 1, 500));
    CHECK(route_is_single(&result.through_bless, BLESS_SLOT, 8, 160));
}

static void test_search_without_a_route_stops_at_the_state_cap(void) {
    BuffFocusInput input = fixture();
    BuffFocusResult result;

    input.candidate_count = 0;
    while (input.candidate_count < BUFF_FOCUS_MAX_CANDIDATES) {
        add_candidate(&input, FIRST_INERT_SLOT + input.candidate_count, BUFF_FOCUS_MAX_POINTS, 1);
    }
    result = buff_focus_search(&input);
    CHECK(result.current_value == 106);
    CHECK(result.best.available == 0);
    CHECK(result.direct.available == 0);
    CHECK(result.through_attributes.available == 0);
    CHECK(result.through_bless.available == 0);
}

int main(void) {
    test_fool_reversed_uses_attribute_pool();
    test_bless_points_win();
    test_direct_when_cheap();
    test_capped_bless_skipped();
    test_nothing_available();
    test_focus_on_bless_itself();
    test_armor_feeds_resistance_through_gear_mastery();
    test_hitpoint_raise_reaches_resistance_through_odachi();
    test_many_candidates_still_find_a_cheap_route();
    test_search_without_a_route_stops_at_the_state_cap();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All buff_focus tests passed\n");
    return 0;
}
