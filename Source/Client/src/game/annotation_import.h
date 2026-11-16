#pragma once

#include "game/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ANNOTATION_IMPORT_OK,
    ANNOTATION_IMPORT_NAME_TAKEN,
    ANNOTATION_IMPORT_TOO_MANY_CATEGORIES,
    ANNOTATION_IMPORT_TOO_DEEP,
    ANNOTATION_IMPORT_CATEGORY_FULL,
    ANNOTATION_IMPORT_FAILED
} AnnotationImportStatus;

typedef struct {
    AnnotationImportStatus status;
    int root_id;
    char category_name[ANNOTATION_CATEGORY_NAME_MAX];
} AnnotationImportResult;

int annotation_import_conflict(const AnnotationSet *set, const AnnotationSet *imported);
AnnotationImportResult annotation_import_add(AnnotationSet *set, AnnotationSet *imported, const char *root_name);
AnnotationImportResult annotation_import_merge(AnnotationSet *set, AnnotationSet *imported, int target_id);

#ifdef __cplusplus
}
#endif
