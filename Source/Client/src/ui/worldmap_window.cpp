#include "worldmap_window.hpp"

#include <imgui.h>
#include <math.h>
#include <stdio.h>

#include "engine.h"
#include "game/annotations.h"
#include "game/game_ui.h"
#include "game/minimap.h"
#include "game/worldmap.h"
#include "game/worldmap_view.h"
#include "graphics/sdl.h"

enum WorldmapTool {
    WORLDMAP_TOOL_SELECT,
    WORLDMAP_TOOL_PAN,
    WORLDMAP_TOOL_PIN,
    WORLDMAP_TOOL_LINE,
    WORLDMAP_TOOL_TEXT,
    WORLDMAP_TOOL_COUNT
};

struct WorldmapPanelState {
    WorldmapView view;
    int current_category_id;
    int selected_annotation_id;
    int expanded_category_ids[ANNOTATION_MAX_CATEGORIES];
    int expanded_count;
    WorldmapTool active_tool;
    bool hovered_tile_valid;
    int hovered_tile_x;
    int hovered_tile_y;
};

struct MapArea {
    ImVec2 origin;
    ImVec2 size;
};

static const float CATEGORY_COLUMN_WIDTH = 220.0F;
static const double CENTRE_ON_PLAYER_PIXELS_PER_TILE = 8.0;
static const double ZOOM_STEP = 1.25;
static const float PIN_RADIUS = 5.0F;
static const double PIN_HOVER_RADIUS = 8.0;
static const float PATH_THICKNESS = 2.0F;
static const float SWATCH_SIZE = 12.0F;
static const ImU32 OUTLINE_COLOUR = IM_COL32(0, 0, 0, 255);
static const ImU32 BACKGROUND_COLOUR = IM_COL32(0, 0, 0, 255);
static const ImU32 HIGHLIGHT_COLOUR = IM_COL32(255, 255, 255, 255);
static const char *const TOOL_NAMES[WORLDMAP_TOOL_COUNT] = {"Select", "Pan", "Pin", "Line", "Text"};

static WorldmapPanelState panel_state = {
    {WORLDMAP_TILES_DOWN / 2.0, WORLDMAP_TILES_ACROSS / 2.0, 0.0}, 0, 0, {0}, 0, WORLDMAP_TOOL_SELECT, false, 0, 0};

static ImU32 category_colour(const AnnotationCategory *category) {
    const unsigned int rgb = annotation_palette_rgb(category->colour_index);
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

static ImVec2 tile_to_screen(const WorldmapPanelState *panel, const MapArea *area, double tile_x, double tile_y) {
    double panel_x, panel_y;
    worldmap_view_tile_to_panel(&panel->view, area->size.x, area->size.y, tile_x, tile_y, &panel_x, &panel_y);
    return ImVec2(area->origin.x + (float) panel_x, area->origin.y + (float) panel_y);
}

static bool category_is_expanded(const WorldmapPanelState *panel, int category_id) {
    for (int expanded_index = 0; expanded_index < panel->expanded_count; expanded_index++) {
        if (panel->expanded_category_ids[expanded_index] == category_id) return true;
    }
    return false;
}

static void set_category_expanded(WorldmapPanelState *panel, int category_id, bool expanded) {
    for (int expanded_index = 0; expanded_index < panel->expanded_count; expanded_index++) {
        if (panel->expanded_category_ids[expanded_index] != category_id) continue;
        if (!expanded) {
            panel->expanded_category_ids[expanded_index] = panel->expanded_category_ids[panel->expanded_count - 1];
            panel->expanded_count--;
        }
        return;
    }
    if (expanded && panel->expanded_count < ANNOTATION_MAX_CATEGORIES) {
        panel->expanded_category_ids[panel->expanded_count++] = category_id;
    }
}

static void ensure_current_category(WorldmapPanelState *panel, AnnotationSet *set) {
    if (annotation_category_find(set, panel->current_category_id)) return;
    panel->current_category_id = set->category_count > 0 ? set->categories[0].id : 0;
}

static void centre_on_annotation(WorldmapPanelState *panel, const Annotation *annotation) {
    if (annotation->kind == ANNOTATION_PATH) {
        if (annotation->segment_count <= 0) return;
        panel->view.centre_tile_x = annotation->segments[0].start_x + 0.5;
        panel->view.centre_tile_y = annotation->segments[0].start_y + 0.5;
        return;
    }
    panel->view.centre_tile_x = annotation->tile_x + 0.5;
    panel->view.centre_tile_y = annotation->tile_y + 0.5;
}

static void draw_outlined_text(ImDrawList *draw_list, ImVec2 position, ImU32 colour, const char *text) {
    static const float OFFSETS[4][2] = {{-1.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, -1.0F}, {0.0F, 1.0F}};
    for (int offset_index = 0; offset_index < 4; offset_index++) {
        draw_list->AddText(ImVec2(position.x + OFFSETS[offset_index][0], position.y + OFFSETS[offset_index][1]),
                           OUTLINE_COLOUR, text);
    }
    draw_list->AddText(position, colour, text);
}

static void draw_path(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list,
                      const Annotation *annotation, ImU32 colour, bool selected) {
    for (int segment_index = 0; segment_index < annotation->segment_count; segment_index++) {
        const AnnotationSegment *segment = &annotation->segments[segment_index];
        const ImVec2 start = tile_to_screen(panel, area, segment->start_x + 0.5, segment->start_y + 0.5);
        const ImVec2 end = tile_to_screen(panel, area, segment->end_x + 0.5, segment->end_y + 0.5);
        if (selected) draw_list->AddLine(start, end, HIGHLIGHT_COLOUR, PATH_THICKNESS + 2.0F);
        draw_list->AddLine(start, end, colour, PATH_THICKNESS);
    }
}

static void draw_pin(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list,
                     const Annotation *annotation, ImU32 colour, bool selected) {
    const ImVec2 centre = tile_to_screen(panel, area, annotation->tile_x + 0.5, annotation->tile_y + 0.5);
    if (selected) draw_list->AddCircle(centre, PIN_RADIUS + 3.0F, HIGHLIGHT_COLOUR, 0, 2.0F);
    draw_list->AddCircleFilled(centre, PIN_RADIUS, colour);
    draw_list->AddCircle(centre, PIN_RADIUS, OUTLINE_COLOUR);
}

static void draw_text_label(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list,
                            const Annotation *annotation, ImU32 colour, bool selected) {
    const ImVec2 anchor = tile_to_screen(panel, area, annotation->tile_x + 0.5, annotation->tile_y + 0.5);
    const ImVec2 text_size = ImGui::CalcTextSize(annotation->label);
    const ImVec2 position(floorf(anchor.x - text_size.x / 2.0F), floorf(anchor.y - text_size.y / 2.0F));
    if (selected) {
        draw_list->AddRect(ImVec2(position.x - 3.0F, position.y - 2.0F),
                           ImVec2(position.x + text_size.x + 3.0F, position.y + text_size.y + 2.0F), HIGHLIGHT_COLOUR);
    }
    draw_outlined_text(draw_list, position, colour, annotation->label);
}

static void draw_annotations(const WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area,
                             ImDrawList *draw_list) {
    for (int category_index = 0; category_index < set->category_count; category_index++) {
        const AnnotationCategory *category = &set->categories[category_index];
        if (!category->visible) continue;
        const ImU32 colour = category_colour(category);
        for (int annotation_index = 0; annotation_index < category->annotation_count; annotation_index++) {
            const Annotation *annotation = &category->annotations[annotation_index];
            const bool selected = annotation->id == panel->selected_annotation_id;
            switch (annotation->kind) {
                case ANNOTATION_PATH: draw_path(panel, area, draw_list, annotation, colour, selected); break;
                case ANNOTATION_PIN: draw_pin(panel, area, draw_list, annotation, colour, selected); break;
                case ANNOTATION_TEXT: draw_text_label(panel, area, draw_list, annotation, colour, selected); break;
            }
        }
    }
}

static void draw_world_texture(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list) {
    const ImVec2 area_end(area->origin.x + area->size.x, area->origin.y + area->size.y);
    const ImVec2 world_start = tile_to_screen(panel, area, 0.0, 0.0);
    const ImVec2 world_end = tile_to_screen(panel, area, WORLDMAP_TILES_DOWN, WORLDMAP_TILES_ACROSS);
    const ImVec2 visible_start(fmaxf(world_start.x, area->origin.x), fmaxf(world_start.y, area->origin.y));
    const ImVec2 visible_end(fminf(world_end.x, area_end.x), fminf(world_end.y, area_end.y));
    if (visible_end.x <= visible_start.x || visible_end.y <= visible_start.y) return;

    // Horizontal on screen is map y (texture u spans 2048 tiles), vertical is map x (texture v spans 1024 tiles).
    double start_tile_x, start_tile_y, end_tile_x, end_tile_y;
    worldmap_view_panel_to_tile(&panel->view, area->size.x, area->size.y, visible_start.x - area->origin.x,
                                visible_start.y - area->origin.y, &start_tile_x, &start_tile_y);
    worldmap_view_panel_to_tile(&panel->view, area->size.x, area->size.y, visible_end.x - area->origin.x,
                                visible_end.y - area->origin.y, &end_tile_x, &end_tile_y);
    const ImVec2 uv_start((float) (start_tile_y / WORLDMAP_TILES_ACROSS), (float) (start_tile_x / WORLDMAP_TILES_DOWN));
    const ImVec2 uv_end((float) (end_tile_y / WORLDMAP_TILES_ACROSS), (float) (end_tile_x / WORLDMAP_TILES_DOWN));

    const unsigned int texture_id = sdl_worldmap_texture(minimap_tile_colours(), minimap_revision());
    if (!texture_id) return;
    draw_list->AddImage((ImTextureID) texture_id, visible_start, visible_end, uv_start, uv_end);
}

static void draw_player_marker(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list) {
    int player_tile_x, player_tile_y;
    engine_player_tile(&player_tile_x, &player_tile_y);
    const ImVec2 centre = tile_to_screen(panel, area, player_tile_x + 0.5, player_tile_y + 0.5);
    draw_list->AddRectFilled(ImVec2(floorf(centre.x) - 1.0F, floorf(centre.y) - 1.0F),
                             ImVec2(floorf(centre.x) + 2.0F, floorf(centre.y) + 2.0F), HIGHLIGHT_COLOUR);
}

static void centre_on_player(WorldmapPanelState *panel) {
    int player_tile_x, player_tile_y;
    engine_player_tile(&player_tile_x, &player_tile_y);
    panel->view.centre_tile_x = player_tile_x + 0.5;
    panel->view.centre_tile_y = player_tile_y + 0.5;
    panel->view.pixels_per_tile = CENTRE_ON_PLAYER_PIXELS_PER_TILE;
}

static void handle_map_input(WorldmapPanelState *panel, const MapArea *area) {
    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 mouse(io.MousePos.x - area->origin.x, io.MousePos.y - area->origin.y);

    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0F) {
        worldmap_view_zoom_at(&panel->view, area->size.x, area->size.y, mouse.x, mouse.y,
                              pow(ZOOM_STEP, io.MouseWheel));
    }

    if (ImGui::IsItemActive()) {
        const bool right_drag = ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0F);
        const bool pan_drag =
                panel->active_tool == WORLDMAP_TOOL_PAN && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F);
        if (right_drag || pan_drag) {
            worldmap_view_pan(&panel->view, area->size.x, area->size.y, io.MouseDelta.x, io.MouseDelta.y);
        }
    }

    panel->hovered_tile_valid = false;
    if (ImGui::IsItemHovered()) {
        double tile_x, tile_y;
        worldmap_view_panel_to_tile(&panel->view, area->size.x, area->size.y, mouse.x, mouse.y, &tile_x, &tile_y);
        if (tile_x >= 0.0 && tile_x < WORLDMAP_TILES_DOWN && tile_y >= 0.0 && tile_y < WORLDMAP_TILES_ACROSS) {
            panel->hovered_tile_valid = true;
            panel->hovered_tile_x = (int) floor(tile_x);
            panel->hovered_tile_y = (int) floor(tile_y);
        }
    }
}

static void show_pin_tooltip(const WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area) {
    if (!ImGui::IsItemHovered()) return;
    const ImVec2 mouse_position = ImGui::GetIO().MousePos;
    const WorldmapHit hit =
            worldmap_hit_test(set, &panel->view, area->size.x, area->size.y, mouse_position.x - area->origin.x,
                              mouse_position.y - area->origin.y, PIN_HOVER_RADIUS);
    if (hit.annotation_id == 0) return;
    const Annotation *annotation = annotation_find(set, hit.annotation_id, NULL);
    if (annotation && annotation->kind == ANNOTATION_PIN && annotation->label[0] != '\0') {
        ImGui::SetTooltip("%s", annotation->label);
    }
}

static void render_map_area(WorldmapPanelState *panel, AnnotationSet *set, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::BeginChild("map", ImVec2(0.0F, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    MapArea area = {ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail()};
    if (area.size.x >= 1.0F && area.size.y >= 1.0F) {
        if (game_ui_state.worldmap_centre_on_player) {
            centre_on_player(panel);
            game_ui_state.worldmap_centre_on_player = false;
        }
        worldmap_view_clamp(&panel->view, area.size.x, area.size.y);

        ImGui::InvisibleButton("canvas", area.size,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        handle_map_input(panel, &area);

        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const ImVec2 area_end(area.origin.x + area.size.x, area.origin.y + area.size.y);
        draw_list->PushClipRect(area.origin, area_end, true);
        draw_list->AddRectFilled(area.origin, area_end, BACKGROUND_COLOUR);
        draw_world_texture(panel, &area, draw_list);
        draw_annotations(panel, set, &area, draw_list);
        draw_player_marker(panel, &area, draw_list);
        draw_list->PopClipRect();

        show_pin_tooltip(panel, set, &area);
    }
    ImGui::EndChild();
}

static const char *annotation_kind_prefix(AnnotationKind kind) {
    switch (kind) {
        case ANNOTATION_PIN: return "Pin: ";
        case ANNOTATION_TEXT: return "Text: ";
        case ANNOTATION_PATH: return "Path: ";
    }
    return "";
}

static void render_annotation_rows(WorldmapPanelState *panel, const AnnotationCategory *category) {
    for (int annotation_index = 0; annotation_index < category->annotation_count; annotation_index++) {
        const Annotation *annotation = &category->annotations[annotation_index];
        char row_text[ANNOTATION_LABEL_MAX + 16];
        snprintf(row_text, sizeof(row_text), "%s%s", annotation_kind_prefix(annotation->kind), annotation->label);
        ImGui::PushID(annotation->id);
        if (ImGui::Selectable(row_text, annotation->id == panel->selected_annotation_id)) {
            panel->selected_annotation_id = annotation->id;
            centre_on_annotation(panel, annotation);
        }
        ImGui::PopID();
    }
}

static void render_category_row(WorldmapPanelState *panel, AnnotationSet *set, AnnotationCategory *category) {
    ImGui::PushID(category->id);

    bool visible = category->visible != 0;
    if (ImGui::Checkbox("##visible", &visible)) {
        category->visible = visible ? 1 : 0;
        set->changed = 1;
        worldmap_save_annotations();
    }

    ImGui::SameLine();
    const ImVec4 swatch = ImGui::ColorConvertU32ToFloat4(category_colour(category));
    ImGui::ColorButton("##swatch", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                       ImVec2(SWATCH_SIZE, SWATCH_SIZE));

    ImGui::SameLine();
    ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (category->id == panel->current_category_id) node_flags |= ImGuiTreeNodeFlags_Selected;
    ImGui::SetNextItemOpen(category_is_expanded(panel, category->id));
    const bool expanded = ImGui::TreeNodeEx("##category", node_flags, "%s", category->name);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) panel->current_category_id = category->id;
    set_category_expanded(panel, category->id, expanded);

    if (expanded) {
        render_annotation_rows(panel, category);
        ImGui::TreePop();
    }

    ImGui::PopID();
}

static void render_category_list(WorldmapPanelState *panel, AnnotationSet *set, float height) {
    ImGui::BeginChild("categories", ImVec2(CATEGORY_COLUMN_WIDTH, height), ImGuiChildFlags_Borders);

    const ImGuiStyle &style = ImGui::GetStyle();
    const float footer_height = ImGui::GetFrameHeightWithSpacing() * 2.0F + style.ItemSpacing.y;
    ImGui::BeginChild("category_rows", ImVec2(0.0F, -footer_height));
    for (int category_index = 0; category_index < set->category_count; category_index++) {
        render_category_row(panel, set, &set->categories[category_index]);
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginDisabled();
    ImGui::Button("+ New");
    ImGui::SameLine();
    ImGui::Button("Import");
    ImGui::SameLine();
    ImGui::Button("Export");
    ImGui::EndDisabled();

    bool show_on_minimap = set->show_on_minimap != 0;
    if (ImGui::Checkbox("Show on minimap", &show_on_minimap)) {
        set->show_on_minimap = show_on_minimap ? 1 : 0;
        set->changed = 1;
        worldmap_save_annotations();
    }

    ImGui::EndChild();
}

static bool tool_is_available(WorldmapTool tool) {
    return tool == WORLDMAP_TOOL_SELECT || tool == WORLDMAP_TOOL_PAN;
}

static void render_bottom_bar(WorldmapPanelState *panel) {
    for (int tool_index = 0; tool_index < WORLDMAP_TOOL_COUNT; tool_index++) {
        const WorldmapTool tool = (WorldmapTool) tool_index;
        const bool active = panel->active_tool == tool;
        if (tool_index > 0) ImGui::SameLine();
        ImGui::BeginDisabled(!tool_is_available(tool));
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(TOOL_NAMES[tool_index])) panel->active_tool = tool;
        if (active) ImGui::PopStyleColor();
        ImGui::EndDisabled();
    }

    if (panel->hovered_tile_valid) {
        char coordinates[32];
        snprintf(coordinates, sizeof(coordinates), "%d, %d", panel->hovered_tile_x, panel->hovered_tile_y);
        const float text_width = ImGui::CalcTextSize(coordinates).x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - text_width);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(coordinates);
    }
}

void worldmap_window_render(void) {
    WorldmapPanelState *panel = &panel_state;
    AnnotationSet *set = worldmap_annotations();
    bool open = true;

    ImGui::SetNextWindowSize(ImVec2(900.0F, 620.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Map", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ensure_current_category(panel, set);

        const float bar_height = ImGui::GetFrameHeightWithSpacing();
        const float body_height = ImGui::GetContentRegionAvail().y - bar_height;

        render_category_list(panel, set, body_height);
        ImGui::SameLine();
        render_map_area(panel, set, body_height);
        render_bottom_bar(panel);
    }
    ImGui::End();

    if (!open) game_ui_state.show_worldmap = false;
}
