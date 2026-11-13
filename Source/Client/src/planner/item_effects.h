#pragma once

#include "planner/tree_effects.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ITEM_EFFECT_CAP 200

typedef enum { /* UI order */
               ENCHANT_MORE_BRAVENESS,
               ENCHANT_MORE_WILLPOWER,
               ENCHANT_MORE_INTUITION,
               ENCHANT_MORE_AGILITY,
               ENCHANT_MORE_STRENGTH,
               ENCHANT_MORE_ATTRIBUTES,
               ENCHANT_MORE_SPELL_BONUS,
               ENCHANT_HIT_TO_PARRY,
               ENCHANT_PARRY_TO_HIT,
               ENCHANT_MORE_ARMOR,
               ENCHANT_MORE_RESISTANCE,
               ENCHANT_ARMOR_TO_RESISTANCE,
               ENCHANT_ARMOR_TO_IMMUNITY,
               ENCHANT_COUNT,
} Enchant;

typedef struct {
    int server_number;
    const char *name;
    const char *one_copy; /* at most 16 characters */
    int counted;          /* 0: a tickbox only; one copy at most */
} EnchantInfo;

extern const EnchantInfo ENCHANT_INFO[ENCHANT_COUNT];

typedef enum { /* UI order */
               GEAR_IMPROVED_VIOLET_GAZE,
               GEAR_IMPROVED_LIONS_PAWS,
               GEAR_IMPROVED_COBALT_LANCE,
               GEAR_HALADIE,
               GEAR_IMPROVED_SHIVAS_SCEPTER,
               GEAR_MURAMASA,
               GEAR_IMPROVED_BARBARIAN_SWORD,
               GEAR_IMPROVED_FELL_NIGHT,
               GEAR_IMPROVED_AJAX_BLUE,
               GEAR_TRAVELERS_GUIDE,
               GEAR_FISTS_OF_THE_HEAVENS,
               GEAR_GRANDE_GRIMOIRE,
               GEAR_IMPROVED_BLACK_TACHI,
               GEAR_IMPROVED_WHITE_ODACHI,
               GEAR_EVERGREEN,
               GEAR_GESTURE_OF_GORN,
               GEAR_IMPROVED_NEIS_CLAW,
               GEAR_SUPERBIA,
               GEAR_CUDGEL_OF_KWAI,
               GEAR_QUICKSILVER,
               GEAR_EXCALIBUR,
               GEAR_COUNT,
} Gear;

typedef struct {
    const char *name;
    const char *effect;
    int modelled; /* 0 for Cudgel of Kwai, Quicksilver and Excalibur */
} GearInfo;

extern const GearInfo GEAR_INFO[GEAR_COUNT];

typedef struct {
    unsigned int enchant_ticks;                  /* bit 1 << Enchant */
    unsigned char enchant_counts[ENCHANT_COUNT]; /* kept when unticked */
    unsigned int gear_ticks;                     /* bit 1 << Gear */
} ItemEffectsInput;

int item_effects_enchant_copies(const ItemEffectsInput *input, Enchant enchant); /* 0 unticked, 1 if not counted */
void item_effects_compute(const TreeEffectsInput *tree_input, const ItemEffectsInput *item_input,
                          PlannerEffects *effects);

#ifdef __cplusplus
}
#endif
