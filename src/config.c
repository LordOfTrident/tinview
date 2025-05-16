#include "config.h"

#define DEFAULTCONF \
	"[window]\n"                                       \
	"fullscreen   = false # false/true\n"              \
	"size.startup = 640x480\n"                         \
	"size.min     = 64x64\n"                           \
	"\n"                                               \
	"[camera]\n"                                       \
	"damping  = 0.02 # Per millisecond\n"              \
	"zoom.max = 200\n"                                 \
	"zoom.min = 0.01\n"                                \
	"zoom.in  = 0.2\n"                                 \
	"zoom.out = 0.2\n"                                 \
	"\n"                                               \
	"[image]\n"                                        \
	"anim-time     = 200     # In milliseconds\n"      \
	"filtering     = auto    # auto/linear/nearest\n"  \
	"border        = shadow  # shadow/outline/none\n"  \
	"fit-on-resize = default # default/integer/none\n" \
	"\n"                                               \
	"[colors]\n"                                       \
	"checkerboard.a = 363636FF # Opacity ignored\n"    \
	"checkerboard.b = 424242FF # Opacity ignored\n"    \
	"icons          = FFFFFFFF\n"                      \
	"darkening      = 00000050\n"                      \
	"outline        = 00000080\n"                      \
	"\n"                                               \
	"[controls]\n"                                     \
	"double-click-time = 300 # In milliseconds\n"


Config conf = {
	.win = {
		.fullscr = false,
		.startw  = 640,
		.starth  = 480,
		.minw    = 128,
		.minh    = 128,
	},
	.cam = {
		.damping = 0.02,
		.zoomMax = 200,
		.zoomMin = 0.01,
		.zoomIn  = 0.2,
		.zoomOut = 0.2,
	},
	.img = {
		.animTime    = 200,
		.filter      = FILTERAUTO,
		.border      = BORDERSHADOW,
		.fitOnResize = FITDEFAULT,
	},
	.colors = {
		.checkerboard = {{0x36, 0x36, 0x36, 0xFF}, {0x42, 0x42, 0x42, 0xFF}},
		.icons        = {0xFF, 0xFF, 0xFF, 0xFF},
		.darken       = {0x00, 0x00, 0x00, 0x50},
		.outline      = {0x00, 0x00, 0x00, 0x80},
	},
	.ctrls = {
		.doubleClickTime = 300,
	},
};

static const char *boolStrs[] = {
	[false] = "false",
	[true]  = "true",
}, *filterStrs[FILTERCOUNT] = {
	[FILTERAUTO]    = "auto",
	[FILTERLINEAR]  = "linear",
	[FILTERNEAREST] = "nearest",
}, *borderStrs[BORDERCOUNT] = {
	[BORDERSHADOW]  = "shadow",
	[BORDEROUTLINE] = "outline",
	[BORDERNONE]    = "none",
}, *fitStrs[FITCOUNT] = {
	[FITDEFAULT] = "default",
	[FITINT]     = "integer",
	[FITNONE]    = "none",
};

#define parseEnum(VAL, RES, ARR) parseEnum_(VAL, RES, ARR, lenOf(ARR))

static Error parseEnum_(char *val, int *res, const char **strs, int count) {
	for (int i = 0; i < count; ++i) {
		if (strcmp(val, strs[i]) == 0) {
			*res = i;
			return NULL;
		}
	}
	return "Invalid enum value";
}

static Error parseBool(char *val, bool *res) {
	int   tmp = 0;
	Error err = parseEnum(val, &tmp, boolStrs);
	*res = tmp;
	return err;
}

static Error parseSize(char *val, int *resW, int *resH) {
	char *tmp, *w = val, *h = strtok(val, "x");
	if (h == NULL) return "Invalid size format";

	*resW = strtol(w, &tmp, 10);
	if (tmp  == w) return "Missing width";
	if (*tmp != 0) return "Invalid width";

	*resH = strtol(h, &tmp, 10);
	if (tmp  == h) return "Missing height";
	if (*tmp != 0) return "Invalid height";
	return NULL;
}

static Error parseNumber(char *val, double *res) {
	char *tmp;
	*res = strtod(val, &tmp);
	if (tmp  == val) return "Missing number";
	if (*tmp != 0)   return "Not a number";
	if (*res < 0)    return "Number cannot be negative"; // Sanity check
	return NULL;
}

static bool getRgbaComponent(char *val, int *res) {
	char *tmp, component[3] = {*val, val[1], 0};
	*res = strtol(component, &tmp, 16);
	assert(tmp != component); // This should never happen because of length check inside parseRgba
	return *tmp == 0;
}

static Error parseRgba(char *val, Rgba *res) {
	if (strlen(val) != 8) return "Invalid RGBA color (expected 8 hexadecimal digits)";

	if (!getRgbaComponent(val,     &res->r)) return "Invalid RGBA component R";
	if (!getRgbaComponent(val + 2, &res->g)) return "Invalid RGBA component G";
	if (!getRgbaComponent(val + 4, &res->b)) return "Invalid RGBA component B";
	if (!getRgbaComponent(val + 6, &res->a)) return "Invalid RGBA component A";
	return NULL;
}

static Error hook(char *sect, char *key, char *val, void *data) {
	unused(data);

#define parseRule(KEY, FN, ...) if (strcmp(key, KEY) == 0) return FN(val, __VA_ARGS__)
	if (strcmp(sect, "window") == 0) {
		parseRule("fullscreen",   parseBool, &conf.win.fullscr);
		parseRule("size.startup", parseSize, &conf.win.startw, &conf.win.starth);
		parseRule("size.min",     parseSize, &conf.win.minw,   &conf.win.minh);
	} else if (strcmp(sect, "camera") == 0) {
		parseRule("damping",  parseNumber, &conf.cam.damping);
		parseRule("zoom.max", parseNumber, &conf.cam.zoomMax);
		parseRule("zoom.min", parseNumber, &conf.cam.zoomMin);
		parseRule("zoom.in",  parseNumber, &conf.cam.zoomIn);
		parseRule("zoom.out", parseNumber, &conf.cam.zoomOut);
	} else if (strcmp(sect, "image") == 0) {
		parseRule("anim-time",     parseNumber, &conf.img.animTime);
		parseRule("filtering",     parseEnum,   &conf.img.filter,      filterStrs);
		parseRule("border",        parseEnum,   &conf.img.border,      borderStrs);
		parseRule("fit-on-resize", parseEnum,   &conf.img.fitOnResize, fitStrs);
	} else if (strcmp(sect, "colors") == 0) {
		parseRule("checkerboard.a", parseRgba, &conf.colors.checkerboard[0]);
		parseRule("checkerboard.b", parseRgba, &conf.colors.checkerboard[1]);
		parseRule("icons",          parseRgba, &conf.colors.icons);
		parseRule("darkening",      parseRgba, &conf.colors.darken);
		parseRule("outline",        parseRgba, &conf.colors.outline);
	} else if (strcmp(sect, "controls") == 0) {
		parseRule("double-click-time", parseNumber, &conf.ctrls.doubleClickTime);
	} else return "Invalid section";

	return "Invalid key";
}

static void createDefaultConfig(const char *path) {
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		error("Failed to create default config: %s", strerror(errno));
		return;
	}
	fputs(DEFAULTCONF, f);
	fclose(f);
}

void initConfig(const char *path) {
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		if (errno == ENOENT) createDefaultConfig(path);
		else error("Failed to open config: %s", strerror(errno));
		return;
	}

	int   errLine;
	Error err = tiniParseFile(f, hook, &errLine, NULL);
	if (err != NULL) {
		if (errLine > 0) error("Config error: %s:%i: %s", path, errLine, err);
		else error("Config error: %s: %s", path, err);
	}
}
