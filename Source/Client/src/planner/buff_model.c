#include "buff_model.h"

#include "game/attributes.h"
#include "util/math_util.h"

#define ITEM_BONUS_CAP 127
#define PERCENT_BASE 100
#define BLESS_BONUS_DIVISOR 5
#define ITEM_BONUS_DIVISOR 4
#define REDUCED_ITEM_BONUS_DIVISOR 6
#define APTITUDE_ATTRIBUTE_FACTOR 5

int buff_model_bless_bonus(const int power, const int attribute_id) {
    if (power <= 0) return 0;
    return min(ITEM_BONUS_CAP, (((power * 2 / 3) - attribute_id) / BLESS_BONUS_DIVISOR) + 3);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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

int buff_model_skill_value(const BuffModelInput *input, const BuffModelSkill *skill,
                           const int attributes[ATTRIBUTE_COUNT]) {
    return skill->server_total + skill->planned + (attribute_sum(skill, attributes) / input->divisor) -
           (attribute_sum(skill, input->server_attributes) / input->divisor);
}

static int aptitude_term(const BuffModelInput *input, const int willpower, const int intuition) {
    return (willpower + intuition) * APTITUDE_ATTRIBUTE_FACTOR / input->divisor * input->race_modifier / PERCENT_BASE;
}

static int planned_aptitude(const BuffModelInput *input) {
    const int base_willpower = input->base_attributes[ATTRIBUTE_WILLPOWER];
    const int base_intuition = input->base_attributes[ATTRIBUTE_INTUITION];

    return input->spell_aptitude +
           aptitude_term(input, base_willpower + input->planned_attributes[ATTRIBUTE_WILLPOWER],
                         base_intuition + input->planned_attributes[ATTRIBUTE_INTUITION]) -
           aptitude_term(input, base_willpower, base_intuition);
}

static int cast_power(const BuffModelInput *input, const int skill_value, const int aptitude) {
    const int power = min((skill_value + input->spell_bonus) * input->spell_multiplier / PERCENT_BASE, 2 * aptitude);
    return max(0, power);
}

static int active_bless_bonus(const BuffReading *active, const int attribute_id) {
    if (!active->known) return 0;
    if (active->has_exact) return active->attribute_bonus[attribute_id];
    return buff_model_bless_bonus(active->power, attribute_id);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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

static void compute_bless(const BuffModelInput *input, BuffModelResult *result, const int unbuffed[ATTRIBUTE_COUNT]) {
    const BuffModelSkill *bless = &input->spell_skills[BUFF_KIND_BLESS];
    int first_cast_attributes[ATTRIBUTE_COUNT];
    int first_power;
    int attribute_id;

    result->cast_power[BUFF_KIND_BLESS] = 0;
    if (input->spell_known[BUFF_KIND_BLESS]) {
        first_power = cast_power(input, buff_model_skill_value(input, bless, unbuffed), result->spell_aptitude);
        for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
            first_cast_attributes[attribute_id] =
                    unbuffed[attribute_id] + buff_model_bless_bonus(first_power, attribute_id);
        }
        result->cast_power[BUFF_KIND_BLESS] =
                cast_power(input, buff_model_skill_value(input, bless, first_cast_attributes), result->spell_aptitude);
    }

    result->effective_power[BUFF_KIND_BLESS] = shown_power(input, result, BUFF_KIND_BLESS);
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        result->attributes[attribute_id] =
                unbuffed[attribute_id] + buff_model_bless_bonus(result->effective_power[BUFF_KIND_BLESS], attribute_id);
    }
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
    int unbuffed[ATTRIBUTE_COUNT];
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        unbuffed[attribute_id] = input->server_attributes[attribute_id] -
                                 active_bless_bonus(&input->active[BUFF_KIND_BLESS], attribute_id) +
                                 input->planned_attributes[attribute_id];
    }
    result->spell_aptitude = planned_aptitude(input);

    compute_bless(input, result, unbuffed);
    result->weapon = compute_item_buff(input, result, BUFF_KIND_ENHANCE, input->server_weapon,
                                       input->active[BUFF_KIND_ENHANCE].weapon_bonus);
    result->armor = compute_item_buff(input, result, BUFF_KIND_PROTECT, input->server_armor,
                                      input->active[BUFF_KIND_PROTECT].armor_bonus);
}
