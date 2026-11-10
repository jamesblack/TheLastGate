#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
} MinimapRgb;

MinimapRgb minimap_colour_rgb(unsigned short colour);

int minimap_colour_matches(unsigned short colour, MinimapRgb rgb);

unsigned short minimap_displayed_colour(unsigned short colour);

#ifdef __cplusplus
}
#endif
