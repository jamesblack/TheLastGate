#pragma once

#include <stddef.h>

#include "game/minimap_colours.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tiles are stored column by column (tile_x * height + tile_y). */
typedef struct {
    MinimapColour *tiles;
    int width;
    int height;
    int changed;
    unsigned int revision;
} MinimapTiles;

void minimap_store_build_path(char *path, size_t path_size, const char *directory, const char *host, int port);

void minimap_store_set_tile(MinimapTiles *minimap, int tile_x, int tile_y, MinimapColour colour);

void minimap_store_clear_box(MinimapTiles *minimap, int left, int top, int box_width, int box_height);

int minimap_store_save(const char *path, MinimapTiles *minimap);

int minimap_store_load(const char *path, MinimapTiles *minimap);

int minimap_store_delete(const char *path);

#ifdef __cplusplus
}
#endif
