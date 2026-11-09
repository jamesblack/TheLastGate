#include "widgets.hpp"

#include <imgui.h>

#include "imgui/imgui_wrapper.h"
#include "ui_common.hpp"
#include "graphics/sdl.h"
#include "config/keybindings.h"
#include <stdio.h>

#include "engine.h"
#include "game/game_input.h"
#include "config/config.h"
#include "ui_utils/imgui_style_builder.hpp"

bool ui_button(const char *label, float width, float height) {
    return ui_button_enabled(label, width, height, true);
}

bool ui_button_enabled(const char *label, float width, float height, bool enabled) {
    if (width < 52) width = 52;
    if (height < 19) height = 19;
    sdl_load_sprite(BUTTON_SPRITE_ID);
    GLuint button_texture = sprite_data[BUTTON_SPRITE_ID].gl_texture;

    /* Get draw list BEFORE creating widgets */
    void *draw_list = imgui_get_window_draw_list();

    /* Create invisible button */
    bool clicked = imgui_invisible_button(label, width, height);

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
        text_color = HINT_GREY_FONT_COLOR_32;
        clicked = false;
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
        tint = BUTTON_TINT;
        text_color = BUTTON_TEXT_COLOR;
    }

    /* Draw the 9-slice button background */
    imgui_draw_list_add_image_9_slice(
        draw_list,
        (void *) (intptr_t) button_texture,
        min_x, min_y, max_x, max_y,
        BUTTON_SPRITE_BORDER_WIDTH,
        BUTTON_SPRITE_BORDER_WIDTH,
        BUTTON_SPRITE_BORDER_WIDTH,
        BUTTON_SPRITE_BORDER_WIDTH,
        sprite_data[BUTTON_SPRITE_ID].pixel_width,
        sprite_data[BUTTON_SPRITE_ID].pixel_height,
        sprite_data[BUTTON_SPRITE_ID].uv0.u,
        sprite_data[BUTTON_SPRITE_ID].uv0.v,
        sprite_data[BUTTON_SPRITE_ID].uv1.u,
        sprite_data[BUTTON_SPRITE_ID].uv1.v,
        tint
    );

    /* Draw centered text */
    const char *button_text = label;
    float text_width, text_height;
    imgui_calc_text_size_simple(&text_width, &text_height, button_text);

    float text_x = min_x + ((max_x - min_x) - text_width) / 2.0f;
    float text_y = min_y + ((max_y - min_y) - text_height) / 2.0f;

    imgui_draw_list_add_text(draw_list, text_x, text_y, text_color, button_text);

    return clicked;
}

bool tab_button(const char *label, bool is_active, float width) {
    void *draw_list = imgui_get_window_draw_list();

    /* Create invisible button for interaction */
    bool clicked = imgui_invisible_button(label, width, 30.0f);

    /* Get button rectangle */
    float min_x, min_y, max_x, max_y;
    imgui_get_item_rect_min(&min_x, &min_y);
    imgui_get_item_rect_max(&max_x, &max_y);

    /* Load button sprite */
    sdl_load_sprite(BUTTON_SPRITE_ID);
    GLuint button_texture = sprite_data[BUTTON_SPRITE_ID].gl_texture;

    /* Determine button state and tint color */
    unsigned int tint;
    if (is_active) {
        /* Active tab - brighter */
        tint = imgui_color_convert_float4_to_u32(1.0f, 1.0f, 1.0f, 1.0f);
    } else if (imgui_is_item_hovered()) {
        /* Hovered inactive tab */
        tint = imgui_color_convert_float4_to_u32(0.85f, 0.85f, 0.85f, 1.0f);
    } else {
        /* Inactive tab - darker */
        tint = imgui_color_convert_float4_to_u32(0.7f, 0.7f, 0.7f, 1.0f);
    }

    /* Draw the 9-slice button background */
    imgui_draw_list_add_image_9_slice(
        draw_list,
        (void *) (intptr_t) button_texture,
        min_x, min_y, max_x, max_y,
        BUTTON_SPRITE_BORDER_WIDTH,
        BUTTON_SPRITE_BORDER_WIDTH,
        BUTTON_SPRITE_BORDER_WIDTH,
        BUTTON_SPRITE_BORDER_WIDTH,
        sprite_data[BUTTON_SPRITE_ID].pixel_width,
        sprite_data[BUTTON_SPRITE_ID].pixel_height,
        sprite_data[BUTTON_SPRITE_ID].uv0.u,
        sprite_data[BUTTON_SPRITE_ID].uv0.v,
        sprite_data[BUTTON_SPRITE_ID].uv1.u,
        sprite_data[BUTTON_SPRITE_ID].uv1.v,
        tint
    );

    /* Draw centered text */
    float text_width, text_height;
    imgui_calc_text_size_simple(&text_width, &text_height, label);

    float text_x = min_x + ((max_x - min_x) - text_width) / 2.0f;
    float text_y = min_y + ((max_y - min_y) - text_height) / 2.0f;

    /* Text color: blue if active, gold if inactive */
    unsigned int text_color = is_active
                                  ? imgui_color_convert_float4_to_u32(BLUE_FONT_COLOR[0], BLUE_FONT_COLOR[1],
                                                                      BLUE_FONT_COLOR[2], 1.0f)
                                  : imgui_color_convert_float4_to_u32(GOLD_FONT_COLOR[0], GOLD_FONT_COLOR[1],
                                                                      GOLD_FONT_COLOR[2], 1.0f);

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

    /* Load and draw 9-slice background behind everything */
    sdl_load_sprite(BUTTON_SPRITE_ID);
    GLuint button_texture = sprite_data[BUTTON_SPRITE_ID].gl_texture;
    void *draw_list = imgui_get_window_draw_list();

    imgui_draw_list_add_image_9_slice(
        draw_list,
        (void *) (intptr_t) button_texture,
        min_x, min_y, max_x, max_y,
        BUTTON_SPRITE_BORDER_WIDTH,
        BUTTON_SPRITE_BORDER_WIDTH,
        BUTTON_SPRITE_BORDER_WIDTH,
        BUTTON_SPRITE_BORDER_WIDTH,
        sprite_data[BUTTON_SPRITE_ID].pixel_width,
        sprite_data[BUTTON_SPRITE_ID].pixel_height,
        sprite_data[BUTTON_SPRITE_ID].uv0.u,
        sprite_data[BUTTON_SPRITE_ID].uv0.v,
        sprite_data[BUTTON_SPRITE_ID].uv1.u,
        sprite_data[BUTTON_SPRITE_ID].uv1.v,
        BUTTON_TINT
    );

    /* Draw text with natural ImGui layout */
    imgui_push_text_wrap_pos(imgui_get_cursor_pos_x() + max_width - padding * 2);
    imgui_push_style_color(IMGUI_COL_TEXT, GOLD_FONT_COLOR[0], GOLD_FONT_COLOR[1], GOLD_FONT_COLOR[2], 1.0f);
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
    .box_text = 0xFF000000,
    .border = 0xFF446A75,
    .box_background = 0xFFAFD8E9,
    .box_background_hovered = 0xFF71B6D8,
    .box_background_active = 0xFF509FCF,
    .list_background = 0xFF210004,
    .selected = 0xFF7F434C,
    .hovered = 0xFF6F4046,
    .active = 0x7F412529,
    .border_size = 2,
};

/* Gold text on dark red with a thin gold-brown edge, like the HUD's Update button. */
static constexpr DropdownPalette GAME_DROPDOWN_PALETTE = {
    .box_text = 0xFF6ADBF3,
    .border = 0xFF086276,
    .box_background = 0xFF02022D,
    .box_background_hovered = 0xFF020247,
    .box_background_active = 0xFF03035C,
    .list_background = 0xFF02021E,
    .selected = 0xFF0A146E,
    .hovered = 0xFF080F5A,
    .active = 0xFF03035C,
    .border_size = 1,
};

/* Options and descriptions are gold in every look. */
static constexpr ImU32 DROPDOWN_GOLD_TEXT_COLOR = 0xFF6ADBF3;
static const ImVec2 DROPDOWN_LIST_PADDING = ImVec2(4, 4);
/* Spacing the dropdown sets for itself, so a window's own spacing doesn't squash it. */
static const ImVec2 DROPDOWN_FRAME_PADDING = ImVec2(4, 3);
static const ImVec2 DROPDOWN_ITEM_SPACING = ImVec2(8, 4);

static void dropdown_description_render(const char *description) {
    if (description == nullptr) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal |
                              ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, DROPDOWN_GOLD_TEXT_COLOR);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
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
                                  .Color(ImGuiCol_Text, DROPDOWN_GOLD_TEXT_COLOR)
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
