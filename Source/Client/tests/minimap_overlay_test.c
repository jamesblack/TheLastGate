#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void test_magnified_segment_is_one_dotted_pixel_wide(void) {
    AnnotationSet set;
    int category;
    int pixel_y;
    AnnotationSegment segment = {0, 0, 0, 3};

    annotation_set_init(&set);
    category = annotation_category_add(&set, "Paths", 0);
    annotation_add_path(&set, category, &segment, 1, "path");
    set.show_on_minimap = 1;
    memset(view, 0, sizeof(view));

    minimap_overlay_paint(view, VIEW_SIZE, 0, 0, 2, &set);
    for (pixel_y = 1; pixel_y <= 7; pixel_y++) {
        CHECK(view[1 * VIEW_SIZE + pixel_y] == (pixel_y % 2 == 1 ? PAINTED : 0));
    }
    CHECK(count_painted() == 4);
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
    CHECK(guarded[1 + 126 * VIEW_SIZE + 126] == PAINTED);
    CHECK(guarded[1 + 127 * VIEW_SIZE + 127] == 0);
    annotation_set_release(&set);
}

static unsigned int reference[VIEW_SIZE * VIEW_SIZE];

static void reference_put_pixel(const int pixel_x, const int pixel_y) {
    if (pixel_x >= 0 && pixel_y >= 0 && pixel_x < VIEW_SIZE && pixel_y < VIEW_SIZE)
        reference[pixel_x * VIEW_SIZE + pixel_y] = PAINTED;
}

static void reference_paint_segment(const AnnotationSegment *segment, const int origin_tile_x, const int origin_tile_y,
                                    const int magnify) {
    int pixel_x = (segment->start_x - origin_tile_x) * magnify + magnify / 2;
    int pixel_y = (segment->start_y - origin_tile_y) * magnify + magnify / 2;
    const int end_x = (segment->end_x - origin_tile_x) * magnify + magnify / 2;
    const int end_y = (segment->end_y - origin_tile_y) * magnify + magnify / 2;
    const int delta_x = abs(end_x - pixel_x);
    const int delta_y = -abs(end_y - pixel_y);
    const int step_x = pixel_x < end_x ? 1 : -1;
    const int step_y = pixel_y < end_y ? 1 : -1;
    const int x_major = delta_x >= -delta_y;
    int error = delta_x + delta_y;
    int major_steps = 0;

    memset(reference, 0, sizeof(reference));
    for (;;) {
        const int doubled_error = 2 * error;
        int moved_x = 0;
        int moved_y = 0;
        if (major_steps % 2 == 0) reference_put_pixel(pixel_x, pixel_y);
        if (pixel_x == end_x && pixel_y == end_y) break;
        if (doubled_error >= delta_y) {
            error += delta_y;
            pixel_x += step_x;
            moved_x = 1;
        }
        if (doubled_error <= delta_x) {
            error += delta_x;
            pixel_y += step_y;
            moved_y = 1;
        }
        if (x_major ? moved_x : moved_y) major_steps++;
    }
}

static int paint_matches_reference(const AnnotationSegment *segment, const int origin_tile_x, const int origin_tile_y,
                                   const int magnify) {
    AnnotationSet set;
    int category;

    annotation_set_init(&set);
    category = annotation_category_add(&set, "Paths", 0);
    annotation_add_path(&set, category, segment, 1, "path");
    set.show_on_minimap = 1;
    memset(view, 0, sizeof(view));
    minimap_overlay_paint(view, VIEW_SIZE, origin_tile_x, origin_tile_y, magnify, &set);
    annotation_set_release(&set);

    reference_paint_segment(segment, origin_tile_x, origin_tile_y, magnify);
    return memcmp(view, reference, sizeof(view)) == 0;
}

static void test_long_row_paints_only_visible_part(void) {
    AnnotationSegment segment = {10, 0, 10, 2000};
    int pixel_y;

    CHECK(paint_matches_reference(&segment, 0, 0, 1));
    for (pixel_y = 0; pixel_y < VIEW_SIZE; pixel_y++) {
        CHECK(view[10 * VIEW_SIZE + pixel_y] == (pixel_y % 2 == 0 ? PAINTED : 0));
    }
    CHECK(count_painted() == VIEW_SIZE / 2);
}

static void test_long_column_paints_only_visible_part(void) {
    AnnotationSegment segment = {0, 10, 2000, 10};
    int pixel_x;

    CHECK(paint_matches_reference(&segment, 0, 0, 1));
    for (pixel_x = 0; pixel_x < VIEW_SIZE; pixel_x++) {
        CHECK(view[pixel_x * VIEW_SIZE + 10] == (pixel_x % 2 == 0 ? PAINTED : 0));
    }
    CHECK(count_painted() == VIEW_SIZE / 2);
}

static void test_segment_outside_view_paints_nothing(void) {
    AnnotationSegment beyond = {500, 0, 500, 2000};
    AnnotationSegment before = {0, 0, 40, 40};

    CHECK(paint_matches_reference(&beyond, 0, 0, 1));
    CHECK(count_painted() == 0);
    CHECK(paint_matches_reference(&before, 300, 300, 1));
    CHECK(count_painted() == 0);
}

static void test_clipped_segments_match_unclipped_reference(void) {
    const AnnotationSegment segments[] = {
        {2000, 5, 3, 1900},    {1900, 1700, 20, 30}, {0, 0, 300, 300},     {700, 2047, 300, 0},
        {350, 410, 360, 1200}, {420, 900, 330, 905}, {400, 400, 401, 450}, {380, 390, 381, 395},
        {200, 2047, 600, 0},   {400, 420, 400, 420}, {0, 2047, 2047, 0},   {1500, 470, 0, 431},
    };
    const int origins[][2] = {{0, 0}, {330, 400}, {390, 410}, {1900, 1900}};
    const int magnifications[] = {1, 2, 3, 5};
    int segment_index;
    int origin_index;
    int magnify_index;

    for (segment_index = 0; segment_index < (int) (sizeof(segments) / sizeof(segments[0])); segment_index++) {
        for (origin_index = 0; origin_index < (int) (sizeof(origins) / sizeof(origins[0])); origin_index++) {
            for (magnify_index = 0; magnify_index < (int) (sizeof(magnifications) / sizeof(magnifications[0]));
                 magnify_index++) {
                const int matched = paint_matches_reference(&segments[segment_index], origins[origin_index][0],
                                                            origins[origin_index][1], magnifications[magnify_index]);
                if (!matched) {
                    printf("segment %d origin %d magnify %d differs from reference\n", segment_index, origin_index,
                           magnifications[magnify_index]);
                }
                CHECK(matched);
            }
        }
    }
}

static void test_many_long_segments_paint_quickly(void) {
    AnnotationSet set;
    AnnotationSegment segments[ANNOTATION_MAX_SEGMENTS];
    int segment_index;
    int path_index;
    int category_index;
    clock_t started;
    double seconds;

    annotation_set_init(&set);
    for (segment_index = 0; segment_index < ANNOTATION_MAX_SEGMENTS; segment_index++) {
        segments[segment_index].start_x = (unsigned short) (200 + segment_index);
        segments[segment_index].start_y = 0;
        segments[segment_index].end_x = (unsigned short) (200 + segment_index);
        segments[segment_index].end_y = 2047;
    }
    for (category_index = 0; category_index < 16; category_index++) {
        const int category = annotation_category_add(&set, "Flood", 0);
        for (path_index = 0; path_index < 8; path_index++)
            annotation_add_path(&set, category, segments, ANNOTATION_MAX_SEGMENTS, "flood");
    }
    set.show_on_minimap = 1;
    memset(view, 0, sizeof(view));

    started = clock();
    minimap_overlay_paint(view, VIEW_SIZE, 0, 0, 1, &set);
    seconds = (double) (clock() - started) / CLOCKS_PER_SEC;
    if (seconds >= 0.05) printf("painting off-view segments took %.3f s\n", seconds);
    CHECK(seconds < 0.05);
    CHECK(count_painted() == 0);
    annotation_set_release(&set);
}

int main(void) {
    test_nothing_painted_when_disabled();
    test_pin_paints_three_by_three();
    test_magnified_segment_is_one_dotted_pixel_wide();
    test_hidden_category_and_text_skipped();
    test_out_of_view_is_clipped();
    test_long_row_paints_only_visible_part();
    test_long_column_paints_only_visible_part();
    test_segment_outside_view_paints_nothing();
    test_clipped_segments_match_unclipped_reference();
    test_many_long_segments_paint_quickly();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All minimap overlay tests passed\n");
    return 0;
}
