#include <stdio.h>
#include <string.h>

#include "planner/tree_effects.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static unsigned int node_bit(const int node) {
    return 1u << ((TREE_NODE_COUNT - node) + 4);
}

static TreeEffectsInput class_with_nodes(const TreeClass tree_class, const int *nodes, const int node_count) {
    TreeEffectsInput input;
    int index;

    memset(&input, 0, sizeof(input));
    input.tree_class = tree_class;
    for (index = 0; index < node_count; index++) {
        input.tree_points |= node_bit(nodes[index]);
    }
    return input;
}

static TreeEffects compute(const TreeEffectsInput *input) {
    TreeEffects effects;

    tree_effects_compute(input, &effects);
    return effects;
}

static int all_zero(const TreeEffects *effects) {
    TreeEffects zero;

    memset(&zero, 0, sizeof(zero));
    return memcmp(effects, &zero, sizeof(zero)) == 0;
}

static void tick(TreeEffectsInput *input, const Corruptor corruptor, const int count) {
    input->corruptor_ticks |= 1u << corruptor;
    input->corruptor_counts[corruptor] = (unsigned char) count;
}

static void test_no_class_has_no_effects(void) {
    TreeEffectsInput input;
    TreeEffects effects;

    memset(&input, 0, sizeof(input));
    effects = compute(&input);
    CHECK(all_zero(&effects));
}

static void test_learned_node_bits(void) {
    const unsigned int points = 1u << ((12 - 5) + 4);

    CHECK(tree_effects_node_learned(points, 5) == 1);
    CHECK(tree_effects_node_learned(points, 4) == 0);
}

static void test_seyan_nodes(void) {
    const int nodes[] = {5, 7, 8, 9, 11};
    const TreeEffectsInput input = class_with_nodes(TREE_CLASS_SEYAN, nodes, 5);
    const TreeEffects effects = compute(&input);
    int attribute;

    CHECK(effects.hit_percent == 4);
    CHECK(effects.determination == 2);
    for (attribute = 0; attribute < ATTRIBUTE_COUNT; attribute++) {
        CHECK(effects.attribute_percent[attribute] == 5);
    }
    CHECK(effects.brilliance == 2);
    CHECK(effects.parry_percent == 4);
}

static void test_class_nodes(void) {
    const int archtemplar_nodes[] = {7, 8, 11};
    const int skald_nodes[] = {8, 9, 11};
    const int warrior_nodes[] = {8};
    const int sorcerer_nodes[] = {4, 5, 8};
    const int summoner_nodes[] = {7, 8, 11};
    const int archharakim_nodes[] = {8, 9};
    const int braver_nodes[] = {5, 7, 8, 11};
    TreeEffectsInput input;
    TreeEffects effects;

    input = class_with_nodes(TREE_CLASS_ARCHTEMPLAR, archtemplar_nodes, 3);
    effects = compute(&input);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_STRENGTH] == 20);
    CHECK(effects.attribute_percent[ATTRIBUTE_STRENGTH] == 6);
    CHECK(effects.armor_percent == 10);

    input = class_with_nodes(TREE_CLASS_SKALD, skald_nodes, 3);
    effects = compute(&input);
    CHECK(effects.attribute_percent[ATTRIBUTE_AGILITY] == 6);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_AGILITY] == 20);
    CHECK(effects.immunity_percent == 10);

    input = class_with_nodes(TREE_CLASS_WARRIOR, warrior_nodes, 1);
    effects = compute(&input);
    CHECK(effects.attribute_percent[ATTRIBUTE_STRENGTH] == 4);
    CHECK(effects.attribute_percent[ATTRIBUTE_AGILITY] == 4);

    input = class_with_nodes(TREE_CLASS_SORCERER, sorcerer_nodes, 3);
    effects = compute(&input);
    CHECK(effects.introspection == 2);
    CHECK(effects.aptitude_percent == 10);
    CHECK(effects.attribute_percent[ATTRIBUTE_WILLPOWER] == 4);
    CHECK(effects.attribute_percent[ATTRIBUTE_INTUITION] == 4);

    input = class_with_nodes(TREE_CLASS_SUMMONER, summoner_nodes, 3);
    effects = compute(&input);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_WILLPOWER] == 20);
    CHECK(effects.attribute_percent[ATTRIBUTE_WILLPOWER] == 6);
    CHECK(effects.resist_percent == 10);

    input = class_with_nodes(TREE_CLASS_ARCHHARAKIM, archharakim_nodes, 2);
    effects = compute(&input);
    CHECK(effects.attribute_percent[ATTRIBUTE_INTUITION] == 6);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_INTUITION] == 20);

    input = class_with_nodes(TREE_CLASS_BRAVER, braver_nodes, 4);
    effects = compute(&input);
    CHECK(effects.weapon_percent == 10);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_BRAVENESS] == 20);
    CHECK(effects.attribute_percent[ATTRIBUTE_BRAVENESS] == 6);
    CHECK(effects.spell_bonus_percent == 20);
}

static void test_corruptor_counts_stack(void) {
    TreeEffectsInput input;
    TreeEffects effects;

    memset(&input, 0, sizeof(input));
    tick(&input, CORRUPTOR_FALSE_STRENGTH, 2);
    effects = compute(&input);
    CHECK(effects.attribute_percent[ATTRIBUTE_STRENGTH] == 6);

    memset(&input, 0, sizeof(input));
    tick(&input, CORRUPTOR_FALSE_HARRIER, 1);
    effects = compute(&input);
    CHECK(effects.attribute_percent[ATTRIBUTE_STRENGTH] == 2);
    CHECK(effects.attribute_percent[ATTRIBUTE_AGILITY] == 2);

    memset(&input, 0, sizeof(input));
    tick(&input, CORRUPTOR_GUARDIAN_ANGEL, 3);
    effects = compute(&input);
    CHECK(effects.guardian_angel == 3);
}

static void test_unticked_count_is_ignored(void) {
    TreeEffectsInput input;
    TreeEffects effects;

    memset(&input, 0, sizeof(input));
    input.corruptor_counts[CORRUPTOR_FALSE_STRENGTH] = 4;
    effects = compute(&input);
    CHECK(all_zero(&effects));
}

static void test_corrupted_node_loses_its_effect(void) {
    const int nodes[] = {5};
    TreeEffectsInput input = class_with_nodes(TREE_CLASS_SEYAN, nodes, 1);
    TreeEffects effects;

    input.tree_node[4] = 89;
    effects = compute(&input);
    CHECK(effects.hit_percent == 0);
    CHECK(effects.weapon_percent == 5);
    CHECK(tree_effects_tree_copies(&input, CORRUPTOR_FALSE_VALOR) == 1);
}

static void test_corrupted_node_with_other_corruptor_gives_nothing(void) {
    const int nodes[] = {5};
    TreeEffectsInput input = class_with_nodes(TREE_CLASS_SEYAN, nodes, 1);
    TreeEffects effects;

    input.tree_node[4] = 13;
    effects = compute(&input);
    CHECK(all_zero(&effects));
}

static void test_tree_and_manual_copies_add(void) {
    const int nodes[] = {5};
    TreeEffectsInput input = class_with_nodes(TREE_CLASS_SEYAN, nodes, 1);
    TreeEffects effects;

    input.tree_node[4] = 5;
    tick(&input, CORRUPTOR_FALSE_RIGOR, 2);
    effects = compute(&input);
    CHECK(effects.hit_percent == 6);
}

static void test_unlearned_corrupted_node_counts_nothing(void) {
    TreeEffectsInput input;
    TreeEffects effects;

    memset(&input, 0, sizeof(input));
    input.tree_class = TREE_CLASS_SEYAN;
    input.tree_node[4] = 5;
    effects = compute(&input);
    CHECK(all_zero(&effects));
    CHECK(tree_effects_tree_copies(&input, CORRUPTOR_FALSE_RIGOR) == 0);
}

static void test_tactician_is_listed_not_modelled(void) {
    TreeEffectsInput input;
    TreeEffects effects;

    CHECK(CORRUPTOR_INFO[CORRUPTOR_FALSE_TACTICIAN].modelled == 0);
    memset(&input, 0, sizeof(input));
    tick(&input, CORRUPTOR_FALSE_TACTICIAN, 3);
    effects = compute(&input);
    CHECK(all_zero(&effects));
}

static void test_server_numbers(void) {
    static const int EXPECTED[CORRUPTOR_COUNT] = {20, 32, 68, 80, 92, 44, 56, 19, 33, 67, 81, 91, 5,
                                                  11, 89, 23, 7,  35, 71, 66, 53, 9,  52, 95, 96};
    int corruptor;

    for (corruptor = 0; corruptor < CORRUPTOR_COUNT; corruptor++) {
        CHECK(CORRUPTOR_INFO[corruptor].server_number == EXPECTED[corruptor]);
        CHECK(tree_effects_corruptor_for_server_number(EXPECTED[corruptor]) == corruptor);
    }
    CHECK(tree_effects_corruptor_for_server_number(13) == -1);
}

static void test_skill_groups(void) {
    CHECK(tree_effects_skill_groups(0) == SKILL_GROUP_PASSIVE);
    CHECK(tree_effects_skill_groups(11) == SKILL_GROUP_ACTIVE_SPELL);
    CHECK(tree_effects_skill_groups(13) == SKILL_GROUP_ACTIVE_MELEE);
    CHECK(tree_effects_skill_groups(31) == (SKILL_GROUP_PASSIVE | SKILL_GROUP_ACTIVE_MELEE));
    CHECK(tree_effects_skill_groups(50) == 0);
}

static void test_class_from_kindred(void) {
    CHECK(tree_effects_class_from_kindred(1u << 1) == TREE_CLASS_SEYAN);
    CHECK(tree_effects_class_from_kindred(1u << 5) == TREE_CLASS_ARCHTEMPLAR);
    CHECK(tree_effects_class_from_kindred(1u << 12) == TREE_CLASS_SKALD);
    CHECK(tree_effects_class_from_kindred(1u << 10) == TREE_CLASS_WARRIOR);
    CHECK(tree_effects_class_from_kindred(1u << 11) == TREE_CLASS_SORCERER);
    CHECK(tree_effects_class_from_kindred(1u << 13) == TREE_CLASS_SUMMONER);
    CHECK(tree_effects_class_from_kindred(1u << 9) == TREE_CLASS_ARCHHARAKIM);
    CHECK(tree_effects_class_from_kindred(1u << 14) == TREE_CLASS_BRAVER);
    CHECK(tree_effects_class_from_kindred(1u << 16) == TREE_CLASS_LYCANTH);
    CHECK(tree_effects_class_from_kindred(0) == TREE_CLASS_NONE);
}

int main(void) {
    test_no_class_has_no_effects();
    test_learned_node_bits();
    test_seyan_nodes();
    test_class_nodes();
    test_corruptor_counts_stack();
    test_unticked_count_is_ignored();
    test_corrupted_node_loses_its_effect();
    test_corrupted_node_with_other_corruptor_gives_nothing();
    test_tree_and_manual_copies_add();
    test_unlearned_corrupted_node_counts_nothing();
    test_tactician_is_listed_not_modelled();
    test_server_numbers();
    test_skill_groups();
    test_class_from_kindred();

    if (failures != 0) {
        printf("tree_effects_test: %d check(s) failed\n", failures);
        return 1;
    }
    printf("tree_effects_test: all checks passed\n");
    return 0;
}
