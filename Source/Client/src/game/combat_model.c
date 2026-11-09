#include "combat_model.h"

#define COMBAT_WEAPON_SKILL_CAP 300
#define COMBAT_STAT_MAX 999

const char *const COMBAT_WEAPON_NAMES[COMBAT_WEAPON_COUNT] = {
    "Unarmed", "Claw", "Dagger", "Sword", "Axe", "Staff", "Two-Handed", "Spear", "Greataxe",
};

const char *const COMBAT_OFFHAND_NAMES[COMBAT_OFFHAND_COUNT] = {"None", "Shield", "Dual Wield"};

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
    unsigned int entry;

    for (entry = 0; entry < sizeof(DEFAULT_WEAPONS) / sizeof(DEFAULT_WEAPONS[0]); entry++) {
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

static int larger(const int first, const int second) {
    return first > second ? first : second;
}

static int clamp_stat(const int value) {
    if (value < 0) return 0;
    if (value > COMBAT_STAT_MAX) return COMBAT_STAT_MAX;
    return value;
}

/* A skill's value on one side (current or shown); 0 when the character doesn't have it. */
static int skill_value(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT],
                       const CombatSkill skill_id) {
    return input->known[skill_id] ? skills[skill_id] : 0;
}

static int capped_skill(const int value) {
    return value < COMBAT_WEAPON_SKILL_CAP ? value : COMBAT_WEAPON_SKILL_CAP;
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
            value = larger(skill_value(input, skills, COMBAT_SKILL_DAGGER),
                           skill_value(input, skills, COMBAT_SKILL_STAFF));
            break;
        case COMBAT_WEAPON_GREATAXE:
            value = larger(skill_value(input, skills, COMBAT_SKILL_AXE),
                           skill_value(input, skills, COMBAT_SKILL_TWOHAND));
            break;
        default: value = skill_value(input, skills, COMBAT_SKILL_HAND); break;
    }
    return capped_skill(value);
}

static int hit_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    int term = weapon_term(input, skills) + skill_value(input, skills, COMBAT_SKILL_TACTICS) / 6;

    if (input->offhand == COMBAT_OFFHAND_DUAL) term += capped_skill(skill_value(input, skills, COMBAT_SKILL_DUAL)) / 4;
    return term;
}

static int parry_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    int term = weapon_term(input, skills) + skill_value(input, skills, COMBAT_SKILL_TACTICS) / 6;

    if (input->offhand == COMBAT_OFFHAND_SHIELD) {
        const int shield = capped_skill(skill_value(input, skills, COMBAT_SKILL_SHIELD));

        term += input->rampart ? shield / 4 + shield / 8 : shield / 4;
    }
    return term;
}

static int gear_mastery_divisor(const CombatGearClass gear_class, const int for_armor) {
    switch (gear_class) {
        case COMBAT_GEAR_TEMPLAR_OR_BRAVER: return 5;
        case COMBAT_GEAR_ARCHTEMPLAR: return for_armor ? 3 : 4;
        case COMBAT_GEAR_SKALD: return for_armor ? 4 : 3;
        default: return 10;
    }
}

static int ferocity_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    const int ferocity = skill_value(input, skills, COMBAT_SKILL_FEROCITY);

    if (!input->lycan) return 0;
    return (ferocity / 2 + ferocity * input->empty_slots / 10) / 4;
}

static int weapon_value_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    int term = skill_value(input, skills, COMBAT_SKILL_GEARMAST) / gear_mastery_divisor(input->gear_class, 0) +
               ferocity_term(input, skills);

    if (input->weapon == COMBAT_WEAPON_UNARMED) term += skill_value(input, skills, COMBAT_SKILL_HAND) / 4;
    return term;
}

static int armor_value_term(const CombatModelInput *input, const int skills[COMBAT_SKILL_COUNT]) {
    return skill_value(input, skills, COMBAT_SKILL_GEARMAST) / gear_mastery_divisor(input->gear_class, 1) +
           ferocity_term(input, skills);
}

void combat_model_compute(const CombatModelInput *input, CombatModelResult *result) {
    result->hit = clamp_stat(input->server_hit + hit_term(input, input->shown) - hit_term(input, input->current));
    result->parry =
            clamp_stat(input->server_parry + parry_term(input, input->shown) - parry_term(input, input->current));
    result->weapon = clamp_stat(input->base_weapon + weapon_value_term(input, input->shown) -
                                weapon_value_term(input, input->current));
    result->armor = clamp_stat(input->base_armor + armor_value_term(input, input->shown) -
                               armor_value_term(input, input->current));
}
