#ifndef CONFIG_H_HEADER_GUARD
#define CONFIG_H_HEADER_GUARD

#include <stdbool.h> // bool, true, false
#include <string.h>  // strcmp, strtok, strlen, strerror
#include <stdlib.h>  // strtol, strtod
#include <errno.h>   // errno
#include <assert.h>  // assert

#include <tini.h>

#include "common.h"

typedef struct {
	int r, g, b, a;
} Rgba;

enum {
	FILTERAUTO = 0,
	FILTERLINEAR,
	FILTERNEAREST,
	FILTERCOUNT,
};

enum {
	BORDERSHADOW = 0,
	BORDEROUTLINE,
	BORDERNONE,
	BORDERCOUNT,
};

enum {
	FITDEFAULT = 0,
	FITINT,
	FITNONE,
	FITCOUNT,
};

typedef struct {
	struct { // [window]
		bool fullscr;
		int  startw, starth, minw, minh;
	} win;
	struct { // [camera]
		double damping, zoomMax, zoomMin, zoomIn, zoomOut;
	} cam;
	struct { // [image]
		double animTime;
		int    filter, border, fitOnResize;
	} img;
	struct { // [colors]
		Rgba checkerboard[2], icons, darken, outline;
	} colors;
	struct { // [controls]
		double doubleClickTime;

		// TODO: Keybinds config
	} ctrls;
} Config;

extern Config conf;

void initConfig(const char *path);

#endif
