#include <stdio.h>        // printf, fprintf, stderr
#include <stdlib.h>       // getenv, exit, EXIT_FAILURE
#include <string.h>       // strcpy, strcat, strerror, strcmp, strlen
#include <errno.h>        // errno
#include <sys/stat.h>     // mkdir
#include <libgen.h>       // dirname
#include <linux/limits.h> // PATH_MAX

#define VERSION "1.0.1"

#include "common.h"
#include "config.h"
#include "viewer.h"

static const char **paths, *browsePath = NULL;
static int          pathCount;

static void usage(void) {
	printf("tinview (v"VERSION", compiled on "__DATE__")\n"
	       "  A pretty and minimalist Linux image viewer\n"
	       "\n"
	       "Usage: tinview [FILE...] [-h | --help] [-v | --version] [-d DIR | --dir DIR]\n"
	       "Github: https://github.com/lordoftrident/tinview\n"
	       "Options:\n"
	       "  -h, --help       Prints the usage and version information\n"
	       "  -v, --version    Prints the version\n"
	       "  -d, --dir        Sets the image browsing directory\n"
	       "\n"
	       "For other information, see the program's manpage tinview(1)\n");
	exit(0);
}

static void version(void) {
	printf("tinview v"VERSION"\n");
	exit(0);
}

static const char *flagArg(const char ***argv) {
	if (*++*argv == NULL) {
			fprintf(stderr, "Error: Flag \"%s\" expects directory path\n", (*argv)[-1]);
			exit(EXIT_FAILURE);
	}
	return **argv;
}

static void parseArgs(int argc, const char **argv) {
	paths = alloc(const char*, argc);

#define flag(SHORT, LONG) (strcmp(*argv, "-"SHORT) == 0 || strcmp(*argv, "--"LONG) == 0)

	while (*++argv != NULL) {
		// TODO: Config flags, which would override config
		if      (flag("h", "help"))    usage();
		else if (flag("v", "version")) version();
		else if (flag("d", "dir"))     browsePath = flagArg(&argv);
		else if (**argv == '-') {
			fprintf(stderr, "Error: Unknown flag \"%s\", try \"--help\"\n", *argv);
			exit(EXIT_FAILURE);
		} else paths[pathCount++] = *argv;
	}
}

static void checkDir(const char *path) {
	if (mkdir(path, 0777) != 0)
		if (errno != EEXIST) error("Failed to create directory \"%s\": %s", path, strerror(errno));
}

int main(int argc, const char **argv) {
	parseArgs(argc, argv);

	char path[PATH_MAX];
	strcpy(path, home());
	strcat(path, "/.config/tinview");
	checkDir(path);
	strcat(path, "/config.ini");
	initConfig(path);

	if (browsePath == NULL) {
		if (pathCount) {
			// Get the parent directory of the first provided image
			strcpy(path, *paths);
			browsePath = dirname(path);
		} else browsePath = ".";
	}

	view(browsePath, paths, pathCount);
	free(paths);
	return 0;
}
