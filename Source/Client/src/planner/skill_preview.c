#include "skill_preview.h"

#define NORMAL_ATTRIBUTE_DIVISOR 5
#define HARD_MODE_ATTRIBUTE_DIVISOR 6

int skill_preview_attribute_divisor(const int is_hard_mode) {
    return is_hard_mode ? HARD_MODE_ATTRIBUTE_DIVISOR : NORMAL_ATTRIBUTE_DIVISOR;
}

int skill_preview_attribute_gain(const int attribute_values[SKILL_PREVIEW_ATTRIBUTE_COUNT],
                                 const int attribute_raises[SKILL_PREVIEW_ATTRIBUTE_COUNT], const int divisor) {
    int current_sum = 0;
    int raised_sum = 0;
    int attribute_slot;

    for (attribute_slot = 0; attribute_slot < SKILL_PREVIEW_ATTRIBUTE_COUNT; attribute_slot++) {
        current_sum += attribute_values[attribute_slot];
        raised_sum += attribute_values[attribute_slot] + attribute_raises[attribute_slot];
    }

    /* Truncating each share separately matches how the game rounds the skill value. */
    return (raised_sum / divisor) - (current_sum / divisor);
}

int skill_preview_uses_raised_attribute(const int attribute_raises[SKILL_PREVIEW_ATTRIBUTE_COUNT]) {
    int attribute_slot;

    for (attribute_slot = 0; attribute_slot < SKILL_PREVIEW_ATTRIBUTE_COUNT; attribute_slot++) {
        if (attribute_raises[attribute_slot] != 0) return 1;
    }
    return 0;
}
