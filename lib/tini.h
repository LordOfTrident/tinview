/* tini - Tiny STB-style INI parser library for C99
 *
 * Licensed under MIT
 * See: https://github.com/lordoftrident/tini
 *
 * ========= FEATURES =========
 *
 * - Inline comments
 * - Quoted strings, optionally multi-line
 * - Escapes (newline and quote escaping)
 * - Customizable (syntax and memory options)
 * - Tiny implementation and memory footprint (uses a single buffer, one key/value pair at a time)
 *
 * ========== USAGE ===========
 *
 * Use:
 *     #define TINI_IMPLEMENTATION
 * to include the implementation of the library.
 *
 * Use, for example:
 *     #define tiniAlloc(SZ)        myMalloc(SZ)
 *     #define tiniRealloc(PTR, SZ) myRealloc(PTR, SZ)
 *     #define tiniFree(PTR)        myFree(PTR)
 * for custom memory allocation functions.
 *
 * Use, for example:
 *     #define tiniIsSeparator(CH) ((CH) == ':')
 *     #define tiniIsComment(CH)   ((CH) == '#')
 *     #define tiniIsQuote(CH)     ((CH) == '`' || (CH) == '"')
 * to customize the syntax.
 *
 * Use, for example:
 *     #define TINI_DEFAULTSECTION "my-default-section"
 * to set a custom default section.
 *
 * Use:
 *     #define TINI_MULTILINE
 * To enable multi-line quoted strings.
 *
 * Use:
 *     #define TINI_INLINECOMMENTS
 * To enable inline comments.
 *
 * Use, for example:
 *     #define TINI_CHUNKSZ 64
 * to set the internal buffer chunk size.
 *
 * Use, for example:
 *     #define TINI_DEF static
 * to change how API functions are defined.
 *
 * API functions:
 *     tiniParseCstr - Parse INI from a null-terminated string
 *     tiniParseFile - Parse INI from a file stream
 *     tiniParsePath - Parse INI from the file at the given path
 * Each of these functions returns NULL on success and an error message on failure. If the errLine
 * parameter is not NULL, the line number (starting from 1) on which the error occured will be
 * stored there. If the error does not come from a specific line, errLine will equal -1.
 */

#ifndef TINI_H_HEADER_GUARD
#define TINI_H_HEADER_GUARD

#include <stdio.h>   // fopen, fclose, fgetc
#include <stdbool.h> // bool, true, false
#include <string.h>  // strerror
#include <errno.h>   // errno
#include <ctype.h>   // isalpha
#include <stddef.h>  // size_t

#ifndef TINI_DEF
#	define TINI_DEF
#endif
#ifndef TINI_DEFAULTSECTION
#	define TINI_DEFAULTSECTION ""
#endif

// Hook is called for each key/value pair
typedef const char *(*TiniHook)(char *sect, char *key, char *val, void *data);

TINI_DEF const char *tiniParseCstr(const char *str,  TiniHook fn, int *errLine, void *data);
TINI_DEF const char *tiniParseFile(FILE       *file, TiniHook fn, int *errLine, void *data);
TINI_DEF const char *tiniParsePath(const char *path, TiniHook fn, int *errLine, void *data);

#endif

#ifdef TINI_IMPLEMENTATION

#ifndef TINI_CHUNKSZ
#	define TINI_CHUNKSZ 256
#endif
#ifndef tiniIsSeparator
#	define tiniIsSeparator(CH) ((CH) == '=')
#endif
#ifndef tiniIsComment
#	define tiniIsComment(CH) ((CH) == '#' || (CH) == ';')
#endif
#ifndef tiniIsQuote
#	define tiniIsQuote(CH) ((CH) == '"')
#endif
#if !defined(tiniAlloc) && !defined(tiniRealloc) && !defined(tiniFree)
#	include <stdlib.h> // malloc, realloc, free
#	define tiniAlloc(SZ)        malloc(SZ)
#	define tiniRealloc(PTR, SZ) realloc(PTR, SZ)
#	define tiniFree(PTR)        free(PTR)
#endif

typedef struct {
	bool file;
	union {
		FILE       *f;
		const char *s;
	} src;

	TiniHook    fn;
	int        *line, ch;
	const char *err;
	char       *buf, *key, *val; // Buffer, and pointers to the key and value inside it
	size_t      cap, sz;         // Capacity and size of the buffer
	void       *data;            // User data
} Tini;

#define tiniError(CTX, ERR) ((CTX)->err = ERR, false)

static int tiniConsume(Tini *ctx) {
	if (ctx->ch == '\n' && ctx->line) ++*ctx->line;
	if (ctx->file? (ctx->ch = fgetc(ctx->src.f)) == EOF : !(ctx->ch = *ctx->src.s++)) ctx->ch = 0;
	return ctx->ch;
}

static bool tiniAddCh(Tini *ctx, char ch) {
	if (ctx->sz >= ctx->cap) {
		char *tmp = tiniRealloc(ctx->buf, ctx->cap *= 2);
		if (tmp == NULL) {
			free(ctx->buf);
			if (ctx->line) *ctx->line = -1;
			return tiniError(ctx, strerror(errno));
		} else ctx->buf = tmp;
	}
	ctx->buf[ctx->sz++] = ch;
	return true;
}

static bool tiniParseSection(Tini *ctx) {
	ctx->sz = 0;
	for (tiniConsume(ctx); ctx->ch != ']'; tiniConsume(ctx)) {
		if (!ctx->ch || ctx->ch == '\n') return tiniError(ctx, "Section name ends unexpectedly");
		if (!tiniAddCh(ctx, ctx->ch)) return false;
	}
	tiniConsume(ctx); // Skip the ]
	if (!tiniAddCh(ctx, 0)) return false;
	ctx->key = ctx->buf + ctx->sz;
	return true;
}

static bool tiniParseKey(Tini *ctx) {
	for (; ctx->ch && !isspace(ctx->ch) && !tiniIsSeparator(ctx->ch); tiniConsume(ctx))
		if (!tiniAddCh(ctx, ctx->ch)) return false;
	if (!tiniAddCh(ctx, 0)) return false;
	ctx->val = ctx->buf + ctx->sz;
	if (!*ctx->key) return tiniError(ctx, "Key is empty");
	return true;
}

static bool tiniParseSeparator(Tini *ctx) {
	// Skip whitespaces before separator
	while (isspace(ctx->ch) && ctx->ch != '\n') tiniConsume(ctx);
	if (!tiniIsSeparator(ctx->ch)) return tiniError(ctx, "Missing separator for key's value");
	tiniConsume(ctx); // Consume separator
	// Skip whitespaces after separator
	while (isspace(ctx->ch) && ctx->ch != '\n') tiniConsume(ctx);
	return true;
}

static bool tiniParseValue(Tini *ctx) {
	bool   quoted = false, escaped = false;
	size_t realSz = ctx->sz; // For trimming trailing whitespaces
	for (; ctx->ch; tiniConsume(ctx)) {
		if (escaped) {
			escaped = false;
			if (ctx->ch == '\n') continue;
			if (ctx->ch != '\\' && !tiniIsQuote(ctx->ch))
				if (!tiniAddCh(ctx, '\\')) return false;
			if (!tiniAddCh(ctx, ctx->ch)) return false;
			realSz = ctx->sz;
			continue;
		} else if (ctx->ch == '\\') {
			escaped = true;
			continue;
		} else if (tiniIsQuote(ctx->ch)) {
			quoted = !quoted;
			continue;
		}

#ifdef TINI_INLINECOMMENTS
		if (!quoted && (ctx->ch == '\n' || tiniIsComment(ctx->ch))) break;
#else
		if (!quoted && ctx->ch == '\n') break;
#endif
		if (!tiniAddCh(ctx, ctx->ch)) return false;
		if (!quoted && isspace(ctx->ch)) continue;
#ifndef TINI_MULTILINE
		if (quoted && ctx->ch == '\n') return tiniError(ctx, "Unterminated string");
#endif
		realSz = ctx->sz;
	}
	ctx->sz = realSz;
	if (quoted && !ctx->ch) return tiniError(ctx, "Unterminated string");
	if (!tiniAddCh(ctx, 0)) return false;
	return true;
}

static bool tiniParseNext(Tini *ctx) {
	for (;;) {
		while (isspace(ctx->ch)) if (!tiniConsume(ctx)) return false; // Skip whitespace
		if (!tiniIsComment(ctx->ch)) break;
		while (ctx->ch != '\n') if (!tiniConsume(ctx)) return false; // Skip comment
	}
	if (ctx->ch == '[') return tiniParseSection(ctx);

	// Parse key/value pair
	ctx->sz = ctx->key - ctx->buf;
	if (!tiniParseKey(ctx))       return false;
	if (!tiniParseSeparator(ctx)) return false;
	if (!tiniParseValue(ctx))     return false;
	if ((ctx->err = ctx->fn(ctx->buf, ctx->key, ctx->val, ctx->data)) != NULL) return false;
	return ctx->ch;
}

static void tiniParse(Tini *ctx) {
	if (!tiniConsume(ctx)) return; // Consume first character
	if ((ctx->buf = tiniAlloc(ctx->cap = TINI_CHUNKSZ)) == NULL) {
		ctx->err = strerror(errno);
		return;
	}

	// Set the current section to the default
	for (size_t i = 0; i < sizeof(TINI_DEFAULTSECTION); ++i)
		if (!tiniAddCh(ctx, TINI_DEFAULTSECTION[i])) return;
	ctx->key = ctx->buf + ctx->sz;

	if (ctx->line) *ctx->line = 1;
	while (tiniParseNext(ctx));
	tiniFree(ctx->buf);
}

TINI_DEF const char *tiniParseCstr(const char *str, TiniHook fn, int *errLine, void *data) {
	if (errLine) *errLine = -1;
	Tini ctx = {.file = false, .src = {.s = str}, .fn = fn, .line = errLine, .data = data};
	tiniParse(&ctx);
	return ctx.err;
}

TINI_DEF const char *tiniParseFile(FILE *file, TiniHook fn, int *errLine, void *data) {
	if (errLine) *errLine = -1;
	Tini ctx = {.file = true, .src = {.f = file}, .fn = fn, .line = errLine, .data = data};
	tiniParse(&ctx);
	return ctx.err;
}

TINI_DEF const char *tiniParsePath(const char *path, TiniHook fn, int *errLine, void *data) {
	if (errLine) *errLine = -1;
	FILE *file = fopen(path, "r");
	if (file == NULL) return strerror(errno);
	const char *err = tiniParseFile(file, fn, errLine, data);
	fclose(file);
	return err;
}

#endif
