#include "worldmap.h"

#include "config/config.h"
#include "game/annotation_store.h"
#include "game/server_file.h"
#include "log/log.h"

static AnnotationSet annotation_set;
static int annotation_set_ready = 0;
static char annotation_file_path[512];
static int annotation_file_set_aside = 0;

AnnotationSet *worldmap_annotations(void) {
    if (!annotation_set_ready) {
        annotation_set_init(&annotation_set);
        annotation_set_ready = 1;
    }
    return &annotation_set;
}

void worldmap_open(const char *host, const int port) {
    AnnotationSet *set = worldmap_annotations();
    int load_result;

    annotation_set_release(set);
    annotation_set_init(set);

    server_file_build_path(annotation_file_path, sizeof(annotation_file_path), g_config.runtime.pref_path,
                           ANNOTATION_STORE_PREFIX, host, port);
    load_result = annotation_store_load(annotation_file_path, set);
    annotation_file_set_aside = load_result < 0;
    if (load_result < 0) {
        log_warning("Saved annotations at %s were damaged and have been set aside", annotation_file_path);
    } else if (load_result > 0) {
        log_info("Loaded annotations from %s", annotation_file_path);
    }

    if (set->category_count == 0) {
        annotation_category_add(set, "My notes", 1);
        set->changed = 0;
    }
}

int worldmap_annotations_were_set_aside(void) {
    return annotation_file_set_aside;
}

const char *worldmap_annotations_path(void) {
    return annotation_file_path;
}

void worldmap_save_annotations(void) {
    AnnotationSet *set = worldmap_annotations();

    if (!set->changed || annotation_file_path[0] == '\0') return;

    if (annotation_store_save(annotation_file_path, set)) {
        log_info("Saved annotations to %s", annotation_file_path);
    } else {
        log_error("Failed to save annotations to %s", annotation_file_path);
    }
}

void worldmap_close(void) {
    worldmap_save_annotations();
}
