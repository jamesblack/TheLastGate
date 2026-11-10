#pragma once

/*
 * Cheapest way to give one skill +1: either one point in the skill itself, or
 * the cheapest set of attribute points that pushes the skill's attribute total
 * over the next divisor boundary. Self-contained (no game headers) so it can be
 * built into the unit tests; the caller supplies every value and cost.
 */

#include "game/attributes.h"
#include "planner/attribute_pool.h"
#include "planner/skill_preview.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Largest divisor: no attribute ever needs more points than this to reach the next boundary. */
#define SKILL_FOCUS_MAX_STEPS HARD_MODE_ATTRIBUTE_DIVISOR
/* Marks a point that can't be bought (the stat is capped). */
#define SKILL_FOCUS_NO_COST ATTRIBUTE_POOL_NO_COST

typedef struct {
    int attribute_ids[ATTRIBUTES_PER_SKILL]; /* -1 marks an unused slot */
    int attribute_values[ATTRIBUTES_PER_SKILL]; /* values the preview sums, planned raises included */
    /* Cost of the 1st..6th extra point of each slot's attribute. A repeated attribute uses its first slot's costs. */
    int attribute_step_costs[ATTRIBUTES_PER_SKILL][SKILL_FOCUS_MAX_STEPS];
    int divisor;
    int direct_cost; /* SKILL_FOCUS_NO_COST when the skill itself is capped */
    /* Reverse Fool: any attribute point counts, so routes buy the cheapest points across all five. */
    int fool_reversed;
    int pool_step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS];
} SkillFocusInput;

typedef struct {
    int attribute_id;
    int points;
    int cost;
} SkillFocusRaise;

typedef struct {
    int available;
    int total_cost;
    int raise_count; /* 0 for the direct route */
    SkillFocusRaise raises[ATTRIBUTE_COUNT]; /* ordered by attribute id, ascending */
} SkillFocusRoute;

typedef struct {
    SkillFocusRoute direct;
    SkillFocusRoute attributes;
    int use_direct; /* 1 when the direct route is the suggestion; ties go to direct */
    int any_available; /* 0 when neither route exists */
} SkillFocusSuggestion;

SkillFocusSuggestion skill_focus_suggest(const SkillFocusInput *input);

#ifdef __cplusplus
}
#endif
