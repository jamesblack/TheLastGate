#pragma once

#include "game/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANNOTATION_CODE_PREFIX "TLGMAP1:"
#define ANNOTATION_CODE_DISCORD_LIMIT 2000

typedef enum {
    ANNOTATION_CODE_OK,
    ANNOTATION_CODE_NOT_A_CODE,
    ANNOTATION_CODE_DAMAGED,
    ANNOTATION_CODE_TOO_LARGE,
} AnnotationCodeStatus;

char *annotation_code_export(const AnnotationCategory *category);
AnnotationCodeStatus annotation_code_import(const char *code, AnnotationCategory *imported);

#ifdef __cplusplus
}
#endif
