#include "string.h"
#include <string.h>

void remove_crlf(char *str)
{
    int len = strlen(str);
    if (str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
    if (str[len - 2] == '\r') {
        str[len - 2] = '\0';
    }
}

bool str_starts_with(char *s, char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool str_ends_with(char *s, char *suffix)
{
    int s_len = strlen(s);
    int suffix_len = strlen(suffix);

    if (s_len < suffix_len) {
        return false;
    }

    return strncmp(s + s_len - suffix_len + 1, suffix + 1, suffix_len - 1) == 0;
}