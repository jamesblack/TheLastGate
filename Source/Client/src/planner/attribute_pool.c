#include "attribute_pool.h"

#include <string.h>

/* Each attribute's points only get dearer, so taking the cheapest next point every time gives the cheapest total. */
AttributePoolPick attribute_pool_cheapest(const int step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS],
                                          const int point_count) {
    AttributePoolPick pick;
    int point_number;

    memset(&pick, 0, sizeof(pick));
    if (point_count < 0 || point_count > ATTRIBUTE_POOL_MAX_POINTS * ATTRIBUTE_COUNT) return pick;

    for (point_number = 0; point_number < point_count; point_number++) {
        int cheapest_id = -1;
        int cheapest_cost = 0;
        int attribute_id;

        for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
            const int next_step = pick.points[attribute_id];
            int next_cost;

            if (next_step >= ATTRIBUTE_POOL_MAX_POINTS) continue;
            next_cost = step_costs[attribute_id][next_step];
            if (next_cost == ATTRIBUTE_POOL_NO_COST) continue;
            if (cheapest_id < 0 || next_cost < cheapest_cost) {
                cheapest_id = attribute_id;
                cheapest_cost = next_cost;
            }
        }
        if (cheapest_id < 0) {
            memset(&pick, 0, sizeof(pick));
            return pick;
        }
        pick.points[cheapest_id]++;
        pick.total_cost += cheapest_cost;
    }
    pick.available = 1;
    return pick;
}
