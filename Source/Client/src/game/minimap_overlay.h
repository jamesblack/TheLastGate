#pragma once

#include "game/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

void minimap_overlay_paint(unsigned int *pixels, int pixel_size, int origin_tile_x, int origin_tile_y, int magnify,
                           const AnnotationSet *set);

const Annotation *minimap_overlay_pin_at(const AnnotationSet *set, int origin_tile_x, int origin_tile_y, int magnify,
                                         int pixel_x, int pixel_y);

#ifdef __cplusplus
}
#endif
