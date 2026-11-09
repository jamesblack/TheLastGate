#include "minimap_store.h"

#include "game/server_file.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* File layout: the four magic bytes, then 32-bit little-endian words for the format version, width, height and
 * compressed byte count, then the tiles compressed with zlib. */
static const char MINIMAP_MAGIC[4] = {'T', 'L', 'G', 'M'};
#define MINIMAP_FORMAT 1
#define MINIMAP_HEADER_WORDS 4

static size_t tile_byte_count(const MinimapTiles *minimap) {
    return (size_t) minimap->width * (size_t) minimap->height * sizeof(unsigned short);
}

void minimap_store_build_path(char *path, const size_t path_size, const char *directory, const char *host,
                              const int port) {
    server_file_build_path(path, path_size, directory, "TLGMap_", host, port);
}

void minimap_store_set_tile(MinimapTiles *minimap, const int tile_x, const int tile_y, const unsigned short colour) {
    unsigned short *tile;

    if (tile_x < 0 || tile_y < 0 || tile_x >= minimap->width || tile_y >= minimap->height) return;

    tile = &minimap->tiles[tile_x * minimap->height + tile_y];
    if (*tile != colour) {
        *tile = colour;
        minimap->changed = 1;
    }
}

void minimap_store_clear_box(MinimapTiles *minimap, const int left, const int top, const int box_width,
                             const int box_height) {
    const int first_x = left < 0 ? 0 : left;
    const int first_y = top < 0 ? 0 : top;
    const long right = (long) left + box_width;
    const long bottom = (long) top + box_height;
    const int end_x = right > minimap->width ? minimap->width : (int) right;
    const int end_y = bottom > minimap->height ? minimap->height : (int) bottom;
    int tile_x;
    int tile_y;

    for (tile_x = first_x; tile_x < end_x; tile_x++) {
        for (tile_y = first_y; tile_y < end_y; tile_y++) {
            minimap_store_set_tile(minimap, tile_x, tile_y, 0);
        }
    }
}

static int replace_file(const char *temporary_path, const char *path) {
#ifdef _WIN32
    return MoveFileExA(temporary_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 1 : 0;
#else
    return rename(temporary_path, path) == 0 ? 1 : 0;
#endif
}

static int write_file(const char *path, const uint32_t header[MINIMAP_HEADER_WORDS], const unsigned char *compressed,
                      const size_t compressed_size) {
    FILE *file = fopen(path, "wb");
    int written;

    if (file == NULL) return 0;
    written = fwrite(MINIMAP_MAGIC, sizeof(MINIMAP_MAGIC), 1, file) == 1 &&
              fwrite(header, sizeof(uint32_t), MINIMAP_HEADER_WORDS, file) == MINIMAP_HEADER_WORDS &&
              fwrite(compressed, 1, compressed_size, file) == compressed_size;
    if (fclose(file) != 0) written = 0;
    return written;
}

int minimap_store_save(const char *path, MinimapTiles *minimap) {
    char temporary_path[512];
    uLongf compressed_size = compressBound((uLong) tile_byte_count(minimap));
    unsigned char *compressed = malloc(compressed_size);
    uint32_t header[MINIMAP_HEADER_WORDS];
    int saved = 0;

    if (compressed == NULL) return 0;

    if (compress2(compressed, &compressed_size, (const Bytef *) minimap->tiles, (uLong) tile_byte_count(minimap),
                  Z_BEST_SPEED) == Z_OK) {
        header[0] = MINIMAP_FORMAT;
        header[1] = (uint32_t) minimap->width;
        header[2] = (uint32_t) minimap->height;
        header[3] = (uint32_t) compressed_size;

        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path);
        if (write_file(temporary_path, header, compressed, compressed_size) && replace_file(temporary_path, path)) {
            saved = 1;
            minimap->changed = 0;
        } else {
            remove(temporary_path);
        }
    }

    free(compressed);
    return saved;
}

static int read_tiles(FILE *file, MinimapTiles *minimap) {
    char magic[sizeof(MINIMAP_MAGIC)];
    uint32_t header[MINIMAP_HEADER_WORDS];
    unsigned char *compressed;
    uLongf tile_size = (uLongf) tile_byte_count(minimap);
    int loaded = 0;

    if (fread(magic, sizeof(magic), 1, file) != 1 || memcmp(magic, MINIMAP_MAGIC, sizeof(magic)) != 0) return 0;
    if (fread(header, sizeof(uint32_t), MINIMAP_HEADER_WORDS, file) != MINIMAP_HEADER_WORDS) return 0;
    if (header[0] != MINIMAP_FORMAT || header[1] != (uint32_t) minimap->width ||
        header[2] != (uint32_t) minimap->height || header[3] == 0 || header[3] > compressBound(tile_size)) {
        return 0;
    }

    compressed = malloc(header[3]);
    if (compressed == NULL) return 0;

    if (fread(compressed, 1, header[3], file) == header[3] &&
        uncompress((Bytef *) minimap->tiles, &tile_size, compressed, header[3]) == Z_OK &&
        tile_size == (uLongf) tile_byte_count(minimap)) {
        loaded = 1;
    }

    free(compressed);
    return loaded;
}

int minimap_store_load(const char *path, MinimapTiles *minimap) {
    FILE *file = fopen(path, "rb");
    int loaded = 0;

    if (file != NULL) {
        loaded = read_tiles(file, minimap);
        fclose(file);
    }

    if (!loaded) memset(minimap->tiles, 0, tile_byte_count(minimap));
    minimap->changed = 0;
    return loaded;
}

int minimap_store_delete(const char *path) {
    char temporary_path[512];
    FILE *file;

    snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path);
    remove(temporary_path);
    remove(path);

    file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return 0;
    }
    return 1;
}
