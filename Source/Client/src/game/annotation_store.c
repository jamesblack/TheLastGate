#include "annotation_store.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "game/annotation_codec.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* File layout: the four magic bytes, then 32-bit little-endian words for the format version, uncompressed payload
 * size and compressed byte count, then the payload compressed with zlib. The payload is the show-on-minimap byte
 * followed, from format 3, by the category tree encoding and nothing else. Formats 1 and 2 hold a varint category
 * count, then per category a visible byte, a show-labels byte (format 2 only) and the flat category encoding. Format 1
 * files load with labels shown. Saving over a format 1 or 2 file first keeps a copy beside it as <path>.format2.bak. */
static const char ANNOTATION_STORE_MAGIC[4] = {'T', 'L', 'G', 'A'};
#define ANNOTATION_STORE_FORMAT 3
#define ANNOTATION_STORE_FIRST_FORMAT 1
#define ANNOTATION_STORE_HEADER_BYTES 12
#define ANNOTATION_STORE_MAX_PAYLOAD (16u * 1024u * 1024u)

static void write_word(unsigned char *destination, const uint32_t value) {
    destination[0] = (unsigned char) (value & 0xff);
    destination[1] = (unsigned char) ((value >> 8) & 0xff);
    destination[2] = (unsigned char) ((value >> 16) & 0xff);
    destination[3] = (unsigned char) ((value >> 24) & 0xff);
}

static uint32_t read_word(const unsigned char *source) {
    return (uint32_t) source[0] | ((uint32_t) source[1] << 8) | ((uint32_t) source[2] << 16) |
           ((uint32_t) source[3] << 24);
}

static int replace_file(const char *source_path, const char *path) {
#ifdef _WIN32
    return MoveFileExA(source_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 1 : 0;
#else
    return rename(source_path, path) == 0 ? 1 : 0;
#endif
}

static int build_payload(const AnnotationSet *set, ByteBuffer *payload) {
    const unsigned char show_byte = set->show_on_minimap ? 1 : 0;

    if (!byte_buffer_append(payload, &show_byte, 1)) return 0;
    return annotation_tree_encode(set, 0, payload);
}

static int has_legacy_header(const char *path) {
    FILE *file = fopen(path, "rb");
    char magic[sizeof(ANNOTATION_STORE_MAGIC)];
    unsigned char header[ANNOTATION_STORE_HEADER_BYTES];
    int legacy = 0;

    if (file == NULL) return 0;
    if (fread(magic, sizeof(magic), 1, file) == 1 && memcmp(magic, ANNOTATION_STORE_MAGIC, sizeof(magic)) == 0 &&
        fread(header, 1, sizeof(header), file) == sizeof(header)) {
        const uint32_t format = read_word(header);
        legacy = format >= ANNOTATION_STORE_FIRST_FORMAT && format < ANNOTATION_STORE_FORMAT;
    }
    fclose(file);
    return legacy;
}

static void back_up_legacy_file(const char *path) {
    char backup_path[512];
    unsigned char chunk[4096];
    FILE *source;
    FILE *backup;
    size_t chunk_size;
    int copied = 1;

    if (!has_legacy_header(path)) return;
    snprintf(backup_path, sizeof(backup_path), "%s.format2.bak", path);
    backup = fopen(backup_path, "rb");
    if (backup != NULL) {
        fclose(backup);
        return;
    }

    source = fopen(path, "rb");
    if (source == NULL) return;
    backup = fopen(backup_path, "wb");
    if (backup == NULL) {
        fclose(source);
        return;
    }
    while (copied && (chunk_size = fread(chunk, 1, sizeof(chunk), source)) > 0) {
        copied = fwrite(chunk, 1, chunk_size, backup) == chunk_size;
    }
    if (ferror(source)) copied = 0;
    fclose(source);
    if (fclose(backup) != 0) copied = 0;
    if (!copied) remove(backup_path);
}

static int write_file(const char *path, const unsigned char *header, const unsigned char *compressed,
                      const size_t compressed_size) {
    FILE *file = fopen(path, "wb");
    int written;

    if (file == NULL) return 0;
    written = fwrite(ANNOTATION_STORE_MAGIC, sizeof(ANNOTATION_STORE_MAGIC), 1, file) == 1 &&
              fwrite(header, 1, ANNOTATION_STORE_HEADER_BYTES, file) == ANNOTATION_STORE_HEADER_BYTES &&
              fwrite(compressed, 1, compressed_size, file) == compressed_size;
    if (fclose(file) != 0) written = 0;
    return written;
}

int annotation_store_save(const char *path, AnnotationSet *set) {
    char temporary_path[512];
    ByteBuffer payload = {0};
    unsigned char header[ANNOTATION_STORE_HEADER_BYTES];
    unsigned char *compressed = NULL;
    uLongf compressed_size;
    int saved = 0;

    if (!build_payload(set, &payload)) {
        byte_buffer_release(&payload);
        return 0;
    }

    compressed_size = compressBound((uLong) payload.size);
    compressed = malloc(compressed_size);
    if (compressed != NULL &&
        compress2(compressed, &compressed_size, payload.bytes, (uLong) payload.size, Z_BEST_COMPRESSION) == Z_OK) {
        write_word(header, ANNOTATION_STORE_FORMAT);
        write_word(header + 4, (uint32_t) payload.size);
        write_word(header + 8, (uint32_t) compressed_size);

        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path);
        if (write_file(temporary_path, header, compressed, compressed_size)) {
            back_up_legacy_file(path);
            saved = replace_file(temporary_path, path);
        }
        if (saved) {
            set->changed = 0;
        } else {
            remove(temporary_path);
        }
    }

    free(compressed);
    byte_buffer_release(&payload);
    return saved;
}

static int read_payload(FILE *file, unsigned char **payload, size_t *payload_size, uint32_t *format) {
    char magic[sizeof(ANNOTATION_STORE_MAGIC)];
    unsigned char header[ANNOTATION_STORE_HEADER_BYTES];
    unsigned char *compressed;
    uLongf expanded_size;
    uint32_t stored_size;
    uint32_t compressed_size;
    int read_ok = 0;

    if (fread(magic, sizeof(magic), 1, file) != 1 || memcmp(magic, ANNOTATION_STORE_MAGIC, sizeof(magic)) != 0) {
        return 0;
    }
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) return 0;
    stored_size = read_word(header + 4);
    compressed_size = read_word(header + 8);
    *format = read_word(header);
    if (*format < ANNOTATION_STORE_FIRST_FORMAT || *format > ANNOTATION_STORE_FORMAT || stored_size == 0 ||
        stored_size > ANNOTATION_STORE_MAX_PAYLOAD || compressed_size == 0 ||
        compressed_size > compressBound(stored_size)) {
        return 0;
    }

    compressed = malloc(compressed_size);
    *payload = malloc(stored_size);
    expanded_size = stored_size;
    if (compressed != NULL && *payload != NULL && fread(compressed, 1, compressed_size, file) == compressed_size &&
        uncompress(*payload, &expanded_size, compressed, compressed_size) == Z_OK && expanded_size == stored_size) {
        *payload_size = stored_size;
        read_ok = 1;
    } else {
        free(*payload);
        *payload = NULL;
    }

    free(compressed);
    return read_ok;
}

static int parse_legacy_payload(const unsigned char *payload, const size_t payload_size, const uint32_t format,
                         AnnotationSet *set) {
    size_t offset = 0;
    unsigned int category_count;
    unsigned int index;

    if (payload_size < 1) return 0;
    set->show_on_minimap = payload[offset++] ? 1 : 0;
    if (!annotation_varint_read(payload, payload_size, &offset, &category_count)) return 0;
    if (category_count > ANNOTATION_MAX_CATEGORIES) return 0;

    for (index = 0; index < category_count; index++) {
        AnnotationCategory category;
        int visible;
        int show_labels = 1;

        if (offset >= payload_size) return 0;
        visible = payload[offset++] ? 1 : 0;
        if (format >= 2) {
            if (offset >= payload_size) return 0;
            show_labels = payload[offset++] ? 1 : 0;
        }
        if (annotation_category_decode(payload, payload_size, &offset, &category) != 1) return 0;
        category.visible = visible;
        category.show_labels = show_labels;
        if (!annotation_category_attach(set, &category, 0)) {
            annotation_category_release(&category);
            return 0;
        }
    }
    return offset == payload_size;
}

static int parse_payload(const unsigned char *payload, const size_t payload_size, const uint32_t format,
                         AnnotationSet *set) {
    AnnotationSet tree;
    size_t offset = 1;
    const int show_on_minimap = payload_size >= 1 && payload[0] ? 1 : 0;

    if (format < 3) return parse_legacy_payload(payload, payload_size, format, set);
    if (payload_size < 1 || annotation_tree_decode(payload, payload_size, &offset, 0, &tree) != 1) return 0;
    if (offset != payload_size) {
        annotation_set_release(&tree);
        return 0;
    }
    annotation_set_release(set);
    *set = tree;
    set->show_on_minimap = show_on_minimap;
    return 1;
}

static void move_aside(const char *path) {
    char bad_path[512];

    snprintf(bad_path, sizeof(bad_path), "%s.bad", path);
    if (!replace_file(path, bad_path)) remove(path);
}

int annotation_store_load(const char *path, AnnotationSet *set) {
    FILE *file = fopen(path, "rb");
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    uint32_t format = 0;
    int loaded;

    if (file == NULL) return 0;

    loaded = read_payload(file, &payload, &payload_size, &format) && parse_payload(payload, payload_size, format, set);
    fclose(file);
    free(payload);

    if (!loaded) {
        annotation_set_release(set);
        annotation_set_init(set);
        move_aside(path);
        return -1;
    }
    set->changed = 0;
    return 1;
}
