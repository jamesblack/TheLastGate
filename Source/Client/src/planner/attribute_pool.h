#pragma once

#include "game/attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATTRIBUTE_POOL_MAX_POINTS 20
#define ATTRIBUTE_POOL_NO_COST (-1)

typedef struct {
    int available;
    int total_cost;
    int points[ATTRIBUTE_COUNT];
} AttributePoolPick;

AttributePoolPick attribute_pool_cheapest(const int step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS],
                                          int point_count);

#ifdef __cplusplus
}
#endif
