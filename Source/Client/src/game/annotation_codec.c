#include "game/annotation_codec.h"

#include <stdlib.h>
#include <string.h>

#define ANNOTATION_INHERIT_BYTE 0xFF
#define ANNOTATION_FLAG_VISIBLE 0x01
#define ANNOTATION_FLAG_SHOW_LABELS 0x02

typedef struct {
    const unsigned char *bytes;
    size_t size;
    size_t *offset;
} Reader;

void byte_buffer_release(ByteBuffer *buffer) {
    free(buffer->bytes);
    memset(buffer, 0, sizeof(*buffer));
}

int byte_buffer_append(ByteBuffer *buffer, const void *bytes, const size_t size) {
    if (buffer->size + size > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 256 : buffer->capacity;
        unsigned char *grown;
        while (capacity < buffer->size + size) capacity *= 2;
        grown = realloc(buffer->bytes, capacity);
        if (grown == NULL) return 0;
        buffer->bytes = grown;
        buffer->capacity = capacity;
    }
    if (size > 0) memcpy(buffer->bytes + buffer->size, bytes, size);
    buffer->size += size;
    return 1;
}

int annotation_varint_write(ByteBuffer *buffer, unsigned int value) {
    unsigned char byte;
    while (value >= 0x80) {
        byte = (unsigned char) (value | 0x80);
        if (!byte_buffer_append(buffer, &byte, 1)) return 0;
        value >>= 7;
    }
    byte = (unsigned char) value;
    return byte_buffer_append(buffer, &byte, 1);
}

int annotation_varint_read(const unsigned char *bytes, const size_t size, size_t *offset, unsigned int *value) {
    unsigned int result = 0;
    int shift;
    for (shift = 0; shift < 35; shift += 7) {
        unsigned int byte;
        if (*offset >= size) return 0;
        byte = bytes[(*offset)++];
        result |= (byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            *value = result;
            return 1;
        }
    }
    return 0;
}

/* Zigzag maps signed deltas to unsigned (0,-1,1,-2 -> 0,1,2,3) so small moves in either direction stay one byte. */
static unsigned int zigzag_encode(const int value) {
    return ((unsigned int) value << 1) ^ (unsigned int) (value >> 31);
}

static int zigzag_decode(const unsigned int value) {
    return (int) (value >> 1) ^ -(int) (value & 1);
}

static int write_byte(ByteBuffer *buffer, const unsigned char value) {
    return byte_buffer_append(buffer, &value, 1);
}

static int write_text(ByteBuffer *buffer, const char *text) {
    const size_t length = strlen(text);
    return write_byte(buffer, (unsigned char) length) && byte_buffer_append(buffer, text, length);
}

static int write_point(ByteBuffer *buffer, int *cursor_x, int *cursor_y, const int tile_x, const int tile_y) {
    if (!annotation_varint_write(buffer, zigzag_encode(tile_x - *cursor_x)) ||
        !annotation_varint_write(buffer, zigzag_encode(tile_y - *cursor_y))) {
        return 0;
    }
    *cursor_x = tile_x;
    *cursor_y = tile_y;
    return 1;
}

static unsigned char annotation_colour_byte(const int colour_index) {
    if (colour_index < 0 || colour_index >= ANNOTATION_PALETTE_SIZE) return ANNOTATION_INHERIT_BYTE;
    return (unsigned char) colour_index;
}

static int write_category_body(const AnnotationCategory *category, ByteBuffer *buffer, const int with_colours) {
    int cursor_x = 0, cursor_y = 0, index, segment_index;
    if (!write_text(buffer, category->name) || !write_byte(buffer, (unsigned char) category->colour_index)) return 0;
    if (!annotation_varint_write(buffer, (unsigned int) category->annotation_count)) return 0;
    for (index = 0; index < category->annotation_count; index++) {
        const Annotation *annotation = &category->annotations[index];
        if (!write_byte(buffer, (unsigned char) annotation->kind) || !write_text(buffer, annotation->label)) return 0;
        if (with_colours && !write_byte(buffer, annotation_colour_byte(annotation->colour_index))) return 0;
        if (annotation->kind != ANNOTATION_PATH) {
            if (!write_point(buffer, &cursor_x, &cursor_y, annotation->tile_x, annotation->tile_y)) return 0;
            continue;
        }
        if (!annotation_varint_write(buffer, (unsigned int) annotation->segment_count)) return 0;
        for (segment_index = 0; segment_index < annotation->segment_count; segment_index++) {
            const AnnotationSegment *segment = &annotation->segments[segment_index];
            if (!write_point(buffer, &cursor_x, &cursor_y, segment->start_x, segment->start_y) ||
                !write_point(buffer, &cursor_x, &cursor_y, segment->end_x, segment->end_y)) {
                return 0;
            }
        }
    }
    return 1;
}

int annotation_category_encode(const AnnotationCategory *category, ByteBuffer *buffer) {
    return write_category_body(category, buffer, 0);
}

static unsigned int parent_reference(const AnnotationSet *set, const int *ordered_indices, const int position) {
    const int parent_id = set->categories[ordered_indices[position]].parent_id;
    int earlier;
    for (earlier = position - 1; earlier >= 0; earlier--) {
        if (set->categories[ordered_indices[earlier]].id == parent_id) return (unsigned int) earlier + 1;
    }
    return 0;
}

int annotation_tree_encode(const AnnotationSet *set, const int root_id, ByteBuffer *buffer) {
    int ordered_indices[ANNOTATION_MAX_CATEGORIES];
    int category_count, position;
    category_count = annotation_tree_collect(set, root_id, ordered_indices, ANNOTATION_MAX_CATEGORIES);
    if (root_id != 0 && category_count == 0) return 0;
    if (!annotation_varint_write(buffer, (unsigned int) category_count)) return 0;
    for (position = 0; position < category_count; position++) {
        const AnnotationCategory *category = &set->categories[ordered_indices[position]];
        const unsigned char flags = (unsigned char) ((category->visible ? ANNOTATION_FLAG_VISIBLE : 0) |
                                                     (category->show_labels ? ANNOTATION_FLAG_SHOW_LABELS : 0));
        if (!annotation_varint_write(buffer, parent_reference(set, ordered_indices, position)) ||
            !write_byte(buffer, flags) || !write_category_body(category, buffer, 1)) {
            return 0;
        }
    }
    return 1;
}

static int read_byte(const Reader *reader, unsigned int *value) {
    if (*reader->offset >= reader->size) return 0;
    *value = reader->bytes[(*reader->offset)++];
    return 1;
}

static int read_text(const Reader *reader, char *destination, const size_t destination_size) {
    unsigned int length;
    if (!read_byte(reader, &length) || length >= destination_size) return 0;
    if (reader->size - *reader->offset < length) return 0;
    memcpy(destination, reader->bytes + *reader->offset, length);
    destination[length] = '\0';
    *reader->offset += length;
    return 1;
}

static int read_coordinate(const Reader *reader, int *cursor, unsigned short *coordinate) {
    unsigned int encoded;
    int delta, position;
    if (!annotation_varint_read(reader->bytes, reader->size, reader->offset, &encoded)) return 0;
    delta = zigzag_decode(encoded);
    if (delta < -ANNOTATION_TILE_LIMIT || delta > ANNOTATION_TILE_LIMIT) return 0;
    position = *cursor + delta;
    if (position < 0 || position >= ANNOTATION_TILE_LIMIT) return 0;
    *cursor = position;
    *coordinate = (unsigned short) position;
    return 1;
}

static int read_point(const Reader *reader, int *cursor_x, int *cursor_y, unsigned short *tile_x,
                      unsigned short *tile_y) {
    return read_coordinate(reader, cursor_x, tile_x) && read_coordinate(reader, cursor_y, tile_y);
}

static int read_path(const Reader *reader, int *cursor_x, int *cursor_y, Annotation *annotation) {
    unsigned int segment_count, segment_index;
    if (!annotation_varint_read(reader->bytes, reader->size, reader->offset, &segment_count)) return 0;
    if (segment_count > ANNOTATION_MAX_SEGMENTS) return -1;
    if (segment_count == 0) return 0;
    annotation->segments = calloc(segment_count, sizeof(AnnotationSegment));
    if (annotation->segments == NULL) return 0;
    annotation->segment_count = (int) segment_count;
    for (segment_index = 0; segment_index < segment_count; segment_index++) {
        AnnotationSegment *segment = &annotation->segments[segment_index];
        if (!read_point(reader, cursor_x, cursor_y, &segment->start_x, &segment->start_y) ||
            !read_point(reader, cursor_x, cursor_y, &segment->end_x, &segment->end_y)) {
            return 0;
        }
    }
    annotation->tile_x = annotation->segments[0].start_x;
    annotation->tile_y = annotation->segments[0].start_y;
    return 1;
}

static int read_annotation(const Reader *reader, int *cursor_x, int *cursor_y, Annotation *annotation,
                           const int with_colours) {
    unsigned int kind, colour_index;
    if (!read_byte(reader, &kind) || kind < ANNOTATION_PIN || kind > ANNOTATION_PATH) return 0;
    annotation->kind = (AnnotationKind) kind;
    annotation->colour_index = ANNOTATION_COLOUR_INHERIT;
    if (!read_text(reader, annotation->label, ANNOTATION_LABEL_MAX)) return 0;
    if (with_colours) {
        if (!read_byte(reader, &colour_index)) return 0;
        if (colour_index < ANNOTATION_PALETTE_SIZE) annotation->colour_index = (int) colour_index;
    }
    if (annotation->kind == ANNOTATION_PATH) return read_path(reader, cursor_x, cursor_y, annotation);
    if (annotation->kind == ANNOTATION_TEXT && annotation->label[0] == '\0') return 0;
    return read_point(reader, cursor_x, cursor_y, &annotation->tile_x, &annotation->tile_y);
}

static int read_category_body(const Reader *reader, AnnotationCategory *category, const int with_colours) {
    unsigned int colour_index, annotation_count, index;
    int cursor_x = 0, cursor_y = 0, segment_total = 0;
    if (!read_text(reader, category->name, ANNOTATION_CATEGORY_NAME_MAX) || !read_byte(reader, &colour_index) ||
        !annotation_varint_read(reader->bytes, reader->size, reader->offset, &annotation_count)) {
        return 0;
    }
    if (category->name[0] == '\0') annotation_copy_text(category->name, sizeof(category->name), "Imported");
    category->colour_index = colour_index >= ANNOTATION_PALETTE_SIZE ? 0 : (int) colour_index;
    if (annotation_count > ANNOTATION_MAX_PER_CATEGORY) return -1;
    if (annotation_count > 0) {
        category->annotations = calloc(annotation_count, sizeof(Annotation));
        if (category->annotations == NULL) return 0;
        category->annotation_count = (int) annotation_count;
    }
    for (index = 0; index < annotation_count; index++) {
        const int result = read_annotation(reader, &cursor_x, &cursor_y, &category->annotations[index], with_colours);
        if (result != 1) return result;
        segment_total += category->annotations[index].segment_count;
        if (segment_total > ANNOTATION_MAX_SEGMENTS_PER_CATEGORY) return -1;
    }
    return 1;
}

int annotation_category_decode(const unsigned char *bytes, const size_t size, size_t *offset,
                               AnnotationCategory *category) {
    const Reader reader = {bytes, size, offset};
    int status;
    memset(category, 0, sizeof(*category));
    category->visible = 1;
    category->show_labels = 1;
    status = read_category_body(&reader, category, 0);
    if (status != 1) annotation_category_release(category);
    return status;
}

int annotation_tree_decode(const unsigned char *bytes, const size_t size, size_t *offset, const int single_root,
                           AnnotationSet *tree) {
    const Reader reader = {bytes, size, offset};
    int assigned_ids[ANNOTATION_MAX_CATEGORIES];
    unsigned int category_count, index, parent_ref, flags;
    int status = 0, root_count = 0;
    annotation_set_init(tree);
    if (!annotation_varint_read(bytes, size, offset, &category_count)) goto failed;
    if (category_count > ANNOTATION_MAX_CATEGORIES) {
        status = -1;
        goto failed;
    }
    if (single_root && category_count == 0) goto failed;
    for (index = 0; index < category_count; index++) {
        AnnotationCategory category;
        int result, category_id = 0;
        if (!annotation_varint_read(bytes, size, offset, &parent_ref) || !read_byte(&reader, &flags)) goto failed;
        /* parent_ref is 1-based, so any value above the current index points at this category or a later one. */
        if (parent_ref > index) goto failed;
        if (parent_ref == 0 && ++root_count > 1 && single_root) goto failed;
        memset(&category, 0, sizeof(category));
        category.visible = (flags & ANNOTATION_FLAG_VISIBLE) != 0;
        category.show_labels = (flags & ANNOTATION_FLAG_SHOW_LABELS) != 0;
        result = read_category_body(&reader, &category, 1);
        if (result == 1) {
            category_id =
                    annotation_category_attach(tree, &category, parent_ref == 0 ? 0 : assigned_ids[parent_ref - 1]);
            if (category_id == 0) result = 0;
        }
        if (result != 1) {
            annotation_category_release(&category);
            status = result;
            goto failed;
        }
        assigned_ids[index] = category_id;
    }
    tree->changed = 0;
    return 1;
failed:
    annotation_set_release(tree);
    annotation_set_init(tree);
    return status;
}
