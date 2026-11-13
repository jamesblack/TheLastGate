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
#define FISTS_NUMERATOR 6
#define FISTS_DENOMINATOR 5
#define GRIMOIRE_NUMERATOR 11
#define GRIMOIRE_DENOMINATOR 10
#define SUPERBIA_PERCENT_CUT 90
#define ARMOR_SHARE_DIVISOR 10
#define EVERGREEN_DIVISOR 10
#define POOL_PER_SPELL_BONUS 100
#define SCALE_STEP_COUNT 3

typedef struct {
    int numerator;
    int denominator;
} ScaleStep;

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

static int scaled_forward(const int raw_value, const ScaleStep steps[SCALE_STEP_COUNT]) {
    int value = raw_value;
    int step;

    for (step = 0; step < SCALE_STEP_COUNT; step++) value = value * steps[step].numerator / steps[step].denominator;
    return value;
}

/*
 * The server rounds down after each step. The raw value is recovered in one division, which loses less to rounding
 * than undoing the steps one by one.
 */
static int stepped_value(const int server_total, const int change, const ScaleStep steps[SCALE_STEP_COUNT]) {
    int numerator = 1;
    int denominator = 1;
    int step;
    int raw_value;

    for (step = 0; step < SCALE_STEP_COUNT; step++) {
        numerator *= steps[step].numerator;
        denominator *= steps[step].denominator;
    }
    raw_value = server_total * denominator / numerator;
    return server_total + scaled_forward(raw_value + change, steps) - scaled_forward(raw_value, steps);
}

static ScaleStep percent_step(const int percent) {
    const ScaleStep step = {PERCENT_BASE + percent, PERCENT_BASE};
    return step;
}

static ScaleStep optional_step(const int applies, const int numerator, const int denominator) {
    const ScaleStep identity = {1, 1};
    const ScaleStep step = {numerator, denominator};
    return applies ? step : identity;
}

/* The skill's percent, then the Resistance enchant, then Hermit; the armor share is added after all of them. */
static int resistance_value(const BuffModelInput *input, const BuffModelSkill *skill, const int percent,
                            const int enchant_percent, const int armor_share, const int shown_armor, const int change) {
    const ScaleStep steps[SCALE_STEP_COUNT] = {
        percent_step(percent),
        percent_step(enchant_percent),
        optional_step(tarot_set_has(input->tarot_cards, TAROT_HERMIT), HERMIT_RESIST_NUMERATOR,
                      HERMIT_RESIST_DENOMINATOR),
    };
    const int server_share = armor_share ? input->server_armor / ARMOR_SHARE_DIVISOR : 0;
    const int shown_share = armor_share ? shown_armor / ARMOR_SHARE_DIVISOR : 0;

    return stepped_value(skill->server_total - server_share, change, steps) + shown_share;
}

static int moon_scaled_tactics(const BuffModelInput *input, const int server_total, const int change) {
    const int percent_scale = PERCENT_BASE * MOON_MANA_PER_PERCENT;
    const int raw_value = server_total * percent_scale / (percent_scale + input->uncapped_mana);

    return (raw_value + change) * (percent_scale + input->uncapped_mana + input->planned_mana) / percent_scale;
}

int buff_model_skill_value_with_armor(const BuffModelInput *input, const BuffModelSkill *skill,
                                      const int attributes[ATTRIBUTE_COUNT], const int shown_armor) {
    const PlannerEffects *effects = &input->effects;
    const int change = skill->planned + attribute_term(input, skill, attributes) -
                       attribute_term(input, skill, input->server_attributes);

    if (skill->scale_kind == BUFF_SKILL_SCALE_RESIST) {
        return resistance_value(input, skill, effects->resist_percent, effects->resist_enchant_percent,
                                effects->armor_to_resist, shown_armor, change);
    }
    if (skill->scale_kind == BUFF_SKILL_SCALE_IMMUNITY) {
        return resistance_value(input, skill, effects->immunity_percent, 0, effects->armor_to_immunity, shown_armor,
                                change);
    }
    if (skill->scale_kind == BUFF_SKILL_SCALE_TACTICS && tarot_set_has(input->tarot_cards, TAROT_MOON_REVERSED)) {
        return moon_scaled_tactics(input, skill->server_total, change);
    }
    return skill->server_total + change;
}

int buff_model_skill_value(const BuffModelInput *input, const BuffModelSkill *skill,
                           const int attributes[ATTRIBUTE_COUNT]) {
    return buff_model_skill_value_with_armor(input, skill, attributes, input->server_armor);
}

static int highest_attribute(const int values[ATTRIBUTE_COUNT]) {
    int highest = values[0];
    int attribute_id;

    for (attribute_id = 1; attribute_id < ATTRIBUTE_COUNT; attribute_id++) highest = max(highest, values[attribute_id]);
    return highest;
}

static void boost_highest(int values[ATTRIBUTE_COUNT], const int numerator, const int denominator) {
    const int highest = highest_attribute(values);
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        if (values[attribute_id] == highest) values[attribute_id] = values[attribute_id] * numerator / denominator;
    }
}

/* Rounds up: the smallest value that boosts back to this one. Rounding down could tie it with a lower attribute. */
static void unboost_highest(int values[ATTRIBUTE_COUNT], const int numerator, const int denominator) {
    const int highest = highest_attribute(values);
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        if (values[attribute_id] == highest) {
            values[attribute_id] = ((values[attribute_id] * denominator) + numerator - 1) / numerator;
        }
    }
}

static void boosted_attributes(const BuffModelInput *input, const int values[ATTRIBUTE_COUNT],
                               int boosted[ATTRIBUTE_COUNT]) {
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) boosted[attribute_id] = values[attribute_id];
    if (input->effects.fists_of_heavens) boost_highest(boosted, FISTS_NUMERATOR, FISTS_DENOMINATOR);
    if (input->effects.grande_grimoire) boost_highest(boosted, GRIMOIRE_NUMERATOR, GRIMOIRE_DENOMINATOR);
}

static void server_attributes_before_boosts(const BuffModelInput *input, int attributes[ATTRIBUTE_COUNT]) {
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        attributes[attribute_id] = input->server_attributes[attribute_id];
    }
    if (input->effects.grande_grimoire) unboost_highest(attributes, GRIMOIRE_NUMERATOR, GRIMOIRE_DENOMINATOR);
    if (input->effects.fists_of_heavens) unboost_highest(attributes, FISTS_NUMERATOR, FISTS_DENOMINATOR);
}

/*
 * Improved Ajax Blue adds the bonus Braveness to Strength, but not the base Braveness the plan raises.
 * Under Reverse Fool the server averages all five attributes, so only the sum of the changes matters. The
 * attribute percent is applied after that averaging, so the averaging works on the total without it.
 * Fists of the Heavens and the Grande Grimoire then boost whichever attributes are highest.
 */
static void attributes_with_changes(const BuffModelInput *input, const int changes[ATTRIBUTE_COUNT],
                                    int attributes[ATTRIBUTE_COUNT]) {
    int strength_changes[ATTRIBUTE_COUNT];
    int before_boosts[ATTRIBUTE_COUNT];
    int planned[ATTRIBUTE_COUNT];
    int boosted_planned[ATTRIBUTE_COUNT];
    int boosted_server[ATTRIBUTE_COUNT];
    int change_sum = 0;
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        strength_changes[attribute_id] = changes[attribute_id];
    }
    if (input->effects.brave_into_strength) {
        strength_changes[ATTRIBUTE_STRENGTH] +=
                changes[ATTRIBUTE_BRAVENESS] - input->planned_attributes[ATTRIBUTE_BRAVENESS];
    }

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) change_sum += strength_changes[attribute_id];
    server_attributes_before_boosts(input, before_boosts);
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        const int server_value = before_boosts[attribute_id];
        const int percent_scale = PERCENT_BASE + input->effects.attribute_percent[attribute_id];
        const int unscaled_value = server_value * PERCENT_BASE / percent_scale;
        const int raw_change = tarot_set_has(input->tarot_cards, TAROT_FOOL_REVERSED)
                                       ? tarot_fool_reversed_total(unscaled_value, change_sum) - unscaled_value
                                       : strength_changes[attribute_id];

        planned[attribute_id] =
                server_value + tarot_scaled_change(server_value, raw_change, percent_scale, PERCENT_BASE);
    }

    boosted_attributes(input, planned, boosted_planned);
    boosted_attributes(input, before_boosts, boosted_server);
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        attributes[attribute_id] =
                input->server_attributes[attribute_id] + boosted_planned[attribute_id] - boosted_server[attribute_id];
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

    const ScaleStep steps[SCALE_STEP_COUNT] = {
        percent_step(input->effects.aptitude_percent),
        optional_step(input->effects.superbia, PERCENT_BASE - SUPERBIA_PERCENT_CUT, PERCENT_BASE),
        optional_step(0, 1, 1),
    };

    return stepped_value(input->spell_aptitude, change, steps);
}

static void lift_lower(const int attributes[ATTRIBUTE_COUNT], int effective[ATTRIBUTE_COUNT], const int first,
                       const int second) {
    if (attributes[second] > attributes[first]) {
        effective[first] = attributes[second];
    } else {
        effective[second] = attributes[first];
    }
}

/*
 * What the WV, AV and Spell Bonus effects read: the Traveler's Guide lets the higher of Braveness and Agility stand
 * in for the lower one, and the Magician does the same with Strength and Intuition. Skills still read the attributes.
 */
static void effect_attributes(const BuffModelInput *input, const int attributes[ATTRIBUTE_COUNT],
                              int effective[ATTRIBUTE_COUNT]) {
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        effective[attribute_id] = attributes[attribute_id];
    }
    if (input->effects.travelers_guide) lift_lower(attributes, effective, ATTRIBUTE_BRAVENESS, ATTRIBUTE_AGILITY);
    if (tarot_set_has(input->tarot_cards, TAROT_MAGICIAN)) {
        lift_lower(attributes, effective, ATTRIBUTE_INTUITION, ATTRIBUTE_STRENGTH);
    }
}

static int effect_attribute_sum(const BuffModelInput *input, const int attributes[ATTRIBUTE_COUNT]) {
    int effective[ATTRIBUTE_COUNT];
    int total = 0;
    int attribute_id;

    effect_attributes(input, attributes, effective);
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) total += effective[attribute_id];
    return total;
}

static int attribute_sum_bonus(const int nodes, const int copies, const int sum) {
    return (nodes * (sum / TREE_EFFECTS_PER_75)) + (copies * sum / TREE_EFFECTS_PER_150);
}

/* The tree node and the corruptor copies each round down on their own. */
static int attribute_sum_bonus_change(const BuffModelInput *input, const int nodes, const int copies,
                                      const int attributes[ATTRIBUTE_COUNT]) {
    return attribute_sum_bonus(nodes, copies, effect_attribute_sum(input, attributes)) -
           attribute_sum_bonus(nodes, copies, effect_attribute_sum(input, input->server_attributes));
}

static int pool_spell_bonus_change(const int applies, const int uncapped_pool, const int planned_pool) {
    if (!applies) return 0;
    return ((uncapped_pool + planned_pool) / POOL_PER_SPELL_BONUS) - (uncapped_pool / POOL_PER_SPELL_BONUS);
}

/* Brilliance, Introspection and the pool gear are flat additions that the Spell Bonus percents then raise. */
static int planned_spell_bonus(const BuffModelInput *input, const int attributes[ATTRIBUTE_COUNT], const int aptitude) {
    const PlannerEffects *effects = &input->effects;
    const int change = attribute_sum_bonus_change(input, effects->brilliance_nodes, effects->brilliance, attributes) +
                       (effects->introspection * aptitude / INTROSPECTION_APTITUDE_DIVISOR) -
                       (effects->introspection * input->spell_aptitude / INTROSPECTION_APTITUDE_DIVISOR) +
                       pool_spell_bonus_change(effects->hitpoints_to_spell_bonus, input->uncapped_hitpoints,
                                               input->planned_hitpoints) +
                       pool_spell_bonus_change(effects->mana_to_spell_bonus, input->uncapped_mana, input->planned_mana);
    const ScaleStep steps[SCALE_STEP_COUNT] = {
        percent_step(effects->spell_bonus_percent),
        percent_step(effects->spell_bonus_enchant_percent),
        optional_step(0, 1, 1),
    };

    return stepped_value(input->spell_bonus, change, steps);
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

static int evergreen_change(const BuffModelInput *input, const int attributes[ATTRIBUTE_COUNT],
                            const int attribute_id) {
    int effective[ATTRIBUTE_COUNT];
    int server_effective[ATTRIBUTE_COUNT];

    if (!input->effects.evergreen) return 0;
    effect_attributes(input, attributes, effective);
    effect_attributes(input, input->server_attributes, server_effective);
    return (effective[attribute_id] / EVERGREEN_DIVISOR) - (server_effective[attribute_id] / EVERGREEN_DIVISOR);
}

void buff_model_compute(const BuffModelInput *input, BuffModelResult *result) {
    const PlannerEffects *effects = &input->effects;
    int unbuffed_changes[ATTRIBUTE_COUNT];
    int unbuffed[ATTRIBUTE_COUNT];
    int determination_change;
    int spell_bonus_change;
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

    determination_change =
            attribute_sum_bonus_change(input, effects->determination_nodes, effects->determination, result->attributes);
    spell_bonus_change = result->spell_bonus - input->spell_bonus;
    result->weapon += determination_change + evergreen_change(input, result->attributes, ATTRIBUTE_AGILITY) +
                      (effects->spell_bonus_to_weapon ? spell_bonus_change : 0);
    result->armor += determination_change + evergreen_change(input, result->attributes, ATTRIBUTE_STRENGTH) +
                     (effects->spell_bonus_to_armor ? spell_bonus_change : 0);
}
