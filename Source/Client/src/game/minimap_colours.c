#include "minimap_colours.h"

#define COLOUR_COUNT 32768

// Colours drawn as black on the minimap and world map. Values are the RGB the world map shows
// under the cursor; anything within the same 5-bit step matches, so a value read off a
// screenshot works too. The saved map keeps the real colour, so removing an entry restores it.
static const MinimapRgb hidden_colours[] = {
    {0, 0, 0},
};

static unsigned char hidden_lookup[COLOUR_COUNT];
static int hidden_lookup_built = 0;

static unsigned char expand_channel(const unsigned short five_bits) {
    const unsigned char shifted = (unsigned char) (five_bits << 3);
    return (unsigned char) (shifted | (shifted >> 5));
}

MinimapRgb minimap_colour_rgb(const unsigned short colour) {
    MinimapRgb rgb;
    rgb.red = expand_channel((colour >> 10) & 0x1F);
    rgb.green = expand_channel((colour >> 5) & 0x1F);
    rgb.blue = expand_channel(colour & 0x1F);
    return rgb;
}

int minimap_colour_matches(const unsigned short colour, const MinimapRgb rgb) {
    return ((colour >> 10) & 0x1F) == (rgb.red >> 3) && ((colour >> 5) & 0x1F) == (rgb.green >> 3) &&
           (colour & 0x1F) == (rgb.blue >> 3);
}

static void build_hidden_lookup(void) {
    const int hidden_count = (int) (sizeof(hidden_colours) / sizeof(hidden_colours[0]));
    int colour;
    int hidden_index;

    for (colour = 0; colour < COLOUR_COUNT; colour++) {
        for (hidden_index = 0; hidden_index < hidden_count; hidden_index++) {
            if (minimap_colour_matches((unsigned short) colour, hidden_colours[hidden_index])) {
                hidden_lookup[colour] = 1;
                break;
            }
        }
    }
    hidden_lookup_built = 1;
}

unsigned short minimap_displayed_colour(const unsigned short colour) {
    if (!hidden_lookup_built) build_hidden_lookup();
    return hidden_lookup[colour & 0x7FFF] ? 0 : colour;
}
