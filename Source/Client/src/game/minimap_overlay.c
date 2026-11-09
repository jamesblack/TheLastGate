#include "minimap_overlay.h"

#include <stdlib.h>

#define DOT_SPACING 2

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

static void paint_pin(const OverlayCanvas *canvas, const int tile_x, const int tile_y) {
    const int centre_x = (tile_x - canvas->origin_tile_x) * canvas->magnify + canvas->magnify / 2;
    const int centre_y = (tile_y - canvas->origin_tile_y) * canvas->magnify + canvas->magnify / 2;
    int offset_x;
    int offset_y;

    for (offset_x = -1; offset_x <= 1; offset_x++) {
        for (offset_y = -1; offset_y <= 1; offset_y++) put_pixel(canvas, centre_x + offset_x, centre_y + offset_y);
    }
}

static int segment_misses_view(const OverlayCanvas *canvas, const AnnotationSegment *segment, const int visible_tiles) {
    const int lowest_x = segment->start_x < segment->end_x ? segment->start_x : segment->end_x;
    const int highest_x = segment->start_x < segment->end_x ? segment->end_x : segment->start_x;
    const int lowest_y = segment->start_y < segment->end_y ? segment->start_y : segment->end_y;
    const int highest_y = segment->start_y < segment->end_y ? segment->end_y : segment->start_y;

    return highest_x < canvas->origin_tile_x || lowest_x >= canvas->origin_tile_x + visible_tiles ||
           highest_y < canvas->origin_tile_y || lowest_y >= canvas->origin_tile_y + visible_tiles;
}

static int tile_centre_pixel(const OverlayCanvas *canvas, const int tile, const int origin_tile) {
    return (tile - origin_tile) * canvas->magnify + canvas->magnify / 2;
}

/*
 * Draws a 1-pixel line between tile centres, lighting every DOT_SPACING-th pixel counted from the segment start so
 * the dots stay put as the minimap scrolls. Only steps whose major-axis pixel lies in the view are walked: Bresenham's
 * minor-axis offset after `step` major steps is floor((2 * minor_length * step + major_length) / (2 * major_length)),
 * so any step can be computed directly and the pixels painted are exactly those of a full walk from the start.
 */
static void paint_segment(const OverlayCanvas *canvas, const AnnotationSegment *segment) {
    const int visible_tiles = (canvas->pixel_size + canvas->magnify - 1) / canvas->magnify;
    const int start_x = tile_centre_pixel(canvas, segment->start_x, canvas->origin_tile_x);
    const int start_y = tile_centre_pixel(canvas, segment->start_y, canvas->origin_tile_y);
    const int end_x = tile_centre_pixel(canvas, segment->end_x, canvas->origin_tile_x);
    const int end_y = tile_centre_pixel(canvas, segment->end_y, canvas->origin_tile_y);
    const int delta_x = abs(end_x - start_x);
    const int delta_y = abs(end_y - start_y);
    const int step_x = start_x < end_x ? 1 : -1;
    const int step_y = start_y < end_y ? 1 : -1;
    const int x_major = delta_x >= delta_y;
    const int major_length = x_major ? delta_x : delta_y;
    const int minor_length = x_major ? delta_y : delta_x;
    const int major_start = x_major ? start_x : start_y;
    const int major_direction = x_major ? step_x : step_y;
    const int view_last_major = canvas->pixel_size - 1;
    int first_step;
    int last_step;
    int step;

    if (segment_misses_view(canvas, segment, visible_tiles)) return;
    if (major_length == 0) {
        put_pixel(canvas, start_x, start_y);
        return;
    }

    if (major_direction > 0) {
        first_step = -major_start;
        last_step = view_last_major - major_start;
    } else {
        first_step = major_start - view_last_major;
        last_step = major_start;
    }
    if (first_step < 0) first_step = 0;
    if (last_step > major_length) last_step = major_length;
    if (first_step % DOT_SPACING != 0) first_step += DOT_SPACING - first_step % DOT_SPACING;

    for (step = first_step; step <= last_step; step += DOT_SPACING) {
        const int minor_offset = (2 * minor_length * step + major_length) / (2 * major_length);

        if (x_major) {
            put_pixel(canvas, start_x + step_x * step, start_y + step_y * minor_offset);
        } else {
            put_pixel(canvas, start_x + step_x * minor_offset, start_y + step_y * step);
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
