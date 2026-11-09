/*
 * Unit tests for the shared stat raise logic.
 *
 * The game globals and cost functions the module uses are defined here with
 * simple, predictable values so each test can check exact exp bookkeeping.
 */

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "inter.h"
#include "game/stat_raise.h"

struct cplayer pl;
int stat_raised[STAT_RAISE_SLOT_COUNT];
int stat_points_used;

static struct skilltab test_skilltab[MAXSKILL];
struct skilltab *skilltab = test_skilltab;

#define TEST_CAP 100

int attrib_needed(int attribute_id, int value) {
    if (value >= TEST_CAP) return HIGH_VAL;
    return value * 10 + attribute_id;
}

int hp_needed(int value) {
    if (value >= TEST_CAP) return HIGH_VAL;
    return value * 3;
}

int mana_needed(int value) {
    if (value >= TEST_CAP) return HIGH_VAL;
    return value * 4;
}

int skill_needed(int skill_id, int value) {
    if (value >= TEST_CAP) return HIGH_VAL;
    return value * 20 + skill_id;
}

#define MAX_SENT 16
static int sent_count;
static int sent_stat[MAX_SENT];
static int sent_amount[MAX_SENT];

void cmd(int command, int stat_id, int amount) {
    if (command != CL_CMD_STAT || sent_count >= MAX_SENT) return;
    sent_stat[sent_count] = stat_id;
    sent_amount[sent_count] = amount;
    sent_count++;
}

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void reset_state(void) {
    memset(&pl, 0, sizeof(pl));
    memset(stat_raised, 0, sizeof(stat_raised));
    memset(test_skilltab, 0, sizeof(test_skilltab));
    stat_points_used = 0;
    sent_count = 0;

    pl.points = 100000;
    pl.attrib[2][0] = 40;
    pl.hp[0] = 50;
    pl.mana[0] = 60;
    test_skilltab[3].nr = 17;
    pl.skill[17][0] = 30;
}

static void test_raise_attribute(void) {
    reset_state();

    CHECK(stat_raise_cost(2) == 402);
    CHECK(stat_raise_try_raise(2) == 1);
    CHECK(stat_raised[2] == 1);
    CHECK(stat_points_used == 402);
    CHECK(stat_raise_cost(2) == 412);
}

static void test_raise_rejected_without_exp(void) {
    reset_state();
    pl.points = 401;

    CHECK(stat_raise_try_raise(2) == 0);
    CHECK(stat_raised[2] == 0);
    CHECK(stat_points_used == 0);
}

static void test_raise_counts_planned_exp(void) {
    reset_state();
    pl.points = 402 + 411;

    CHECK(stat_raise_try_raise(2) == 1);
    CHECK(stat_raise_try_raise(2) == 0);
    CHECK(stat_raised[2] == 1);
}

static void test_lower_refunds_exact_cost(void) {
    reset_state();

    stat_raise_try_raise(2);
    stat_raise_try_raise(2);
    CHECK(stat_points_used == 402 + 412);
    CHECK(stat_raise_try_lower(2) == 1);
    CHECK(stat_raised[2] == 1);
    CHECK(stat_points_used == 402);
}

static void test_lower_without_plan(void) {
    reset_state();

    CHECK(stat_raise_try_lower(2) == 0);
    CHECK(stat_points_used == 0);
}

static void test_hitpoints_and_mana(void) {
    reset_state();

    CHECK(stat_raise_cost(STAT_RAISE_HITPOINTS) == 150);
    CHECK(stat_raise_cost(STAT_RAISE_MANA) == 240);
    CHECK(stat_raise_try_raise(STAT_RAISE_HITPOINTS) == 1);
    CHECK(stat_raise_try_raise(STAT_RAISE_MANA) == 1);
    CHECK(stat_points_used == 150 + 240);
}

static void test_endurance_never_raises(void) {
    reset_state();

    CHECK(stat_raise_cost(6) == HIGH_VAL);
    CHECK(stat_raise_try_raise(6) == 0);
    CHECK(stat_raised[6] == 0);
}

static void test_out_of_range(void) {
    reset_state();

    CHECK(stat_raise_cost(-1) == HIGH_VAL);
    CHECK(stat_raise_cost(STAT_RAISE_SLOT_COUNT) == HIGH_VAL);
    CHECK(stat_raise_try_raise(STAT_RAISE_SLOT_COUNT) == 0);
    CHECK(stat_raise_try_lower(-1) == 0);
}

static void test_skill_slot_past_skill_table(void) {
    reset_state();

    CHECK(stat_raise_cost(STAT_RAISE_SKILL_BASE + MAXSKILL - 1) != HIGH_VAL);
    CHECK(stat_raise_cost(STAT_RAISE_SKILL_BASE + MAXSKILL) == HIGH_VAL);
    CHECK(stat_raise_try_raise(STAT_RAISE_SKILL_BASE + MAXSKILL) == 0);
}

static void test_skill_uses_skilltab_position(void) {
    reset_state();

    CHECK(stat_raise_cost(STAT_RAISE_SKILL_BASE + 3) == 30 * 20 + 17);
    CHECK(stat_raise_try_raise(STAT_RAISE_SKILL_BASE + 3) == 1);
    CHECK(stat_raised[STAT_RAISE_SKILL_BASE + 3] == 1);
}

static void test_capped_stat(void) {
    reset_state();
    pl.attrib[2][0] = TEST_CAP;

    CHECK(stat_raise_cost(2) == HIGH_VAL);
    CHECK(stat_raise_try_raise(2) == 0);
}

static void test_commit_sends_mapped_ids(void) {
    reset_state();

    stat_raise_try_raise(2);
    stat_raise_try_raise(STAT_RAISE_MANA);
    stat_raise_try_raise(STAT_RAISE_SKILL_BASE + 3);
    stat_raise_try_raise(STAT_RAISE_SKILL_BASE + 3);
    stat_raise_commit_all();

    CHECK(sent_count == 3);
    CHECK(sent_stat[0] == 2 && sent_amount[0] == 1);
    CHECK(sent_stat[1] == STAT_RAISE_MANA && sent_amount[1] == 1);
    CHECK(sent_stat[2] == 17 + STAT_RAISE_SKILL_BASE && sent_amount[2] == 2);
    CHECK(stat_raised[2] == 0 && stat_raised[STAT_RAISE_SKILL_BASE + 3] == 0);
    CHECK(stat_points_used == 0);
}

static void test_reset_sends_nothing(void) {
    reset_state();

    stat_raise_try_raise(2);
    stat_raise_reset_all();

    CHECK(sent_count == 0);
    CHECK(stat_raised[2] == 0);
    CHECK(stat_points_used == 0);
}

static void test_cost_after(void) {
    reset_state();

    CHECK(stat_raise_cost_after(2, 0) == 402);
    CHECK(stat_raise_cost_after(2, 1) == 412);
    CHECK(stat_raise_cost_after(2, -1) == HIGH_VAL);
    CHECK(stat_raise_cost_after(STAT_RAISE_SLOT_COUNT, 0) == HIGH_VAL);
}

static void test_bundle_raises_all(void) {
    const int raise_indices[] = {2, STAT_RAISE_MANA};
    const int point_counts[] = {2, 1};

    reset_state();

    CHECK(stat_raise_try_raise_bundle(raise_indices, point_counts, 2) == 1);
    CHECK(stat_raised[2] == 2);
    CHECK(stat_raised[STAT_RAISE_MANA] == 1);
    CHECK(stat_points_used == 402 + 412 + 240);
}

static void test_bundle_rejected_leaves_plan_unchanged(void) {
    const int raise_indices[] = {2};
    const int point_counts[] = {2};

    reset_state();
    pl.points = 402 + 411;

    CHECK(stat_raise_try_raise_bundle(raise_indices, point_counts, 1) == 0);
    CHECK(stat_raised[2] == 0);
    CHECK(stat_points_used == 0);
}

static void test_bundle_capped_rolls_back(void) {
    const int raise_indices[] = {STAT_RAISE_MANA, 2};
    const int point_counts[] = {1, 2};

    reset_state();
    pl.attrib[2][0] = TEST_CAP - 1;

    CHECK(stat_raise_try_raise_bundle(raise_indices, point_counts, 2) == 0);
    CHECK(stat_raised[STAT_RAISE_MANA] == 0);
    CHECK(stat_raised[2] == 0);
    CHECK(stat_points_used == 0);
}

static void test_click_multiplier(void) {
    CHECK(stat_raise_click_multiplier(0, 0) == 1);
    CHECK(stat_raise_click_multiplier(1, 0) == 10);
    CHECK(stat_raise_click_multiplier(0, 1) == 90);
    CHECK(stat_raise_click_multiplier(1, 1) == 90);
}

static void number_skill_list(void) {
    int skill_position;

    for (skill_position = 0; skill_position < MAXSKILL; skill_position++)
        test_skilltab[skill_position].nr = skill_position;
}

static void test_skill_list_first_sort_keeps_plan(void) {
    reset_state();
    number_skill_list();
    pl.points = 1000;
    CHECK(stat_raise_try_raise(STAT_RAISE_SKILL_BASE + 2) == 1);
    stat_raise_skill_list_sorted();
    CHECK(stat_raised[STAT_RAISE_SKILL_BASE + 2] == 1);
}

static void test_skill_list_same_order_keeps_plan(void) {
    const int version_before = stat_raise_skill_list_version();

    reset_state();
    number_skill_list();
    stat_raise_skill_list_sorted();
    pl.points = 1000;
    CHECK(stat_raise_try_raise(STAT_RAISE_SKILL_BASE + 2) == 1);
    stat_raise_skill_list_sorted();
    CHECK(stat_raised[STAT_RAISE_SKILL_BASE + 2] == 1);
    CHECK(stat_raise_skill_list_version() == version_before);
}

static void test_skill_list_reorder_clears_plan(void) {
    int version_before;

    reset_state();
    number_skill_list();
    stat_raise_skill_list_sorted();
    version_before = stat_raise_skill_list_version();
    pl.points = 1000;
    CHECK(stat_raise_try_raise(0) == 1);
    CHECK(stat_raise_try_raise(STAT_RAISE_SKILL_BASE + 2) == 1);
    test_skilltab[2].nr = 3;
    test_skilltab[3].nr = 2;
    stat_raise_skill_list_sorted();
    CHECK(stat_raised[0] == 0);
    CHECK(stat_raised[STAT_RAISE_SKILL_BASE + 2] == 0);
    CHECK(stat_points_used == 0);
    CHECK(stat_raise_skill_list_version() == version_before + 1);
}

int main(void) {
    test_raise_attribute();
    test_raise_rejected_without_exp();
    test_raise_counts_planned_exp();
    test_lower_refunds_exact_cost();
    test_lower_without_plan();
    test_hitpoints_and_mana();
    test_endurance_never_raises();
    test_out_of_range();
    test_skill_slot_past_skill_table();
    test_skill_uses_skilltab_position();
    test_capped_stat();
    test_commit_sends_mapped_ids();
    test_reset_sends_nothing();
    test_cost_after();
    test_bundle_raises_all();
    test_bundle_rejected_leaves_plan_unchanged();
    test_bundle_capped_rolls_back();
    test_click_multiplier();
    test_skill_list_first_sort_keeps_plan();
    test_skill_list_same_order_keeps_plan();
    test_skill_list_reorder_clears_plan();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All stat_raise tests passed\n");
    return 0;
}
