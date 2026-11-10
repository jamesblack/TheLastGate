#include "widgets.hpp"

#include <imgui.h>

#include "imgui/imgui_wrapper.h"
#include "launcher_ui.hpp"
#include "ui_common.hpp"
#include "graphics/sdl.h"
#include "config/keybindings.h"
#include <math.h>
#include <stdio.h>

#include "engine.h"
#include "game/game_input.h"
#include "config/config.h"
#include "ui_utils/imgui_style_builder.hpp"

static void button_sprite_render(void *draw_list, const float min_x, const float min_y, const float max_x,
                                 const float max_y, const unsigned int tint) {
    sdl_load_sprite(BUTTON_SPRITE_ID);
    const SpriteData *button = &sprite_data[BUTTON_SPRITE_ID];

    imgui_draw_list_add_image_9_slice(
            draw_list, (void *) (intptr_t) button->gl_texture, min_x, min_y, max_x, max_y, BUTTON_SPRITE_BORDER_WIDTH,
            BUTTON_SPRITE_BORDER_WIDTH, BUTTON_SPRITE_BORDER_WIDTH, BUTTON_SPRITE_BORDER_WIDTH, button->pixel_width,
            button->pixel_height, button->uv0.u, button->uv0.v, button->uv1.u, button->uv1.v, tint);
}

bool ui_button(const char *label, float width, float height) {
    return ui_button_enabled(label, width, height, true);
}

bool ui_button_enabled(const char *label, float width, float height, bool enabled) {
    if (width < 52) width = 52;
    if (height < 19) height = 19;
    return ui_game_button(label, label, width, height, enabled, false);
}

bool ui_game_button(const char *id, const char *text, float width, float height, bool enabled, bool pressed) {
    /* Get draw list BEFORE creating widgets */
    void *draw_list = imgui_get_window_draw_list();

    /* Create invisible button */
    bool clicked = imgui_invisible_button(id, width, height);

    /* Get button rectangle */
    float min_x, min_y, max_x, max_y;
    imgui_get_item_rect_min(&min_x, &min_y);
    imgui_get_item_rect_max(&max_x, &max_y);

    /* Determine button state and tint color */
    unsigned int tint;
    unsigned int text_color;
    if (!enabled) {
        /* Disabled - dimmed sprite and grey text, never reports a click */
        tint = BUTTON_DISABLED_TINT;
        text_color = HINT_GREY_FONT_COLOR;
        clicked = false;
    } else if (pressed) {
        tint = BUTTON_ACTIVE_TINT;
        text_color = BLUE_FONT_COLOR;
    } else if (imgui_is_item_active()) {
        /* Clicked/pressed state - darker */
        tint = BUTTON_ACTIVE_TINT;
        text_color = BUTTON_TEXT_ACTIVE_COLOR;
    } else if (imgui_is_item_hovered()) {
        /* Hovered state - slightly darker */
        tint = BUTTON_HOVER_TINT;
        text_color = BUTTON_TEXT_HOVER_COLOR;
    } else {
        /* Normal state - full brightness */
        tint = WHITE_TINT;
        text_color = BUTTON_TEXT_COLOR;
    }

    /* Draw the 9-slice button background */
    button_sprite_render(draw_list, min_x, min_y, max_x, max_y, tint);

    /* Draw centered text */
    const char *button_text = text;
    float text_width, text_height;
    imgui_calc_text_size_simple(&text_width, &text_height, button_text);

    float text_x = min_x + ((max_x - min_x) - text_width) / 2.0f;
    float text_y = min_y + ((max_y - min_y) - text_height) / 2.0f;

    imgui_draw_list_add_text(draw_list, text_x, text_y, text_color, button_text);

    return clicked;
}

void ui_game_frame(float left, float top, float right, float bottom) {
    sdl_load_sprite(WINDOW_SPRITE_ID);
    const SpriteData *frame = &sprite_data[WINDOW_SPRITE_ID];
    imgui_draw_list_add_image_9_slice(imgui_get_window_draw_list(), (void *) (intptr_t) frame->gl_texture, left, top,
                                      right, bottom, WINDOW_SPRITE_BORDER_WIDTH, WINDOW_SPRITE_BORDER_WIDTH,
                                      WINDOW_SPRITE_BORDER_WIDTH, WINDOW_SPRITE_BORDER_WIDTH, frame->pixel_width,
                                      frame->pixel_height, frame->uv0.u, frame->uv0.v, frame->uv1.u, frame->uv1.v,
                                      WHITE_TINT);
}

static const ImU32 GAME_WINDOW_BACKGROUND = IM_COL32(17, 5, 5, 255);
static const ImU32 GAME_BORDER = IM_COL32(117, 106, 68, 255);
static const ImU32 GAME_BORDER_HOVERED = IM_COL32(168, 147, 90, 255);
static const ImU32 GAME_FIELD = IM_COL32(51, 51, 51, 255);
static const ImU32 GAME_FIELD_HOVERED = IM_COL32(77, 77, 77, 255);
static const ImU32 GAME_FIELD_ACTIVE = IM_COL32(102, 102, 102, 255);
static const ImU32 GAME_ROW = IM_COL32(58, 16, 16, 255);
static const ImU32 GAME_ROW_HOVERED = IM_COL32(74, 24, 24, 255);
static const ImU32 GAME_ROW_ACTIVE = IM_COL32(90, 32, 32, 255);
static const ImU32 GAME_TEXT_SELECTION = IM_COL32(90, 32, 32, 200);
static const ImU32 GAME_SCROLL_TRACK = IM_COL32(23, 0, 5, 255);
static const ImU32 GAME_SCROLL_GRAB = IM_COL32(0, 97, 0, 255);
static const ImU32 GAME_SCROLL_GRAB_HOVERED = IM_COL32(0, 122, 0, 255);
static const ImU32 GAME_SCROLL_GRAB_ACTIVE = IM_COL32(0, 148, 0, 255);

static const struct {
    ImGuiCol slot;
    ImU32 colour;
} GAME_WINDOW_COLOURS[] = {
    {ImGuiCol_Text, GOLD_FONT_COLOR},
    {ImGuiCol_TextDisabled, HINT_GREY_FONT_COLOR},
    {ImGuiCol_WindowBg, GAME_WINDOW_BACKGROUND},
    {ImGuiCol_ChildBg, TRANSPARENT_COLOR},
    {ImGuiCol_PopupBg, GAME_WINDOW_BACKGROUND},
    {ImGuiCol_Border, GAME_BORDER},
    {ImGuiCol_BorderShadow, TRANSPARENT_COLOR},
    {ImGuiCol_FrameBg, GAME_FIELD},
    {ImGuiCol_FrameBgHovered, GAME_FIELD_HOVERED},
    {ImGuiCol_FrameBgActive, GAME_FIELD_ACTIVE},
    {ImGuiCol_TitleBg, GAME_WINDOW_BACKGROUND},
    {ImGuiCol_TitleBgActive, GAME_WINDOW_BACKGROUND},
    {ImGuiCol_TitleBgCollapsed, GAME_WINDOW_BACKGROUND},
    {ImGuiCol_CheckMark, GOLD_FONT_COLOR},
    {ImGuiCol_Button, GAME_ROW},
    {ImGuiCol_ButtonHovered, GAME_ROW_HOVERED},
    {ImGuiCol_ButtonActive, GAME_ROW_ACTIVE},
    {ImGuiCol_Header, GAME_ROW},
    {ImGuiCol_HeaderHovered, GAME_ROW_HOVERED},
    {ImGuiCol_HeaderActive, GAME_ROW_ACTIVE},
    {ImGuiCol_Separator, GAME_BORDER},
    {ImGuiCol_SeparatorHovered, GAME_BORDER_HOVERED},
    {ImGuiCol_SeparatorActive, GOLD_FONT_COLOR},
    {ImGuiCol_ResizeGrip, GAME_BORDER},
    {ImGuiCol_ResizeGripHovered, GAME_BORDER_HOVERED},
    {ImGuiCol_ResizeGripActive, GOLD_FONT_COLOR},
    {ImGuiCol_ScrollbarBg, GAME_SCROLL_TRACK},
    {ImGuiCol_ScrollbarGrab, GAME_SCROLL_GRAB},
    {ImGuiCol_ScrollbarGrabHovered, GAME_SCROLL_GRAB_HOVERED},
    {ImGuiCol_ScrollbarGrabActive, GAME_SCROLL_GRAB_ACTIVE},
    {ImGuiCol_TextSelectedBg, GAME_TEXT_SELECTION},
};

static const int GAME_WINDOW_COLOUR_COUNT = (int) (sizeof(GAME_WINDOW_COLOURS) / sizeof(GAME_WINDOW_COLOURS[0]));
static const int GAME_WINDOW_VAR_COUNT = 11;
static const float GAME_WINDOW_PADDING = 10.0F;

void ui_push_game_window_style(void) {
    for (int colour_index = 0; colour_index < GAME_WINDOW_COLOUR_COUNT; colour_index++) {
        ImGui::PushStyleColor(GAME_WINDOW_COLOURS[colour_index].slot, GAME_WINDOW_COLOURS[colour_index].colour);
    }
    // Frames are as tall as the button sprite so text, fields and buttons share one row height
    const float frame_padding_y = floorf((BUTTON_SPRITE_HEIGHT - ImGui::GetFontSize()) / 2.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(GAME_WINDOW_PADDING, GAME_WINDOW_PADDING));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0F, frame_padding_y > 0.0F ? frame_padding_y : 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0F);
}

void ui_pop_game_window_style(void) {
    ImGui::PopStyleVar(GAME_WINDOW_VAR_COUNT);
    ImGui::PopStyleColor(GAME_WINDOW_COLOUR_COUNT);
}

bool tab_button(const char *label, bool is_active, float width) {
    void *draw_list = imgui_get_window_draw_list();

    /* Create invisible button for interaction */
    bool clicked = imgui_invisible_button(label, width, 30.0f);

    /* Get button rectangle */
    float min_x, min_y, max_x, max_y;
    imgui_get_item_rect_min(&min_x, &min_y);
    imgui_get_item_rect_max(&max_x, &max_y);

    /* Determine button state and tint color */
    unsigned int tint;
    if (is_active) {
        /* Active tab - brighter */
        tint = WHITE_TINT;
    } else if (imgui_is_item_hovered()) {
        /* Hovered inactive tab */
        tint = BUTTON_ACTIVE_TINT;
    } else {
        /* Inactive tab - darker */
        tint = BUTTON_HOVER_TINT;
    }

    /* Draw the 9-slice button background */
    button_sprite_render(draw_list, min_x, min_y, max_x, max_y, tint);

    /* Draw centered text */
    float text_width, text_height;
    imgui_calc_text_size_simple(&text_width, &text_height, label);

    float text_x = min_x + ((max_x - min_x) - text_width) / 2.0f;
    float text_y = min_y + ((max_y - min_y) - text_height) / 2.0f;

    /* Text color: blue if active, gold if inactive */
    unsigned int text_color = is_active ? BLUE_FONT_COLOR : GOLD_FONT_COLOR;

    imgui_draw_list_add_text(draw_list, text_x, text_y, text_color, label);

    return clicked;
}

void ui_tooltip(const char *tooltip_text) {
    /* Custom styled tooltip with 9-slice background */
    float padding = 8.0f;
    float max_width = 200.0f;

    /* Calculate text size with padding */
    float text_width, text_height;
    imgui_calc_text_size(&text_width, &text_height, tooltip_text, NULL, false, max_width - padding * 2);

    /* Set padding for tooltip window */
    imgui_push_style_var_vec2(IMGUI_STYLE_VAR_WINDOW_PADDING, padding, padding);
    imgui_push_style_color(IMGUI_COL_POPUP_BG, 0.0f, 0.0f, 0.0f, 0.0f); /* Transparent background */

    imgui_begin_tooltip();

    /* Get window position and size for background */
    float win_x = imgui_get_window_pos_x();
    float win_y = imgui_get_window_pos_y();
    float win_width, win_height;
    imgui_get_content_region_avail(&win_width, &win_height);

    /* Calculate actual window bounds including padding */
    float min_x = win_x;
    float min_y = win_y;
    float max_x = win_x + text_width + padding * 2;
    float max_y = win_y + text_height + padding * 2;

    /* Draw 9-slice background behind everything */
    void *draw_list = imgui_get_window_draw_list();

    button_sprite_render(draw_list, min_x, min_y, max_x, max_y, WHITE_TINT);

    /* Draw text with natural ImGui layout */
    imgui_push_text_wrap_pos(imgui_get_cursor_pos_x() + max_width - padding * 2);
    imgui_push_style_color_32(IMGUI_COL_TEXT, GOLD_FONT_COLOR);
    imgui_text_wrapped(tooltip_text);
    imgui_pop_style_color(1);
    imgui_pop_text_wrap_pos();

    imgui_end_tooltip();
    imgui_pop_style_color(1);
    imgui_pop_style_var(1);
}

/* The colours of one dropdown look; ImU32 colours are 0xAABBGGRR. */
typedef struct {
    ImU32 box_text;
    ImU32 border;
    ImU32 box_background;
    ImU32 box_background_hovered;
    ImU32 box_background_active;
    ImU32 list_background;
    ImU32 selected;
    ImU32 hovered;
    ImU32 active;
    float border_size;
} DropdownPalette;

/* Parchment box with black text and a purple-red list. */
static constexpr DropdownPalette LAUNCHER_DROPDOWN_PALETTE = {
    .box_text = LAUNCHER_BLACK_COLOR,
    .border = LAUNCHER_BORDER_COLOR,
    .box_background = LAUNCHER_BEIGE_COLOR,
    .box_background_hovered = LAUNCHER_BEIGE_HOVERED_COLOR,
    .box_background_active = LAUNCHER_BEIGE_ACTIVE_COLOR,
    .list_background = LAUNCHER_INNER_WINDOW_COLOR,
    .selected = LAUNCHER_DROPDOWN_SELECTED_COLOR,
    .hovered = LAUNCHER_DROPDOWN_HIGHLIGHT_COLOR,
    .active = LAUNCHER_DROPDOWN_ACTIVE_COLOR,
    .border_size = 2,
};

/* Gold text on dark red with a thin gold-brown edge, like the HUD's Update button. */
static constexpr DropdownPalette GAME_DROPDOWN_PALETTE = {
    .box_text = GOLD_FONT_COLOR,
    .border = color_rgb(118, 98, 8),
    .box_background = color_rgb(45, 2, 2),
    .box_background_hovered = color_rgb(71, 2, 2),
    .box_background_active = color_rgb(92, 3, 3),
    .list_background = color_rgb(30, 2, 2),
    .selected = color_rgb(110, 20, 10),
    .hovered = color_rgb(90, 15, 8),
    .active = color_rgb(92, 3, 3),
    .border_size = 1,
};

/* Options and descriptions are gold in every look. */
static constexpr ImVec2 DROPDOWN_LIST_PADDING = ImVec2(4, 4);
static constexpr float DROPDOWN_DESCRIPTION_WRAP_FONT_SIZES = 25.0F;
/* Spacing the dropdown sets for itself, so a window's own spacing doesn't squash it. */
static constexpr ImVec2 DROPDOWN_FRAME_PADDING = ImVec2(4, 3);
static constexpr ImVec2 DROPDOWN_ITEM_SPACING = ImVec2(8, 4);

static void dropdown_description_render(const char *description) {
    if (description == nullptr) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal |
                              ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, GOLD_FONT_COLOR);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * DROPDOWN_DESCRIPTION_WRAP_FONT_SIZES);
    ImGui::TextUnformatted(description);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleColor(1);
}

static bool dropdown_render(const DropdownPalette *palette, const char *id, const char *const *options,
                            const int option_count, int *current_option, const char *const *descriptions) {
    bool changed = false;
    auto pop_list_style = ImGuiStyleBuilder()
                                  .Var(ImGuiStyleVar_WindowPadding, DROPDOWN_LIST_PADDING)
                                  .Var(ImGuiStyleVar_FramePadding, DROPDOWN_FRAME_PADDING)
                                  .Var(ImGuiStyleVar_ItemSpacing, DROPDOWN_ITEM_SPACING)
                                  .Color(ImGuiCol_PopupBg, palette->list_background)
                                  .Color(ImGuiCol_Text, GOLD_FONT_COLOR)
                                  .Build();

    auto pop_box_style = ImGuiStyleBuilder()
                                 .Var(ImGuiStyleVar_FrameBorderSize, palette->border_size)
                                 .Color(ImGuiCol_Border, palette->border)
                                 .Color(ImGuiCol_FrameBg, palette->box_background)
                                 .Color(ImGuiCol_FrameBgHovered, palette->box_background_hovered)
                                 .Color(ImGuiCol_FrameBgActive, palette->box_background_active)
                                 .Color(ImGuiCol_Text, palette->box_text)
                                 .Build();
    const bool opened = ImGui::BeginCombo(id, options[*current_option], ImGuiComboFlags_NoArrowButton);
    pop_box_style();
    if (!opened && descriptions != nullptr) dropdown_description_render(descriptions[*current_option]);

    auto pop_option_style = ImGuiStyleBuilder()
                                    .Var(ImGuiStyleVar_PopupBorderSize, palette->border_size)
                                    .Color(ImGuiCol_Header, palette->selected)
                                    .Color(ImGuiCol_HeaderHovered, palette->hovered)
                                    .Color(ImGuiCol_HeaderActive, palette->active)
                                    .Color(ImGuiCol_Border, palette->border)
                                    .Build();
    if (opened) {
        for (int option_index = 0; option_index < option_count; option_index++) {
            const bool selected = *current_option == option_index;

            if (ImGui::Selectable(options[option_index], selected)) {
                changed = true;
                *current_option = option_index;
            }
            if (descriptions != nullptr) dropdown_description_render(descriptions[option_index]);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    pop_option_style();
    pop_list_style();
    return changed;
}

bool ui_dropdown(const char *id, const char *const *options, const int option_count, int *current_option,
                 const char *const *descriptions) {
    return dropdown_render(&LAUNCHER_DROPDOWN_PALETTE, id, options, option_count, current_option, descriptions);
}

bool ui_game_dropdown(const char *id, const char *const *options, const int option_count, int *current_option,
                      const char *const *descriptions) {
    return dropdown_render(&GAME_DROPDOWN_PALETTE, id, options, option_count, current_option, descriptions);
}
