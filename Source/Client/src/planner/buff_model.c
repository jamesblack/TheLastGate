#include "buff_model.h"

#include "game/attributes.h"
#include "planner/tarot_math.h"
#include "util/math_util.h"

#define ITEM_BONUS_CAP 127
#define MOON_MANA_PER_PERCENT 50
#define BLESS_BONUS_DIVISOR 5
#define ITEM_BONUS_DIVISOR 4
#define REDUCED_ITEM_BONUS_DIVISOR 6
#define HERMIT_RESIST_NUMERATOR 4
#define HERMIT_RESIST_DENOMINATOR 5
#define APTITUDE_ATTRIBUTE_FACTOR 5
#define INTROSPECTION_APTITUDE_DIVISOR 50
#define GUARDIAN_ANGEL_DIVISOR 20

int buff_model_bless_bonus(const int power, const int attribute_id) {
    if (power <= 0) return 0;
    return min(ITEM_BONUS_CAP, (((power * 2 / 3) - attribute_id) / BLESS_BONUS_DIVISOR) + 3);
}

int buff_model_item_bonus(const int power, const int reduced) {
    if (power <= 0) return 0;
    if (reduced) return min(ITEM_BONUS_CAP, (power / REDUCED_ITEM_BONUS_DIVISOR) + 3);
    return min(ITEM_BONUS_CAP, (power / ITEM_BONUS_DIVISOR) + 4);
}

static int attribute_sum(const BuffModelSkill *skill, const int attributes[ATTRIBUTE_COUNT]) {
    int slot;
    int total = 0;

    for (slot = 0; slot < ATTRIBUTES_PER_SKILL; slot++) {
        const int attribute_id = skill->attribute_ids[slot];
        if (attribute_id >= 0 && attribute_id < ATTRIBUTE_COUNT) total += attributes[attribute_id];
    }
    return total;
}

static int skill_extra_bonus(const BuffModelInput *input, const BuffModelSkill *skill,
                             const int attributes[ATTRIBUTE_COUNT]) {
    const int *extra_percent = input->effects.skill_extra_percent;
    int extra = 0;

    if (skill->skill_groups & SKILL_GROUP_PASSIVE) {
        extra += extra_percent[ATTRIBUTE_BRAVENESS] * attributes[ATTRIBUTE_BRAVENESS];
    }
    if (skill->skill_groups & SKILL_GROUP_ACTIVE_SPELL) {
        extra += (extra_percent[ATTRIBUTE_WILLPOWER] * attributes[ATTRIBUTE_WILLPOWER]) +
                 (extra_percent[ATTRIBUTE_INTUITION] * attributes[ATTRIBUTE_INTUITION]);
    }
    if (skill->skill_groups & SKILL_GROUP_ACTIVE_MELEE) {
        extra += (extra_percent[ATTRIBUTE_AGILITY] * attributes[ATTRIBUTE_AGILITY]) +
                 (extra_percent[ATTRIBUTE_STRENGTH] * attributes[ATTRIBUTE_STRENGTH]);
    }
    return extra;
}

static int attribute_term(const BuffModelInput *input, const BuffModelSkill *skill,
                          const int attributes[ATTRIBUTE_COUNT]) {
    return (attribute_sum(skill, attributes) + (skill_extra_bonus(input, skill, attributes) / PERCENT_BASE)) /
           input->divisor;
}

static int resistance_shown(const int raw_value, const int percent, const int hermit) {
    const int raised = raw_value * (PERCENT_BASE + percent) / PERCENT_BASE;

    return hermit ? raised * HERMIT_RESIST_NUMERATOR / HERMIT_RESIST_DENOMINATOR : raised;
}

/*
 * The server raises the whole skill by its percent, then Hermit takes a fifth off, rounding down after each step.
 * The raw value is recovered in one division, which loses less to rounding than undoing the steps one by one.
 */
static int resistance_value(const BuffModelInput *input, const BuffModelSkill *skill, const int percent,
                            const int change) {
    const int hermit = tarot_set_has(input->tarot_cards, TAROT_HERMIT);
    const int numerator = (PERCENT_BASE + percent) * (hermit ? HERMIT_RESIST_NUMERATOR : 1);
    const int denominator = PERCENT_BASE * (hermit ? HERMIT_RESIST_DENOMINATOR : 1);
    const int raw_value = skill->server_total * denominator / numerator;

    return skill->server_total + resistance_shown(raw_value + change, percent, hermit) -
           resistance_shown(raw_value, percent, hermit);
}

static int moon_scaled_tactics(const BuffModelInput *input, const int server_total, const int change) {
    const int percent_scale = PERCENT_BASE * MOON_MANA_PER_PERCENT;
    const int raw_value = server_total * percent_scale / (percent_scale + input->uncapped_mana);

    return (raw_value + change) * (percent_scale + input->uncapped_mana + input->planned_mana) / percent_scale;
}

int buff_model_skill_value(const BuffModelInput *input, const BuffModelSkill *skill,
                           const int attributes[ATTRIBUTE_COUNT]) {
    const int change = skill->planned + attribute_term(input, skill, attributes) -
                       attribute_term(input, skill, input->server_attributes);

    if (skill->scale_kind == BUFF_SKILL_SCALE_RESIST) {
        return resistance_value(input, skill, input->effects.resist_percent, change);
    }
    if (skill->scale_kind == BUFF_SKILL_SCALE_IMMUNITY) {
        return resistance_value(input, skill, input->effects.immunity_percent, change);
    }
    if (skill->scale_kind == BUFF_SKILL_SCALE_TACTICS && tarot_set_has(input->tarot_cards, TAROT_MOON_REVERSED)) {
        return moon_scaled_tactics(input, skill->server_total, change);
    }
    return skill->server_total + change;
}

/*
 * Under Reverse Fool the server averages all five attributes, so only the sum of the changes matters. The
 * attribute percent is applied after that averaging, so the averaging works on the total without it.
 */
static void attributes_with_changes(const BuffModelInput *input, const int changes[ATTRIBUTE_COUNT],
                                    int attributes[ATTRIBUTE_COUNT]) {
    int change_sum = 0;
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) change_sum += changes[attribute_id];
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        const int server_value = input->server_attributes[attribute_id];
        const int percent_scale = PERCENT_BASE + input->effects.attribute_percent[attribute_id];
        const int unscaled_value = server_value * PERCENT_BASE / percent_scale;
        const int raw_change = tarot_set_has(input->tarot_cards, TAROT_FOOL_REVERSED)
                                       ? tarot_fool_reversed_total(unscaled_value, change_sum) - unscaled_value
                                       : changes[attribute_id];

        attributes[attribute_id] =
                server_value + tarot_scaled_change(server_value, raw_change, percent_scale, PERCENT_BASE);
    }
}

static int aptitude_term(const BuffModelInput *input, const int willpower, const int intuition) {
    return (willpower + intuition) * APTITUDE_ATTRIBUTE_FACTOR / input->divisor * input->race_modifier / PERCENT_BASE;
}

/* The Magician gives Intuition's effects Strength's base when Strength's total is higher. */
static int aptitude_intuition(const BuffModelInput *input, const int totals[ATTRIBUTE_COUNT], const int with_plan) {
    const int source = tarot_set_has(input->tarot_cards, TAROT_MAGICIAN) &&
                                       totals[ATTRIBUTE_STRENGTH] > totals[ATTRIBUTE_INTUITION]
                               ? ATTRIBUTE_STRENGTH
                               : ATTRIBUTE_INTUITION;

    return input->base_attributes[source] + (with_plan ? input->planned_attributes[source] : 0);
}

static int planned_aptitude(const BuffModelInput *input, const int unbuffed[ATTRIBUTE_COUNT]) {
    const int base_willpower = input->base_attributes[ATTRIBUTE_WILLPOWER];
    const int change = aptitude_term(input, base_willpower + input->planned_attributes[ATTRIBUTE_WILLPOWER],
                                     aptitude_intuition(input, unbuffed, 1)) -
                       aptitude_term(input, base_willpower, aptitude_intuition(input, input->server_attributes, 0));

    return input->spell_aptitude + tarot_scaled_change(input->spell_aptitude, change,
                                                       PERCENT_BASE + input->effects.aptitude_percent, PERCENT_BASE);
}

/* The Magician lets the higher of Strength and Intuition stand in for the lower one. */
static int effect_attribute_sum(const BuffModelInput *input, const int attributes[ATTRIBUTE_COUNT]) {
    const int strength = attributes[ATTRIBUTE_STRENGTH];
    const int intuition = attributes[ATTRIBUTE_INTUITION];
    int total = 0;
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) total += attributes[attribute_id];
    if (tarot_set_has(input->tarot_cards, TAROT_MAGICIAN)) total += max(strength, intuition) - min(strength, intuition);
    return total;
}

static int attribute_sum_bonus_change(const BuffModelInput *input, const int copies,
                                      const int attributes[ATTRIBUTE_COUNT]) {
    return (copies * effect_attribute_sum(input, attributes) / TREE_EFFECTS_PER_150) -
           (copies * effect_attribute_sum(input, input->server_attributes) / TREE_EFFECTS_PER_150);
}

/* Brilliance and Introspection are flat additions that the Spell Bonus percent then raises. */
static int planned_spell_bonus(const BuffModelInput *input, const int attributes[ATTRIBUTE_COUNT], const int aptitude) {
    const TreeEffects *effects = &input->effects;
    const int change = attribute_sum_bonus_change(input, effects->brilliance, attributes) +
                       (effects->introspection * aptitude / INTROSPECTION_APTITUDE_DIVISOR) -
                       (effects->introspection * input->spell_aptitude / INTROSPECTION_APTITUDE_DIVISOR);

    return input->spell_bonus +
           tarot_scaled_change(input->spell_bonus, change, PERCENT_BASE + effects->spell_bonus_percent, PERCENT_BASE);
}

static int cast_power(const BuffModelInput *input, const int skill_value, const int aptitude, const int planned_bonus) {
    const int spell_bonus = tarot_set_has(input->tarot_cards, TAROT_STAR_REVERSED) ? 0 : planned_bonus;
    const int power = min((skill_value + spell_bonus) * input->spell_multiplier / PERCENT_BASE, 2 * aptitude);
    return max(0, power);
}

/* Each Bless cast reads the Spell Bonus and Braveness its attributes give; Guardian Angel comes after the cap. */
static int bless_cast_power(const BuffModelInput *input, const BuffModelResult *result,
                            const int attributes[ATTRIBUTE_COUNT]) {
    const BuffModelSkill *bless = &input->spell_skills[BUFF_KIND_BLESS];
    const int scale = PERCENT_BASE * GUARDIAN_ANGEL_DIVISOR;
    const int power = cast_power(input, buff_model_skill_value(input, bless, attributes), result->spell_aptitude,
                                 planned_spell_bonus(input, attributes, result->spell_aptitude));

    return power * (scale + (attributes[ATTRIBUTE_BRAVENESS] * input->effects.guardian_angel)) / scale;
}

static int active_bless_bonus(const BuffReading *active, const int attribute_id) {
    if (!active->known) return 0;
    if (active->has_exact) return active->attribute_bonus[attribute_id];
    return buff_model_bless_bonus(active->power, attribute_id);
}

static int active_item_bonus(const BuffReading *active, const int exact_bonus, const int reduced) {
    if (!active->known) return 0;
    if (active->has_exact) return exact_bonus;
    return buff_model_item_bonus(active->power, reduced);
}

static int shown_power(const BuffModelInput *input, const BuffModelResult *result, const int kind) {
    const int active_power = input->active[kind].known ? input->active[kind].power : 0;

    if (!input->show_buffed[kind]) return 0;
    return max(result->cast_power[kind], active_power);
}

static void attributes_with_bless(const BuffModelInput *input, const int unbuffed_changes[ATTRIBUTE_COUNT],
                                  const int power, int attributes[ATTRIBUTE_COUNT]) {
    int changes[ATTRIBUTE_COUNT];
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        changes[attribute_id] = unbuffed_changes[attribute_id] + buff_model_bless_bonus(power, attribute_id);
    }
    attributes_with_changes(input, changes, attributes);
}

static void compute_bless(const BuffModelInput *input, BuffModelResult *result,
                          const int unbuffed_changes[ATTRIBUTE_COUNT], const int unbuffed[ATTRIBUTE_COUNT]) {
    int first_cast_attributes[ATTRIBUTE_COUNT];

    result->cast_power[BUFF_KIND_BLESS] = 0;
    if (input->spell_known[BUFF_KIND_BLESS]) {
        attributes_with_bless(input, unbuffed_changes, bless_cast_power(input, result, unbuffed),
                              first_cast_attributes);
        result->cast_power[BUFF_KIND_BLESS] = bless_cast_power(input, result, first_cast_attributes);
    }

    result->effective_power[BUFF_KIND_BLESS] = shown_power(input, result, BUFF_KIND_BLESS);
    attributes_with_bless(input, unbuffed_changes, result->effective_power[BUFF_KIND_BLESS], result->attributes);
}

/* Enhance or Protect, cast after Bless so their skills see the shown attributes. */
static int compute_item_buff(const BuffModelInput *input, BuffModelResult *result, const int kind,
                             const int server_value, const int exact_bonus) {
    const BuffReading *active = &input->active[kind];

    result->cast_power[kind] = 0;
    if (input->spell_known[kind]) {
        result->cast_power[kind] =
                cast_power(input, buff_model_skill_value(input, &input->spell_skills[kind], result->attributes),
                           result->spell_aptitude, result->spell_bonus);
    }
    result->effective_power[kind] = shown_power(input, result, kind);
    return server_value - active_item_bonus(active, exact_bonus, input->reduced_item_buffs) +
           buff_model_item_bonus(result->effective_power[kind], input->reduced_item_buffs);
}

void buff_model_compute(const BuffModelInput *input, BuffModelResult *result) {
    int unbuffed_changes[ATTRIBUTE_COUNT];
    int unbuffed[ATTRIBUTE_COUNT];
    int determination_change;
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        unbuffed_changes[attribute_id] = input->planned_attributes[attribute_id] -
                                         active_bless_bonus(&input->active[BUFF_KIND_BLESS], attribute_id);
    }
    attributes_with_changes(input, unbuffed_changes, unbuffed);
    result->spell_aptitude = planned_aptitude(input, unbuffed);

    compute_bless(input, result, unbuffed_changes, unbuffed);
    result->spell_bonus = planned_spell_bonus(input, result->attributes, result->spell_aptitude);
    result->weapon = compute_item_buff(input, result, BUFF_KIND_ENHANCE, input->server_weapon,
                                       input->active[BUFF_KIND_ENHANCE].weapon_bonus);
    result->armor = compute_item_buff(input, result, BUFF_KIND_PROTECT, input->server_armor,
                                      input->active[BUFF_KIND_PROTECT].armor_bonus);

    determination_change = attribute_sum_bonus_change(input, input->effects.determination, result->attributes);
    result->weapon += determination_change;
    result->armor += determination_change;
}
