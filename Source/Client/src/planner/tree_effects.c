#include "tree_effects.h"

#include <string.h>

#define ATTRIBUTE_BIT(attribute) (1 << (attribute))
#define MASK_BRV ATTRIBUTE_BIT(ATTRIBUTE_BRAVENESS)
#define MASK_WIL ATTRIBUTE_BIT(ATTRIBUTE_WILLPOWER)
#define MASK_INT ATTRIBUTE_BIT(ATTRIBUTE_INTUITION)
#define MASK_AGL ATTRIBUTE_BIT(ATTRIBUTE_AGILITY)
#define MASK_STR ATTRIBUTE_BIT(ATTRIBUTE_STRENGTH)
#define ALL_ATTRIBUTES (MASK_BRV | MASK_WIL | MASK_INT | MASK_AGL | MASK_STR)

#define LEARNED_BIT_OFFSET 4

typedef enum {
    FIELD_ATTRIBUTE_PERCENT,
    FIELD_SKILL_EXTRA_PERCENT,
    FIELD_HIT_PERCENT,
    FIELD_PARRY_PERCENT,
    FIELD_WEAPON_PERCENT,
    FIELD_ARMOR_PERCENT,
    FIELD_IMMUNITY_PERCENT,
    FIELD_RESIST_PERCENT,
    FIELD_APTITUDE_PERCENT,
    FIELD_SPELL_BONUS_PERCENT,
    FIELD_DETERMINATION,
    FIELD_DETERMINATION_NODES,
    FIELD_BRILLIANCE,
    FIELD_BRILLIANCE_NODES,
    FIELD_INTROSPECTION,
    FIELD_GUARDIAN_ANGEL,
    FIELD_FLOW,
} EffectField;

typedef struct {
    EffectField field;
    int attribute_mask; /* attributes the amount applies to; 0 for fields that are not per attribute */
    int amount;
} Effect;

typedef struct {
    TreeClass tree_class;
    int node;
    Effect effect;
} TreeNodeEffect;

typedef struct {
    Corruptor corruptor;
    Effect effect;
} CorruptorEffect;

typedef struct {
    int first_skill;
    int last_skill;
    int group;
} SkillRange;

typedef struct {
    unsigned int kindred_bit;
    TreeClass tree_class;
} KindredClass;

const CorruptorInfo CORRUPTOR_INFO[CORRUPTOR_COUNT] = {
    [CORRUPTOR_FALSE_STRENGTH] = {20, "False Strength", "+3% Strength", 1},
    [CORRUPTOR_FALSE_AGILITY] = {32, "False Agility", "+3% Agility", 1},
    [CORRUPTOR_FALSE_WILLPOWER] = {68, "False Willpower", "+3% Willpower", 1},
    [CORRUPTOR_FALSE_INTUITION] = {80, "False Intuition", "+3% Intuition", 1},
    [CORRUPTOR_FALSE_BRAVENESS] = {92, "False Braveness", "+3% Braveness", 1},
    [CORRUPTOR_FALSE_HARRIER] = {44, "False Harrier", "+2% Str & Agl", 1},
    [CORRUPTOR_FALSE_PRAGMATISM] = {56, "False Pragmatism", "+2% Wil & Int", 1},
    [CORRUPTOR_FALSE_BARBARISM] = {19, "False Barbarism", "+10% Str melee", 1},
    [CORRUPTOR_FALSE_CELERITY] = {33, "False Celerity", "+10% Agl melee", 1},
    [CORRUPTOR_FALSE_MYSTICISM] = {67, "False Mysticism", "+10% Wil spells", 1},
    [CORRUPTOR_FALSE_CONCENTRATION] = {81, "False Concentration", "+10% Int spells", 1},
    [CORRUPTOR_FALSE_VIRTUOSITY] = {91, "False Virtuosity", "+10% Brv passive", 1},
    [CORRUPTOR_FALSE_RIGOR] = {5, "False Rigor", "+2% Hit Score", 1},
    [CORRUPTOR_FALSE_FLEXIBILITY] = {11, "False Flexibility", "+2% Parry Score", 1},
    [CORRUPTOR_FALSE_VALOR] = {89, "False Valor", "+5% Weapon Val", 1},
    [CORRUPTOR_FALSE_UNBREAKABLE] = {23, "False Unbreakable", "+5% Armor Val", 1},
    [CORRUPTOR_FALSE_DETERMINATION] = {7, "False Determination", "+1 WV/AV /150", 1},
    [CORRUPTOR_FALSE_INDIFFERENCE] = {35, "False Indifference", "+5% Immunity", 1},
    [CORRUPTOR_FALSE_CONSTITUTION] = {71, "False Constitution", "+5% Resistance", 1},
    [CORRUPTOR_FALSE_TACTICIAN] = {66, "False Tactician", "Tactics by speed", 0},
    [CORRUPTOR_FALSE_ZEALOTRY] = {53, "False Zealotry", "+5% Aptitude", 1},
    [CORRUPTOR_FALSE_BRILLIANCE] = {9, "False Brilliance", "+1 SpB /150", 1},
    [CORRUPTOR_FALSE_INTROSPECTION] = {52, "False Introspection", "+1 SpB /50 Apt", 1},
    [CORRUPTOR_FALSE_WIZARDRY] = {95, "False Wizardry", "+10% Spell Bonus", 1},
    [CORRUPTOR_GUARDIAN_ANGEL] = {96, "Guardian Angel", "Bless by Brv", 1},
    [CORRUPTOR_FALSE_FLOW] = {82, "False Flow", "+1 SpB /200 Mana", 1},
};

static const TreeNodeEffect TREE_NODE_EFFECTS[] = {
    {TREE_CLASS_SEYAN, 5, {FIELD_HIT_PERCENT, 0, 4}},
    {TREE_CLASS_SEYAN, 7, {FIELD_DETERMINATION_NODES, 0, 1}},
    {TREE_CLASS_SEYAN, 8, {FIELD_ATTRIBUTE_PERCENT, ALL_ATTRIBUTES, 5}},
    {TREE_CLASS_SEYAN, 9, {FIELD_BRILLIANCE_NODES, 0, 1}},
    {TREE_CLASS_SEYAN, 11, {FIELD_PARRY_PERCENT, 0, 4}},
    {TREE_CLASS_ARCHTEMPLAR, 7, {FIELD_SKILL_EXTRA_PERCENT, MASK_STR, 20}},
    {TREE_CLASS_ARCHTEMPLAR, 8, {FIELD_ATTRIBUTE_PERCENT, MASK_STR, 6}},
    {TREE_CLASS_ARCHTEMPLAR, 11, {FIELD_ARMOR_PERCENT, 0, 10}},
    {TREE_CLASS_SKALD, 8, {FIELD_ATTRIBUTE_PERCENT, MASK_AGL, 6}},
    {TREE_CLASS_SKALD, 9, {FIELD_SKILL_EXTRA_PERCENT, MASK_AGL, 20}},
    {TREE_CLASS_SKALD, 11, {FIELD_IMMUNITY_PERCENT, 0, 10}},
    {TREE_CLASS_WARRIOR, 8, {FIELD_ATTRIBUTE_PERCENT, MASK_STR | MASK_AGL, 4}},
    {TREE_CLASS_SORCERER, 4, {FIELD_INTROSPECTION, 0, 2}},
    {TREE_CLASS_SORCERER, 5, {FIELD_APTITUDE_PERCENT, 0, 10}},
    {TREE_CLASS_SORCERER, 8, {FIELD_ATTRIBUTE_PERCENT, MASK_WIL | MASK_INT, 4}},
    {TREE_CLASS_SUMMONER, 7, {FIELD_SKILL_EXTRA_PERCENT, MASK_WIL, 20}},
    {TREE_CLASS_SUMMONER, 8, {FIELD_ATTRIBUTE_PERCENT, MASK_WIL, 6}},
    {TREE_CLASS_SUMMONER, 11, {FIELD_RESIST_PERCENT, 0, 10}},
    {TREE_CLASS_ARCHHARAKIM, 8, {FIELD_ATTRIBUTE_PERCENT, MASK_INT, 6}},
    {TREE_CLASS_ARCHHARAKIM, 9, {FIELD_SKILL_EXTRA_PERCENT, MASK_INT, 20}},
    {TREE_CLASS_ARCHHARAKIM, 10, {FIELD_FLOW, 0, 2}},
    {TREE_CLASS_BRAVER, 5, {FIELD_WEAPON_PERCENT, 0, 10}},
    {TREE_CLASS_BRAVER, 7, {FIELD_SKILL_EXTRA_PERCENT, MASK_BRV, 20}},
    {TREE_CLASS_BRAVER, 8, {FIELD_ATTRIBUTE_PERCENT, MASK_BRV, 6}},
    {TREE_CLASS_BRAVER, 11, {FIELD_SPELL_BONUS_PERCENT, 0, 20}},
};

static const CorruptorEffect CORRUPTOR_EFFECTS[] = {
    {CORRUPTOR_FALSE_STRENGTH, {FIELD_ATTRIBUTE_PERCENT, MASK_STR, 3}},
    {CORRUPTOR_FALSE_AGILITY, {FIELD_ATTRIBUTE_PERCENT, MASK_AGL, 3}},
    {CORRUPTOR_FALSE_WILLPOWER, {FIELD_ATTRIBUTE_PERCENT, MASK_WIL, 3}},
    {CORRUPTOR_FALSE_INTUITION, {FIELD_ATTRIBUTE_PERCENT, MASK_INT, 3}},
    {CORRUPTOR_FALSE_BRAVENESS, {FIELD_ATTRIBUTE_PERCENT, MASK_BRV, 3}},
    {CORRUPTOR_FALSE_HARRIER, {FIELD_ATTRIBUTE_PERCENT, MASK_STR | MASK_AGL, 2}},
    {CORRUPTOR_FALSE_PRAGMATISM, {FIELD_ATTRIBUTE_PERCENT, MASK_WIL | MASK_INT, 2}},
    {CORRUPTOR_FALSE_BARBARISM, {FIELD_SKILL_EXTRA_PERCENT, MASK_STR, 10}},
    {CORRUPTOR_FALSE_CELERITY, {FIELD_SKILL_EXTRA_PERCENT, MASK_AGL, 10}},
    {CORRUPTOR_FALSE_MYSTICISM, {FIELD_SKILL_EXTRA_PERCENT, MASK_WIL, 10}},
    {CORRUPTOR_FALSE_CONCENTRATION, {FIELD_SKILL_EXTRA_PERCENT, MASK_INT, 10}},
    {CORRUPTOR_FALSE_VIRTUOSITY, {FIELD_SKILL_EXTRA_PERCENT, MASK_BRV, 10}},
    {CORRUPTOR_FALSE_RIGOR, {FIELD_HIT_PERCENT, 0, 2}},
    {CORRUPTOR_FALSE_FLEXIBILITY, {FIELD_PARRY_PERCENT, 0, 2}},
    {CORRUPTOR_FALSE_VALOR, {FIELD_WEAPON_PERCENT, 0, 5}},
    {CORRUPTOR_FALSE_UNBREAKABLE, {FIELD_ARMOR_PERCENT, 0, 5}},
    {CORRUPTOR_FALSE_DETERMINATION, {FIELD_DETERMINATION, 0, 1}},
    {CORRUPTOR_FALSE_INDIFFERENCE, {FIELD_IMMUNITY_PERCENT, 0, 5}},
    {CORRUPTOR_FALSE_CONSTITUTION, {FIELD_RESIST_PERCENT, 0, 5}},
    {CORRUPTOR_FALSE_ZEALOTRY, {FIELD_APTITUDE_PERCENT, 0, 5}},
    {CORRUPTOR_FALSE_BRILLIANCE, {FIELD_BRILLIANCE, 0, 1}},
    {CORRUPTOR_FALSE_INTROSPECTION, {FIELD_INTROSPECTION, 0, 1}},
    {CORRUPTOR_FALSE_WIZARDRY, {FIELD_SPELL_BONUS_PERCENT, 0, 10}},
    {CORRUPTOR_GUARDIAN_ANGEL, {FIELD_GUARDIAN_ANGEL, 0, 1}},
    {CORRUPTOR_FALSE_FLOW, {FIELD_FLOW, 0, 1}},
};

static const SkillRange SKILL_RANGES[] = {
    {0, 10, SKILL_GROUP_PASSIVE},       {12, 12, SKILL_GROUP_PASSIVE},      {14, 14, SKILL_GROUP_PASSIVE},
    {16, 16, SKILL_GROUP_PASSIVE},      {23, 23, SKILL_GROUP_PASSIVE},      {28, 34, SKILL_GROUP_PASSIVE},
    {36, 36, SKILL_GROUP_PASSIVE},      {38, 39, SKILL_GROUP_PASSIVE},      {44, 45, SKILL_GROUP_PASSIVE},
    {11, 11, SKILL_GROUP_ACTIVE_SPELL}, {15, 15, SKILL_GROUP_ACTIVE_SPELL}, {17, 21, SKILL_GROUP_ACTIVE_SPELL},
    {24, 27, SKILL_GROUP_ACTIVE_SPELL}, {42, 43, SKILL_GROUP_ACTIVE_SPELL}, {46, 47, SKILL_GROUP_ACTIVE_SPELL},
    {13, 13, SKILL_GROUP_ACTIVE_MELEE}, {22, 22, SKILL_GROUP_ACTIVE_MELEE}, {31, 31, SKILL_GROUP_ACTIVE_MELEE},
    {35, 35, SKILL_GROUP_ACTIVE_MELEE}, {37, 37, SKILL_GROUP_ACTIVE_MELEE}, {40, 41, SKILL_GROUP_ACTIVE_MELEE},
    {48, 49, SKILL_GROUP_ACTIVE_MELEE},
};

static const KindredClass KINDRED_CLASSES[] = {
    {1, TREE_CLASS_SEYAN},       {5, TREE_CLASS_ARCHTEMPLAR}, {12, TREE_CLASS_SKALD},
    {10, TREE_CLASS_WARRIOR},    {11, TREE_CLASS_SORCERER},   {13, TREE_CLASS_SUMMONER},
    {9, TREE_CLASS_ARCHHARAKIM}, {14, TREE_CLASS_BRAVER},     {16, TREE_CLASS_LYCANTH},
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
        case FIELD_HIT_PERCENT: effects->hit_percent += amount; break;
        case FIELD_PARRY_PERCENT: effects->parry_percent += amount; break;
        case FIELD_WEAPON_PERCENT: effects->weapon_percent += amount; break;
        case FIELD_ARMOR_PERCENT: effects->armor_percent += amount; break;
        case FIELD_IMMUNITY_PERCENT: effects->immunity_percent += amount; break;
        case FIELD_RESIST_PERCENT: effects->resist_percent += amount; break;
        case FIELD_APTITUDE_PERCENT: effects->aptitude_percent += amount; break;
        case FIELD_SPELL_BONUS_PERCENT: effects->spell_bonus_percent += amount; break;
        case FIELD_DETERMINATION: effects->determination += amount; break;
        case FIELD_DETERMINATION_NODES: effects->determination_nodes += amount; break;
        case FIELD_BRILLIANCE: effects->brilliance += amount; break;
        case FIELD_BRILLIANCE_NODES: effects->brilliance_nodes += amount; break;
        case FIELD_INTROSPECTION: effects->introspection += amount; break;
        case FIELD_GUARDIAN_ANGEL: effects->guardian_angel += amount; break;
        case FIELD_FLOW: effects->flow += amount; break;
    }
}

TreeClass tree_effects_class_from_kindred(const unsigned int kindred) {
    int index;

    for (index = 0; index < LENGTH_OF(KINDRED_CLASSES); index++) {
        if (kindred & (1u << KINDRED_CLASSES[index].kindred_bit)) {
            return KINDRED_CLASSES[index].tree_class;
        }
    }
    return TREE_CLASS_NONE;
}

int tree_effects_node_learned(const unsigned int tree_points, const int node) {
    if (node < 1 || node > TREE_NODE_COUNT) {
        return 0;
    }
    return (int) ((tree_points >> ((TREE_NODE_COUNT - node) + LEARNED_BIT_OFFSET)) & 1u);
}

int tree_effects_corruptor_for_server_number(const int server_number) {
    int corruptor;

    for (corruptor = 0; corruptor < CORRUPTOR_COUNT; corruptor++) {
        if (CORRUPTOR_INFO[corruptor].server_number == server_number) {
            return corruptor;
        }
    }
    return -1;
}

int tree_effects_tree_copies(const TreeEffectsInput *input, const Corruptor corruptor) {
    int node;
    int copies = 0;

    for (node = 1; node <= TREE_NODE_COUNT; node++) {
        const int server_number = input->tree_node[node - 1];

        if (server_number != 0 && tree_effects_node_learned(input->tree_points, node) &&
            tree_effects_corruptor_for_server_number(server_number) == (int) corruptor) {
            copies++;
        }
    }
    return copies;
}

int tree_effects_node_in_math(const TreeClass tree_class, const int node) {
    int index;

    for (index = 0; index < LENGTH_OF(TREE_NODE_EFFECTS); index++) {
        if (TREE_NODE_EFFECTS[index].tree_class == tree_class && TREE_NODE_EFFECTS[index].node == node) {
            return 1;
        }
    }
    return 0;
}

void tree_effects_compute(const TreeEffectsInput *input, PlannerEffects *effects) {
    int index;

    memset(effects, 0, sizeof(*effects));

    for (index = 0; index < LENGTH_OF(TREE_NODE_EFFECTS); index++) {
        const TreeNodeEffect *node_effect = &TREE_NODE_EFFECTS[index];

        if (node_effect->tree_class == input->tree_class &&
            tree_effects_node_learned(input->tree_points, node_effect->node) &&
            input->tree_node[node_effect->node - 1] == 0) {
            add_effect(effects, &node_effect->effect, 1);
        }
    }

    for (index = 0; index < LENGTH_OF(CORRUPTOR_EFFECTS); index++) {
        const CorruptorEffect *corruptor_effect = &CORRUPTOR_EFFECTS[index];
        const Corruptor corruptor = corruptor_effect->corruptor;
        int copies = tree_effects_tree_copies(input, corruptor);

        if (input->corruptor_ticks & (1u << corruptor)) {
            copies += input->corruptor_counts[corruptor];
        }
        add_effect(effects, &corruptor_effect->effect, copies);
    }
}

int tree_effects_skill_groups(const int skill_number) {
    int index;
    int groups = 0;

    for (index = 0; index < LENGTH_OF(SKILL_RANGES); index++) {
        if (skill_number >= SKILL_RANGES[index].first_skill && skill_number <= SKILL_RANGES[index].last_skill) {
            groups |= SKILL_RANGES[index].group;
        }
    }
    return groups;
}
