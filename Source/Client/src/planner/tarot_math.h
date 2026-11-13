#pragma once

#include <stdint.h>

#include "planner/combat_model.h"
#include "planner/tree_effects.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAROT_CARD_COUNT 44
#define TAROT_TEMPERANCE_SICKNESS_STACKS 4
#define FOOL_POINTS_PER_BREAKPOINT 5

typedef uint64_t TarotSet;

typedef enum {
    TAROT_MAGICIAN = 1,
    TAROT_LOVERS = 6,
    TAROT_HERMIT = 9,
    TAROT_FOOL_REVERSED = 22,
    TAROT_LOVERS_REVERSED = 28,
    TAROT_STRENGTH_REVERSED = 30,
    TAROT_HANGED_REVERSED = 34,
    TAROT_TEMPERANCE_REVERSED = 36,
    TAROT_STAR_REVERSED = 39,
    TAROT_MOON_REVERSED = 40,
} TarotCard;

#define TAROT_MODELLED_CARD_COUNT 10
/* The cards the planner's math reads, in card order; the rest change nothing it shows. */
extern const int TAROT_MODELLED_CARDS[TAROT_MODELLED_CARD_COUNT];

int tarot_set_has(TarotSet cards, int card_index);

int tarot_floor_divide(int dividend, int divisor);

int tarot_fool_reversed_total(int shown_total, int sum_change);

int tarot_scaled_change(int server_value, int raw_change, int numerator, int denominator);

CombatValues tarot_combat_shown(TarotSet cards, const PlannerEffects *effects, CombatValues server,
                                CombatValues raw_change);

#ifdef __cplusplus
}
#endif
