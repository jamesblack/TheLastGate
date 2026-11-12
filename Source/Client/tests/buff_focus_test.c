/*
 * Unit tests for the cheapest +1 search that can use Bless as a lever.
 */

#include <stdio.h>
#include <string.h>

#include "game/buff_focus.h"
#include "game/skill_focus.h"

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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void set_skill(BuffModelSkill *skill, const int server_total, const int first_id, const int second_id,
                      const int third_id) {
    skill->server_total = server_total;
    skill->planned = 0;
    skill->attribute_ids[0] = first_id;
    skill->attribute_ids[1] = second_id;
    skill->attribute_ids[2] = third_id;
}

static void set_model(BuffModelInput *model) {
    int attribute_id;

    for (attribute_id = 0; attribute_id < BUFF_ATTRIBUTE_COUNT; attribute_id++) {
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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

int main(void) {
    test_bless_points_win();
    test_direct_when_cheap();
    test_capped_bless_skipped();
    test_nothing_available();
    test_focus_on_bless_itself();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All buff_focus tests passed\n");
    return 0;
}
