#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void server_file_build_path(char *path, size_t path_size, const char *directory, const char *prefix, const char *host,
                            int port);

#ifdef __cplusplus
}
#endif
