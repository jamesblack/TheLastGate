#include <stdio.h>

#include "planner/attribute_pool.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void fill_costs(int step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS], const int cost) {
    int attribute_id;
    int step_index;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        for (step_index = 0; step_index < ATTRIBUTE_POOL_MAX_POINTS; step_index++) {
            step_costs[attribute_id][step_index] = cost;
        }
    }
}

static void test_pool_takes_cheapest_points_first(void) {
    int step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS];
    AttributePoolPick pick;
    int step_index;

    fill_costs(step_costs, 30);
    for (step_index = 0; step_index < ATTRIBUTE_POOL_MAX_POINTS; step_index++) {
        step_costs[3][step_index] = step_index < 3 ? 10 : 50;
    }
    pick = attribute_pool_cheapest(step_costs, 5);
    CHECK(pick.available == 1);
    CHECK(pick.total_cost == 90);
    CHECK(pick.points[3] == 3);
    CHECK(pick.points[0] == 2);
    CHECK(pick.points[1] == 0);
}

static void test_pool_skips_capped_attributes(void) {
    int step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS];
    int step_index;
    AttributePoolPick pick;

    fill_costs(step_costs, 10);
    for (step_index = 0; step_index < ATTRIBUTE_POOL_MAX_POINTS; step_index++) {
        step_costs[0][step_index] = ATTRIBUTE_POOL_NO_COST;
    }
    pick = attribute_pool_cheapest(step_costs, 5);
    CHECK(pick.available == 1);
    CHECK(pick.points[0] == 0);
    CHECK(pick.total_cost == 50);
}

static void test_pool_unavailable_when_too_few_points(void) {
    int step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS];
    AttributePoolPick pick;

    fill_costs(step_costs, ATTRIBUTE_POOL_NO_COST);
    step_costs[1][0] = 10;
    step_costs[1][1] = 10;
    step_costs[2][0] = 10;
    pick = attribute_pool_cheapest(step_costs, 5);
    CHECK(pick.available == 0);
}

int main(void) {
    test_pool_takes_cheapest_points_first();
    test_pool_skips_capped_attributes();
    test_pool_unavailable_when_too_few_points();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All attribute pool tests passed\n");
    return 0;
}
