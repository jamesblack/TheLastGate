#include "worldmap_help.hpp"

#include <imgui.h>

#include "ui_common.hpp"

enum { HELP_MAX_LINES = 8 };

struct HelpPage {
    const char *title;
    const char *lines[HELP_MAX_LINES];
};

static const float HELP_BODY_WIDTH = 460.0F;
static const float HELP_BODY_HEIGHT = 240.0F;

static const HelpPage HELP_PAGES[] = {
    {"Getting around",
     {
         "Open the map with the World Map hotkey. Bind it under Keybindings in the options.",
         "Scroll the mouse wheel to zoom in and out around the cursor.",
         "Hold the right mouse button and drag to pan, or use the Pan tool and drag with the left button.",
         "Tick Follow player to keep the map centred on you as you walk.",
         "The bottom right corner shows the tile under the cursor and its colour.",
         "Drag the title bar to move the window and the bottom right corner to resize it. Escape closes it.",
     }},
    {"Categories",
     {
         "Everything you draw lives in a category, listed on the left.",
         "Click a category to make it current. New pins, labels and paths go into the current category.",
         "+ New makes a top-level category. Click the arrow beside a category to open or close it.",
         "Categories can hold subcategories up to 8 levels deep.",
     }},
    {"Category menu",
     {
         "Right-click a category to change it.",
         "Show on map and Show labels hide or show everything inside it, subcategories included.",
         "New subcategory, Rename and Colour do what they say.",
         "Send to moves it under another category. Move up and Move down change its place in the list.",
         "Export copies it and its subcategories as a share code.",
         "Delete category asks first. Empty or move its subcategories before deleting it.",
     }},
    {"Pins and labels",
     {
         "Pick a tool from the bottom bar. Select lets you click annotations to highlight them.",
         "Pin: click the map to drop a marker, then give it a name. Names show beside pins when zoomed in, "
         "and on hover when zoomed out.",
         "Text: click the map to place a label. Text labels always need some text.",
         "Clicking an entry in the category list selects it and centres the map on it.",
     }},
    {"Drawing paths",
     {
         "Pick the Line tool, then drag from one point to another to draw each segment.",
         "A new segment snaps to the ends of the path you are drawing, so routes join up cleanly.",
         "Press Enter or switch tools to finish the path and give it a name.",
         "Press Escape to throw away a path you have not finished.",
         "Arrows along a path point the way it was drawn, from the hollow square to the solid one.",
     }},
    {"Changing annotations",
     {
         "Right-click a pin, label or path on the map or in the list.",
         "Colour gives it its own colour. Use category colour sets it back.",
         "Send to moves it to another category.",
         "Delete segment removes only the piece of path under the cursor.",
         "Delete removes the whole annotation after asking first.",
     }},
    {"Edit mode",
     {
         "Choose Edit from the right-click menu to change an annotation.",
         "Type a new name in the box on the bottom bar.",
         "Pins and labels: drag them to a new spot.",
         "Paths: drag to add segments, or click a segment to remove it.",
         "Press Save to keep the changes or Cancel to undo them.",
     }},
    {"Sharing and the minimap",
     {
         "Export puts a share code on the clipboard. Paste it to friends, on Discord or on the wiki.",
         "Import reads a code from the clipboard and previews what it holds before you add it.",
         "If you already have a category with the same name, choose Merge or add it under a new name.",
         "Very large codes may not fit in one Discord message but still work elsewhere.",
         "Tick Show on minimap to draw your annotations on the minimap. Hover a pin there to see its name.",
     }},
};

int worldmap_help_page_count(void) {
    return (int) (sizeof(HELP_PAGES) / sizeof(HELP_PAGES[0]));
}

void worldmap_help_render_page(int page_index) {
    if (page_index < 0 || page_index >= worldmap_help_page_count()) return;
    const HelpPage *page = &HELP_PAGES[page_index];

    ImGui::BeginChild("help_page", ImVec2(HELP_BODY_WIDTH, HELP_BODY_HEIGHT));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GOLD_FONT_COLOR), "%s", page->title);
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0F);
    for (int line_index = 0; line_index < HELP_MAX_LINES && page->lines[line_index]; line_index++) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(page->lines[line_index]);
        ImGui::Spacing();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}
