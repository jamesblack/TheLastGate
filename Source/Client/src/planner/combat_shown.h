#pragma once

#include "planner/buff_model.h"
#include "planner/combat_model.h"

#ifdef __cplusplus
extern "C" {
#endif

CombatValues combat_shown_values(const BuffModelInput *model, const BuffModelResult *buffed,
                                 const CombatModelInput *combat,
                                 const BuffModelSkill combat_skills[COMBAT_SKILL_COUNT]);

#ifdef __cplusplus
}
#endif
