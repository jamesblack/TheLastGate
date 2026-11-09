#pragma once

/*
 * Cheapest way to give the focused skill +1 when buffs are part of the plan.
 *
 * Every mix of extra points over the candidate stats (the skill itself, its
 * attributes, Bless and what feeds Bless) is tried within each candidate's
 * bound, and the buff model decides whether the mix lifts the skill's shown
 * value by one. Self-contained (no game headers) so it can be built into the
 * unit tests; the caller supplies every value and cost.
 */

#include "game/buff_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUFF_FOCUS_MAX_CANDIDATES 8
#define BUFF_FOCUS_MAX_POINTS 16

typedef struct {
    int raise_index; /* stat_raise slot: 0-4 attribute, 8+ skill */
    int max_points; /* at most BUFF_FOCUS_MAX_POINTS */
    int step_costs[BUFF_FOCUS_MAX_POINTS]; /* cost of the 1st..nth extra point; SKILL_FOCUS_NO_COST when capped */
} BuffFocusCandidate;

typedef struct {
    BuffModelInput model; /* planner state, plan included */
    BuffModelSkill focused;
    int focused_raise_index;
    int spell_raise_index[BUFF_KIND_COUNT]; /* stat_raise slot of each spell skill, -1 when not known */
    int candidate_count;
    BuffFocusCandidate candidates[BUFF_FOCUS_MAX_CANDIDATES];
} BuffFocusInput;

typedef struct {
    int raise_index;
    int points;
    int cost;
} BuffFocusRaise;

typedef struct {
    int available;
    int total_cost;
    int raise_count;
    BuffFocusRaise raises[BUFF_FOCUS_MAX_CANDIDATES]; /* ordered by raise_index, ascending */
} BuffFocusRoute;

typedef struct {
    int current_value; /* the focused skill's shown value with the plan */
    BuffFocusRoute best;
    BuffFocusRoute direct; /* one point in the focused skill itself */
    BuffFocusRoute through_attributes; /* no points in the focused skill or in Bless */
    /* Needs its Bless points: one fewer and it falls short. Never when Bless is focused. */
    BuffFocusRoute through_bless;
} BuffFocusResult;

BuffFocusResult buff_focus_search(const BuffFocusInput *input);

#ifdef __cplusplus
}
#endif
