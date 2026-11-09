#include "minimap.h"

#include <SDL2/SDL.h>

#include "inter.h"
#include "config/config.h"
#include "game/minimap_store.h"
#include "log/log.h"

#define MINIMAP_AUTOSAVE_INTERVAL_MS (5u * 60u * 1000u)

static unsigned short tile_colours[MAPX_MAX * MAPY_MAX];
static MinimapTiles minimap = {tile_colours, MAPX_MAX, MAPY_MAX, 0};
static char map_file_path[512];
static Uint32 last_save_ticks = 0;

unsigned short *minimap_tile_colours(void) {
    return tile_colours;
}

static void save_if_changed(void) {
    if (!minimap.changed || map_file_path[0] == '\0') return;

    if (minimap_store_save(map_file_path, &minimap)) {
        log_info("Saved minimap to %s", map_file_path);
    } else {
        log_error("Failed to save minimap to %s", map_file_path);
    }
}

void minimap_open(const char *host, const int port) {
    minimap_store_build_path(map_file_path, sizeof(map_file_path), g_config.runtime.pref_path, host, port);

    if (minimap_store_load(map_file_path, &minimap)) {
        log_info("Loaded minimap from %s", map_file_path);
    } else {
        log_info("No usable saved minimap at %s, starting blank", map_file_path);
    }
    last_save_ticks = SDL_GetTicks();
}

void minimap_see_tile(const int tile_x, const int tile_y, const unsigned short colour) {
    minimap_store_set_tile(&minimap, tile_x, tile_y, colour);
}

void minimap_clear_box(const int left, const int top, const int box_width, const int box_height) {
    minimap_store_clear_box(&minimap, left, top, box_width, box_height);
}

void minimap_autosave_tick(void) {
    const Uint32 now_ticks = SDL_GetTicks();

    if (now_ticks - last_save_ticks < MINIMAP_AUTOSAVE_INTERVAL_MS) return;
    last_save_ticks = now_ticks;
    save_if_changed();
}

void minimap_close(void) {
    save_if_changed();
}

int minimap_delete_saved(const char *host, const int port) {
    char path[512];

    minimap_store_build_path(path, sizeof(path), g_config.runtime.pref_path, host, port);
    if (minimap_store_delete(path)) {
        log_info("Deleted saved minimap %s", path);
        return 1;
    }
    log_error("Failed to delete saved minimap %s", path);
    return 0;
}
