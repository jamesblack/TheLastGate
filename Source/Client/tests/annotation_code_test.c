#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "game/annotation_code.h"
#include "game/annotation_codec.h"
#include "game/annotations.h"

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static const char base64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *code_from_bytes(const char *prefix, const unsigned char *bytes, const size_t size) {
    uLongf compressed_size = compressBound((uLong) size);
    unsigned char *compressed = malloc(compressed_size);
    char *code;
    size_t position, written;
    compress2(compressed, &compressed_size, bytes, (uLong) size, 9);
    code = malloc(strlen(prefix) + (size_t) compressed_size * 2 + 8);
    strcpy(code, prefix);
    written = strlen(code);
    for (position = 0; position < compressed_size; position += 3) {
        unsigned int group = (unsigned int) compressed[position] << 16;
        const size_t remaining = compressed_size - position;
        if (remaining > 1) group |= (unsigned int) compressed[position + 1] << 8;
        if (remaining > 2) group |= compressed[position + 2];
        code[written++] = base64_alphabet[(group >> 18) & 63];
        code[written++] = base64_alphabet[(group >> 12) & 63];
        code[written++] = remaining > 1 ? base64_alphabet[(group >> 6) & 63] : '=';
        code[written++] = remaining > 2 ? base64_alphabet[group & 63] : '=';
    }
    code[written] = '\0';
    free(compressed);
    return code;
}

static char *legacy_code_from_bytes(const ByteBuffer *buffer) {
    return code_from_bytes(ANNOTATION_CODE_LEGACY_PREFIX, buffer->bytes, buffer->size);
}

static AnnotationCodeStatus import_code(const char *code, AnnotationSet *imported) {
    annotation_set_init(imported);
    return annotation_code_import(code, imported);
}

static void check_tree_emptied(const AnnotationSet *tree) {
    CHECK(tree->category_count == 0);
    CHECK(tree->categories == NULL);
}

static void append_byte(ByteBuffer *buffer, const unsigned char value) {
    byte_buffer_append(buffer, &value, 1);
}

static void append_zigzag(ByteBuffer *buffer, const int value) {
    annotation_varint_write(buffer, ((unsigned int) value << 1) ^ (unsigned int) (value >> 31));
}

static void append_header(ByteBuffer *buffer, const char *name, const unsigned int annotation_count) {
    append_byte(buffer, (unsigned char) strlen(name));
    byte_buffer_append(buffer, name, strlen(name));
    append_byte(buffer, 0);
    annotation_varint_write(buffer, annotation_count);
}

static void test_varint_round_trip(void) {
    const unsigned int values[] = {0, 127, 128, 300, 2047, 65535};
    unsigned int index;
    for (index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
        ByteBuffer buffer = {0};
        size_t offset = 0;
        unsigned int decoded = 12345;
        annotation_varint_write(&buffer, values[index]);
        CHECK(annotation_varint_read(buffer.bytes, buffer.size, &offset, &decoded));
        CHECK(decoded == values[index]);
        CHECK(offset == buffer.size);
        byte_buffer_release(&buffer);
    }
}

static int build_sample_set(AnnotationSet *set) {
    AnnotationSegment segments[2] = {{100, 100, 100, 140}, {100, 140, 130, 140}};
    int category_id;
    annotation_set_init(set);
    category_id = annotation_category_add(set, "Stronghold keys", 2);
    annotation_add_point(set, category_id, ANNOTATION_PIN, 512, 498, "Silver key");
    annotation_add_point(set, category_id, ANNOTATION_PIN, 520, 505, "Gold key");
    annotation_add_point(set, category_id, ANNOTATION_TEXT, 500, 500, "Start");
    annotation_add_path(set, category_id, segments, 2, "Route to boss");
    return category_id;
}

static void check_sample_matches(const AnnotationCategory *category) {
    CHECK(strcmp(category->name, "Stronghold keys") == 0);
    CHECK(category->colour_index == 2);
    CHECK(category->show_labels == 1);
    CHECK(category->annotation_count == 4);
    if (category->annotation_count != 4) return;
    CHECK(category->annotations[0].kind == ANNOTATION_PIN);
    CHECK(strcmp(category->annotations[0].label, "Silver key") == 0);
    CHECK(category->annotations[0].tile_x == 512 && category->annotations[0].tile_y == 498);
    CHECK(category->annotations[1].tile_x == 520 && category->annotations[1].tile_y == 505);
    CHECK(strcmp(category->annotations[1].label, "Gold key") == 0);
    CHECK(category->annotations[2].kind == ANNOTATION_TEXT);
    CHECK(strcmp(category->annotations[2].label, "Start") == 0);
    CHECK(category->annotations[2].tile_x == 500 && category->annotations[2].tile_y == 500);
    CHECK(category->annotations[3].kind == ANNOTATION_PATH);
    CHECK(strcmp(category->annotations[3].label, "Route to boss") == 0);
    CHECK(category->annotations[3].tile_x == 100 && category->annotations[3].tile_y == 100);
    CHECK(category->annotations[3].segment_count == 2);
    if (category->annotations[3].segment_count != 2) return;
    CHECK(category->annotations[3].segments[0].start_x == 100 && category->annotations[3].segments[0].start_y == 100);
    CHECK(category->annotations[3].segments[0].end_x == 100 && category->annotations[3].segments[0].end_y == 140);
    CHECK(category->annotations[3].segments[1].start_x == 100 && category->annotations[3].segments[1].start_y == 140);
    CHECK(category->annotations[3].segments[1].end_x == 130 && category->annotations[3].segments[1].end_y == 140);
}

static void check_single_sample_imported(const AnnotationSet *imported) {
    CHECK(imported->category_count == 1);
    if (imported->category_count != 1) return;
    CHECK(imported->categories[0].parent_id == 0);
    check_sample_matches(&imported->categories[0]);
}

static void test_code_round_trip(void) {
    AnnotationSet sample, imported;
    const int category_id = build_sample_set(&sample);
    char *code = annotation_code_export(&sample, category_id);
    CHECK(code != NULL);
    if (code == NULL) {
        annotation_set_release(&sample);
        return;
    }
    CHECK(strncmp(code, "TLGMAP2:", 8) == 0);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_OK);
    check_single_sample_imported(&imported);
    if (imported.category_count == 1) {
        CHECK(imported.categories[0].visible == 1);
        CHECK(imported.categories[0].annotation_count > 0 &&
              imported.categories[0].annotations[0].colour_index == ANNOTATION_COLOUR_INHERIT);
    }
    annotation_set_release(&imported);
    annotation_set_release(&sample);
    free(code);
}

static void test_code_with_whitespace_imports(void) {
    AnnotationSet sample, imported;
    const int category_id = build_sample_set(&sample);
    char *code = annotation_code_export(&sample, category_id);
    const size_t length = strlen(code);
    char *spaced = malloc(length * 2 + 8);
    size_t position, written = 0;
    spaced[written++] = ' ';
    for (position = 0; position < length; position++) {
        spaced[written++] = code[position];
        if (position % 40 == 39) {
            spaced[written++] = ' ';
            spaced[written++] = '\n';
        }
    }
    spaced[written++] = ' ';
    spaced[written] = '\0';
    CHECK(import_code(spaced, &imported) == ANNOTATION_CODE_OK);
    check_single_sample_imported(&imported);
    annotation_set_release(&imported);
    annotation_set_release(&sample);
    free(spaced);
    free(code);
}

static void test_missing_prefix_is_not_a_code(void) {
    AnnotationSet imported;
    CHECK(import_code("hello", &imported) == ANNOTATION_CODE_NOT_A_CODE);
    check_tree_emptied(&imported);
    CHECK(import_code("TLGMAP3:eNoDAAAAAAE=", &imported) == ANNOTATION_CODE_NOT_A_CODE);
    check_tree_emptied(&imported);
}

static void test_truncated_code_is_damaged(void) {
    AnnotationSet sample, imported;
    const int category_id = build_sample_set(&sample);
    char *code = annotation_code_export(&sample, category_id);
    code[strlen(code) - 8] = '\0';
    CHECK(strchr(code, '=') == NULL);
    CHECK(strlen(code) % 4 == 0);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_DAMAGED);
    check_tree_emptied(&imported);
    code[strlen(code) - 2] = '\0';
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_DAMAGED);
    check_tree_emptied(&imported);
    annotation_set_release(&sample);
    free(code);
}

static void test_out_of_range_coordinate_is_damaged(void) {
    ByteBuffer buffer = {0};
    AnnotationSet imported;
    char *code;
    append_header(&buffer, "Bad", 1);
    append_byte(&buffer, ANNOTATION_PIN);
    append_byte(&buffer, 0);
    append_zigzag(&buffer, 3000);
    append_zigzag(&buffer, 0);
    code = legacy_code_from_bytes(&buffer);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_DAMAGED);
    check_tree_emptied(&imported);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_huge_delta_is_damaged(void) {
    ByteBuffer buffer = {0};
    AnnotationSet imported;
    char *code;
    append_header(&buffer, "Huge delta", 1);
    append_byte(&buffer, ANNOTATION_PIN);
    append_byte(&buffer, 0);
    annotation_varint_write(&buffer, 0xFFFFFFFEu);
    append_zigzag(&buffer, 0);
    code = legacy_code_from_bytes(&buffer);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_DAMAGED);
    check_tree_emptied(&imported);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_oversized_count_is_rejected(void) {
    ByteBuffer buffer = {0};
    AnnotationSet imported;
    char *code;
    append_header(&buffer, "Huge", 100000);
    code = legacy_code_from_bytes(&buffer);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_TOO_LARGE);
    check_tree_emptied(&imported);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_typical_maze_guide_fits_discord(void) {
    AnnotationSet set;
    AnnotationSegment segments[150];
    char label[16];
    int category_id, index, tile_x = 400, tile_y = 400;
    char *code;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Maze guide", 1);
    for (index = 0; index < 150; index++) {
        const int next_x = (index % 2 == 0) ? tile_x + 3 : tile_x;
        const int next_y = (index % 2 == 0) ? tile_y : tile_y + 2;
        segments[index].start_x = (unsigned short) tile_x;
        segments[index].start_y = (unsigned short) tile_y;
        segments[index].end_x = (unsigned short) next_x;
        segments[index].end_y = (unsigned short) next_y;
        tile_x = next_x;
        tile_y = next_y;
    }
    annotation_add_path(&set, category_id, segments, 150, "Maze");
    for (index = 1; index <= 12; index++) {
        sprintf(label, "Key %d", index);
        annotation_add_point(&set, category_id, ANNOTATION_PIN, 400 + index * 7, 400 + index * 5, label);
    }
    code = annotation_code_export(&set, category_id);
    CHECK(code != NULL);
    if (code != NULL) {
        CHECK(strlen(code) < ANNOTATION_CODE_DISCORD_LIMIT);
    }
    free(code);
    annotation_set_release(&set);
}

static char *code_for_flood(const int path_count) {
    ByteBuffer buffer = {0};
    char *code;
    int path_index, segment_index;
    append_header(&buffer, "Flood", (unsigned int) path_count);
    for (path_index = 0; path_index < path_count; path_index++) {
        append_byte(&buffer, ANNOTATION_PATH);
        append_byte(&buffer, 0);
        annotation_varint_write(&buffer, ANNOTATION_MAX_SEGMENTS);
        for (segment_index = 0; segment_index < ANNOTATION_MAX_SEGMENTS; segment_index++) {
            append_zigzag(&buffer, segment_index == 0 && path_index == 0 ? 0 : -1);
            append_zigzag(&buffer, 0);
            append_zigzag(&buffer, 1);
            append_zigzag(&buffer, 0);
        }
    }
    code = legacy_code_from_bytes(&buffer);
    byte_buffer_release(&buffer);
    return code;
}

static void test_segment_total_over_cap_is_too_large(void) {
    const int paths_at_cap = ANNOTATION_MAX_SEGMENTS_PER_CATEGORY / ANNOTATION_MAX_SEGMENTS;
    AnnotationSet imported;
    char *code = code_for_flood(paths_at_cap);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_OK);
    CHECK(imported.category_count == 1);
    annotation_set_release(&imported);
    free(code);

    code = code_for_flood(paths_at_cap + 1);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_TOO_LARGE);
    check_tree_emptied(&imported);
    free(code);
}

static void test_empty_category_name_becomes_imported(void) {
    ByteBuffer buffer = {0};
    AnnotationSet imported;
    char *code;
    append_header(&buffer, "", 1);
    append_byte(&buffer, ANNOTATION_PIN);
    append_byte(&buffer, 0);
    append_zigzag(&buffer, 10);
    append_zigzag(&buffer, 20);
    code = legacy_code_from_bytes(&buffer);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_OK);
    CHECK(imported.category_count == 1);
    if (imported.category_count == 1) {
        const AnnotationCategory *category = &imported.categories[0];
        CHECK(strcmp(category->name, "Imported") == 0);
        CHECK(category->annotation_count == 1);
        if (category->annotation_count == 1) CHECK(category->annotations[0].label[0] == '\0');
    }
    annotation_set_release(&imported);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_empty_text_label_is_damaged(void) {
    ByteBuffer buffer = {0};
    AnnotationSet imported;
    char *code;
    append_header(&buffer, "Notes", 1);
    append_byte(&buffer, ANNOTATION_TEXT);
    append_byte(&buffer, 0);
    append_zigzag(&buffer, 10);
    append_zigzag(&buffer, 20);
    code = legacy_code_from_bytes(&buffer);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_DAMAGED);
    check_tree_emptied(&imported);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_trailing_bytes_are_damaged(void) {
    ByteBuffer buffer = {0};
    AnnotationSet imported;
    char *code;
    append_header(&buffer, "Keys", 1);
    append_byte(&buffer, ANNOTATION_PIN);
    append_byte(&buffer, 0);
    append_zigzag(&buffer, 10);
    append_zigzag(&buffer, 20);
    append_byte(&buffer, 0);
    code = legacy_code_from_bytes(&buffer);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_DAMAGED);
    check_tree_emptied(&imported);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_frozen_version_one_code_still_imports(void) {
    const AnnotationSegment expected[3] = {{120, 200, 120, 215}, {120, 215, 135, 215}, {135, 215, 135, 230}};
    AnnotationSet imported;
    int segment_index;
    CHECK(import_code("TLGMAP1:eNpj8U6tLGZiYmQPSk1RyE6tPME4gZmZxTexKpVZg4FBDowYGOQAuoUHpg==", &imported) ==
          ANNOTATION_CODE_OK);
    CHECK(imported.category_count == 1);
    if (imported.category_count == 1) {
        const AnnotationCategory *category = &imported.categories[0];
        CHECK(strcmp(category->name, "Keys") == 0);
        CHECK(category->parent_id == 0);
        CHECK(category->colour_index == 2);
        CHECK(category->visible == 1);
        CHECK(category->show_labels == 1);
        CHECK(category->annotation_count == 2);
        if (category->annotation_count == 2) {
            const Annotation *pin = &category->annotations[0];
            const Annotation *path = &category->annotations[1];
            CHECK(pin->kind == ANNOTATION_PIN);
            CHECK(strcmp(pin->label, "Red key") == 0);
            CHECK(pin->tile_x == 100 && pin->tile_y == 200);
            CHECK(pin->colour_index == ANNOTATION_COLOUR_INHERIT);
            CHECK(path->kind == ANNOTATION_PATH);
            CHECK(strcmp(path->label, "Maze") == 0);
            CHECK(path->colour_index == ANNOTATION_COLOUR_INHERIT);
            CHECK(path->tile_x == 120 && path->tile_y == 200);
            CHECK(path->segment_count == 3);
            for (segment_index = 0; segment_index < 3 && segment_index < path->segment_count; segment_index++) {
                const AnnotationSegment *segment = &path->segments[segment_index];
                CHECK(segment->start_x == expected[segment_index].start_x);
                CHECK(segment->start_y == expected[segment_index].start_y);
                CHECK(segment->end_x == expected[segment_index].end_x);
                CHECK(segment->end_y == expected[segment_index].end_y);
            }
        }
    }
    annotation_set_release(&imported);
}

static void build_dungeon_set(AnnotationSet *set, int *dungeons_id, int *cavern_id) {
    const AnnotationSegment segments[2] = {{100, 100, 100, 140}, {100, 140, 130, 140}};
    int pin_id, towns_id;
    annotation_set_init(set);
    *dungeons_id = annotation_category_add(set, "Dungeons", 5);
    *cavern_id = annotation_category_add_under(set, *dungeons_id, "Cold Cavern", 1);
    annotation_category_find(set, *dungeons_id)->show_labels = 0;
    annotation_category_find(set, *cavern_id)->visible = 0;
    pin_id = annotation_add_point(set, *cavern_id, ANNOTATION_PIN, 300, 310, "Ice key");
    annotation_set_colour(set, pin_id, 2);
    annotation_add_path(set, *cavern_id, segments, 2, "Way down");
    towns_id = annotation_category_add(set, "Towns", 3);
    annotation_add_point(set, towns_id, ANNOTATION_PIN, 700, 700, "Inn");
}

static int decode_tree_bytes(const ByteBuffer *buffer, const int single_root, AnnotationSet *tree) {
    size_t offset = 0;
    const int status = annotation_tree_decode(buffer->bytes, buffer->size, &offset, single_root, tree);
    if (status == 1) CHECK(offset == buffer->size);
    return status;
}

static void append_tree_category(ByteBuffer *buffer, const unsigned int parent_ref, const char *name) {
    annotation_varint_write(buffer, parent_ref);
    append_byte(buffer, 0x03);
    append_header(buffer, name, 0);
}

static void test_tree_round_trip_keeps_shape_settings_and_colours(void) {
    AnnotationSet set, decoded;
    ByteBuffer buffer = {0};
    const AnnotationCategory *root, *child;
    int dungeons_id, cavern_id;
    build_dungeon_set(&set, &dungeons_id, &cavern_id);
    CHECK(annotation_tree_encode(&set, dungeons_id, &buffer) == 1);
    CHECK(decode_tree_bytes(&buffer, 1, &decoded) == 1);
    CHECK(decoded.category_count == 2);
    if (decoded.category_count == 2) {
        root = &decoded.categories[0];
        child = &decoded.categories[1];
        CHECK(strcmp(root->name, "Dungeons") == 0);
        CHECK(root->parent_id == 0);
        CHECK(root->colour_index == 5);
        CHECK(root->visible == 1 && root->show_labels == 0);
        CHECK(root->annotation_count == 0);
        CHECK(strcmp(child->name, "Cold Cavern") == 0);
        CHECK(child->parent_id == root->id);
        CHECK(child->colour_index == 1);
        CHECK(child->visible == 0 && child->show_labels == 1);
        CHECK(child->annotation_count == 2);
        if (child->annotation_count == 2) {
            const Annotation *pin = &child->annotations[0];
            const Annotation *path = &child->annotations[1];
            CHECK(pin->kind == ANNOTATION_PIN && strcmp(pin->label, "Ice key") == 0);
            CHECK(pin->tile_x == 300 && pin->tile_y == 310);
            CHECK(pin->colour_index == 2);
            CHECK(path->kind == ANNOTATION_PATH && strcmp(path->label, "Way down") == 0);
            CHECK(path->colour_index == ANNOTATION_COLOUR_INHERIT);
            CHECK(path->segment_count == 2);
            if (path->segment_count == 2) {
                CHECK(annotation_identical(path, &annotation_category_find(&set, cavern_id)->annotations[1]));
                CHECK(path->segments[0].start_x == 100 && path->segments[0].start_y == 100);
                CHECK(path->segments[0].end_x == 100 && path->segments[0].end_y == 140);
                CHECK(path->segments[1].start_x == 100 && path->segments[1].start_y == 140);
                CHECK(path->segments[1].end_x == 130 && path->segments[1].end_y == 140);
            }
        }
    }
    annotation_set_release(&decoded);
    annotation_set_release(&set);
    byte_buffer_release(&buffer);
}

static void test_tree_encode_subtree_excludes_siblings(void) {
    AnnotationSet set, decoded;
    ByteBuffer buffer = {0};
    int dungeons_id, cavern_id;
    build_dungeon_set(&set, &dungeons_id, &cavern_id);
    annotation_category_add_under(&set, dungeons_id, "Warm Cavern", 4);
    CHECK(annotation_tree_encode(&set, cavern_id, &buffer) == 1);
    CHECK(decode_tree_bytes(&buffer, 1, &decoded) == 1);
    CHECK(decoded.category_count == 1);
    if (decoded.category_count == 1) {
        CHECK(strcmp(decoded.categories[0].name, "Cold Cavern") == 0);
        CHECK(decoded.categories[0].parent_id == 0);
        CHECK(decoded.categories[0].annotation_count == 2);
    }
    annotation_set_release(&decoded);
    annotation_set_release(&set);
    byte_buffer_release(&buffer);
}

static void test_tree_whole_set_keeps_every_root(void) {
    AnnotationSet set, decoded;
    ByteBuffer buffer = {0};
    int dungeons_id, cavern_id;
    build_dungeon_set(&set, &dungeons_id, &cavern_id);
    CHECK(annotation_tree_encode(&set, 0, &buffer) == 1);
    CHECK(decode_tree_bytes(&buffer, 0, &decoded) == 1);
    CHECK(decoded.category_count == 3);
    if (decoded.category_count == 3) {
        CHECK(strcmp(decoded.categories[2].name, "Towns") == 0);
        CHECK(decoded.categories[2].parent_id == 0);
        CHECK(decoded.categories[1].parent_id == decoded.categories[0].id);
    }
    annotation_set_release(&decoded);
    annotation_set_release(&set);
    byte_buffer_release(&buffer);
}

static void test_tree_rejects_forward_and_self_parent(void) {
    ByteBuffer first_refers_ahead = {0}, refers_beyond = {0}, refers_to_self = {0};
    AnnotationSet decoded;
    annotation_varint_write(&first_refers_ahead, 1);
    append_tree_category(&first_refers_ahead, 1, "Ahead");
    CHECK(decode_tree_bytes(&first_refers_ahead, 0, &decoded) == 0);
    check_tree_emptied(&decoded);

    annotation_varint_write(&refers_beyond, 2);
    append_tree_category(&refers_beyond, 0, "Root");
    append_tree_category(&refers_beyond, 3, "Beyond");
    CHECK(decode_tree_bytes(&refers_beyond, 0, &decoded) == 0);
    check_tree_emptied(&decoded);

    annotation_varint_write(&refers_to_self, 2);
    append_tree_category(&refers_to_self, 0, "Root");
    append_tree_category(&refers_to_self, 2, "Self");
    CHECK(decode_tree_bytes(&refers_to_self, 0, &decoded) == 0);
    check_tree_emptied(&decoded);

    byte_buffer_release(&first_refers_ahead);
    byte_buffer_release(&refers_beyond);
    byte_buffer_release(&refers_to_self);
}

static void test_tree_rejects_second_root_when_single(void) {
    ByteBuffer buffer = {0};
    AnnotationSet decoded;
    annotation_varint_write(&buffer, 2);
    append_tree_category(&buffer, 0, "First");
    append_tree_category(&buffer, 0, "Second");
    CHECK(decode_tree_bytes(&buffer, 1, &decoded) == 0);
    check_tree_emptied(&decoded);
    CHECK(decode_tree_bytes(&buffer, 0, &decoded) == 1);
    CHECK(decoded.category_count == 2);
    if (decoded.category_count == 2) {
        CHECK(decoded.categories[0].parent_id == 0 && decoded.categories[1].parent_id == 0);
    }
    annotation_set_release(&decoded);
    byte_buffer_release(&buffer);
}

static void append_chain(ByteBuffer *buffer, const int length) {
    int level;
    annotation_varint_write(buffer, (unsigned int) length);
    for (level = 0; level < length; level++) append_tree_category(buffer, (unsigned int) level, "Level");
}

static void test_tree_rejects_depth_nine(void) {
    ByteBuffer deepest_allowed = {0}, too_deep = {0};
    AnnotationSet decoded;
    append_chain(&deepest_allowed, ANNOTATION_MAX_DEPTH);
    CHECK(decode_tree_bytes(&deepest_allowed, 1, &decoded) == 1);
    CHECK(decoded.category_count == ANNOTATION_MAX_DEPTH);
    annotation_set_release(&decoded);

    append_chain(&too_deep, ANNOTATION_MAX_DEPTH + 1);
    CHECK(decode_tree_bytes(&too_deep, 1, &decoded) == 0);
    check_tree_emptied(&decoded);
    byte_buffer_release(&deepest_allowed);
    byte_buffer_release(&too_deep);
}

static void test_tree_rejects_too_many_categories(void) {
    ByteBuffer at_cap = {0}, over_cap = {0};
    AnnotationSet decoded;
    int index;
    annotation_varint_write(&at_cap, ANNOTATION_MAX_CATEGORIES);
    for (index = 0; index < ANNOTATION_MAX_CATEGORIES; index++) append_tree_category(&at_cap, 0, "Many");
    CHECK(decode_tree_bytes(&at_cap, 0, &decoded) == 1);
    CHECK(decoded.category_count == ANNOTATION_MAX_CATEGORIES);
    annotation_set_release(&decoded);

    annotation_varint_write(&over_cap, ANNOTATION_MAX_CATEGORIES + 1);
    CHECK(decode_tree_bytes(&over_cap, 0, &decoded) == -1);
    check_tree_emptied(&decoded);
    byte_buffer_release(&at_cap);
    byte_buffer_release(&over_cap);
}

static void test_export_is_version_two_and_carries_children(void) {
    AnnotationSet set, imported;
    int dungeons_id, cavern_id;
    char *code;
    build_dungeon_set(&set, &dungeons_id, &cavern_id);
    code = annotation_code_export(&set, dungeons_id);
    CHECK(code != NULL);
    if (code != NULL) {
        CHECK(strncmp(code, "TLGMAP2:", 8) == 0);
        CHECK(import_code(code, &imported) == ANNOTATION_CODE_OK);
        CHECK(imported.category_count == 2);
        CHECK(imported.changed == 0);
        if (imported.category_count == 2) {
            const AnnotationCategory *root = &imported.categories[0];
            const AnnotationCategory *child = &imported.categories[1];
            CHECK(strcmp(root->name, "Dungeons") == 0);
            CHECK(root->parent_id == 0);
            CHECK(root->show_labels == 0);
            CHECK(strcmp(child->name, "Cold Cavern") == 0);
            CHECK(child->parent_id == root->id);
            CHECK(child->visible == 0);
            CHECK(child->annotation_count == 2);
            if (child->annotation_count == 2) CHECK(child->annotations[0].colour_index == 2);
        }
        annotation_set_release(&imported);
    }
    free(code);
    annotation_set_release(&set);
}

static void test_export_unknown_category_is_null(void) {
    AnnotationSet set;
    int dungeons_id, cavern_id;
    build_dungeon_set(&set, &dungeons_id, &cavern_id);
    CHECK(annotation_code_export(&set, 9999) == NULL);
    CHECK(annotation_code_export(&set, 0) == NULL);
    annotation_set_release(&set);
}

static void test_has_prefix_accepts_both(void) {
    CHECK(annotation_code_has_prefix("TLGMAP1:x") == 1);
    CHECK(annotation_code_has_prefix("TLGMAP2:x") == 1);
    CHECK(annotation_code_has_prefix("TLGMAP3:x") == 0);
    CHECK(annotation_code_has_prefix("TLGMAP") == 0);
    CHECK(annotation_code_has_prefix("") == 0);
}

static void test_version_two_with_two_roots_is_damaged(void) {
    AnnotationSet set, imported;
    ByteBuffer buffer = {0};
    int dungeons_id, cavern_id;
    char *code;
    build_dungeon_set(&set, &dungeons_id, &cavern_id);
    CHECK(annotation_tree_encode(&set, 0, &buffer) == 1);
    code = code_from_bytes(ANNOTATION_CODE_PREFIX, buffer.bytes, buffer.size);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_DAMAGED);
    check_tree_emptied(&imported);
    free(code);
    byte_buffer_release(&buffer);
    annotation_set_release(&set);
}

static void test_version_two_trailing_bytes_are_damaged(void) {
    AnnotationSet set, imported;
    ByteBuffer buffer = {0};
    int dungeons_id, cavern_id;
    char *code;
    build_dungeon_set(&set, &dungeons_id, &cavern_id);
    CHECK(annotation_tree_encode(&set, dungeons_id, &buffer) == 1);
    append_byte(&buffer, 0);
    code = code_from_bytes(ANNOTATION_CODE_PREFIX, buffer.bytes, buffer.size);
    CHECK(import_code(code, &imported) == ANNOTATION_CODE_DAMAGED);
    check_tree_emptied(&imported);
    free(code);
    byte_buffer_release(&buffer);
    annotation_set_release(&set);
}

static AnnotationCodeStatus import_zeros(const char *prefix, const size_t size) {
    unsigned char *zeros = calloc(size, 1);
    AnnotationSet imported;
    AnnotationCodeStatus status;
    char *code = code_from_bytes(prefix, zeros, size);
    status = import_code(code, &imported);
    check_tree_emptied(&imported);
    annotation_set_release(&imported);
    free(code);
    free(zeros);
    return status;
}

static void test_version_two_over_limit_is_too_large(void) {
    CHECK(import_zeros(ANNOTATION_CODE_PREFIX, (size_t) 16 * 1024 * 1024 + 1) == ANNOTATION_CODE_TOO_LARGE);
}

static void test_version_one_limit_is_unchanged(void) {
    CHECK(import_zeros(ANNOTATION_CODE_LEGACY_PREFIX, 262144) == ANNOTATION_CODE_DAMAGED);
    CHECK(import_zeros(ANNOTATION_CODE_LEGACY_PREFIX, 262145) == ANNOTATION_CODE_TOO_LARGE);
}

int main(void) {
    test_varint_round_trip();
    test_code_round_trip();
    test_code_with_whitespace_imports();
    test_missing_prefix_is_not_a_code();
    test_truncated_code_is_damaged();
    test_out_of_range_coordinate_is_damaged();
    test_huge_delta_is_damaged();
    test_oversized_count_is_rejected();
    test_typical_maze_guide_fits_discord();
    test_segment_total_over_cap_is_too_large();
    test_empty_category_name_becomes_imported();
    test_empty_text_label_is_damaged();
    test_trailing_bytes_are_damaged();
    test_frozen_version_one_code_still_imports();
    test_tree_round_trip_keeps_shape_settings_and_colours();
    test_tree_encode_subtree_excludes_siblings();
    test_tree_whole_set_keeps_every_root();
    test_tree_rejects_forward_and_self_parent();
    test_tree_rejects_second_root_when_single();
    test_tree_rejects_depth_nine();
    test_tree_rejects_too_many_categories();
    test_export_is_version_two_and_carries_children();
    test_export_unknown_category_is_null();
    test_has_prefix_accepts_both();
    test_version_two_with_two_roots_is_damaged();
    test_version_two_trailing_bytes_are_damaged();
    test_version_two_over_limit_is_too_large();
    test_version_one_limit_is_unchanged();
    if (failures != 0) {
        printf("%d annotation code check(s) failed\n", failures);
        return 1;
    }
    printf("All annotation code tests passed\n");
    return 0;
}
