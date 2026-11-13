#include "buff_focus.h"

#include <string.h>

#include "planner/combat_shown.h"
#include "planner/focus_search.h"

static void add_combat_points(const BuffFocusInput *input, const int raise_index, const int extra,
                              BuffModelSkill combat_skills[COMBAT_SKILL_COUNT]) {
    int skill_id;

    /* Unknown skills are skipped: their raise index may be left at zero, which is the Braveness slot. */
    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
        if (input->combat.known[skill_id] && raise_index == input->combat_raise_index[skill_id]) {
            combat_skills[skill_id].planned += extra;
        }
    }
}

static int value_with_points(const BuffFocusInput *input, const int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    BuffModelInput model = input->model;
    BuffModelSkill focused = input->focused;
    BuffModelSkill combat_skills[COMBAT_SKILL_COUNT];
    BuffModelResult result;
    CombatValues shown;
    int candidate_index;
    int kind;

    memcpy(combat_skills, input->combat_skills, sizeof(combat_skills));
    for (candidate_index = 0; candidate_index < input->candidate_count; candidate_index++) {
        const int raise_index = input->candidates[candidate_index].raise_index;
        const int extra = points[candidate_index];

        if (extra == 0) continue;
        if (raise_index >= 0 && raise_index < ATTRIBUTE_COUNT) model.planned_attributes[raise_index] += extra;
        /* Under Reverse Fool only the sum of planned attribute points matters, so any attribute can hold them. */
        if (raise_index == BUFF_FOCUS_ATTRIBUTE_POOL) model.planned_attributes[0] += extra;
        if (raise_index == BUFF_FOCUS_HITPOINTS_RAISE) model.planned_hitpoints += extra;
        if (raise_index == BUFF_FOCUS_MANA_RAISE) model.planned_mana += extra;
        if (raise_index == input->focused_raise_index) focused.planned += extra;
        for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
            if (raise_index == input->spell_raise_index[kind]) model.spell_skills[kind].planned += extra;
        }
        if (input->armor_feeds_focused) add_combat_points(input, raise_index, extra, combat_skills);
    }
    buff_model_compute(&model, &result);
    if (!input->armor_feeds_focused) return buff_model_skill_value(&model, &focused, result.attributes);
    shown = combat_shown_values(&model, &result, &input->combat, combat_skills);
    return buff_model_skill_value_with_armor(&model, &focused, result.attributes, shown.armor);
}

typedef struct {
    const BuffFocusInput *input;
    int target;
    int bless_candidate;
} FocusTarget;

static int reaches_target(const void *context, const int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    const FocusTarget *focus_target = (const FocusTarget *) context;

    return value_with_points(focus_target->input, points) >= focus_target->target;
}

/* Only a route whose last Bless point is needed counts as going through Bless. */
static int needs_its_bless_points(const void *context, const int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    const FocusTarget *focus_target = (const FocusTarget *) context;
    int fewer_bless[BUFF_FOCUS_MAX_CANDIDATES];

    if (points[focus_target->bless_candidate] == 0 || !reaches_target(context, points)) return 0;
    memcpy(fewer_bless, points, sizeof(fewer_bless));
    fewer_bless[focus_target->bless_candidate]--;
    return !reaches_target(context, fewer_bless);
}

static unsigned int candidate_bit(const int candidate_index) {
    return candidate_index >= 0 ? 1U << (unsigned int) candidate_index : 0U;
}

static BuffFocusRoute cheapest_route(const BuffFocusInput *input, const FocusTarget *focus_target,
                                     const unsigned int allowed_candidates, const FocusSearchAccept accept) {
    FocusSearchQuery query;

    query.candidates = input->candidates;
    query.candidate_count = input->candidate_count;
    query.allowed_candidates = allowed_candidates;
    query.accept = accept;
    query.context = focus_target;
    return focus_search_cheapest(&query);
}

BuffFocusResult buff_focus_search(const BuffFocusInput *input) {
    BuffFocusResult result;
    FocusTarget focus_target;
    int points[BUFF_FOCUS_MAX_CANDIDATES];
    int focused_candidate = -1;
    int bless_candidate = -1;
    int candidate_index;

    memset(&result, 0, sizeof(result));
    memset(points, 0, sizeof(points));
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
    focus_target.input = input;
    focus_target.target = result.current_value + 1;
    focus_target.bless_candidate = bless_candidate;

    /* Every route is one of three kinds: the skill itself, its attributes, or through Bless. */
    result.best = cheapest_route(input, &focus_target, ~0U, reaches_target);
    if (focused_candidate >= 0) {
        result.direct = cheapest_route(input, &focus_target, candidate_bit(focused_candidate), reaches_target);
    }
    result.through_attributes = cheapest_route(
            input, &focus_target, ~(candidate_bit(focused_candidate) | candidate_bit(bless_candidate)), reaches_target);
    if (bless_candidate >= 0) {
        result.through_bless = cheapest_route(input, &focus_target, ~0U, needs_its_bless_points);
    }
    return result;
}
