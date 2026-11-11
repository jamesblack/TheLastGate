/*
 * Unit tests for the skill preview math.
 */

#include <stdio.h>

#include "planner/skill_preview.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void test_divisor(void) {
    CHECK(skill_preview_attribute_divisor(0) == 5);
    CHECK(skill_preview_attribute_divisor(1) == 6);
}

static void test_gain_below_boundary(void) {
    const int values[3] = {60, 61, 62};
    const int raises[3] = {1, 0, 0};

    CHECK(skill_preview_attribute_gain(values, raises, 5) == 0);
}

static void test_gain_crosses_boundary(void) {
    const int values[3] = {60, 61, 62};
    const int raises[3] = {2, 0, 0};

    CHECK(skill_preview_attribute_gain(values, raises, 5) == 1);
}

static void test_gain_hard_mode(void) {
    const int values[3] = {60, 61, 62};
    const int crossing_raises[3] = {3, 0, 0};
    const int short_raises[3] = {2, 0, 0};

    CHECK(skill_preview_attribute_gain(values, crossing_raises, 6) == 1);
    CHECK(skill_preview_attribute_gain(values, short_raises, 6) == 0);
}

static void test_gain_multiple_attributes(void) {
    const int values[3] = {50, 50, 50};
    const int raises[3] = {5, 5, 5};

    CHECK(skill_preview_attribute_gain(values, raises, 5) == 3);
}

static void test_no_raises(void) {
    const int values[3] = {60, 61, 62};
    const int no_raises[3] = {0, 0, 0};
    const int last_raised[3] = {0, 0, 4};

    CHECK(skill_preview_attribute_gain(values, no_raises, 5) == 0);
    CHECK(skill_preview_uses_raised_attribute(no_raises) == 0);
    CHECK(skill_preview_uses_raised_attribute(last_raised) == 1);
}

int main(void) {
    test_divisor();
    test_gain_below_boundary();
    test_gain_crosses_boundary();
    test_gain_hard_mode();
    test_gain_multiple_attributes();
    test_no_raises();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All skill_preview tests passed\n");
    return 0;
}
