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

static int moon_scaled_tactics(const BuffModelInput *input, const int server_total, const int change) {
    const int percent_scale = PERCENT_BASE * MOON_MANA_PER_PERCENT;
    const int raw_value = server_total * percent_scale / (percent_scale + input->uncapped_mana);

    return (raw_value + change) * (percent_scale + input->uncapped_mana + input->planned_mana) / percent_scale;
}

int buff_model_skill_value(const BuffModelInput *input, const BuffModelSkill *skill,
                           const int attributes[ATTRIBUTE_COUNT]) {
    const int change = skill->planned + (attribute_sum(skill, attributes) / input->divisor) -
                       (attribute_sum(skill, input->server_attributes) / input->divisor);

    if (skill->scale_kind == BUFF_SKILL_SCALE_RESIST && tarot_set_has(input->tarot_cards, TAROT_HERMIT)) {
        return skill->server_total +
               tarot_scaled_change(skill->server_total, change, HERMIT_RESIST_NUMERATOR, HERMIT_RESIST_DENOMINATOR);
    }
    if (skill->scale_kind == BUFF_SKILL_SCALE_TACTICS && tarot_set_has(input->tarot_cards, TAROT_MOON_REVERSED)) {
        return moon_scaled_tactics(input, skill->server_total, change);
    }
    return skill->server_total + change;
}

/* Under Reverse Fool the server averages all five attributes, so only the sum of the changes matters. */
static void attributes_with_changes(const BuffModelInput *input, const int changes[ATTRIBUTE_COUNT],
                                    int attributes[ATTRIBUTE_COUNT]) {
    int change_sum = 0;
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) change_sum += changes[attribute_id];
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        attributes[attribute_id] =
                tarot_set_has(input->tarot_cards, TAROT_FOOL_REVERSED)
                        ? tarot_fool_reversed_total(input->server_attributes[attribute_id], change_sum)
                        : input->server_attributes[attribute_id] + changes[attribute_id];
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

    return input->spell_aptitude +
           aptitude_term(input, base_willpower + input->planned_attributes[ATTRIBUTE_WILLPOWER],
                         aptitude_intuition(input, unbuffed, 1)) -
           aptitude_term(input, base_willpower, aptitude_intuition(input, input->server_attributes, 0));
}

static int cast_power(const BuffModelInput *input, const int skill_value, const int aptitude) {
    const int spell_bonus = tarot_set_has(input->tarot_cards, TAROT_STAR_REVERSED) ? 0 : input->spell_bonus;
    const int power = min((skill_value + spell_bonus) * input->spell_multiplier / PERCENT_BASE, 2 * aptitude);
    return max(0, power);
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
    const BuffModelSkill *bless = &input->spell_skills[BUFF_KIND_BLESS];
    int first_cast_attributes[ATTRIBUTE_COUNT];
    int first_power;

    result->cast_power[BUFF_KIND_BLESS] = 0;
    if (input->spell_known[BUFF_KIND_BLESS]) {
        first_power = cast_power(input, buff_model_skill_value(input, bless, unbuffed), result->spell_aptitude);
        attributes_with_bless(input, unbuffed_changes, first_power, first_cast_attributes);
        result->cast_power[BUFF_KIND_BLESS] =
                cast_power(input, buff_model_skill_value(input, bless, first_cast_attributes), result->spell_aptitude);
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
                           result->spell_aptitude);
    }
    result->effective_power[kind] = shown_power(input, result, kind);
    return server_value - active_item_bonus(active, exact_bonus, input->reduced_item_buffs) +
           buff_model_item_bonus(result->effective_power[kind], input->reduced_item_buffs);
}

void buff_model_compute(const BuffModelInput *input, BuffModelResult *result) {
    int unbuffed_changes[ATTRIBUTE_COUNT];
    int unbuffed[ATTRIBUTE_COUNT];
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        unbuffed_changes[attribute_id] = input->planned_attributes[attribute_id] -
                                         active_bless_bonus(&input->active[BUFF_KIND_BLESS], attribute_id);
    }
    attributes_with_changes(input, unbuffed_changes, unbuffed);
    result->spell_aptitude = planned_aptitude(input, unbuffed);

    compute_bless(input, result, unbuffed_changes, unbuffed);
    result->weapon = compute_item_buff(input, result, BUFF_KIND_ENHANCE, input->server_weapon,
                                       input->active[BUFF_KIND_ENHANCE].weapon_bonus);
    result->armor = compute_item_buff(input, result, BUFF_KIND_PROTECT, input->server_armor,
                                      input->active[BUFF_KIND_PROTECT].armor_bonus);
}
