#include "item_tooltip_ui.hpp"

#include <imgui.h>
#include <iterator>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "inter.h"
#include "ui_common.hpp"
#include "config/config.h"
#include "mods/item_tooltip.h"

extern "C" unsigned char inv_block[];

static const float TOOLTIP_MAX_WIDTH = 280.0F;
static const float TOOLTIP_WINDOW_PADDING[2] = {8, 6};
static const float TOOLTIP_CELL_PADDING[2] = {6, 0};
static const float TOOLTIP_BORDER_SIZE = 1.0F;
static const float TOOLTIP_DELTA_SPACING = 4.0F;
static constexpr unsigned int TOOLTIP_BORDER_COLOR = color_rgb(140, 107, 58);
static constexpr unsigned int TOOLTIP_DIM_COLOR = color_rgb(144, 154, 154);
static constexpr unsigned int TOOLTIP_UPGRADE_COLOR = color_rgb(90, 220, 90);
static constexpr unsigned int TOOLTIP_DOWNGRADE_COLOR = color_rgb(230, 80, 80);

static const int TENTHS_PER_PERCENT = 10;
static const float TENTHS_PER_PERCENT_FLOAT = 10.0F;
static const int SILVER_PER_GOLD = 100;
static const int WORST_CONDITION = 4; /* "Very old and battered" */

#define STAT_TEXT_SIZE 48
#define FOOTER_TEXT_SIZE 96

#define WORN_ITEM_SLOTS 13 /* worn slots from here up carry character stats */

typedef struct {
    const char *label;
    unsigned int flag;
    int font;
} ModBadge;

/* Which optional columns the stat table shows. */
typedef struct {
    bool show_active;
    bool show_required;
} TableColumns;

/* The item a stat table describes and, when comparing, the held item that would replace it. */
typedef struct {
    const ItemDetails *shown;
    const ItemDetails *held;
    TableColumns columns;
} StatTable;

static const ModBadge applied_badges[] = {
    {"Whetstoned", ITEM_MOD_WHETSTONED, CHAT_FONT_ORANGE}, {"Soulstoned", ITEM_MOD_SOULSTONED, CHAT_FONT_BLUE},
    {"Catalyzed", ITEM_MOD_CATALYZED, CHAT_FONT_BLUE},     {"Enchanted", ITEM_MOD_ENCHANTED, CHAT_FONT_VIOLET},
    {"Focused", ITEM_MOD_FOCUSED, CHAT_FONT_VIOLET},       {"Augmented", ITEM_MOD_AUGMENTED, CHAT_FONT_SKYBLUE},
    {"Easier to use", ITEM_MOD_EASIER, CHAT_FONT_LIME},    {"Corrupted", ITEM_MOD_CORRUPTED, CHAT_FONT_PINK},
    {"Legacy", ITEM_MOD_LEGACY, CHAT_FONT_WHITE},
};

static const ModBadge possible_badges[] = {
    {"Whetstone", ITEM_CAN_WHETSTONE, CHAT_FONT_ORANGE},
    {"Soulstone", ITEM_CAN_SOULSTONE, CHAT_FONT_BLUE},
    {"Osiris", ITEM_CAN_OSIRIS, CHAT_FONT_BLUE},
    {"Enchant", ITEM_CAN_ENCHANT, CHAT_FONT_VIOLET},
};

static const ModBadge impossible_badges[] = {
    {"Soulstone", ITEM_CANNOT_SOULSTONE, CHAT_FONT_RED},
    {"Enchant", ITEM_CANNOT_ENCHANT, CHAT_FONT_RED},
};

static const char *condition_labels[] = {
    "Perfect condition", "Showing signs of age", "Fairly old", "Old", "Very old and battered",
};

/* Approximate colours of the chat fonts, so tooltip lines match the chat box. */
static ImU32 chat_font_color(const int font) {
    switch (font) {
        case CHAT_FONT_RED: return IM_COL32(230, 70, 70, 255);
        case CHAT_FONT_YELLOW: return GOLD_FONT_COLOR;
        case CHAT_FONT_GREEN: return IM_COL32(100, 210, 90, 255);
        case CHAT_FONT_BLUE: return IM_COL32(90, 130, 255, 255);
        case CHAT_FONT_PINK: return IM_COL32(240, 130, 200, 255);
        case CHAT_FONT_ORANGE: return IM_COL32(245, 160, 60, 255);
        case CHAT_FONT_LIME: return IM_COL32(170, 240, 80, 255);
        case CHAT_FONT_SKYBLUE: return IM_COL32(73, 188, 226, 255);
        case CHAT_FONT_VIOLET: return IM_COL32(170, 120, 240, 255);
        case CHAT_FONT_WHITE: return IM_COL32(235, 235, 235, 255);
        default: return GOLD_FONT_COLOR;
    }
}

static bool held_item_fits(const int slot) {
    return pl.citem != 0 && inv_block[slot] == 0;
}

/* With an item on the cursor, empty slots are reported too when the item fits there. */
static void report_equipment_hover() {
    if (hightlight != HL_EQUIPMENT || hightlight_sub < 0 || hightlight_sub >= WORN_ITEM_SLOTS) return;
    if (pl.worn[hightlight_sub] != 0 || held_item_fits(hightlight_sub)) {
        item_tooltip_hover(ITEM_TOOLTIP_WORN, hightlight_sub);
    }
}

static void text_colored(const ImU32 color, const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

static void text_wrapped_colored(const ImU32 color, const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

static void format_percent(char *output, const size_t size, const int tenths) {
    if (tenths % TENTHS_PER_PERCENT == 0) {
        snprintf(output, size, "%+d%%", tenths / TENTHS_PER_PERCENT);
    } else {
        snprintf(output, size, "%+.1f%%", (double) ((float) tenths / TENTHS_PER_PERCENT_FLOAT));
    }
}

static void format_value(char *output, const size_t size, const ItemStat *stat, const int value) {
    if (stat->kind == ITEM_STAT_PERCENT) {
        format_percent(output, size, value);
    } else {
        snprintf(output, size, "%+d", value);
    }
}

/* Stats that do not fit the passive/active columns get a single text value. */
static bool format_special(char *output, const size_t size, const ItemStat *stat) {
    switch (stat->kind) {
        case ITEM_STAT_REGEN: snprintf(output, size, "%+d (%+d/s)", stat->passive, stat->active); return true;
        case ITEM_STAT_DURATION: snprintf(output, size, "%d seconds", stat->passive); return true;
        case ITEM_STAT_SINGLE: snprintf(output, size, "%d", stat->passive); return true;
        case ITEM_STAT_RANK: snprintf(output, size, "%d (%s)", stat->passive, stat->rank_name); return true;
        default: return false;
    }
}

static void draw_badges(const char *label, const unsigned int flags, const ModBadge *badges, const int count) {
    bool shown = false;

    for (int badge = 0; badge < count; badge++) {
        if ((flags & badges[badge].flag) == 0) continue;
        if (!shown) {
            text_colored(TOOLTIP_DIM_COLOR, label);
            shown = true;
        }
        ImGui::SameLine();
        text_colored(chat_font_color(badges[badge].font), badges[badge].label);
    }
}

/*
 * Whether a smaller number is the improvement for this stat, so its
 * comparison colours flip. `name` is the stat name as the table prints it,
 * already cut to 16 characters.
 */
static bool stat_lower_is_better(const char *name) {
    // TODO: return true for stats where a decrease is an upgrade.
    (void) name;
    return false;
}

static const ItemStat *find_stat(const ItemDetails *details, const char *name) {
    for (int stat_index = 0; stat_index < details->stat_count; stat_index++) {
        if (strcmp(details->stats[stat_index].name, name) == 0) return &details->stats[stat_index];
    }
    return nullptr;
}

/* Stats whose numbers can be meaningfully subtracted. */
static bool stat_comparable(const ItemStat *stat) {
    return stat->kind == ITEM_STAT_PLAIN || stat->kind == ITEM_STAT_PERCENT || stat->kind == ITEM_STAT_SINGLE ||
           stat->kind == ITEM_STAT_REGEN;
}

/* Complexity and Min Rank read better as lines under the table than as a column of mostly empty cells. */
static bool stat_in_table(const ItemStat *stat) {
    return stat->kind != ITEM_STAT_SINGLE && stat->kind != ITEM_STAT_RANK;
}

static int table_stat_count(const ItemDetails *details) {
    int count = 0;

    for (int stat_index = 0; stat_index < details->stat_count; stat_index++) {
        if (stat_in_table(&details->stats[stat_index])) count++;
    }
    return count;
}

static bool stat_has_active(const ItemStat *stat) {
    return stat->kind == ITEM_STAT_PLAIN || stat->kind == ITEM_STAT_PERCENT;
}

static bool stat_shows_active(const ItemStat *stat) {
    return stat_has_active(stat) && stat->active != stat->passive;
}

static bool stat_shows_required(const ItemStat *stat) {
    return stat->has_required != 0 && stat->required != 0;
}

static void draw_delta(const ItemStat *stat, const int delta) {
    char delta_text[STAT_TEXT_SIZE];

    if (delta == 0) return;
    format_value(delta_text, sizeof(delta_text), stat, delta);
    const bool better = stat_lower_is_better(stat->name) ? delta < 0 : delta > 0;
    ImGui::SameLine(0, TOOLTIP_DELTA_SPACING);
    text_colored(better ? TOOLTIP_UPGRADE_COLOR : TOOLTIP_DOWNGRADE_COLOR, delta_text);
}

static TableColumns table_columns(const ItemDetails *shown, const ItemDetails *held) {
    TableColumns columns = {false, false};

    for (int stat_index = 0; stat_index < shown->stat_count; stat_index++) {
        const ItemStat *stat = &shown->stats[stat_index];
        columns.show_active = columns.show_active || stat_shows_active(stat);
        columns.show_required = columns.show_required || stat_shows_required(stat);
    }
    if (held != nullptr) {
        for (int stat_index = 0; stat_index < held->stat_count; stat_index++) {
            columns.show_active = columns.show_active || stat_shows_active(&held->stats[stat_index]);
        }
    }
    return columns;
}

static int table_column_count(const StatTable *table) {
    if (table->shown->soulstone_table != 0) return 2;
    return 2 + (table->columns.show_active ? 1 : 0) + (table->columns.show_required ? 1 : 0);
}

static const char *value_column_label(const StatTable *table) {
    if (table->shown->soulstone_table != 0) return "Adds";
    if (table->columns.show_active) return "Passive";
    return "Value";
}

static void draw_table_header(const StatTable *table) {
    const bool plain_table = table->shown->soulstone_table == 0;

    ImGui::PushStyleColor(ImGuiCol_Text, chat_font_color(CHAT_FONT_ORANGE));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Stat");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value_column_label(table));
    if (plain_table && table->columns.show_active) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Active");
    }
    if (plain_table && table->columns.show_required) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Req");
    }
    ImGui::PopStyleColor();
}

/* `held_stat` is the same stat on the held item, or NULL if it has none; `compare` adds the deltas. */
static void draw_active_cell(const ItemStat *stat, const ItemStat *held_stat, const bool compare) {
    char value_text[STAT_TEXT_SIZE];

    ImGui::TableNextColumn();
    if (!stat_has_active(stat)) return;

    format_value(value_text, sizeof(value_text), stat, stat->active);
    text_colored(chat_font_color(stat->active < 0 ? CHAT_FONT_RED : CHAT_FONT_SKYBLUE), value_text);
    if (compare) {
        const int held_active = held_stat != nullptr && stat_has_active(held_stat) ? held_stat->active : 0;
        draw_delta(stat, held_active - stat->active);
    }
}

static void draw_required_cell(const ItemStat *stat) {
    char value_text[STAT_TEXT_SIZE];

    ImGui::TableNextColumn();
    if (!stat_shows_required(stat)) return;

    snprintf(value_text, sizeof(value_text), "%d", stat->required);
    text_colored(TOOLTIP_DIM_COLOR, value_text);
}

/* Each value is followed by what swapping the held item in would change: held minus shown. */
static void draw_stat_row(const StatTable *table, const ItemStat *stat) {
    char value_text[STAT_TEXT_SIZE];
    const bool compare = table->held != nullptr && stat_comparable(stat);
    const ItemStat *held_stat = compare ? find_stat(table->held, stat->name) : nullptr;

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    text_colored(chat_font_color(CHAT_FONT_YELLOW), stat->name);

    ImGui::TableNextColumn();
    if (!format_special(value_text, sizeof(value_text), stat)) {
        format_value(value_text, sizeof(value_text), stat, stat->passive);
    }
    text_colored(chat_font_color(stat->passive < 0 ? CHAT_FONT_RED : CHAT_FONT_WHITE), value_text);
    if (compare) draw_delta(stat, (held_stat != nullptr ? held_stat->passive : 0) - stat->passive);

    if (table->shown->soulstone_table != 0) return;
    if (table->columns.show_active) draw_active_cell(stat, held_stat, compare);
    if (table->columns.show_required) draw_required_cell(stat);
}

/* Rows for stats only the held item has: equipping it would add them outright. */
static void draw_gained_stats(const StatTable *table) {
    for (int stat_index = 0; stat_index < table->held->stat_count; stat_index++) {
        const ItemStat *gain = &table->held->stats[stat_index];
        if (!stat_in_table(gain) || !stat_comparable(gain) || find_stat(table->shown, gain->name) != nullptr) continue;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        text_colored(TOOLTIP_DIM_COLOR, gain->name);

        ImGui::TableNextColumn();
        text_colored(TOOLTIP_DIM_COLOR, "-");
        draw_delta(gain, gain->passive);

        if (table->columns.show_active) {
            ImGui::TableNextColumn();
            if (stat_has_active(gain)) {
                text_colored(TOOLTIP_DIM_COLOR, "-");
                draw_delta(gain, gain->active);
            }
        }
        if (table->columns.show_required) ImGui::TableNextColumn();
    }
}

/* `held` is the item that would replace `shown`, or NULL when not comparing. */
static void draw_stat_table(const ItemDetails *shown, const ItemDetails *held) {
    if (held != nullptr && (shown->soulstone_table != 0 || held->soulstone_table != 0)) held = nullptr;

    StatTable table = {shown, held, table_columns(shown, held)};
    if (table_stat_count(shown) == 0 && (held == nullptr || table_stat_count(held) == 0)) return;

    /* The tooltip wrap edge is a fixed window x, so a wide cell would squeeze every column after it to nothing. */
    ImGui::PushTextWrapPos(-1.0F);
    if (ImGui::BeginTable("##item_stats", table_column_count(&table), ImGuiTableFlags_SizingFixedFit)) {
        draw_table_header(&table);
        for (int stat_index = 0; stat_index < shown->stat_count; stat_index++) {
            if (stat_in_table(&shown->stats[stat_index])) draw_stat_row(&table, &shown->stats[stat_index]);
        }
        if (held != nullptr) draw_gained_stats(&table);
        ImGui::EndTable();
    }
    ImGui::PopTextWrapPos();
}

static void draw_stat_line(const ItemStat *stat, const ItemStat *held_stat, const bool compare) {
    char value_text[STAT_TEXT_SIZE];
    char line_text[STAT_TEXT_SIZE * 2];

    format_special(value_text, sizeof(value_text), stat);
    snprintf(line_text, sizeof(line_text), "%s: %s", stat->name, value_text);
    text_colored(chat_font_color(CHAT_FONT_YELLOW), line_text);
    if (compare && stat_comparable(stat)) {
        draw_delta(stat, (held_stat != nullptr ? held_stat->passive : 0) - stat->passive);
    }
}

/* `held` is the item that would replace `shown`, or NULL when not comparing. */
static void draw_stat_lines(const ItemDetails *shown, const ItemDetails *held) {
    char gain_text[STAT_TEXT_SIZE];

    if (held != nullptr && (shown->soulstone_table != 0 || held->soulstone_table != 0)) held = nullptr;

    for (int stat_index = 0; stat_index < shown->stat_count; stat_index++) {
        const ItemStat *stat = &shown->stats[stat_index];
        if (stat_in_table(stat)) continue;
        draw_stat_line(stat, held != nullptr ? find_stat(held, stat->name) : nullptr, held != nullptr);
    }
    if (held == nullptr) return;

    for (int stat_index = 0; stat_index < held->stat_count; stat_index++) {
        const ItemStat *gain = &held->stats[stat_index];
        if (stat_in_table(gain) || find_stat(shown, gain->name) != nullptr) continue;

        snprintf(gain_text, sizeof(gain_text), "%s: -", gain->name);
        text_colored(TOOLTIP_DIM_COLOR, gain_text);
        if (stat_comparable(gain)) draw_delta(gain, gain->passive);
    }
}

static void draw_line_refs(const TooltipBlock *block, const unsigned char *refs, const int count) {
    for (int ref_index = 0; ref_index < count; ref_index++) {
        const TooltipLine *line = &block->lines[refs[ref_index]];
        text_wrapped_colored(chat_font_color(line->font), line->text);
    }
}

static bool has_footer(const ItemDetails *details) {
    return details->level >= 0 || details->blessings >= 0 || details->condition >= 0 || details->sell_silver >= 0;
}

static void draw_footer(const ItemDetails *details) {
    char footer_text[FOOTER_TEXT_SIZE];

    if (details->level >= 0) {
        snprintf(footer_text, sizeof(footer_text), "Level %d - %d kills to next", details->level,
                 details->kills_to_level);
        text_colored(chat_font_color(CHAT_FONT_GREEN), footer_text);
    }
    if (details->blessings >= 0) {
        snprintf(footer_text, sizeof(footer_text), "%d blessing%s left - next in %d kills", details->blessings,
                 details->blessings == 1 ? "" : "s", details->kills_to_blessing);
        text_colored(chat_font_color(CHAT_FONT_GREEN), footer_text);
    }
    if (details->condition >= 0 && details->condition < std::size(condition_labels)) {
        const int font = details->condition >= WORST_CONDITION ? CHAT_FONT_RED : CHAT_FONT_YELLOW;
        text_colored(chat_font_color(font), condition_labels[details->condition]);
    }
    if (details->sell_silver >= 0) {
        snprintf(footer_text, sizeof(footer_text), "Sells for about %dG %dS", details->sell_silver / SILVER_PER_GOLD,
                 details->sell_silver % SILVER_PER_GOLD);
        text_colored(chat_font_color(CHAT_FONT_LIME), footer_text);
    }
}

static void draw_tooltip_body(const TooltipBlock *block, const ItemDetails *details, const ItemDetails *held) {
    text_colored(chat_font_color(block->lines[0].font), details->name);

    if (details->description_line >= 0 && block->lines[details->description_line].text[0] != 0) {
        const TooltipLine *line = &block->lines[details->description_line];
        text_wrapped_colored(chat_font_color(line->font), line->text);
    }

    if (details->effect_count > 0) {
        ImGui::Separator();
        draw_line_refs(block, details->effect_lines, details->effect_count);
    }

    if (details->stat_count > 0 || (held != nullptr && held->stat_count > 0)) {
        ImGui::Separator();
        draw_stat_table(details, held);
        draw_stat_lines(details, held);
    }

    if (details->mods_applied != 0 || details->mods_possible != 0) {
        ImGui::Separator();
        draw_badges("Has:", details->mods_applied, applied_badges, std::size(applied_badges));
        draw_badges("Can:", details->mods_possible, possible_badges, std::size(possible_badges));
        draw_badges("Cannot:", details->mods_possible, impossible_badges, std::size(impossible_badges));
    }

    if (has_footer(details)) {
        ImGui::Separator();
        draw_footer(details);
    }

    if (details->other_count > 0) {
        ImGui::Separator();
        draw_line_refs(block, details->other_lines, details->other_count);
    }
}

static bool begin_item_tooltip() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(TOOLTIP_WINDOW_PADDING[0], TOOLTIP_WINDOW_PADDING[1]));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, TOOLTIP_BORDER_SIZE);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(TOOLTIP_CELL_PADDING[0], TOOLTIP_CELL_PADDING[1]));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, BACKGROUND_COLOR_DARK_MODE);
    ImGui::PushStyleColor(ImGuiCol_Border, TOOLTIP_BORDER_COLOR);

    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(TOOLTIP_MAX_WIDTH);
        return true;
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return false;
}

static void end_item_tooltip() {
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

static bool has_text(const TooltipBlock *block, const ItemDetails *details) {
    return block != nullptr && details != nullptr && block->count > 0;
}

/* Nothing to replace: everything the held item has is a gain. */
static void draw_empty_slot_comparison(const ItemDetails *held) {
    static const ItemDetails empty_slot = {};

    if (held == nullptr || !begin_item_tooltip()) return;
    text_colored(TOOLTIP_DIM_COLOR, "Empty slot");
    if (held->stat_count > 0) {
        ImGui::Separator();
        draw_stat_table(&empty_slot, held);
        draw_stat_lines(&empty_slot, held);
    }
    end_item_tooltip();
}

/*
 * Hovering an equipment slot that accepts the held item compares the two.
 * Returns true if the hover belongs to the comparison, even when nothing could be drawn.
 */
static bool draw_held_comparison() {
    ItemTooltipKind kind = ITEM_TOOLTIP_INVENTORY;
    int slot = -1;

    if (pl.citem == 0 || item_tooltip_hovered_slot(&kind, &slot) == 0) return false;
    if (kind != ITEM_TOOLTIP_WORN || slot < 0 || slot >= WORN_ITEM_SLOTS || !held_item_fits(slot)) return false;

    const ItemDetails *held = item_tooltip_cursor_details();
    if (pl.worn[slot] == 0) {
        draw_empty_slot_comparison(held);
        return true;
    }

    const TooltipBlock *block = item_tooltip_hovered_block();
    const ItemDetails *details = item_tooltip_hovered_details();
    if (!has_text(block, details) || !begin_item_tooltip()) return true;

    text_colored(TOOLTIP_DIM_COLOR, "Equipped");
    draw_tooltip_body(block, details, held);
    if (held == nullptr) text_colored(TOOLTIP_DIM_COLOR, "No details for the held item.");
    end_item_tooltip();
    return true;
}

static bool tooltips_visible() {
    if (!g_config.ui.item_tooltips) return false;
    if (g_config.ui.item_tooltips_always) return true;

    const BindingDescriptor *binding = binding_find_by_id(ITEM_TOOLTIP_HOTKEY);
    return binding != nullptr && keybinding_is_held(binding->keybinding);
}

void item_tooltip_ui_render(void) {
    report_equipment_hover();

    if (!tooltips_visible()) {
        item_tooltip_frame_end();
        return;
    }

    if (!draw_held_comparison()) {
        const TooltipBlock *block = item_tooltip_hovered_block();
        const ItemDetails *details = item_tooltip_hovered_details();
        if (has_text(block, details) && begin_item_tooltip()) {
            draw_tooltip_body(block, details, nullptr);
            end_item_tooltip();
        }
    }

    item_tooltip_frame_end();
}
