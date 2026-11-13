#include "item_effects.h"

#define ATTRIBUTE_BIT(attribute) (1 << (attribute))
#define MASK_BRV ATTRIBUTE_BIT(ATTRIBUTE_BRAVENESS)
#define MASK_WIL ATTRIBUTE_BIT(ATTRIBUTE_WILLPOWER)
#define MASK_INT ATTRIBUTE_BIT(ATTRIBUTE_INTUITION)
#define MASK_AGL ATTRIBUTE_BIT(ATTRIBUTE_AGILITY)
#define MASK_STR ATTRIBUTE_BIT(ATTRIBUTE_STRENGTH)
#define ALL_ATTRIBUTES (MASK_BRV | MASK_WIL | MASK_INT | MASK_AGL | MASK_STR)

typedef enum {
    FIELD_ATTRIBUTE_PERCENT,
    FIELD_SKILL_EXTRA_PERCENT,
    FIELD_SPELL_BONUS_ENCHANT_PERCENT,
    FIELD_RESIST_ENCHANT_PERCENT,
    FIELD_HIT_TO_PARRY_PERCENT,
    FIELD_PARRY_TO_HIT_PERCENT,
    FIELD_ARMOR_ENCHANT,
    FIELD_ARMOR_TO_RESIST,
    FIELD_ARMOR_TO_IMMUNITY,
    FIELD_BRAVE_INTO_STRENGTH,
    FIELD_TRAVELERS_GUIDE,
    FIELD_FISTS_OF_HEAVENS,
    FIELD_GRANDE_GRIMOIRE,
    FIELD_SPELL_BONUS_TO_WEAPON,
    FIELD_SPELL_BONUS_TO_ARMOR,
    FIELD_EVERGREEN,
    FIELD_HITPOINTS_TO_SPELL_BONUS,
    FIELD_MANA_TO_SPELL_BONUS,
    FIELD_SUPERBIA,
} EffectField;

typedef struct {
    EffectField field;
    int attribute_mask; /* attributes the amount applies to; 0 for fields that are not per attribute */
    int amount;
} Effect;

typedef struct {
    Enchant enchant;
    Effect effect;
} EnchantEffect;

typedef struct {
    Gear gear;
    Effect effect;
} GearEffect;

const EnchantInfo ENCHANT_INFO[ENCHANT_COUNT] = {
    [ENCHANT_MORE_BRAVENESS] = {6, "More Braveness", "+3% Braveness", 1},
    [ENCHANT_MORE_WILLPOWER] = {17, "More Willpower", "+3% Willpower", 1},
    [ENCHANT_MORE_INTUITION] = {27, "More Intuition", "+3% Intuition", 1},
    [ENCHANT_MORE_AGILITY] = {36, "More Agility", "+3% Agility", 1},
    [ENCHANT_MORE_STRENGTH] = {44, "More Strength", "+3% Strength", 1},
    [ENCHANT_MORE_ATTRIBUTES] = {84, "More attributes", "+2% attributes", 1},
    [ENCHANT_MORE_SPELL_BONUS] = {87, "More Spell Bonus", "+10% Spell Bonus", 1},
    [ENCHANT_HIT_TO_PARRY] = {96, "Hit to Parry", "3% Hit to Parry", 1},
    [ENCHANT_PARRY_TO_HIT] = {97, "Parry to Hit", "3% Parry to Hit", 1},
    [ENCHANT_MORE_ARMOR] = {2, "More Armor Value", "Chest: AV x1.1", 0},
    [ENCHANT_MORE_RESISTANCE] = {21, "More Resistance", "Cloak: Res x1.1", 0},
    [ENCHANT_ARMOR_TO_RESISTANCE] = {16, "Armor to Resistance", "Chest: AV/10 Res", 0},
    [ENCHANT_ARMOR_TO_IMMUNITY] = {35, "Armor to Immunity", "Chest: AV/10 Imm", 0},
};

const GearInfo GEAR_INFO[GEAR_COUNT] = {
    [GEAR_IMPROVED_VIOLET_GAZE] = {"Improved Violet Gaze", "+4% Wil & Int", 1},
    [GEAR_IMPROVED_LIONS_PAWS] = {"Improved Lion's Paws", "+20% Brv passive", 1},
    [GEAR_IMPROVED_COBALT_LANCE] = {"Improved Cobalt Lance", "+20% Brv passive", 1},
    [GEAR_HALADIE] = {"Haladie", "+10% Wil spells", 1},
    [GEAR_IMPROVED_SHIVAS_SCEPTER] = {"Improved Shiva's Scepter", "+10% Int spells", 1},
    [GEAR_MURAMASA] = {"Muramasa", "+20% Agl melee", 1},
    [GEAR_IMPROVED_BARBARIAN_SWORD] = {"Improved Barbarian Sword", "+10% Str melee", 1},
    [GEAR_IMPROVED_FELL_NIGHT] = {"Improved Fell Night", "+10% Str melee", 1},
    [GEAR_IMPROVED_AJAX_BLUE] = {"Improved Ajax Blue", "Brv bonus and % to Str", 1},
    [GEAR_TRAVELERS_GUIDE] = {"Traveler's Guide", "Higher of Brv/Agl for effects", 1},
    [GEAR_FISTS_OF_THE_HEAVENS] = {"Fists of the Heavens", "Top attribute x1.2, best weapon skill", 1},
    [GEAR_GRANDE_GRIMOIRE] = {"Grande Grimoire", "Top attribute x1.1", 1},
    [GEAR_IMPROVED_BLACK_TACHI] = {"Improved Black Tachi", "Spell Bonus to WV", 1},
    [GEAR_IMPROVED_WHITE_ODACHI] = {"Improved White Odachi", "Spell Bonus to AV", 1},
    [GEAR_EVERGREEN] = {"Evergreen", "Agl/10 to WV, Str/10 to AV", 1},
    [GEAR_GESTURE_OF_GORN] = {"Gesture of Gorn", "HP/100 to Spell Bonus", 1},
    [GEAR_IMPROVED_NEIS_CLAW] = {"Improved Nei's Claw", "Mana/100 to Spell Bonus", 1},
    [GEAR_SUPERBIA] = {"Superbia", "Aptitude x0.1", 1},
    [GEAR_CUDGEL_OF_KWAI] = {"Cudgel of Kwai", "Hit and Parry use the higher", 0},
    [GEAR_QUICKSILVER] = {"Quicksilver", "Move speed/30 to Hit and Parry", 0},
    [GEAR_EXCALIBUR] = {"Excalibur", "Attack speed/5 to WV", 0},
};

static const EnchantEffect ENCHANT_EFFECTS[] = {
    {ENCHANT_MORE_BRAVENESS, {FIELD_ATTRIBUTE_PERCENT, MASK_BRV, 3}},
    {ENCHANT_MORE_WILLPOWER, {FIELD_ATTRIBUTE_PERCENT, MASK_WIL, 3}},
    {ENCHANT_MORE_INTUITION, {FIELD_ATTRIBUTE_PERCENT, MASK_INT, 3}},
    {ENCHANT_MORE_AGILITY, {FIELD_ATTRIBUTE_PERCENT, MASK_AGL, 3}},
    {ENCHANT_MORE_STRENGTH, {FIELD_ATTRIBUTE_PERCENT, MASK_STR, 3}},
    {ENCHANT_MORE_ATTRIBUTES, {FIELD_ATTRIBUTE_PERCENT, ALL_ATTRIBUTES, 2}},
    {ENCHANT_MORE_SPELL_BONUS, {FIELD_SPELL_BONUS_ENCHANT_PERCENT, 0, 10}},
    {ENCHANT_HIT_TO_PARRY, {FIELD_HIT_TO_PARRY_PERCENT, 0, 3}},
    {ENCHANT_PARRY_TO_HIT, {FIELD_PARRY_TO_HIT_PERCENT, 0, 3}},
    {ENCHANT_MORE_ARMOR, {FIELD_ARMOR_ENCHANT, 0, 1}},
    {ENCHANT_MORE_RESISTANCE, {FIELD_RESIST_ENCHANT_PERCENT, 0, 10}},
    {ENCHANT_ARMOR_TO_RESISTANCE, {FIELD_ARMOR_TO_RESIST, 0, 1}},
    {ENCHANT_ARMOR_TO_IMMUNITY, {FIELD_ARMOR_TO_IMMUNITY, 0, 1}},
};

static const GearEffect GEAR_EFFECTS[] = {
    {GEAR_IMPROVED_VIOLET_GAZE, {FIELD_ATTRIBUTE_PERCENT, MASK_WIL | MASK_INT, 4}},
    {GEAR_IMPROVED_LIONS_PAWS, {FIELD_SKILL_EXTRA_PERCENT, MASK_BRV, 20}},
    {GEAR_IMPROVED_COBALT_LANCE, {FIELD_SKILL_EXTRA_PERCENT, MASK_BRV, 20}},
    {GEAR_HALADIE, {FIELD_SKILL_EXTRA_PERCENT, MASK_WIL, 10}},
    {GEAR_IMPROVED_SHIVAS_SCEPTER, {FIELD_SKILL_EXTRA_PERCENT, MASK_INT, 10}},
    {GEAR_MURAMASA, {FIELD_SKILL_EXTRA_PERCENT, MASK_AGL, 20}},
    {GEAR_IMPROVED_BARBARIAN_SWORD, {FIELD_SKILL_EXTRA_PERCENT, MASK_STR, 10}},
    {GEAR_IMPROVED_FELL_NIGHT, {FIELD_SKILL_EXTRA_PERCENT, MASK_STR, 10}},
    {GEAR_IMPROVED_AJAX_BLUE, {FIELD_BRAVE_INTO_STRENGTH, 0, 1}},
    {GEAR_TRAVELERS_GUIDE, {FIELD_TRAVELERS_GUIDE, 0, 1}},
    {GEAR_FISTS_OF_THE_HEAVENS, {FIELD_FISTS_OF_HEAVENS, 0, 1}},
    {GEAR_GRANDE_GRIMOIRE, {FIELD_GRANDE_GRIMOIRE, 0, 1}},
    {GEAR_IMPROVED_BLACK_TACHI, {FIELD_SPELL_BONUS_TO_WEAPON, 0, 1}},
    {GEAR_IMPROVED_WHITE_ODACHI, {FIELD_SPELL_BONUS_TO_ARMOR, 0, 1}},
    {GEAR_EVERGREEN, {FIELD_EVERGREEN, 0, 1}},
    {GEAR_GESTURE_OF_GORN, {FIELD_HITPOINTS_TO_SPELL_BONUS, 0, 1}},
    {GEAR_IMPROVED_NEIS_CLAW, {FIELD_MANA_TO_SPELL_BONUS, 0, 1}},
    {GEAR_SUPERBIA, {FIELD_SUPERBIA, 0, 1}},
};

#define LENGTH_OF(array) ((int) (sizeof(array) / sizeof((array)[0])))

static void add_effect(PlannerEffects *effects, const Effect *effect, const int copies) {
    const int amount = effect->amount * copies;
    int attribute;

    switch (effect->field) {
        case FIELD_ATTRIBUTE_PERCENT:
        case FIELD_SKILL_EXTRA_PERCENT:
            for (attribute = 0; attribute < ATTRIBUTE_COUNT; attribute++) {
                if (effect->attribute_mask & ATTRIBUTE_BIT(attribute)) {
                    if (effect->field == FIELD_ATTRIBUTE_PERCENT) {
                        effects->attribute_percent[attribute] += amount;
                    } else {
                        effects->skill_extra_percent[attribute] += amount;
                    }
                }
            }
            break;
        case FIELD_SPELL_BONUS_ENCHANT_PERCENT: effects->spell_bonus_enchant_percent += amount; break;
        case FIELD_RESIST_ENCHANT_PERCENT: effects->resist_enchant_percent += amount; break;
        case FIELD_HIT_TO_PARRY_PERCENT: effects->hit_to_parry_percent += amount; break;
        case FIELD_PARRY_TO_HIT_PERCENT: effects->parry_to_hit_percent += amount; break;
        case FIELD_ARMOR_ENCHANT: effects->armor_enchant += amount; break;
        case FIELD_ARMOR_TO_RESIST: effects->armor_to_resist += amount; break;
        case FIELD_ARMOR_TO_IMMUNITY: effects->armor_to_immunity += amount; break;
        case FIELD_BRAVE_INTO_STRENGTH: effects->brave_into_strength += amount; break;
        case FIELD_TRAVELERS_GUIDE: effects->travelers_guide += amount; break;
        case FIELD_FISTS_OF_HEAVENS: effects->fists_of_heavens += amount; break;
        case FIELD_GRANDE_GRIMOIRE: effects->grande_grimoire += amount; break;
        case FIELD_SPELL_BONUS_TO_WEAPON: effects->spell_bonus_to_weapon += amount; break;
        case FIELD_SPELL_BONUS_TO_ARMOR: effects->spell_bonus_to_armor += amount; break;
        case FIELD_EVERGREEN: effects->evergreen += amount; break;
        case FIELD_HITPOINTS_TO_SPELL_BONUS: effects->hitpoints_to_spell_bonus += amount; break;
        case FIELD_MANA_TO_SPELL_BONUS: effects->mana_to_spell_bonus += amount; break;
        case FIELD_SUPERBIA: effects->superbia += amount; break;
    }
}

static int capped(const int percent) {
    return percent > ITEM_EFFECT_CAP ? ITEM_EFFECT_CAP : percent;
}

int item_effects_enchant_copies(const ItemEffectsInput *input, const Enchant enchant) {
    if (!(input->enchant_ticks & (1u << enchant))) {
        return 0;
    }
    return ENCHANT_INFO[enchant].counted ? input->enchant_counts[enchant] : 1;
}

void item_effects_compute(const TreeEffectsInput *tree_input, const ItemEffectsInput *item_input,
                          PlannerEffects *effects) {
    int index;
    int attribute;

    tree_effects_compute(tree_input, effects);

    for (index = 0; index < LENGTH_OF(ENCHANT_EFFECTS); index++) {
        const EnchantEffect *enchant_effect = &ENCHANT_EFFECTS[index];

        add_effect(effects, &enchant_effect->effect, item_effects_enchant_copies(item_input, enchant_effect->enchant));
    }

    for (index = 0; index < LENGTH_OF(GEAR_EFFECTS); index++) {
        const GearEffect *gear_effect = &GEAR_EFFECTS[index];

        if (item_input->gear_ticks & (1u << gear_effect->gear)) {
            add_effect(effects, &gear_effect->effect, 1);
        }
    }

    if (effects->brave_into_strength > 0) {
        effects->attribute_percent[ATTRIBUTE_STRENGTH] += effects->attribute_percent[ATTRIBUTE_BRAVENESS];
    }

    for (attribute = 0; attribute < ATTRIBUTE_COUNT; attribute++) {
        effects->attribute_percent[attribute] = capped(effects->attribute_percent[attribute]);
        effects->skill_extra_percent[attribute] = capped(effects->skill_extra_percent[attribute]);
    }
    effects->spell_bonus_enchant_percent = capped(effects->spell_bonus_enchant_percent);
    effects->hit_to_parry_percent = capped(effects->hit_to_parry_percent);
    effects->parry_to_hit_percent = capped(effects->parry_to_hit_percent);
    effects->resist_enchant_percent = capped(effects->resist_enchant_percent);
}
