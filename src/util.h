#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "daemonify.h"

#ifndef UTIL_H
#define UTIL_H

#define DIRSEP "/"

char *basename(const char *path);
void *xmalloc(size_t size);
char *xstrdup(const char *str);
void xfree(void *ptr);
bool create_directories_for(const char *path);
void daemon_set_socket(DaemonConfig *config);
void daemon_set_defaults(DaemonConfig *config);

#endif  // UTIL_H
