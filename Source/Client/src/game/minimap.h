#pragma once

#ifdef __cplusplus
extern "C" {
#endif

unsigned short *minimap_tile_colours(void);

unsigned int minimap_revision(void);

void minimap_open(const char *host, int port);

void minimap_note_log_line(const char *line, int centre_character);

void minimap_note_view_centre(int centre_character);

void minimap_see_tile(int tile_x, int tile_y, unsigned short colour);

void minimap_clear_box(int left, int top, int box_width, int box_height);

void minimap_autosave_tick(void);

void minimap_close(void);

int minimap_delete_saved(const char *host, int port);

#ifdef __cplusplus
}
#endif
