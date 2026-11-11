#pragma once

#define WINDOW_SPRITE_ID 18128
#define WINDOW_SPRITE_BORDER_WIDTH 6
#define WINDOW_SPRITE_WIDTH 50
#define WINDOW_SPRITE_HEIGHT 50

#define BUTTON_SPRITE_ID 18129
#define BUTTON_SPRITE_BORDER_WIDTH 3
#define BUTTON_SPRITE_WIDTH 52
#define BUTTON_SPRITE_HEIGHT 19

static constexpr unsigned int COLOR_GREEN_SHIFT = 8U;
static constexpr unsigned int COLOR_BLUE_SHIFT = 16U;
static constexpr unsigned int COLOR_ALPHA_SHIFT = 24U;
static constexpr unsigned int COLOR_OPAQUE = 255U;

static constexpr unsigned int color_rgba(const unsigned int red, const unsigned int green, const unsigned int blue,
                                         const unsigned int alpha) {
    return (alpha << COLOR_ALPHA_SHIFT) | (blue << COLOR_BLUE_SHIFT) | (green << COLOR_GREEN_SHIFT) | red;
}

static constexpr unsigned int color_rgb(const unsigned int red, const unsigned int green, const unsigned int blue) {
    return color_rgba(red, green, blue, COLOR_OPAQUE);
}

static constexpr unsigned int BACKGROUND_COLOR_DARK_MODE = color_rgb(23, 0, 5);
static const float GOLD_FONT_COLOR[3] = {243.0f / 255.0f, 219.0f / 255.0f, 106.0f / 255.0f};
static constexpr unsigned int GOLD_FONT_COLOR_32 = color_rgb(243, 219, 106);
static const float BLUE_FONT_COLOR[3] = {73.0f / 255.0f, 188.0f / 255.0f, 226.0f / 255.0f};
static constexpr unsigned int TRANSPARENT_COLOR_32 = color_rgba(255, 255, 255, 0);

static constexpr unsigned int HINT_GREY_FONT_COLOR_32 = color_rgb(72, 81, 81);