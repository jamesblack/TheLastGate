#include "game/annotation_code.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "game/annotation_codec.h"

#define DECOMPRESSED_LIMIT 262144

static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const unsigned char *bytes, const size_t size) {
    const size_t prefix_length = strlen(ANNOTATION_CODE_PREFIX);
    char *text = malloc(prefix_length + (size + 2) / 3 * 4 + 1);
    size_t position, written;
    if (text == NULL) return NULL;
    memcpy(text, ANNOTATION_CODE_PREFIX, prefix_length);
    written = prefix_length;
    for (position = 0; position < size; position += 3) {
        const size_t remaining = size - position;
        unsigned int group = (unsigned int) bytes[position] << 16;
        if (remaining > 1) group |= (unsigned int) bytes[position + 1] << 8;
        if (remaining > 2) group |= bytes[position + 2];
        text[written++] = alphabet[(group >> 18) & 63];
        text[written++] = alphabet[(group >> 12) & 63];
        text[written++] = remaining > 1 ? alphabet[(group >> 6) & 63] : '=';
        text[written++] = remaining > 2 ? alphabet[group & 63] : '=';
    }
    text[written] = '\0';
    return text;
}

static int base64_value(const char letter) {
    const char *found = letter == '\0' ? NULL : strchr(alphabet, letter);
    return found == NULL ? -1 : (int) (found - alphabet);
}

static unsigned char *base64_decode(const char *text, const size_t length, size_t *size) {
    unsigned char *bytes;
    size_t position, written = 0;
    int padding = 0;
    if (length == 0 || length % 4 != 0) return NULL;
    if (text[length - 1] == '=') padding++;
    if (text[length - 2] == '=') padding++;
    bytes = malloc(length / 4 * 3 + 1);
    if (bytes == NULL) return NULL;
    for (position = 0; position < length; position += 4) {
        const int last_group = position + 4 == length;
        unsigned int group = 0;
        int slot;
        for (slot = 0; slot < 4; slot++) {
            const int is_padding = last_group && slot >= 4 - padding;
            const int value = is_padding ? 0 : base64_value(text[position + (size_t) slot]);
            if (value < 0) {
                free(bytes);
                return NULL;
            }
            group = (group << 6) | (unsigned int) value;
        }
        bytes[written++] = (unsigned char) (group >> 16);
        if (!last_group || padding < 2) bytes[written++] = (unsigned char) (group >> 8);
        if (!last_group || padding < 1) bytes[written++] = (unsigned char) group;
    }
    *size = written;
    return bytes;
}

char *annotation_code_export(const AnnotationCategory *category) {
    ByteBuffer raw = {0};
    unsigned char *compressed = NULL;
    uLongf compressed_size;
    char *code = NULL;
    if (!annotation_category_encode(category, &raw)) goto finished;
    compressed_size = compressBound((uLong) raw.size);
    compressed = malloc(compressed_size);
    if (compressed == NULL) goto finished;
    if (compress2(compressed, &compressed_size, raw.bytes, (uLong) raw.size, 9) != Z_OK) goto finished;
    code = base64_encode(compressed, compressed_size);
finished:
    free(compressed);
    byte_buffer_release(&raw);
    return code;
}

static char *strip_whitespace(const char *code) {
    const size_t length = strlen(code);
    char *stripped = malloc(length + 1);
    size_t position, written = 0;
    if (stripped == NULL) return NULL;
    for (position = 0; position < length; position++) {
        if (!isspace((unsigned char) code[position])) stripped[written++] = code[position];
    }
    stripped[written] = '\0';
    return stripped;
}

static AnnotationCodeStatus inflate_bytes(const unsigned char *compressed, const size_t compressed_size,
                                          unsigned char *raw, size_t *raw_size) {
    z_stream stream;
    int result;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit(&stream) != Z_OK) return ANNOTATION_CODE_DAMAGED;
    stream.next_in = (Bytef *) compressed;
    stream.avail_in = (uInt) compressed_size;
    stream.next_out = raw;
    /* One spare byte tells output beyond the limit apart from a stream that ends exactly at it. */
    stream.avail_out = DECOMPRESSED_LIMIT + 1;
    result = inflate(&stream, Z_FINISH);
    *raw_size = stream.total_out;
    inflateEnd(&stream);
    if (*raw_size > DECOMPRESSED_LIMIT) return ANNOTATION_CODE_TOO_LARGE;
    return result == Z_STREAM_END ? ANNOTATION_CODE_OK : ANNOTATION_CODE_DAMAGED;
}

AnnotationCodeStatus annotation_code_import(const char *code, AnnotationCategory *imported) {
    const size_t prefix_length = strlen(ANNOTATION_CODE_PREFIX);
    AnnotationCodeStatus status = ANNOTATION_CODE_DAMAGED;
    unsigned char *compressed = NULL, *raw = NULL;
    size_t compressed_size = 0, raw_size = 0, offset = 0;
    char *stripped = strip_whitespace(code);
    int decoded;
    memset(imported, 0, sizeof(*imported));
    if (stripped == NULL) goto finished;
    if (strncmp(stripped, ANNOTATION_CODE_PREFIX, prefix_length) != 0) {
        status = ANNOTATION_CODE_NOT_A_CODE;
        goto finished;
    }
    compressed = base64_decode(stripped + prefix_length, strlen(stripped + prefix_length), &compressed_size);
    raw = malloc(DECOMPRESSED_LIMIT + 1);
    if (compressed == NULL || raw == NULL) goto finished;
    status = inflate_bytes(compressed, compressed_size, raw, &raw_size);
    if (status != ANNOTATION_CODE_OK) goto finished;
    decoded = annotation_category_decode(raw, raw_size, &offset, imported);
    if (decoded == 1 && offset != raw_size) {
        annotation_category_release(imported);
        decoded = 0;
    }
    status = decoded == 1 ? ANNOTATION_CODE_OK : decoded < 0 ? ANNOTATION_CODE_TOO_LARGE : ANNOTATION_CODE_DAMAGED;
finished:
    free(stripped);
    free(compressed);
    free(raw);
    return status;
}
