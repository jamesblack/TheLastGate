#pragma once

/*
 * The planner's Weapon and Off-hand picks, kept per character in a small file
 * so they survive between sessions.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define PLANNER_CHOICE_NONE (-1) /* nothing picked yet: follow the character's best skill */

typedef struct {
    int weapon;  /* a CombatWeapon, or PLANNER_CHOICE_NONE */
    int offhand; /* a CombatOffhand, or PLANNER_CHOICE_NONE */
    unsigned int spell_toggles; /* bit 1 << BuffKind is set when that spell is shown buffed */
} PlannerChoices;

/* The picks saved for this character. Missing, unreadable or out-of-range values come back as PLANNER_CHOICE_NONE,
 * as does everything for character number 0 (not yet created). */
PlannerChoices planner_choices_load(const char *path, unsigned int character_number);

/* Stores this character's picks, keeping every other character's. Returns 1 when the file was written. */
int planner_choices_save(const char *path, unsigned int character_number, PlannerChoices choices);

#ifdef __cplusplus
}
#endif
