#include <stdio.h>
#include <stdlib.h>
#include <string.h>


char *copy_string(const char *s)
{
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len + 1);
    return copy;
}

void format_real(char *buf, size_t size, double x)
{
    snprintf(buf, size, "%.6f", x);
    if (strcmp(buf, "-0.000000") == 0) {
        strcpy(buf, "0.000000");
    }
}
