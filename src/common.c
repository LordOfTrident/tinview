#include "common.h"

void error_(bool msgBox, const char *type, const char *fn, const char *fmt, ...) {
	char    buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	fprintf(stderr, "%s from %s(): %s\n", type, fn, buf);
	if (msgBox) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, type, buf, NULL);
}

void *alloc_(size_t sz) {
	assert(sz > 0);

	void *ptr = malloc(sz);
	if (ptr == NULL) die("Failed to allocate %zu bytes: %s", sz, strerror(errno));
	return ptr;
}

void *resize_(void *ptr, size_t sz) {
	assert(sz > 0);

	if ((ptr = realloc(ptr, sz)) == NULL)
		die("Failed to reallocate %zu bytes: %s", sz, strerror(errno));
	return ptr;
}

const char *home(void) {
	static char path[PATH_MAX] = {0};
	if (!*path) strcpy(path, getenv("HOME"));
	return path;
}

void initBuffer(Buffer *buf, size_t sz) {
	buf->raw = alloc(uint8_t, buf->sz = sz);
}

Error readToBufferAtOnce(Buffer *buf, FILE *f) {
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	if (sz == -1) return strerror(errno);
	rewind(f);

	initBuffer(buf, sz);
	if (fread(buf->raw, 1, buf->sz, f) < buf->sz) {
		freeBuffer(buf);
		return "fread() fail";
		// Apparently, fread setting errno is a POSIX extension, and GNU doesn't follow it
		//return strerror(errno);
	}
	return NULL;
}

Error readToBufferByChunks(Buffer *buf, FILE *f) {
	int    byte;
	size_t cap = BUFCHUNKSZ;
	buf->sz  = 0;
	buf->raw = alloc(uint8_t, cap);
	while ((byte = fgetc(f)) != EOF) {
		if (buf->sz >= cap) resize(buf->raw, cap *= 2);
		buf->raw[buf->sz++] = byte;
	}
	if (ferror(f)) {
		freeBuffer(buf);
		return strerror(errno); // fgetc should set errno, right?
	}

	// Make sure buffer is not too big
	if (buf->sz != cap) resize(buf->raw, buf->sz);
	return NULL;
}

void freeBuffer(Buffer *buf) {
	free(buf->raw);
}
