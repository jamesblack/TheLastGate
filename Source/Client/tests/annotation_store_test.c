#include <stdio.h>
#include <string.h>

#include <zlib.h>

#include "game/annotation_codec.h"
#include "game/annotation_store.h"
#include "game/annotations.h"
#include "game/server_file.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define TEST_PATH "annotation_store_test.dat"
#define BAD_PATH "annotation_store_test.dat.bad"

static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    fclose(file);
    return 1;
}

static void remove_test_files(void) {
    remove(TEST_PATH);
    remove(BAD_PATH);
    remove(TEST_PATH ".tmp");
}

static void test_server_file_path(void) {
    char path[256];

    server_file_build_path(path, sizeof(path), "C:/p/", "TLGAnnotations_", "bad:host", 5555);
    CHECK(strcmp(path, "C:/p/TLGAnnotations_bad_host_5555.dat") == 0);
}

static void fill_sample(AnnotationSet *set) {
    AnnotationSegment segments[3] = {{10, 20, 30, 40}, {30, 40, 50, 60}, {50, 60, 70, 90}};
    const int first = annotation_category_add(set, "Keys", 2);
    const int second = annotation_category_add(set, "Route", 5);

    annotation_add_point(set, first, ANNOTATION_PIN, 100, 200, "Red key");
    annotation_add_point(set, first, ANNOTATION_TEXT, 300, 400, "Beware");
    annotation_add_path(set, second, segments, 3, "Maze");
    set->categories[0].show_labels = 0;
    set->categories[1].visible = 0;
    set->show_on_minimap = 1;
}

static void test_save_load_round_trip(void) {
    AnnotationSet saved;
    AnnotationSet loaded;
    AnnotationCategory *keys;
    AnnotationCategory *route;

    remove_test_files();
    annotation_set_init(&saved);
    annotation_set_init(&loaded);
    fill_sample(&saved);

    CHECK(annotation_store_save(TEST_PATH, &saved) == 1);
    CHECK(annotation_store_load(TEST_PATH, &loaded) == 1);
    CHECK(loaded.changed == 0);
    CHECK(loaded.show_on_minimap == 1);
    CHECK(loaded.category_count == 2);
    if (loaded.category_count == 2) {
        keys = &loaded.categories[0];
        route = &loaded.categories[1];
        CHECK(strcmp(keys->name, "Keys") == 0);
        CHECK(strcmp(route->name, "Route") == 0);
        CHECK(keys->visible == 1);
        CHECK(route->visible == 0);
        CHECK(keys->show_labels == 0);
        CHECK(route->show_labels == 1);
        CHECK(keys->colour_index == 2);
        CHECK(route->colour_index == 5);
        CHECK(keys->annotation_count == 2);
        CHECK(route->annotation_count == 1);
        if (keys->annotation_count == 2) {
            CHECK(keys->annotations[0].kind == ANNOTATION_PIN);
            CHECK(strcmp(keys->annotations[0].label, "Red key") == 0);
            CHECK(keys->annotations[0].tile_x == 100 && keys->annotations[0].tile_y == 200);
            CHECK(keys->annotations[1].kind == ANNOTATION_TEXT);
            CHECK(strcmp(keys->annotations[1].label, "Beware") == 0);
            CHECK(keys->annotations[1].tile_x == 300 && keys->annotations[1].tile_y == 400);
        }
        if (route->annotation_count == 1) {
            CHECK(route->annotations[0].kind == ANNOTATION_PATH);
            CHECK(strcmp(route->annotations[0].label, "Maze") == 0);
            CHECK(route->annotations[0].segment_count == 3);
            if (route->annotations[0].segment_count == 3) {
                CHECK(route->annotations[0].segments[0].start_x == 10);
                CHECK(route->annotations[0].segments[2].end_y == 90);
            }
        }
    }

    annotation_set_release(&saved);
    annotation_set_release(&loaded);
    remove_test_files();
}

static void test_missing_file_returns_zero(void) {
    AnnotationSet set;

    remove_test_files();
    annotation_set_init(&set);
    CHECK(annotation_store_load(TEST_PATH, &set) == 0);
    CHECK(set.category_count == 0);
    annotation_set_release(&set);
}

static void test_damaged_file_is_moved_aside(void) {
    AnnotationSet set;
    FILE *file;
    char contents[16] = {0};

    remove_test_files();
    file = fopen(BAD_PATH, "wb");
    if (file != NULL) {
        fputs("older", file);
        fclose(file);
    }
    file = fopen(TEST_PATH, "wb");
    if (file != NULL) {
        fputs("garbage", file);
        fclose(file);
    }

    annotation_set_init(&set);
    CHECK(annotation_store_load(TEST_PATH, &set) == -1);
    CHECK(set.category_count == 0);
    CHECK(!file_exists(TEST_PATH));
    CHECK(file_exists(BAD_PATH));
    file = fopen(BAD_PATH, "rb");
    if (file != NULL) {
        fread(contents, 1, sizeof(contents) - 1, file);
        fclose(file);
    }
    CHECK(strcmp(contents, "garbage") == 0);
    annotation_set_release(&set);
    remove_test_files();
}

static void test_save_clears_changed(void) {
    AnnotationSet set;

    remove_test_files();
    annotation_set_init(&set);
    fill_sample(&set);
    CHECK(set.changed == 1);
    CHECK(annotation_store_save(TEST_PATH, &set) == 1);
    CHECK(set.changed == 0);
    annotation_set_release(&set);
    remove_test_files();
}

static void write_word(FILE *file, const unsigned int value) {
    const unsigned char bytes[4] = {(unsigned char) (value & 0xff), (unsigned char) ((value >> 8) & 0xff),
                                    (unsigned char) ((value >> 16) & 0xff), (unsigned char) ((value >> 24) & 0xff)};
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void write_version_one_file(const AnnotationCategory *category) {
    ByteBuffer payload = {NULL, 0, 0};
    const unsigned char show_on_minimap = 1;
    const unsigned char visible = 1;
    unsigned char compressed[4096];
    uLongf compressed_size = sizeof(compressed);
    FILE *file;

    byte_buffer_append(&payload, &show_on_minimap, 1);
    annotation_varint_write(&payload, 1);
    byte_buffer_append(&payload, &visible, 1);
    annotation_category_encode(category, &payload);
    compress2(compressed, &compressed_size, payload.bytes, (uLong) payload.size, 9);

    file = fopen(TEST_PATH, "wb");
    if (file != NULL) {
        fwrite("TLGA", 1, 4, file);
        write_word(file, 1);
        write_word(file, (unsigned int) payload.size);
        write_word(file, (unsigned int) compressed_size);
        fwrite(compressed, 1, compressed_size, file);
        fclose(file);
    }
    byte_buffer_release(&payload);
}

static void test_version_one_file_loads_with_labels_shown(void) {
    AnnotationSet source;
    AnnotationSet loaded;
    int category_id;

    remove_test_files();
    annotation_set_init(&source);
    category_id = annotation_category_add(&source, "Old", 3);
    annotation_add_point(&source, category_id, ANNOTATION_PIN, 12, 34, "Gate");
    write_version_one_file(&source.categories[0]);

    annotation_set_init(&loaded);
    CHECK(annotation_store_load(TEST_PATH, &loaded) == 1);
    CHECK(loaded.category_count == 1);
    if (loaded.category_count == 1) {
        CHECK(strcmp(loaded.categories[0].name, "Old") == 0);
        CHECK(loaded.categories[0].show_labels == 1);
        CHECK(loaded.categories[0].annotation_count == 1);
    }
    CHECK(!file_exists(TEST_PATH ".bad"));

    annotation_set_release(&source);
    annotation_set_release(&loaded);
    remove_test_files();
}

int main(void) {
    test_server_file_path();
    test_save_load_round_trip();
    test_missing_file_returns_zero();
    test_damaged_file_is_moved_aside();
    test_save_clears_changed();
    test_version_one_file_loads_with_labels_shown();
    if (failures != 0) {
        printf("%d annotation store check(s) failed\n", failures);
        return 1;
    }
    printf("All annotation store tests passed\n");
    return 0;
}
