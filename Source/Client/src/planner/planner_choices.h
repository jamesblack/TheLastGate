#pragma once

/*
 * The planner's picks (Weapon and Off-hand, spell toggles, tarot cards and
 * corruptors), kept per character in a small file so they survive between
 * sessions.
 */

#include "planner/tarot_math.h"
#include "planner/tree_effects.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLANNER_CHOICE_NONE (-1) /* nothing picked yet: follow the character's best skill */

typedef struct {
    int weapon;  /* a CombatWeapon, or PLANNER_CHOICE_NONE */
    int offhand; /* a CombatOffhand, or PLANNER_CHOICE_NONE */
    unsigned int spell_toggles; /* bit 1 << BuffKind is set when that spell is shown buffed */
    TarotSet tarot_cards;
    unsigned int corruptor_ticks;                    /* bit 1 << Corruptor is set when that corruptor is ticked */
    unsigned char corruptor_counts[CORRUPTOR_COUNT]; /* card and ring copies, kept when unticked */
} PlannerChoices;

/* The picks saved for this character. Missing, unreadable or out-of-range values come back as PLANNER_CHOICE_NONE,
 * as does everything for character number 0 (not yet created). */
PlannerChoices planner_choices_load(const char *path, unsigned int character_number);

/* Stores this character's picks, keeping every other character's. Returns 1 when the file was written. */
int planner_choices_save(const char *path, unsigned int character_number, PlannerChoices choices);

#ifdef __cplusplus
}
#endif
