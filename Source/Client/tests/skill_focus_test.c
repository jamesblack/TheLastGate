/*
 * Unit tests for the cheapest +1 search behind the planner's skill focus.
 */

#include <stdio.h>

#include "planner/skill_focus.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static SkillFocusInput make_input(const int first_id, const int second_id, const int third_id, const int divisor) {
    SkillFocusInput input;
    int slot_index;
    int step_index;

    input.attribute_ids[0] = first_id;
    input.attribute_ids[1] = second_id;
    input.attribute_ids[2] = third_id;
    for (slot_index = 0; slot_index < ATTRIBUTES_PER_SKILL; slot_index++) {
        input.attribute_values[slot_index] = 0;
        for (step_index = 0; step_index < SKILL_FOCUS_MAX_STEPS; step_index++) {
            input.attribute_step_costs[slot_index][step_index] = 100;
        }
    }
    input.divisor = divisor;
    input.direct_cost = 1000;
    return input;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void set_values(SkillFocusInput *input, const int first, const int second, const int third) {
    input->attribute_values[0] = first;
    input->attribute_values[1] = second;
    input->attribute_values[2] = third;
}

static void set_costs(SkillFocusInput *input, const int slot_index, const int first, const int second) {
    input->attribute_step_costs[slot_index][0] = first;
    input->attribute_step_costs[slot_index][1] = second;
}

static int total_points(const SkillFocusRoute *route) {
    int point_sum = 0;
    int raise_index;

    for (raise_index = 0; raise_index < route->raise_count; raise_index++) {
        point_sum += route->raises[raise_index].points;
    }
    return point_sum;
}

static void test_direct_cheaper(void) {
    SkillFocusInput input = make_input(0, 1, 2, 5);
    SkillFocusSuggestion suggestion;

    set_values(&input, 10, 10, 20);
    input.direct_cost = 300;
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.use_direct == 1);
    CHECK(suggestion.direct.available == 1);
    CHECK(suggestion.direct.total_cost == 300);
    CHECK(suggestion.attributes.available == 1);
    CHECK(suggestion.attributes.total_cost == 500);
}

static void test_attribute_split_cheapest(void) {
    SkillFocusInput input = make_input(0, 1, 2, 5);
    SkillFocusSuggestion suggestion;

    set_values(&input, 14, 14, 15);
    set_costs(&input, 0, 100, 150);
    set_costs(&input, 1, 120, 200);
    set_costs(&input, 2, 500, 500);
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.use_direct == 0);
    CHECK(suggestion.attributes.total_cost == 220);
    CHECK(suggestion.attributes.raise_count == 2);
    CHECK(suggestion.attributes.raises[0].attribute_id == 0);
    CHECK(suggestion.attributes.raises[0].points == 1);
    CHECK(suggestion.attributes.raises[0].cost == 100);
    CHECK(suggestion.attributes.raises[1].attribute_id == 1);
    CHECK(suggestion.attributes.raises[1].points == 1);
    CHECK(suggestion.attributes.raises[1].cost == 120);
}

static void test_on_boundary_needs_full_step(void) {
    SkillFocusInput input = make_input(0, 1, 2, 5);
    SkillFocusSuggestion suggestion;
    int slot_index;
    int step_index;

    set_values(&input, 15, 15, 15);
    for (slot_index = 0; slot_index < ATTRIBUTES_PER_SKILL; slot_index++) {
        for (step_index = 0; step_index < SKILL_FOCUS_MAX_STEPS; step_index++) {
            input.attribute_step_costs[slot_index][step_index] = 10;
        }
    }
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.attributes.total_cost == 50);
    CHECK(total_points(&suggestion.attributes) == 5);
}

static void set_doubled_costs(SkillFocusInput *input, const int doubled_cost) {
    int step_index;

    for (step_index = 0; step_index < SKILL_FOCUS_MAX_STEPS; step_index++) {
        input->attribute_step_costs[0][step_index] = doubled_cost;
        input->attribute_step_costs[1][step_index] = doubled_cost;
        input->attribute_step_costs[2][step_index] = 50 + (step_index * 10);
    }
}

static void test_doubled_attribute_prefers_cheaper_singles(void) {
    SkillFocusInput input = make_input(3, 3, 4, 5);
    SkillFocusSuggestion suggestion;

    set_values(&input, 10, 10, 11);
    set_doubled_costs(&input, 300);
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.attributes.total_cost == 260);
    CHECK(suggestion.attributes.raise_count == 1);
    CHECK(suggestion.attributes.raises[0].attribute_id == 4);
    CHECK(suggestion.attributes.raises[0].points == 4);
    CHECK(suggestion.attributes.raises[0].cost == 260);
}

static void test_doubled_attribute_prefers_double(void) {
    SkillFocusInput input = make_input(3, 3, 4, 5);
    SkillFocusSuggestion suggestion;

    set_values(&input, 10, 10, 11);
    set_doubled_costs(&input, 40);
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.attributes.total_cost == 80);
    CHECK(suggestion.attributes.raise_count == 1);
    CHECK(suggestion.attributes.raises[0].attribute_id == 3);
    CHECK(suggestion.attributes.raises[0].points == 2);
    CHECK(suggestion.attributes.raises[0].cost == 80);
}

static void test_capped_attribute_skips_split(void) {
    SkillFocusInput input = make_input(0, 1, 2, 5);
    SkillFocusSuggestion suggestion;

    set_values(&input, 14, 14, 15);
    set_costs(&input, 0, 10, SKILL_FOCUS_NO_COST);
    set_costs(&input, 1, 50, 100);
    set_costs(&input, 2, 60, 100);
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.attributes.total_cost == 60);
    CHECK(suggestion.attributes.raise_count == 2);
}

static void make_all_capped(SkillFocusInput *input) {
    int slot_index;
    int step_index;

    for (slot_index = 0; slot_index < ATTRIBUTES_PER_SKILL; slot_index++) {
        for (step_index = 0; step_index < SKILL_FOCUS_MAX_STEPS; step_index++) {
            input->attribute_step_costs[slot_index][step_index] = SKILL_FOCUS_NO_COST;
        }
    }
}

static void test_no_attribute_route(void) {
    SkillFocusInput input = make_input(0, 1, 2, 5);
    SkillFocusSuggestion suggestion;

    make_all_capped(&input);
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.attributes.available == 0);
    CHECK(suggestion.use_direct == 1);
    CHECK(suggestion.any_available == 1);
}

static void test_nothing_available(void) {
    SkillFocusInput input = make_input(0, 1, 2, 5);
    SkillFocusSuggestion suggestion;

    make_all_capped(&input);
    input.direct_cost = SKILL_FOCUS_NO_COST;
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.direct.available == 0);
    CHECK(suggestion.attributes.available == 0);
    CHECK(suggestion.any_available == 0);
}

static void test_tie_goes_direct(void) {
    SkillFocusInput input = make_input(0, 1, 2, 5);
    SkillFocusSuggestion suggestion;

    set_values(&input, 10, 10, 20);
    input.direct_cost = 500;
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.attributes.total_cost == 500);
    CHECK(suggestion.use_direct == 1);
}

static void test_hard_mode_divisor(void) {
    SkillFocusInput input = make_input(0, 1, 2, 6);
    SkillFocusSuggestion suggestion;
    int slot_index;
    int step_index;

    set_values(&input, 12, 12, 12);
    for (slot_index = 0; slot_index < ATTRIBUTES_PER_SKILL; slot_index++) {
        for (step_index = 0; step_index < SKILL_FOCUS_MAX_STEPS; step_index++) {
            input.attribute_step_costs[slot_index][step_index] = 10;
        }
    }
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.attributes.total_cost == 60);
    CHECK(total_points(&suggestion.attributes) == 6);
}

static void test_invalid_attribute_slot_ignored(void) {
    SkillFocusInput input = make_input(0, -1, 2, 5);
    SkillFocusSuggestion suggestion;
    int raise_index;

    set_values(&input, 14, 99, 15);
    set_costs(&input, 0, 30, 100);
    set_costs(&input, 1, 1, 1);
    set_costs(&input, 2, 20, 100);
    suggestion = skill_focus_suggest(&input);

    CHECK(suggestion.attributes.total_cost == 20);
    CHECK(suggestion.attributes.raise_count == 1);
    CHECK(suggestion.attributes.raises[0].attribute_id == 2);
    CHECK(suggestion.attributes.raises[0].points == 1);
    for (raise_index = 0; raise_index < suggestion.attributes.raise_count; raise_index++) {
        CHECK(suggestion.attributes.raises[raise_index].attribute_id != -1);
    }
}

int main(void) {
    test_direct_cheaper();
    test_attribute_split_cheapest();
    test_on_boundary_needs_full_step();
    test_doubled_attribute_prefers_cheaper_singles();
    test_doubled_attribute_prefers_double();
    test_capped_attribute_skips_split();
    test_no_attribute_route();
    test_nothing_available();
    test_tie_goes_direct();
    test_hard_mode_divisor();
    test_invalid_attribute_slot_ignored();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All skill_focus tests passed\n");
    return 0;
}
