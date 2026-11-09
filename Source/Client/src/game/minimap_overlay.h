#pragma once

#include "game/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

void minimap_overlay_paint(unsigned int *pixels, int pixel_size, int origin_tile_x, int origin_tile_y, int magnify,
                           const AnnotationSet *set);

#ifdef __cplusplus
}
#endif
