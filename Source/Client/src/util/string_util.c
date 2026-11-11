#include "string_util.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define DECIMAL_BASE 10

const char *skip_spaces(const char *text) {
    while (*text == ' ') text++;
    return text;
}

int is_blank(const char *text) {
    return *skip_spaces(text) == 0;
}

int starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

int read_int(const char **cursor, int *value) {
    char *end = NULL;

    errno = 0;
    const long parsed = strtol(*cursor, &end, DECIMAL_BASE);
    if (end == *cursor || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) return 0;

    *value = (int) parsed;
    *cursor = end;
    return 1;
}
