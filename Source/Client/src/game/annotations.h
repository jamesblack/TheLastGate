#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANNOTATION_LABEL_MAX 48
#define ANNOTATION_CATEGORY_NAME_MAX 32
#define ANNOTATION_MAX_CATEGORIES 256
#define ANNOTATION_MAX_DEPTH 8
#define ANNOTATION_COLOUR_INHERIT (-1)
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
    int colour_index;
} Annotation;

typedef struct {
    int id;
    int parent_id;
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
int annotation_category_add_under(AnnotationSet *set, int parent_id, const char *name, int colour_index);
int annotation_category_attach(AnnotationSet *set, AnnotationCategory *detached, int parent_id);
int annotation_category_has_children(const AnnotationSet *set, int category_id);
int annotation_category_depth(const AnnotationSet *set, int category_id);
int annotation_category_subtree_height(const AnnotationSet *set, int category_id);
int annotation_category_is_within(const AnnotationSet *set, int category_id, int ancestor_id);
int annotation_category_shown(const AnnotationSet *set, const AnnotationCategory *category);
int annotation_category_labels_shown(const AnnotationSet *set, const AnnotationCategory *category);
int annotation_tree_collect(const AnnotationSet *set, int root_id, int *ordered_indices, int capacity);
int annotation_name_equal(const char *left, const char *right);
void annotation_category_unique_name(const AnnotationSet *set, int parent_id, char *destination, const char *wanted);
int annotation_category_remove(AnnotationSet *set, int category_id);
int annotation_category_rename(AnnotationSet *set, int category_id, const char *name);
int annotation_category_set_colour(AnnotationSet *set, int category_id, int colour_index);
int annotation_category_can_move(const AnnotationSet *set, int category_id, int new_parent_id);
int annotation_category_move(AnnotationSet *set, int category_id, int new_parent_id);
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
int annotation_set_colour(AnnotationSet *set, int annotation_id, int colour_index);
int annotation_move(AnnotationSet *set, int annotation_id, int category_id);
unsigned int annotation_effective_rgb(const AnnotationCategory *category, const Annotation *annotation);
int annotation_identical(const Annotation *left, const Annotation *right);
int annotation_copy(Annotation *destination, const Annotation *source);
void annotation_release(Annotation *annotation);
int annotation_replace(AnnotationSet *set, int annotation_id, const Annotation *edited);

#ifdef __cplusplus
}
#endif
