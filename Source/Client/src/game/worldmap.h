#pragma once

#include "game/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

void worldmap_open(const char *host, int port);

void worldmap_close(void);

void worldmap_save_annotations(void);

AnnotationSet *worldmap_annotations(void);

int worldmap_annotations_were_set_aside(void);

const char *worldmap_annotations_path(void);

#ifdef __cplusplus
}
#endif
