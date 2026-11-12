#include "minimap_colours.h"

#define COLOUR_COUNT (1u << 24)

// Colours drawn as black on the minimap and world map, exactly as the world map shows them under
// the cursor. The saved map keeps the real colour, so removing an entry restores those tiles.
static const MinimapRgb hidden_colours[] = {
    {36, 36, 36},
    {33, 34, 33},
    {53, 13, 8},
};

static unsigned char hidden_bits[COLOUR_COUNT / 8];
static int hidden_bits_built = 0;

MinimapColour minimap_colour_from_rgb(const unsigned char red, const unsigned char green, const unsigned char blue) {
    return ((MinimapColour) red << 16) | ((MinimapColour) green << 8) | (MinimapColour) blue;
}

static unsigned char expand_five_bits(const unsigned short five_bits) {
    const unsigned char shifted = (unsigned char) (five_bits << 3);
    return (unsigned char) (shifted | (shifted >> 5));
}

MinimapColour minimap_colour_from_rgb555(const unsigned short colour555) {
    return minimap_colour_from_rgb(expand_five_bits((colour555 >> 10) & 0x1F),
                                   expand_five_bits((colour555 >> 5) & 0x1F), expand_five_bits(colour555 & 0x1F));
}

MinimapRgb minimap_colour_rgb(const MinimapColour colour) {
    MinimapRgb rgb;
    rgb.red = (unsigned char) ((colour >> 16) & 0xFF);
    rgb.green = (unsigned char) ((colour >> 8) & 0xFF);
    rgb.blue = (unsigned char) (colour & 0xFF);
    return rgb;
}

static void build_hidden_bits(void) {
    const int hidden_count = (int) (sizeof(hidden_colours) / sizeof(hidden_colours[0]));
    int hidden_index;

    for (hidden_index = 0; hidden_index < hidden_count; hidden_index++) {
        const MinimapRgb rgb = hidden_colours[hidden_index];
        const MinimapColour colour = minimap_colour_from_rgb(rgb.red, rgb.green, rgb.blue);
        hidden_bits[colour >> 3] |= (unsigned char) (1u << (colour & 7));
    }
    hidden_bits_built = 1;
}

MinimapColour minimap_displayed_colour(const MinimapColour colour) {
    const MinimapColour rgb_only = colour & 0xFFFFFFu;

    if (!hidden_bits_built) build_hidden_bits();
    return (hidden_bits[rgb_only >> 3] & (1u << (rgb_only & 7))) ? 0 : rgb_only;
}
