#include "skill_focus.h"

#include <string.h>

#include "util/math_util.h"

/* One attribute the skill uses, with how many of its slots name it. */
typedef struct {
    int attribute_id;
    int appearances;
    const int *step_costs;
} DistinctAttribute;

static int collect_distinct_attributes(const SkillFocusInput *input, DistinctAttribute distinct[ATTRIBUTES_PER_SKILL]) {
    int distinct_count = 0;
    int slot_index;

    for (slot_index = 0; slot_index < ATTRIBUTES_PER_SKILL; slot_index++) {
        const int attribute_id = input->attribute_ids[slot_index];
        int known_index;
        int insert_index;

        if (attribute_id < 0) continue;
        for (known_index = 0; known_index < distinct_count; known_index++) {
            if (distinct[known_index].attribute_id == attribute_id) break;
        }
        if (known_index < distinct_count) {
            distinct[known_index].appearances++;
            continue;
        }

        /* Keep the list sorted by attribute id so the bundle reads in a stable order. */
        insert_index = distinct_count;
        while (insert_index > 0 && distinct[insert_index - 1].attribute_id > attribute_id) {
            distinct[insert_index] = distinct[insert_index - 1];
            insert_index--;
        }
        distinct[insert_index].attribute_id = attribute_id;
        distinct[insert_index].appearances = 1;
        distinct[insert_index].step_costs = input->attribute_step_costs[slot_index];
        distinct_count++;
    }
    return distinct_count;
}

/* Cost of the first point_count extra points, or SKILL_FOCUS_NO_COST if any of them can't be bought. */
static int prefix_cost(const int *step_costs, const int point_count) {
    int total = 0;
    int step_index;

    for (step_index = 0; step_index < point_count; step_index++) {
        if (step_costs[step_index] == SKILL_FOCUS_NO_COST) return SKILL_FOCUS_NO_COST;
        total += step_costs[step_index];
    }
    return total;
}

static SkillFocusRoute cheapest_attribute_route(const SkillFocusInput *input) {
    DistinctAttribute distinct[ATTRIBUTES_PER_SKILL];
    const int distinct_count = collect_distinct_attributes(input, distinct);
    const int max_points = min(input->divisor, SKILL_FOCUS_MAX_STEPS);
    int point_counts[ATTRIBUTES_PER_SKILL] = {0, 0, 0};
    int best_counts[ATTRIBUTES_PER_SKILL] = {0, 0, 0};
    int best_cost = SKILL_FOCUS_NO_COST;
    int best_points = 0;
    int attribute_sum = 0;
    int needed;
    int slot_index;
    SkillFocusRoute route;

    memset(&route, 0, sizeof(route));
    if (distinct_count == 0 || input->divisor <= 0) return route;

    for (slot_index = 0; slot_index < ATTRIBUTES_PER_SKILL; slot_index++) {
        if (input->attribute_ids[slot_index] >= 0) attribute_sum += input->attribute_values[slot_index];
    }
    needed = input->divisor - (attribute_sum % input->divisor);

    /* Every combination of 0..max_points per distinct attribute; unused positions stay at 0. */
    for (point_counts[0] = 0; point_counts[0] <= max_points; point_counts[0]++) {
        for (point_counts[1] = 0; point_counts[1] <= (distinct_count > 1 ? max_points : 0); point_counts[1]++) {
            for (point_counts[2] = 0; point_counts[2] <= (distinct_count > 2 ? max_points : 0); point_counts[2]++) {
                int gain = 0;
                int cost = 0;
                int point_sum = 0;
                int distinct_index;

                for (distinct_index = 0; distinct_index < distinct_count; distinct_index++) {
                    const int step_cost =
                            prefix_cost(distinct[distinct_index].step_costs, point_counts[distinct_index]);
                    if (step_cost == SKILL_FOCUS_NO_COST) break;
                    cost += step_cost;
                    gain += point_counts[distinct_index] * distinct[distinct_index].appearances;
                    point_sum += point_counts[distinct_index];
                }
                if (distinct_index < distinct_count || gain < needed) continue;

                if (best_cost == SKILL_FOCUS_NO_COST || cost < best_cost ||
                    (cost == best_cost && point_sum < best_points)) {
                    best_cost = cost;
                    best_points = point_sum;
                    memcpy(best_counts, point_counts, sizeof(best_counts));
                }
            }
        }
    }

    if (best_cost == SKILL_FOCUS_NO_COST) return route;

    route.available = 1;
    route.total_cost = best_cost;
    for (slot_index = 0; slot_index < distinct_count; slot_index++) {
        if (best_counts[slot_index] == 0) continue;
        route.raises[route.raise_count].attribute_id = distinct[slot_index].attribute_id;
        route.raises[route.raise_count].points = best_counts[slot_index];
        route.raises[route.raise_count].cost = prefix_cost(distinct[slot_index].step_costs, best_counts[slot_index]);
        route.raise_count++;
    }
    return route;
}

SkillFocusSuggestion skill_focus_suggest(const SkillFocusInput *input) {
    SkillFocusSuggestion suggestion;

    memset(&suggestion, 0, sizeof(suggestion));
    if (input->direct_cost != SKILL_FOCUS_NO_COST) {
        suggestion.direct.available = 1;
        suggestion.direct.total_cost = input->direct_cost;
    }
    suggestion.attributes = cheapest_attribute_route(input);

    suggestion.any_available = suggestion.direct.available || suggestion.attributes.available;
    suggestion.use_direct =
            suggestion.direct.available &&
            (!suggestion.attributes.available || suggestion.direct.total_cost <= suggestion.attributes.total_cost);
    return suggestion;
}
