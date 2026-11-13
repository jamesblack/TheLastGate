#include "combat_shown.h"

#include "planner/tarot_math.h"

CombatValues combat_shown_values(const BuffModelInput *model, const BuffModelResult *buffed,
                                 const CombatModelInput *combat,
                                 const BuffModelSkill combat_skills[COMBAT_SKILL_COUNT]) {
    CombatModelInput planned = *combat;
    CombatValues raw;
    CombatValues server;
    CombatValues raw_change;
    int skill_id;

    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
        if (planned.known[skill_id]) {
            planned.shown[skill_id] = buff_model_skill_value(model, &combat_skills[skill_id], buffed->attributes);
        }
    }
    planned.base_weapon = buffed->weapon;
    planned.base_armor = buffed->armor;
    combat_model_compute(&planned, &raw);

    server.hit = combat->server_hit;
    server.parry = combat->server_parry;
    server.weapon = model->server_weapon;
    server.armor = model->server_armor;
    raw_change.hit = raw.hit - server.hit;
    raw_change.parry = raw.parry - server.parry;
    raw_change.weapon = raw.weapon - server.weapon;
    raw_change.armor = raw.armor - server.armor;
    return tarot_combat_shown(model->tarot_cards, &model->effects, server, raw_change);
}
