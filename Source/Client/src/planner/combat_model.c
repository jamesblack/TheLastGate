#include "combat_model.h"

#include "util/array_util.h"
#include "util/math_util.h"

#define COMBAT_WEAPON_SKILL_CAP 300
#define COMBAT_STAT_MAX 999
#define TACTICS_HIT_PARRY_DIVISOR 6
#define RAMPART_SHIELD_DIVISOR 8
#define GEAR_MASTERY_DIVISOR_TEMPLAR_OR_BRAVER 5
#define GEAR_MASTERY_DIVISOR_OTHER 10
#define FEROCITY_EMPTY_SLOT_DIVISOR 10

const char *const COMBAT_WEAPON_NAMES[COMBAT_WEAPON_COUNT] = {
    "Unarmed", "Claw", "Dagger", "Sword", "Axe", "Staff", "Two-Handed", "Spear", "Greataxe",
};

const char *const COMBAT_OFFHAND_NAMES[COMBAT_OFFHAND_COUNT] = {"None", "Shield", "Dual Wield"};

const char *const COMBAT_STAT_NAMES[COMBAT_STAT_COUNT] = {"Hit Score", "Parry Score", "Weapon Value", "Armor Value"};

/* Weapon skills a default can come from, with the weapon each one stands for; ties go to the earlier. */
static const struct {
    CombatSkill skill_id;
    CombatWeapon weapon;
} DEFAULT_WEAPONS[] = {
    {COMBAT_SKILL_HAND, COMBAT_WEAPON_UNARMED}, {COMBAT_SKILL_DAGGER, COMBAT_WEAPON_DAGGER},
    {COMBAT_SKILL_SWORD, COMBAT_WEAPON_SWORD},  {COMBAT_SKILL_AXE, COMBAT_WEAPON_AXE},
    {COMBAT_SKILL_STAFF, COMBAT_WEAPON_STAFF},  {COMBAT_SKILL_TWOHAND, COMBAT_WEAPON_TWOHAND},
};

CombatWeapon combat_model_default_weapon(const int known[COMBAT_SKILL_COUNT], const int current[COMBAT_SKILL_COUNT]) {
    CombatWeapon best_weapon = COMBAT_WEAPON_UNARMED;
    int best_value = -1;
    int entry;

    for (entry = 0; entry < ARRAY_SIZE(DEFAULT_WEAPONS); entry++) {
        const CombatSkill skill_id = DEFAULT_WEAPONS[entry].skill_id;

        if (known[skill_id] && current[skill_id] > best_value) {
            best_value = current[skill_id];
            best_weapon = DEFAULT_WEAPONS[entry].weapon;
        }
    }
    return best_weapon;
}

CombatOffhand combat_model_default_offhand(const int known[COMBAT_SKILL_COUNT], const int current[COMBAT_SKILL_COUNT]) {
    if (known[COMBAT_SKILL_SHIELD] &&
        (!known[COMBAT_SKILL_DUAL] || current[COMBAT_SKILL_SHIELD] >= current[COMBAT_SKILL_DUAL])) {
        return COMBAT_OFFHAND_SHIELD;
    }
    if (known[COMBAT_SKILL_DUAL]) return COMBAT_OFFHAND_DUAL;
    return COMBAT_OFFHAND_NONE;
}

/* A skill's value on one side (current or shown); 0 when the character doesn't have it. */
static int skill_value(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT],
                       const CombatSkill skill_id) {
    return input->known[skill_id] ? skills[skill_id] : 0;
}

/* What the weapon's skill adds to both Hit and Parry. */
static int weapon_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    int value;

    switch (input->weapon) {
        case COMBAT_WEAPON_DAGGER: value = skill_value(input, skills, COMBAT_SKILL_DAGGER); break;
        case COMBAT_WEAPON_SWORD: value = skill_value(input, skills, COMBAT_SKILL_SWORD); break;
        case COMBAT_WEAPON_AXE: value = skill_value(input, skills, COMBAT_SKILL_AXE); break;
        case COMBAT_WEAPON_STAFF: value = skill_value(input, skills, COMBAT_SKILL_STAFF); break;
        case COMBAT_WEAPON_TWOHAND: value = skill_value(input, skills, COMBAT_SKILL_TWOHAND); break;
        case COMBAT_WEAPON_SPEAR:
            value = max(skill_value(input, skills, COMBAT_SKILL_DAGGER),
                        skill_value(input, skills, COMBAT_SKILL_STAFF));
            break;
        case COMBAT_WEAPON_GREATAXE:
            value = max(skill_value(input, skills, COMBAT_SKILL_AXE), skill_value(input, skills, COMBAT_SKILL_TWOHAND));
            break;
        default: value = skill_value(input, skills, COMBAT_SKILL_HAND); break;
    }
    return min(value, COMBAT_WEAPON_SKILL_CAP);
}

static int hit_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    int term =
            weapon_term(input, skills) + (skill_value(input, skills, COMBAT_SKILL_TACTICS) / TACTICS_HIT_PARRY_DIVISOR);

    if (input->offhand == COMBAT_OFFHAND_DUAL) {
        term += min(skill_value(input, skills, COMBAT_SKILL_DUAL), COMBAT_WEAPON_SKILL_CAP) / 4;
    }
    return term;
}

static int parry_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    int term =
            weapon_term(input, skills) + (skill_value(input, skills, COMBAT_SKILL_TACTICS) / TACTICS_HIT_PARRY_DIVISOR);

    if (input->offhand == COMBAT_OFFHAND_SHIELD) {
        const int shield = min(skill_value(input, skills, COMBAT_SKILL_SHIELD), COMBAT_WEAPON_SKILL_CAP);

        term += input->rampart ? (shield / 4) + (shield / RAMPART_SHIELD_DIVISOR) : shield / 4;
    }
    return term;
}

static int gear_mastery_divisor(const CombatGearClass gear_class, const int for_armor) {
    switch (gear_class) {
        case COMBAT_GEAR_TEMPLAR_OR_BRAVER: return GEAR_MASTERY_DIVISOR_TEMPLAR_OR_BRAVER;
        case COMBAT_GEAR_ARCHTEMPLAR: return for_armor ? 3 : 4;
        case COMBAT_GEAR_SKALD: return for_armor ? 4 : 3;
        default: return GEAR_MASTERY_DIVISOR_OTHER;
    }
}

static int ferocity_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    const int ferocity = skill_value(input, skills, COMBAT_SKILL_FEROCITY);

    if (!input->lycan) return 0;
    return ((ferocity / 2) + (ferocity * input->empty_slots / FEROCITY_EMPTY_SLOT_DIVISOR)) / 4;
}

static int weapon_value_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    int term = (skill_value(input, skills, COMBAT_SKILL_GEARMAST) / gear_mastery_divisor(input->gear_class, 0)) +
               ferocity_term(input, skills);

    if (input->weapon == COMBAT_WEAPON_UNARMED) term += skill_value(input, skills, COMBAT_SKILL_HAND) / 4;
    return term;
}

static int armor_value_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    return (skill_value(input, skills, COMBAT_SKILL_GEARMAST) / gear_mastery_divisor(input->gear_class, 1)) +
           ferocity_term(input, skills);
}

void combat_model_compute(const CombatModelInput *input, CombatValues *result) {
    const int hit = input->server_hit + hit_term(input, input->shown) - hit_term(input, input->current);
    const int parry = input->server_parry + parry_term(input, input->shown) - parry_term(input, input->current);
    const int weapon =
            input->base_weapon + weapon_value_term(input, input->shown) - weapon_value_term(input, input->current);
    const int armor =
            input->base_armor + armor_value_term(input, input->shown) - armor_value_term(input, input->current);

    result->hit = clamp(hit, 0, COMBAT_STAT_MAX);
    result->parry = clamp(parry, 0, COMBAT_STAT_MAX);
    result->weapon = clamp(weapon, 0, COMBAT_STAT_MAX);
    result->armor = clamp(armor, 0, COMBAT_STAT_MAX);
}

int combat_model_stat_value(const CombatValues *values, const CombatStat stat) {
    switch (stat) {
        case COMBAT_STAT_HIT: return values->hit;
        case COMBAT_STAT_PARRY: return values->parry;
        case COMBAT_STAT_WEAPON: return values->weapon;
        case COMBAT_STAT_ARMOR: return values->armor;
        default: return 0;
    }
}

static void mark_weapon_skills(const CombatWeapon weapon, int affects[COMBAT_SKILL_COUNT]) {
    switch (weapon) {
        case COMBAT_WEAPON_DAGGER: affects[COMBAT_SKILL_DAGGER] = 1; break;
        case COMBAT_WEAPON_SWORD: affects[COMBAT_SKILL_SWORD] = 1; break;
        case COMBAT_WEAPON_AXE: affects[COMBAT_SKILL_AXE] = 1; break;
        case COMBAT_WEAPON_STAFF: affects[COMBAT_SKILL_STAFF] = 1; break;
        case COMBAT_WEAPON_TWOHAND: affects[COMBAT_SKILL_TWOHAND] = 1; break;
        case COMBAT_WEAPON_SPEAR:
            affects[COMBAT_SKILL_DAGGER] = 1;
            affects[COMBAT_SKILL_STAFF] = 1;
            break;
        case COMBAT_WEAPON_GREATAXE:
            affects[COMBAT_SKILL_AXE] = 1;
            affects[COMBAT_SKILL_TWOHAND] = 1;
            break;
        default: affects[COMBAT_SKILL_HAND] = 1; break;
    }
}

void combat_model_skills_affecting(const CombatModelInput *input, const CombatStat stat,
                                   int affects[COMBAT_SKILL_COUNT]) {
    int skill_id;

    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) affects[skill_id] = 0;
    switch (stat) {
        case COMBAT_STAT_HIT:
        case COMBAT_STAT_PARRY:
            mark_weapon_skills(input->weapon, affects);
            affects[COMBAT_SKILL_TACTICS] = 1;
            if (stat == COMBAT_STAT_HIT && input->offhand == COMBAT_OFFHAND_DUAL) affects[COMBAT_SKILL_DUAL] = 1;
            if (stat == COMBAT_STAT_PARRY && input->offhand == COMBAT_OFFHAND_SHIELD) {
                affects[COMBAT_SKILL_SHIELD] = 1;
            }
            break;
        case COMBAT_STAT_WEAPON:
        case COMBAT_STAT_ARMOR:
            affects[COMBAT_SKILL_GEARMAST] = 1;
            if (input->lycan) affects[COMBAT_SKILL_FEROCITY] = 1;
            if (stat == COMBAT_STAT_WEAPON && input->weapon == COMBAT_WEAPON_UNARMED) affects[COMBAT_SKILL_HAND] = 1;
            break;
        default: break;
    }
    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
        if (!input->known[skill_id]) affects[skill_id] = 0;
    }
}
