#include "game/annotations.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned int palette[ANNOTATION_PALETTE_SIZE] = {0x4FC3FF, 0xFFD34D, 0xFF6B6B, 0x7CFC8A,
                                                              0xD58CFF, 0xFFA040, 0xFFFFFF, 0x40E0D0};

unsigned int annotation_palette_rgb(const int colour_index) {
    if (colour_index < 0 || colour_index >= ANNOTATION_PALETTE_SIZE) return palette[0];
    return palette[colour_index];
}

void annotation_copy_text(char *destination, const size_t destination_size, const char *source) {
    size_t length;
    if (destination == NULL || destination_size == 0) return;
    if (source == NULL) source = "";
    length = strlen(source);
    if (length > destination_size - 1) {
        length = destination_size - 1;
        /* Back up so the cut never lands inside a UTF-8 multibyte sequence. */
        while (length > 0 && ((unsigned char) source[length] & 0xC0) == 0x80) length--;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static int tile_in_range(const int tile) {
    return tile >= 0 && tile < ANNOTATION_TILE_LIMIT;
}

static int segments_valid(const AnnotationSegment *segments, const int segment_count) {
    int index;
    if (segments == NULL || segment_count < 1 || segment_count > ANNOTATION_MAX_SEGMENTS) return 0;
    for (index = 0; index < segment_count; index++) {
        if (!tile_in_range(segments[index].start_x) || !tile_in_range(segments[index].start_y) ||
            !tile_in_range(segments[index].end_x) || !tile_in_range(segments[index].end_y))
            return 0;
    }
    return 1;
}

static AnnotationSegment *duplicate_segments(const AnnotationSegment *segments, const int segment_count) {
    AnnotationSegment *copied = malloc(sizeof(AnnotationSegment) * (size_t) segment_count);
    if (copied != NULL) memcpy(copied, segments, sizeof(AnnotationSegment) * (size_t) segment_count);
    return copied;
}

void annotation_set_init(AnnotationSet *set) {
    memset(set, 0, sizeof(*set));
    set->next_id = 1;
}

void annotation_release(Annotation *annotation) {
    free(annotation->segments);
    annotation->segments = NULL;
    annotation->segment_count = 0;
}

void annotation_category_release(AnnotationCategory *category) {
    int index;
    for (index = 0; index < category->annotation_count; index++) annotation_release(&category->annotations[index]);
    free(category->annotations);
    memset(category, 0, sizeof(*category));
}

void annotation_set_release(AnnotationSet *set) {
    int index;
    for (index = 0; index < set->category_count; index++) annotation_category_release(&set->categories[index]);
    free(set->categories);
    set->categories = NULL;
    set->category_count = 0;
}

AnnotationCategory *annotation_category_find(AnnotationSet *set, const int category_id) {
    int index;
    for (index = 0; index < set->category_count; index++) {
        if (set->categories[index].id == category_id) return &set->categories[index];
    }
    return NULL;
}

static int name_in_use(const AnnotationSet *set, const char *name) {
    int index;
    for (index = 0; index < set->category_count; index++) {
        if (strcmp(set->categories[index].name, name) == 0) return 1;
    }
    return 0;
}

static void make_unique_name(const AnnotationSet *set, char *destination, const char *wanted) {
    char suffix[16];
    int attempt;
    annotation_copy_text(destination, ANNOTATION_CATEGORY_NAME_MAX, wanted);
    for (attempt = 2; name_in_use(set, destination); attempt++) {
        snprintf(suffix, sizeof(suffix), " (%d)", attempt);
        annotation_copy_text(destination, ANNOTATION_CATEGORY_NAME_MAX - strlen(suffix), wanted);
        strcat(destination, suffix);
    }
}

static AnnotationCategory *append_category(AnnotationSet *set) {
    AnnotationCategory *grown;
    if (set->category_count >= ANNOTATION_MAX_CATEGORIES) return NULL;
    grown = realloc(set->categories, sizeof(AnnotationCategory) * (size_t) (set->category_count + 1));
    if (grown == NULL) return NULL;
    set->categories = grown;
    return &set->categories[set->category_count++];
}

int annotation_category_add(AnnotationSet *set, const char *name, const int colour_index) {
    AnnotationCategory *category;
    char unique_name[ANNOTATION_CATEGORY_NAME_MAX];
    make_unique_name(set, unique_name, name);
    category = append_category(set);
    if (category == NULL) return 0;
    memset(category, 0, sizeof(*category));
    category->id = set->next_id++;
    memcpy(category->name, unique_name, sizeof(unique_name));
    category->colour_index = colour_index >= 0 && colour_index < ANNOTATION_PALETTE_SIZE ? colour_index : 0;
    category->visible = 1;
    set->changed = 1;
    return category->id;
}

int annotation_category_adopt(AnnotationSet *set, AnnotationCategory *detached) {
    AnnotationCategory *category;
    char unique_name[ANNOTATION_CATEGORY_NAME_MAX];
    int index;
    make_unique_name(set, unique_name, detached->name);
    category = append_category(set);
    if (category == NULL) return 0;
    *category = *detached;
    memcpy(category->name, unique_name, sizeof(unique_name));
    category->id = set->next_id++;
    for (index = 0; index < category->annotation_count; index++) category->annotations[index].id = set->next_id++;
    memset(detached, 0, sizeof(*detached));
    set->changed = 1;
    return category->id;
}

int annotation_category_remove(AnnotationSet *set, const int category_id) {
    AnnotationCategory *category = annotation_category_find(set, category_id);
    int index;
    if (category == NULL) return 0;
    index = (int) (category - set->categories);
    annotation_category_release(category);
    memmove(category, category + 1, sizeof(AnnotationCategory) * (size_t) (set->category_count - index - 1));
    set->category_count--;
    set->changed = 1;
    return 1;
}

int annotation_category_count_kind(const AnnotationCategory *category, const AnnotationKind kind) {
    int index, total = 0;
    for (index = 0; index < category->annotation_count; index++) {
        if (category->annotations[index].kind == kind) total++;
    }
    return total;
}

static Annotation *append_annotation(AnnotationCategory *category) {
    Annotation *grown;
    if (category->annotation_count >= ANNOTATION_MAX_PER_CATEGORY) return NULL;
    grown = realloc(category->annotations, sizeof(Annotation) * (size_t) (category->annotation_count + 1));
    if (grown == NULL) return NULL;
    category->annotations = grown;
    return &category->annotations[category->annotation_count++];
}

int annotation_add_point(AnnotationSet *set, const int category_id, const AnnotationKind kind, const int tile_x,
                         const int tile_y, const char *label) {
    AnnotationCategory *category = annotation_category_find(set, category_id);
    Annotation *annotation;
    if (category == NULL || (kind != ANNOTATION_PIN && kind != ANNOTATION_TEXT)) return 0;
    if (!tile_in_range(tile_x) || !tile_in_range(tile_y)) return 0;
    annotation = append_annotation(category);
    if (annotation == NULL) return 0;
    memset(annotation, 0, sizeof(*annotation));
    annotation->id = set->next_id++;
    annotation->kind = kind;
    annotation->tile_x = (unsigned short) tile_x;
    annotation->tile_y = (unsigned short) tile_y;
    annotation_copy_text(annotation->label, sizeof(annotation->label), label);
    set->changed = 1;
    return annotation->id;
}

int annotation_add_path(AnnotationSet *set, const int category_id, const AnnotationSegment *segments,
                        const int segment_count, const char *label) {
    AnnotationCategory *category = annotation_category_find(set, category_id);
    Annotation *annotation;
    AnnotationSegment *copied;
    if (category == NULL || !segments_valid(segments, segment_count)) return 0;
    if (category->annotation_count >= ANNOTATION_MAX_PER_CATEGORY) return 0;
    copied = duplicate_segments(segments, segment_count);
    if (copied == NULL) return 0;
    annotation = append_annotation(category);
    if (annotation == NULL) {
        free(copied);
        return 0;
    }
    memset(annotation, 0, sizeof(*annotation));
    annotation->id = set->next_id++;
    annotation->kind = ANNOTATION_PATH;
    annotation->tile_x = segments[0].start_x;
    annotation->tile_y = segments[0].start_y;
    annotation->segments = copied;
    annotation->segment_count = segment_count;
    annotation_copy_text(annotation->label, sizeof(annotation->label), label);
    set->changed = 1;
    return annotation->id;
}

Annotation *annotation_find(AnnotationSet *set, const int annotation_id, AnnotationCategory **owner) {
    int category_index, annotation_index;
    for (category_index = 0; category_index < set->category_count; category_index++) {
        AnnotationCategory *category = &set->categories[category_index];
        for (annotation_index = 0; annotation_index < category->annotation_count; annotation_index++) {
            if (category->annotations[annotation_index].id == annotation_id) {
                if (owner != NULL) *owner = category;
                return &category->annotations[annotation_index];
            }
        }
    }
    return NULL;
}

int annotation_remove(AnnotationSet *set, const int annotation_id) {
    AnnotationCategory *category = NULL;
    Annotation *annotation = annotation_find(set, annotation_id, &category);
    int index;
    if (annotation == NULL) return 0;
    index = (int) (annotation - category->annotations);
    annotation_release(annotation);
    memmove(annotation, annotation + 1, sizeof(Annotation) * (size_t) (category->annotation_count - index - 1));
    category->annotation_count--;
    set->changed = 1;
    return 1;
}

int annotation_copy(Annotation *destination, const Annotation *source) {
    AnnotationSegment *copied = NULL;
    if (source->segment_count > 0) {
        copied = duplicate_segments(source->segments, source->segment_count);
        if (copied == NULL) return 0;
    }
    *destination = *source;
    destination->segments = copied;
    return 1;
}

int annotation_replace(AnnotationSet *set, const int annotation_id, const Annotation *edited) {
    Annotation *annotation = annotation_find(set, annotation_id, NULL);
    AnnotationSegment *copied = NULL;
    if (annotation == NULL) return 0;
    if (annotation->kind == ANNOTATION_PATH) {
        if (!segments_valid(edited->segments, edited->segment_count)) return 0;
        copied = duplicate_segments(edited->segments, edited->segment_count);
        if (copied == NULL) return 0;
    } else if (!tile_in_range(edited->tile_x) || !tile_in_range(edited->tile_y)) {
        return 0;
    }
    annotation_release(annotation);
    annotation_copy_text(annotation->label, sizeof(annotation->label), edited->label);
    annotation->tile_x = edited->tile_x;
    annotation->tile_y = edited->tile_y;
    annotation->segments = copied;
    annotation->segment_count = copied != NULL ? edited->segment_count : 0;
    set->changed = 1;
    return 1;
}
