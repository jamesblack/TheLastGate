#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const char *skip_spaces(const char *text);

int is_blank(const char *text);

int starts_with(const char *text, const char *prefix);

int read_int(const char **cursor, int *value);

#ifdef __cplusplus
}
#endif
