#pragma once

#include <stddef.h>

#include "game/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *bytes;
    size_t size;
    size_t capacity;
} ByteBuffer;

void byte_buffer_release(ByteBuffer *buffer);
int byte_buffer_append(ByteBuffer *buffer, const void *bytes, size_t size);
int annotation_category_encode(const AnnotationCategory *category, ByteBuffer *buffer);
int annotation_category_decode(const unsigned char *bytes, size_t size, size_t *offset, AnnotationCategory *category);
void annotation_varint_write(ByteBuffer *buffer, unsigned int value);
int annotation_varint_read(const unsigned char *bytes, size_t size, size_t *offset, unsigned int *value);

#ifdef __cplusplus
}
#endif
