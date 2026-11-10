#pragma once
#include <stdbool.h>

#include "config/keybindings.h"
#include "ui_common.hpp"

static constexpr unsigned int BUTTON_TEXT_COLOR = color_rgb(242, 217, 105);
static constexpr unsigned int BUTTON_TEXT_HOVER_COLOR = color_rgb(217, 191, 79);
static constexpr unsigned int BUTTON_TEXT_ACTIVE_COLOR = color_rgb(191, 166, 54);

static constexpr unsigned int BUTTON_HOVER_TINT = color_rgb(179, 179, 179);
static constexpr unsigned int BUTTON_DISABLED_TINT = color_rgb(115, 115, 115);
static constexpr unsigned int BUTTON_ACTIVE_TINT = color_rgb(217, 217, 217);

bool ui_button(const char *label, float width, float height);

/* Same as ui_button, but drawn dimmed and never clicked when enabled is false. */
bool ui_button_enabled(const char *label, float width, float height, bool enabled);
bool ui_game_button(const char *id, const char *text, float width, float height, bool enabled, bool pressed);
void ui_game_frame(float left, float top, float right, float bottom);
void ui_push_game_window_style(void);
void ui_pop_game_window_style(void);
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
