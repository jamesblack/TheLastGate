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

static unsigned short pack(const int red, const int green, const int blue) {
    return (unsigned short) ((red << 10) | (green << 5) | blue);
}

static void test_rgb_expands_each_channel_to_full_range(void) {
    const MinimapRgb white = minimap_colour_rgb(pack(31, 31, 31));
    const MinimapRgb mixed = minimap_colour_rgb(pack(16, 1, 0));
    CHECK(white.red == 255 && white.green == 255 && white.blue == 255);
    CHECK(mixed.red == 132 && mixed.green == 8 && mixed.blue == 0);
}

static void test_every_colour_matches_its_own_readout(void) {
    int colour;
    int mismatches = 0;
    for (colour = 0; colour < 32768; colour++) {
        if (!minimap_colour_matches((unsigned short) colour, minimap_colour_rgb((unsigned short) colour))) {
            mismatches++;
        }
    }
    CHECK(mismatches == 0);
}

static void test_match_accepts_values_within_the_same_step(void) {
    const MinimapRgb nearby = {135, 15, 7};
    const MinimapRgb next_step = {136, 15, 7};
    CHECK(minimap_colour_matches(pack(16, 1, 0), nearby));
    CHECK(!minimap_colour_matches(pack(16, 1, 0), next_step));
}

static void test_black_stays_black(void) {
    CHECK(minimap_displayed_colour(0) == 0);
}

int main(void) {
    test_rgb_expands_each_channel_to_full_range();
    test_every_colour_matches_its_own_readout();
    test_match_accepts_values_within_the_same_step();
    test_black_stays_black();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All minimap colour tests passed\n");
    return 0;
}
