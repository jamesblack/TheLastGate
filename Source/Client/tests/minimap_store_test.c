#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zlib.h>

#include "game/minimap_store.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define TEST_PATH "minimap_store_test.dat"
#define TEST_WIDTH 64
#define TEST_HEIGHT 32

static MinimapColour saved_tiles[TEST_WIDTH * TEST_HEIGHT];
static MinimapColour loaded_tiles[TEST_WIDTH * TEST_HEIGHT];

static MinimapTiles make_minimap(MinimapColour *tiles) {
    MinimapTiles minimap;
    minimap.tiles = tiles;
    minimap.width = TEST_WIDTH;
    minimap.height = TEST_HEIGHT;
    minimap.changed = 0;
    minimap.revision = 0;
    return minimap;
}

static MinimapColour tile_at(const MinimapTiles *minimap, const int tile_x, const int tile_y) {
    return minimap->tiles[tile_x * minimap->height + tile_y];
}

static void write_raw_file(const void *bytes, const size_t byte_count) {
    FILE *file = fopen(TEST_PATH, "wb");
    if (file == NULL) return;
    fwrite(bytes, 1, byte_count, file);
    fclose(file);
}

static void fill_loaded_with_garbage(void) {
    memset(loaded_tiles, 0x5a, sizeof(loaded_tiles));
}

static int loaded_is_blank(void) {
    int index;
    for (index = 0; index < TEST_WIDTH * TEST_HEIGHT; index++) {
        if (loaded_tiles[index] != 0) return 0;
    }
    return 1;
}

static void test_path_uses_host_and_port(void) {
    char path[256];

    minimap_store_build_path(path, sizeof(path), "C:/prefs/", "play.example.com", 5555);
    CHECK(strcmp(path, "C:/prefs/TLGMap_play.example.com_5555.dat") == 0);
}

static void test_path_replaces_unsafe_host_characters(void) {
    char path[256];

    minimap_store_build_path(path, sizeof(path), "", "bad:host/name\\x", 1);
    CHECK(strcmp(path, "TLGMap_bad_host_name_x_1.dat") == 0);
}

static void test_set_tile_marks_changed_only_on_difference(void) {
    MinimapTiles minimap;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    minimap = make_minimap(saved_tiles);

    minimap_store_set_tile(&minimap, 3, 4, 0x1234);
    CHECK(tile_at(&minimap, 3, 4) == 0x1234);
    CHECK(minimap.changed == 1);

    minimap.changed = 0;
    minimap_store_set_tile(&minimap, 3, 4, 0x1234);
    CHECK(minimap.changed == 0);

    minimap_store_set_tile(&minimap, 3, 4, 0x4321);
    CHECK(tile_at(&minimap, 3, 4) == 0x4321);
    CHECK(minimap.changed == 1);
}

static void test_set_tile_ignores_out_of_range(void) {
    MinimapTiles minimap;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    minimap = make_minimap(saved_tiles);

    minimap_store_set_tile(&minimap, -1, 0, 0x1111);
    minimap_store_set_tile(&minimap, TEST_WIDTH, 0, 0x1111);
    minimap_store_set_tile(&minimap, 0, TEST_HEIGHT, 0x1111);
    CHECK(minimap.changed == 0);
}

static void test_clear_box_wipes_only_the_box(void) {
    MinimapTiles minimap;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    minimap = make_minimap(saved_tiles);
    minimap_store_set_tile(&minimap, 1, 1, 0x0101);
    minimap_store_set_tile(&minimap, 2, 2, 0x0202);
    minimap_store_set_tile(&minimap, 5, 5, 0x0505);
    minimap.changed = 0;

    minimap_store_clear_box(&minimap, 1, 1, 2, 2);
    CHECK(tile_at(&minimap, 1, 1) == 0);
    CHECK(tile_at(&minimap, 2, 2) == 0);
    CHECK(tile_at(&minimap, 5, 5) == 0x0505);
    CHECK(minimap.changed == 1);
}

static void test_clear_box_is_clamped_to_the_map(void) {
    MinimapTiles minimap;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    minimap = make_minimap(saved_tiles);
    minimap_store_set_tile(&minimap, TEST_WIDTH - 1, TEST_HEIGHT - 1, 0x7777);

    minimap_store_clear_box(&minimap, TEST_WIDTH - 1, TEST_HEIGHT - 1, 1000, 1000);
    CHECK(tile_at(&minimap, TEST_WIDTH - 1, TEST_HEIGHT - 1) == 0);

    minimap_store_clear_box(&minimap, -10, -10, 5, 5);
}

static void test_clear_box_of_empty_area_is_not_a_change(void) {
    MinimapTiles minimap;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    minimap = make_minimap(saved_tiles);

    minimap_store_clear_box(&minimap, 0, 0, 4, 4);
    CHECK(minimap.changed == 0);
}

static void test_saved_map_loads_back(void) {
    MinimapTiles saved;
    MinimapTiles loaded;
    int index;

    for (index = 0; index < TEST_WIDTH * TEST_HEIGHT; index++) {
        saved_tiles[index] = (MinimapColour) (index % 7 == 0 ? 0x10203 * (unsigned int) index : 0);
    }
    saved = make_minimap(saved_tiles);
    saved.changed = 1;

    remove(TEST_PATH);
    CHECK(minimap_store_save(TEST_PATH, &saved) == 1);
    CHECK(saved.changed == 0);

    fill_loaded_with_garbage();
    loaded = make_minimap(loaded_tiles);
    loaded.changed = 1;
    CHECK(minimap_store_load(TEST_PATH, &loaded) == 1);
    CHECK(memcmp(saved_tiles, loaded_tiles, sizeof(saved_tiles)) == 0);
    CHECK(loaded.changed == 0);
}

static void test_missing_file_gives_blank_map(void) {
    MinimapTiles loaded;

    remove(TEST_PATH);
    fill_loaded_with_garbage();
    loaded = make_minimap(loaded_tiles);
    CHECK(minimap_store_load(TEST_PATH, &loaded) == 0);
    CHECK(loaded_is_blank());
}

static void test_garbage_file_gives_blank_map(void) {
    const char garbage[] = "this is not a minimap file at all";
    MinimapTiles loaded;

    write_raw_file(garbage, sizeof(garbage));
    fill_loaded_with_garbage();
    loaded = make_minimap(loaded_tiles);
    CHECK(minimap_store_load(TEST_PATH, &loaded) == 0);
    CHECK(loaded_is_blank());
}

static void test_truncated_file_gives_blank_map(void) {
    MinimapTiles saved;
    MinimapTiles loaded;
    FILE *file;
    char bytes[4096];
    size_t byte_count;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    saved_tiles[10] = 0x1234;
    saved = make_minimap(saved_tiles);
    CHECK(minimap_store_save(TEST_PATH, &saved) == 1);

    file = fopen(TEST_PATH, "rb");
    byte_count = file ? fread(bytes, 1, sizeof(bytes), file) : 0;
    if (file) fclose(file);
    CHECK(byte_count > 8);
    write_raw_file(bytes, byte_count - 4);

    fill_loaded_with_garbage();
    loaded = make_minimap(loaded_tiles);
    CHECK(minimap_store_load(TEST_PATH, &loaded) == 0);
    CHECK(loaded_is_blank());
}

static void test_different_map_size_gives_blank_map(void) {
    MinimapTiles saved;
    MinimapTiles loaded;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    saved_tiles[0] = 0x1234;
    saved = make_minimap(saved_tiles);
    CHECK(minimap_store_save(TEST_PATH, &saved) == 1);

    fill_loaded_with_garbage();
    loaded = make_minimap(loaded_tiles);
    loaded.width = TEST_HEIGHT;
    loaded.height = TEST_WIDTH;
    CHECK(minimap_store_load(TEST_PATH, &loaded) == 0);
    CHECK(loaded_is_blank());
}

static void test_wrong_version_gives_blank_map(void) {
    MinimapTiles saved;
    MinimapTiles loaded;
    FILE *file;
    char bytes[4096];
    size_t byte_count;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    saved = make_minimap(saved_tiles);
    CHECK(minimap_store_save(TEST_PATH, &saved) == 1);

    file = fopen(TEST_PATH, "rb");
    byte_count = file ? fread(bytes, 1, sizeof(bytes), file) : 0;
    if (file) fclose(file);
    CHECK(byte_count > 8);
    bytes[4] = (char) 0x7f; /* first byte of the format version */
    write_raw_file(bytes, byte_count);

    fill_loaded_with_garbage();
    loaded = make_minimap(loaded_tiles);
    CHECK(minimap_store_load(TEST_PATH, &loaded) == 0);
    CHECK(loaded_is_blank());
}

static void write_rgb555_file(const unsigned short *packed_tiles) {
    const char magic[4] = {'T', 'L', 'G', 'M'};
    const uLong packed_size = sizeof(unsigned short) * TEST_WIDTH * TEST_HEIGHT;
    unsigned char compressed[TEST_WIDTH * TEST_HEIGHT * 4];
    uLongf compressed_size = sizeof(compressed);
    uint32_t header[4];
    FILE *file;

    if (compress2(compressed, &compressed_size, (const Bytef *) packed_tiles, packed_size, Z_BEST_SPEED) != Z_OK) {
        return;
    }
    header[0] = 1;
    header[1] = TEST_WIDTH;
    header[2] = TEST_HEIGHT;
    header[3] = (uint32_t) compressed_size;
    file = fopen(TEST_PATH, "wb");
    if (file == NULL) return;
    fwrite(magic, sizeof(magic), 1, file);
    fwrite(header, sizeof(uint32_t), 4, file);
    fwrite(compressed, 1, compressed_size, file);
    fclose(file);
}

static void test_rgb555_file_loads_as_full_colour(void) {
    static unsigned short packed_tiles[TEST_WIDTH * TEST_HEIGHT];
    MinimapTiles loaded;

    memset(packed_tiles, 0, sizeof(packed_tiles));
    packed_tiles[0] = 0x7FFF;
    packed_tiles[5] = (unsigned short) ((4 << 10) | (4 << 5) | 4);
    packed_tiles[TEST_WIDTH * TEST_HEIGHT - 1] = (unsigned short) (31 << 10);
    write_rgb555_file(packed_tiles);

    fill_loaded_with_garbage();
    loaded = make_minimap(loaded_tiles);
    CHECK(minimap_store_load(TEST_PATH, &loaded) == 1);
    CHECK(loaded_tiles[0] == 0xFFFFFF);
    CHECK(loaded_tiles[1] == 0);
    CHECK(loaded_tiles[5] == 0x212121);
    CHECK(loaded_tiles[TEST_WIDTH * TEST_HEIGHT - 1] == 0xFF0000);
}

static void test_revision_counts_changes(void) {
    MinimapTiles minimap;
    unsigned int before;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    minimap = make_minimap(saved_tiles);

    minimap_store_set_tile(&minimap, 1, 1, 0x0101);
    CHECK(minimap.revision == 1);
    minimap_store_set_tile(&minimap, 1, 1, 0x0101);
    CHECK(minimap.revision == 1);

    remove(TEST_PATH);
    before = minimap.revision;
    minimap_store_load(TEST_PATH, &minimap);
    CHECK(minimap.revision == before + 1);
}

static void test_delete_removes_the_file(void) {
    MinimapTiles saved;
    FILE *file;

    memset(saved_tiles, 0, sizeof(saved_tiles));
    saved = make_minimap(saved_tiles);
    CHECK(minimap_store_save(TEST_PATH, &saved) == 1);
    CHECK(minimap_store_delete(TEST_PATH) == 1);

    file = fopen(TEST_PATH, "rb");
    CHECK(file == NULL);
    if (file) fclose(file);

    CHECK(minimap_store_delete(TEST_PATH) == 1);
}

int main(void) {
    test_path_uses_host_and_port();
    test_path_replaces_unsafe_host_characters();
    test_set_tile_marks_changed_only_on_difference();
    test_set_tile_ignores_out_of_range();
    test_clear_box_wipes_only_the_box();
    test_clear_box_is_clamped_to_the_map();
    test_clear_box_of_empty_area_is_not_a_change();
    test_saved_map_loads_back();
    test_missing_file_gives_blank_map();
    test_garbage_file_gives_blank_map();
    test_truncated_file_gives_blank_map();
    test_different_map_size_gives_blank_map();
    test_wrong_version_gives_blank_map();
    test_rgb555_file_loads_as_full_colour();
    test_revision_counts_changes();
    test_delete_removes_the_file();

    remove(TEST_PATH);

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All minimap store tests passed\n");
    return 0;
}
