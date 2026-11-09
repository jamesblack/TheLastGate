#include "minimap_overlay.h"

#include <stdlib.h>

typedef struct {
    unsigned int *pixels;
    int pixel_size;
    int origin_tile_x;
    int origin_tile_y;
    int magnify;
    unsigned int colour;
} OverlayCanvas;

static unsigned int abgr_from_rgb(const unsigned int rgb) {
    const unsigned int red = (rgb >> 16) & 0xFF;
    const unsigned int green = (rgb >> 8) & 0xFF;
    const unsigned int blue = rgb & 0xFF;

    return 0xFF000000u | (blue << 16) | (green << 8) | red;
}

static void put_pixel(const OverlayCanvas *canvas, const int pixel_x, const int pixel_y) {
    if (pixel_x < 0 || pixel_y < 0 || pixel_x >= canvas->pixel_size || pixel_y >= canvas->pixel_size) return;
    canvas->pixels[pixel_x * canvas->pixel_size + pixel_y] = canvas->colour;
}

static void fill_tile(const OverlayCanvas *canvas, const int tile_x, const int tile_y) {
    const int base_x = (tile_x - canvas->origin_tile_x) * canvas->magnify;
    const int base_y = (tile_y - canvas->origin_tile_y) * canvas->magnify;
    int sub_x;
    int sub_y;

    for (sub_x = 0; sub_x < canvas->magnify; sub_x++) {
        for (sub_y = 0; sub_y < canvas->magnify; sub_y++) put_pixel(canvas, base_x + sub_x, base_y + sub_y);
    }
}

static void paint_pin(const OverlayCanvas *canvas, const int tile_x, const int tile_y) {
    const int centre_x = (tile_x - canvas->origin_tile_x) * canvas->magnify + canvas->magnify / 2;
    const int centre_y = (tile_y - canvas->origin_tile_y) * canvas->magnify + canvas->magnify / 2;
    int offset_x;
    int offset_y;

    for (offset_x = -1; offset_x <= 1; offset_x++) {
        for (offset_y = -1; offset_y <= 1; offset_y++) put_pixel(canvas, centre_x + offset_x, centre_y + offset_y);
    }
}

static void paint_segment(const OverlayCanvas *canvas, const AnnotationSegment *segment) {
    int tile_x = segment->start_x;
    int tile_y = segment->start_y;
    const int delta_x = abs((int) segment->end_x - tile_x);
    const int delta_y = -abs((int) segment->end_y - tile_y);
    const int step_x = tile_x < segment->end_x ? 1 : -1;
    const int step_y = tile_y < segment->end_y ? 1 : -1;
    int error = delta_x + delta_y;
    int doubled_error;

    for (;;) {
        fill_tile(canvas, tile_x, tile_y);
        if (tile_x == segment->end_x && tile_y == segment->end_y) break;
        doubled_error = 2 * error;
        if (doubled_error >= delta_y) {
            error += delta_y;
            tile_x += step_x;
        }
        if (doubled_error <= delta_x) {
            error += delta_x;
            tile_y += step_y;
        }
    }
}

void minimap_overlay_paint(unsigned int *pixels, const int pixel_size, const int origin_tile_x, const int origin_tile_y,
                           const int magnify, const AnnotationSet *set) {
    OverlayCanvas canvas;
    int category_index;
    int annotation_index;
    int segment_index;

    if (set == NULL || !set->show_on_minimap || magnify < 1) return;

    canvas.pixels = pixels;
    canvas.pixel_size = pixel_size;
    canvas.origin_tile_x = origin_tile_x;
    canvas.origin_tile_y = origin_tile_y;
    canvas.magnify = magnify;

    for (category_index = 0; category_index < set->category_count; category_index++) {
        const AnnotationCategory *category = &set->categories[category_index];

        if (!category->visible) continue;
        canvas.colour = abgr_from_rgb(annotation_palette_rgb(category->colour_index));

        for (annotation_index = 0; annotation_index < category->annotation_count; annotation_index++) {
            const Annotation *annotation = &category->annotations[annotation_index];

            if (annotation->kind == ANNOTATION_PIN) {
                paint_pin(&canvas, annotation->tile_x, annotation->tile_y);
            } else if (annotation->kind == ANNOTATION_PATH) {
                for (segment_index = 0; segment_index < annotation->segment_count; segment_index++) {
                    paint_segment(&canvas, &annotation->segments[segment_index]);
                }
            }
        }
    }
}
