#pragma once

#include "ui/imgui/imgui_wrapper.h"

#define LABEL_TEXT_SIZE 64

static void text_in_row(const float left, const float top, const float row_height, const unsigned int color,
                        const char *text) {
    imgui_set_cursor_pos(left, top + ((row_height - imgui_get_text_line_height()) / 2));
    imgui_text_colored_32(color, text);
}
