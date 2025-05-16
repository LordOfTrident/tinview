#ifndef COMMON_H_HEADER_GUARD
#define COMMON_H_HEADER_GUARD

#include <stddef.h>       // size_t
#include <stdlib.h>       // malloc, realloc, free, exit, EXIT_FAILURE, getenv
#include <string.h>       // strerror, strcpy
#include <stdbool.h>      // bool, true, false
#include <errno.h>        // errno
#include <assert.h>       // assert
#include <stdarg.h>       // va_start, va_end
#include <stdio.h>        // fprintf, stderr, vsnprintf, fseek, ftell, rewind, fread, fgetc
#include <stdint.h>       // uint8_t
#include <time.h>         // time, localtime
#include <linux/limits.h> // PATH_MAX

#include <SDL2/SDL.h>

typedef const char *Error;

#define lenOf(ARR) (sizeof(ARR)/(sizeof(*(ARR))))
#define unused(X) ((void)X)

#define die(...)  (error_(true,  "Fatal error", __func__, __VA_ARGS__), exit(EXIT_FAILURE))
#define error(...) error_(false, "Error",       __func__, __VA_ARGS__)
void error_(bool msgBox, const char *type, const char *fn, const char *fmt, ...);

#define zeroMem(PTR)    memset(PTR, 0, sizeof(*(PTR)))
#define alloc(TYPE, SZ) ((TYPE*)alloc_((SZ)*sizeof(TYPE)))
#define resize(PTR, SZ) (PTR = resize_(PTR, (SZ)*sizeof(*(PTR))))
void *alloc_(size_t sz);
void *resize_(void *ptr, size_t sz);

const char *home(void);

typedef struct {
	uint8_t *raw;
	size_t   sz;
} Buffer;

#define BUFCHUNKSZ (256*256)

void  initBuffer(Buffer *buf, size_t sz);
Error readToBufferAtOnce(Buffer *buf, FILE *f);
Error readToBufferByChunks(Buffer *buf, FILE *f);
void  freeBuffer(Buffer *buf);

#endif
