#include "common.h"

#define STBI_MALLOC(SZ)       alloc_(SZ)
#define STBI_REALLOC(PTR, SZ) resize_(PTR, SZ)
#define STBI_FREE(PTR)        free(PTR)

#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINI_ALLOC(SZ)        alloc_(SZ)
#define TINI_REALLOC(PTR, SZ) resize_(PTR, SZ)
#define TINI_FREE(PTR)        free(PTR)

#define TINI_INLINECOMMENTS
#define TINI_IMPLEMENTATION
#include <tini.h>
