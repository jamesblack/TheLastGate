#include "combat_focus.h"

#include <string.h>

#include "planner/combat_shown.h"
#include "planner/focus_search.h"

static int stat_with_points(const CombatFocusInput *input, const int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    BuffModelInput model = input->model;
    BuffModelSkill combat_skills[COMBAT_SKILL_COUNT];
    BuffModelResult buffed;
    CombatValues shown;
    int candidate_index;
    int skill_id;
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
        for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
            if (raise_index == input->spell_raise_index[kind]) model.spell_skills[kind].planned += extra;
        }
        for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
            if (raise_index == input->combat_raise_index[skill_id]) combat_skills[skill_id].planned += extra;
        }
    }

    buff_model_compute(&model, &buffed);
    shown = combat_shown_values(&model, &buffed, &input->combat, combat_skills);
    return combat_model_stat_value(&shown, input->stat);
}

typedef struct {
    const CombatFocusInput *input;
    int target;
} StatTarget;

static int reaches_target(const void *context, const int points[BUFF_FOCUS_MAX_CANDIDATES]) {
    const StatTarget *stat_target = (const StatTarget *) context;

    return stat_with_points(stat_target->input, points) >= stat_target->target;
}

CombatFocusResult combat_focus_search(const CombatFocusInput *input) {
    CombatFocusResult result;
    int points[BUFF_FOCUS_MAX_CANDIDATES];
    StatTarget stat_target;
    FocusSearchQuery query;

    memset(&result, 0, sizeof(result));
    memset(points, 0, sizeof(points));
    result.current_value = stat_with_points(input, points);
    stat_target.input = input;
    stat_target.target = result.current_value + 1;

    query.candidates = input->candidates;
    query.candidate_count = input->candidate_count;
    query.allowed_candidates = ~0U;
    query.accept = reaches_target;
    query.context = &stat_target;
    result.best = focus_search_cheapest(&query);
    return result;
}
