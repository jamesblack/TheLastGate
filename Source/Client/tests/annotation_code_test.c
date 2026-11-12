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

static char *code_from_bytes(const unsigned char *bytes, const size_t size) {
    uLongf compressed_size = compressBound((uLong) size);
    unsigned char *compressed = malloc(compressed_size);
    char *code = malloc(strlen(ANNOTATION_CODE_PREFIX) + (size_t) compressed_size * 2 + 8);
    size_t position, written;
    compress2(compressed, &compressed_size, bytes, (uLong) size, 9);
    strcpy(code, ANNOTATION_CODE_PREFIX);
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

static AnnotationCategory build_sample_category(void) {
    AnnotationSet set;
    AnnotationCategory category;
    AnnotationSegment segments[2] = {{100, 100, 100, 140}, {100, 140, 130, 140}};
    int category_id;
    annotation_set_init(&set);
    category_id = annotation_category_add(&set, "Stronghold keys", 2);
    annotation_add_point(&set, category_id, ANNOTATION_PIN, 512, 498, "Silver key");
    annotation_add_point(&set, category_id, ANNOTATION_PIN, 520, 505, "Gold key");
    annotation_add_point(&set, category_id, ANNOTATION_TEXT, 500, 500, "Start");
    annotation_add_path(&set, category_id, segments, 2, "Route to boss");
    category = set.categories[0];
    free(set.categories);
    return category;
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

static void test_code_round_trip(void) {
    AnnotationCategory sample = build_sample_category();
    AnnotationCategory imported;
    char *code = annotation_code_export(&sample);
    CHECK(code != NULL);
    if (code == NULL) return;
    CHECK(strncmp(code, "TLGMAP1:", 8) == 0);
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_OK);
    check_sample_matches(&imported);
    CHECK(imported.visible == 1);
    annotation_category_release(&imported);
    annotation_category_release(&sample);
    free(code);
}

static void test_code_with_whitespace_imports(void) {
    AnnotationCategory sample = build_sample_category();
    AnnotationCategory imported;
    char *code = annotation_code_export(&sample);
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
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(spaced, &imported) == ANNOTATION_CODE_OK);
    check_sample_matches(&imported);
    annotation_category_release(&imported);
    annotation_category_release(&sample);
    free(spaced);
    free(code);
}

static void test_missing_prefix_is_not_a_code(void) {
    AnnotationCategory imported;
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import("hello", &imported) == ANNOTATION_CODE_NOT_A_CODE);
}

static void test_truncated_code_is_damaged(void) {
    AnnotationCategory sample = build_sample_category();
    AnnotationCategory imported;
    char *code = annotation_code_export(&sample);
    memset(&imported, 0, sizeof(imported));
    code[strlen(code) - 8] = '\0';
    CHECK(strchr(code, '=') == NULL);
    CHECK(strlen(code) % 4 == 0);
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_DAMAGED);
    CHECK(imported.annotation_count == 0);
    CHECK(imported.annotations == NULL);
    code[strlen(code) - 2] = '\0';
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_DAMAGED);
    CHECK(imported.annotations == NULL);
    annotation_category_release(&sample);
    free(code);
}

static void test_out_of_range_coordinate_is_damaged(void) {
    ByteBuffer buffer = {0};
    AnnotationCategory imported;
    char *code;
    append_header(&buffer, "Bad", 1);
    append_byte(&buffer, ANNOTATION_PIN);
    append_byte(&buffer, 0);
    append_zigzag(&buffer, 3000);
    append_zigzag(&buffer, 0);
    code = code_from_bytes(buffer.bytes, buffer.size);
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_DAMAGED);
    CHECK(imported.annotations == NULL);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_huge_delta_is_damaged(void) {
    ByteBuffer buffer = {0};
    AnnotationCategory imported;
    char *code;
    append_header(&buffer, "Huge delta", 1);
    append_byte(&buffer, ANNOTATION_PIN);
    append_byte(&buffer, 0);
    annotation_varint_write(&buffer, 0xFFFFFFFEu);
    append_zigzag(&buffer, 0);
    code = code_from_bytes(buffer.bytes, buffer.size);
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_DAMAGED);
    CHECK(imported.annotations == NULL);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_oversized_count_is_rejected(void) {
    ByteBuffer buffer = {0};
    AnnotationCategory imported;
    char *code;
    append_header(&buffer, "Huge", 100000);
    code = code_from_bytes(buffer.bytes, buffer.size);
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_TOO_LARGE);
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
    code = annotation_code_export(&set.categories[0]);
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
    code = code_from_bytes(buffer.bytes, buffer.size);
    byte_buffer_release(&buffer);
    return code;
}

static void test_segment_total_over_cap_is_too_large(void) {
    const int paths_at_cap = ANNOTATION_MAX_SEGMENTS_PER_CATEGORY / ANNOTATION_MAX_SEGMENTS;
    AnnotationCategory imported;
    char *code = code_for_flood(paths_at_cap);
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_OK);
    annotation_category_release(&imported);
    free(code);

    code = code_for_flood(paths_at_cap + 1);
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_TOO_LARGE);
    CHECK(imported.annotations == NULL);
    free(code);
}

static void test_empty_category_name_becomes_imported(void) {
    ByteBuffer buffer = {0};
    AnnotationCategory imported;
    char *code;
    append_header(&buffer, "", 1);
    append_byte(&buffer, ANNOTATION_PIN);
    append_byte(&buffer, 0);
    append_zigzag(&buffer, 10);
    append_zigzag(&buffer, 20);
    code = code_from_bytes(buffer.bytes, buffer.size);
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_OK);
    CHECK(strcmp(imported.name, "Imported") == 0);
    CHECK(imported.annotation_count == 1);
    if (imported.annotation_count == 1) CHECK(imported.annotations[0].label[0] == '\0');
    annotation_category_release(&imported);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_empty_text_label_is_damaged(void) {
    ByteBuffer buffer = {0};
    AnnotationCategory imported;
    char *code;
    append_header(&buffer, "Notes", 1);
    append_byte(&buffer, ANNOTATION_TEXT);
    append_byte(&buffer, 0);
    append_zigzag(&buffer, 10);
    append_zigzag(&buffer, 20);
    code = code_from_bytes(buffer.bytes, buffer.size);
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_DAMAGED);
    CHECK(imported.annotations == NULL);
    free(code);
    byte_buffer_release(&buffer);
}

static void test_trailing_bytes_are_damaged(void) {
    ByteBuffer buffer = {0};
    AnnotationCategory imported;
    char *code;
    append_header(&buffer, "Keys", 1);
    append_byte(&buffer, ANNOTATION_PIN);
    append_byte(&buffer, 0);
    append_zigzag(&buffer, 10);
    append_zigzag(&buffer, 20);
    append_byte(&buffer, 0);
    code = code_from_bytes(buffer.bytes, buffer.size);
    memset(&imported, 0, sizeof(imported));
    CHECK(annotation_code_import(code, &imported) == ANNOTATION_CODE_DAMAGED);
    CHECK(imported.annotations == NULL);
    free(code);
    byte_buffer_release(&buffer);
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
    if (failures != 0) {
        printf("%d annotation code check(s) failed\n", failures);
        return 1;
    }
    printf("All annotation code tests passed\n");
    return 0;
}
