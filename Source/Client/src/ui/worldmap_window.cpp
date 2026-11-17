#include "worldmap_window.hpp"

#include <SDL2/SDL.h>
#include <imgui.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "game/annotation_code.h"
#include "game/annotation_import.h"
#include "game/annotations.h"
#include "game/game_ui.h"
#include "game/minimap.h"
#include "game/minimap_colours.h"
#include "game/minimap_overlay.h"
#include "game/worldmap.h"
#include "game/worldmap_view.h"
#include "graphics/sdl.h"
#include "ui_common.hpp"
#include "widgets.hpp"
#include "worldmap_help.hpp"

enum WorldmapTool {
    WORLDMAP_TOOL_SELECT,
    WORLDMAP_TOOL_PAN,
    WORLDMAP_TOOL_PIN,
    WORLDMAP_TOOL_LINE,
    WORLDMAP_TOOL_TEXT,
    WORLDMAP_TOOL_COUNT,
};

enum LabelPurpose { LABEL_PURPOSE_NONE, LABEL_PURPOSE_PIN, LABEL_PURPOSE_TEXT, LABEL_PURPOSE_PATH };

struct WorldmapPanelState {
    WorldmapView view;
    int current_category_id;
    int selected_annotation_id;
    int expanded_category_ids[ANNOTATION_MAX_CATEGORIES];
    int expanded_count;
    WorldmapTool active_tool;
    bool window_focused;
    bool follow_player;
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
    bool dismiss_popups;
    LabelPurpose label_purpose;
    char label_text[ANNOTATION_LABEL_MAX];
    int point_category_id;
    int point_tile_x;
    int point_tile_y;
    int menu_annotation_id;
    int menu_segment_index;
    int menu_category_id;
    ImVec2 title_grab_offset;
    int help_page;
    char new_category_name[ANNOTATION_CATEGORY_NAME_MAX];
    int new_category_colour;
    int new_category_parent_id;
    int rename_category_id;
    char rename_text[ANNOTATION_CATEGORY_NAME_MAX];
    bool rename_focus_pending;
    char *import_text;
    int import_capacity;
    AnnotationSet import_preview;
    AnnotationCodeStatus import_status;
    int import_conflict_id;
    char import_root_name[ANNOTATION_CATEGORY_NAME_MAX];
    char import_error[ANNOTATION_CATEGORY_NAME_MAX + 64];
    char export_message[64];
    bool export_too_long;
    char status_text[192];
    double status_until;
};

struct MapArea {
    ImVec2 origin;
    ImVec2 size;
};

static const float CATEGORY_COLUMN_WIDTH = 220.0F;
static const double CENTRE_ON_PLAYER_PIXELS_PER_TILE = 8.0;
static const double ZOOM_STEP = 1.25;
static const int PIN_DIAMOND_RADIUS = 3;
static const int PIN_SELECTION_RADIUS = 6;
static const float PIN_LABEL_GAP = 4.0F;
static const double PIN_LABEL_MIN_PIXELS_PER_TILE = 2.0;
static const double PIN_HOVER_RADIUS = 8.0;
static const double SELECT_RADIUS = 8.0;
static const double SNAP_PIXELS = 8.0;
static const float PATH_THICKNESS = 2.0F;
static const float PATH_EDGE_THICKNESS = 4.0F;
static const float CHEVRON_SPACING = 48.0F;
static const float CHEVRON_SIZE = 3.0F;
static const float TEXT_PLATE_PADDING_X = 4.0F;
static const float TEXT_PLATE_PADDING_Y = 2.0F;
static const float FOLLOW_RING_RADIUS = 5.0F;
static const float DASH_LENGTH = 6.0F;
static const float DASH_GAP = 4.0F;
static const float SWATCH_SIZE = 12.0F;
static const float DIMMED_SWATCH_ALPHA = 0.35F;
static const float TITLE_HEIGHT = 20.0F;
static const int CLOSE_CROSS_SIZE = 7;
static const ImU32 CLOSE_CROSS_HOVERED_COLOUR = IM_COL32(217, 191, 79, 255);
static const int RESIZE_GRIP_SIZE = 9;
static const ImU32 RESIZE_GRIP_COLOUR = IM_COL32(117, 106, 68, 255);
static const ImU32 RESIZE_GRIP_HOVERED_COLOUR = IM_COL32(168, 147, 90, 255);
static const float BUTTON_TEXT_PADDING = 8.0F;
static const char *const WINDOW_TITLE = "World Map";
static const float PICKER_SWATCH_SIZE = 20.0F;
static const float EDIT_LABEL_WIDTH = 180.0F;
static const double STATUS_SECONDS = 3.0;
static const double NOTICE_SECONDS = 12.0;
static const int IMPORT_INITIAL_CAPACITY = 4096;
static const float MINIMAP_SCREEN_LEFT = 6.0F;
static const float MINIMAP_SCREEN_TOP = 582.0F;
static const float MINIMAP_SCREEN_SIZE = 128.0F;
static const ImU32 OUTLINE_COLOUR = IM_COL32(0, 0, 0, 255);
static const ImU32 BACKGROUND_COLOUR = IM_COL32(0, 0, 0, 255);
static const ImU32 HIGHLIGHT_COLOUR = IM_COL32(255, 255, 255, 255);
static const ImU32 SHADOW_COLOUR = IM_COL32(0, 0, 0, 255);
static const ImU32 PATH_EDGE_COLOUR = IM_COL32(0, 0, 0, 200);
static const ImU32 TEXT_PLATE_COLOUR = IM_COL32(17, 5, 5, 200);
static const char *const TOOL_NAMES[WORLDMAP_TOOL_COUNT] = {"Select", "Pan", "Pin", "Line", "Text"};

static const char *const ANNOTATION_MENU_ID = "annotation_menu";
static const char *const CATEGORY_MENU_ID = "category_menu";
static const char *const LABEL_POPUP_ID = "Label###label_popup";
static const char *const NEW_CATEGORY_POPUP_ID = "New category###new_category_popup";
static const char *const IMPORT_POPUP_ID = "Import###import_popup";
static const char *const EXPORT_POPUP_ID = "Export###export_popup";
static const char *const DELETE_CATEGORY_POPUP_ID = "Delete category###delete_category_popup";
static const char *const DELETE_ANNOTATION_POPUP_ID = "Delete annotation###delete_annotation_popup";
static const char *const HELP_POPUP_ID = "World Map Help###help_popup";

static WorldmapPanelState initial_panel_state(void) {
    WorldmapPanelState state = {};
    state.view.centre_tile_x = WORLDMAP_TILES_DOWN / 2.0;
    state.view.centre_tile_y = WORLDMAP_TILES_ACROSS / 2.0;
    state.active_tool = WORLDMAP_TOOL_SELECT;
    annotation_set_init(&state.import_preview);
    return state;
}

static WorldmapPanelState panel_state = initial_panel_state();

static ImU32 rgb_colour(unsigned int rgb) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

static ImU32 palette_colour(int colour_index) {
    return rgb_colour(annotation_palette_rgb(colour_index));
}

static ImU32 category_colour(const AnnotationCategory *category) {
    return palette_colour(category->colour_index);
}

static ImU32 category_colour_by_id(AnnotationSet *set, int category_id) {
    const AnnotationCategory *category = annotation_category_find(set, category_id);
    return category ? category_colour(category) : HIGHLIGHT_COLOUR;
}

static void set_status_for(WorldmapPanelState *panel, const char *text, double seconds) {
    annotation_copy_text(panel->status_text, sizeof(panel->status_text), text);
    panel->status_until = ImGui::GetTime() + seconds;
}

static void set_status(WorldmapPanelState *panel, const char *text) {
    set_status_for(panel, text, STATUS_SECONDS);
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
    panel->follow_player = false;
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

static void remove_segment(Annotation *path, int segment_index) {
    memmove(&path->segments[segment_index], &path->segments[segment_index + 1],
            sizeof(AnnotationSegment) * (size_t) (path->segment_count - segment_index - 1));
    path->segment_count--;
}

static void delete_path_segment(WorldmapPanelState *panel, AnnotationSet *set, int annotation_id, int segment_index) {
    const Annotation *stored = annotation_find(set, annotation_id, NULL);
    if (!stored || stored->kind != ANNOTATION_PATH || segment_index < 0 || segment_index >= stored->segment_count) {
        return;
    }
    if (stored->segment_count == 1) {
        delete_annotation(panel, set, annotation_id);
        return;
    }
    Annotation trimmed;
    if (!annotation_copy(&trimmed, stored)) return;
    remove_segment(&trimmed, segment_index);
    if (annotation_replace(set, annotation_id, &trimmed)) worldmap_save_annotations();
    annotation_release(&trimmed);
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
    if (panel->rename_category_id == category_id) panel->rename_category_id = 0;
    set_category_expanded(panel, category_id, false);
    if (annotation_category_remove(set, category_id)) worldmap_save_annotations();
}

static void export_category(WorldmapPanelState *panel, AnnotationSet *set, int category_id) {
    if (!annotation_category_find(set, category_id)) return;
    char *code = annotation_code_export(set, category_id);
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
    annotation_set_release(&panel->import_preview);
    annotation_set_init(&panel->import_preview);
    panel->import_error[0] = '\0';
}

static void update_import_preview(WorldmapPanelState *panel, const AnnotationSet *set) {
    annotation_set_release(&panel->import_preview);
    annotation_set_init(&panel->import_preview);
    panel->import_status = annotation_code_import(panel->import_text, &panel->import_preview);
    panel->import_conflict_id = 0;
    panel->import_root_name[0] = '\0';
    panel->import_error[0] = '\0';
    if (panel->import_status != ANNOTATION_CODE_OK || panel->import_preview.category_count < 1) return;
    panel->import_conflict_id = annotation_import_conflict(set, &panel->import_preview);
    annotation_copy_text(panel->import_root_name, sizeof(panel->import_root_name),
                         panel->import_preview.categories[0].name);
}

static void open_import(WorldmapPanelState *panel, const AnnotationSet *set) {
    release_import(panel);

    char *clipboard = SDL_GetClipboardText();
    const char *trimmed = clipboard ? clipboard : "";
    while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\r' || *trimmed == '\n') trimmed++;
    const bool is_code = annotation_code_has_prefix(trimmed) != 0;
    const int wanted_capacity = is_code ? (int) strlen(trimmed) + 1 : 0;

    panel->import_capacity = wanted_capacity > IMPORT_INITIAL_CAPACITY ? wanted_capacity : IMPORT_INITIAL_CAPACITY;
    panel->import_text = (char *) malloc((size_t) panel->import_capacity);
    if (panel->import_text) {
        annotation_copy_text(panel->import_text, (size_t) panel->import_capacity, is_code ? trimmed : "");
        update_import_preview(panel, set);
        panel->popup_request = IMPORT_POPUP_ID;
    } else {
        panel->import_capacity = 0;
    }
    SDL_free(clipboard);
}

static void open_new_category(WorldmapPanelState *panel, int parent_id) {
    panel->new_category_name[0] = '\0';
    panel->new_category_colour = 0;
    panel->new_category_parent_id = parent_id;
    panel->popup_request = NEW_CATEGORY_POPUP_ID;
}

static void begin_rename(WorldmapPanelState *panel, const AnnotationCategory *category) {
    panel->rename_category_id = category->id;
    annotation_copy_text(panel->rename_text, sizeof(panel->rename_text), category->name);
    panel->rename_focus_pending = true;
}

static void expand_with_ancestors(WorldmapPanelState *panel, AnnotationSet *set, int category_id) {
    for (int depth = 0; category_id != 0 && depth <= ANNOTATION_MAX_DEPTH; depth++) {
        const AnnotationCategory *category = annotation_category_find(set, category_id);
        if (!category) return;
        set_category_expanded(panel, category_id, true);
        category_id = category->parent_id;
    }
}

static void move_annotation(WorldmapPanelState *panel, AnnotationSet *set, int annotation_id, int category_id) {
    if (!annotation_move(set, annotation_id, category_id)) {
        set_status(panel, "That category is full.");
        return;
    }
    if (panel->editing && panel->edit_annotation_id == annotation_id) cancel_edit(panel);
    expand_with_ancestors(panel, set, category_id);
    worldmap_save_annotations();
}

static void move_category(WorldmapPanelState *panel, AnnotationSet *set, int category_id, int parent_id) {
    if (!annotation_category_move(set, category_id, parent_id)) return;
    expand_with_ancestors(panel, set, parent_id);
    worldmap_save_annotations();
}

static void set_annotation_colour(WorldmapPanelState *panel, AnnotationSet *set, int annotation_id, int colour_index) {
    if (!annotation_set_colour(set, annotation_id, colour_index)) return;
    if (panel->editing && panel->edit_annotation_id == annotation_id) panel->edit_copy.colour_index = colour_index;
    worldmap_save_annotations();
}

static void draw_pixel(ImDrawList *draw_list, float left, float top, ImU32 colour) {
    draw_list->AddRectFilled(ImVec2(left, top), ImVec2(left + 1.0F, top + 1.0F), colour);
}

static void fill_square(ImDrawList *draw_list, ImVec2 centre, int radius, ImU32 colour) {
    draw_list->AddRectFilled(ImVec2(centre.x - (float) radius, centre.y - (float) radius),
                             ImVec2(centre.x + (float) radius + 1.0F, centre.y + (float) radius + 1.0F), colour);
}

static ImVec2 snap_to_pixel(ImVec2 point) {
    return ImVec2(floorf(point.x), floorf(point.y));
}

static void draw_diamond(ImDrawList *draw_list, ImVec2 centre, int radius, ImU32 colour) {
    for (int row = -radius; row <= radius; row++) {
        const float half_width = (float) (radius - abs(row));
        draw_list->AddRectFilled(ImVec2(centre.x - half_width, centre.y + (float) row),
                                 ImVec2(centre.x + half_width + 1.0F, centre.y + (float) row + 1.0F), colour);
    }
}

static void draw_dotted_diamond(ImDrawList *draw_list, ImVec2 centre, int radius, ImU32 colour) {
    for (int row = -radius; row <= radius; row += 2) {
        const float half_width = (float) (radius - abs(row));
        draw_pixel(draw_list, centre.x - half_width, centre.y + (float) row, colour);
        draw_pixel(draw_list, centre.x + half_width, centre.y + (float) row, colour);
    }
}

static void draw_dotted_rect(ImDrawList *draw_list, ImVec2 rect_min, ImVec2 rect_max, ImU32 colour) {
    const int width = (int) ceilf(rect_max.x - rect_min.x);
    const int height = (int) ceilf(rect_max.y - rect_min.y);
    for (int column_offset = 0; column_offset < width; column_offset += 2) {
        const float column = rect_min.x + (float) column_offset;
        draw_pixel(draw_list, column, rect_min.y, colour);
        draw_pixel(draw_list, column, rect_max.y - 1.0F, colour);
    }
    for (int row_offset = 0; row_offset < height; row_offset += 2) {
        const float row = rect_min.y + (float) row_offset;
        draw_pixel(draw_list, rect_min.x, row, colour);
        draw_pixel(draw_list, rect_max.x - 1.0F, row, colour);
    }
}

static void draw_shadowed_text(ImDrawList *draw_list, ImVec2 position, ImU32 colour, const char *text) {
    draw_list->AddText(ImVec2(position.x + 1.0F, position.y + 1.0F), SHADOW_COLOUR, text);
    draw_list->AddText(position, colour, text);
}

static void draw_dashed_line(ImDrawList *draw_list, ImVec2 start, ImVec2 end, ImU32 colour, float thickness) {
    const float delta_x = end.x - start.x;
    const float delta_y = end.y - start.y;
    const float length = sqrtf(delta_x * delta_x + delta_y * delta_y);
    if (length < 1.0F) return;
    const float dash_step = DASH_LENGTH + DASH_GAP;
    const int dash_count = (int) ceilf(length / dash_step);
    for (int dash_index = 0; dash_index < dash_count; dash_index++) {
        const float distance = (float) dash_index * dash_step;
        const float dash_end = fminf(distance + DASH_LENGTH, length);
        draw_list->AddLine(ImVec2(start.x + delta_x * distance / length, start.y + delta_y * distance / length),
                           ImVec2(start.x + delta_x * dash_end / length, start.y + delta_y * dash_end / length), colour,
                           thickness);
    }
}

static void draw_chevrons(ImDrawList *draw_list, ImVec2 start, ImVec2 end, ImU32 colour) {
    const float delta_x = end.x - start.x;
    const float delta_y = end.y - start.y;
    const float length = sqrtf(delta_x * delta_x + delta_y * delta_y);
    if (length < CHEVRON_SPACING) return;
    const float along_x = delta_x / length;
    const float along_y = delta_y / length;
    const float first_distance = CHEVRON_SPACING / 2.0F;
    const int chevron_count = (int) ceilf((length - CHEVRON_SIZE - first_distance) / CHEVRON_SPACING);
    for (int chevron_index = 0; chevron_index < chevron_count; chevron_index++) {
        const float distance = first_distance + (float) chevron_index * CHEVRON_SPACING;
        const ImVec2 middle(start.x + along_x * distance, start.y + along_y * distance);
        const ImVec2 points[3] = {
            ImVec2(middle.x - along_x * CHEVRON_SIZE - along_y * CHEVRON_SIZE,
                   middle.y - along_y * CHEVRON_SIZE + along_x * CHEVRON_SIZE),
            ImVec2(middle.x + along_x * CHEVRON_SIZE, middle.y + along_y * CHEVRON_SIZE),
            ImVec2(middle.x - along_x * CHEVRON_SIZE + along_y * CHEVRON_SIZE,
                   middle.y - along_y * CHEVRON_SIZE - along_x * CHEVRON_SIZE),
        };
        draw_list->AddPolyline(points, 3, PATH_EDGE_COLOUR, ImDrawFlags_None, 3.0F);
        draw_list->AddPolyline(points, 3, colour, ImDrawFlags_None, 1.0F);
    }
}

static bool line_misses_area(const MapArea *area, ImVec2 start, ImVec2 end) {
    const float margin = PATH_THICKNESS + 2.0F;
    return fmaxf(start.x, end.x) < area->origin.x - margin ||
           fminf(start.x, end.x) > area->origin.x + area->size.x + margin ||
           fmaxf(start.y, end.y) < area->origin.y - margin ||
           fminf(start.y, end.y) > area->origin.y + area->size.y + margin;
}

static void draw_path(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list,
                      const Annotation *annotation, ImU32 colour, bool selected) {
    if (annotation->segment_count == 0) return;
    // Edges for every segment go down first so joins between segments stay clean
    for (int pass = 0; pass < 3; pass++) {
        for (int segment_index = 0; segment_index < annotation->segment_count; segment_index++) {
            const AnnotationSegment *segment = &annotation->segments[segment_index];
            const ImVec2 start = snap_to_pixel(tile_centre_to_screen(panel, area, segment->start_x, segment->start_y));
            const ImVec2 end = snap_to_pixel(tile_centre_to_screen(panel, area, segment->end_x, segment->end_y));
            if (line_misses_area(area, start, end)) continue;
            if (pass == 0 && selected) {
                draw_dashed_line(draw_list, start, end, GOLD_FONT_COLOR, PATH_EDGE_THICKNESS + 2.0F);
            } else if (pass == 1) {
                draw_list->AddLine(start, end, PATH_EDGE_COLOUR, PATH_EDGE_THICKNESS);
            } else if (pass == 2) {
                draw_list->AddLine(start, end, colour, PATH_THICKNESS);
                draw_chevrons(draw_list, start, end, colour);
            }
        }
    }

    const AnnotationSegment *first = &annotation->segments[0];
    const AnnotationSegment *last = &annotation->segments[annotation->segment_count - 1];
    const ImVec2 start_mark = snap_to_pixel(tile_centre_to_screen(panel, area, first->start_x, first->start_y));
    const ImVec2 end_mark = snap_to_pixel(tile_centre_to_screen(panel, area, last->end_x, last->end_y));
    fill_square(draw_list, start_mark, 3, PATH_EDGE_COLOUR);
    fill_square(draw_list, start_mark, 2, colour);
    fill_square(draw_list, start_mark, 1, SHADOW_COLOUR);
    fill_square(draw_list, end_mark, 3, PATH_EDGE_COLOUR);
    fill_square(draw_list, end_mark, 2, colour);
}

static bool pin_label_shown(const WorldmapPanelState *panel, const AnnotationSet *set,
                            const AnnotationCategory *category, const Annotation *annotation) {
    return annotation_category_labels_shown(set, category) && annotation->label[0] != '\0' &&
           panel->view.pixels_per_tile >= PIN_LABEL_MIN_PIXELS_PER_TILE;
}

static void draw_pin(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list,
                     const Annotation *annotation, ImU32 colour, bool selected, bool show_label) {
    const ImVec2 centre = snap_to_pixel(tile_centre_to_screen(panel, area, annotation->tile_x, annotation->tile_y));
    if (selected) draw_dotted_diamond(draw_list, centre, PIN_SELECTION_RADIUS, GOLD_FONT_COLOR);
    draw_diamond(draw_list, centre, PIN_DIAMOND_RADIUS + 1, OUTLINE_COLOUR);
    draw_diamond(draw_list, centre, PIN_DIAMOND_RADIUS, colour);
    if (show_label) {
        const ImVec2 text_size = ImGui::CalcTextSize(annotation->label);
        const ImVec2 position(centre.x + (float) (PIN_DIAMOND_RADIUS + 1) + PIN_LABEL_GAP,
                              floorf(centre.y - text_size.y / 2.0F));
        draw_shadowed_text(draw_list, position, colour, annotation->label);
    }
}

static void draw_text_label(const WorldmapPanelState *panel, const MapArea *area, ImDrawList *draw_list,
                            const Annotation *annotation, ImU32 colour, bool selected) {
    const ImVec2 anchor = tile_centre_to_screen(panel, area, annotation->tile_x, annotation->tile_y);
    const ImVec2 text_size = ImGui::CalcTextSize(annotation->label);
    const ImVec2 position(floorf(anchor.x - text_size.x / 2.0F), floorf(anchor.y - text_size.y / 2.0F));
    const ImVec2 plate_min(position.x - TEXT_PLATE_PADDING_X, position.y - TEXT_PLATE_PADDING_Y);
    const ImVec2 plate_max(position.x + floorf(text_size.x) + TEXT_PLATE_PADDING_X,
                           position.y + floorf(text_size.y) + TEXT_PLATE_PADDING_Y);
    draw_list->AddRectFilled(plate_min, plate_max, TEXT_PLATE_COLOUR);
    if (selected) draw_dotted_rect(draw_list, plate_min, plate_max, GOLD_FONT_COLOR);
    draw_shadowed_text(draw_list, position, colour, annotation->label);
}

static void draw_annotations(const WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area,
                             ImDrawList *draw_list) {
    for (int category_index = 0; category_index < set->category_count; category_index++) {
        const AnnotationCategory *category = &set->categories[category_index];
        if (!annotation_category_shown(set, category)) continue;
        for (int annotation_index = 0; annotation_index < category->annotation_count; annotation_index++) {
            const Annotation *annotation = &category->annotations[annotation_index];
            const bool selected = annotation->id == panel->selected_annotation_id;
            if (panel->editing && annotation->id == panel->edit_annotation_id) annotation = &panel->edit_copy;
            const ImU32 colour = rgb_colour(annotation_effective_rgb(category, annotation));
            switch (annotation->kind) {
                case ANNOTATION_PATH: draw_path(panel, area, draw_list, annotation, colour, selected); break;
                case ANNOTATION_PIN:
                    draw_pin(panel, area, draw_list, annotation, colour, selected,
                             pin_label_shown(panel, set, category, annotation));
                    break;
                case ANNOTATION_TEXT: draw_text_label(panel, area, draw_list, annotation, colour, selected); break;
                case ANNOTATION_NONE: break;
            }
        }
    }
}

static void draw_pending_path(const WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area,
                              ImDrawList *draw_list) {
    const ImU32 colour = category_colour_by_id(set, panel->pending_category_id);
    for (int segment_index = 0; segment_index < panel->pending_segment_count; segment_index++) {
        const AnnotationSegment *segment = &panel->pending_segments[segment_index];
        const ImVec2 start = tile_centre_to_screen(panel, area, segment->start_x, segment->start_y);
        const ImVec2 end = tile_centre_to_screen(panel, area, segment->end_x, segment->end_y);
        if (line_misses_area(area, start, end)) continue;
        draw_dashed_line(draw_list, start, end, colour, PATH_THICKNESS);
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

    int category_id = panel->pending_segment_count > 0 ? panel->pending_category_id : panel->current_category_id;
    if (panel->editing) {
        AnnotationCategory *owner = NULL;
        if (annotation_find(set, panel->edit_annotation_id, &owner)) category_id = owner->id;
    }
    int mouse_tile_x, mouse_tile_y;
    mouse_tile(panel, area, &mouse_tile_x, &mouse_tile_y);
    draw_dashed_line(draw_list, tile_centre_to_screen(panel, area, panel->press_tile_x, panel->press_tile_y),
                     tile_centre_to_screen(panel, area, mouse_tile_x, mouse_tile_y),
                     category_colour_by_id(set, category_id), PATH_THICKNESS);
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
    const ImVec2 centre = snap_to_pixel(tile_centre_to_screen(panel, area, player_tile_x, player_tile_y));
    if (panel->follow_player) {
        draw_list->AddCircle(ImVec2(centre.x + 0.5F, centre.y + 0.5F), FOLLOW_RING_RADIUS, GOLD_FONT_COLOR, 16, 1.0F);
    }
    fill_square(draw_list, centre, 2, OUTLINE_COLOUR);
    fill_square(draw_list, centre, 1, HIGHLIGHT_COLOUR);
}

static void move_view_to_player(WorldmapPanelState *panel) {
    int player_tile_x, player_tile_y;
    engine_player_tile(&player_tile_x, &player_tile_y);
    panel->view.centre_tile_x = player_tile_x + 0.5;
    panel->view.centre_tile_y = player_tile_y + 0.5;
}

static void centre_on_player(WorldmapPanelState *panel) {
    move_view_to_player(panel);
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

static bool category_has_segment_room(AnnotationSet *set, int category_id, int extra_segments) {
    const AnnotationCategory *category = annotation_category_find(set, category_id);
    if (!category) return true;
    return annotation_category_segment_total(category) + extra_segments <= ANNOTATION_MAX_SEGMENTS_PER_CATEGORY;
}

static void add_pending_segment(WorldmapPanelState *panel, AnnotationSet *set, int end_x, int end_y) {
    if (panel->press_tile_x == end_x && panel->press_tile_y == end_y) return;
    if (panel->pending_segment_count >= ANNOTATION_MAX_SEGMENTS) {
        set_status(panel, "Path is full.");
        return;
    }
    if (panel->pending_segment_count == 0) panel->pending_category_id = panel->current_category_id;
    if (!category_has_segment_room(set, panel->pending_category_id, panel->pending_segment_count + 1)) {
        set_status(panel, "Category is full.");
        return;
    }
    if (append_segment(&panel->pending_segments[panel->pending_segment_count], panel->press_tile_x, panel->press_tile_y,
                       end_x, end_y)) {
        panel->pending_segment_count++;
    }
}

static void add_edit_segment(WorldmapPanelState *panel, AnnotationSet *set, int end_x, int end_y) {
    Annotation *edited = &panel->edit_copy;
    if (panel->press_tile_x == end_x && panel->press_tile_y == end_y) return;
    if (edited->segment_count >= ANNOTATION_MAX_SEGMENTS) {
        set_status(panel, "Path is full.");
        return;
    }
    AnnotationCategory *owner = NULL;
    const Annotation *stored = annotation_find(set, panel->edit_annotation_id, &owner);
    if (stored && !category_has_segment_room(set, owner->id, edited->segment_count + 1 - stored->segment_count)) {
        set_status(panel, "Category is full.");
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
    remove_segment(edited, segment_index);
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
        if (editing_path && panel->press_inside_world) add_edit_segment(panel, set, release_x, release_y);
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
            if (panel->press_inside_world) add_pending_segment(panel, set, release_x, release_y);
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
    panel->menu_segment_index = hit.segment_index;
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
            panel->follow_player = false;
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

static bool game_button(const char *text, bool enabled = true, bool pressed = false) {
    const float width = fmaxf((float) BUTTON_SPRITE_WIDTH, ImGui::CalcTextSize(text).x + BUTTON_TEXT_PADDING * 2.0F);
    return ui_game_button(text, text, width, (float) BUTTON_SPRITE_HEIGHT, enabled, pressed);
}

static void frame_current_window(void) {
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    ui_game_frame(position.x, position.y, position.x + size.x, position.y + size.y);
}

// ImGui keeps popups open while their window is hidden, so popups left open when the map closed are shut here
static bool close_if_dismissed(void) {
    if (!panel_state.dismiss_popups) return false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return true;
}

static bool begin_game_menu(const char *popup_id) {
    if (!ImGui::BeginPopup(popup_id) || close_if_dismissed()) return false;
    frame_current_window();
    return true;
}

static bool begin_game_modal(const char *popup_id) {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::BeginPopupModal(popup_id, NULL, flags) || close_if_dismissed()) return false;
    frame_current_window();
    const char *title_end = strstr(popup_id, "###");
    ImGui::TextUnformatted(popup_id, title_end);
    ImGui::Separator();
    return true;
}

static void show_pin_tooltip(const WorldmapPanelState *panel, AnnotationSet *set, const MapArea *area) {
    if (!ImGui::IsItemHovered()) return;
    const WorldmapHit hit = hit_test_at_mouse(panel, set, area, PIN_HOVER_RADIUS);
    if (hit.annotation_id == 0) return;
    AnnotationCategory *owner = NULL;
    const Annotation *annotation = annotation_find(set, hit.annotation_id, &owner);
    if (annotation && owner && annotation->kind == ANNOTATION_PIN && annotation->label[0] != '\0' &&
        !pin_label_shown(panel, set, owner, annotation)) {
        ui_tooltip(annotation->label);
    }
}

static void render_map_area(WorldmapPanelState *panel, AnnotationSet *set, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::BeginChild("map", ImVec2(0.0F, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    frame_current_window();

    const float inset = (float) WINDOW_SPRITE_BORDER_WIDTH;
    const ImVec2 child_origin = ImGui::GetCursorScreenPos();
    const ImVec2 child_size = ImGui::GetContentRegionAvail();
    MapArea area = {
        ImVec2(child_origin.x + inset, child_origin.y + inset),
        ImVec2(child_size.x - inset * 2.0F, child_size.y - inset * 2.0F),
    };
    if (area.size.x >= 1.0F && area.size.y >= 1.0F) {
        ImGui::SetCursorScreenPos(area.origin);
        if (game_ui_state.worldmap_centre_on_player) {
            centre_on_player(panel);
            game_ui_state.worldmap_centre_on_player = false;
        }
        if (panel->follow_player) move_view_to_player(panel);
        worldmap_view_clamp(&panel->view, area.size.x, area.size.y);

        ImGui::InvisibleButton("canvas", area.size,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        handle_map_input(panel, set, &area);

        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const ImVec2 area_end(area.origin.x + area.size.x, area.origin.y + area.size.y);
        draw_list->PushClipRect(area.origin, area_end, true);
        const ImDrawListFlags saved_flags = draw_list->Flags;
        draw_list->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex |
                              ImDrawListFlags_AntiAliasedFill);
        draw_list->AddRectFilled(area.origin, area_end, BACKGROUND_COLOUR);
        draw_world_texture(panel, &area, draw_list);
        draw_annotations(panel, set, &area, draw_list);
        draw_pending_path(panel, set, &area, draw_list);
        draw_drag_preview(panel, set, &area, draw_list);
        draw_player_marker(panel, &area, draw_list);
        draw_list->Flags = saved_flags;
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
        case ANNOTATION_NONE: break;
    }
    return "";
}

static void render_swatch(ImU32 colour, bool dimmed) {
    ImVec4 swatch = ImGui::ColorConvertU32ToFloat4(colour);
    if (dimmed) swatch.w = DIMMED_SWATCH_ALPHA;
    ImGui::Dummy(ImVec2(SWATCH_SIZE, SWATCH_SIZE));
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                              ImGui::ColorConvertFloat4ToU32(swatch));
}

static void render_annotation_rows(WorldmapPanelState *panel, const AnnotationCategory *category, const bool dimmed) {
    for (int annotation_index = 0; annotation_index < category->annotation_count; annotation_index++) {
        const Annotation *annotation = &category->annotations[annotation_index];
        const bool selected = annotation->id == panel->selected_annotation_id;
        ImGui::PushID(annotation->id);
        if (dimmed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        const float row_start_x = ImGui::GetCursorPosX();
        if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap)) {
            select_annotation(panel, annotation->id);
            centre_on_annotation(panel, annotation);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            panel->menu_annotation_id = annotation->id;
            panel->menu_segment_index = -1;
            panel->popup_request = ANNOTATION_MENU_ID;
        }
        ImGui::SameLine(row_start_x);
        if (annotation->colour_index != ANNOTATION_COLOUR_INHERIT) {
            render_swatch(rgb_colour(annotation_effective_rgb(category, annotation)), dimmed);
        } else {
            ImGui::Dummy(ImVec2(SWATCH_SIZE, SWATCH_SIZE));
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(annotation_kind_prefix(annotation->kind));
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::TextUnformatted(annotation->label);
        if (dimmed) ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

static void render_rename_input(WorldmapPanelState *panel, AnnotationSet *set) {
    if (panel->rename_focus_pending) {
        ImGui::SetKeyboardFocusHere();
        panel->rename_focus_pending = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool submitted = ImGui::InputText("##rename", panel->rename_text, sizeof(panel->rename_text),
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    if (submitted) {
        if (annotation_category_rename(set, panel->rename_category_id, panel->rename_text)) {
            worldmap_save_annotations();
        }
        panel->rename_category_id = 0;
    } else if (ImGui::IsItemDeactivated()) {
        panel->rename_category_id = 0;
    }
}

static void render_category_branch(WorldmapPanelState *panel, AnnotationSet *set, int parent_id);

static void render_category_row(WorldmapPanelState *panel, AnnotationSet *set, AnnotationCategory *category) {
    ImGui::PushID(category->id);
    const bool dimmed = !annotation_category_shown(set, category);
    if (dimmed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    const float row_start_x = ImGui::GetCursorPosX();
    ImGuiTreeNodeFlags node_flags =
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowOverlap;
    if (category->id == panel->current_category_id) node_flags |= ImGuiTreeNodeFlags_Selected;
    ImGui::SetNextItemOpen(category_is_expanded(panel, category->id));
    const bool expanded = ImGui::TreeNodeEx("##category", node_flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) panel->current_category_id = category->id;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        panel->menu_category_id = category->id;
        panel->popup_request = CATEGORY_MENU_ID;
    }
    set_category_expanded(panel, category->id, expanded);

    ImGui::SameLine(row_start_x + ImGui::GetTreeNodeToLabelSpacing());
    render_swatch(category_colour(category), dimmed);
    ImGui::SameLine();
    if (panel->rename_category_id == category->id) {
        render_rename_input(panel, set);
    } else {
        ImGui::TextUnformatted(category->name);
    }
    if (dimmed) ImGui::PopStyleColor();

    if (expanded) {
        render_category_branch(panel, set, category->id);
        render_annotation_rows(panel, category, dimmed);
        ImGui::TreePop();
    }

    ImGui::PopID();
}

static void render_category_branch(WorldmapPanelState *panel, AnnotationSet *set, int parent_id) {
    for (int category_index = 0; category_index < set->category_count; category_index++) {
        AnnotationCategory *category = &set->categories[category_index];
        if (category->parent_id == parent_id && category->id != parent_id) render_category_row(panel, set, category);
    }
}

static void render_category_list(WorldmapPanelState *panel, AnnotationSet *set, float height) {
    ImGui::BeginChild("categories", ImVec2(CATEGORY_COLUMN_WIDTH, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    frame_current_window();

    const ImGuiStyle &style = ImGui::GetStyle();
    const float button_row_height = fmaxf((float) BUTTON_SPRITE_HEIGHT, ImGui::GetFrameHeight()) + style.ItemSpacing.y;
    const float footer_height = button_row_height + ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
    ImGui::BeginChild("category_rows", ImVec2(0.0F, -footer_height));
    render_category_branch(panel, set, 0);
    ImGui::EndChild();

    ImGui::Separator();
    if (game_button("+ New")) open_new_category(panel, 0);
    ImGui::SameLine();
    if (game_button("Import")) open_import(panel, set);
    ImGui::SameLine();
    if (game_button("Export", annotation_category_find(set, panel->current_category_id) != NULL)) {
        export_category(panel, set, panel->current_category_id);
    }

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
        if (game_button("Save", !empty_text)) save_edit(panel, set);
        ImGui::SameLine();
        if (game_button("Cancel")) cancel_edit(panel);
    }
}

static void render_bottom_bar(WorldmapPanelState *panel, AnnotationSet *set) {
    const bool has_category = annotation_category_find(set, panel->current_category_id) != NULL;
    if (!tool_is_available(panel->active_tool, has_category)) set_active_tool(panel, WORLDMAP_TOOL_SELECT);
    for (int tool_index = 0; tool_index < WORLDMAP_TOOL_COUNT; tool_index++) {
        const WorldmapTool tool = (WorldmapTool) tool_index;
        const bool active = panel->active_tool == tool;
        if (tool_index > 0) ImGui::SameLine();
        if (game_button(TOOL_NAMES[tool_index], tool_is_available(tool, has_category), active)) {
            set_active_tool(panel, tool);
        }
    }

    ImGui::SameLine();
    ImGui::Checkbox("Follow player", &panel->follow_player);

    render_edit_controls(panel, set);

    if (ImGui::GetTime() < panel->status_until) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(panel->status_text);
    }

    if (panel->hovered_tile_valid) {
        const MinimapColour colour =
                minimap_tile_colours()[panel->hovered_tile_x * WORLDMAP_TILES_ACROSS + panel->hovered_tile_y];
        const MinimapRgb rgb = minimap_colour_rgb(colour);
        char coordinates[64];
        snprintf(coordinates, sizeof(coordinates), "RGB %d,%d,%d   %d, %d", rgb.red, rgb.green, rgb.blue,
                 panel->hovered_tile_x, panel->hovered_tile_y);
        const float text_width = ImGui::CalcTextSize(coordinates).x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - text_width -
                             (float) RESIZE_GRIP_SIZE);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(coordinates);
    }
}

static void outline_last_item(void) {
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(ImVec2(item_min.x - 2.0F, item_min.y - 2.0F),
                                        ImVec2(item_max.x + 2.0F, item_max.y + 2.0F), GOLD_FONT_COLOR, 0.0F, 0, 2.0F);
}

static bool render_palette_row(int *colour_index) {
    bool chosen = false;
    for (int palette_index = 0; palette_index < ANNOTATION_PALETTE_SIZE; palette_index++) {
        if (palette_index > 0) ImGui::SameLine();
        ImGui::PushID(palette_index);
        const ImVec4 swatch = ImGui::ColorConvertU32ToFloat4(palette_colour(palette_index));
        if (ImGui::ColorButton("##colour", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                               ImVec2(PICKER_SWATCH_SIZE, PICKER_SWATCH_SIZE))) {
            *colour_index = palette_index;
            chosen = true;
        }
        if (palette_index == *colour_index) outline_last_item();
        ImGui::PopID();
    }
    return chosen;
}

static bool send_to_allowed(const AnnotationSet *set, int moving_category_id, int owner_category_id, int target_id) {
    if (moving_category_id != 0) return annotation_category_can_move(set, moving_category_id, target_id) != 0;
    return target_id != owner_category_id;
}

static void render_send_to_branch(const AnnotationSet *set, int parent_id, int moving_category_id,
                                  int owner_category_id, int *chosen_id) {
    for (int category_index = 0; category_index < set->category_count; category_index++) {
        const AnnotationCategory *category = &set->categories[category_index];
        if (category->parent_id != parent_id || category->id == parent_id) continue;
        ImGui::PushID(category->id);
        const bool allowed = send_to_allowed(set, moving_category_id, owner_category_id, category->id);
        if (ImGui::MenuItem(category->name, NULL, false, allowed)) *chosen_id = category->id;
        ImGui::Indent(ImGui::GetStyle().IndentSpacing);
        render_send_to_branch(set, category->id, moving_category_id, owner_category_id, chosen_id);
        ImGui::Unindent(ImGui::GetStyle().IndentSpacing);
        ImGui::PopID();
    }
}

static void render_annotation_menu(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!begin_game_menu(ANNOTATION_MENU_ID)) return;
    AnnotationCategory *owner = NULL;
    const Annotation *annotation = annotation_find(set, panel->menu_annotation_id, &owner);
    if (!annotation) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const int annotation_id = annotation->id;

    if (ImGui::MenuItem("Edit")) begin_edit(panel, set, annotation_id);
    if (ImGui::BeginMenu("Colour")) {
        int colour_index = annotation->colour_index;
        if (render_palette_row(&colour_index)) set_annotation_colour(panel, set, annotation_id, colour_index);
        if (ImGui::MenuItem("Use category colour", NULL, annotation->colour_index == ANNOTATION_COLOUR_INHERIT)) {
            set_annotation_colour(panel, set, annotation_id, ANNOTATION_COLOUR_INHERIT);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Send to")) {
        int chosen_id = -1;
        render_send_to_branch(set, 0, 0, owner->id, &chosen_id);
        ImGui::EndMenu();
        if (chosen_id > 0) move_annotation(panel, set, annotation_id, chosen_id);
    }
    ImGui::Separator();
    const bool editing_this = panel->editing && panel->edit_annotation_id == annotation_id;
    const bool segment_hit = annotation->kind == ANNOTATION_PATH && annotation->segment_count > 1 &&
                             panel->menu_segment_index >= 0 && !editing_this;
    if (segment_hit && ImGui::MenuItem("Delete segment")) {
        delete_path_segment(panel, set, annotation_id, panel->menu_segment_index);
    }
    if (ImGui::MenuItem("Delete...")) panel->popup_request = DELETE_ANNOTATION_POPUP_ID;
    ImGui::EndPopup();
}

static void render_category_menu(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!begin_game_menu(CATEGORY_MENU_ID)) return;
    AnnotationCategory *category = annotation_category_find(set, panel->menu_category_id);
    if (!category) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const int category_id = category->id;

    ImGui::TextDisabled("%s", category->name);
    ImGui::Separator();
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    if (ImGui::MenuItem("Show on map", NULL, category->visible != 0)) {
        category->visible = category->visible ? 0 : 1;
        set->changed = 1;
        worldmap_save_annotations();
    }
    if (ImGui::MenuItem("Show labels", NULL, category->show_labels != 0)) {
        category->show_labels = category->show_labels ? 0 : 1;
        set->changed = 1;
        worldmap_save_annotations();
    }
    ImGui::PopItemFlag();
    ImGui::Separator();

    const bool can_nest = annotation_category_depth(set, category_id) < ANNOTATION_MAX_DEPTH;
    if (ImGui::MenuItem("New subcategory...", NULL, false, can_nest)) open_new_category(panel, category_id);
    if (ImGui::MenuItem("Rename")) begin_rename(panel, category);
    if (ImGui::BeginMenu("Colour")) {
        int colour_index = category->colour_index;
        if (render_palette_row(&colour_index) && annotation_category_set_colour(set, category_id, colour_index)) {
            worldmap_save_annotations();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Send to")) {
        int chosen_id = -1;
        if (ImGui::MenuItem("Top level", NULL, false, annotation_category_can_move(set, category_id, 0) != 0)) {
            chosen_id = 0;
        }
        render_send_to_branch(set, 0, category_id, 0, &chosen_id);
        ImGui::EndMenu();
        if (chosen_id >= 0) move_category(panel, set, category_id, chosen_id);
    }
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    if (ImGui::MenuItem("Move up", NULL, false, annotation_category_can_shift(set, category_id, -1) != 0) &&
        annotation_category_shift(set, category_id, -1)) {
        worldmap_save_annotations();
    }
    if (ImGui::MenuItem("Move down", NULL, false, annotation_category_can_shift(set, category_id, 1) != 0) &&
        annotation_category_shift(set, category_id, 1)) {
        worldmap_save_annotations();
    }
    ImGui::PopItemFlag();
    ImGui::Separator();

    if (ImGui::MenuItem("Export")) export_category(panel, set, category_id);
    const bool has_children = annotation_category_has_children(set, category_id) != 0;
    if (ImGui::MenuItem("Delete category...", NULL, false, !has_children)) {
        panel->popup_request = DELETE_CATEGORY_POPUP_ID;
    }
    if (has_children && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ui_tooltip("Delete or move its subcategories first.");
    }
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
    if (!begin_game_modal(LABEL_POPUP_ID)) return;
    if (panel->label_purpose == LABEL_PURPOSE_NONE) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool submitted = ImGui::InputText("##label", panel->label_text, sizeof(panel->label_text),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    const bool can_confirm = panel->label_purpose != LABEL_PURPOSE_TEXT || panel->label_text[0] != '\0';

    const bool confirmed = game_button("OK", can_confirm);
    ImGui::SameLine();
    const bool cancelled = game_button("Cancel");

    if ((confirmed || submitted) && can_confirm) {
        commit_label(panel, set);
        ImGui::CloseCurrentPopup();
    } else if (cancelled) {
        cancel_label(panel);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void render_new_category_popup(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!begin_game_modal(NEW_CATEGORY_POPUP_ID)) return;

    ImGui::TextUnformatted("Name");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool submitted = ImGui::InputText("##name", panel->new_category_name, sizeof(panel->new_category_name),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    render_palette_row(&panel->new_category_colour);
    const bool can_create = panel->new_category_name[0] != '\0';

    const bool created = game_button("Create", can_create);
    ImGui::SameLine();
    const bool cancelled = game_button("Cancel");

    if ((created || submitted) && can_create) {
        const int parent_id = panel->new_category_parent_id;
        const int category_id =
                annotation_category_add_under(set, parent_id, panel->new_category_name, panel->new_category_colour);
        if (category_id == 0) {
            const bool too_deep = annotation_category_depth(set, parent_id) >= ANNOTATION_MAX_DEPTH;
            set_status(panel, too_deep ? "Categories can only nest 8 deep." : "Too many categories.");
        } else {
            panel->current_category_id = category_id;
            if (parent_id != 0) set_category_expanded(panel, parent_id, true);
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
            const AnnotationSet *imported = &panel->import_preview;
            int pin_count = 0, label_count = 0, path_count = 0;
            for (int category_index = 0; category_index < imported->category_count; category_index++) {
                const AnnotationCategory *category = &imported->categories[category_index];
                pin_count += annotation_category_count_kind(category, ANNOTATION_PIN);
                label_count += annotation_category_count_kind(category, ANNOTATION_TEXT);
                path_count += annotation_category_count_kind(category, ANNOTATION_PATH);
            }
            const char *root_name = imported->category_count > 0 ? imported->categories[0].name : "";
            const char *category_word = imported->category_count == 1 ? "category" : "categories";
            char preview[160];
            snprintf(preview, sizeof(preview), "%s: %d %s, %d pins, %d labels, %d paths", root_name,
                     imported->category_count, category_word, pin_count, label_count, path_count);
            ImGui::TextUnformatted(preview);
            break;
        }
        case ANNOTATION_CODE_NOT_A_CODE: ImGui::TextUnformatted("That is not a world map code."); break;
        case ANNOTATION_CODE_DAMAGED: ImGui::TextUnformatted("This code is damaged or incomplete."); break;
        case ANNOTATION_CODE_TOO_LARGE: ImGui::TextUnformatted("This code is too large to import."); break;
    }
}

static bool top_level_name_taken(const AnnotationSet *set, const char *name) {
    for (int category_index = 0; category_index < set->category_count; category_index++) {
        const AnnotationCategory *category = &set->categories[category_index];
        if (category->parent_id == 0 && annotation_name_equal(category->name, name)) return true;
    }
    return false;
}

static bool report_import_result(WorldmapPanelState *panel, const AnnotationImportResult result) {
    switch (result.status) {
        case ANNOTATION_IMPORT_OK:
            panel->current_category_id = result.root_id;
            set_category_expanded(panel, result.root_id, true);
            worldmap_save_annotations();
            return true;
        case ANNOTATION_IMPORT_NAME_TAKEN:
            annotation_copy_text(panel->import_error, sizeof(panel->import_error), "That name is already taken.");
            break;
        case ANNOTATION_IMPORT_TOO_MANY_CATEGORIES:
            annotation_copy_text(panel->import_error, sizeof(panel->import_error),
                                 "Import would go over the 256 category limit.");
            break;
        case ANNOTATION_IMPORT_TOO_DEEP:
            annotation_copy_text(panel->import_error, sizeof(panel->import_error),
                                 "Import would nest categories more than 8 deep.");
            break;
        case ANNOTATION_IMPORT_CATEGORY_FULL:
            snprintf(panel->import_error, sizeof(panel->import_error), "Import would overfill \"%s\".",
                     result.category_name);
            break;
        case ANNOTATION_IMPORT_FAILED:
            annotation_copy_text(panel->import_error, sizeof(panel->import_error), "Import failed.");
            break;
    }
    return false;
}

static void render_import_popup(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!begin_game_modal(IMPORT_POPUP_ID)) return;
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
        update_import_preview(panel, set);
    }
    render_import_preview(panel);

    const bool code_ready = panel->import_text[0] != '\0' && panel->import_status == ANNOTATION_CODE_OK;
    bool finished = false;
    if (code_ready && panel->import_conflict_id != 0) {
        char conflict_text[ANNOTATION_CATEGORY_NAME_MAX + 48];
        snprintf(conflict_text, sizeof(conflict_text), "You already have a category called \"%s\".",
                 panel->import_preview.categories[0].name);
        ImGui::TextUnformatted(conflict_text);
        if (game_button("Merge")) {
            finished = report_import_result(
                    panel, annotation_import_merge(set, &panel->import_preview, panel->import_conflict_id));
        }
        ImGui::TextUnformatted("Rename to");
        ImGui::SameLine();
        ImGui::InputText("##import_rename", panel->import_root_name, sizeof(panel->import_root_name));
        const bool can_rename =
                panel->import_root_name[0] != '\0' && !top_level_name_taken(set, panel->import_root_name);
        if (game_button("Add as new", can_rename) && can_rename) {
            finished = report_import_result(
                    panel, annotation_import_add(set, &panel->import_preview, panel->import_root_name));
        }
        ImGui::SameLine();
    } else {
        if (game_button("Add", code_ready) && code_ready) {
            finished = report_import_result(panel, annotation_import_add(set, &panel->import_preview, NULL));
        }
        ImGui::SameLine();
    }
    if (game_button("Cancel")) finished = true;
    if (panel->import_error[0] != '\0') ImGui::TextUnformatted(panel->import_error);

    if (finished) {
        release_import(panel);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void render_export_popup(WorldmapPanelState *panel) {
    if (!begin_game_modal(EXPORT_POPUP_ID)) return;
    ImGui::TextUnformatted(panel->export_message);
    if (panel->export_too_long) {
        ImGui::TextUnformatted("Too long for one Discord message; it will still work on the wiki.");
    }
    if (game_button("OK")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static void render_delete_category_popup(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!begin_game_modal(DELETE_CATEGORY_POPUP_ID)) return;
    const AnnotationCategory *category = annotation_category_find(set, panel->menu_category_id);
    if (!category || annotation_category_has_children(set, category->id)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    char question[ANNOTATION_CATEGORY_NAME_MAX + 64];
    snprintf(question, sizeof(question), "Delete category \"%s\" and its %d annotations?", category->name,
             category->annotation_count);
    ImGui::TextUnformatted(question);

    if (game_button("Delete")) {
        delete_category(panel, set, panel->menu_category_id);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (game_button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static const char *annotation_kind_noun(AnnotationKind kind) {
    switch (kind) {
        case ANNOTATION_PIN: return "pin";
        case ANNOTATION_TEXT: return "text label";
        case ANNOTATION_PATH: return "path";
        case ANNOTATION_NONE: break;
    }
    return "annotation";
}

static void render_delete_annotation_popup(WorldmapPanelState *panel, AnnotationSet *set) {
    if (!begin_game_modal(DELETE_ANNOTATION_POPUP_ID)) return;
    const Annotation *annotation = annotation_find(set, panel->menu_annotation_id, NULL);
    if (!annotation) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    char question[ANNOTATION_LABEL_MAX + 64];
    if (annotation->label[0] != '\0') {
        snprintf(question, sizeof(question), "Delete %s \"%s\"?", annotation_kind_noun(annotation->kind),
                 annotation->label);
    } else {
        snprintf(question, sizeof(question), "Delete this unnamed %s?", annotation_kind_noun(annotation->kind));
    }
    ImGui::TextUnformatted(question);

    if (game_button("Delete")) {
        delete_annotation(panel, set, panel->menu_annotation_id);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (game_button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static void render_help_popup(WorldmapPanelState *panel) {
    if (!begin_game_modal(HELP_POPUP_ID)) return;
    const int page_count = worldmap_help_page_count();
    panel->help_page = clamp_int(panel->help_page, 0, page_count - 1);
    worldmap_help_render_page(panel->help_page);

    if (game_button("Back", panel->help_page > 0)) panel->help_page--;
    ImGui::SameLine();
    if (game_button("Next", panel->help_page < page_count - 1)) panel->help_page++;
    ImGui::SameLine();
    char page_text[32];
    snprintf(page_text, sizeof(page_text), "%d / %d", panel->help_page + 1, page_count);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(page_text);
    ImGui::SameLine();
    if (game_button("Close")) ImGui::CloseCurrentPopup();
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
    render_delete_annotation_popup(panel, set);
    render_help_popup(panel);
}

static void drop_interrupted_work(WorldmapPanelState *panel) {
    cancel_edit(panel);
    // Pending Line segments outlive the panel closing; returning to the Line tool lets Enter still finish the path.
    if (panel->label_purpose == LABEL_PURPOSE_PATH && panel->pending_segment_count > 0) {
        panel->active_tool = WORLDMAP_TOOL_LINE;
    }
    panel->label_purpose = LABEL_PURPOSE_NONE;
    panel->rename_category_id = 0;
    panel->popup_request = NULL;
    panel->dismiss_popups = true;
    release_import(panel);
    panel->left_press_active = false;
    panel->right_press_active = false;
}

static void show_set_aside_notice(WorldmapPanelState *panel) {
    static bool notice_shown = false;
    if (notice_shown || !worldmap_annotations_were_set_aside()) return;
    notice_shown = true;

    const char *path = worldmap_annotations_path();
    const char *file_name = path;
    for (const char *cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor == '/' || *cursor == '\\') file_name = cursor + 1;
    }
    char message[sizeof(panel->status_text)];
    snprintf(message, sizeof(message), "Your saved annotations could not be read and were set aside as %s.bad.",
             file_name);
    set_status_for(panel, message, NOTICE_SECONDS);
}

static void draw_close_cross(ImU32 colour) {
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    const float left = floorf((item_min.x + item_max.x - (float) CLOSE_CROSS_SIZE) / 2.0F);
    const float top = floorf((item_min.y + item_max.y - (float) CLOSE_CROSS_SIZE) / 2.0F);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    for (int step = 0; step < CLOSE_CROSS_SIZE; step++) {
        draw_pixel(draw_list, left + (float) step, top + (float) step, colour);
        draw_pixel(draw_list, left + (float) (CLOSE_CROSS_SIZE - 1 - step), top + (float) step, colour);
    }
}

// The whole title bar stays on screen so the window can always be grabbed again
static ImVec2 keep_title_bar_on_screen(ImVec2 position, float title_bottom_offset) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 size = ImGui::GetWindowSize();
    const float max_left = fmaxf(display.x - size.x, 0.0F);
    const float max_top = fmaxf(display.y - title_bottom_offset, 0.0F);
    return ImVec2(fminf(fmaxf(position.x, 0.0F), max_left), fminf(fmaxf(position.y, 0.0F), max_top));
}

static void render_title_bar(WorldmapPanelState *panel, bool *open) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float close_size = (float) BUTTON_SPRITE_HEIGHT;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float help_left = start.x + width - close_size * 2.0F - spacing;
    const float drag_width = fmaxf(help_left - start.x - spacing, 1.0F);

    const ImVec2 window_position = ImGui::GetWindowPos();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImGui::InvisibleButton("##title", ImVec2(drag_width, TITLE_HEIGHT));
    if (ImGui::IsItemActivated()) {
        panel->title_grab_offset = ImVec2(mouse.x - window_position.x, mouse.y - window_position.y);
    }
    ImVec2 wanted_position = window_position;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)) {
        wanted_position = ImVec2(mouse.x - panel->title_grab_offset.x, mouse.y - panel->title_grab_offset.y);
    }
    const ImVec2 kept_position = keep_title_bar_on_screen(wanted_position, start.y - window_position.y + TITLE_HEIGHT);
    if (kept_position.x != window_position.x || kept_position.y != window_position.y) {
        ImGui::SetWindowPos(kept_position);
    }

    const ImVec2 title_size = ImGui::CalcTextSize(WINDOW_TITLE);
    ImGui::GetWindowDrawList()->AddText(ImVec2(floorf(start.x + (width - title_size.x) / 2.0F),
                                               floorf(start.y + (TITLE_HEIGHT - title_size.y) / 2.0F)),
                                        GOLD_FONT_COLOR, WINDOW_TITLE);

    const float button_top = start.y + (TITLE_HEIGHT - close_size) / 2.0F;
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(help_left, button_top));
    if (ui_game_button("##help", "?", close_size, close_size, true, false)) {
        panel->help_page = 0;
        panel->popup_request = HELP_POPUP_ID;
    }
    if (ImGui::IsItemHovered()) ui_tooltip("How to use the world map");

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(start.x + width - close_size, button_top));
    if (ui_game_button("##close", "", close_size, close_size, true, false)) *open = false;
    draw_close_cross(ImGui::IsItemHovered() ? CLOSE_CROSS_HOVERED_COLOUR : GOLD_FONT_COLOR);
}

// ImGui draws its own grip with the window background, which the frame sprite then covers
static void draw_resize_grip(void) {
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float right = floorf(position.x + size.x) - (float) WINDOW_SPRITE_BORDER_WIDTH - 1.0F;
    const float bottom = floorf(position.y + size.y) - (float) WINDOW_SPRITE_BORDER_WIDTH - 1.0F;
    const ImVec2 grip_min(right - (float) RESIZE_GRIP_SIZE, bottom - (float) RESIZE_GRIP_SIZE);
    const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(grip_min, ImVec2(right, bottom), false);
    const ImU32 colour = !hovered                                    ? RESIZE_GRIP_COLOUR
                         : ImGui::IsMouseDown(ImGuiMouseButton_Left) ? GOLD_FONT_COLOR
                                                                     : RESIZE_GRIP_HOVERED_COLOUR;
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    for (int row = 0; row < RESIZE_GRIP_SIZE; row++) {
        const float row_top = bottom - (float) RESIZE_GRIP_SIZE + (float) row;
        draw_list->AddRectFilled(ImVec2(right - (float) (row + 1), row_top), ImVec2(right, row_top + 1.0F), colour);
    }
}

void worldmap_window_render(void) {
    WorldmapPanelState *panel = &panel_state;
    AnnotationSet *set = worldmap_annotations();
    bool open = true;

    show_set_aside_notice(panel);

    panel->hovered_tile_valid = false;
    ui_push_game_window_style();
    ImGui::SetNextWindowSize(ImVec2(900.0F, 620.0F), ImGuiCond_FirstUseEver);
    // The title bar is drawn by hand, so the window only moves when its title is dragged
    const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                          ImGuiWindowFlags_NoScrollWithMouse;
    const bool window_visible = ImGui::Begin(WINDOW_TITLE, NULL, window_flags);
    panel->window_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (window_visible) {
        frame_current_window();
        render_title_bar(panel, &open);
        ensure_current_category(panel, set);

        const float bar_height =
                fmaxf((float) BUTTON_SPRITE_HEIGHT, ImGui::GetFrameHeight()) + ImGui::GetStyle().ItemSpacing.y;
        const float body_height = fmaxf(ImGui::GetContentRegionAvail().y - bar_height, 1.0F);

        render_category_list(panel, set, body_height);
        ImGui::SameLine();
        render_map_area(panel, set, body_height);
        render_bottom_bar(panel, set);
        render_popups(panel, set);
        panel->dismiss_popups = false;
        draw_resize_grip();
    }
    ImGui::End();
    ui_pop_game_window_style();

    if (!open) worldmap_window_close();
}

void worldmap_minimap_tooltip_render(void) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!game_ui_state.minimap_shown || io.WantCaptureMouse) return;

    const float offset_left = io.MousePos.x - MINIMAP_SCREEN_LEFT;
    const float offset_top = io.MousePos.y - MINIMAP_SCREEN_TOP;
    if (offset_left < 0.0F || offset_top < 0.0F || offset_left >= MINIMAP_SCREEN_SIZE ||
        offset_top >= MINIMAP_SCREEN_SIZE) {
        return;
    }

    // The minimap runs map x down the screen and map y across it
    const Annotation *pin = minimap_overlay_pin_at(
            worldmap_annotations(), game_ui_state.minimap_origin_tile_x, game_ui_state.minimap_origin_tile_y,
            game_ui_state.minimap_magnification < 1 ? 1 : game_ui_state.minimap_magnification, (int) offset_top,
            (int) offset_left);
    if (pin && pin->label[0] != '\0') ui_tooltip(pin->label);
}

void worldmap_window_close(void) {
    drop_interrupted_work(&panel_state);
    game_ui_state.show_worldmap = false;
}

bool worldmap_window_finish_pending_path(void) {
    WorldmapPanelState *panel = &panel_state;
    if (!panel->window_focused || panel->active_tool != WORLDMAP_TOOL_LINE || panel->pending_segment_count == 0) {
        return false;
    }
    request_path_label(panel);
    return true;
}

bool worldmap_window_discard_pending_path(void) {
    WorldmapPanelState *panel = &panel_state;
    if (panel->active_tool != WORLDMAP_TOOL_LINE || panel->pending_segment_count == 0) return false;
    panel->pending_segment_count = 0;
    return true;
}
