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
