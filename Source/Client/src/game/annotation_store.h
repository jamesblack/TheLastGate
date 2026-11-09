#pragma once

#include "game/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANNOTATION_STORE_PREFIX "TLGAnnotations_"

int annotation_store_save(const char *path, AnnotationSet *set);

int annotation_store_load(const char *path, AnnotationSet *set);

#ifdef __cplusplus
}
#endif
