#include "server_file.h"

#include <stdio.h>

static int is_safe_file_character(const char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '.' || character == '-' || character == '_';
}

void server_file_build_path(char *path, const size_t path_size, const char *directory, const char *prefix,
                            const char *host, const int port) {
    char safe_host[128];
    size_t position;

    for (position = 0; host[position] != '\0' && position < sizeof(safe_host) - 1; position++) {
        safe_host[position] = is_safe_file_character(host[position]) ? host[position] : '_';
    }
    safe_host[position] = '\0';

    snprintf(path, path_size, "%s%s%s_%d.dat", directory, prefix, safe_host, port);
}
