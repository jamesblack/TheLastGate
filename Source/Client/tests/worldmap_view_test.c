#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/annotations.h"
#include "game/worldmap_view.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(actual, expected) CHECK(fabs((actual) - (expected)) < 1e-9)

static const double PANEL_WIDTH = 800.0;
static const double PANEL_HEIGHT = 600.0;

static WorldmapView standard_view(void) {
    WorldmapView view;
    view.centre_tile_x = 512.0;
    view.centre_tile_y = 1024.0;
    view.pixels_per_tile = 4.0;
    return view;
}

static AnnotationSegment make_segment(int start_x, int start_y, int end_x, int end_y) {
    AnnotationSegment segment;
    segment.start_x = (unsigned short) start_x;
    segment.start_y = (unsigned short) start_y;
    segment.end_x = (unsigned short) end_x;
    segment.end_y = (unsigned short) end_y;
    return segment;
}

static int add_three_segment_path(AnnotationSet *set, int category_id) {
    AnnotationSegment segments[3];
    segments[0] = make_segment(500, 1000, 500, 1010);
    segments[1] = make_segment(500, 1010, 500, 1020);
    segments[2] = make_segment(500, 1020, 500, 1030);
    return annotation_add_path(set, category_id, segments, 3, "road");
}

static void tile_centre_to_panel(const WorldmapView *view, int tile_x, int tile_y, double *panel_x, double *panel_y) {
    worldmap_view_tile_to_panel(view, PANEL_WIDTH, PANEL_HEIGHT, tile_x + 0.5, tile_y + 0.5, panel_x, panel_y);
}

static void test_orientation(void) {
    WorldmapView view = standard_view();
    double panel_x, panel_y;
    worldmap_view_tile_to_panel(&view, PANEL_WIDTH, PANEL_HEIGHT, 512.0, 1034.0, &panel_x, &panel_y);
    CHECK_NEAR(panel_x, 440.0);
    CHECK_NEAR(panel_y, 300.0);
    worldmap_view_tile_to_panel(&view, PANEL_WIDTH, PANEL_HEIGHT, 522.0, 1024.0, &panel_x, &panel_y);
    CHECK_NEAR(panel_x, 400.0);
    CHECK_NEAR(panel_y, 340.0);
}

static void test_panel_to_tile_inverts(void) {
    WorldmapView view = standard_view();
    double probes[3][2] = {{0.0, 0.0}, {123.25, 456.5}, {799.0, 599.0}};
    int probe;
    for (probe = 0; probe < 3; probe++) {
        double tile_x, tile_y, back_x, back_y;
        worldmap_view_panel_to_tile(&view, PANEL_WIDTH, PANEL_HEIGHT, probes[probe][0], probes[probe][1], &tile_x,
                                    &tile_y);
        worldmap_view_tile_to_panel(&view, PANEL_WIDTH, PANEL_HEIGHT, tile_x, tile_y, &back_x, &back_y);
        CHECK_NEAR(back_x, probes[probe][0]);
        CHECK_NEAR(back_y, probes[probe][1]);
    }
}

static void test_min_zoom_fits_world(void) {
    CHECK_NEAR(worldmap_view_min_pixels_per_tile(1024.0, 1024.0), 0.5);
    CHECK_NEAR(worldmap_view_min_pixels_per_tile(2048.0, 512.0), 0.5);
}

static void test_clamp_limits_zoom_and_centre(void) {
    WorldmapView view = standard_view();
    view.pixels_per_tile = 100.0;
    worldmap_view_clamp(&view, PANEL_WIDTH, PANEL_HEIGHT);
    CHECK_NEAR(view.pixels_per_tile, 16.0);

    view.pixels_per_tile = 0.01;
    worldmap_view_clamp(&view, PANEL_WIDTH, PANEL_HEIGHT);
    CHECK_NEAR(view.pixels_per_tile, worldmap_view_min_pixels_per_tile(PANEL_WIDTH, PANEL_HEIGHT));

    view.centre_tile_x = -50.0;
    view.centre_tile_y = 5000.0;
    worldmap_view_clamp(&view, PANEL_WIDTH, PANEL_HEIGHT);
    CHECK_NEAR(view.centre_tile_x, 0.0);
    CHECK_NEAR(view.centre_tile_y, 2048.0);
}

static void test_zoom_keeps_cursor_tile(void) {
    WorldmapView view = standard_view();
    double before_x, before_y, after_x, after_y;
    worldmap_view_panel_to_tile(&view, PANEL_WIDTH, PANEL_HEIGHT, 100.0, 50.0, &before_x, &before_y);
    worldmap_view_zoom_at(&view, PANEL_WIDTH, PANEL_HEIGHT, 100.0, 50.0, 2.0);
    CHECK_NEAR(view.pixels_per_tile, 8.0);
    worldmap_view_panel_to_tile(&view, PANEL_WIDTH, PANEL_HEIGHT, 100.0, 50.0, &after_x, &after_y);
    CHECK_NEAR(after_x, before_x);
    CHECK_NEAR(after_y, before_y);
}

static void test_pan_moves_centre(void) {
    WorldmapView view = standard_view();
    worldmap_view_pan(&view, PANEL_WIDTH, PANEL_HEIGHT, 40.0, 20.0);
    CHECK_NEAR(view.centre_tile_y, 1014.0);
    CHECK_NEAR(view.centre_tile_x, 507.0);
}

static void test_hit_prefers_pin_over_path(void) {
    AnnotationSet set;
    WorldmapView view = standard_view();
    WorldmapHit hit;
    double panel_x, panel_y;
    int category_id, path_id, pin_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Mixed", 0);
    path_id = add_three_segment_path(&set, category_id);
    pin_id = annotation_add_point(&set, category_id, ANNOTATION_PIN, 500, 1015, "pin");
    tile_centre_to_panel(&view, 500, 1015, &panel_x, &panel_y);
    hit = worldmap_hit_test(&set, &view, PANEL_WIDTH, PANEL_HEIGHT, panel_x, panel_y, 6.0);
    CHECK(hit.annotation_id == pin_id);
    CHECK(hit.annotation_id != path_id);
    CHECK(hit.segment_index == -1);
    annotation_set_release(&set);
}

static void test_hit_prefers_text_over_path(void) {
    AnnotationSet set;
    WorldmapView view = standard_view();
    WorldmapHit hit;
    double panel_x, panel_y;
    int category_id, text_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Mixed", 0);
    add_three_segment_path(&set, category_id);
    text_id = annotation_add_point(&set, category_id, ANNOTATION_TEXT, 500, 1015, "note");
    tile_centre_to_panel(&view, 500, 1015, &panel_x, &panel_y);
    hit = worldmap_hit_test(&set, &view, PANEL_WIDTH, PANEL_HEIGHT, panel_x, panel_y, 6.0);
    CHECK(hit.annotation_id == text_id);
    annotation_set_release(&set);
}

static void test_hit_returns_segment_index(void) {
    AnnotationSet set;
    WorldmapView view = standard_view();
    WorldmapHit hit;
    double panel_x, panel_y;
    int category_id, path_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Roads", 0);
    path_id = add_three_segment_path(&set, category_id);
    tile_centre_to_panel(&view, 500, 1015, &panel_x, &panel_y);
    hit = worldmap_hit_test(&set, &view, PANEL_WIDTH, PANEL_HEIGHT, panel_x, panel_y + 2.0, 6.0);
    CHECK(hit.annotation_id == path_id);
    CHECK(hit.segment_index == 1);
    annotation_set_release(&set);
}

static void test_hit_segment_on_single_path(void) {
    AnnotationSet set;
    WorldmapView view = standard_view();
    AnnotationCategory *category;
    double panel_x, panel_y;
    int category_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Roads", 0);
    add_three_segment_path(&set, category_id);
    category = annotation_category_find(&set, category_id);
    tile_centre_to_panel(&view, 500, 1015, &panel_x, &panel_y);
    CHECK(worldmap_hit_segment(&category->annotations[0], &view, PANEL_WIDTH, PANEL_HEIGHT, panel_x, panel_y, 6.0) ==
          1);
    CHECK(worldmap_hit_segment(&category->annotations[0], &view, PANEL_WIDTH, PANEL_HEIGHT, panel_x, panel_y + 100.0,
                               6.0) == -1);
    annotation_set_release(&set);
}

static void test_hit_ignores_hidden_category(void) {
    AnnotationSet set;
    WorldmapView view = standard_view();
    WorldmapHit hit;
    double panel_x, panel_y;
    int category_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Hidden", 0);
    annotation_add_point(&set, category_id, ANNOTATION_PIN, 500, 1015, "pin");
    annotation_category_find(&set, category_id)->visible = 0;
    tile_centre_to_panel(&view, 500, 1015, &panel_x, &panel_y);
    hit = worldmap_hit_test(&set, &view, PANEL_WIDTH, PANEL_HEIGHT, panel_x, panel_y, 6.0);
    CHECK(hit.annotation_id == 0);
    CHECK(hit.segment_index == -1);
    annotation_set_release(&set);
}

static void test_hit_miss_outside_radius(void) {
    AnnotationSet set;
    WorldmapView view = standard_view();
    WorldmapHit hit;
    double panel_x, panel_y;
    int category_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Pins", 0);
    annotation_add_point(&set, category_id, ANNOTATION_PIN, 500, 1015, "pin");
    tile_centre_to_panel(&view, 500, 1015, &panel_x, &panel_y);
    hit = worldmap_hit_test(&set, &view, PANEL_WIDTH, PANEL_HEIGHT, panel_x + 20.0, panel_y, 6.0);
    CHECK(hit.annotation_id == 0);
    CHECK(hit.segment_index == -1);
    annotation_set_release(&set);
}

static void test_snap_to_nearby_endpoint(void) {
    AnnotationSegment segments[2];
    int snapped_x = 0, snapped_y = 0;
    segments[0] = make_segment(90, 100, 100, 140);
    segments[1] = make_segment(100, 140, 120, 160);
    CHECK(worldmap_snap_to_endpoint(segments, 2, 101, 141, 2.0, &snapped_x, &snapped_y) == 1);
    CHECK(snapped_x == 100);
    CHECK(snapped_y == 140);
    CHECK(worldmap_snap_to_endpoint(segments, 2, 101, 141, 1.0, &snapped_x, &snapped_y) == 0);
}

int main(void) {
    test_orientation();
    test_panel_to_tile_inverts();
    test_min_zoom_fits_world();
    test_clamp_limits_zoom_and_centre();
    test_zoom_keeps_cursor_tile();
    test_pan_moves_centre();
    test_hit_prefers_pin_over_path();
    test_hit_prefers_text_over_path();
    test_hit_returns_segment_index();
    test_hit_segment_on_single_path();
    test_hit_ignores_hidden_category();
    test_hit_miss_outside_radius();
    test_snap_to_nearby_endpoint();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("worldmap_view_test passed\n");
    return 0;
}
