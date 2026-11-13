#include "enhanced_skill_window.hpp"

#include <SDL_keycode.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "engine.h"
#include "inter.h"
#include "main.h"
#include "options.h"
#include "ui/ui_common.hpp"
#include "ui/widgets.hpp"
#include "planner/attribute_pool.h"
#include "planner/buff_focus.h"
#include "planner/buff_model.h"
#include "planner/combat_focus.h"
#include "planner/combat_model.h"
#include "planner/combat_shown.h"
#include "game/game_ui.h"
#include "planner/item_effects.h"
#include "planner/planner_advanced_window.hpp"
#include "planner/planner_choices.h"
#include "planner/planner_ui.hpp"
#include "planner/skill_focus.h"
#include "planner/skill_preview.h"
#include "game/stat_raise.h"
#include "planner/tarot_math.h"
#include "planner/tree_effects.h"
#include "graphics/sdl.h"
#include "ui/imgui/imgui_wrapper.h"
#include "mods/buff_watch.h"
#include "util/math_util.h"

static_assert(BUFF_FOCUS_HITPOINTS_RAISE == STAT_RAISE_HITPOINTS);
static_assert(BUFF_FOCUS_MANA_RAISE == STAT_RAISE_MANA);

typedef struct {
    Vec2 window_position;
    Vec2 window_size;
    float window_padding;
    float frame_border;
    float header_height;
    float row_height;
    float column_width;
    float column_gap;
    /* Offsets inside a stat row, measured from the row's left edge. */
    float base_right;
    float current_right;
    float lower_left;
    float raise_left;
    float glyph_width;
    float preview_left;
    float cost_right;
    float command_button_width;
    /* Skill focus: clickable name area, indent of bundle lines, Plan it button width. */
    float focus_name_right;
    float raise_line_indent;
    float plan_button_width;
    unsigned int background_color;
} EnhancedSkillLayout;

static const EnhancedSkillLayout LAYOUT_LIGHT = {
    .window_position = {326, 25},
    .window_size = {628, 670},
    .window_padding = 10,
    .frame_border = WINDOW_SPRITE_BORDER_WIDTH,
    .header_height = 26,
    .row_height = 14,
    .column_width = 290,
    .column_gap = 14,
    .base_right = 122,
    .current_right = 148,
    .lower_left = 154,
    .raise_left = 166,
    .glyph_width = 10,
    .preview_left = 182,
    .cost_right = 288,
    .command_button_width = 56,
    .focus_name_right = 118,
    .raise_line_indent = 10,
    .plan_button_width = 60,
    .background_color = BACKGROUND_COLOR_LIGHT_MODE,
};

static const EnhancedSkillLayout LAYOUT_DARK = {
    .window_position = {326, 25},
    .window_size = {628, 670},
    .window_padding = 10,
    .frame_border = WINDOW_SPRITE_BORDER_WIDTH,
    .header_height = 26,
    .row_height = 14,
    .column_width = 290,
    .column_gap = 14,
    .base_right = 122,
    .current_right = 148,
    .lower_left = 154,
    .raise_left = 166,
    .glyph_width = 10,
    .preview_left = 182,
    .cost_right = 288,
    .command_button_width = 56,
    .focus_name_right = 118,
    .raise_line_indent = 10,
    .plan_button_width = 60,
    .background_color = BACKGROUND_COLOR_DARK_MODE,
};

static constexpr unsigned int PREVIEW_CHANGED_COLOR = color_rgb(255, 255, 255);
static constexpr unsigned int BASE_VALUE_COLOR = color_rgb(160, 160, 160);
static constexpr unsigned int DIRECT_RAISE_COLOR = color_rgb(125, 220, 106);
static constexpr unsigned int ATTRIBUTE_GAIN_COLOR = color_rgb(90, 180, 255);
static constexpr unsigned int RAISED_ROW_HIGHLIGHT_COLOR = color_rgba(243, 219, 106, 56);
static const float ATTRIBUTE_MARKER_WIDTH = 2;
static constexpr unsigned int COST_TEXT_COLOR = color_rgb(168, 176, 176);
static constexpr unsigned int GLYPH_HOVER_COLOR = color_rgb(255, 255, 255);

#define VALUE_TEXT_SIZE 32

/* skilltab position of the skill the suggestion panel works on, or -1. */
static int focused_skill_position = -1;
/* Skill list version the focus was set under; a newer version means the list was re-sorted. */
static int focused_list_version = -1;
/* The combat stat the suggestion panel works on, or -1; only one of this and the skill is ever set. */
static int focused_combat_stat = -1;

/* Only learned skills that are on the list can be focused. */
static bool is_focusable(const int skill_position) {
    if (skill_position < 0 || skill_position >= MAXSKILL) return false;
    if (skilltab[skill_position].show != 1) return false;
    return pl.skill[skilltab[skill_position].nr][0] > 0;
}

void enhanced_skill_window_clear_focus(void) {
    focused_skill_position = -1;
    focused_combat_stat = -1;
}

static void focus_skill(const int skill_position) {
    focused_combat_stat = -1;
    focused_skill_position = skill_position;
    focused_list_version = stat_raise_skill_list_version();
}

static void toggle_combat_stat_focus(const CombatStat stat) {
    if (focused_combat_stat == (int) stat) {
        enhanced_skill_window_clear_focus();
        return;
    }
    focused_skill_position = -1;
    focused_combat_stat = stat;
}

static void drop_focus_if_list_changed(void) {
    if (focused_skill_position < 0) return;
    if (stat_raise_skill_list_version() != focused_list_version) enhanced_skill_window_clear_focus();
}

typedef struct {
    bool available; /* false until the spell stats have arrived */
    BuffModelInput planned_input;
    BuffModelInput unplanned_input; /* same, with every planned point removed */
    BuffModelResult planned;
    BuffModelResult unplanned;
    int planned_armor; /* Armor Value as the Combat block shows it */
    int unplanned_armor;
} PlannerBuffState;

static PlannerBuffState buff_state;

/* attrib is a signed char where -1 marks an unused slot. */
static int skill_attribute_id(const struct skilltab *skill_entry, const int attribute_slot) {
    const int attribute_id = (int) skill_entry->attrib[attribute_slot];
    return attribute_id >= 0 && attribute_id < ATTRIBUTE_COUNT ? attribute_id : -1;
}

static BuffModelSkill model_skill_for(const int skill_position, const int planned) {
    const struct skilltab *skill_entry = &skilltab[skill_position];
    BuffModelSkill skill;
    int attribute_slot;

    skill.server_total = sk_score(skill_entry->nr);
    skill.planned = planned;
    skill.scale_kind = BUFF_SKILL_SCALE_NONE;
    if (skill_entry->nr == SK_RESIST) skill.scale_kind = BUFF_SKILL_SCALE_RESIST;
    if (skill_entry->nr == SK_IMMUN) skill.scale_kind = BUFF_SKILL_SCALE_IMMUNITY;
    if (skill_entry->nr == SK_TACTICS) skill.scale_kind = BUFF_SKILL_SCALE_TACTICS;
    skill.skill_groups = tree_effects_skill_groups(skill_entry->nr);
    for (attribute_slot = 0; attribute_slot < ATTRIBUTES_PER_SKILL; attribute_slot++) {
        skill.attribute_ids[attribute_slot] = skill_attribute_id(skill_entry, attribute_slot);
    }
    return skill;
}

/* skilltab position of a skill number, or -1 when the list doesn't have it. */
static int skill_position_for_number(const int skill_number) {
    int skill_position;

    for (skill_position = 0; skill_position < MAXSKILL; skill_position++) {
        if (skilltab[skill_position].nr == skill_number) return skill_position;
    }
    return -1;
}

static const EnhancedSkillLayout *get_layout(void) {
    if (do_darkmode) return &LAYOUT_DARK;
    return &LAYOUT_LIGHT;
}

typedef struct {
    const char *name;
    int base;
    int current;
    int direct_raise;   /* points bought for this stat itself */
    int attribute_gain; /* what the plan adds beyond direct_raise: attribute raises and percent effects */
    int buff_shift;     /* shown value without a plan minus the server's value: buffs toggled off or on */
    int cost;
    int raise_index;
    int skill_position; /* skilltab position for skill rows, -1 otherwise */
    bool can_raise;
    bool can_lower;
    bool uses_raised_attribute;
} StatRowView;

static int row_preview(const StatRowView *row_view) {
    return row_view->current + row_view->buff_shift + row_view->direct_raise + row_view->attribute_gain;
}

/* Fills the parts of a row that depend only on its stat_raised slot. */
static void fill_raise_state(StatRowView *row_view, const int raise_index) {
    row_view->raise_index = raise_index;
    row_view->skill_position = raise_index >= STAT_RAISE_SKILL_BASE ? raise_index - STAT_RAISE_SKILL_BASE : -1;
    row_view->direct_raise = stat_raised[raise_index];
    row_view->cost = stat_raise_cost(raise_index);
    row_view->can_raise = row_view->cost != HIGH_VAL && row_view->cost <= pl.points - stat_points_used;
    row_view->can_lower = stat_raised[raise_index] > 0;
}

static StatRowView build_attribute_row(const int attribute_id) {
    StatRowView row_view = {0};

    row_view.name = attribute_names[attribute_id];
    row_view.base = pl.attrib[attribute_id][0];
    row_view.current = at_score(attribute_id);
    fill_raise_state(&row_view, attribute_id);
    if (buff_state.available) {
        row_view.buff_shift = buff_state.unplanned.attributes[attribute_id] - row_view.current;
        row_view.attribute_gain = buff_state.planned.attributes[attribute_id] -
                                  buff_state.unplanned.attributes[attribute_id] - row_view.direct_raise;
    }
    return row_view;
}

static StatRowView build_vital_row(const char *name, const int base, const int current, const int raise_index) {
    StatRowView row_view = {0};

    row_view.name = name;
    row_view.base = base;
    row_view.current = current;
    fill_raise_state(&row_view, raise_index);
    return row_view;
}

static StatRowView build_skill_row(const int skill_position, const int attribute_divisor) {
    const struct skilltab *skill_entry = &skilltab[skill_position];
    const int raise_index = STAT_RAISE_SKILL_BASE + skill_position;
    int attribute_values[ATTRIBUTES_PER_SKILL];
    int attribute_raises[ATTRIBUTES_PER_SKILL];
    StatRowView row_view = {0};
    int attribute_slot;

    for (attribute_slot = 0; attribute_slot < ATTRIBUTES_PER_SKILL; attribute_slot++) {
        const int attribute_id = skill_attribute_id(skill_entry, attribute_slot);
        const bool valid_attribute = attribute_id >= 0;

        attribute_values[attribute_slot] = valid_attribute ? at_score(attribute_id) : 0;
        attribute_raises[attribute_slot] = valid_attribute ? stat_raised[attribute_id] : 0;
    }

    row_view.name = skill_entry->name;
    row_view.base = pl.skill[skill_entry->nr][0];
    row_view.current = sk_score(skill_entry->nr);
    row_view.attribute_gain = skill_preview_attribute_gain(attribute_values, attribute_raises, attribute_divisor);
    row_view.uses_raised_attribute = skill_preview_uses_raised_attribute(attribute_raises) != 0;
    fill_raise_state(&row_view, raise_index);
    if (buff_state.available) {
        const BuffModelSkill unplanned_skill = model_skill_for(skill_position, 0);
        const BuffModelSkill planned_skill = model_skill_for(skill_position, stat_raised[raise_index]);
        const int unplanned_value =
                buff_model_skill_value_with_armor(&buff_state.unplanned_input, &unplanned_skill,
                                                  buff_state.unplanned.attributes, buff_state.unplanned_armor);
        const int planned_value = buff_model_skill_value_with_armor(
                &buff_state.planned_input, &planned_skill, buff_state.planned.attributes, buff_state.planned_armor);

        row_view.buff_shift = unplanned_value - row_view.current;
        row_view.attribute_gain = planned_value - unplanned_value - row_view.direct_raise;
    }
    return row_view;
}

static float text_width(const char *text) {
    float width = 0;
    imgui_calc_text_size_simple(&width, NULL, text);
    return width;
}

/* Draws text at a row-relative position, vertically centred in the row. */
static void row_text(const EnhancedSkillLayout *layout, const float row_left, const float row_top, const float offset,
                     const unsigned int color, const char *text) {
    text_in_row(row_left + offset, row_top, layout->row_height, color, text);
}

static void row_text_right(const EnhancedSkillLayout *layout, const float row_left, const float row_top,
                           const float right_edge, const unsigned int color, const char *text) {
    row_text(layout, row_left, row_top, right_edge - text_width(text), color, text);
}

/* A + or - glyph that acts as a button. Returns true when clicked while enabled. */
static bool glyph_button(const EnhancedSkillLayout *layout, const float row_left, const float row_top,
                         const float offset, const char *glyph, const bool enabled) {
    bool clicked;
    bool hovered;

    imgui_set_cursor_pos(row_left + offset, row_top);
    clicked = imgui_invisible_button(glyph, layout->glyph_width, layout->row_height);
    hovered = imgui_is_item_hovered();
    if (!enabled) {
        row_text(layout, row_left, row_top, offset + 2, HINT_GREY_FONT_COLOR, glyph);
        return false;
    }
    row_text(layout, row_left, row_top, offset + 2, hovered ? GLYPH_HOVER_COLOR : GOLD_FONT_COLOR, glyph);
    return clicked;
}

static int click_multiplier(void) {
    const int key_mods = imgui_get_key_mods();
    const int shift_held = (key_mods & KMOD_SHIFT) != 0 ? 1 : 0;
    const int ctrl_or_alt_held = (key_mods & (KMOD_CTRL | KMOD_ALT)) != 0 ? 1 : 0;
    return stat_raise_click_multiplier(shift_held, ctrl_or_alt_held);
}

static void apply_clicks(const int raise_index, const bool raise) {
    const int click_count = click_multiplier();
    int click_number;

    for (click_number = 0; click_number < click_count; click_number++) {
        const int applied = raise ? stat_raise_try_raise(raise_index) : stat_raise_try_lower(raise_index);
        if (applied == 0) break;
    }
}

static void stat_row_render(const EnhancedSkillLayout *layout, const StatRowView *row_view) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();
    const int preview = row_preview(row_view);
    const bool changed = preview != row_view->current;
    void *draw_list = imgui_get_window_draw_list();
    const float screen_left = imgui_get_window_pos_x() + row_left - imgui_get_scroll_x();
    const float screen_top = imgui_get_window_pos_y() + row_top - imgui_get_scroll_y();
    char value_text[VALUE_TEXT_SIZE];

    /* Rows you bought points for get the gold band; rows fed by a raised attribute get a blue edge marker. */
    if (row_view->direct_raise != 0) {
        imgui_draw_list_add_rect_filled(draw_list, screen_left, screen_top, screen_left + layout->column_width,
                                        screen_top + layout->row_height, RAISED_ROW_HIGHLIGHT_COLOR, 0, 0);
    }
    if (row_view->uses_raised_attribute) {
        imgui_draw_list_add_rect_filled(draw_list, screen_left, screen_top, screen_left + ATTRIBUTE_MARKER_WIDTH,
                                        screen_top + layout->row_height, ATTRIBUTE_GAIN_COLOR, 0, 0);
    }

    if (row_view->skill_position >= 0 && row_view->skill_position == focused_skill_position) {
        imgui_draw_list_add_rect(draw_list, screen_left, screen_top, screen_left + layout->column_width,
                                 screen_top + layout->row_height, GOLD_FONT_COLOR, 0, 0, 1);
    }

    imgui_push_id_int(row_view->raise_index);

    /* Clicking a skill's name focuses it; clicking the focused one again clears the focus. */
    if (row_view->skill_position >= 0) {
        imgui_set_cursor_pos(row_left, row_top);
        if (imgui_invisible_button("focus", layout->focus_name_right, layout->row_height) &&
            is_focusable(row_view->skill_position)) {
            if (focused_skill_position == row_view->skill_position) {
                enhanced_skill_window_clear_focus();
            } else {
                focus_skill(row_view->skill_position);
            }
        }
    }

    row_text(layout, row_left, row_top, 2, GOLD_FONT_COLOR, row_view->name);

    snprintf(value_text, sizeof(value_text), "%d", row_view->base);
    row_text_right(layout, row_left, row_top, layout->base_right, BASE_VALUE_COLOR, value_text);

    snprintf(value_text, sizeof(value_text), "%d", row_view->current);
    row_text_right(layout, row_left, row_top, layout->current_right, GOLD_FONT_COLOR, value_text);

    if (glyph_button(layout, row_left, row_top, layout->lower_left, "-", row_view->can_lower)) {
        apply_clicks(row_view->raise_index, false);
    }
    if (glyph_button(layout, row_left, row_top, layout->raise_left, "+", row_view->can_raise)) {
        apply_clicks(row_view->raise_index, true);
    }

    /* Preview reads "total +bought +from attributes", each part in its own colour. */
    float preview_offset = layout->preview_left;
    snprintf(value_text, sizeof(value_text), "%d", preview);
    row_text(layout, row_left, row_top, preview_offset, changed ? PREVIEW_CHANGED_COLOR : GOLD_FONT_COLOR, value_text);
    preview_offset += text_width(value_text);
    if (row_view->direct_raise != 0) {
        snprintf(value_text, sizeof(value_text), " +%d", row_view->direct_raise);
        row_text(layout, row_left, row_top, preview_offset, DIRECT_RAISE_COLOR, value_text);
        preview_offset += text_width(value_text);
    }
    if (row_view->attribute_gain != 0) {
        snprintf(value_text, sizeof(value_text), " %+d", row_view->attribute_gain);
        row_text(layout, row_left, row_top, preview_offset, ATTRIBUTE_GAIN_COLOR, value_text);
    }

    if (row_view->cost == HIGH_VAL) {
        snprintf(value_text, sizeof(value_text), "-");
    } else {
        snprintf(value_text, sizeof(value_text), "%d", row_view->cost);
    }
    row_text_right(layout, row_left, row_top, layout->cost_right, COST_TEXT_COLOR, value_text);

    imgui_pop_id();

    /* Reserve the row's space so the next row and the scroll region know where this one ended. */
    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width, layout->row_height);
}

static void section_label_render(const EnhancedSkillLayout *layout, const char *label) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();

    row_text(layout, row_left, row_top, 2, BLUE_FONT_COLOR, label);
    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width, layout->row_height);
}

static void value_row_render(const EnhancedSkillLayout *layout, const char *label, const char *value) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();

    row_text(layout, row_left, row_top, 2, GOLD_FONT_COLOR, label);
    row_text_right(layout, row_left, row_top, layout->current_right, GOLD_FONT_COLOR, value);
    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width, layout->row_height);
}

/* Meta stats arrive from the server by name; their positions are looked up once the table is in. */
typedef struct {
    const char *name;
    int index;
} MetaLookup;

static MetaLookup meta_lookups[] = {
    {"Hit Score", -1}, {"Parry Score", -1}, {"Spell Multiplier", -1}, {"Spell Aptitude", -1}, {"Spell Bonus", -1},
};

enum : unsigned char {
    META_HIT,
    META_PARRY,
    META_SPELL_MULTIPLIER,
    META_SPELL_APTITUDE,
    META_SPELL_BONUS,
    META_LOOKUP_COUNT,
};

static const struct MetaStat *find_meta(const int lookup_id) {
    MetaLookup *lookup = &meta_lookups[lookup_id];
    int meta_index;

    if (lookup->index < 0 && meta_stats != NULL) {
        for (meta_index = 0; meta_index < MAXMETA; meta_index++) {
            if (strcmp(meta_stats[meta_index].name, lookup->name) == 0) {
                lookup->index = meta_index;
                break;
            }
        }
    }
    if (lookup->index < 0) return NULL;
    return &meta_stats[lookup->index];
}

#define META_DECIMAL_SCALE 100

/* The meta stat's raw value (hundredths for decimal stats); false while it hasn't arrived. */
static bool meta_value(const int lookup_id, int *value) {
    const struct MetaStat *meta = find_meta(lookup_id);

    if (meta == NULL) return false;
    *value = meta->value;
    return true;
}

static void format_meta_value(const int lookup_id, char *text, const size_t text_size) {
    const struct MetaStat *meta = find_meta(lookup_id);

    if (meta == NULL) {
        snprintf(text, text_size, "-");
        return;
    }
    if (meta->flag != 0) {
        snprintf(text, text_size, "%d.%02d%s", meta->value / META_DECIMAL_SCALE, meta->value % META_DECIMAL_SCALE,
                 meta->affix);
    } else {
        snprintf(text, text_size, "%d%s", meta->value, meta->affix);
    }
}

/* Skill numbers of the combat skills, in CombatSkill order. */
static const int COMBAT_SKILL_NUMBERS[COMBAT_SKILL_COUNT] = {
    SK_HAND, SK_DAGGER, SK_SWORD, SK_AXE, SK_STAFF, SK_TWOHAND, SK_DUAL, SK_SHIELD, SK_TACTICS, SK_GEARMAST, SK_FEROC,
};

#define COMBO_LABEL_WIDTH 46
#define COMBO_WIDTH 92
#define OFFHAND_LABEL_LEFT 146
#define OFFHAND_COMBO_GAP 6

#define PLANNER_CHOICES_FILE "TLGPlanner.dat"

/* Weapon and off-hand the combat numbers assume, saved per character. The first time a character opens the planner
 * they start at the best skill's weapon and are saved, so they stay put after that. */
static PlannerChoices chosen = {PLANNER_CHOICE_NONE, PLANNER_CHOICE_NONE};
static unsigned int chosen_character = 0;
static bool chosen_loaded = false;

static bool spell_toggle_on(const int kind) {
    return ((chosen.spell_toggles >> (unsigned int) kind) & 1U) != 0;
}

static void save_choices(void) {
    planner_choices_save(PLANNER_CHOICES_FILE, okey.usnr, chosen);
}

static void load_choices_for_character(void) {
    if (chosen_loaded && chosen_character == okey.usnr) return;
    chosen = planner_choices_load(PLANNER_CHOICES_FILE, okey.usnr);
    chosen_character = okey.usnr;
    chosen_loaded = true;
}

/* Gear slots left empty, which make Ferocity stronger; charms and the off-hand don't count. */
static int count_empty_gear_slots(void) {
    int empty_slots = 0;
    int slot;

    for (slot = 0; slot <= WN_CHARM2; slot++) {
        if (slot == WN_CHARM || slot == WN_CHARM2 || slot == WN_LHAND) continue;
        if (pl.worn[slot] == 0) empty_slots++;
    }
    return empty_slots;
}

static CombatGearClass combat_gear_class(void) {
    if (IS_SKALD) return COMBAT_GEAR_SKALD;
    if (IS_ARCHTEMPLAR) return COMBAT_GEAR_ARCHTEMPLAR;
    if (IS_TEMPLAR || IS_BRAVER) return COMBAT_GEAR_TEMPLAR_OR_BRAVER;
    return COMBAT_GEAR_OTHER;
}

static bool build_combat_model_input(CombatModelInput *input) {
    const PlannerEffects *effects = &buff_state.planned_input.effects;
    int skill_id;

    memset(input, 0, sizeof(*input));
    if (!buff_state.available) return false;
    if (!meta_value(META_HIT, &input->server_hit) || !meta_value(META_PARRY, &input->server_parry)) return false;

    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
        const int skill_number = COMBAT_SKILL_NUMBERS[skill_id];
        const int skill_position = skill_position_for_number(skill_number);

        input->known[skill_id] = skill_position >= 0 && pl.skill[skill_number][0] > 0 ? 1 : 0;
        input->current[skill_id] = sk_score(skill_number);
        input->shown[skill_id] = input->current[skill_id];
        if (input->known[skill_id] != 0) {
            const BuffModelSkill skill =
                    model_skill_for(skill_position, stat_raised[STAT_RAISE_SKILL_BASE + skill_position]);
            input->shown[skill_id] =
                    buff_model_skill_value(&buff_state.planned_input, &skill, buff_state.planned.attributes);
        }
    }

    load_choices_for_character();
    if (chosen.weapon == PLANNER_CHOICE_NONE || chosen.offhand == PLANNER_CHOICE_NONE) {
        if (chosen.weapon == PLANNER_CHOICE_NONE) {
            chosen.weapon = combat_model_default_weapon(input->known, input->current);
        }
        if (chosen.offhand == PLANNER_CHOICE_NONE) {
            chosen.offhand = combat_model_default_offhand(input->known, input->current);
        }
        save_choices();
    }
    input->weapon = (CombatWeapon) chosen.weapon;
    input->offhand = (CombatOffhand) chosen.offhand;
    input->gear_class = combat_gear_class();
    input->rampart = IS_ARCHTEMPLAR && T_ARTM_SK(10);
    input->lycan = IS_LYCANTH ? 1 : 0;
    input->empty_slots = count_empty_gear_slots();
    input->base_weapon = buff_state.planned.weapon;
    input->base_armor = buff_state.planned.armor;
    input->best_weapon_skill = effects->fists_of_heavens != 0 ? 1 : 0;
    input->hit_feeds_parry = effects->hit_to_parry_percent != 0 ? 1 : 0;
    input->parry_feeds_hit = effects->parry_to_hit_percent != 0 ? 1 : 0;
    return true;
}

static void combat_skills_for(BuffModelSkill skills[COMBAT_SKILL_COUNT], const bool with_plan) {
    int skill_id;

    memset(skills, 0, sizeof(skills[0]) * COMBAT_SKILL_COUNT);
    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
        const int skill_number = COMBAT_SKILL_NUMBERS[skill_id];
        const int skill_position = skill_position_for_number(skill_number);

        if (skill_position < 0 || pl.skill[skill_number][0] <= 0) continue;
        skills[skill_id] =
                model_skill_for(skill_position, with_plan ? stat_raised[STAT_RAISE_SKILL_BASE + skill_position] : 0);
    }
}

static void combat_raise_indices_for(const CombatModelInput *combat, int raise_indices[COMBAT_SKILL_COUNT]) {
    int skill_id;

    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
        raise_indices[skill_id] = -1;
        if (combat->known[skill_id] == 0) continue;
        raise_indices[skill_id] = STAT_RAISE_SKILL_BASE + skill_position_for_number(COMBAT_SKILL_NUMBERS[skill_id]);
    }
}

/* A dropdown over `names`; returns the picked index, or -1 when nothing was picked this frame. */
static int choice_combo_render(const char *combo_id, const char *const *names, const int name_count,
                               const int selected) {
    int current = selected;
    bool changed;

    imgui_push_item_width(COMBO_WIDTH);
    changed = ui_game_dropdown(combo_id, names, name_count, &current, NULL);
    imgui_pop_item_width();
    return changed ? current : -1;
}

/* The equipment the combat numbers assume, which the client can't see for itself. */
static void equipment_row_render(const EnhancedSkillLayout *layout, const CombatModelInput *input) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();
    const bool fists_ticked = input->best_weapon_skill != 0;
    int picked;

    row_text(layout, row_left, row_top, 2, GOLD_FONT_COLOR, "Weapon");
    imgui_set_cursor_pos(row_left + COMBO_LABEL_WIDTH, row_top);
    imgui_begin_disabled(fists_ticked);
    picked = choice_combo_render("##combat_weapon", COMBAT_WEAPON_NAMES, COMBAT_WEAPON_COUNT, input->weapon);
    const bool weapon_hovered = imgui_is_item_hovered_flag(IMGUI_HOVERED_FLAGS_ALLOW_WHEN_DISABLED);
    imgui_end_disabled();
    if (fists_ticked && weapon_hovered) ui_tooltip("Fists of the Heavens uses your best weapon skill");
    if (picked >= 0 && picked != chosen.weapon) {
        chosen.weapon = picked;
        save_choices();
    }

    row_text(layout, row_left, row_top, OFFHAND_LABEL_LEFT, GOLD_FONT_COLOR, "Off-hand");
    imgui_set_cursor_pos(row_left + OFFHAND_LABEL_LEFT + COMBO_LABEL_WIDTH + OFFHAND_COMBO_GAP, row_top);
    picked = choice_combo_render("##combat_offhand", COMBAT_OFFHAND_NAMES, COMBAT_OFFHAND_COUNT, input->offhand);
    if (picked >= 0 && picked != chosen.offhand) {
        chosen.offhand = picked;
        save_choices();
    }

    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width,
                imgui_get_frame_height() > layout->row_height ? imgui_get_frame_height() : layout->row_height);
}

/* The server's value, then the planned one and the difference when they differ. */
static void planned_value_row_render(const EnhancedSkillLayout *layout, const char *label, const int current,
                                     const int shown) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();
    char value_text[VALUE_TEXT_SIZE];

    snprintf(value_text, sizeof(value_text), "%d", current);
    value_row_render(layout, label, value_text);
    if (shown == current) return;

    const float next_row_left = imgui_get_cursor_pos_x();
    const float next_row_top = imgui_get_cursor_pos_y();
    snprintf(value_text, sizeof(value_text), "%d", shown);
    row_text(layout, row_left, row_top, layout->preview_left, PREVIEW_CHANGED_COLOR, value_text);
    const float difference_left = layout->preview_left + text_width(value_text);
    snprintf(value_text, sizeof(value_text), " %+d", shown - current);
    row_text(layout, row_left, row_top, difference_left, ATTRIBUTE_GAIN_COLOR, value_text);
    imgui_set_cursor_pos(next_row_left, next_row_top);
}

/* Clicking a combat stat's name focuses it, like a skill's; clicking the focused one again clears the focus. */
static void combat_stat_row_render(const EnhancedSkillLayout *layout, const CombatStat stat, const int current,
                                   const int shown) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();

    if (focused_combat_stat == (int) stat) {
        const float screen_left = imgui_get_window_pos_x() + row_left - imgui_get_scroll_x();
        const float screen_top = imgui_get_window_pos_y() + row_top - imgui_get_scroll_y();
        imgui_draw_list_add_rect(imgui_get_window_draw_list(), screen_left, screen_top,
                                 screen_left + layout->column_width, screen_top + layout->row_height, GOLD_FONT_COLOR,
                                 0, 0, 1);
    }

    imgui_push_id_int(stat);
    if (imgui_invisible_button("combat_focus", layout->focus_name_right, layout->row_height)) {
        toggle_combat_stat_focus(stat);
    }
    imgui_pop_id();
    imgui_set_cursor_pos(row_left, row_top);
    planned_value_row_render(layout, COMBAT_STAT_NAMES[stat], current, shown);
}

static void combat_block_render(const EnhancedSkillLayout *layout) {
    CombatModelInput input;
    BuffModelSkill combat_skills[COMBAT_SKILL_COUNT];
    CombatValues result;
    char value_text[VALUE_TEXT_SIZE];
    int server_aptitude = 0;

    if (!build_combat_model_input(&input) || !meta_value(META_SPELL_APTITUDE, &server_aptitude)) {
        section_label_render(layout, "Combat (current)");
        format_meta_value(META_HIT, value_text, sizeof(value_text));
        value_row_render(layout, "Hit Score", value_text);
        format_meta_value(META_PARRY, value_text, sizeof(value_text));
        value_row_render(layout, "Parry Score", value_text);
        snprintf(value_text, sizeof(value_text), "%d", pl.weapon);
        value_row_render(layout, "Weapon Value", value_text);
        snprintf(value_text, sizeof(value_text), "%d", pl.armor);
        value_row_render(layout, "Armor Value", value_text);
        format_meta_value(META_SPELL_MULTIPLIER, value_text, sizeof(value_text));
        value_row_render(layout, "Spell Multiplier", value_text);
        format_meta_value(META_SPELL_APTITUDE, value_text, sizeof(value_text));
        value_row_render(layout, "Spell Aptitude", value_text);
        return;
    }

    combat_skills_for(combat_skills, true);
    result = combat_shown_values(&buff_state.planned_input, &buff_state.planned, &input, combat_skills);
    const int shown_aptitude = buff_state.planned.spell_aptitude;
    const bool changed = result.hit != input.server_hit || result.parry != input.server_parry ||
                         result.weapon != pl.weapon || result.armor != pl.armor || shown_aptitude != server_aptitude;

    section_label_render(layout, changed ? "Combat" : "Combat (current)");
    equipment_row_render(layout, &input);
    combat_stat_row_render(layout, COMBAT_STAT_HIT, input.server_hit, result.hit);
    combat_stat_row_render(layout, COMBAT_STAT_PARRY, input.server_parry, result.parry);
    combat_stat_row_render(layout, COMBAT_STAT_WEAPON, pl.weapon, result.weapon);
    combat_stat_row_render(layout, COMBAT_STAT_ARMOR, pl.armor, result.armor);
    format_meta_value(META_SPELL_MULTIPLIER, value_text, sizeof(value_text));
    value_row_render(layout, "Spell Multiplier", value_text);
    planned_value_row_render(layout, "Spell Aptitude", server_aptitude, shown_aptitude);
}

/* Skill numbers of the spells the Buffs section can toggle, in BuffKind order. */
static const int SPELL_SKILL_NUMBERS[BUFF_KIND_COUNT] = {SK_BLESS, SK_ENHANCE, SK_PROTECT};

static int spell_skill_position(const int kind) {
    return skill_position_for_number(SPELL_SKILL_NUMBERS[kind]);
}

static bool spell_known(const int kind) {
    return pl.skill[SPELL_SKILL_NUMBERS[kind]][0] > 0 && spell_skill_position(kind) >= 0;
}

/* A buff that is on the player can only be modelled once its power has been read. */
static bool buff_reading_ready(const int kind) {
    return buff_watch_is_active((BuffKind) kind) == 0 || buff_watch_reading((BuffKind) kind) != NULL;
}

/* How strongly the class turns Willpower and Intuition into spell aptitude, in percent. */
static int spell_race_modifier(void) {
    if (IS_TEMPLAR) return 75;
    if (IS_MERCENARY) return 100;
    if (IS_HARAKIM) return 105;
    if (IS_SEYAN_DU) return 95;
    if (IS_ARCHTEMPLAR) return 80;
    if (IS_SKALD) return 80;
    if (IS_WARRIOR) return 105;
    if (IS_SORCERER) return 105;
    if (IS_SUMMONER) return 110;
    if (IS_ARCHHARAKIM) return 110;
    if (IS_BRAVER) return 115;
    if (IS_LYCANTH) return 100;
    return 100;
}

static TreeEffectsInput tree_effects_input(void) {
    TreeEffectsInput input;
    int node;

    memset(&input, 0, sizeof(input));
    input.tree_class = tree_effects_class_from_kindred(pl.kindred);
    input.tree_points = pl.tree_points;
    for (node = 0; node < TREE_NODE_COUNT; node++) input.tree_node[node] = pl.tree_node[node];
    input.corruptor_ticks = chosen.corruptor_ticks;
    memcpy(input.corruptor_counts, chosen.corruptor_counts, sizeof(input.corruptor_counts));
    return input;
}

static ItemEffectsInput item_effects_input(void) {
    ItemEffectsInput input;

    memset(&input, 0, sizeof(input));
    input.enchant_ticks = chosen.enchant_ticks;
    memcpy(input.enchant_counts, chosen.enchant_counts, sizeof(input.enchant_counts));
    input.gear_ticks = chosen.gear_ticks;
    return input;
}

static bool build_buff_model_input(BuffModelInput *input) {
    int multiplier;
    int aptitude;
    int bonus = 0;
    int attribute_id;
    int kind;

    memset(input, 0, sizeof(*input));
    if (!meta_value(META_SPELL_MULTIPLIER, &multiplier) || !meta_value(META_SPELL_APTITUDE, &aptitude)) return false;
    meta_value(META_SPELL_BONUS, &bonus);

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        input->server_attributes[attribute_id] = at_score(attribute_id);
        input->base_attributes[attribute_id] = pl.attrib[attribute_id][0];
        input->planned_attributes[attribute_id] = stat_raised[attribute_id];
    }
    input->divisor = game_ui_attribute_divisor();
    input->spell_multiplier = multiplier;
    input->spell_bonus = bonus;
    input->spell_aptitude = aptitude;
    input->race_modifier = spell_race_modifier();
    input->reduced_item_buffs = IS_SEYAN_DU || IS_BRAVER;
    input->server_weapon = pl.weapon;
    input->server_armor = pl.armor;
    input->tarot_cards = chosen.tarot_cards;
    const TreeEffectsInput tree_input = tree_effects_input();
    const ItemEffectsInput item_input = item_effects_input();
    item_effects_compute(&tree_input, &item_input, &input->effects);
    input->uncapped_hitpoints = pl.hp[4];
    input->planned_hitpoints = stat_raised[STAT_RAISE_HITPOINTS];
    input->uncapped_mana = pl.mana[4];
    input->planned_mana = stat_raised[STAT_RAISE_MANA];

    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
        const int skill_position = spell_skill_position(kind);
        const BuffReading *reading = buff_watch_reading((BuffKind) kind);

        input->spell_known[kind] = spell_known(kind) ? 1 : 0;
        if (skill_position >= 0) {
            input->spell_skills[kind] =
                    model_skill_for(skill_position, stat_raised[STAT_RAISE_SKILL_BASE + skill_position]);
        } else {
            input->spell_skills[kind].attribute_ids[0] = -1;
            input->spell_skills[kind].attribute_ids[1] = -1;
            input->spell_skills[kind].attribute_ids[2] = -1;
        }
        if (reading != NULL) input->active[kind] = *reading;
        /* A buff from a spell you don't know stays in the numbers; it has no toggle. */
        if (input->spell_known[kind] != 0) {
            input->show_buffed[kind] = spell_toggle_on(kind) && buff_reading_ready(kind) ? 1 : 0;
        } else {
            input->show_buffed[kind] = buff_watch_is_active((BuffKind) kind);
        }
    }
    return true;
}

static void refresh_shown_armor(void) {
    CombatModelInput combat;
    BuffModelSkill planned_skills[COMBAT_SKILL_COUNT];
    BuffModelSkill unplanned_skills[COMBAT_SKILL_COUNT];

    buff_state.planned_armor = pl.armor;
    buff_state.unplanned_armor = pl.armor;
    if (!build_combat_model_input(&combat)) return;
    combat_skills_for(planned_skills, true);
    combat_skills_for(unplanned_skills, false);
    buff_state.planned_armor =
            combat_shown_values(&buff_state.planned_input, &buff_state.planned, &combat, planned_skills).armor;
    buff_state.unplanned_armor =
            combat_shown_values(&buff_state.unplanned_input, &buff_state.unplanned, &combat, unplanned_skills).armor;
}

static void refresh_buff_state(void) {
    int attribute_id;
    int kind;

    buff_state.available = build_buff_model_input(&buff_state.planned_input);
    if (!buff_state.available) return;

    buff_state.unplanned_input = buff_state.planned_input;
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        buff_state.unplanned_input.planned_attributes[attribute_id] = 0;
    }
    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) buff_state.unplanned_input.spell_skills[kind].planned = 0;
    buff_state.unplanned_input.planned_hitpoints = 0;
    buff_state.unplanned_input.planned_mana = 0;

    buff_model_compute(&buff_state.planned_input, &buff_state.planned);
    buff_model_compute(&buff_state.unplanned_input, &buff_state.unplanned);
    refresh_shown_armor();
}

static char spell_warnings[BUFF_KIND_COUNT][LABEL_TEXT_SIZE];

/* A weaker cast of your own doesn't replace the buff already on you. */
static const char *spell_warning(const int kind) {
    const BuffReading *reading = buff_watch_reading((BuffKind) kind);

    if (!buff_state.available || !buff_reading_ready(kind) || !spell_toggle_on(kind) || reading == NULL ||
        buff_state.planned.cast_power[kind] >= reading->power) {
        return NULL;
    }
    snprintf(spell_warnings[kind], sizeof(spell_warnings[kind]), "Your %s (%d) won't replace the active one (%d)",
             skilltab[spell_skill_position(kind)].name, buff_state.planned.cast_power[kind], reading->power);
    return spell_warnings[kind];
}

static char tree_row_names[TREE_NODE_COUNT][LABEL_TEXT_SIZE];

static void add_tree_rows(PlannerAdvancedView *view) {
    const TreeEffectsInput effects_input = tree_effects_input();
    int node;
    int corruptor;

    for (node = 1; node <= TREE_NODE_COUNT; node++) {
        const int corruptor_number = effects_input.tree_node[node - 1];
        PlannerAdvancedTreeRow *row = &view->tree_rows[view->tree_row_count];

        if (tree_effects_node_learned(effects_input.tree_points, node) == 0) continue;
        if (corruptor_number == 0) {
            if (tree_effects_node_in_math(effects_input.tree_class, node) == 0) continue;
            snprintf(tree_row_names[node - 1], sizeof(tree_row_names[node - 1]), "%.*s",
                     (int) sizeof(sk_tree[0][node - 1].name), sk_tree[0][node - 1].name);
            row->name = tree_row_names[node - 1];
        } else {
            corruptor = tree_effects_corruptor_for_server_number(corruptor_number);
            if (corruptor < 0) continue;
            snprintf(tree_row_names[node - 1], sizeof(tree_row_names[node - 1]), "%s%s", CORRUPTOR_INFO[corruptor].name,
                     CORRUPTOR_INFO[corruptor].modelled != 0 ? "" : " (not modelled)");
            row->name = tree_row_names[node - 1];
        }
        view->tree_row_count++;
    }
    for (corruptor = 0; corruptor < CORRUPTOR_COUNT; corruptor++) {
        view->tree_copies[corruptor] = tree_effects_tree_copies(&effects_input, (Corruptor) corruptor);
    }
}

static PlannerAdvancedView build_advanced_view(void) {
    PlannerAdvancedView view;
    int kind;

    memset(&view, 0, sizeof(view));
    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
        view.spells[kind].known = spell_known(kind) ? 1 : 0;
        if (view.spells[kind].known == 0) continue;
        view.spells[kind].ready = buff_state.available && buff_reading_ready(kind) ? 1 : 0;
        view.spells[kind].name = skilltab[spell_skill_position(kind)].name;
        view.spells[kind].warning = spell_warning(kind);
    }
    add_tree_rows(&view);
    return view;
}

#define ADVANCED_BUTTON_WIDTH 80

static void math_header_render(const EnhancedSkillLayout *layout) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();
    const float button_row_height = BUTTON_SPRITE_HEIGHT + 2;

    row_text(layout, row_left, row_top + ((button_row_height - layout->row_height) / 2), 2, BLUE_FONT_COLOR,
             "In the math");
    imgui_set_cursor_pos(row_left + layout->cost_right - ADVANCED_BUTTON_WIDTH, row_top);
    if (ui_button("Advanced...", ADVANCED_BUTTON_WIDTH, BUTTON_SPRITE_HEIGHT)) {
        game_ui_state.show_planner_advanced = true;
    }
    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width, button_row_height);
}

static void math_line_render(const EnhancedSkillLayout *layout, const char *name) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();

    row_text(layout, row_left, row_top, layout->raise_line_indent, GOLD_FONT_COLOR, name);
    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width, layout->row_height);
}

#define MATH_VISIBLE_LINES 5

static void math_lines_render(const EnhancedSkillLayout *layout) {
    PlannerAdvancedView tree_view;
    char line_text[LABEL_TEXT_SIZE];
    int kind;
    int card_index;
    int row_index;
    int corruptor;
    int enchant;
    int gear;

    if (game_ui_hard_mode()) math_line_render(layout, "Hard Mode");
    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) {
        if (spell_known(kind) && spell_toggle_on(kind)) {
            math_line_render(layout, skilltab[spell_skill_position(kind)].name);
        }
    }
    for (card_index = 0; card_index < TAROT_MODELLED_CARD_COUNT; card_index++) {
        const int card = TAROT_MODELLED_CARDS[card_index];
        if (tarot_set_has(chosen.tarot_cards, card) != 0) math_line_render(layout, planner_card_name(card));
    }
    memset(&tree_view, 0, sizeof(tree_view));
    add_tree_rows(&tree_view);
    for (row_index = 0; row_index < tree_view.tree_row_count; row_index++) {
        snprintf(line_text, sizeof(line_text), "%s (skill tree)", tree_view.tree_rows[row_index].name);
        math_line_render(layout, line_text);
    }
    for (corruptor = 0; corruptor < CORRUPTOR_COUNT; corruptor++) {
        const int count = chosen.corruptor_counts[corruptor];

        if ((chosen.corruptor_ticks & (1U << (unsigned int) corruptor)) == 0 || count == 0) continue;
        snprintf(line_text, sizeof(line_text), "%s x%d", CORRUPTOR_INFO[corruptor].name, count);
        math_line_render(layout, line_text);
    }
    const ItemEffectsInput item_input = item_effects_input();
    for (enchant = 0; enchant < ENCHANT_COUNT; enchant++) {
        const int copies = item_effects_enchant_copies(&item_input, (Enchant) enchant);

        if (copies == 0) continue;
        if (ENCHANT_INFO[enchant].counted != 0) {
            snprintf(line_text, sizeof(line_text), "%s x%d", ENCHANT_INFO[enchant].name, copies);
        } else {
            snprintf(line_text, sizeof(line_text), "%s", ENCHANT_INFO[enchant].name);
        }
        math_line_render(layout, line_text);
    }
    for (gear = 0; gear < GEAR_COUNT; gear++) {
        if ((chosen.gear_ticks & (1U << (unsigned int) gear)) == 0) continue;
        snprintf(line_text, sizeof(line_text), "%s%s", GEAR_INFO[gear].name,
                 GEAR_INFO[gear].modelled != 0 ? "" : " (not modelled)");
        math_line_render(layout, line_text);
    }
}

static void math_block_render(const EnhancedSkillLayout *layout) {
    imgui_dummy(layout->column_width, layout->row_height);
    math_header_render(layout);
    if (imgui_begin_child("Planner Math", layout->column_width, MATH_VISIBLE_LINES * layout->row_height, false, 0)) {
        math_lines_render(layout);
    }
    imgui_end_child();
}

#define LEGEND_TEXT_LEFT 24

static void legend_render(const EnhancedSkillLayout *layout) {
    const float row_left = imgui_get_cursor_pos_x();
    float row_top = imgui_get_cursor_pos_y();
    void *draw_list = imgui_get_window_draw_list();
    const float screen_left = imgui_get_window_pos_x() + row_left;
    const float screen_top = imgui_get_window_pos_y() + row_top + layout->row_height;

    section_label_render(layout, "Key");

    imgui_draw_list_add_rect_filled(draw_list, screen_left, screen_top, screen_left + layout->column_width,
                                    screen_top + layout->row_height, RAISED_ROW_HIGHLIGHT_COLOR, 0, 0);
    row_top = imgui_get_cursor_pos_y();
    row_text(layout, row_left, row_top, 2, DIRECT_RAISE_COLOR, "+N");
    row_text(layout, row_left, row_top, LEGEND_TEXT_LEFT, GOLD_FONT_COLOR, "points you bought for it");
    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width, layout->row_height);

    imgui_draw_list_add_rect_filled(draw_list, screen_left, screen_top + layout->row_height,
                                    screen_left + ATTRIBUTE_MARKER_WIDTH, screen_top + (layout->row_height * 2),
                                    ATTRIBUTE_GAIN_COLOR, 0, 0);
    row_top = imgui_get_cursor_pos_y();
    row_text(layout, row_left, row_top, 2, ATTRIBUTE_GAIN_COLOR, "+N");
    row_text(layout, row_left, row_top, LEGEND_TEXT_LEFT, GOLD_FONT_COLOR, "from raised attributes");
    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width, layout->row_height);

    row_top = imgui_get_cursor_pos_y();
    row_text(layout, row_left, row_top, 2, BASE_VALUE_COLOR, "N");
    row_text(layout, row_left, row_top, LEGEND_TEXT_LEFT, GOLD_FONT_COLOR, "base, before gear and buffs");
    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width, layout->row_height);
}

static int to_focus_cost(const int stat_cost) {
    return stat_cost == HIGH_VAL ? SKILL_FOCUS_NO_COST : stat_cost;
}

static SkillFocusInput build_focus_input(const int skill_position) {
    const struct skilltab *skill_entry = &skilltab[skill_position];
    SkillFocusInput input;
    int slot_index;
    int step_index;

    for (slot_index = 0; slot_index < ATTRIBUTES_PER_SKILL; slot_index++) {
        const int attribute_id = skill_attribute_id(skill_entry, slot_index);
        const bool valid_attribute = attribute_id >= 0;

        input.attribute_ids[slot_index] = attribute_id;
        /* The values the rows show, so the suggested +1 is a +1 in the preview. */
        int shown_value = 0;
        if (valid_attribute && buff_state.available) {
            shown_value = buff_state.planned.attributes[attribute_id];
        } else if (valid_attribute) {
            shown_value = at_score(attribute_id) + stat_raised[attribute_id];
        }
        input.attribute_values[slot_index] = shown_value;
        for (step_index = 0; step_index < SKILL_FOCUS_MAX_STEPS; step_index++) {
            input.attribute_step_costs[slot_index][step_index] =
                    valid_attribute ? to_focus_cost(stat_raise_cost_after(attribute_id, step_index))
                                    : SKILL_FOCUS_NO_COST;
        }
    }
    input.divisor = game_ui_attribute_divisor();
    input.direct_cost = to_focus_cost(stat_raise_cost(STAT_RAISE_SKILL_BASE + skill_position));
    input.fool_reversed = tarot_set_has(chosen.tarot_cards, TAROT_FOOL_REVERSED);
    for (slot_index = 0; slot_index < ATTRIBUTE_COUNT; slot_index++) {
        for (step_index = 0; step_index < ATTRIBUTE_POOL_MAX_POINTS; step_index++) {
            input.pool_step_costs[slot_index][step_index] =
                    to_focus_cost(stat_raise_cost_after(slot_index, step_index));
        }
    }
    return input;
}

/* A panel row with a label at an offset and an optional right-aligned value. */
static void panel_row_render(const EnhancedSkillLayout *layout, const float label_offset, const unsigned int color,
                             const char *label, const char *value) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();

    row_text(layout, row_left, row_top, label_offset, color, label);
    if (value != NULL) row_text_right(layout, row_left, row_top, layout->cost_right, color, value);
    imgui_set_cursor_pos(row_left, row_top);
    imgui_dummy(layout->column_width, layout->row_height);
}

static void plan_suggestion(const SkillFocusSuggestion *suggestion, const int skill_position) {
    int raise_indices[ATTRIBUTE_COUNT];
    int point_counts[ATTRIBUTE_COUNT];
    int entry_count = 0;
    int raise_number;

    if (suggestion->use_direct != 0) {
        raise_indices[0] = STAT_RAISE_SKILL_BASE + skill_position;
        point_counts[0] = 1;
        entry_count = 1;
    } else {
        for (raise_number = 0; raise_number < suggestion->attributes.raise_count; raise_number++) {
            raise_indices[entry_count] = suggestion->attributes.raises[raise_number].attribute_id;
            point_counts[entry_count] = suggestion->attributes.raises[raise_number].points;
            entry_count++;
        }
    }
    stat_raise_try_raise_bundle(raise_indices, point_counts, entry_count);
}

static void add_focus_candidate(BuffFocusCandidate *candidates, int *candidate_count, const int raise_index,
                                const int max_points) {
    BuffFocusCandidate *candidate;
    int candidate_index;
    int step_index;

    if (raise_index < 0 || *candidate_count >= BUFF_FOCUS_MAX_CANDIDATES) return;
    for (candidate_index = 0; candidate_index < *candidate_count; candidate_index++) {
        if (candidates[candidate_index].raise_index == raise_index) return;
    }
    candidate = &candidates[(*candidate_count)++];
    candidate->raise_index = raise_index;
    candidate->max_points = max_points;
    for (step_index = 0; step_index < BUFF_FOCUS_MAX_POINTS; step_index++) {
        candidate->step_costs[step_index] = step_index < max_points
                                                    ? to_focus_cost(stat_raise_cost_after(raise_index, step_index))
                                                    : SKILL_FOCUS_NO_COST;
    }
}

static void add_skill_attribute_candidates(BuffFocusCandidate *candidates, int *candidate_count,
                                           const BuffModelSkill *skill) {
    int attribute_slot;

    for (attribute_slot = 0; attribute_slot < ATTRIBUTES_PER_SKILL; attribute_slot++) {
        add_focus_candidate(candidates, candidate_count, skill->attribute_ids[attribute_slot], SKILL_FOCUS_MAX_STEPS);
    }
}

#define FOOL_POOL_MAX_POINTS 10

static void fill_attribute_pool_costs(int step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS]) {
    int attribute_id;
    int step_index;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        for (step_index = 0; step_index < ATTRIBUTE_POOL_MAX_POINTS; step_index++) {
            step_costs[attribute_id][step_index] = to_focus_cost(stat_raise_cost_after(attribute_id, step_index));
        }
    }
}

/* The pool's nth point costs what buying n points cheapest-first costs over buying n - 1. */
static void add_attribute_pool_candidate(BuffFocusCandidate *candidates, int *candidate_count) {
    int step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS];
    BuffFocusCandidate *candidate;
    int previous_total = 0;
    int point_count;
    int candidate_index;

    if (*candidate_count >= BUFF_FOCUS_MAX_CANDIDATES) return;
    for (candidate_index = 0; candidate_index < *candidate_count; candidate_index++) {
        if (candidates[candidate_index].raise_index == BUFF_FOCUS_ATTRIBUTE_POOL) return;
    }
    fill_attribute_pool_costs(step_costs);
    candidate = &candidates[(*candidate_count)++];
    candidate->raise_index = BUFF_FOCUS_ATTRIBUTE_POOL;
    candidate->max_points = FOOL_POOL_MAX_POINTS;
    for (point_count = 1; point_count <= BUFF_FOCUS_MAX_POINTS; point_count++) {
        const AttributePoolPick pick = attribute_pool_cheapest(step_costs, point_count);

        if (point_count > FOOL_POOL_MAX_POINTS || pick.available == 0) {
            candidate->step_costs[point_count - 1] = SKILL_FOCUS_NO_COST;
            continue;
        }
        candidate->step_costs[point_count - 1] = pick.total_cost - previous_total;
        previous_total = pick.total_cost;
    }
}

static BuffFocusRoute expand_attribute_pool(const BuffFocusRoute *route) {
    int step_costs[ATTRIBUTE_COUNT][ATTRIBUTE_POOL_MAX_POINTS];
    BuffFocusRoute expanded;
    int raise_number;

    memset(&expanded, 0, sizeof(expanded));
    expanded.available = route->available;
    expanded.total_cost = route->total_cost;
    for (raise_number = 0; raise_number < route->raise_count; raise_number++) {
        const BuffFocusRaise *raise = &route->raises[raise_number];
        AttributePoolPick pick;
        int attribute_id;

        if (raise->raise_index != BUFF_FOCUS_ATTRIBUTE_POOL) {
            expanded.raises[expanded.raise_count++] = *raise;
            continue;
        }
        fill_attribute_pool_costs(step_costs);
        pick = attribute_pool_cheapest(step_costs, raise->points);
        for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
            int step_index;
            BuffFocusRaise *attribute_raise;

            if (pick.points[attribute_id] == 0 || expanded.raise_count >= BUFF_FOCUS_MAX_CANDIDATES) continue;
            attribute_raise = &expanded.raises[expanded.raise_count++];
            attribute_raise->raise_index = attribute_id;
            attribute_raise->points = pick.points[attribute_id];
            attribute_raise->cost = 0;
            for (step_index = 0; step_index < pick.points[attribute_id]; step_index++) {
                attribute_raise->cost += step_costs[attribute_id][step_index];
            }
        }
    }
    return expanded;
}

static bool spell_shown(const int kind) {
    return spell_known(kind) && buff_state.planned_input.show_buffed[kind] != 0;
}

/* The tree's extra percents add attributes the skill doesn't list, chosen by the skill's group. */
static void add_extra_percent_candidates(BuffFocusCandidate *candidates, int *candidate_count,
                                         const BuffModelSkill *skill, const PlannerEffects *effects) {
    static const int GROUP_OF_ATTRIBUTE[ATTRIBUTE_COUNT] = {
        SKILL_GROUP_PASSIVE,      SKILL_GROUP_ACTIVE_SPELL, SKILL_GROUP_ACTIVE_SPELL,
        SKILL_GROUP_ACTIVE_MELEE, SKILL_GROUP_ACTIVE_MELEE,
    };
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        if (effects->skill_extra_percent[attribute_id] == 0) continue;
        if ((skill->skill_groups & GROUP_OF_ATTRIBUTE[attribute_id]) == 0) continue;
        add_focus_candidate(candidates, candidate_count, attribute_id, SKILL_FOCUS_MAX_STEPS);
    }
}

static int spell_raise_index_for(const int kind) {
    return spell_known(kind) ? STAT_RAISE_SKILL_BASE + spell_skill_position(kind) : -1;
}

static void add_aptitude_candidates(BuffFocusCandidate *candidates, int *candidate_count) {
    add_focus_candidate(candidates, candidate_count, ATTRIBUTE_WILLPOWER, SKILL_FOCUS_MAX_STEPS);
    add_focus_candidate(candidates, candidate_count, ATTRIBUTE_INTUITION, SKILL_FOCUS_MAX_STEPS);
}

static void add_spell_candidates(BuffFocusCandidate *candidates, int *candidate_count, const int kind,
                                 const bool attribute_pool) {
    if (!spell_shown(kind)) return;
    add_focus_candidate(candidates, candidate_count, spell_raise_index_for(kind), BUFF_FOCUS_MAX_POINTS);
    if (attribute_pool) return;
    add_skill_attribute_candidates(candidates, candidate_count, &buff_state.planned_input.spell_skills[kind]);
    add_aptitude_candidates(candidates, candidate_count);
}

static bool item_value_takes_spell_bonus(const CombatStat stat) {
    const PlannerEffects *effects = &buff_state.planned_input.effects;

    if (stat == COMBAT_STAT_WEAPON) return effects->spell_bonus_to_weapon != 0;
    if (stat == COMBAT_STAT_ARMOR) return effects->spell_bonus_to_armor != 0;
    return false;
}

/* Determination and Evergreen add attributes to WV and AV; Brilliance adds them to the Spell Bonus behind Enhance,
 * Protect, Tachi and Odachi. */
static bool attribute_sum_feeds_item_value(const CombatStat stat) {
    const PlannerEffects *effects = &buff_state.planned_input.effects;
    const int item_buff = stat == COMBAT_STAT_WEAPON ? BUFF_KIND_ENHANCE : BUFF_KIND_PROTECT;

    if (stat != COMBAT_STAT_WEAPON && stat != COMBAT_STAT_ARMOR) return false;
    if (effects->determination != 0 || effects->determination_nodes != 0 || effects->evergreen != 0) return true;
    if (effects->brilliance == 0 && effects->brilliance_nodes == 0) return false;
    return spell_shown(item_buff) || item_value_takes_spell_bonus(stat);
}

static bool spell_bonus_feeds_combat_stat(const CombatStat stat) {
    switch (stat) {
        case COMBAT_STAT_HIT:
        case COMBAT_STAT_PARRY: return spell_shown(BUFF_KIND_BLESS);
        case COMBAT_STAT_WEAPON: return spell_shown(BUFF_KIND_ENHANCE) || item_value_takes_spell_bonus(stat);
        case COMBAT_STAT_ARMOR: return spell_shown(BUFF_KIND_PROTECT) || item_value_takes_spell_bonus(stat);
        default: return false;
    }
}

static void add_pool_candidates(BuffFocusCandidate *candidates, int *candidate_count) {
    const PlannerEffects *effects = &buff_state.planned_input.effects;

    if (effects->hitpoints_to_spell_bonus != 0) {
        add_focus_candidate(candidates, candidate_count, BUFF_FOCUS_HITPOINTS_RAISE, BUFF_FOCUS_MAX_POINTS);
    }
    if (effects->mana_to_spell_bonus != 0) {
        add_focus_candidate(candidates, candidate_count, BUFF_FOCUS_MANA_RAISE, BUFF_FOCUS_MAX_POINTS);
    }
}

static void add_all_attribute_candidates(BuffFocusCandidate *candidates, int *candidate_count) {
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        add_focus_candidate(candidates, candidate_count, attribute_id, SKILL_FOCUS_MAX_STEPS);
    }
}

static void add_combat_stat_candidates(BuffFocusCandidate *candidates, int *candidate_count,
                                       const CombatModelInput *combat,
                                       const BuffModelSkill combat_skills[COMBAT_SKILL_COUNT],
                                       const int combat_raise_index[COMBAT_SKILL_COUNT], const CombatStat stat) {
    const bool attribute_pool = tarot_set_has(buff_state.planned_input.tarot_cards, TAROT_FOOL_REVERSED) != 0;
    int affects[COMBAT_SKILL_COUNT];
    int skill_id;

    combat_model_skills_affecting(combat, stat, BUFF_FOCUS_MAX_POINTS, affects);
    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
        if (affects[skill_id] != 0) {
            add_focus_candidate(candidates, candidate_count, combat_raise_index[skill_id], BUFF_FOCUS_MAX_POINTS);
        }
    }
    if (attribute_pool) add_attribute_pool_candidate(candidates, candidate_count);
    /* Spells and pools go before the skills' attributes so the candidate cap drops attributes first. */
    add_spell_candidates(candidates, candidate_count, BUFF_KIND_BLESS, attribute_pool);
    if (stat == COMBAT_STAT_WEAPON) {
        add_spell_candidates(candidates, candidate_count, BUFF_KIND_ENHANCE, attribute_pool);
    }
    if (stat == COMBAT_STAT_ARMOR) {
        add_spell_candidates(candidates, candidate_count, BUFF_KIND_PROTECT, attribute_pool);
    }
    if (spell_bonus_feeds_combat_stat(stat)) add_pool_candidates(candidates, candidate_count);
    if (attribute_pool) return;
    for (skill_id = 0; skill_id < COMBAT_SKILL_COUNT; skill_id++) {
        if (affects[skill_id] != 0) {
            add_skill_attribute_candidates(candidates, candidate_count, &combat_skills[skill_id]);
        }
    }
    if (attribute_sum_feeds_item_value(stat)) add_all_attribute_candidates(candidates, candidate_count);
    /* Introspection turns aptitude into the Spell Bonus that Tachi and Odachi add. */
    if (buff_state.planned_input.effects.introspection != 0 && item_value_takes_spell_bonus(stat)) {
        add_aptitude_candidates(candidates, candidate_count);
    }
}

static bool armor_feeds_skill(const int skill_position) {
    const PlannerEffects *effects = &buff_state.planned_input.effects;
    const int skill_number = skilltab[skill_position].nr;

    if (skill_number == SK_RESIST) return effects->armor_to_resist != 0;
    if (skill_number == SK_IMMUN) return effects->armor_to_immunity != 0;
    return false;
}

static void fill_armor_feed(BuffFocusInput *input, const int skill_position) {
    if (!armor_feeds_skill(skill_position)) return;
    if (!build_combat_model_input(&input->combat)) {
        memset(&input->combat, 0, sizeof(input->combat));
        return;
    }
    input->armor_feeds_focused = 1;
    combat_skills_for(input->combat_skills, true);
    combat_raise_indices_for(&input->combat, input->combat_raise_index);
}

static void build_buff_focus_input(BuffFocusInput *input, const int skill_position) {
    int kind;

    memset(input, 0, sizeof(*input));
    input->model = buff_state.planned_input;
    input->focused = model_skill_for(skill_position, stat_raised[STAT_RAISE_SKILL_BASE + skill_position]);
    input->focused_raise_index = STAT_RAISE_SKILL_BASE + skill_position;
    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) input->spell_raise_index[kind] = spell_raise_index_for(kind);
    fill_armor_feed(input, skill_position);

    BuffFocusCandidate *candidates = input->candidates;
    int *candidate_count = &input->candidate_count;
    add_focus_candidate(candidates, candidate_count, input->focused_raise_index, 1);
    if (tarot_set_has(input->model.tarot_cards, TAROT_FOOL_REVERSED) != 0) {
        add_attribute_pool_candidate(candidates, candidate_count);
    } else {
        add_skill_attribute_candidates(candidates, candidate_count, &input->focused);
        add_extra_percent_candidates(candidates, candidate_count, &input->focused, &input->model.effects);
        if (spell_shown(BUFF_KIND_BLESS)) {
            add_skill_attribute_candidates(candidates, candidate_count, &input->model.spell_skills[BUFF_KIND_BLESS]);
            add_aptitude_candidates(candidates, candidate_count);
        }
    }
    if (spell_shown(BUFF_KIND_BLESS)) {
        add_focus_candidate(candidates, candidate_count, input->spell_raise_index[BUFF_KIND_BLESS],
                            BUFF_FOCUS_MAX_POINTS);
    }
    if (input->armor_feeds_focused != 0) {
        add_combat_stat_candidates(candidates, candidate_count, &input->combat, input->combat_skills,
                                   input->combat_raise_index, COMBAT_STAT_ARMOR);
    }
    if (spell_shown(BUFF_KIND_BLESS)) add_pool_candidates(candidates, candidate_count);
}

/* The search is costly, so it only runs again when something it reads has changed. */
static const BuffFocusResult *cached_buff_focus(const int skill_position) {
    static BuffFocusInput cached_input;
    static BuffFocusResult cached_result;
    static bool has_cached = false;
    BuffFocusInput input;

    build_buff_focus_input(&input, skill_position);
    /* The input is memset before it is filled, so its padding is always zero and a byte compare is exact. */
    if (!has_cached || memcmp(&input, &cached_input, sizeof(input)) != 0) {
        cached_input = input;
        cached_result = buff_focus_search(&input);
        cached_result.best = expand_attribute_pool(&cached_result.best);
        cached_result.direct = expand_attribute_pool(&cached_result.direct);
        cached_result.through_attributes = expand_attribute_pool(&cached_result.through_attributes);
        cached_result.through_bless = expand_attribute_pool(&cached_result.through_bless);
        has_cached = true;
    }
    return &cached_result;
}

static bool build_combat_focus_input(CombatFocusInput *input, const CombatStat stat) {
    int kind;

    memset(input, 0, sizeof(*input));
    if (!build_combat_model_input(&input->combat)) return false;
    input->model = buff_state.planned_input;
    input->stat = stat;
    for (kind = 0; kind < BUFF_KIND_COUNT; kind++) input->spell_raise_index[kind] = spell_raise_index_for(kind);
    combat_skills_for(input->combat_skills, true);
    combat_raise_indices_for(&input->combat, input->combat_raise_index);
    add_combat_stat_candidates(input->candidates, &input->candidate_count, &input->combat, input->combat_skills,
                               input->combat_raise_index, stat);
    return true;
}

/* NULL while the Combat block can only show the server's values. */
static const CombatFocusResult *cached_combat_focus(const CombatStat stat) {
    static CombatFocusInput cached_input;
    static CombatFocusResult cached_result;
    static bool has_cached = false;
    CombatFocusInput input;

    if (!build_combat_focus_input(&input, stat)) return NULL;
    /* The input is memset before it is filled, so its padding is always zero and a byte compare is exact. */
    if (!has_cached || memcmp(&input, &cached_input, sizeof(input)) != 0) {
        cached_input = input;
        cached_result = combat_focus_search(&input);
        cached_result.best = expand_attribute_pool(&cached_result.best);
        has_cached = true;
    }
    return &cached_result;
}

static const char *raise_name(const int raise_index) {
    if (raise_index == STAT_RAISE_HITPOINTS) return "Hitpoints";
    if (raise_index == STAT_RAISE_MANA) return "Mana";
    if (raise_index < ATTRIBUTE_COUNT) return attribute_names[raise_index];
    return skilltab[raise_index - STAT_RAISE_SKILL_BASE].name;
}

static void buff_route_lines_render(const EnhancedSkillLayout *layout, const BuffFocusRoute *route) {
    char label_text[LABEL_TEXT_SIZE];
    char value_text[VALUE_TEXT_SIZE];
    int raise_number;

    for (raise_number = 0; raise_number < route->raise_count; raise_number++) {
        const BuffFocusRaise *raise = &route->raises[raise_number];
        const bool is_skill = raise->raise_index >= STAT_RAISE_SKILL_BASE;

        snprintf(label_text, sizeof(label_text), "+%d %s", raise->points, raise_name(raise->raise_index));
        snprintf(value_text, sizeof(value_text), "%d", raise->cost);
        panel_row_render(layout, layout->raise_line_indent, is_skill ? DIRECT_RAISE_COLOR : ATTRIBUTE_GAIN_COLOR,
                         label_text, value_text);
    }
}

static void plan_buff_route(const BuffFocusRoute *route) {
    int raise_indices[BUFF_FOCUS_MAX_CANDIDATES];
    int point_counts[BUFF_FOCUS_MAX_CANDIDATES];
    int raise_number;

    for (raise_number = 0; raise_number < route->raise_count; raise_number++) {
        raise_indices[raise_number] = route->raises[raise_number].raise_index;
        point_counts[raise_number] = route->raises[raise_number].points;
    }
    stat_raise_try_raise_bundle(raise_indices, point_counts, route->raise_count);
}

typedef enum : unsigned char { ROUTE_DIRECT, ROUTE_ATTRIBUTES, ROUTE_BLESS } RouteKind;

static bool focused_is_bless(void) {
    return focused_skill_position == spell_skill_position(BUFF_KIND_BLESS);
}

static RouteKind route_kind(const BuffFocusRoute *route) {
    const int focused_raise_index = STAT_RAISE_SKILL_BASE + focused_skill_position;
    const int bless_raise_index =
            spell_known(BUFF_KIND_BLESS) ? STAT_RAISE_SKILL_BASE + spell_skill_position(BUFF_KIND_BLESS) : -1;
    bool has_focused = false;
    int raise_number;

    for (raise_number = 0; raise_number < route->raise_count; raise_number++) {
        const int raise_index = route->raises[raise_number].raise_index;
        if (raise_index == focused_raise_index) has_focused = true;
        if (raise_index == bless_raise_index && !focused_is_bless()) return ROUTE_BLESS;
    }
    return has_focused ? ROUTE_DIRECT : ROUTE_ATTRIBUTES;
}

static void other_route_render(const EnhancedSkillLayout *layout, const char *label, const int available,
                               const int total_cost) {
    char label_text[LABEL_TEXT_SIZE];
    char value_text[VALUE_TEXT_SIZE];

    if (available != 0) {
        snprintf(value_text, sizeof(value_text), "%d exp", total_cost);
        panel_row_render(layout, 2, COST_TEXT_COLOR, label, value_text);
    } else {
        snprintf(label_text, sizeof(label_text), "%s not available", label);
        panel_row_render(layout, 2, COST_TEXT_COLOR, label_text, NULL);
    }
}

static bool plan_button_render(const EnhancedSkillLayout *layout, const int total_cost) {
    const float row_left = imgui_get_cursor_pos_x();
    const float row_top = imgui_get_cursor_pos_y();
    const bool affordable = total_cost <= pl.points - stat_points_used;

    imgui_set_cursor_pos(row_left + layout->cost_right - layout->plan_button_width, row_top + 2);
    return ui_button_enabled("Plan it", layout->plan_button_width, BUTTON_SPRITE_HEIGHT, affordable);
}

/* Focus panel body while Bless is shown or model effects apply: the search follows the full buff model. */
static void buff_focus_panel_render(const EnhancedSkillLayout *layout, const char *skill_name) {
    const BuffFocusResult *result = cached_buff_focus(focused_skill_position);
    char label_text[LABEL_TEXT_SIZE];
    char value_text[VALUE_TEXT_SIZE];

    snprintf(label_text, sizeof(label_text), "%s  %d -> %d", skill_name, result->current_value,
             result->current_value + 1);
    panel_row_render(layout, 2, PREVIEW_CHANGED_COLOR, label_text, NULL);

    if (result->best.available == 0) {
        snprintf(label_text, sizeof(label_text), "%s can't be raised further", skill_name);
        panel_row_render(layout, 2, GOLD_FONT_COLOR, label_text, NULL);
        return;
    }

    snprintf(value_text, sizeof(value_text), "%d exp", result->best.total_cost);
    panel_row_render(layout, 2, GOLD_FONT_COLOR, "Cheapest +1", value_text);
    buff_route_lines_render(layout, &result->best);

    /* One line for each kind of route that didn't win. */
    const RouteKind winning_kind = route_kind(&result->best);
    char direct_label[LABEL_TEXT_SIZE];
    snprintf(direct_label, sizeof(direct_label), "Direct (+1 %s)", skill_name);
    if (winning_kind != ROUTE_DIRECT) {
        other_route_render(layout, direct_label, result->direct.available, result->direct.total_cost);
    }
    if (winning_kind != ROUTE_ATTRIBUTES) {
        other_route_render(layout, "Through attributes", result->through_attributes.available,
                           result->through_attributes.total_cost);
    }
    if (winning_kind != ROUTE_BLESS && !focused_is_bless() && spell_shown(BUFF_KIND_BLESS)) {
        other_route_render(layout, "Through Bless", result->through_bless.available, result->through_bless.total_cost);
    }

    if (plan_button_render(layout, result->best.total_cost)) plan_buff_route(&result->best);
}

static void combat_focus_panel_render(const EnhancedSkillLayout *layout) {
    const CombatStat stat = (CombatStat) focused_combat_stat;
    const CombatFocusResult *result = cached_combat_focus(stat);
    const char *stat_name = COMBAT_STAT_NAMES[stat];
    char label_text[LABEL_TEXT_SIZE];
    char value_text[VALUE_TEXT_SIZE];

    if (result == NULL) {
        enhanced_skill_window_clear_focus();
        panel_row_render(layout, 2, GOLD_FONT_COLOR, "Click a skill or combat stat to focus it", NULL);
        return;
    }

    snprintf(label_text, sizeof(label_text), "%s  %d -> %d", stat_name, result->current_value,
             result->current_value + 1);
    panel_row_render(layout, 2, PREVIEW_CHANGED_COLOR, label_text, NULL);

    if (result->best.available == 0) {
        snprintf(label_text, sizeof(label_text), "%s can't be raised further", stat_name);
        panel_row_render(layout, 2, GOLD_FONT_COLOR, label_text, NULL);
        return;
    }

    snprintf(value_text, sizeof(value_text), "%d exp", result->best.total_cost);
    panel_row_render(layout, 2, GOLD_FONT_COLOR, "Cheapest +1", value_text);
    buff_route_lines_render(layout, &result->best);
    if (plan_button_render(layout, result->best.total_cost)) plan_buff_route(&result->best);
}

/* The plain divisor model can't follow percent effects, the highest-attribute boosts or Armor Value in a skill. */
static bool model_effects_present(void) {
    const PlannerEffects *effects = &buff_state.planned_input.effects;
    int attribute_id;

    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        if (effects->attribute_percent[attribute_id] != 0 || effects->skill_extra_percent[attribute_id] != 0) {
            return true;
        }
    }
    if (effects->immunity_percent != 0 || effects->resist_percent != 0 || effects->resist_enchant_percent != 0 ||
        effects->fists_of_heavens != 0 || effects->grande_grimoire != 0) {
        return true;
    }
    return armor_feeds_skill(focused_skill_position);
}

static void focus_panel_render(const EnhancedSkillLayout *layout) {
    const char *skill_name;
    StatRowView focused_row;
    SkillFocusInput input;
    SkillFocusSuggestion suggestion;
    const SkillFocusRoute *winning_route;
    const SkillFocusRoute *other_route;
    char label_text[LABEL_TEXT_SIZE];
    char value_text[VALUE_TEXT_SIZE];
    int raise_number;

    section_label_render(layout, "Focus");

    if (focused_combat_stat >= 0) {
        combat_focus_panel_render(layout);
        return;
    }
    if (focused_skill_position >= 0 && !is_focusable(focused_skill_position)) enhanced_skill_window_clear_focus();
    if (focused_skill_position < 0) {
        panel_row_render(layout, 2, GOLD_FONT_COLOR, "Click a skill or combat stat to focus it", NULL);
        return;
    }

    skill_name = skilltab[focused_skill_position].name;
    if (buff_state.available && (spell_shown(BUFF_KIND_BLESS) || model_effects_present())) {
        buff_focus_panel_render(layout, skill_name);
        return;
    }
    focused_row = build_skill_row(focused_skill_position, game_ui_attribute_divisor());
    input = build_focus_input(focused_skill_position);
    suggestion = skill_focus_suggest(&input);

    const int preview = row_preview(&focused_row);
    snprintf(label_text, sizeof(label_text), "%s  %d -> %d", skill_name, preview, preview + 1);
    panel_row_render(layout, 2, PREVIEW_CHANGED_COLOR, label_text, NULL);

    if (suggestion.any_available == 0) {
        snprintf(label_text, sizeof(label_text), "%s can't be raised further", skill_name);
        panel_row_render(layout, 2, GOLD_FONT_COLOR, label_text, NULL);
        return;
    }

    winning_route = suggestion.use_direct != 0 ? &suggestion.direct : &suggestion.attributes;
    other_route = suggestion.use_direct != 0 ? &suggestion.attributes : &suggestion.direct;

    snprintf(value_text, sizeof(value_text), "%d exp", winning_route->total_cost);
    panel_row_render(layout, 2, GOLD_FONT_COLOR, "Cheapest +1", value_text);
    if (suggestion.use_direct != 0) {
        snprintf(label_text, sizeof(label_text), "+1 %s", skill_name);
        snprintf(value_text, sizeof(value_text), "%d", winning_route->total_cost);
        panel_row_render(layout, layout->raise_line_indent, DIRECT_RAISE_COLOR, label_text, value_text);
    } else {
        for (raise_number = 0; raise_number < winning_route->raise_count; raise_number++) {
            const SkillFocusRaise *raise = &winning_route->raises[raise_number];
            snprintf(label_text, sizeof(label_text), "+%d %s", raise->points, attribute_names[raise->attribute_id]);
            snprintf(value_text, sizeof(value_text), "%d", raise->cost);
            panel_row_render(layout, layout->raise_line_indent, ATTRIBUTE_GAIN_COLOR, label_text, value_text);
        }
    }

    if (suggestion.use_direct != 0) {
        snprintf(label_text, sizeof(label_text), "Through attributes");
    } else {
        snprintf(label_text, sizeof(label_text), "Direct (+1 %s)", skill_name);
    }
    other_route_render(layout, label_text, other_route->available, other_route->total_cost);

    if (plan_button_render(layout, winning_route->total_cost)) plan_suggestion(&suggestion, focused_skill_position);
}

#define VITAL_TOTAL_INDEX 5

static void left_column_render(const EnhancedSkillLayout *layout) {
    StatRowView row_view;
    int attribute_id;

    section_label_render(layout, "Attributes");
    for (attribute_id = 0; attribute_id < ATTRIBUTE_COUNT; attribute_id++) {
        row_view = build_attribute_row(attribute_id);
        stat_row_render(layout, &row_view);
    }

    row_view = build_vital_row("Hitpoints", pl.hp[0], pl.hp[VITAL_TOTAL_INDEX], STAT_RAISE_HITPOINTS);
    stat_row_render(layout, &row_view);
    row_view = build_vital_row("Mana", pl.mana[0], pl.mana[VITAL_TOTAL_INDEX], STAT_RAISE_MANA);
    stat_row_render(layout, &row_view);

    imgui_dummy(layout->column_width, layout->row_height);
    combat_block_render(layout);
    math_block_render(layout);

    imgui_dummy(layout->column_width, layout->row_height);
    focus_panel_render(layout);
}

static void skill_column_render(const EnhancedSkillLayout *layout, const float column_height) {
    const int attribute_divisor = game_ui_attribute_divisor();
    int skill_position;

    const float column_top = imgui_get_cursor_pos_y();

    legend_render(layout);
    imgui_dummy(layout->column_width, layout->row_height);

    section_label_render(layout, "Skills");

    /* The skill list fills whatever height the Key leaves below it. */
    const float list_height = column_height - (imgui_get_cursor_pos_y() - column_top);
    if (imgui_begin_child("Planner Skills", layout->column_width + imgui_get_style_scrollbar_size(), list_height, false,
                          0)) {
        for (skill_position = 0; skill_position < MAXSKILL; skill_position++) {
            if (skilltab[skill_position].show != 1) continue;
            StatRowView row_view = build_skill_row(skill_position, attribute_divisor);
            stat_row_render(layout, &row_view);
        }
    }
    imgui_end_child();
}

#define COMMAND_BUTTON_GAP 4

static void header_render(const EnhancedSkillLayout *layout) {
    const float header_top = imgui_get_cursor_pos_y();
    /* Whole pixels only: a half-pixel offset clips the button sprite's top border. */
    const float button_top = (float) (int) (header_top + ((layout->header_height - BUTTON_SPRITE_HEIGHT) / 2));
    const float content_right = layout->window_size.x - layout->window_padding;
    char header_text[LABEL_TEXT_SIZE];

    snprintf(header_text, sizeof(header_text), "Exp left %d    Planned %d", pl.points - stat_points_used,
             stat_points_used);
    imgui_set_cursor_pos(layout->window_padding + 2,
                         header_top + ((layout->header_height - imgui_get_text_line_height()) / 2));
    imgui_text_colored_32(GOLD_FONT_COLOR, header_text);

    imgui_set_cursor_pos(content_right - (layout->command_button_width * 3) - (COMMAND_BUTTON_GAP * 2), button_top);
    if (ui_button("Commit", layout->command_button_width, BUTTON_SPRITE_HEIGHT)) {
        stat_raise_commit_all();
        enhanced_skill_window_clear_focus();
    }
    imgui_set_cursor_pos(content_right - (layout->command_button_width * 2) - COMMAND_BUTTON_GAP, button_top);
    if (ui_button("Reset", layout->command_button_width, BUTTON_SPRITE_HEIGHT)) stat_raise_reset_all();
    imgui_set_cursor_pos(content_right - layout->command_button_width, button_top);
    if (ui_button("Close", layout->command_button_width, BUTTON_SPRITE_HEIGHT)) {
        game_ui_state.show_enhanced_skills = false;
        game_ui_state.show_planner_advanced = false;
    }

    imgui_set_cursor_pos(layout->window_padding, header_top);
    imgui_dummy(content_right - layout->window_padding, layout->header_height);
}

static void frame_render(const EnhancedSkillLayout *layout) {
    void *draw_list = imgui_get_window_draw_list();
    const float left = imgui_get_window_pos_x();
    const float top = imgui_get_window_pos_y();
    const float right = left + layout->window_size.x;
    const float bottom = top + layout->window_size.y;
    const SpriteData *frame;

    imgui_draw_list_add_rect_filled(draw_list, left, top, right, bottom, layout->background_color, 0, 0);

    sdl_load_sprite(WINDOW_SPRITE_ID);
    frame = &sprite_data[WINDOW_SPRITE_ID];
    imgui_draw_list_add_image_9_slice(draw_list, (void *) (intptr_t) frame->gl_texture, left, top, right, bottom,
                                      layout->frame_border, layout->frame_border, layout->frame_border,
                                      layout->frame_border, (float) frame->pixel_width, (float) frame->pixel_height,
                                      frame->uv0.u, frame->uv0.v, frame->uv1.u, frame->uv1.v, WHITE_TINT);
}

void enhanced_skill_window_render(void) {
    const EnhancedSkillLayout *layout = get_layout();
    const float columns_top = layout->window_padding + layout->header_height;
    const float column_height = layout->window_size.y - columns_top - layout->window_padding;
    const float right_column_left = layout->window_padding + layout->column_width + layout->column_gap;

    drop_focus_if_list_changed();
    load_choices_for_character();
    refresh_buff_state();

    imgui_set_next_window_pos(layout->window_position.x, layout->window_position.y);
    imgui_set_next_windows_size(layout->window_size.x, layout->window_size.y);
    imgui_push_style_var_float(IMGUI_STYLE_VAR_WINDOW_BORDER_SIZE, 0);
    imgui_push_style_var_vec2(IMGUI_STYLE_VAR_WINDOW_PADDING, 0, 0);
    imgui_push_style_var_vec2(IMGUI_STYLE_VAR_ITEM_SPACING, 0, 0);
    imgui_push_style_var_vec2(IMGUI_STYLE_VAR_ITEM_INNER_SPACING, 0, 0);

    const int window_flags = IMGUI_WINDOW_FLAG_NO_TITLE_BAR | IMGUI_WINDOW_FLAG_NO_RESIZE | IMGUI_WINDOW_FLAG_NO_MOVE |
                             IMGUI_WINDOW_FLAG_NO_BACKGROUND | IMGUI_WINDOW_FLAG_NO_SCROLLBAR |
                             IMGUI_WINDOW_FLAG_NO_SCROLL_WITH_MOUSE | IMGUI_WINDOW_FLAG_NO_COLLAPSE;
    if (imgui_begin("Skill Planner", NULL, window_flags)) {
        frame_render(layout);

        imgui_set_cursor_pos(0, layout->window_padding);
        header_render(layout);

        imgui_set_cursor_pos(layout->window_padding, columns_top);
        imgui_begin_group();
        left_column_render(layout);
        imgui_end_group();

        imgui_set_cursor_pos(right_column_left, columns_top);
        imgui_begin_group();
        skill_column_render(layout, column_height);
        imgui_end_group();
    }
    imgui_end();
    imgui_pop_style_var(4);

    const PlannerAdvancedView advanced_view = build_advanced_view();
    if (planner_advanced_window_render(&chosen, &advanced_view) != 0) save_choices();
}
