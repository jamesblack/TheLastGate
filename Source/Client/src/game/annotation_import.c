#include "game/annotation_import.h"

#include <stdlib.h>
#include <string.h>

/* The same traversal runs twice: a counting pass that only checks limits, then an applying pass that changes the
   set. The applying pass runs only when the counting pass reports OK, so the limits never stop it halfway. */
typedef struct {
    AnnotationSet *set;
    AnnotationSet *imported;
    int applying;
    int original_count;
    int added_categories;
    int visited[ANNOTATION_MAX_CATEGORIES];
    int matched_local[ANNOTATION_MAX_CATEGORIES];
    int planned_count[ANNOTATION_MAX_CATEGORIES];
    int planned_segments[ANNOTATION_MAX_CATEGORIES];
    AnnotationImportResult result;
} ImportPass;

static int find_match(const AnnotationSet *set, const int parent_id, const char *name, const int limit) {
    int index, loose_match = -1;
    for (index = 0; index < limit; index++) {
        const AnnotationCategory *category = &set->categories[index];
        if (category->parent_id != parent_id || category->id == parent_id) continue;
        if (strcmp(category->name, name) == 0) return index;
        if (loose_match < 0 && annotation_name_equal(category->name, name)) loose_match = index;
    }
    return loose_match;
}

int annotation_import_conflict(const AnnotationSet *set, const AnnotationSet *imported) {
    int index;
    if (imported->category_count < 1) return 0;
    index = find_match(set, 0, imported->categories[0].name, set->category_count);
    return index < 0 ? 0 : set->categories[index].id;
}

static void reset_pass(ImportPass *pass, const int applying) {
    int index;
    pass->applying = applying;
    pass->added_categories = 0;
    for (index = 0; index < ANNOTATION_MAX_CATEGORIES; index++) {
        pass->visited[index] = 0;
        pass->matched_local[index] = -1;
        pass->planned_count[index] = 0;
        pass->planned_segments[index] = 0;
    }
}

static int begin_pass(ImportPass *pass, AnnotationSet *set, AnnotationSet *imported) {
    memset(&pass->result, 0, sizeof(pass->result));
    pass->result.status = ANNOTATION_IMPORT_FAILED;
    pass->set = set;
    pass->imported = imported;
    pass->original_count = set->category_count;
    reset_pass(pass, 0);
    if (imported->category_count < 1) return 0;
    if (imported->category_count > ANNOTATION_MAX_CATEGORIES || set->category_count > ANNOTATION_MAX_CATEGORIES) {
        pass->result.status = ANNOTATION_IMPORT_TOO_MANY_CATEGORIES;
        return 0;
    }
    return 1;
}

static AnnotationImportStatus place_subtree(ImportPass *pass, const int imported_index, const int local_parent_id,
                                            const int level, const char *root_name, int *placed_id) {
    AnnotationCategory *incoming = &pass->imported->categories[imported_index];
    const int incoming_id = incoming->id;
    int local_id = 0, child_index;
    pass->visited[imported_index] = 1;
    if (!pass->applying) {
        if (level > ANNOTATION_MAX_DEPTH) return ANNOTATION_IMPORT_TOO_DEEP;
        pass->added_categories++;
        if (pass->original_count + pass->added_categories > ANNOTATION_MAX_CATEGORIES) {
            return ANNOTATION_IMPORT_TOO_MANY_CATEGORIES;
        }
        if (incoming->annotation_count > ANNOTATION_MAX_PER_CATEGORY ||
            annotation_category_segment_total(incoming) > ANNOTATION_MAX_SEGMENTS_PER_CATEGORY) {
            annotation_copy_text(pass->result.category_name, sizeof(pass->result.category_name),
                                 root_name != NULL ? root_name : incoming->name);
            return ANNOTATION_IMPORT_CATEGORY_FULL;
        }
    } else {
        char name[ANNOTATION_CATEGORY_NAME_MAX];
        if (root_name != NULL) {
            annotation_copy_text(name, sizeof(name), root_name);
        } else {
            annotation_category_unique_name(pass->set, local_parent_id, name, incoming->name);
        }
        local_id = annotation_category_attach(pass->set, incoming, local_parent_id);
        if (local_id == 0) return ANNOTATION_IMPORT_FAILED;
        memcpy(annotation_category_find(pass->set, local_id)->name, name, sizeof(name));
        if (placed_id != NULL) *placed_id = local_id;
    }
    if (incoming_id == 0) return ANNOTATION_IMPORT_OK;
    for (child_index = 0; child_index < pass->imported->category_count; child_index++) {
        if (!pass->visited[child_index] && pass->imported->categories[child_index].parent_id == incoming_id) {
            const AnnotationImportStatus status = place_subtree(pass, child_index, local_id, level + 1, NULL, NULL);
            if (status != ANNOTATION_IMPORT_OK) return status;
        }
    }
    return ANNOTATION_IMPORT_OK;
}

static int matches_any(const Annotation *annotations, const int count, const Annotation *annotation) {
    int index;
    for (index = 0; index < count; index++) {
        if (annotation_identical(&annotations[index], annotation)) return 1;
    }
    return 0;
}

/* Identity is an equivalence, so comparing against every earlier incoming annotation for this local category
   (kept or skipped) gives the same answer as comparing against what the applying pass will have appended. */
static int is_known_annotation(const ImportPass *pass, const int imported_index, const int local_index,
                               const int annotation_index) {
    const AnnotationCategory *local = &pass->set->categories[local_index];
    const AnnotationCategory *incoming = &pass->imported->categories[imported_index];
    const Annotation *annotation = &incoming->annotations[annotation_index];
    int category_index;
    if (matches_any(local->annotations, local->annotation_count, annotation)) return 1;
    if (matches_any(incoming->annotations, annotation_index, annotation)) return 1;
    for (category_index = 0; category_index < pass->imported->category_count; category_index++) {
        const AnnotationCategory *earlier = &pass->imported->categories[category_index];
        if (category_index == imported_index || pass->matched_local[category_index] != local_index) continue;
        if (matches_any(earlier->annotations, earlier->annotation_count, annotation)) return 1;
    }
    return 0;
}

static AnnotationImportStatus plan_annotations(ImportPass *pass, const int imported_index, const int local_index) {
    const AnnotationCategory *local = &pass->set->categories[local_index];
    const AnnotationCategory *incoming = &pass->imported->categories[imported_index];
    int index;
    for (index = 0; index < incoming->annotation_count; index++) {
        if (is_known_annotation(pass, imported_index, local_index, index)) continue;
        pass->planned_count[local_index]++;
        pass->planned_segments[local_index] += incoming->annotations[index].segment_count;
    }
    if (local->annotation_count + pass->planned_count[local_index] > ANNOTATION_MAX_PER_CATEGORY ||
        annotation_category_segment_total(local) + pass->planned_segments[local_index] >
                ANNOTATION_MAX_SEGMENTS_PER_CATEGORY) {
        annotation_copy_text(pass->result.category_name, sizeof(pass->result.category_name), local->name);
        return ANNOTATION_IMPORT_CATEGORY_FULL;
    }
    return ANNOTATION_IMPORT_OK;
}

static AnnotationImportStatus move_annotations(ImportPass *pass, const int imported_index, const int local_index) {
    AnnotationCategory *local = &pass->set->categories[local_index];
    AnnotationCategory *incoming = &pass->imported->categories[imported_index];
    Annotation *grown;
    int index, remaining_count = 0, moved_count = 0;
    if (incoming->annotation_count == 0) return ANNOTATION_IMPORT_OK;
    grown = realloc(local->annotations,
                    sizeof(Annotation) * (size_t) (local->annotation_count + incoming->annotation_count));
    if (grown == NULL) return ANNOTATION_IMPORT_FAILED;
    local->annotations = grown;
    for (index = 0; index < incoming->annotation_count; index++) {
        if (matches_any(local->annotations, local->annotation_count, &incoming->annotations[index])) {
            incoming->annotations[remaining_count++] = incoming->annotations[index];
            continue;
        }
        local->annotations[local->annotation_count] = incoming->annotations[index];
        local->annotations[local->annotation_count].id = pass->set->next_id++;
        local->annotation_count++;
        moved_count++;
    }
    incoming->annotation_count = remaining_count;
    if (moved_count > 0) pass->set->changed = 1;
    return ANNOTATION_IMPORT_OK;
}

/* Incoming children match only categories that existed before the merge, so duplicate incoming siblings without a
   local match are each attached under their own unique name rather than merged into one another. */
static AnnotationImportStatus merge_category(ImportPass *pass, const int imported_index, const int local_index,
                                             const int level) {
    const int incoming_id = pass->imported->categories[imported_index].id;
    const int local_id = pass->set->categories[local_index].id;
    AnnotationImportStatus status;
    int child_index;
    pass->visited[imported_index] = 1;
    pass->matched_local[imported_index] = local_index;
    status = pass->applying ? move_annotations(pass, imported_index, local_index)
                            : plan_annotations(pass, imported_index, local_index);
    if (status != ANNOTATION_IMPORT_OK || incoming_id == 0) return status;
    for (child_index = 0; child_index < pass->imported->category_count; child_index++) {
        const AnnotationCategory *child = &pass->imported->categories[child_index];
        int match;
        if (pass->visited[child_index] || child->parent_id != incoming_id) continue;
        match = find_match(pass->set, local_id, child->name, pass->original_count);
        if (match >= 0) {
            status = merge_category(pass, child_index, match, level + 1);
        } else {
            status = place_subtree(pass, child_index, local_id, level + 1, NULL, NULL);
        }
        if (status != ANNOTATION_IMPORT_OK) return status;
    }
    return ANNOTATION_IMPORT_OK;
}

AnnotationImportResult annotation_import_add(AnnotationSet *set, AnnotationSet *imported, const char *root_name) {
    ImportPass pass;
    char name[ANNOTATION_CATEGORY_NAME_MAX];
    if (!begin_pass(&pass, set, imported)) return pass.result;
    annotation_copy_text(name, sizeof(name), root_name != NULL ? root_name : imported->categories[0].name);
    if (name[0] == '\0') return pass.result;
    if (find_match(set, 0, name, set->category_count) >= 0) {
        pass.result.status = ANNOTATION_IMPORT_NAME_TAKEN;
        return pass.result;
    }
    pass.result.status = place_subtree(&pass, 0, 0, 1, name, NULL);
    if (pass.result.status != ANNOTATION_IMPORT_OK) return pass.result;
    reset_pass(&pass, 1);
    pass.result.status = place_subtree(&pass, 0, 0, 1, name, &pass.result.root_id);
    return pass.result;
}

AnnotationImportResult annotation_import_merge(AnnotationSet *set, AnnotationSet *imported, const int target_id) {
    ImportPass pass;
    AnnotationCategory *target;
    int target_index, target_depth;
    if (!begin_pass(&pass, set, imported)) return pass.result;
    target = annotation_category_find(set, target_id);
    if (target == NULL) return pass.result;
    target_index = (int) (target - set->categories);
    target_depth = annotation_category_depth(set, target_id);
    pass.result.status = merge_category(&pass, 0, target_index, target_depth);
    if (pass.result.status != ANNOTATION_IMPORT_OK) return pass.result;
    reset_pass(&pass, 1);
    pass.result.status = merge_category(&pass, 0, target_index, target_depth);
    if (pass.result.status == ANNOTATION_IMPORT_OK) pass.result.root_id = target_id;
    return pass.result;
}
