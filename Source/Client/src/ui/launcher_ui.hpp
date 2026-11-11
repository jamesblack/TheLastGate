#pragma once

#include "ui_common.hpp"

static constexpr unsigned int LAUNCHER_BLACK_COLOR = color_rgb(0, 0, 0);
static constexpr unsigned int LAUNCHER_GOLD_COLOR = color_rgb(206, 171, 74);
static constexpr unsigned int LAUNCHER_BEIGE_COLOR = color_rgb(233, 216, 175);
static constexpr unsigned int LAUNCHER_BEIGE_HOVERED_COLOR = color_rgb(216, 182, 113);
static constexpr unsigned int LAUNCHER_BEIGE_ACTIVE_COLOR = color_rgb(207, 159, 80);
static constexpr unsigned int LAUNCHER_BUTTON_COLOR = color_rgb(206, 171, 75);
static constexpr unsigned int LAUNCHER_BUTTON_HOVERED_COLOR = color_rgb(188, 147, 54);
static constexpr unsigned int LAUNCHER_BUTTON_ACTIVE_COLOR = color_rgb(162, 116, 44);
static constexpr unsigned int LAUNCHER_BUTTON_DISABLED_COLOR = color_rgb(62, 53, 13);
static constexpr unsigned int LAUNCHER_BORDER_COLOR = color_rgb(117, 106, 68);
static constexpr unsigned int LAUNCHER_HEADER_TEXT_COLOR = color_rgb(30, 129, 171);
static constexpr unsigned int LAUNCHER_DROPDOWN_SELECTED_COLOR = color_rgb(76, 67, 127);
static constexpr unsigned int LAUNCHER_DROPDOWN_HIGHLIGHT_COLOR = color_rgb(70, 64, 111);
static constexpr unsigned int LAUNCHER_DROPDOWN_ACTIVE_COLOR = color_rgba(41, 37, 65, 127);
static constexpr unsigned int LAUNCHER_INNER_WINDOW_COLOR = color_rgb(4, 0, 33);

void launcher_ui_render(void);
