#pragma once

#include "game/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANNOTATION_CODE_PREFIX "TLGMAP2:"
#define ANNOTATION_CODE_LEGACY_PREFIX "TLGMAP1:"
#define ANNOTATION_CODE_DISCORD_LIMIT 2000

typedef enum {
    ANNOTATION_CODE_OK,
    ANNOTATION_CODE_NOT_A_CODE,
    ANNOTATION_CODE_DAMAGED,
    ANNOTATION_CODE_TOO_LARGE,
} AnnotationCodeStatus;

int annotation_code_has_prefix(const char *text);
char *annotation_code_export(const AnnotationSet *set, int category_id);
AnnotationCodeStatus annotation_code_import(const char *code, AnnotationSet *imported);

#ifdef __cplusplus
}
#endif
