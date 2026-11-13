#pragma once

#include "planner/buff_model.h"
#include "planner/combat_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hit, Parry, Weapon and Armor Value as shown: combat skills at their model values, then cards and item steps. */
CombatValues combat_shown_values(const BuffModelInput *model, const BuffModelResult *buffed,
                                 const CombatModelInput *combat,
                                 const BuffModelSkill combat_skills[COMBAT_SKILL_COUNT]);

#ifdef __cplusplus
}
#endif
