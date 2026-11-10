#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 0x00RRGGBB; 0 means unexplored
typedef uint32_t MinimapColour;

#define MINIMAP_COLOUR_WHITE 0xFFFFFFu

typedef struct {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
} MinimapRgb;

MinimapColour minimap_colour_from_rgb(unsigned char red, unsigned char green, unsigned char blue);

MinimapColour minimap_colour_from_rgb555(unsigned short colour555);

MinimapRgb minimap_colour_rgb(MinimapColour colour);

MinimapColour minimap_displayed_colour(MinimapColour colour);

#ifdef __cplusplus
}
#endif
