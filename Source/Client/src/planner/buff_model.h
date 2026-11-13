#pragma once

/*
 * What the skill planner shows with self-buffs toggled on or off.
 *
 * Bless is modelled as two casts: the first from the unbuffed (planned)
 * stats, the second from the stats the first one gave, which raises the
 * Bless skill through its own attributes. Spell power is
 * (skill + Spell Bonus) x Spell Multiplier, capped at twice the Spell
 * Aptitude; aptitude only follows base Willpower and Intuition. Buffs already
 * on the player are taken out of the server's totals first, using their
 * fetched power. Self-contained (no game headers) so it can be built into the
 * unit tests.
 */

#include "planner/tarot_math.h"
#include "planner/tree_effects.h"
#include "mods/buff_list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUFF_SKILL_SCALE_NONE = 0,
    BUFF_SKILL_SCALE_RESIST = 1,
    BUFF_SKILL_SCALE_TACTICS = 2,
    BUFF_SKILL_SCALE_IMMUNITY = 3,
} BuffSkillScale;

typedef struct {
    int server_total; /* the skill's value as the server sends it */
    int planned; /* points planned for the skill itself */
    int attribute_ids[ATTRIBUTES_PER_SKILL]; /* -1 marks an unused slot */
    int scale_kind;
    int skill_groups; /* SkillGroup mask */
} BuffModelSkill;

typedef struct {
    int server_attributes[ATTRIBUTE_COUNT]; /* totals as the server sends them */
    int base_attributes[ATTRIBUTE_COUNT]; /* bare values, without gear or buffs */
    int planned_attributes[ATTRIBUTE_COUNT];
    int divisor; /* 5, or 6 in hard mode */
    int spell_multiplier; /* hundredths, 100 = 1.00x */
    int spell_bonus;
    int spell_aptitude; /* server total */
    int race_modifier; /* percent */
    int reduced_item_buffs; /* Enhance and Protect give power/6+3 instead of power/4+4 */
    int server_weapon;
    int server_armor;
    int spell_known[BUFF_KIND_COUNT];
    BuffModelSkill spell_skills[BUFF_KIND_COUNT];
    BuffReading active[BUFF_KIND_COUNT]; /* known == 0 when that buff isn't on the player */
    int show_buffed[BUFF_KIND_COUNT];
    TarotSet tarot_cards;
    int uncapped_mana;
    int planned_mana;
    TreeEffects effects;
} BuffModelInput;

typedef struct {
    int attributes[ATTRIBUTE_COUNT]; /* the values the planner shows, plan included */
    int spell_aptitude; /* aptitude with the planned base Willpower and Intuition */
    int cast_power[BUFF_KIND_COUNT]; /* your own planned cast (the second one for Bless); 0 if unknown */
    int effective_power[BUFF_KIND_COUNT]; /* power shown: the stronger of cast and active when shown, else 0 */
    int weapon;
    int armor;
} BuffModelResult;

/* What Bless adds to one attribute (0 Brv .. 4 Str) at a power; 0 for no power. */
int buff_model_bless_bonus(int power, int attribute_id);

/* What Enhance adds to weapon value, or Protect to armor value, at a power; 0 for no power. */
int buff_model_item_bonus(int power, int reduced);

/* The skill's value with these attribute values, its own planned points included. */
int buff_model_skill_value(const BuffModelInput *input, const BuffModelSkill *skill,
                           const int attributes[ATTRIBUTE_COUNT]);

void buff_model_compute(const BuffModelInput *input, BuffModelResult *result);

#ifdef __cplusplus
}
#endif
