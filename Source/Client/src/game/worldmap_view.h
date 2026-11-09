#pragma once

#include "game/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORLDMAP_TILES_DOWN 1024
#define WORLDMAP_TILES_ACROSS 2048
#define WORLDMAP_MAX_PIXELS_PER_TILE 16.0

typedef struct {
    double centre_tile_x, centre_tile_y, pixels_per_tile;
} WorldmapView;

typedef struct {
    int annotation_id;
    int segment_index;
} WorldmapHit;

double worldmap_view_min_pixels_per_tile(double panel_width, double panel_height);
void worldmap_view_clamp(WorldmapView *view, double panel_width, double panel_height);
void worldmap_view_tile_to_panel(const WorldmapView *view, double panel_width, double panel_height, double tile_x,
                                 double tile_y, double *panel_x, double *panel_y);
void worldmap_view_panel_to_tile(const WorldmapView *view, double panel_width, double panel_height, double panel_x,
                                 double panel_y, double *tile_x, double *tile_y);
void worldmap_view_pan(WorldmapView *view, double panel_width, double panel_height, double delta_panel_x,
                       double delta_panel_y);
void worldmap_view_zoom_at(WorldmapView *view, double panel_width, double panel_height, double panel_x, double panel_y,
                           double zoom_factor);
WorldmapHit worldmap_hit_test(AnnotationSet *set, const WorldmapView *view, double panel_width, double panel_height,
                              double panel_x, double panel_y, double radius_pixels);
int worldmap_hit_segment(const Annotation *path, const WorldmapView *view, double panel_width, double panel_height,
                         double panel_x, double panel_y, double radius_pixels);
int worldmap_snap_to_endpoint(const AnnotationSegment *segments, int segment_count, int tile_x, int tile_y,
                              double max_tiles, int *snapped_x, int *snapped_y);

#ifdef __cplusplus
}
#endif
