#pragma once

#include "planner/buff_focus.h"
#include "planner/combat_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bounds the search when the stat can't be raised, so it always finishes. */
#define COMBAT_FOCUS_MAX_STATES 65536

typedef struct {
    BuffModelInput model; /* planner state, plan included */
    /* Weapon, off-hand, race and server values; the shown skills and base weapon and armor are recomputed. */
    CombatModelInput combat;
    BuffModelSkill combat_skills[COMBAT_SKILL_COUNT];
    int combat_raise_index[COMBAT_SKILL_COUNT]; /* stat_raise slot of each combat skill, -1 when not known */
    int spell_raise_index[BUFF_KIND_COUNT]; /* stat_raise slot of each spell skill, -1 when not known */
    CombatStat stat;
    int candidate_count;
    BuffFocusCandidate candidates[BUFF_FOCUS_MAX_CANDIDATES];
} CombatFocusInput;

typedef struct {
    int current_value; /* the stat as the Combat block shows it */
    BuffFocusRoute best; /* cheapest route to +1; ties go to fewer points */
} CombatFocusResult;

CombatFocusResult combat_focus_search(const CombatFocusInput *input);

#ifdef __cplusplus
}
#endif
