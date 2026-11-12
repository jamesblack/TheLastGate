#pragma once

#include "game_ui_state.h"
#include "ui/states/perf_window_state.h"

#ifdef __cplusplus
extern "C" {
#endif

extern GameUiState game_ui_state;

bool game_ui_hard_mode(void);

int game_ui_attribute_divisor(void);

void ui_calculate_perf_window_state(void);

void ui_calculate_quick_stats_window_state(void);

#ifdef __cplusplus
}
#endif
