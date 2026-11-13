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

#include "planner/buff_model.h"
#include "planner/combat_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUFF_FOCUS_MAX_CANDIDATES 12
#define BUFF_FOCUS_MAX_POINTS 16
#define BUFF_FOCUS_ATTRIBUTE_POOL (-2)
#define BUFF_FOCUS_HITPOINTS_RAISE 5 /* stat_raise slots of the Hitpoints and Mana raises */
#define BUFF_FOCUS_MANA_RAISE 7

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
    int armor_feeds_focused; /* the focused Resistance or Immunity gains shown AV / 10 */
    CombatModelInput combat; /* read only when armor_feeds_focused */
    BuffModelSkill combat_skills[COMBAT_SKILL_COUNT];
    int combat_raise_index[COMBAT_SKILL_COUNT];
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
