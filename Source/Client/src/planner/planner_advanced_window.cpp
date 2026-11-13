#include "planner_advanced_window.hpp"

#include <imgui.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "game/game_ui.h"
#include "graphics/sdl.h"
#include "planner/planner_ui.hpp"
#include "ui/imgui/imgui_wrapper.h"
#include "ui/ui_common.hpp"
#include "ui/widgets.hpp"

#define ADVANCED_LEFT 360
#define ADVANCED_TOP 60
#define ADVANCED_WIDTH 560
#define ADVANCED_HEIGHT 600
#define ADVANCED_PADDING 12
#define ADVANCED_HEADER_HEIGHT 26
#define ADVANCED_ROW_HEIGHT BUTTON_SPRITE_HEIGHT
#define ADVANCED_COLUMN_WIDTH 268
#define ADVANCED_CLOSE_WIDTH 56
#define CARDS_PER_COLUMN 5
#define CARD_NUMERAL_COUNT 22
#define TICKBOX_LABEL_GAP 4
#define CARD_ROW_ID_BASE 100
#define CORRUPTOR_ROW_ID_BASE 200
#define CORRUPTOR_MAX_COPIES 20
#define OTHER_MIN_ROWS 3
#define OTHER_EFFECT_LEFT 170
#define OTHER_COUNT_LEFT 300
#define OTHER_COUNT_WIDTH 40
#define OTHER_TREE_LEFT 348
static constexpr float SECTION_GAP = ADVANCED_ROW_HEIGHT / 2.0F;

static const char *const CARD_NUMERALS[CARD_NUMERAL_COUNT] = {
    "0",  "I",   "II",   "III", "IV", "V",   "VI",   "VII",   "VIII", "IX", "X",
    "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX",  "XX", "XXI",
};

static const char *const CARD_NAMES[TAROT_CARD_COUNT] = {
    "The Fool",
    "The Magician",
    "The High Priestess",
    "The Empress",
    "The Emperor",
    "The Hierophant",
    "The Lovers",
    "The Chariot",
    "Strength",
    "The Hermit",
    "Wheel of Fortune",
    "Justice",
    "The Hanged Man",
    "Death",
    "Temperance",
    "The Devil",
    "The Tower",
    "The Star",
    "The Moon",
    "The Sun",
    "Judgement",
    "The World",
    "The Fool, Reversed",
    "The Magician, Reversed",
    "The High Priestess, Reversed",
    "The Empress, Reversed",
    "The Emperor, Reversed",
    "The Hierophant, Reversed",
    "The Lovers, Reversed",
    "The Chariot, Reversed",
    "Strength, Reversed",
    "The Hermit, Reversed",
    "Wheel of Fortune, Reversed",
    "Justice, Reversed",
    "The Hanged Man, Reversed",
    "Death, Reversed",
    "Temperance, Reversed",
    "The Devil, Reversed",
    "The Tower, Reversed",
    "The Star, Reversed",
    "The Moon, Reversed",
    "The Sun, Reversed",
    "Judgement, Reversed",
    "The World, Reversed",
};

const char *planner_card_name(const int card_index) {
    if (card_index < 0 || card_index >= TAROT_CARD_COUNT) return "";
    return CARD_NAMES[card_index];
}

static void frame_render(void) {
    void *draw_list = imgui_get_window_draw_list();
    const float left = imgui_get_window_pos_x();
    const float top = imgui_get_window_pos_y();
    const SpriteData *frame;

    imgui_draw_list_add_rect_filled(draw_list, left, top, left + ADVANCED_WIDTH, top + ADVANCED_HEIGHT,
                                    BACKGROUND_COLOR_LIGHT_MODE, 0, 0);
    sdl_load_sprite(WINDOW_SPRITE_ID);
    frame = &sprite_data[WINDOW_SPRITE_ID];
    void *frame_texture = (void *) (intptr_t) frame->gl_texture;
    imgui_draw_list_add_image_9_slice(draw_list, frame_texture, left, top, left + ADVANCED_WIDTH, top + ADVANCED_HEIGHT,
                                      WINDOW_SPRITE_BORDER_WIDTH, WINDOW_SPRITE_BORDER_WIDTH,
                                      WINDOW_SPRITE_BORDER_WIDTH, WINDOW_SPRITE_BORDER_WIDTH,
                                      (float) frame->pixel_width, (float) frame->pixel_height, frame->uv0.u,
                                      frame->uv0.v, frame->uv1.u, frame->uv1.v, WHITE_TINT);
}

static void text_at(const float left, const float top, const unsigned int color, const char *text) {
    text_in_row(left, top, ADVANCED_ROW_HEIGHT, color, text);
}

static float section_render(const float top, const char *label) {
    text_at(ADVANCED_PADDING, top, BLUE_FONT_COLOR, label);
    return top + ADVANCED_ROW_HEIGHT;
}

static bool tickbox_row(const float left, const float top, const int row_id, bool ticked, const bool enabled,
                        const unsigned int color, const char *label) {
    bool changed;

    imgui_push_id_int(row_id);
    imgui_set_cursor_pos(left, top);
    imgui_begin_disabled(!enabled);
    changed = imgui_checkbox("##tick", &ticked);
    imgui_end_disabled();
    imgui_pop_id();
    text_at(left + ADVANCED_ROW_HEIGHT + TICKBOX_LABEL_GAP, top, enabled ? color : HINT_GREY_FONT_COLOR, label);
    return changed;
}

static float spells_render(const float top, PlannerChoices *choices, const PlannerAdvancedView *view, int *changed) {
    float row_top = section_render(top, "Spells");
    char label_text[LABEL_TEXT_SIZE];
    int kind;

    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
        const PlannerAdvancedSpell *spell = &view->spells[kind];
        const unsigned int spell_bit = 1U << (unsigned int) kind;

        if (spell->known == 0) continue;
        snprintf(label_text, sizeof(label_text), spell->ready != 0 ? "%s" : "%s (reading buffs...)", spell->name);
        if (tickbox_row(ADVANCED_PADDING, row_top, kind, (choices->spell_toggles & spell_bit) != 0, spell->ready != 0,
                        GOLD_FONT_COLOR, label_text)) {
            choices->spell_toggles ^= spell_bit;
            *changed = 1;
        }
        row_top += ADVANCED_ROW_HEIGHT;
        if (spell->warning != NULL) {
            text_at(ADVANCED_PADDING + ADVANCED_ROW_HEIGHT + TICKBOX_LABEL_GAP, row_top, HINT_GREY_FONT_COLOR,
                    spell->warning);
            row_top += ADVANCED_ROW_HEIGHT;
        }
    }
    return row_top;
}

static float cards_render(const float top, PlannerChoices *choices, const PlannerAdvancedView *view, int *changed) {
    const float rows_top = section_render(top, "Tarot cards");
    char label_text[LABEL_TEXT_SIZE];
    int card_index;

    for (card_index = 0; card_index < TAROT_MODELLED_CARD_COUNT; card_index++) {
        const int card = TAROT_MODELLED_CARDS[card_index];
        const int column = card_index / CARDS_PER_COLUMN;
        const int row_number = card_index % CARDS_PER_COLUMN;
        const float row_left = (float) (ADVANCED_PADDING + (column * ADVANCED_COLUMN_WIDTH));
        const float row_top = rows_top + (float) (row_number * ADVANCED_ROW_HEIGHT);
        const TarotSet card_bit = (TarotSet) 1 << (unsigned int) card;

        snprintf(label_text, sizeof(label_text), "%s %s", CARD_NUMERALS[card % CARD_NUMERAL_COUNT], CARD_NAMES[card]);
        if (tickbox_row(row_left, row_top, CARD_ROW_ID_BASE + card, (choices->tarot_cards & card_bit) != 0, true,
                        GOLD_FONT_COLOR, label_text)) {
            choices->tarot_cards ^= card_bit;
            *changed = 1;
        }
    }
    return rows_top + (CARDS_PER_COLUMN * ADVANCED_ROW_HEIGHT);
}

static void corruptor_count_render(const int corruptor, const bool ticked, PlannerChoices *choices, int *changed) {
    int count = choices->corruptor_counts[corruptor];

    imgui_push_id_int(CORRUPTOR_ROW_ID_BASE + corruptor);
    imgui_begin_disabled(!ticked);
    ImGui::SetNextItemWidth(OTHER_COUNT_WIDTH);
    if (ImGui::InputInt("##count", &count, 0, 0)) {
        /* 0 is allowed while typing so the field can be cleared; it becomes 1 when editing ends. */
        if (count < 0) count = 0;
        if (count > CORRUPTOR_MAX_COPIES) count = CORRUPTOR_MAX_COPIES;
        choices->corruptor_counts[corruptor] = (unsigned char) count;
        *changed = 1;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && choices->corruptor_counts[corruptor] == 0) {
        choices->corruptor_counts[corruptor] = 1;
        *changed = 1;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ui_tooltip("Copies on tarot cards and sinbinder rings. A sinbinder with a Sign of Symmetry counts twice.");
    }
    imgui_end_disabled();
    imgui_pop_id();
}

static void corruptor_row_render(const float row_top, const int corruptor, PlannerChoices *choices,
                                 const PlannerAdvancedView *view, int *changed) {
    const unsigned int tick_bit = 1U << (unsigned int) corruptor;
    const CorruptorInfo *info = &CORRUPTOR_INFO[corruptor];
    const bool ticked = (choices->corruptor_ticks & tick_bit) != 0;
    char tree_text[LABEL_TEXT_SIZE];

    if (tickbox_row(0, row_top, CORRUPTOR_ROW_ID_BASE + corruptor, ticked, true, GOLD_FONT_COLOR, info->name)) {
        choices->corruptor_ticks ^= tick_bit;
        if (!ticked && choices->corruptor_counts[corruptor] == 0) choices->corruptor_counts[corruptor] = 1;
        *changed = 1;
    }
    text_at(OTHER_EFFECT_LEFT, row_top, HINT_GREY_FONT_COLOR, info->modelled != 0 ? info->one_copy : "(not modelled)");
    imgui_set_cursor_pos(OTHER_COUNT_LEFT, row_top);
    corruptor_count_render(corruptor, ticked, choices, changed);
    if (view->tree_copies[corruptor] > 0) {
        snprintf(tree_text, sizeof(tree_text), "+%d tree", view->tree_copies[corruptor]);
        text_at(OTHER_TREE_LEFT, row_top, HINT_GREY_FONT_COLOR, tree_text);
    }
}

static void other_render(const float top, PlannerChoices *choices, const PlannerAdvancedView *view, int *changed) {
    const float rows_top = section_render(top, "Other");
    const float remaining_height = ADVANCED_HEIGHT - rows_top - ADVANCED_PADDING;
    const float min_height = OTHER_MIN_ROWS * ADVANCED_ROW_HEIGHT;
    const float box_height = remaining_height > min_height ? remaining_height : min_height;
    float row_top = 0;
    int row_index;
    int corruptor;

    imgui_set_cursor_pos(ADVANCED_PADDING, rows_top);
    if (imgui_begin_child("other_rows", ADVANCED_WIDTH - (2 * ADVANCED_PADDING), box_height, false, 0)) {
        for (row_index = 0; row_index < view->tree_row_count; row_index++) {
            const unsigned int grey = HINT_GREY_FONT_COLOR;

            text_at(ADVANCED_ROW_HEIGHT + TICKBOX_LABEL_GAP, row_top, grey, view->tree_rows[row_index].name);
            text_at(OTHER_EFFECT_LEFT, row_top, grey, view->tree_rows[row_index].effect);
            row_top += ADVANCED_ROW_HEIGHT;
        }
        for (corruptor = 0; corruptor < CORRUPTOR_COUNT; corruptor++) {
            corruptor_row_render(row_top, corruptor, choices, view, changed);
            row_top += ADVANCED_ROW_HEIGHT;
        }
        imgui_set_cursor_pos(0, row_top);
        imgui_dummy(1, 1);
    }
    imgui_end_child();
}

int planner_advanced_window_render(PlannerChoices *choices, const PlannerAdvancedView *view) {
    int changed = 0;

    if (!game_ui_state.show_planner_advanced) return 0;

    ui_push_game_window_style();
    imgui_set_next_window_pos(ADVANCED_LEFT, ADVANCED_TOP);
    imgui_set_next_windows_size(ADVANCED_WIDTH, ADVANCED_HEIGHT);
    imgui_push_style_var_float(IMGUI_STYLE_VAR_WINDOW_BORDER_SIZE, 0);
    imgui_push_style_var_vec2(IMGUI_STYLE_VAR_WINDOW_PADDING, 0, 0);
    if (imgui_begin("Planner Advanced", NULL,
                    IMGUI_WINDOW_FLAG_NO_TITLE_BAR | IMGUI_WINDOW_FLAG_NO_RESIZE | IMGUI_WINDOW_FLAG_NO_MOVE |
                            IMGUI_WINDOW_FLAG_NO_BACKGROUND | IMGUI_WINDOW_FLAG_NO_SCROLLBAR |
                            IMGUI_WINDOW_FLAG_NO_SCROLL_WITH_MOUSE | IMGUI_WINDOW_FLAG_NO_COLLAPSE)) {
        float row_top;

        frame_render();
        text_at(ADVANCED_PADDING, ADVANCED_PADDING, GOLD_FONT_COLOR, "Advanced");
        imgui_set_cursor_pos(ADVANCED_WIDTH - ADVANCED_PADDING - ADVANCED_CLOSE_WIDTH, ADVANCED_PADDING);
        if (ui_button("Close", ADVANCED_CLOSE_WIDTH, BUTTON_SPRITE_HEIGHT)) game_ui_state.show_planner_advanced = false;

        row_top = spells_render(ADVANCED_PADDING + ADVANCED_HEADER_HEIGHT, choices, view, &changed);
        row_top = cards_render(row_top + SECTION_GAP, choices, view, &changed);
        other_render(row_top + SECTION_GAP, choices, view, &changed);
    }
    imgui_end();
    imgui_pop_style_var(2);
    ui_pop_game_window_style();
    return changed;
}
