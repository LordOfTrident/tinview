#ifndef LOADER_H_HEADER_GUARD
#define LOADER_H_HEADER_GUARD

#include <stdio.h>        // fopen, fclose, stdin
#include <stdlib.h>       // realpath
#include <stdbool.h>      // bool, true, false
#include <stdint.h>       // uint8_t, uint64_t
#include <string.h>       // strerror, strcpy, strcat
#include <ctype.h>        // isalpha, tolower
#include <errno.h>        // errno
#include <assert.h>       // assert
#include <dirent.h>       // opendir, closedir, readdir
#include <pthread.h>      // pthread_*, pthread_mutex_*
#include <unistd.h>       // close
#include <sys/stat.h>     // stat
#include <sys/inotify.h>  // inotify_*
#include <sys/ioctl.h>    // ioctl
#include <linux/limits.h> // PATH_MAX

#include <stb_image.h>
#include <webp/decode.h>
// TODO: libwebp caused tinview size to go from around 100k to 500k

#include "common.h"

#define IMGSTDIN ""

typedef struct {
	char     path[PATH_MAX];
	int      w, h;
	uint8_t *pxs;
	bool     isGif;
	int     *delays, len; // Only for gifs

	bool flipv, fliph; // Vertical and horizontal flip
	int  rot; // 0 - 3, rot*90 translates to degrees

	/* loading        - Image is currently being loaded in a thread
	 * loaded         - Image loading has succesfully finished
	 * deferredUnload - Unload the image when it's finished loading
	 * err            - Image loading error, NULL if no error
	 */
	bool  loading, loaded, deferredUnload;
	Error err;
	pthread_t       thread;
	pthread_mutex_t mutex;
} Image;

void normalizeImagePath(const char *path, char *buf);

Image *newImage(const char *path);
void   freeImage(Image *img);

void lockImage     (Image *img);
void unlockImage   (Image *img);
bool isImageLoading(Image *img);
bool isImageLoaded (Image *img);

void loadImage(Image *img);
void loadImageFromStdin(Image *img);
void unloadImage(Image *img);

// Ordered list of images
typedef struct {
	const char *path;
	Image     **raw;
	size_t      sz, cap;
	int         fd, wd; // inotify and watch file descriptors
} Images;

#define IMGSCHUNKSZ 128

Error initImages(Images *imgs, const char *dirPath);
void  freeImages(Images *imgs);
Error watchImages(Images *imgs);
bool  searchImageByName(Images *imgs, const char *path, int *idx); // TODO: Use size_t for indexes?
int   getOrAddImage(Images *imgs, const char *path);

#endif
