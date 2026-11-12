#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANNOTATION_LABEL_MAX 48
#define ANNOTATION_CATEGORY_NAME_MAX 32
#define ANNOTATION_MAX_CATEGORIES 64
#define ANNOTATION_MAX_PER_CATEGORY 256
#define ANNOTATION_MAX_SEGMENTS 512
#define ANNOTATION_MAX_SEGMENTS_PER_CATEGORY 4096
#define ANNOTATION_TILE_LIMIT 2048
#define ANNOTATION_PALETTE_SIZE 8

typedef enum { ANNOTATION_NONE = 0, ANNOTATION_PIN = 1, ANNOTATION_TEXT = 2, ANNOTATION_PATH = 3 } AnnotationKind;

typedef struct {
    unsigned short start_x, start_y, end_x, end_y;
} AnnotationSegment;

typedef struct {
    int id;
    AnnotationKind kind;
    char label[ANNOTATION_LABEL_MAX];
    unsigned short tile_x, tile_y;
    AnnotationSegment *segments;
    int segment_count;
} Annotation;

typedef struct {
    int id;
    char name[ANNOTATION_CATEGORY_NAME_MAX];
    int colour_index;
    int visible;
    int show_labels;
    Annotation *annotations;
    int annotation_count;
} AnnotationCategory;

typedef struct {
    AnnotationCategory *categories;
    int category_count;
    int next_id;
    int show_on_minimap;
    int changed;
} AnnotationSet;

unsigned int annotation_palette_rgb(int colour_index);
void annotation_copy_text(char *destination, size_t destination_size, const char *source);
void annotation_set_init(AnnotationSet *set);
void annotation_set_release(AnnotationSet *set);
int annotation_category_add(AnnotationSet *set, const char *name, int colour_index);
int annotation_category_adopt(AnnotationSet *set, AnnotationCategory *detached);
int annotation_category_remove(AnnotationSet *set, int category_id);
AnnotationCategory *annotation_category_find(AnnotationSet *set, int category_id);
void annotation_category_release(AnnotationCategory *category);
int annotation_category_count_kind(const AnnotationCategory *category, AnnotationKind kind);
int annotation_category_segment_total(const AnnotationCategory *category);
int annotation_add_point(AnnotationSet *set, int category_id, AnnotationKind kind, int tile_x, int tile_y,
                         const char *label);
int annotation_add_path(AnnotationSet *set, int category_id, const AnnotationSegment *segments, int segment_count,
                        const char *label);
Annotation *annotation_find(AnnotationSet *set, int annotation_id, AnnotationCategory **owner);
int annotation_remove(AnnotationSet *set, int annotation_id);
int annotation_copy(Annotation *destination, const Annotation *source);
void annotation_release(Annotation *annotation);
int annotation_replace(AnnotationSet *set, int annotation_id, const Annotation *edited);

#ifdef __cplusplus
}
#endif
