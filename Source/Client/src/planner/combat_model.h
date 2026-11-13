#pragma once

/*
 * What the skill planner shows for Hit, Parry, Weapon Value and Armor Value.
 *
 * The server only sends the totals, so each stat is shown as the server's
 * value plus what every combat skill adds at its shown (planned, buffed)
 * value, minus what it adds now. The client can't see what is equipped, so
 * the weapon and off-hand are chosen in the planner. Self-contained (no game
 * headers) so it can be built into the unit tests.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COMBAT_SKILL_HAND,
    COMBAT_SKILL_DAGGER,
    COMBAT_SKILL_SWORD,
    COMBAT_SKILL_AXE,
    COMBAT_SKILL_STAFF,
    COMBAT_SKILL_TWOHAND,
    COMBAT_SKILL_DUAL,
    COMBAT_SKILL_SHIELD,
    COMBAT_SKILL_TACTICS,
    COMBAT_SKILL_GEARMAST,
    COMBAT_SKILL_FEROCITY,
    COMBAT_SKILL_COUNT,
} CombatSkill;

typedef enum {
    COMBAT_WEAPON_UNARMED,
    COMBAT_WEAPON_CLAW,
    COMBAT_WEAPON_DAGGER,
    COMBAT_WEAPON_SWORD,
    COMBAT_WEAPON_AXE,
    COMBAT_WEAPON_STAFF,
    COMBAT_WEAPON_TWOHAND,
    COMBAT_WEAPON_SPEAR,
    COMBAT_WEAPON_GREATAXE,
    COMBAT_WEAPON_COUNT,
} CombatWeapon;

typedef enum { COMBAT_OFFHAND_NONE, COMBAT_OFFHAND_SHIELD, COMBAT_OFFHAND_DUAL, COMBAT_OFFHAND_COUNT } CombatOffhand;

/* Gear Mastery divisors: OTHER 10/10, TEMPLAR_OR_BRAVER 5/5, ARCHTEMPLAR 4 WV / 3 AV, SKALD 3 WV / 4 AV. */
typedef enum {
    COMBAT_GEAR_OTHER,
    COMBAT_GEAR_TEMPLAR_OR_BRAVER,
    COMBAT_GEAR_ARCHTEMPLAR,
    COMBAT_GEAR_SKALD,
} CombatGearClass;

typedef struct {
    int known[COMBAT_SKILL_COUNT];
    int current[COMBAT_SKILL_COUNT]; /* server totals */
    int shown[COMBAT_SKILL_COUNT]; /* planned, with shown buffs */
    CombatWeapon weapon;
    CombatOffhand offhand;
    CombatGearClass gear_class;
    int rampart; /* the Shield off-hand gives a quarter and an eighth instead of a quarter */
    int lycan; /* Ferocity applies */
    int empty_slots; /* empty gear slots, which make Ferocity stronger */
    int server_hit;
    int server_parry;
    int base_weapon; /* weapon value before skill changes, shown buffs included */
    int base_armor;
    int best_weapon_skill; /* Hit and Parry use the highest weapon skill, whatever the weapon */
    int hit_feeds_parry;   /* Parry gains part of Hit after the tarot steps; read only by skills_affecting */
    int parry_feeds_hit;   /* Hit gains part of Parry, likewise */
} CombatModelInput;

typedef struct {
    int hit;
    int parry;
    int weapon;
    int armor;
} CombatValues;

typedef enum {
    COMBAT_STAT_HIT,
    COMBAT_STAT_PARRY,
    COMBAT_STAT_WEAPON,
    COMBAT_STAT_ARMOR,
    COMBAT_STAT_COUNT,
} CombatStat;

extern const char *const COMBAT_WEAPON_NAMES[COMBAT_WEAPON_COUNT];
extern const char *const COMBAT_OFFHAND_NAMES[COMBAT_OFFHAND_COUNT];
extern const char *const COMBAT_STAT_NAMES[COMBAT_STAT_COUNT];

int combat_model_stat_value(const CombatValues *values, CombatStat stat);

/* Marks the known skills whose value can move the stat with the chosen weapon, off-hand and race. Under the best
 * weapon skill, only weapon skills at most weapon_skill_reach below the best are marked. */
void combat_model_skills_affecting(const CombatModelInput *input, CombatStat stat, int weapon_skill_reach,
                                   int affects[COMBAT_SKILL_COUNT]);

/* The weapon of the highest known weapon skill; Unarmed when none is known. */
CombatWeapon combat_model_default_weapon(const int known[COMBAT_SKILL_COUNT], const int current[COMBAT_SKILL_COUNT]);

/* The higher known of Shield and Dual Wield; None when neither is known. */
CombatOffhand combat_model_default_offhand(const int known[COMBAT_SKILL_COUNT], const int current[COMBAT_SKILL_COUNT]);

void combat_model_compute(const CombatModelInput *input, CombatValues *result);

#ifdef __cplusplus
}
#endif
