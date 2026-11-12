#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/annotations.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static AnnotationCategory make_detached_category(const char *name) {
    AnnotationCategory category;
    memset(&category, 0, sizeof(category));
    strcpy(category.name, name);
    category.visible = 1;
    category.annotations = calloc(1, sizeof(Annotation));
    category.annotation_count = 1;
    category.annotations[0].id = 77;
    category.annotations[0].kind = ANNOTATION_PIN;
    category.annotations[0].tile_x = 5;
    category.annotations[0].tile_y = 6;
    return category;
}

static void test_ids_are_unique_and_start_at_one(void) {
    AnnotationSet set;
    int category_id, first_pin, second_pin;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Keys", 0);
    CHECK(category_id == 1);
    first_pin = annotation_add_point(&set, category_id, ANNOTATION_PIN, 10, 20, "a");
    CHECK(first_pin == 2);
    CHECK(annotation_remove(&set, first_pin) == 1);
    second_pin = annotation_add_point(&set, category_id, ANNOTATION_PIN, 10, 20, "b");
    CHECK(second_pin == 3);
    annotation_set_release(&set);
}

static void test_add_point_rejects_unknown_category_and_out_of_range(void) {
    AnnotationSet set;
    int category_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Keys", 0);
    CHECK(annotation_add_point(&set, 999, ANNOTATION_PIN, 1, 1, "x") == 0);
    CHECK(annotation_add_point(&set, category_id, ANNOTATION_PIN, 2048, 1, "x") == 0);
    CHECK(annotation_add_point(&set, category_id, ANNOTATION_PIN, 1, -1, "x") == 0);
    annotation_set_release(&set);
}

static void test_text_needs_a_label(void) {
    AnnotationSet set;
    Annotation edited;
    int category_id, text_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Notes", 0);
    CHECK(annotation_add_point(&set, category_id, ANNOTATION_TEXT, 1, 1, "") == 0);
    CHECK(annotation_add_point(&set, category_id, ANNOTATION_TEXT, 1, 1, NULL) == 0);
    CHECK(annotation_add_point(&set, category_id, ANNOTATION_PIN, 1, 1, "") != 0);
    text_id = annotation_add_point(&set, category_id, ANNOTATION_TEXT, 1, 1, "Gate");
    CHECK(text_id != 0);
    CHECK(annotation_copy(&edited, annotation_find(&set, text_id, NULL)));
    edited.label[0] = '\0';
    CHECK(annotation_replace(&set, text_id, &edited) == 0);
    CHECK(strcmp(annotation_find(&set, text_id, NULL)->label, "Gate") == 0);
    annotation_release(&edited);
    annotation_set_release(&set);
}

static void test_add_path_copies_segments(void) {
    AnnotationSet set;
    AnnotationSegment segments[2] = {{1, 2, 3, 4}, {3, 4, 5, 6}};
    int category_id, path_id;
    Annotation *found;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Routes", 1);
    path_id = annotation_add_path(&set, category_id, segments, 2, "road");
    CHECK(path_id != 0);
    segments[0].start_x = 900;
    segments[1].end_y = 901;
    found = annotation_find(&set, path_id, NULL);
    CHECK(found != NULL);
    if (found != NULL) {
        CHECK(found->segment_count == 2);
        CHECK(found->segments[0].start_x == 1);
        CHECK(found->segments[1].end_y == 6);
    }
    CHECK(annotation_add_path(&set, category_id, segments, 0, "empty") == 0);
    annotation_set_release(&set);
}

static void test_per_category_limit(void) {
    AnnotationSet set;
    int category_id, count;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Many", 0);
    for (count = 0; count < ANNOTATION_MAX_PER_CATEGORY; count++) {
        CHECK(annotation_add_point(&set, category_id, ANNOTATION_PIN, 1, 1, "p") != 0);
    }
    CHECK(annotation_add_point(&set, category_id, ANNOTATION_PIN, 1, 1, "p") == 0);
    annotation_set_release(&set);
}

static void test_adopt_renames_duplicate_category(void) {
    AnnotationSet set;
    AnnotationCategory first = make_detached_category("Keys");
    AnnotationCategory second = make_detached_category("Keys");
    int first_id, second_id;
    AnnotationCategory *adopted;
    annotation_set_init(&set);
    annotation_category_add(&set, "Keys", 0);
    first_id = annotation_category_adopt(&set, &first);
    adopted = annotation_category_find(&set, first_id);
    CHECK(adopted != NULL);
    if (adopted != NULL) {
        CHECK(strcmp(adopted->name, "Keys (2)") == 0);
        CHECK(adopted->annotations[0].id != 0);
        CHECK(adopted->annotations[0].id != 77);
        CHECK(adopted->visible == 1);
    }
    CHECK(first.annotations == NULL);
    second_id = annotation_category_adopt(&set, &second);
    adopted = annotation_category_find(&set, second_id);
    CHECK(adopted != NULL);
    if (adopted != NULL) CHECK(strcmp(adopted->name, "Keys (3)") == 0);
    annotation_set_release(&set);
}

static void test_long_utf8_label_is_cut_on_character_boundary(void) {
    AnnotationSet set;
    char label[64];
    int category_id, pin_id;
    Annotation *found;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Text", 0);
    memset(label, 'a', 46);
    strcpy(label + 46, "\xC3\xA9");
    pin_id = annotation_add_point(&set, category_id, ANNOTATION_TEXT, 3, 3, label);
    found = annotation_find(&set, pin_id, NULL);
    CHECK(found != NULL);
    if (found != NULL) {
        CHECK(strlen(found->label) == 46);
        CHECK(strspn(found->label, "a") == 46);
    }
    memset(label, 'b', 47);
    label[47] = '\0';
    pin_id = annotation_add_point(&set, category_id, ANNOTATION_TEXT, 3, 3, label);
    found = annotation_find(&set, pin_id, NULL);
    CHECK(found != NULL);
    if (found != NULL) CHECK(strlen(found->label) == 47);
    annotation_set_release(&set);
}

static void test_copy_and_replace_round_trip(void) {
    AnnotationSet set;
    AnnotationSegment segments[2] = {{1, 2, 3, 4}, {3, 4, 5, 6}};
    int category_id, path_id;
    Annotation edited;
    Annotation *stored;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Routes", 0);
    path_id = annotation_add_path(&set, category_id, segments, 2, "old");
    stored = annotation_find(&set, path_id, NULL);
    CHECK(stored != NULL);
    CHECK(annotation_copy(&edited, stored) == 1);
    edited.segment_count = 1;
    strcpy(edited.label, "new");
    CHECK(annotation_replace(&set, path_id, &edited) == 1);
    stored = annotation_find(&set, path_id, NULL);
    CHECK(stored != NULL);
    if (stored != NULL) {
        CHECK(stored->id == path_id);
        CHECK(stored->segment_count == 1);
        CHECK(strcmp(stored->label, "new") == 0);
        CHECK(stored->kind == ANNOTATION_PATH);
    }
    annotation_release(&edited);
    CHECK(edited.segments == NULL);
    annotation_release(&edited);
    annotation_set_release(&set);
}

static void test_remove_category_removes_its_annotations(void) {
    AnnotationSet set;
    int keep_id, doomed_id, pin_id;
    annotation_set_init(&set);
    keep_id = annotation_category_add(&set, "Keep", 0);
    doomed_id = annotation_category_add(&set, "Doomed", 1);
    pin_id = annotation_add_point(&set, doomed_id, ANNOTATION_PIN, 4, 4, "x");
    CHECK(set.category_count == 2);
    CHECK(annotation_category_remove(&set, doomed_id) == 1);
    CHECK(annotation_find(&set, pin_id, NULL) == NULL);
    CHECK(set.category_count == 1);
    CHECK(annotation_category_find(&set, keep_id) != NULL);
    annotation_set_release(&set);
}

static void test_changed_flag_set_on_mutation(void) {
    AnnotationSet set;
    int category_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Keys", 0);
    set.changed = 0;
    annotation_add_point(&set, category_id, ANNOTATION_PIN, 1, 1, "x");
    CHECK(set.changed == 1);
    annotation_set_release(&set);
}

static void test_palette_wraps_invalid_index(void) {
    CHECK(annotation_palette_rgb(1) == 0xFFD34D);
    CHECK(annotation_palette_rgb(99) == 0x4FC3FF);
}

static void test_new_category_shows_labels(void) {
    AnnotationSet set;
    int category_id;

    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Keys", 0);
    CHECK(annotation_category_find(&set, category_id) != NULL);
    if (annotation_category_find(&set, category_id) != NULL) {
        CHECK(annotation_category_find(&set, category_id)->show_labels == 1);
    }
    annotation_set_release(&set);
}

static void test_count_kind(void) {
    AnnotationSet set;
    AnnotationSegment segment = {1, 1, 2, 2};
    int category_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Mixed", 0);
    annotation_add_point(&set, category_id, ANNOTATION_PIN, 1, 1, "a");
    annotation_add_point(&set, category_id, ANNOTATION_PIN, 2, 2, "b");
    annotation_add_point(&set, category_id, ANNOTATION_TEXT, 3, 3, "c");
    annotation_add_path(&set, category_id, &segment, 1, "d");
    CHECK(annotation_category_count_kind(annotation_category_find(&set, category_id), ANNOTATION_PIN) == 2);
    CHECK(annotation_category_count_kind(annotation_category_find(&set, category_id), ANNOTATION_TEXT) == 1);
    CHECK(annotation_category_count_kind(annotation_category_find(&set, category_id), ANNOTATION_PATH) == 1);
    annotation_set_release(&set);
}

static void fill_long_path(AnnotationSegment *segments, const int segment_count) {
    int index;
    for (index = 0; index < segment_count; index++) {
        segments[index].start_x = (unsigned short) index;
        segments[index].start_y = 1;
        segments[index].end_x = (unsigned short) (index + 1);
        segments[index].end_y = 1;
    }
}

static void test_segment_total_per_category_is_capped(void) {
    AnnotationSet set;
    static AnnotationSegment segments[ANNOTATION_MAX_SEGMENTS];
    AnnotationSegment single = {1, 1, 2, 2};
    int category_id, other_id, path_index;
    annotation_set_init(&set);
    fill_long_path(segments, ANNOTATION_MAX_SEGMENTS);
    category_id = annotation_category_add(&set, "Flood", 0);
    other_id = annotation_category_add(&set, "Other", 0);
    for (path_index = 0; path_index < ANNOTATION_MAX_SEGMENTS_PER_CATEGORY / ANNOTATION_MAX_SEGMENTS; path_index++) {
        CHECK(annotation_add_path(&set, category_id, segments, ANNOTATION_MAX_SEGMENTS, "flood") != 0);
    }
    CHECK(annotation_category_segment_total(annotation_category_find(&set, category_id)) ==
          ANNOTATION_MAX_SEGMENTS_PER_CATEGORY);
    CHECK(annotation_add_path(&set, category_id, &single, 1, "one more") == 0);
    CHECK(annotation_add_path(&set, other_id, &single, 1, "elsewhere") != 0);
    annotation_set_release(&set);
}

static void test_replace_refuses_growing_past_segment_total(void) {
    AnnotationSet set;
    static AnnotationSegment segments[ANNOTATION_MAX_SEGMENTS];
    AnnotationSegment pair[2] = {{1, 1, 2, 2}, {2, 2, 3, 3}};
    Annotation edited;
    Annotation *stored;
    int category_id, small_id, path_index;
    annotation_set_init(&set);
    fill_long_path(segments, ANNOTATION_MAX_SEGMENTS);
    category_id = annotation_category_add(&set, "Flood", 0);
    for (path_index = 0; path_index < ANNOTATION_MAX_SEGMENTS_PER_CATEGORY / ANNOTATION_MAX_SEGMENTS - 1;
         path_index++) {
        CHECK(annotation_add_path(&set, category_id, segments, ANNOTATION_MAX_SEGMENTS, "flood") != 0);
    }
    CHECK(annotation_add_path(&set, category_id, segments, ANNOTATION_MAX_SEGMENTS - 1, "almost") != 0);
    small_id = annotation_add_path(&set, category_id, pair, 1, "small");
    CHECK(small_id != 0);
    stored = annotation_find(&set, small_id, NULL);
    CHECK(stored != NULL);
    if (stored == NULL) {
        annotation_set_release(&set);
        return;
    }
    CHECK(annotation_copy(&edited, stored) == 1);
    free(edited.segments);
    edited.segments = pair;
    edited.segment_count = 2;
    CHECK(annotation_replace(&set, small_id, &edited) == 0);
    edited.segment_count = 1;
    strcpy(edited.label, "renamed");
    CHECK(annotation_replace(&set, small_id, &edited) == 1);
    annotation_set_release(&set);
}

int main(void) {
    test_ids_are_unique_and_start_at_one();
    test_add_point_rejects_unknown_category_and_out_of_range();
    test_text_needs_a_label();
    test_add_path_copies_segments();
    test_per_category_limit();
    test_adopt_renames_duplicate_category();
    test_long_utf8_label_is_cut_on_character_boundary();
    test_copy_and_replace_round_trip();
    test_remove_category_removes_its_annotations();
    test_changed_flag_set_on_mutation();
    test_palette_wraps_invalid_index();
    test_count_kind();
    test_new_category_shows_labels();
    test_segment_total_per_category_is_capped();
    test_replace_refuses_growing_past_segment_total();
    if (failures != 0) {
        printf("%d annotations check(s) failed\n", failures);
        return 1;
    }
    printf("All annotations tests passed\n");
    return 0;
}
