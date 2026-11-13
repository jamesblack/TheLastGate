#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "planner/item_effects.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

typedef struct {
    Gear gear;
    size_t field_offset;
} GearFlag;

static PlannerEffects compute(const TreeEffectsInput *tree, const ItemEffectsInput *items) {
    PlannerEffects effects;

    item_effects_compute(tree, items, &effects);
    return effects;
}

static int all_zero(const PlannerEffects *effects) {
    PlannerEffects zero;

    memset(&zero, 0, sizeof(zero));
    return memcmp(effects, &zero, sizeof(zero)) == 0;
}

static void tick_corruptor(TreeEffectsInput *tree, const Corruptor corruptor, const int count) {
    tree->corruptor_ticks |= 1u << corruptor;
    tree->corruptor_counts[corruptor] = (unsigned char) count;
}

static void tick_enchant(ItemEffectsInput *items, const Enchant enchant, const int count) {
    items->enchant_ticks |= 1u << enchant;
    items->enchant_counts[enchant] = (unsigned char) count;
}

static void tick_gear(ItemEffectsInput *items, const Gear gear) {
    items->gear_ticks |= 1u << gear;
}

static void test_no_items_add_nothing(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    effects = compute(&tree, &items);
    CHECK(all_zero(&effects));
}

static void test_attribute_enchants_stack(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tick_enchant(&items, ENCHANT_MORE_STRENGTH, 2);
    tick_enchant(&items, ENCHANT_MORE_ATTRIBUTES, 1);
    effects = compute(&tree, &items);
    CHECK(effects.attribute_percent[ATTRIBUTE_STRENGTH] == 8 && effects.attribute_percent[ATTRIBUTE_BRAVENESS] == 2);
}

static void test_unticked_enchant_count_is_ignored(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    items.enchant_counts[ENCHANT_MORE_STRENGTH] = 4;
    effects = compute(&tree, &items);
    CHECK(effects.attribute_percent[ATTRIBUTE_STRENGTH] == 0);
}

static void test_tickbox_enchants_ignore_count(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tick_enchant(&items, ENCHANT_MORE_ARMOR, 5);
    tick_enchant(&items, ENCHANT_MORE_RESISTANCE, 5);
    effects = compute(&tree, &items);
    CHECK(effects.armor_enchant == 1 && effects.resist_enchant_percent == 10);
    CHECK(item_effects_enchant_copies(&items, ENCHANT_MORE_ARMOR) == 1);
    CHECK(item_effects_enchant_copies(&items, ENCHANT_MORE_STRENGTH) == 0);
}

static void test_spell_bonus_and_kwai_enchants(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tick_enchant(&items, ENCHANT_MORE_SPELL_BONUS, 3);
    tick_enchant(&items, ENCHANT_HIT_TO_PARRY, 2);
    tick_enchant(&items, ENCHANT_PARRY_TO_HIT, 1);
    effects = compute(&tree, &items);
    CHECK(effects.spell_bonus_enchant_percent == 30 && effects.hit_to_parry_percent == 6);
    CHECK(effects.parry_to_hit_percent == 3);
}

static void test_armor_share_enchants(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tick_enchant(&items, ENCHANT_ARMOR_TO_RESISTANCE, 1);
    tick_enchant(&items, ENCHANT_ARMOR_TO_IMMUNITY, 1);
    effects = compute(&tree, &items);
    CHECK(effects.armor_to_resist == 1 && effects.armor_to_immunity == 1);
}

static void test_gear_extras(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tick_gear(&items, GEAR_IMPROVED_LIONS_PAWS);
    tick_gear(&items, GEAR_IMPROVED_COBALT_LANCE);
    tick_gear(&items, GEAR_HALADIE);
    tick_gear(&items, GEAR_IMPROVED_SHIVAS_SCEPTER);
    tick_gear(&items, GEAR_MURAMASA);
    tick_gear(&items, GEAR_IMPROVED_BARBARIAN_SWORD);
    tick_gear(&items, GEAR_IMPROVED_VIOLET_GAZE);
    effects = compute(&tree, &items);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_BRAVENESS] == 40);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_WILLPOWER] == 10);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_INTUITION] == 10);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_AGILITY] == 20);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_STRENGTH] == 10);
    CHECK(effects.attribute_percent[ATTRIBUTE_WILLPOWER] == 4 && effects.attribute_percent[ATTRIBUTE_INTUITION] == 4);
}

static void test_gear_flags(void) {
    static const GearFlag FLAGS[] = {
        {GEAR_TRAVELERS_GUIDE, offsetof(PlannerEffects, travelers_guide)},
        {GEAR_FISTS_OF_THE_HEAVENS, offsetof(PlannerEffects, fists_of_heavens)},
        {GEAR_GRANDE_GRIMOIRE, offsetof(PlannerEffects, grande_grimoire)},
        {GEAR_IMPROVED_BLACK_TACHI, offsetof(PlannerEffects, spell_bonus_to_weapon)},
        {GEAR_IMPROVED_WHITE_ODACHI, offsetof(PlannerEffects, spell_bonus_to_armor)},
        {GEAR_EVERGREEN, offsetof(PlannerEffects, evergreen)},
        {GEAR_GESTURE_OF_GORN, offsetof(PlannerEffects, hitpoints_to_spell_bonus)},
        {GEAR_IMPROVED_NEIS_CLAW, offsetof(PlannerEffects, mana_to_spell_bonus)},
        {GEAR_SUPERBIA, offsetof(PlannerEffects, superbia)},
        {GEAR_IMPROVED_AJAX_BLUE, offsetof(PlannerEffects, brave_into_strength)},
    };
    int index;

    for (index = 0; index < (int) (sizeof(FLAGS) / sizeof(FLAGS[0])); index++) {
        TreeEffectsInput tree;
        ItemEffectsInput items;
        PlannerEffects effects;
        int *flag;

        memset(&tree, 0, sizeof(tree));
        memset(&items, 0, sizeof(items));
        tick_gear(&items, FLAGS[index].gear);
        effects = compute(&tree, &items);
        flag = (int *) ((char *) &effects + FLAGS[index].field_offset);
        CHECK(*flag == 1);
        *flag = 0;
        CHECK(all_zero(&effects));
    }
}

static void test_ajax_blue_adds_braveness_percent(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tick_enchant(&items, ENCHANT_MORE_BRAVENESS, 1);
    tick_gear(&items, GEAR_IMPROVED_AJAX_BLUE);
    effects = compute(&tree, &items);
    CHECK(effects.attribute_percent[ATTRIBUTE_STRENGTH] == 3 && effects.brave_into_strength == 1);
}

static void test_item_percents_cap_at_200(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tick_corruptor(&tree, CORRUPTOR_FALSE_BARBARISM, 20);
    tick_gear(&items, GEAR_IMPROVED_BARBARIAN_SWORD);
    effects = compute(&tree, &items);
    CHECK(effects.skill_extra_percent[ATTRIBUTE_STRENGTH] == 200);

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tick_corruptor(&tree, CORRUPTOR_FALSE_BRAVENESS, 20);
    tick_corruptor(&tree, CORRUPTOR_FALSE_STRENGTH, 20);
    tick_enchant(&items, ENCHANT_MORE_BRAVENESS, 20);
    tick_enchant(&items, ENCHANT_MORE_STRENGTH, 20);
    tick_enchant(&items, ENCHANT_MORE_ATTRIBUTES, 20);
    tick_gear(&items, GEAR_IMPROVED_AJAX_BLUE);
    effects = compute(&tree, &items);
    CHECK(effects.attribute_percent[ATTRIBUTE_BRAVENESS] == 160);
    CHECK(effects.attribute_percent[ATTRIBUTE_STRENGTH] == 200);
}

static void test_not_modelled_gear_changes_nothing(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tick_gear(&items, GEAR_CUDGEL_OF_KWAI);
    tick_gear(&items, GEAR_QUICKSILVER);
    tick_gear(&items, GEAR_EXCALIBUR);
    effects = compute(&tree, &items);
    CHECK(all_zero(&effects));
    CHECK(GEAR_INFO[GEAR_CUDGEL_OF_KWAI].modelled == 0 && GEAR_INFO[GEAR_EXCALIBUR].modelled == 0);
    CHECK(GEAR_INFO[GEAR_QUICKSILVER].modelled == 0 && GEAR_INFO[GEAR_SUPERBIA].modelled == 1);
}

static void test_tree_effects_still_apply(void) {
    TreeEffectsInput tree;
    ItemEffectsInput items;
    PlannerEffects effects;

    memset(&tree, 0, sizeof(tree));
    memset(&items, 0, sizeof(items));
    tree.tree_class = TREE_CLASS_SEYAN;
    tree.tree_points = 1u << ((TREE_NODE_COUNT - 8) + 4);
    tick_enchant(&items, ENCHANT_MORE_STRENGTH, 1);
    effects = compute(&tree, &items);
    CHECK(effects.attribute_percent[ATTRIBUTE_STRENGTH] == 8 && effects.attribute_percent[ATTRIBUTE_WILLPOWER] == 5);
}

static void test_enchant_server_numbers(void) {
    static const int SERVER_NUMBERS[ENCHANT_COUNT] = {6, 17, 27, 36, 44, 84, 87, 96, 97, 2, 21, 16, 35};
    int index;

    for (index = 0; index < ENCHANT_COUNT; index++) {
        const int tickbox_only = index == ENCHANT_MORE_ARMOR || index == ENCHANT_MORE_RESISTANCE ||
                                 index == ENCHANT_ARMOR_TO_RESISTANCE || index == ENCHANT_ARMOR_TO_IMMUNITY;

        CHECK(ENCHANT_INFO[index].server_number == SERVER_NUMBERS[index]);
        CHECK((ENCHANT_INFO[index].counted == 0) == tickbox_only);
        CHECK(strlen(ENCHANT_INFO[index].one_copy) <= 16);
    }
}

int main(void) {
    test_no_items_add_nothing();
    test_attribute_enchants_stack();
    test_unticked_enchant_count_is_ignored();
    test_tickbox_enchants_ignore_count();
    test_spell_bonus_and_kwai_enchants();
    test_armor_share_enchants();
    test_gear_extras();
    test_gear_flags();
    test_ajax_blue_adds_braveness_percent();
    test_item_percents_cap_at_200();
    test_not_modelled_gear_changes_nothing();
    test_tree_effects_still_apply();
    test_enchant_server_numbers();

    if (failures != 0) {
        printf("item_effects_test: %d check(s) failed\n", failures);
        return 1;
    }
    printf("item_effects_test: all checks passed\n");
    return 0;
}
