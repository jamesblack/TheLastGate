#include <stdio.h>
#include <string.h>

#include "game/annotations.h"
#include "game/minimap_overlay.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define VIEW_SIZE 128
#define CANARY 0xDEADBEEFu
#define PAINTED 0xFFFFC34Fu

static unsigned int view[VIEW_SIZE * VIEW_SIZE];

static int count_painted(void) {
    int index;
    int total = 0;
    for (index = 0; index < VIEW_SIZE * VIEW_SIZE; index++) {
        if (view[index] != 0) total++;
    }
    return total;
}

static void test_nothing_painted_when_disabled(void) {
    AnnotationSet set;
    int category;

    annotation_set_init(&set);
    category = annotation_category_add(&set, "Pins", 0);
    annotation_add_point(&set, category, ANNOTATION_PIN, 10, 20, "pin");
    set.show_on_minimap = 0;
    memset(view, 0, sizeof(view));

    minimap_overlay_paint(view, VIEW_SIZE, 0, 0, 1, &set);
    CHECK(count_painted() == 0);
    annotation_set_release(&set);
}

static void test_pin_paints_three_by_three(void) {
    AnnotationSet set;
    int category;
    int column;
    int row;

    annotation_set_init(&set);
    category = annotation_category_add(&set, "Pins", 0);
    annotation_add_point(&set, category, ANNOTATION_PIN, 10, 20, "pin");
    set.show_on_minimap = 1;
    memset(view, 0, sizeof(view));

    minimap_overlay_paint(view, VIEW_SIZE, 0, 0, 1, &set);
    for (column = 9; column <= 11; column++) {
        for (row = 19; row <= 21; row++) CHECK(view[column * VIEW_SIZE + row] == PAINTED);
    }
    CHECK(view[8 * VIEW_SIZE + 20] == 0);
    CHECK(count_painted() == 9);
    annotation_set_release(&set);
}

static void test_segment_fills_magnified_blocks(void) {
    AnnotationSet set;
    int category;
    int tile_y;
    int sub_x;
    int sub_y;
    AnnotationSegment segment = {0, 0, 0, 3};

    annotation_set_init(&set);
    category = annotation_category_add(&set, "Paths", 0);
    annotation_add_path(&set, category, &segment, 1, "path");
    set.show_on_minimap = 1;
    memset(view, 0, sizeof(view));

    minimap_overlay_paint(view, VIEW_SIZE, 0, 0, 2, &set);
    for (tile_y = 0; tile_y <= 3; tile_y++) {
        for (sub_x = 0; sub_x < 2; sub_x++) {
            for (sub_y = 0; sub_y < 2; sub_y++) CHECK(view[sub_x * VIEW_SIZE + tile_y * 2 + sub_y] == PAINTED);
        }
    }
    CHECK(count_painted() == 16);
    annotation_set_release(&set);
}

static void test_hidden_category_and_text_skipped(void) {
    AnnotationSet set;
    int shown;
    int hidden;

    annotation_set_init(&set);
    shown = annotation_category_add(&set, "Shown", 0);
    hidden = annotation_category_add(&set, "Hidden", 1);
    annotation_add_point(&set, shown, ANNOTATION_TEXT, 10, 10, "text");
    annotation_add_point(&set, hidden, ANNOTATION_PIN, 30, 30, "pin");
    annotation_category_find(&set, hidden)->visible = 0;
    set.show_on_minimap = 1;
    memset(view, 0, sizeof(view));

    minimap_overlay_paint(view, VIEW_SIZE, 0, 0, 1, &set);
    CHECK(count_painted() == 0);
    annotation_set_release(&set);
}

static void test_out_of_view_is_clipped(void) {
    AnnotationSet set;
    int category;
    unsigned int guarded[VIEW_SIZE * VIEW_SIZE + 2];
    AnnotationSegment segment = {0, 0, 300, 300};

    annotation_set_init(&set);
    category = annotation_category_add(&set, "Pins", 0);
    annotation_add_point(&set, category, ANNOTATION_PIN, 500, 500, "far");
    annotation_add_point(&set, category, ANNOTATION_PIN, 0, 0, "corner");
    annotation_add_path(&set, category, &segment, 1, "long");
    set.show_on_minimap = 1;
    memset(guarded, 0, sizeof(guarded));
    guarded[0] = CANARY;
    guarded[VIEW_SIZE * VIEW_SIZE + 1] = CANARY;

    minimap_overlay_paint(guarded + 1, VIEW_SIZE, 0, 0, 1, &set);
    CHECK(guarded[0] == CANARY);
    CHECK(guarded[VIEW_SIZE * VIEW_SIZE + 1] == CANARY);
    CHECK(guarded[1 + 127 * VIEW_SIZE + 127] == PAINTED);
    annotation_set_release(&set);
}

int main(void) {
    test_nothing_painted_when_disabled();
    test_pin_paints_three_by_three();
    test_segment_fills_magnified_blocks();
    test_hidden_category_and_text_skipped();
    test_out_of_view_is_clipped();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All minimap overlay tests passed\n");
    return 0;
}
