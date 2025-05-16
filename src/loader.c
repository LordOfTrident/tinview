#include "loader.h"

void normalizeImagePath(const char *path, char *buf) {
	if (!*path) *buf = 0;
	else if (realpath(path, buf) == NULL)
		die("Failed to normalize image path \"%s\": %s", path, strerror(errno));
}

Image *newImage(const char *path) {
	Image *img = alloc(Image, 1);
	zeroMem(img);
	normalizeImagePath(path, img->path);
	pthread_mutex_init(&img->mutex, NULL);
	return img;
}

void freeImage(Image *img) {
	if (isImageLoading(img)) {
		/* Let's just detach the thread and not destroy the mutex, if an image is beeing free'd,
		   the program is probably about to quit anyways */
		int err = pthread_detach(img->thread);
		if (err != 0) die("Failed to detach image thread: %s", strerror(err));
	} else {
		if (isImageLoaded(img)) unloadImage(img);

		int err = pthread_mutex_destroy(&img->mutex);
		if (err != 0) die("Failed to destroy image mutex: %s", strerror(err));
	}
	free(img);
}

void lockImage(Image *img) {
	int err = pthread_mutex_lock(&img->mutex);
	if (err != 0) die("Failed to lock image mutex: %s", strerror(err));
}

void unlockImage(Image *img) {
	int err = pthread_mutex_unlock(&img->mutex);
	if (err != 0) die("Failed to unlock image mutex: %s", strerror(err));
}

bool isImageLoading(Image *img) {
	lockImage(img); bool tmp = img->loading; unlockImage(img);
	return tmp;
}

bool isImageLoaded(Image *img) {
	lockImage(img); bool tmp = img->loaded; unlockImage(img);
	return tmp;
}

// Annoying, but threads can only take data that's either global or heap-allocated
typedef struct {
	Image *img;
	Buffer buf;
} ImageThreadData;

#define imgError(IMG, ERR) ((IMG)->err = ERR)

static void *imageLoadingThread(void *data) {
	Image  *img =  ((ImageThreadData*)data)->img;
	Buffer *buf = &((ImageThreadData*)data)->buf;

	// TODO: Support https://platinumsrc.github.io/docs/formats/ptf/
	// Try to load as a gif first, because I don't know how to easily make STB check if it's a gif
	if ((img->pxs = (uint32_t*)stbi_load_gif_from_memory(buf->raw, buf->sz, &img->delays, &img->w,
	                                                     &img->h, &img->len, NULL, 4)) == NULL) {
		// If loading as a gif failed, assume it's not a gif
		img->isGif = false;
		if ((img->pxs = (uint32_t*)stbi_load_from_memory(buf->raw, buf->sz, &img->w,
		                                                 &img->h, NULL, 4)) == NULL)
			imgError(img, stbi_failure_reason());
	} else img->isGif = true;

	if (img->err == NULL && (img->w <= 0 || img->h <= 0)) imgError(img, "Invalid image dimensions");

	freeBuffer(buf);
	free(data);
	lockImage(img);
	img->loading = false;
	img->loaded  = img->err == NULL;
	if (img->loaded && img->deferredUnload) {
		img->deferredUnload = false;
		unloadImage(img);
	}
	unlockImage(img);
	return NULL;
}

static void startLoadingThread(Image *img, Buffer *buf) {
	assert(!isImageLoading(img));

	if (img->loaded) unloadImage(img);
	img->err     = NULL;
	img->loading = true;

	ImageThreadData *data = alloc(ImageThreadData, 1);
	data->img =  img;
	data->buf = *buf;
	int err = pthread_create(&img->thread, NULL, imageLoadingThread, data);
	if (err != 0) die("Failed to start image loading thread: %s", strerror(err));
}

// TODO: Potential race conditions?
static bool isPathAnImage(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return false;
	if (!S_ISREG(st.st_mode)) return false;
	/* If we can't even read the size of the image, we assume it is either not an image, or so
	   corrupted that we just won't classify it as an image */
	if (!stbi_info(path, NULL, NULL, NULL)) return false;
	return true;
}

void loadImage(Image *img) {
	if (!isPathAnImage(img->path)) {
		imgError(img, "File is not an image");
		return;
	}

	FILE *f = fopen(img->path, "rb");
	if (f == NULL) {
		imgError(img, strerror(errno));
		return;
	}

	Buffer buf;
	Error  err = readToBufferAtOnce(&buf, f);
	if (err != NULL) {
		imgError(img, err);
		fclose(f);
		return;
	}
	fclose(f);
	startLoadingThread(img, &buf);
}

void loadImageFromStdin(Image *img) {
	Buffer buf;
	Error  err = readToBufferByChunks(&buf, stdin); // ftell doesn't work with piping
	if (err != NULL) {
		imgError(img, err);
		return;
	}
	startLoadingThread(img, &buf);
}

void unloadImage(Image *img) {
	assert(img->loaded);
	img->loaded = false;

	/* Probably don't need to join the thread at this point
	int err = pthread_join(img->thread);
	if (err != 0) die("Failed to join image thread: %s", strerror(err));
	*/

	free(img->pxs);
	if (img->isGif) free(img->delays);
}

static void insertImage(Images *imgs, Image *img, int idx) {
	assert((size_t)idx <= imgs->sz);

	if (imgs->sz >= imgs->cap) resize(imgs->raw, imgs->cap *= 2);
	for (size_t i = ++imgs->sz; i --> (size_t)idx + 1;) imgs->raw[i] = imgs->raw[i - 1];
	imgs->raw[idx] = img;
}

static void removeImage(Images *imgs, int idx) {
	assert((size_t)idx <= imgs->sz);

	freeImage(imgs->raw[idx]);
	--imgs->sz;
	for (size_t i = idx; i < imgs->sz; ++i) imgs->raw[i] = imgs->raw[i + 1];
}

static int cmpNames(const char *a, const char *b) {
	// Which goes first in alphabetical order?
	int i;
	for (i = 0; a[i] != 0 && b[i] != 0; ++i) {
		char ch1 = isalpha(a[i])? tolower(a[i]) : a[i], ch2 = isalpha(b[i])? tolower(b[i]) : b[i];
		if (ch2 != ch1) return ch1 < ch2;
	}

	// Which is shorter?
	if (a[i] != 0 || a[i] != 0) return a[i] == 0;

	// Which has lowercase letters?
	for (i = 0; a[i] != 0; ++i) if (isalpha(a[i]) && a[i] != b[i]) return islower(a[i]);

	return 2; // They're equal
}

static void swapImages(Images *imgs, size_t a, size_t b) {
	Image *tmp = imgs->raw[a];
	imgs->raw[a] = imgs->raw[b];
	imgs->raw[b] = tmp;
}

static size_t quicksortImagesPartition(Images *imgs, int begin, int end) {
	int pivot = begin - 1;
	for (int i = begin; i < end; ++i)
		if (!cmpNames(imgs->raw[end]->path, imgs->raw[i]->path)) swapImages(imgs, i, ++pivot);
	swapImages(imgs, end, ++pivot);
	return pivot;
}

static void quicksortImages(Images *imgs, int begin, int end) {
	if (begin >= end) return;
	int pivot = quicksortImagesPartition(imgs, begin, end);
	quicksortImages(imgs, begin,     pivot - 1);
	quicksortImages(imgs, pivot + 1, end);
}

Error initImages(Images *imgs, const char *dirPath) {
	imgs->path = dirPath;
	imgs->sz   = 0;
	imgs->raw  = alloc(Image*, imgs->cap = IMGSCHUNKSZ);

	DIR *dir = opendir(imgs->path);
	if (dir == NULL) return strerror(errno);

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		char path[PATH_MAX]; // Using these static buffers should be ok, realpath uses them too
		strcpy(path, dirPath);
		strcat(path, "/");
		strcat(path, ent->d_name);

		// man readdir(3) says not all filesystems support d_type and they might return DT_UNKNOWN
		if (ent->d_type != DT_REG && ent->d_type != DT_UNKNOWN) continue;
		if (isPathAnImage(path)) {
			if (imgs->sz >= imgs->cap) resize(imgs->raw, imgs->cap *= 2);
			imgs->raw[imgs->sz++] = newImage(path);
		}
	}
	if (imgs->sz > 1) quicksortImages(imgs, 0, imgs->sz - 1);
	closedir(dir);

	if ((imgs->fd = inotify_init()) == -1) return strerror(errno);
	if ((imgs->wd = inotify_add_watch(imgs->fd, imgs->path, IN_CLOSE_WRITE | IN_DELETE)) == -1)
		return strerror(errno);
	return NULL;
}

void freeImages(Images *imgs) {
	if (imgs->fd != -1) {
		if (imgs->wd != -1) if (inotify_rm_watch(imgs->fd, imgs->wd) != 0)
			die("Failed to remove inotify watch: %s", strerror(errno));
		if (close(imgs->fd) != 0) die("Failed to close inotify: %s", strerror(errno));
	}

	for (size_t i = 0; i < imgs->sz; ++i) freeImage(imgs->raw[i]);
	free(imgs->raw);
}

bool searchImageByName(Images *imgs, const char *path, int *idx) {
	if (imgs->sz == 0) {
		*idx = 0;
		return false;
	}

	char normPath[PATH_MAX];
	normalizeImagePath(path, normPath);

	// Binary search
	int begin = 0, end = imgs->sz - 1;
	while (begin <= end) {
		*idx = begin + (end - begin)/2;
		switch (cmpNames(imgs->raw[*idx]->path, normPath)) {
		case 2: return true; // Equal
		case 1: begin = *idx + 1; break;
		case 0: end   = *idx - 1; break;
		}
	}

	// When not found, idx is where the image would have been or should be inserted at
	*idx += cmpNames(imgs->raw[*idx]->path, normPath);
	return false;
}

int getOrAddImage(Images *imgs, const char *path) {
	int idx;
	if (!searchImageByName(imgs, path, &idx)) insertImage(imgs, newImage(path), idx);
	return idx;
}

Error watchImages(Images *imgs) {
	if (imgs->fd == -1 || imgs->wd == -1) return NULL;

	int sz;
	if (ioctl(imgs->fd, FIONREAD, &sz) == -1) return strerror(errno);
	if (sz == 0) return NULL;

	Buffer buf;
	initBuffer(&buf, sz);
	if (read(imgs->fd, buf.raw, buf.sz) == -1) return strerror(errno);

	struct inotify_event *e;
	for (size_t off = 0; off < buf.sz; off += sizeof(*e) + e->len) {
		e = (struct inotify_event*)(buf.raw + off);
		char path[PATH_MAX];
		if (e->mask & IN_CLOSE_WRITE) {
			normalizeImagePath(e->name, path);
			/* Creation and modification are very similar actions, because creation could have the
			   same effect - if a loaded image got deleted from the disk, we keep it loaded, so if
			   it gets re-created, for us it's as if it got modified */
			int idx;
			if (searchImageByName(imgs, path, &idx)) {
				Image *img = imgs->raw[idx];
				if      (isImageLoaded(img))  unloadImage(img);
				else if (isImageLoading(img)) img->deferredUnload = true;
			} else if (isPathAnImage(path)) insertImage(imgs, newImage(path), idx);
		} else if (e->mask & IN_DELETE) {
			normalizeImagePath(e->name, path);
			int idx;
			if (!searchImageByName(imgs, path, &idx)) continue;
			/* Do not delete the image if it's loaded or still loading, because that means the
			   viewer might currently be viewing it */
			/* TODO: A way to signal to the viewer when an image is deleted, so that this can
			         delete loaded images too? */
			if (isImageLoaded(imgs->raw[idx]) || isImageLoading(imgs->raw[idx])) continue;
			removeImage(imgs, idx);
		}
	}
	freeBuffer(&buf);
	return NULL;
}
