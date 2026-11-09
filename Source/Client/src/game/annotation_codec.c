#include "game/annotation_codec.h"

#include <stdlib.h>
#include <string.h>

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

void annotation_varint_write(ByteBuffer *buffer, unsigned int value) {
    unsigned char byte;
    while (value >= 0x80) {
        byte = (unsigned char) (value | 0x80);
        byte_buffer_append(buffer, &byte, 1);
        value >>= 7;
    }
    byte = (unsigned char) value;
    byte_buffer_append(buffer, &byte, 1);
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
    const size_t before = buffer->size;
    annotation_varint_write(buffer, zigzag_encode(tile_x - *cursor_x));
    annotation_varint_write(buffer, zigzag_encode(tile_y - *cursor_y));
    *cursor_x = tile_x;
    *cursor_y = tile_y;
    return buffer->size > before;
}

int annotation_category_encode(const AnnotationCategory *category, ByteBuffer *buffer) {
    int cursor_x = 0, cursor_y = 0, index, segment_index;
    if (!write_text(buffer, category->name) || !write_byte(buffer, (unsigned char) category->colour_index)) return 0;
    annotation_varint_write(buffer, (unsigned int) category->annotation_count);
    for (index = 0; index < category->annotation_count; index++) {
        const Annotation *annotation = &category->annotations[index];
        if (!write_byte(buffer, (unsigned char) annotation->kind) || !write_text(buffer, annotation->label)) return 0;
        if (annotation->kind != ANNOTATION_PATH) {
            if (!write_point(buffer, &cursor_x, &cursor_y, annotation->tile_x, annotation->tile_y)) return 0;
            continue;
        }
        annotation_varint_write(buffer, (unsigned int) annotation->segment_count);
        for (segment_index = 0; segment_index < annotation->segment_count; segment_index++) {
            const AnnotationSegment *segment = &annotation->segments[segment_index];
            if (!write_point(buffer, &cursor_x, &cursor_y, segment->start_x, segment->start_y) ||
                !write_point(buffer, &cursor_x, &cursor_y, segment->end_x, segment->end_y))
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
            !read_point(reader, cursor_x, cursor_y, &segment->end_x, &segment->end_y))
            return 0;
    }
    return 1;
}

static int read_annotation(const Reader *reader, int *cursor_x, int *cursor_y, Annotation *annotation) {
    unsigned int kind;
    if (!read_byte(reader, &kind) || kind < ANNOTATION_PIN || kind > ANNOTATION_PATH) return 0;
    annotation->kind = (AnnotationKind) kind;
    if (!read_text(reader, annotation->label, ANNOTATION_LABEL_MAX)) return 0;
    if (annotation->kind == ANNOTATION_PATH) return read_path(reader, cursor_x, cursor_y, annotation);
    return read_point(reader, cursor_x, cursor_y, &annotation->tile_x, &annotation->tile_y);
}

int annotation_category_decode(const unsigned char *bytes, const size_t size, size_t *offset,
                               AnnotationCategory *category) {
    const Reader reader = {bytes, size, offset};
    unsigned int colour_index, annotation_count, index;
    int cursor_x = 0, cursor_y = 0, status = 0;
    memset(category, 0, sizeof(*category));
    category->visible = 1;
    if (!read_text(&reader, category->name, ANNOTATION_CATEGORY_NAME_MAX) || !read_byte(&reader, &colour_index) ||
        !annotation_varint_read(bytes, size, offset, &annotation_count))
        goto failed;
    category->colour_index = colour_index >= ANNOTATION_PALETTE_SIZE ? 0 : (int) colour_index;
    if (annotation_count > ANNOTATION_MAX_PER_CATEGORY) {
        status = -1;
        goto failed;
    }
    if (annotation_count > 0) {
        category->annotations = calloc(annotation_count, sizeof(Annotation));
        if (category->annotations == NULL) goto failed;
        category->annotation_count = (int) annotation_count;
    }
    for (index = 0; index < annotation_count; index++) {
        const int result = read_annotation(&reader, &cursor_x, &cursor_y, &category->annotations[index]);
        if (result != 1) {
            status = result;
            goto failed;
        }
    }
    return 1;
failed:
    annotation_category_release(category);
    return status;
}
