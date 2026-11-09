#include "worldmap_window.hpp"

#include <SDL2/SDL.h>
#include <imgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "game/annotation_code.h"
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

enum LabelPurpose { LABEL_PURPOSE_NONE, LABEL_PURPOSE_PIN, LABEL_PURPOSE_TEXT, LABEL_PURPOSE_PATH };

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

    bool left_press_active;
    bool press_inside_world;
    int press_tile_x;
    int press_tile_y;
    bool right_press_active;

    AnnotationSegment pending_segments[ANNOTATION_MAX_SEGMENTS];
    int pending_segment_count;
    int pending_category_id;

    bool editing;
    int edit_annotation_id;
    Annotation edit_copy;

    const char *popup_request;
    LabelPurpose label_purpose;
    char label_text[ANNOTATION_LABEL_MAX];
    int point_category_id;
    int point_tile_x;
    int point_tile_y;
    int menu_annotation_id;
    int menu_category_id;
    char new_category_name[ANNOTATION_CATEGORY_NAME_MAX];
    int new_category_colour;
    char *import_text;
    int import_capacity;
    AnnotationCategory import_preview;
    AnnotationCodeStatus import_status;
    char export_message[64];
    bool export_too_long;
    char status_text[64];
    double status_until;
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
static const double SELECT_RADIUS = 8.0;
static const double SNAP_PIXELS = 8.0;
static const float PATH_THICKNESS = 2.0F;
static const float DASH_LENGTH = 6.0F;
static const float DASH_GAP = 4.0F;
static const float SWATCH_SIZE = 12.0F;
static const float PICKER_SWATCH_SIZE = 20.0F;
static const float EDIT_LABEL_WIDTH = 180.0F;
static const double STATUS_SECONDS = 3.0;
static const int IMPORT_INITIAL_CAPACITY = 4096;
static const ImU32 OUTLINE_COLOUR = IM_COL32(0, 0, 0, 255);
static const ImU32 BACKGROUND_COLOUR = IM_COL32(0, 0, 0, 255);
static const ImU32 HIGHLIGHT_COLOUR = IM_COL32(255, 255, 255, 255);
static const char *const TOOL_NAMES[WORLDMAP_TOOL_COUNT] = {"Select", "Pan", "Pin", "Line", "Text"};

static const char *const ANNOTATION_MENU_ID = "annotation_menu";
static const char *const CATEGORY_MENU_ID = "category_menu";
static const char *const LABEL_POPUP_ID = "Label###label_popup";
static const char *const NEW_CATEGORY_POPUP_ID = "New category###new_category_popup";
static const char *const IMPORT_POPUP_ID = "Import###import_popup";
static const char *const EXPORT_POPUP_ID = "Export###export_popup";
static const char *const DELETE_CATEGORY_POPUP_ID = "Delete category###delete_category_popup";

static WorldmapPanelState initial_panel_state(void) {
    WorldmapPanelState state = {};
    state.view.centre_tile_x = WORLDMAP_TILES_DOWN / 2.0;
    state.view.centre_tile_y = WORLDMAP_TILES_ACROSS / 2.0;
    state.active_tool = WORLDMAP_TOOL_SELECT;
    return state;
}

static WorldmapPanelState panel_state = initial_panel_state();

static ImU32 palette_colour(int colour_index) {
    const unsigned int rgb = annotation_palette_rgb(colour_index);
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

static ImU32 category_colour(const AnnotationCategory *category) {
    return palette_colour(category->colour_index);
}

static ImU32 category_colour_by_id(AnnotationSet *set, int category_id) {
    const AnnotationCategory *category = annotation_category_find(set, category_id);
    return category ? category_colour(category) : HIGHLIGHT_COLOUR;
}

static void set_status(WorldmapPanelState *panel, const char *text) {
    annotation_copy_text(panel->status_text, sizeof(panel->status_text), text);
    panel->status_until = ImGui::GetTime() + STATUS_SECONDS;
}

static ImVec2 tile_to_screen(const WorldmapPanelState *panel, const MapArea *area, double tile_x, double tile_y) {
    double panel_x, panel_y;
    worldmap_view_tile_to_panel(&panel->view, area->size.x, area->size.y, tile_x, tile_y, &panel_x, &panel_y);
    return ImVec2(area->origin.x + (float) panel_x, area->origin.y + (float) panel_y);
}

static ImVec2 tile_centre_to_screen(const WorldmapPanelState *panel, const MapArea *area, int tile_x, int tile_y) {
    return tile_to_screen(panel, area, tile_x + 0.5, tile_y + 0.5);
}

static int clamp_int(int value, int minimum, int maximum) {
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

static bool mouse_tile(const WorldmapPanelState *panel, const MapArea *area, int *tile_x, int *tile_y) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    double exact_x, exact_y;
    worldmap_view_panel_to_tile(&panel->view, area->size.x, area->size.y, mouse.x - area->origin.x,
                                mouse.y - area->origin.y, &exact_x, &exact_y);
    const int floor_x = (int) floor(exact_x);
    const int floor_y = (int) floor(exact_y);
    *tile_x = clamp_int(floor_x, 0, WORLDMAP_TILES_DOWN - 1);
    *tile_y = clamp_int(floor_y, 0, WORLDMAP_TILES_ACROSS - 1);
    return floor_x == *tile_x && floor_y == *tile_y;
}

static double snap_distance_tiles(const WorldmapPanelState *panel) {
    return fmax(1.0, SNAP_PIXELS / panel->view.pixels_per_tile);
}

static bool mouse_dragged(ImGuiMouseButton button) {
    const ImVec2 delta = ImGui::GetMouseDragDelta(button);
    return delta.x != 0.0F || delta.y != 0.0F;
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

static void cancel_edit(WorldmapPanelState *panel) {
    if (!panel->editing) return;
    annotation_release(&panel->edit_copy);
    panel->editing = false;
    panel->edit_annotation_id = 0;
}

static void save_edit(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!panel->editing) return;
    Annotation *edited = &panel->edit_copy;
    if (edited->kind == ANNOTATION_PATH && edited->segment_count == 0) {
        annotation_remove(set, panel->edit_annotation_id);
        if (panel->selected_annotation_id == panel->edit_annotation_id) panel->selected_annotation_id = 0;
    } else {
        if (edited->kind == ANNOTATION_PATH) {
            edited->tile_x = edited->segments[0].start_x;
            edited->tile_y = edited->segments[0].start_y;
        }
        annotation_replace(set, panel->edit_annotation_id, edited);
    }
    worldmap_save_annotations();
    cancel_edit(panel);
}

static void request_path_label(WorldmapPanelState *panel) {
    if (panel->pending_segment_count == 0) return;
    panel->label_purpose = LABEL_PURPOSE_PATH;
    panel->label_text[0] = '\0';
    panel->popup_request = LABEL_POPUP_ID;
}

static void request_point_label(WorldmapPanelState *panel, AnnotationKind kind, int tile_x, int tile_y) {
    panel->label_purpose = kind == ANNOTATION_PIN ? LABEL_PURPOSE_PIN : LABEL_PURPOSE_TEXT;
    panel->label_text[0] = '\0';
    panel->point_category_id = panel->current_category_id;
    panel->point_tile_x = tile_x;
    panel->point_tile_y = tile_y;
    panel->popup_request = LABEL_POPUP_ID;
}

static void set_active_tool(WorldmapPanelState *panel, WorldmapTool tool) {
    if (tool == panel->active_tool) return;
    cancel_edit(panel);
    if (panel->active_tool == WORLDMAP_TOOL_LINE) request_path_label(panel);
    panel->active_tool = tool;
    if (tool == WORLDMAP_TOOL_LINE) {
        panel->pending_segment_count = 0;
        panel->pending_category_id = panel->current_category_id;
    }
}

static void select_annotation(WorldmapPanelState *panel, int annotation_id) {
    if (panel->editing && panel->edit_annotation_id != annotation_id) cancel_edit(panel);
    panel->selected_annotation_id = annotation_id;
}

static void begin_edit(WorldmapPanelState *panel, AnnotationSet *set, int annotation_id) {
    set_active_tool(panel, WORLDMAP_TOOL_SELECT);
    cancel_edit(panel);
    const Annotation *annotation = annotation_find(set, annotation_id, NULL);
    if (!annotation || !annotation_copy(&panel->edit_copy, annotation)) return;
    panel->editing = true;
    panel->edit_annotation_id = annotation_id;
    panel->selected_annotation_id = annotation_id;
}

static void delete_annotation(WorldmapPanelState *panel, AnnotationSet *set, int annotation_id) {
    if (panel->editing && panel->edit_annotation_id == annotation_id) cancel_edit(panel);
    if (panel->selected_annotation_id == annotation_id) panel->selected_annotation_id = 0;
    if (annotation_remove(set, annotation_id)) worldmap_save_annotations();
}

static bool annotation_in_category(AnnotationSet *set, int annotation_id, int category_id) {
    AnnotationCategory *owner = NULL;
    return annotation_id != 0 && annotation_find(set, annotation_id, &owner) && owner->id == category_id;
}

static void delete_category(WorldmapPanelState *panel, AnnotationSet *set, int category_id) {
    if (panel->editing && annotation_in_category(set, panel->edit_annotation_id, category_id)) cancel_edit(panel);
    if (annotation_in_category(set, panel->selected_annotation_id, category_id)) panel->selected_annotation_id = 0;
    if (panel->pending_category_id == category_id) panel->pending_segment_count = 0;
    if (panel->current_category_id == category_id) panel->current_category_id = 0;
    set_category_expanded(panel, category_id, false);
    if (annotation_category_remove(set, category_id)) worldmap_save_annotations();
}

static void export_category(WorldmapPanelState *panel, AnnotationSet *set, int category_id) {
    const AnnotationCategory *category = annotation_category_find(set, category_id);
    if (!category) return;
    char *code = annotation_code_export(category);
    if (!code) {
        set_status(panel, "Export failed.");
        return;
    }
    const int code_length = (int) strlen(code);
    const bool copied = SDL_SetClipboardText(code) == 0;
    free(code);
    if (!copied) {
        set_status(panel, "Could not copy to the clipboard.");
        return;
    }
    snprintf(panel->export_message, sizeof(panel->export_message), "Copied %d characters to the clipboard.",
             code_length);
    panel->export_too_long = code_length > ANNOTATION_CODE_DISCORD_LIMIT;
    panel->popup_request = EXPORT_POPUP_ID;
}

static void release_import(WorldmapPanelState *panel) {
    free(panel->import_text);
    panel->import_text = NULL;
    panel->import_capacity = 0;
    annotation_category_release(&panel->import_preview);
}

static void update_import_preview(WorldmapPanelState *panel) {
    annotation_category_release(&panel->import_preview);
    panel->import_status = annotation_code_import(panel->import_text, &panel->import_preview);
}

static void open_import(WorldmapPanelState *panel) {
    release_import(panel);

    char *clipboard = SDL_GetClipboardText();
    const char *trimmed = clipboard ? clipboard : "";
    while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\r' || *trimmed == '\n') trimmed++;
    const bool is_code = strncmp(trimmed, ANNOTATION_CODE_PREFIX, strlen(ANNOTATION_CODE_PREFIX)) == 0;
    const int wanted_capacity = is_code ? (int) strlen(trimmed) + 1 : 0;

    panel->import_capacity = wanted_capacity > IMPORT_INITIAL_CAPACITY ? wanted_capacity : IMPORT_INITIAL_CAPACITY;
    panel->import_text = (char *) malloc((size_t) panel->import_capacity);
    if (panel->import_text) {
        annotation_copy_text(panel->import_text, (size_t) panel->import_capacity, is_code ? trimmed : "");
        update_import_preview(panel);
        panel->popup_request = IMPORT_POPUP_ID;
    } else {
        panel->import_capacity = 0;
    }
    SDL_free(clipboard);
}

static void open_new_category(WorldmapPanelState *panel) {
    panel->new_category_name[0] = '\0';
    panel->new_category_colour = 0;
    panel->popup_request = NEW_CATEGORY_POPUP_ID;
}

static void draw_outlined_text(ImDrawList *draw_list, ImVec2 position, ImU32 colour, const char *text) {
    static const float OFFSETS[4][2] = {{-1.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, -1.0F}, {0.0F, 1.0F}};
    for (int offset_index = 0; offset_index < 4; offset_index++) {
        draw_list->AddText(ImVec2(position.x + OFFSETS[offset_index][0], position.y + OFFSETS[offset_index][1]),
                           OUTLINE_COLOUR, text);
    }
    draw_list->AddText(position, colour, text);
}

static void draw_dashed_line(ImDrawList *draw_list, ImVec2 start, ImVec2 end, ImU32 colour) {
    const float delta_x = end.x - start.x;
    const float delta_y = end.y - start.y;
    const float length = sqrtf(delta_x * delta_x + delta_y * delta_y);
    if (length < 1.0F) return;
    for (float distance = 0.0F; distance < length; distance += DASH_LENGTH + DASH_GAP) {
        const float dash_end = fminf(distance + DASH_LENGTH, length);
        draw_list->AddLine(ImVec2(start.x + delta_x * distance / length, start.y + delta_y * distance / length),
                           ImVec2(start.x + delta_x * dash_end / length, start.y + delta_y * dash_end / length), colour,
                           PATH_THICKNESS);
    }
}

static void draw_path(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list,
                      const Annotation *annotation, ImU32 colour, bool selected) {
    for (int segment_index = 0; segment_index < annotation->segment_count; segment_index++) {
        const AnnotationSegment *segment = &annotation->segments[segment_index];
        const ImVec2 start = tile_centre_to_screen(panel, area, segment->start_x, segment->start_y);
        const ImVec2 end = tile_centre_to_screen(panel, area, segment->end_x, segment->end_y);
        if (selected) draw_list->AddLine(start, end, HIGHLIGHT_COLOUR, PATH_THICKNESS + 2.0F);
        draw_list->AddLine(start, end, colour, PATH_THICKNESS);
    }
}

static void draw_pin(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list,
                     const Annotation *annotation, ImU32 colour, bool selected) {
    const ImVec2 centre = tile_centre_to_screen(panel, area, annotation->tile_x, annotation->tile_y);
    if (selected) draw_list->AddCircle(centre, PIN_RADIUS + 3.0F, HIGHLIGHT_COLOUR, 0, 2.0F);
    draw_list->AddCircleFilled(centre, PIN_RADIUS, colour);
    draw_list->AddCircle(centre, PIN_RADIUS, OUTLINE_COLOUR);
}

static void draw_text_label(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list,
                            const Annotation *annotation, ImU32 colour, bool selected) {
    const ImVec2 anchor = tile_centre_to_screen(panel, area, annotation->tile_x, annotation->tile_y);
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
            if (panel->editing && annotation->id == panel->edit_annotation_id) annotation = &panel->edit_copy;
            switch (annotation->kind) {
                case ANNOTATION_PATH: draw_path(panel, area, draw_list, annotation, colour, selected); break;
                case ANNOTATION_PIN: draw_pin(panel, area, draw_list, annotation, colour, selected); break;
                case ANNOTATION_TEXT: draw_text_label(panel, area, draw_list, annotation, colour, selected); break;
            }
        }
    }
}

static void draw_pending_path(const WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area,
                              ImDrawList *draw_list) {
    const ImU32 colour = category_colour_by_id(set, panel->pending_category_id);
    for (int segment_index = 0; segment_index < panel->pending_segment_count; segment_index++) {
        const AnnotationSegment *segment = &panel->pending_segments[segment_index];
        draw_dashed_line(draw_list, tile_centre_to_screen(panel, area, segment->start_x, segment->start_y),
                         tile_centre_to_screen(panel, area, segment->end_x, segment->end_y), colour);
    }
}

static bool drawing_segment(const WorldmapPanelState *panel) {
    if (panel->editing) return panel->edit_copy.kind == ANNOTATION_PATH;
    return panel->active_tool == WORLDMAP_TOOL_LINE;
}

static void draw_drag_preview(const WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area,
                              ImDrawList *draw_list) {
    if (!panel->left_press_active || !panel->press_inside_world || !drawing_segment(panel)) return;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !mouse_dragged(ImGuiMouseButton_Left)) return;

    int category_id = panel->current_category_id;
    if (panel->editing) {
        AnnotationCategory *owner = NULL;
        if (annotation_find(set, panel->edit_annotation_id, &owner)) category_id = owner->id;
    }
    int mouse_tile_x, mouse_tile_y;
    mouse_tile(panel, area, &mouse_tile_x, &mouse_tile_y);
    draw_dashed_line(draw_list, tile_centre_to_screen(panel, area, panel->press_tile_x, panel->press_tile_y),
                     tile_centre_to_screen(panel, area, mouse_tile_x, mouse_tile_y),
                     category_colour_by_id(set, category_id));
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
    const ImVec2 centre = tile_centre_to_screen(panel, area, player_tile_x, player_tile_y);
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

static WorldmapHit hit_test_at_mouse(const WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area,
                                     double radius_pixels) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return worldmap_hit_test(set, &panel->view, area->size.x, area->size.y, mouse.x - area->origin.x,
                             mouse.y - area->origin.y, radius_pixels);
}

static bool append_segment(AnnotationSegment *segment, int start_x, int start_y, int end_x, int end_y) {
    if (start_x == end_x && start_y == end_y) return false;
    segment->start_x = (unsigned short) start_x;
    segment->start_y = (unsigned short) start_y;
    segment->end_x = (unsigned short) end_x;
    segment->end_y = (unsigned short) end_y;
    return true;
}

static void add_pending_segment(WorldmapPanelState *panel, int end_x, int end_y) {
    if (panel->pending_segment_count >= ANNOTATION_MAX_SEGMENTS) {
        set_status(panel, "Path is full.");
        return;
    }
    if (panel->pending_segment_count == 0) panel->pending_category_id = panel->current_category_id;
    if (append_segment(&panel->pending_segments[panel->pending_segment_count], panel->press_tile_x, panel->press_tile_y,
                       end_x, end_y)) {
        panel->pending_segment_count++;
    }
}

static void add_edit_segment(WorldmapPanelState *panel, int end_x, int end_y) {
    Annotation *edited = &panel->edit_copy;
    if (panel->press_tile_x == end_x && panel->press_tile_y == end_y) return;
    if (edited->segment_count >= ANNOTATION_MAX_SEGMENTS) {
        set_status(panel, "Path is full.");
        return;
    }
    AnnotationSegment *grown = (AnnotationSegment *) realloc(
            edited->segments, sizeof(AnnotationSegment) * (size_t) (edited->segment_count + 1));
    if (!grown) return;
    edited->segments = grown;
    if (append_segment(&grown[edited->segment_count], panel->press_tile_x, panel->press_tile_y, end_x, end_y)) {
        edited->segment_count++;
    }
}

static bool delete_edit_segment_at_mouse(WorldmapPanelState *panel, const MapArea *area) {
    Annotation *edited = &panel->edit_copy;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int segment_index = worldmap_hit_segment(edited, &panel->view, area->size.x, area->size.y,
                                                   mouse.x - area->origin.x, mouse.y - area->origin.y, SELECT_RADIUS);
    if (segment_index < 0) return false;
    memmove(&edited->segments[segment_index], &edited->segments[segment_index + 1],
            sizeof(AnnotationSegment) * (size_t) (edited->segment_count - segment_index - 1));
    edited->segment_count--;
    return true;
}

static void begin_left_press(WorldmapPanelState *panel, const MapArea *area) {
    int tile_x, tile_y;
    panel->press_inside_world = mouse_tile(panel, area, &tile_x, &tile_y);

    const AnnotationSegment *snap_segments = NULL;
    int snap_count = 0;
    if (panel->editing && panel->edit_copy.kind == ANNOTATION_PATH) {
        snap_segments = panel->edit_copy.segments;
        snap_count = panel->edit_copy.segment_count;
    } else if (!panel->editing && panel->active_tool == WORLDMAP_TOOL_LINE) {
        snap_segments = panel->pending_segments;
        snap_count = panel->pending_segment_count;
    }
    if (snap_count > 0) {
        worldmap_snap_to_endpoint(snap_segments, snap_count, tile_x, tile_y, snap_distance_tiles(panel), &tile_x,
                                  &tile_y);
    }

    panel->left_press_active = true;
    panel->press_tile_x = tile_x;
    panel->press_tile_y = tile_y;
}

static void finish_edit_press(WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area, bool dragged,
                              int release_x, int release_y) {
    const bool editing_path = panel->edit_copy.kind == ANNOTATION_PATH;
    if (dragged) {
        if (editing_path && panel->press_inside_world) add_edit_segment(panel, release_x, release_y);
        return;
    }
    if (editing_path && delete_edit_segment_at_mouse(panel, area)) return;
    const int hit_id = hit_test_at_mouse(panel, set, area, SELECT_RADIUS).annotation_id;
    if (hit_id != 0 && hit_id != panel->edit_annotation_id) select_annotation(panel, hit_id);
}

static void finish_left_press(WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area, bool dragged) {
    int release_x, release_y;
    mouse_tile(panel, area, &release_x, &release_y);

    if (panel->editing) {
        finish_edit_press(panel, set, area, dragged, release_x, release_y);
        return;
    }
    switch (panel->active_tool) {
        case WORLDMAP_TOOL_SELECT:
            if (!dragged) select_annotation(panel, hit_test_at_mouse(panel, set, area, SELECT_RADIUS).annotation_id);
            break;
        case WORLDMAP_TOOL_PIN:
        case WORLDMAP_TOOL_TEXT:
            if (!dragged && panel->press_inside_world) {
                const AnnotationKind kind = panel->active_tool == WORLDMAP_TOOL_PIN ? ANNOTATION_PIN : ANNOTATION_TEXT;
                request_point_label(panel, kind, panel->press_tile_x, panel->press_tile_y);
            }
            break;
        case WORLDMAP_TOOL_LINE:
            if (panel->press_inside_world) add_pending_segment(panel, release_x, release_y);
            break;
        default: break;
    }
}

static void handle_left_mouse(WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area) {
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) begin_left_press(panel, area);
    if (!panel->left_press_active) return;

    const bool dragged = mouse_dragged(ImGuiMouseButton_Left);
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (dragged && panel->editing && panel->edit_copy.kind != ANNOTATION_PATH) {
            int tile_x, tile_y;
            mouse_tile(panel, area, &tile_x, &tile_y);
            panel->edit_copy.tile_x = (unsigned short) tile_x;
            panel->edit_copy.tile_y = (unsigned short) tile_y;
        }
        return;
    }
    panel->left_press_active = false;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) finish_left_press(panel, set, area, dragged);
}

static void handle_right_mouse(WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area) {
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) panel->right_press_active = true;
    if (!panel->right_press_active || ImGui::IsMouseDown(ImGuiMouseButton_Right)) return;
    panel->right_press_active = false;
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Right) || mouse_dragged(ImGuiMouseButton_Right)) return;
    if (panel->left_press_active) return;

    const WorldmapHit hit = hit_test_at_mouse(panel, set, area, SELECT_RADIUS);
    if (hit.annotation_id == 0) return;
    panel->menu_annotation_id = hit.annotation_id;
    panel->popup_request = ANNOTATION_MENU_ID;
}

static void handle_map_input(WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area) {
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

    if (ImGui::IsItemHovered()) {
        int tile_x, tile_y;
        if (mouse_tile(panel, area, &tile_x, &tile_y)) {
            panel->hovered_tile_valid = true;
            panel->hovered_tile_x = tile_x;
            panel->hovered_tile_y = tile_y;
        }
    }

    handle_left_mouse(panel, set, area);
    handle_right_mouse(panel, set, area);
}

static void handle_line_keys(WorldmapPanelState *panel) {
    if (panel->active_tool != WORLDMAP_TOOL_LINE || panel->pending_segment_count == 0) return;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        request_path_label(panel);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        panel->pending_segment_count = 0;
    }
}

static void show_pin_tooltip(const WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area) {
    if (!ImGui::IsItemHovered()) return;
    const WorldmapHit hit = hit_test_at_mouse(panel, set, area, PIN_HOVER_RADIUS);
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
        handle_map_input(panel, set, &area);

        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const ImVec2 area_end(area.origin.x + area.size.x, area.origin.y + area.size.y);
        draw_list->PushClipRect(area.origin, area_end, true);
        draw_list->AddRectFilled(area.origin, area_end, BACKGROUND_COLOUR);
        draw_world_texture(panel, &area, draw_list);
        draw_annotations(panel, set, &area, draw_list);
        draw_pending_path(panel, set, &area, draw_list);
        draw_drag_preview(panel, set, &area, draw_list);
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

static void render_annotation_rows(WorldmapPanelState *panel, AnnotationSet *set, const AnnotationCategory *category) {
    for (int annotation_index = 0; annotation_index < category->annotation_count; annotation_index++) {
        const Annotation *annotation = &category->annotations[annotation_index];
        const bool selected = annotation->id == panel->selected_annotation_id;
        ImGui::PushID(annotation->id);

        const float row_start_x = ImGui::GetCursorPosX();
        if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap)) {
            select_annotation(panel, annotation->id);
            centre_on_annotation(panel, annotation);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            panel->menu_annotation_id = annotation->id;
            panel->popup_request = ANNOTATION_MENU_ID;
        }
        ImGui::SameLine(row_start_x);
        ImGui::TextUnformatted(annotation_kind_prefix(annotation->kind));
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::TextUnformatted(annotation->label);

        const bool being_edited = panel->editing && panel->edit_annotation_id == annotation->id;
        if (selected && !being_edited) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit")) begin_edit(panel, set, annotation->id);
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
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        panel->menu_category_id = category->id;
        panel->popup_request = CATEGORY_MENU_ID;
    }
    set_category_expanded(panel, category->id, expanded);

    if (expanded) {
        render_annotation_rows(panel, set, category);
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
    if (ImGui::Button("+ New")) open_new_category(panel);
    ImGui::SameLine();
    if (ImGui::Button("Import")) open_import(panel);
    ImGui::SameLine();
    ImGui::BeginDisabled(!annotation_category_find(set, panel->current_category_id));
    if (ImGui::Button("Export")) export_category(panel, set, panel->current_category_id);
    ImGui::EndDisabled();

    bool show_on_minimap = set->show_on_minimap != 0;
    if (ImGui::Checkbox("Show on minimap", &show_on_minimap)) {
        set->show_on_minimap = show_on_minimap ? 1 : 0;
        set->changed = 1;
        worldmap_save_annotations();
    }

    ImGui::EndChild();
}

static bool tool_is_available(WorldmapTool tool, bool has_category) {
    return tool == WORLDMAP_TOOL_SELECT || tool == WORLDMAP_TOOL_PAN || has_category;
}

static void render_edit_controls(WorldmapPanelState *panel, AnnotationSet *set) {
    if (panel->editing) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(EDIT_LABEL_WIDTH);
        ImGui::InputText("##edit_label", panel->edit_copy.label, sizeof(panel->edit_copy.label));
        ImGui::SameLine();
        const bool empty_text = panel->edit_copy.kind == ANNOTATION_TEXT && panel->edit_copy.label[0] == '\0';
        ImGui::BeginDisabled(empty_text);
        if (ImGui::Button("Save")) save_edit(panel, set);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) cancel_edit(panel);
        return;
    }
    if (panel->selected_annotation_id != 0 && annotation_find(set, panel->selected_annotation_id, NULL)) {
        ImGui::SameLine();
        if (ImGui::Button("Edit")) begin_edit(panel, set, panel->selected_annotation_id);
    }
}

static void render_bottom_bar(WorldmapPanelState *panel, AnnotationSet *set) {
    const bool has_category = annotation_category_find(set, panel->current_category_id) != NULL;
    if (!tool_is_available(panel->active_tool, has_category)) set_active_tool(panel, WORLDMAP_TOOL_SELECT);
    for (int tool_index = 0; tool_index < WORLDMAP_TOOL_COUNT; tool_index++) {
        const WorldmapTool tool = (WorldmapTool) tool_index;
        const bool active = panel->active_tool == tool;
        if (tool_index > 0) ImGui::SameLine();
        ImGui::BeginDisabled(!tool_is_available(tool, has_category));
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(TOOL_NAMES[tool_index])) set_active_tool(panel, tool);
        if (active) ImGui::PopStyleColor();
        ImGui::EndDisabled();
    }

    render_edit_controls(panel, set);

    if (ImGui::GetTime() < panel->status_until) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(panel->status_text);
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

static void render_annotation_menu(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!ImGui::BeginPopup(ANNOTATION_MENU_ID)) return;
    if (ImGui::MenuItem("Delete")) delete_annotation(panel, set, panel->menu_annotation_id);
    ImGui::EndPopup();
}

static void render_category_menu(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!ImGui::BeginPopup(CATEGORY_MENU_ID)) return;
    if (ImGui::MenuItem("Export")) export_category(panel, set, panel->menu_category_id);
    if (ImGui::MenuItem("Delete category...")) panel->popup_request = DELETE_CATEGORY_POPUP_ID;
    ImGui::EndPopup();
}

static void commit_label(WorldmapPanelState *panel, AnnotationSet *set) {
    int added_id = 0;
    switch (panel->label_purpose) {
        case LABEL_PURPOSE_PIN:
        case LABEL_PURPOSE_TEXT:
            added_id =
                    annotation_add_point(set, panel->point_category_id,
                                         panel->label_purpose == LABEL_PURPOSE_PIN ? ANNOTATION_PIN : ANNOTATION_TEXT,
                                         panel->point_tile_x, panel->point_tile_y, panel->label_text);
            break;
        case LABEL_PURPOSE_PATH:
            added_id = annotation_add_path(set, panel->pending_category_id, panel->pending_segments,
                                           panel->pending_segment_count, panel->label_text);
            panel->pending_segment_count = 0;
            break;
        case LABEL_PURPOSE_NONE: return;
    }
    panel->label_purpose = LABEL_PURPOSE_NONE;
    if (added_id == 0) {
        set_status(panel, "Category is full.");
        return;
    }
    worldmap_save_annotations();
}

static void cancel_label(WorldmapPanelState *panel) {
    // A path finished by switching tools has nowhere to return to, so cancelling its label discards it.
    if (panel->label_purpose == LABEL_PURPOSE_PATH && panel->active_tool != WORLDMAP_TOOL_LINE) {
        panel->pending_segment_count = 0;
    }
    panel->label_purpose = LABEL_PURPOSE_NONE;
}

static void render_label_popup(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!ImGui::BeginPopupModal(LABEL_POPUP_ID, NULL, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (panel->label_purpose == LABEL_PURPOSE_NONE) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool submitted = ImGui::InputText("##label", panel->label_text, sizeof(panel->label_text),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    const bool can_confirm = panel->label_purpose != LABEL_PURPOSE_TEXT || panel->label_text[0] != '\0';

    ImGui::BeginDisabled(!can_confirm);
    const bool confirmed = ImGui::Button("OK");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel");

    if ((confirmed || submitted) && can_confirm) {
        commit_label(panel, set);
        ImGui::CloseCurrentPopup();
    } else if (cancelled) {
        cancel_label(panel);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void render_colour_picker(WorldmapPanelState *panel) {
    for (int colour_index = 0; colour_index < ANNOTATION_PALETTE_SIZE; colour_index++) {
        if (colour_index > 0) ImGui::SameLine();
        ImGui::PushID(colour_index);
        const ImVec4 swatch = ImGui::ColorConvertU32ToFloat4(palette_colour(colour_index));
        if (ImGui::ColorButton("##colour", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                               ImVec2(PICKER_SWATCH_SIZE, PICKER_SWATCH_SIZE))) {
            panel->new_category_colour = colour_index;
        }
        if (colour_index == panel->new_category_colour) {
            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(ImVec2(item_min.x - 2.0F, item_min.y - 2.0F),
                                                ImVec2(item_max.x + 2.0F, item_max.y + 2.0F), HIGHLIGHT_COLOUR, 0.0F, 0,
                                                2.0F);
        }
        ImGui::PopID();
    }
}

static void render_new_category_popup(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!ImGui::BeginPopupModal(NEW_CATEGORY_POPUP_ID, NULL, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextUnformatted("Name");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool submitted = ImGui::InputText("##name", panel->new_category_name, sizeof(panel->new_category_name),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    render_colour_picker(panel);
    const bool can_create = panel->new_category_name[0] != '\0';

    ImGui::BeginDisabled(!can_create);
    const bool created = ImGui::Button("Create");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel");

    if ((created || submitted) && can_create) {
        const int category_id = annotation_category_add(set, panel->new_category_name, panel->new_category_colour);
        if (category_id == 0) {
            set_status(panel, "Too many categories.");
        } else {
            panel->current_category_id = category_id;
            worldmap_save_annotations();
        }
        ImGui::CloseCurrentPopup();
    } else if (cancelled) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static int import_text_resize(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
    WorldmapPanelState *panel = (WorldmapPanelState *) data->UserData;
    char *grown = (char *) realloc(panel->import_text, (size_t) data->BufSize);
    if (!grown) {
        // Keeping the old capacity makes ImGui truncate the paste instead of writing past the buffer.
        data->BufSize = panel->import_capacity;
        return 0;
    }
    panel->import_text = grown;
    panel->import_capacity = data->BufSize;
    data->Buf = grown;
    return 0;
}

static void render_import_preview(const WorldmapPanelState *panel) {
    if (panel->import_text[0] == '\0') return;
    switch (panel->import_status) {
        case ANNOTATION_CODE_OK: {
            char preview[128];
            snprintf(preview, sizeof(preview), "%s: %d pins, %d labels, %d paths", panel->import_preview.name,
                     annotation_category_count_kind(&panel->import_preview, ANNOTATION_PIN),
                     annotation_category_count_kind(&panel->import_preview, ANNOTATION_TEXT),
                     annotation_category_count_kind(&panel->import_preview, ANNOTATION_PATH));
            ImGui::TextUnformatted(preview);
            break;
        }
        case ANNOTATION_CODE_NOT_A_CODE: ImGui::TextUnformatted("That is not a world map code."); break;
        case ANNOTATION_CODE_DAMAGED: ImGui::TextUnformatted("This code is damaged or incomplete."); break;
        case ANNOTATION_CODE_TOO_LARGE: ImGui::TextUnformatted("This code is too large to import."); break;
    }
}

static void render_import_popup(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!ImGui::BeginPopupModal(IMPORT_POPUP_ID, NULL, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (!panel->import_text) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted("Paste a world map code:");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputTextMultiline("##code", panel->import_text, (size_t) panel->import_capacity,
                                  ImVec2(420.0F, ImGui::GetTextLineHeight() * 6.0F), ImGuiInputTextFlags_CallbackResize,
                                  import_text_resize, panel)) {
        update_import_preview(panel);
    }
    render_import_preview(panel);

    const bool can_add = panel->import_text[0] != '\0' && panel->import_status == ANNOTATION_CODE_OK;
    ImGui::BeginDisabled(!can_add);
    const bool added = ImGui::Button("Add");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel");

    if (added && can_add) {
        const int category_id = annotation_category_adopt(set, &panel->import_preview);
        if (category_id == 0) {
            set_status(panel, "Too many categories.");
        } else {
            panel->current_category_id = category_id;
            worldmap_save_annotations();
        }
    }
    if (added || cancelled) {
        release_import(panel);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void render_export_popup(WorldmapPanelState *panel) {
    if (!ImGui::BeginPopupModal(EXPORT_POPUP_ID, NULL, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(panel->export_message);
    if (panel->export_too_long) {
        ImGui::TextUnformatted("Too long for one Discord message; it will still work on the wiki.");
    }
    if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static void render_delete_category_popup(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!ImGui::BeginPopupModal(DELETE_CATEGORY_POPUP_ID, NULL, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const AnnotationCategory *category = annotation_category_find(set, panel->menu_category_id);
    if (!category) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    char question[ANNOTATION_CATEGORY_NAME_MAX + 64];
    snprintf(question, sizeof(question), "Delete category \"%s\" and its %d annotations?", category->name,
             category->annotation_count);
    ImGui::TextUnformatted(question);

    if (ImGui::Button("Delete")) {
        delete_category(panel, set, panel->menu_category_id);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static void render_popups(WorldmapPanelState *panel, AnnotationSet *set) {
    if (panel->popup_request) {
        ImGui::OpenPopup(panel->popup_request);
        panel->popup_request = NULL;
    }
    render_annotation_menu(panel, set);
    render_category_menu(panel, set);
    render_label_popup(panel, set);
    render_new_category_popup(panel, set);
    render_import_popup(panel, set);
    render_export_popup(panel);
    render_delete_category_popup(panel, set);
}

static void discard_unfinished_work(WorldmapPanelState *panel) {
    cancel_edit(panel);
    panel->pending_segment_count = 0;
    panel->label_purpose = LABEL_PURPOSE_NONE;
    if (panel->popup_request == LABEL_POPUP_ID) panel->popup_request = NULL;
    panel->left_press_active = false;
    panel->right_press_active = false;
}

void worldmap_window_render(void) {
    static int last_rendered_frame = -1;
    WorldmapPanelState *panel = &panel_state;
    AnnotationSet *set = worldmap_annotations();
    bool open = true;

    // A gap in rendered frames means the window was hidden (e.g. by its hotkey) since the last render.
    const int frame = ImGui::GetFrameCount();
    if (last_rendered_frame >= 0 && frame > last_rendered_frame + 1) discard_unfinished_work(panel);
    last_rendered_frame = frame;

    panel->hovered_tile_valid = false;
    ImGui::SetNextWindowSize(ImVec2(900.0F, 620.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Map", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ensure_current_category(panel, set);
        handle_line_keys(panel);

        const float bar_height = ImGui::GetFrameHeightWithSpacing();
        const float body_height = fmaxf(ImGui::GetContentRegionAvail().y - bar_height, 1.0F);

        render_category_list(panel, set, body_height);
        ImGui::SameLine();
        render_map_area(panel, set, body_height);
        render_bottom_bar(panel, set);
        render_popups(panel, set);
    }
    ImGui::End();

    if (!open) {
        discard_unfinished_work(panel);
        game_ui_state.show_worldmap = false;
    }
}
