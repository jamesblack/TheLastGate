#include "game/worldmap_view.h"

#include <math.h>

// On screen, horizontal is map y and vertical is map x. Annotated tile (tx, ty) sits at tile coordinate
// (tx + 0.5, ty + 0.5).

#define WORLDMAP_MIN_PIXELS_FLOOR 1e-3

static double clamp_double(double value, double lowest, double highest) {
    if (value < lowest) {
        return lowest;
    }
    if (value > highest) {
        return highest;
    }
    return value;
}

double worldmap_view_min_pixels_per_tile(double panel_width, double panel_height) {
    double fit_across = panel_width / WORLDMAP_TILES_ACROSS;
    double fit_down = panel_height / WORLDMAP_TILES_DOWN;
    double fit = fit_across < fit_down ? fit_across : fit_down;
    return fit > WORLDMAP_MIN_PIXELS_FLOOR ? fit : WORLDMAP_MIN_PIXELS_FLOOR;
}

void worldmap_view_clamp(WorldmapView *view, double panel_width, double panel_height) {
    view->pixels_per_tile =
            clamp_double(view->pixels_per_tile, worldmap_view_min_pixels_per_tile(panel_width, panel_height),
                         WORLDMAP_MAX_PIXELS_PER_TILE);
    view->centre_tile_x = clamp_double(view->centre_tile_x, 0.0, WORLDMAP_TILES_DOWN);
    view->centre_tile_y = clamp_double(view->centre_tile_y, 0.0, WORLDMAP_TILES_ACROSS);
}

void worldmap_view_tile_to_panel(const WorldmapView *view, double panel_width, double panel_height, double tile_x,
                                 double tile_y, double *panel_x, double *panel_y) {
    *panel_x = (tile_y - view->centre_tile_y) * view->pixels_per_tile + panel_width / 2.0;
    *panel_y = (tile_x - view->centre_tile_x) * view->pixels_per_tile + panel_height / 2.0;
}

void worldmap_view_panel_to_tile(const WorldmapView *view, double panel_width, double panel_height, double panel_x,
                                 double panel_y, double *tile_x, double *tile_y) {
    *tile_y = (panel_x - panel_width / 2.0) / view->pixels_per_tile + view->centre_tile_y;
    *tile_x = (panel_y - panel_height / 2.0) / view->pixels_per_tile + view->centre_tile_x;
}

void worldmap_view_pan(WorldmapView *view, double panel_width, double panel_height, double delta_panel_x,
                       double delta_panel_y) {
    view->centre_tile_y -= delta_panel_x / view->pixels_per_tile;
    view->centre_tile_x -= delta_panel_y / view->pixels_per_tile;
    worldmap_view_clamp(view, panel_width, panel_height);
}

void worldmap_view_zoom_at(WorldmapView *view, double panel_width, double panel_height, double panel_x, double panel_y,
                           double zoom_factor) {
    double anchor_tile_x, anchor_tile_y;
    worldmap_view_panel_to_tile(view, panel_width, panel_height, panel_x, panel_y, &anchor_tile_x, &anchor_tile_y);
    view->pixels_per_tile =
            clamp_double(view->pixels_per_tile * zoom_factor,
                         worldmap_view_min_pixels_per_tile(panel_width, panel_height), WORLDMAP_MAX_PIXELS_PER_TILE);
    view->centre_tile_y = anchor_tile_y - (panel_x - panel_width / 2.0) / view->pixels_per_tile;
    view->centre_tile_x = anchor_tile_x - (panel_y - panel_height / 2.0) / view->pixels_per_tile;
    worldmap_view_clamp(view, panel_width, panel_height);
}

static double distance_to_segment(double point_x, double point_y, double start_x, double start_y, double end_x,
                                  double end_y) {
    double span_x = end_x - start_x;
    double span_y = end_y - start_y;
    double span_length_squared = span_x * span_x + span_y * span_y;
    double along = 0.0;
    double nearest_x, nearest_y;
    if (span_length_squared > 0.0) {
        along = clamp_double(((point_x - start_x) * span_x + (point_y - start_y) * span_y) / span_length_squared, 0.0,
                             1.0);
    }
    nearest_x = start_x + along * span_x;
    nearest_y = start_y + along * span_y;
    return hypot(point_x - nearest_x, point_y - nearest_y);
}

static double distance_to_point_annotation(const Annotation *annotation, const WorldmapView *view, double panel_width,
                                           double panel_height, double panel_x, double panel_y) {
    double centre_x, centre_y;
    worldmap_view_tile_to_panel(view, panel_width, panel_height, annotation->tile_x + 0.5, annotation->tile_y + 0.5,
                                &centre_x, &centre_y);
    return hypot(panel_x - centre_x, panel_y - centre_y);
}

static double nearest_segment(const Annotation *path, const WorldmapView *view, double panel_width, double panel_height,
                              double panel_x, double panel_y, int *segment_index) {
    double best_distance = INFINITY;
    int index;
    *segment_index = -1;
    for (index = 0; index < path->segment_count; index++) {
        const AnnotationSegment *segment = &path->segments[index];
        double start_panel_x, start_panel_y, end_panel_x, end_panel_y, distance;
        worldmap_view_tile_to_panel(view, panel_width, panel_height, segment->start_x + 0.5, segment->start_y + 0.5,
                                    &start_panel_x, &start_panel_y);
        worldmap_view_tile_to_panel(view, panel_width, panel_height, segment->end_x + 0.5, segment->end_y + 0.5,
                                    &end_panel_x, &end_panel_y);
        distance = distance_to_segment(panel_x, panel_y, start_panel_x, start_panel_y, end_panel_x, end_panel_y);
        if (distance < best_distance) {
            best_distance = distance;
            *segment_index = index;
        }
    }
    return best_distance;
}

int worldmap_hit_segment(const Annotation *path, const WorldmapView *view, double panel_width, double panel_height,
                         double panel_x, double panel_y, double radius_pixels) {
    int segment_index;
    double distance = nearest_segment(path, view, panel_width, panel_height, panel_x, panel_y, &segment_index);
    return distance <= radius_pixels ? segment_index : -1;
}

WorldmapHit worldmap_hit_test(AnnotationSet *set, const WorldmapView *view, double panel_width, double panel_height,
                              double panel_x, double panel_y, double radius_pixels) {
    WorldmapHit hit = {0, -1};
    // Pins win over text, text wins over paths; within a kind the nearest candidate wins.
    static const AnnotationKind PRIORITY[3] = {ANNOTATION_PIN, ANNOTATION_TEXT, ANNOTATION_PATH};
    int priority_index;
    for (priority_index = 0; priority_index < 3; priority_index++) {
        double best_distance = INFINITY;
        int category_index;
        for (category_index = 0; category_index < set->category_count; category_index++) {
            const AnnotationCategory *category = &set->categories[category_index];
            int annotation_index;
            if (!category->visible) {
                continue;
            }
            for (annotation_index = 0; annotation_index < category->annotation_count; annotation_index++) {
                const Annotation *annotation = &category->annotations[annotation_index];
                double distance;
                int segment_index = -1;
                if (annotation->kind != PRIORITY[priority_index]) {
                    continue;
                }
                if (annotation->kind == ANNOTATION_PATH) {
                    distance = nearest_segment(annotation, view, panel_width, panel_height, panel_x, panel_y,
                                               &segment_index);
                } else {
                    distance =
                            distance_to_point_annotation(annotation, view, panel_width, panel_height, panel_x, panel_y);
                }
                if (distance <= radius_pixels && distance < best_distance) {
                    best_distance = distance;
                    hit.annotation_id = annotation->id;
                    hit.segment_index = segment_index;
                }
            }
        }
        if (hit.annotation_id != 0) {
            return hit;
        }
    }
    return hit;
}

int worldmap_snap_to_endpoint(const AnnotationSegment *segments, int segment_count, int tile_x, int tile_y,
                              double max_tiles, int *snapped_x, int *snapped_y) {
    double best_distance = INFINITY;
    int found = 0;
    int index;
    for (index = 0; index < segment_count; index++) {
        int end_selector;
        for (end_selector = 0; end_selector < 2; end_selector++) {
            int candidate_x = end_selector ? segments[index].end_x : segments[index].start_x;
            int candidate_y = end_selector ? segments[index].end_y : segments[index].start_y;
            double distance = hypot((double) (candidate_x - tile_x), (double) (candidate_y - tile_y));
            if (distance <= max_tiles && distance < best_distance) {
                best_distance = distance;
                *snapped_x = candidate_x;
                *snapped_y = candidate_y;
                found = 1;
            }
        }
    }
    return found;
}
