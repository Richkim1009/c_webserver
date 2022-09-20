#ifndef WEBSERVER_STRING_H
#define WEBSERVER_STRING_H

#include <stdbool.h>

extern void remove_crlf(char *str);
extern bool str_starts_with(char *s, char *prefix);
extern bool str_ends_with(char *s, char *suffix);

#endif