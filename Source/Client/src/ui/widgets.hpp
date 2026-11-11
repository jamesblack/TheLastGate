#pragma once
#include <stdbool.h>

#include "config/keybindings.h"

#define BUTTON_TEXT_COLOR imgui_color_convert_float4_to_u32(0.95f, 0.85f, 0.41f, 1.0f)
#define BUTTON_TEXT_HOVER_COLOR imgui_color_convert_float4_to_u32(0.85f, 0.75f, 0.31f, 1.0f)
#define BUTTON_TEXT_ACTIVE_COLOR imgui_color_convert_float4_to_u32(0.75f, 0.65f, 0.21f, 1.0f)

#define BUTTON_TINT imgui_color_convert_float4_to_u32(1.0f, 1.0f, 1.0f, 1.0f)
#define BUTTON_HOVER_TINT imgui_color_convert_float4_to_u32(0.7f, 0.7f, 0.7f, 1.0f)
#define BUTTON_DISABLED_TINT imgui_color_convert_float4_to_u32(0.45f, 0.45f, 0.45f, 1.0f)
#define BUTTON_ACTIVE_TINT imgui_color_convert_float4_to_u32(0.85f, 0.85f, 0.85f, 1.0f);

bool ui_button(const char *label, float width, float height);

/* Same as ui_button, but drawn dimmed and never clicked when enabled is false. */
bool ui_button_enabled(const char *label, float width, float height, bool enabled);
bool tab_button(const char *label, bool is_active, float width);
void ui_tooltip(const char *tooltip_text);

/*
 * The launcher-styled dropdown: a parchment box with a bordered list of options.
 * Sets *current_option and returns true when a new option was picked this frame.
 * descriptions may be NULL; otherwise each option shows its description on hover.
 */
bool ui_dropdown(const char *id, const char *const *options, int option_count, int *current_option,
                 const char *const *descriptions);

/* Same as ui_dropdown, in the in-game look: gold text on dark red, for windows drawn in the game font. */
bool ui_game_dropdown(const char *id, const char *const *options, int option_count, int *current_option,
                      const char *const *descriptions);
