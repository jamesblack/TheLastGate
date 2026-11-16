#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/annotation_import.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static AnnotationCategory *find_child(AnnotationSet *set, const int parent_id, const char *name) {
    int index;
    for (index = 0; index < set->category_count; index++) {
        if (set->categories[index].parent_id == parent_id && strcmp(set->categories[index].name, name) == 0) {
            return &set->categories[index];
        }
    }
    return NULL;
}

static int count_children(const AnnotationSet *set, const int parent_id) {
    int index, total = 0;
    for (index = 0; index < set->category_count; index++) {
        if (set->categories[index].parent_id == parent_id) total++;
    }
    return total;
}

static void test_conflict_prefers_exact_then_first_case_insensitive(void) {
    AnnotationSet set, exact_import, loose_import, other_import;
    int lower_id, upper_id;
    annotation_set_init(&set);
    annotation_set_init(&exact_import);
    annotation_set_init(&loose_import);
    annotation_set_init(&other_import);
    lower_id = annotation_category_add(&set, "dungeons", 0);
    upper_id = annotation_category_add(&set, "Dungeons", 0);
    strcpy(set.categories[1].name, "Dungeons");
    annotation_category_add(&exact_import, "Dungeons", 0);
    annotation_category_add(&loose_import, "DUNGEONS", 0);
    annotation_category_add(&other_import, "Keys", 0);
    CHECK(annotation_import_conflict(&set, &exact_import) == upper_id);
    CHECK(annotation_import_conflict(&set, &loose_import) == lower_id);
    CHECK(annotation_import_conflict(&set, &other_import) == 0);
    annotation_set_release(&set);
    annotation_set_release(&exact_import);
    annotation_set_release(&loose_import);
    annotation_set_release(&other_import);
}

static void test_add_with_new_name(void) {
    AnnotationSet set, imported;
    AnnotationImportResult result;
    AnnotationCategory *root, *cavern;
    int incoming_root, incoming_cavern;
    annotation_set_init(&set);
    annotation_set_init(&imported);
    annotation_add_point(&set, annotation_category_add(&set, "Dungeons", 1), ANNOTATION_PIN, 1, 1, "Home");
    incoming_root = annotation_category_add(&imported, "Dungeons", 4);
    imported.categories[0].show_labels = 0;
    incoming_cavern = annotation_category_add_under(&imported, incoming_root, "Cold Cavern", 2);
    annotation_add_point(&imported, incoming_cavern, ANNOTATION_PIN, 3, 4, "Entrance");
    annotation_category_add_under(&imported, incoming_root, "Ice Lair", 3);

    result = annotation_import_add(&set, &imported, "Friend Dungeons");
    CHECK(result.status == ANNOTATION_IMPORT_OK);
    CHECK(set.category_count == 4);
    root = annotation_category_find(&set, result.root_id);
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(strcmp(root->name, "Friend Dungeons") == 0);
        CHECK(root->parent_id == 0);
        CHECK(root->colour_index == 4);
        CHECK(root->show_labels == 0);
    }
    CHECK(count_children(&set, result.root_id) == 2);
    cavern = find_child(&set, result.root_id, "Cold Cavern");
    CHECK(cavern != NULL);
    if (cavern != NULL) {
        CHECK(cavern->colour_index == 2);
        CHECK(cavern->annotation_count == 1 && cavern->annotations[0].tile_x == 3);
    }
    CHECK(find_child(&set, result.root_id, "Ice Lair") != NULL);
    CHECK(find_child(&set, 0, "Dungeons") != NULL);
    annotation_set_release(&imported);
    annotation_set_release(&set);
}

static void test_add_with_taken_name_changes_nothing(void) {
    AnnotationSet set, imported;
    AnnotationImportResult result;
    int incoming_root;
    annotation_set_init(&set);
    annotation_set_init(&imported);
    annotation_category_add(&set, "Dungeons", 1);
    incoming_root = annotation_category_add(&imported, "DUNGEONS", 4);
    annotation_category_add_under(&imported, incoming_root, "Cold Cavern", 2);

    result = annotation_import_add(&set, &imported, NULL);
    CHECK(result.status == ANNOTATION_IMPORT_NAME_TAKEN);
    result = annotation_import_add(&set, &imported, "dungeons");
    CHECK(result.status == ANNOTATION_IMPORT_NAME_TAKEN);
    CHECK(set.category_count == 1);
    CHECK(imported.category_count == 2);
    CHECK(strcmp(imported.categories[1].name, "Cold Cavern") == 0);
    annotation_set_release(&imported);
    annotation_set_release(&set);
}

static void test_add_gives_duplicate_siblings_unique_names(void) {
    AnnotationSet set, imported, merge_set, merge_import;
    AnnotationImportResult result;
    int incoming_root, local_root;
    annotation_set_init(&set);
    annotation_set_init(&imported);
    incoming_root = annotation_category_add(&imported, "Dungeons", 0);
    annotation_category_add_under(&imported, incoming_root, "Cold Cavern", 0);
    annotation_category_add_under(&imported, incoming_root, "Other", 0);
    strcpy(imported.categories[2].name, "Cold Cavern");

    result = annotation_import_add(&set, &imported, NULL);
    CHECK(result.status == ANNOTATION_IMPORT_OK);
    CHECK(count_children(&set, result.root_id) == 2);
    CHECK(find_child(&set, result.root_id, "Cold Cavern") != NULL);
    CHECK(find_child(&set, result.root_id, "Cold Cavern (2)") != NULL);
    annotation_set_release(&imported);
    annotation_set_release(&set);

    annotation_set_init(&merge_set);
    annotation_set_init(&merge_import);
    local_root = annotation_category_add(&merge_set, "Dungeons", 0);
    incoming_root = annotation_category_add(&merge_import, "Dungeons", 0);
    annotation_category_add_under(&merge_import, incoming_root, "Cold Cavern", 0);
    annotation_category_add_under(&merge_import, incoming_root, "Other", 0);
    strcpy(merge_import.categories[2].name, "Cold Cavern");

    result = annotation_import_merge(&merge_set, &merge_import, local_root);
    CHECK(result.status == ANNOTATION_IMPORT_OK);
    CHECK(count_children(&merge_set, local_root) == 2);
    CHECK(find_child(&merge_set, local_root, "Cold Cavern") != NULL);
    CHECK(find_child(&merge_set, local_root, "Cold Cavern (2)") != NULL);
    annotation_set_release(&merge_import);
    annotation_set_release(&merge_set);
}

static void test_merge_matches_by_name_and_keeps_local_settings(void) {
    AnnotationSet set, imported;
    AnnotationImportResult result;
    AnnotationCategory *dungeons, *cavern, *lair;
    int local_root, local_cavern, incoming_root, incoming_cavern, incoming_lair;
    annotation_set_init(&set);
    annotation_set_init(&imported);
    local_root = annotation_category_add(&set, "Dungeons", 1);
    local_cavern = annotation_category_add_under(&set, local_root, "Cold Cavern", 1);
    annotation_category_find(&set, local_cavern)->visible = 0;
    annotation_add_point(&set, local_cavern, ANNOTATION_PIN, 10, 10, "Old");
    incoming_root = annotation_category_add(&imported, "Dungeons", 4);
    incoming_cavern = annotation_category_add_under(&imported, incoming_root, "cold cavern", 4);
    annotation_add_point(&imported, incoming_cavern, ANNOTATION_PIN, 20, 20, "New");
    incoming_lair = annotation_category_add_under(&imported, incoming_root, "Ice Lair", 5);
    annotation_add_point(&imported, incoming_lair, ANNOTATION_PIN, 30, 30, "Lair");

    CHECK(annotation_import_conflict(&set, &imported) == local_root);
    result = annotation_import_merge(&set, &imported, local_root);
    CHECK(result.status == ANNOTATION_IMPORT_OK);
    CHECK(result.root_id == local_root);
    CHECK(set.category_count == 3);
    dungeons = annotation_category_find(&set, local_root);
    CHECK(dungeons != NULL && dungeons->colour_index == 1);
    cavern = annotation_category_find(&set, local_cavern);
    CHECK(cavern != NULL);
    if (cavern != NULL) {
        CHECK(strcmp(cavern->name, "Cold Cavern") == 0);
        CHECK(cavern->visible == 0);
        CHECK(cavern->colour_index == 1);
        CHECK(cavern->annotation_count == 2);
        CHECK(cavern->annotation_count == 2 && cavern->annotations[1].tile_x == 20);
        CHECK(cavern->annotation_count == 2 && cavern->annotations[0].id != cavern->annotations[1].id);
    }
    lair = find_child(&set, local_root, "Ice Lair");
    CHECK(lair != NULL);
    if (lair != NULL) {
        CHECK(lair->colour_index == 5);
        CHECK(lair->annotation_count == 1);
    }
    CHECK(imported.categories[1].annotation_count == 0);
    annotation_set_release(&imported);
    annotation_set_release(&set);
}

static void test_merge_skips_identical_but_keeps_near_duplicates(void) {
    AnnotationSet set, imported;
    AnnotationImportResult result;
    AnnotationCategory *dungeons;
    int local_root, incoming_root, recoloured, recoloured_again;
    annotation_set_init(&set);
    annotation_set_init(&imported);
    local_root = annotation_category_add(&set, "Dungeons", 1);
    annotation_add_point(&set, local_root, ANNOTATION_PIN, 5, 5, "Gate");
    incoming_root = annotation_category_add(&imported, "Dungeons", 4);
    annotation_add_point(&imported, incoming_root, ANNOTATION_PIN, 5, 5, "Gate");
    recoloured = annotation_add_point(&imported, incoming_root, ANNOTATION_PIN, 5, 5, "Gate");
    annotation_set_colour(&imported, recoloured, 2);
    recoloured_again = annotation_add_point(&imported, incoming_root, ANNOTATION_PIN, 5, 5, "Gate");
    annotation_set_colour(&imported, recoloured_again, 2);

    result = annotation_import_merge(&set, &imported, local_root);
    CHECK(result.status == ANNOTATION_IMPORT_OK);
    dungeons = annotation_category_find(&set, local_root);
    CHECK(dungeons != NULL && dungeons->annotation_count == 2);
    CHECK(dungeons != NULL && dungeons->annotation_count == 2 && dungeons->annotations[1].colour_index == 2);
    CHECK(imported.categories[0].annotation_count == 2);
    annotation_set_release(&imported);
    annotation_set_release(&set);
}

static void test_merge_over_annotation_limit_changes_nothing(void) {
    AnnotationSet set, imported;
    AnnotationImportResult result;
    int local_root, local_cavern, incoming_root, incoming_cavern, index;
    annotation_set_init(&set);
    annotation_set_init(&imported);
    local_root = annotation_category_add(&set, "Dungeons", 1);
    local_cavern = annotation_category_add_under(&set, local_root, "Cold Cavern", 1);
    for (index = 0; index < ANNOTATION_MAX_PER_CATEGORY - 1; index++) {
        annotation_add_point(&set, local_cavern, ANNOTATION_PIN, index, 0, "Pin");
    }
    incoming_root = annotation_category_add(&imported, "Dungeons", 4);
    incoming_cavern = annotation_category_add_under(&imported, incoming_root, "cold cavern", 4);
    annotation_add_point(&imported, incoming_cavern, ANNOTATION_PIN, 1, 100, "New");
    annotation_add_point(&imported, incoming_cavern, ANNOTATION_PIN, 2, 100, "New");
    annotation_category_add_under(&imported, incoming_root, "Ice Lair", 5);

    result = annotation_import_merge(&set, &imported, local_root);
    CHECK(result.status == ANNOTATION_IMPORT_CATEGORY_FULL);
    CHECK(strcmp(result.category_name, "Cold Cavern") == 0);
    CHECK(set.category_count == 2);
    CHECK(annotation_category_find(&set, local_cavern)->annotation_count == ANNOTATION_MAX_PER_CATEGORY - 1);
    CHECK(imported.category_count == 3);
    CHECK(imported.categories[1].annotation_count == 2);
    annotation_set_release(&imported);
    annotation_set_release(&set);
}

static void test_merge_counts_every_match_against_the_limit(void) {
    AnnotationSet set, imported;
    AnnotationImportResult result;
    int local_root, local_cavern, incoming_root, first_cavern, second_cavern, index;
    annotation_set_init(&set);
    annotation_set_init(&imported);
    local_root = annotation_category_add(&set, "Dungeons", 1);
    local_cavern = annotation_category_add_under(&set, local_root, "Cold Cavern", 1);
    for (index = 0; index < ANNOTATION_MAX_PER_CATEGORY - 1; index++) {
        annotation_add_point(&set, local_cavern, ANNOTATION_PIN, index, 0, "Pin");
    }
    incoming_root = annotation_category_add(&imported, "Dungeons", 4);
    first_cavern = annotation_category_add_under(&imported, incoming_root, "Cold Cavern", 4);
    second_cavern = annotation_category_add_under(&imported, incoming_root, "Other", 4);
    strcpy(imported.categories[2].name, "cold cavern");
    annotation_add_point(&imported, first_cavern, ANNOTATION_PIN, 1, 100, "New");
    annotation_add_point(&imported, second_cavern, ANNOTATION_PIN, 2, 100, "New");

    result = annotation_import_merge(&set, &imported, local_root);
    CHECK(result.status == ANNOTATION_IMPORT_CATEGORY_FULL);
    CHECK(strcmp(result.category_name, "Cold Cavern") == 0);
    CHECK(annotation_category_find(&set, local_cavern)->annotation_count == ANNOTATION_MAX_PER_CATEGORY - 1);
    annotation_set_release(&imported);
    annotation_set_release(&set);
}

static void test_merge_over_depth_changes_nothing(void) {
    AnnotationSet set, deep_import, shallow_import;
    AnnotationImportResult result;
    int parent_id = 0, level, incoming_root, incoming_child;
    annotation_set_init(&set);
    annotation_set_init(&deep_import);
    annotation_set_init(&shallow_import);
    for (level = 1; level <= 7; level++) parent_id = annotation_category_add_under(&set, parent_id, "Level", 0);
    CHECK(annotation_category_depth(&set, parent_id) == 7);
    incoming_root = annotation_category_add(&deep_import, "Level", 0);
    incoming_child = annotation_category_add_under(&deep_import, incoming_root, "Outer", 0);
    annotation_category_add_under(&deep_import, incoming_child, "Inner", 0);

    result = annotation_import_merge(&set, &deep_import, parent_id);
    CHECK(result.status == ANNOTATION_IMPORT_TOO_DEEP);
    CHECK(set.category_count == 7);
    CHECK(deep_import.category_count == 3);

    incoming_root = annotation_category_add(&shallow_import, "Level", 0);
    annotation_category_add_under(&shallow_import, incoming_root, "Outer", 0);
    result = annotation_import_merge(&set, &shallow_import, parent_id);
    CHECK(result.status == ANNOTATION_IMPORT_OK);
    CHECK(set.category_count == 8);
    annotation_set_release(&shallow_import);
    annotation_set_release(&deep_import);
    annotation_set_release(&set);
}

static void test_merge_over_category_cap_changes_nothing(void) {
    AnnotationSet set, imported;
    AnnotationImportResult result;
    char name[ANNOTATION_CATEGORY_NAME_MAX];
    int local_root, incoming_root, index;
    annotation_set_init(&set);
    annotation_set_init(&imported);
    local_root = annotation_category_add(&set, "Dungeons", 0);
    for (index = 1; index < ANNOTATION_MAX_CATEGORIES - 1; index++) {
        snprintf(name, sizeof(name), "Category %d", index);
        annotation_category_add(&set, name, 0);
    }
    CHECK(set.category_count == ANNOTATION_MAX_CATEGORIES - 1);
    incoming_root = annotation_category_add(&imported, "Dungeons", 0);
    annotation_category_add_under(&imported, incoming_root, "New A", 0);
    annotation_category_add_under(&imported, incoming_root, "New B", 0);

    result = annotation_import_merge(&set, &imported, local_root);
    CHECK(result.status == ANNOTATION_IMPORT_TOO_MANY_CATEGORIES);
    CHECK(set.category_count == ANNOTATION_MAX_CATEGORIES - 1);
    CHECK(imported.category_count == 3);
    annotation_set_release(&imported);
    annotation_set_release(&set);
}

int main(void) {
    test_conflict_prefers_exact_then_first_case_insensitive();
    test_add_with_new_name();
    test_add_with_taken_name_changes_nothing();
    test_add_gives_duplicate_siblings_unique_names();
    test_merge_matches_by_name_and_keeps_local_settings();
    test_merge_skips_identical_but_keeps_near_duplicates();
    test_merge_over_annotation_limit_changes_nothing();
    test_merge_counts_every_match_against_the_limit();
    test_merge_over_depth_changes_nothing();
    test_merge_over_category_cap_changes_nothing();
    if (failures != 0) {
        printf("%d annotation import check(s) failed\n", failures);
        return 1;
    }
    printf("All annotation import tests passed\n");
    return 0;
}
