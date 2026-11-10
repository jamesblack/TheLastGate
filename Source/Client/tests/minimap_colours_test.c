#include <stdio.h>

#include "game/minimap_colours.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void test_rgb_round_trips(void) {
    const MinimapColour colour = minimap_colour_from_rgb(12, 200, 99);
    const MinimapRgb rgb = minimap_colour_rgb(colour);
    CHECK(colour == 0x0CC863);
    CHECK(rgb.red == 12 && rgb.green == 200 && rgb.blue == 99);
}

static void test_rgb555_expands_each_channel_to_full_range(void) {
    CHECK(minimap_colour_from_rgb555(0x7FFF) == 0xFFFFFF);
    CHECK(minimap_colour_from_rgb555((unsigned short) ((16 << 10) | (1 << 5))) == 0x840800);
    CHECK(minimap_colour_from_rgb555(0) == 0);
}

static void test_black_stays_black(void) {
    CHECK(minimap_displayed_colour(0) == 0);
}

int main(void) {
    test_rgb_round_trips();
    test_rgb555_expands_each_channel_to_full_range();
    test_black_stays_black();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All minimap colour tests passed\n");
    return 0;
}
