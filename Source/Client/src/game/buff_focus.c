#include "buff_focus.h"

#include <string.h>

#include "game/skill_focus.h"

/* Cost of the first `points` extra points of every candidate, or -1 once a capped point is included. */
typedef struct {
    int prefix_costs[BUFF_FOCUS_MAX_CANDIDATES][BUFF_FOCUS_MAX_POINTS + 1];
} CostTable;

/* A route under consideration: points per candidate, with its cost and point total. */
typedef struct {
    int found;
    int cost;
    int point_total;
    int points[BUFF_FOCUS_MAX_CANDIDATES];
} SearchBest;

static int clamp_bound(const int max_points) {
    if (max_points < 0) return 0;
    return max_points > BUFF_FOCUS_MAX_POINTS ? BUFF_FOCUS_MAX_POINTS : max_points;
}

static void build_cost_table(const BuffFocusInput *input, CostTable *table) {
    int candidate_index;
    int step_index;

    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        const BuffFocusCandidate *candidate = &input->candidates[candidate_index];
        int running = 0;

        table->prefix_costs[candidate_index][0] = 0;
        for (step_index = 0; step_index < clamp_bound(candidate->max_points); step_index++) {
            const int step_cost = candidate->step_costs[step_index];
            if (running < 0 || step_cost == SKILL_FOCUS_NO_COST) {
                running = -1;
            } else {
                running += step_cost;
            }
            table->prefix_costs[candidate_index][step_index + 1] = running;
        }
    }
}

static int value_with_points(const BuffFocusInput *input, const int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    BuffModelInput model = input->model;
    BuffModelSkill focused = input->focused;
    BuffModelResult result;
    int candidate_index;
    int kind;

    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        const int raise_index = input->candidates[candidate_index].raise_index;
        const int extra = points[candidate_index];

        if (extra == 0) continue;
        if (raise_index >= 0 && raise_index < BUFF_ATTRIBUTE_COUNT) model.planned_attributes[raise_index] += extra;
        if (raise_index == input->focused_raise_index) focused.planned += extra;
        for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
            if (raise_index == input->spell_raise_index[kind]) model.spell_skills[kind].planned += extra;
        }
    }
    buff_model_compute(&model, &result);
    return buff_model_skill_value(&model, &focused, result.attributes);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int beats(const SearchBest *best, const int cost, const int point_total) {
    if (!best->found) return 1;
    if (cost != best->cost) return cost < best->cost;
    return point_total < best->point_total;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void keep(SearchBest *best, const int cost, const int point_total, const int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    best->found = 1;
    best->cost = cost;
    best->point_total = point_total;
    memcpy(best->points, points, sizeof(best->points));
}

static BuffFocusRoute to_route(const BuffFocusInput *input, const CostTable *table, const SearchBest *best) {
    BuffFocusRoute route;
    int candidate_index;
    int sorted_index;

    memset(&route, 0, sizeof(route));
    if (!best->found) return route;
    route.available = 1;
    route.total_cost = best->cost;
    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        const int points = best->points[candidate_index];
        BuffFocusRaise raise;

        if (points == 0) continue;
        raise.raise_index = input->candidates[candidate_index].raise_index;
        raise.points = points;
        raise.cost = table->prefix_costs[candidate_index][points];

        /* Insert in raise_index order. */
        sorted_index = route.raise_count;
        while (sorted_index > 0 && route.raises[sorted_index - 1].raise_index > raise.raise_index) {
            route.raises[sorted_index] = route.raises[sorted_index - 1];
            sorted_index--;
        }
        route.raises[sorted_index] = raise;
        route.raise_count++;
    }
    return route;
}

/* Moves to the next mix of points; returns 0 once every mix has been visited. */
static int next_mix(const BuffFocusInput *input, int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    int candidate_index;

    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        if (points[candidate_index] < clamp_bound(input->candidates[candidate_index].max_points)) {
            points[candidate_index]++;
            return 1;
        }
        points[candidate_index] = 0;
    }
    return 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
BuffFocusResult buff_focus_search(const BuffFocusInput *input) {
    BuffFocusResult result;
    CostTable table;
    SearchBest best;
    SearchBest direct;
    SearchBest through_attributes;
    SearchBest through_bless;
    int points[BUFF_FOCUS_MAX_CANDIDATES];
    int focused_candidate = -1;
    int bless_candidate = -1;
    int candidate_index;
    int target;

    memset(&result, 0, sizeof(result));
    memset(&best, 0, sizeof(best));
    memset(&direct, 0, sizeof(direct));
    memset(&through_attributes, 0, sizeof(through_attributes));
    memset(&through_bless, 0, sizeof(through_bless));
    memset(points, 0, sizeof(points));
    build_cost_table(input, &table);

    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        const int raise_index = input->candidates[candidate_index].raise_index;

        if (raise_index == input->focused_raise_index) {
            focused_candidate = candidate_index;
        } else if (input->spell_raise_index[BUFF_KIND_BLESS] >= 0 &&
                   raise_index == input->spell_raise_index[BUFF_KIND_BLESS]) {
            bless_candidate = candidate_index;
        }
    }

    result.current_value = value_with_points(input, points);
    target = result.current_value + 1;

    while (next_mix(input, points)) {
        int cost = 0;
        int point_total = 0;
        int affordable = 1;
        int focused_points;
        int uses_bless;
        int worth_best;
        int worth_direct;
        int worth_attributes;
        int worth_bless;

        for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
            const int prefix = table.prefix_costs[candidate_index][points[candidate_index]];
            if (prefix < 0) {
                affordable = 0;
                break;
            }
            cost += prefix;
            point_total += points[candidate_index];
        }
        if (!affordable) continue;

        /* Every route is one of three kinds: the skill itself, its attributes, or through Bless. */
        focused_points = focused_candidate >= 0 ? points[focused_candidate] : 0;
        uses_bless = bless_candidate >= 0 && points[bless_candidate] > 0;
        worth_best = beats(&best, cost, point_total);
        worth_direct = focused_points > 0 && focused_points == point_total && beats(&direct, cost, point_total);
        worth_attributes = focused_points == 0 && !uses_bless && beats(&through_attributes, cost, point_total);
        worth_bless = uses_bless && beats(&through_bless, cost, point_total);
        if (!worth_best && !worth_direct && !worth_attributes && !worth_bless) continue;
        if (value_with_points(input, points) < target) continue;

        if (worth_best) keep(&best, cost, point_total, points);
        if (worth_direct) keep(&direct, cost, point_total, points);
        if (worth_attributes) keep(&through_attributes, cost, point_total, points);
        if (worth_bless) {
            /* Only a route whose last Bless point is needed counts as going through Bless. */
            points[bless_candidate]--;
            if (value_with_points(input, points) < target) {
                points[bless_candidate]++;
                keep(&through_bless, cost, point_total, points);
            } else {
                points[bless_candidate]++;
            }
        }
    }

    result.best = to_route(input, &table, &best);
    result.direct = to_route(input, &table, &direct);
    result.through_attributes = to_route(input, &table, &through_attributes);
    result.through_bless = to_route(input, &table, &through_bless);
    return result;
}
