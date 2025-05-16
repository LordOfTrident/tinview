#ifndef VIEWER_H_HEADER_GUARD
#define VIEWER_H_HEADER_GUARD

#include <stdbool.h>      // bool, true, false
#include <stdint.h>       // uint32_t
#include <math.h>         // sin, floor
#include <string.h>       // strlen, strcpy, strncmp, strcat
#include <unistd.h>       // isatty, getcwd
#include <sys/stat.h>     // fstat
#include <linux/limits.h> // PATH_MAX

#include <SDL2/SDL.h>

#include "common.h"
#include "config.h"
#include "loader.h"

#define TITLE          "tinview"
#define TILESZ         10
#define FILTERICONTIME 1000

void view(const char *browsePath, const char **paths, int count);

#endif
