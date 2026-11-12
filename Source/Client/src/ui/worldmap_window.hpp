#pragma once

#include <stdbool.h>

#include "ui_ffi.h"

EXTERN_C_BEGIN

void worldmap_window_render(void);

void worldmap_minimap_tooltip_render(void);

void worldmap_window_close(void);

bool worldmap_window_finish_pending_path(void);

bool worldmap_window_discard_pending_path(void);

EXTERN_C_END
