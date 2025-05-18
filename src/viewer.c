#include "viewer.h"

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Cursor   *cursorNormal, *cursorMove;
static bool          keys[SDL_NUM_SCANCODES];
static int           winw, winh;
static double        dt, elapsed; // Delta time, elapsed time
static bool          quit, fullscr;

// zoomt is for smooth zooming (t = transition), same with camxt and camyt
static double zoom = 1, zoomt = 1, camx, camy, camxt, camyt, mtimer;
static int    mx, my;
static bool   mgrabbed;

// Image-related
static Images       imgs;
static Image       *img;
static int          imgIdx;
static SDL_Texture *imgTex;
static bool         waiting; // Is the viewer waiting for the image to finish loading?
static void       (*runAfterHidden)(void);
static double       showTimer, hideTimer, gifTimer, filterIconTimer;
static int          gifFrame, filter;

typedef struct {
	SDL_Texture *tex;
	int          w, h;
} Baked;

static Baked loadingIcon, errorIcon, filteringIcon, shadowSheet;

#include "baked_icon.inc"
#include "baked_loading.inc"
#include "baked_error.inc"
#include "baked_filtering.inc"
#include "baked_shadow.inc"

static struct {
	Baked   *baked;
	uint8_t *raw;
	int      sz;
} bakedList[] = {
	{&loadingIcon,   baked_loading_png,   sizeof(baked_loading_png)},
	{&errorIcon,     baked_error_png,     sizeof(baked_error_png)},
	{&filteringIcon, baked_filtering_png, sizeof(baked_filtering_png)},
	{&shadowSheet,   baked_shadow_png,    sizeof(baked_shadow_png)},
};

#define togglefmt(X) ((X)? "enable" : "disable")

static void fullscreen(bool on) {
	if (SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP*on) < 0)
		error("Failed to %s fullscreen: %s", togglefmt(on), SDL_GetError());
}

static SDL_Texture *createTexture(int w, int h, bool filtering) {
	if (!SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, filtering? "linear" : "nearest"))
		error("Failed to %s filtering: %s", togglefmt(filtering), SDL_GetError());

	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
	                                     SDL_TEXTUREACCESS_STREAMING, w, h);
	if (tex == NULL) die("Failed to create texture: %s", SDL_GetError());
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	return tex;
}

static void loadBaked(Baked *baked, uint8_t *raw, int sz) {
	uint8_t *pxs = stbi_load_from_memory(raw, sz, &baked->w, &baked->h, NULL, 4);
	if (pxs == NULL) die("Failed to load baked asset: %s", stbi_failure_reason());

	baked->tex = createTexture(baked->w, baked->h, true);
	SDL_UpdateTexture(baked->tex, NULL, pxs, baked->w*4);
	free(pxs);

	Rgba color = conf.colors.icons;
	SDL_SetTextureColorMod(baked->tex, color.r, color.g, color.b);
	SDL_SetTextureAlphaMod(baked->tex, color.a);
}

static void setCursor(SDL_Cursor *cursor) {
	if (SDL_GetCursor() != cursor) SDL_SetCursor(cursor);
}

static void loadWindowIcon(void) {
	int      w, h;
	uint8_t *pxs = stbi_load_from_memory(baked_icon_png, sizeof(baked_icon_png), &w, &h, NULL, 4);
	if (pxs == NULL) die("Failed to load baked window icon asset: %s", stbi_failure_reason());

	SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(pxs, w, h, 32, w*4,
	                                             0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
	if (surf == NULL) die("Failed to create surface for window icon: %s", SDL_GetError());
	free(pxs);
	SDL_SetWindowIcon(win, surf);
	SDL_FreeSurface(surf);
}

static void setup(const char *browsePath) {
	winw   = conf.win.startw;
	winh   = conf.win.starth;
	filter = conf.img.filter;

	/* TODO: SDL2 startup is slow for some reason. Switch to some other graphics library? Maybe
	         OpenGL + glfw? */
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) die("Failed to initialize SDL2: %s", SDL_GetError());
	if ((win = SDL_CreateWindow(TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                            winw, winh, SDL_WINDOW_RESIZABLE)) == NULL)
		die("Failed to create window: %s", SDL_GetError());
	SDL_SetWindowMinimumSize(win, conf.win.minw, conf.win.minh);
	fullscreen(conf.win.fullscr);
	if ((ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE |
	                                       SDL_RENDERER_PRESENTVSYNC)) == NULL)
		die("Failed to create renderer: %s", SDL_GetError());
	if (SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND) < 0)
		error("Failed to set blend mode: %s", SDL_GetError());

	cursorNormal = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	cursorMove   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
	if (cursorNormal == NULL || cursorMove == NULL)
		die("Failed to create system cursor: %s", SDL_GetError());

	setCursor(cursorNormal);
	loadWindowIcon();

	for (size_t i = 0; i < lenOf(bakedList); ++i)
		loadBaked(bakedList[i].baked, bakedList[i].raw, bakedList[i].sz);

	Error err = initImages(&imgs, browsePath);
	// Error in initImages still leaves it in a usable state
	if (err != NULL) error("Error while initializing images list: %s", err);
}

static void cleanup(void) {
	freeImages(&imgs);
	SDL_FreeCursor(cursorNormal);
	SDL_FreeCursor(cursorMove);
	if (imgTex != NULL) SDL_DestroyTexture(imgTex);
	for (size_t i = 0; i < lenOf(bakedList); ++i) SDL_DestroyTexture(bakedList[i].baked->tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
}

static void darken(double t) {
	Rgba color = conf.colors.darken;
	SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, t*color.a);
	SDL_RenderFillRect(ren, NULL);
}

static void renderError(void) {
	darken(1);
	SDL_RenderCopy(ren, errorIcon.tex, NULL, &(SDL_Rect){
		.x = winw/2 - errorIcon.w/2,
		.y = winh/2 - errorIcon.h/2,
		.w = errorIcon.w,
		.h = errorIcon.h,
	});
}

static void renderLoading(void) {
	darken(1);
	SDL_RenderCopyEx(ren, loadingIcon.tex, NULL, &(SDL_Rect){
		.x = winw/2 - loadingIcon.w/2,
		.y = winh/2 - loadingIcon.h/2,
		.w = loadingIcon.w,
		.h = loadingIcon.h,
	}, sin(elapsed/400)*360, NULL, SDL_FLIP_NONE);
}

static void renderShadow(SDL_Rect r) {
	int sz = shadowSheet.w/2;
	// Top left
	SDL_RenderCopy(ren, shadowSheet.tex, &(SDL_Rect){.x = 0, .y = 0, .w = sz, .h = sz},
	               &(SDL_Rect){.x = r.x - sz, .y = r.y - sz, .w = sz, .h = sz});
	// Top
	SDL_RenderCopy(ren, shadowSheet.tex, &(SDL_Rect){ .x = sz, .y = 0, .w = 1, .h = sz},
	               &(SDL_Rect){ .x = r.x, .y = r.y - sz, .w = r.w, .h = sz});
	// Top right
	SDL_RenderCopy(ren, shadowSheet.tex, &(SDL_Rect){.x = sz + 1, .y = 0, .w = sz, .h = sz},
	               &(SDL_Rect){.x = r.x + r.w, .y = r.y - sz, .w = sz, .h = sz});
	// Right
	SDL_RenderCopy(ren, shadowSheet.tex, &(SDL_Rect){.x = sz, .y = sz, .w = sz, .h = 1},
	               &(SDL_Rect){.x = r.x + r.w, .y = r.y, .w = sz, .h = r.h});
	// Bottom right
	SDL_RenderCopy(ren, shadowSheet.tex, &(SDL_Rect){.x = sz, .y = sz + 1, .w = sz, .h = sz},
	               &(SDL_Rect){.x = r.x + r.w, .y = r.y + r.h, .w = sz, .h = sz});
	// Bottom
	SDL_RenderCopy(ren, shadowSheet.tex, &(SDL_Rect){.x = sz, .y = sz, .w = 1, .h = sz},
	               &(SDL_Rect){.x = r.x, .y = r.y + r.h, .w = r.w, .h = sz});
	// Bottom left
	SDL_RenderCopy(ren, shadowSheet.tex, &(SDL_Rect){.x = 0, .y = sz + 1, .w = sz, .h = sz},
	               &(SDL_Rect){.x = r.x - sz, .y = r.y + r.h, .w = sz, .h = sz});
	// Left
	SDL_RenderCopy(ren, shadowSheet.tex, &(SDL_Rect){.x = 0, .y = sz, .w = sz, .h = 1},
	               &(SDL_Rect){.x = r.x - sz, .y = r.y, .w = sz, .h = r.h});
}

static void renderOutline(SDL_Rect r) {
	r.x -= 1;
	r.y -= 1;
	r.w += 1;
	r.h += 1;
	Rgba color = conf.colors.outline;
	SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
	SDL_RenderDrawRect(ren, &r);
}

static double lerp(double a, double b, double t) {
	return a + t*(b - a);
}

static double easeOutCubic(double t) {
	double t2 = t - 1;
	return 1 + t2*t2*t2;
}

static double easeInCubic(double t) {
	return t*t*t;
}

static void renderImage(void) {
	double tshow = 1, thide = 1;
	if (conf.img.animTime > 0) {
		tshow = easeOutCubic(1 - showTimer/conf.img.animTime);
		thide = hideTimer > 0? easeInCubic(hideTimer/conf.img.animTime) : 1;
	}
	double scale = zoomt*tshow*thide;
	SDL_Rect r = {
		.x = winw/2 - camxt - img->w/2*scale,
		.y = winh/2 - camyt - img->h/2*scale,
		.w = img->w*scale,
		.h = img->h*scale,
	};
	SDL_RenderCopy(ren, imgTex, NULL, &r);
	switch (conf.img.border) {
	case BORDERSHADOW:  renderShadow(r);  break;
	case BORDEROUTLINE: renderOutline(r); break;
	case BORDERNONE: break;
	}
	darken(lerp(1 - tshow, 1, 1 - thide));
}

static void renderFilterIcon(void) {
	SDL_Rect src = {
		.x = filter*filteringIcon.w/3,
		.y = 0,
		.w = filteringIcon.w/3,
		.h = filteringIcon.h,
	}, dest = {
		.x = 10,
		.y = 10,
		.w = filteringIcon.w/3,
		.h = filteringIcon.h,
	};
	double t = filterIconTimer/FILTERICONTIME*1.3;
	if (t > 1) t = 1;
	SDL_SetTextureAlphaMod(filteringIcon.tex, t*conf.colors.icons.a);
	SDL_RenderCopy(ren, filteringIcon.tex, &src, &dest);
}

static void renderCheckerboard(void) {
	for (int y = 0; y <= winh/TILESZ; ++y) {
		for (int x = 0; x <= winw/TILESZ; ++x) {
			Rgba color = conf.colors.checkerboard[(x + y)%2 == 0];
			SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
			SDL_RenderFillRect(ren, &(SDL_Rect){
				.x = x*TILESZ,
				.y = y*TILESZ,
				.w = TILESZ,
				.h = TILESZ,
			});
		}
	}
}

static void render(void) {
	Rgba color = conf.colors.checkerboard[0];
	SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(ren);

	renderCheckerboard();

	if      (!imgs.sz) renderError();
	else if (waiting)  renderLoading();
	/* If there's no error, render the image, even if it's possibly not loaded. This is so that
	   when an image gets unloaded after being modified, we can still play the hide animation and
	   render the image before reloading it */
	else if (img->err == NULL) renderImage();
	else renderError();

	if (filterIconTimer > 0) renderFilterIcon();

	SDL_RenderPresent(ren);
}

static void updateWindowTitle(void) {
	static char title[PATH_MAX + 32];
	if (waiting) strcpy(title, TITLE" - (...) ");
	else sprintf(title, TITLE" - (%ix%i) ", img->w, img->h);

	const char *path = img->path;
	if (*path == 0) {
		SDL_SetWindowTitle(win, title);
		return;
	}

	// Shorten path by current working directory path
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) != NULL) {
		size_t len = strlen(cwd);
		if (strncmp(cwd, path, len) == 0) path += len + 1;
	}

	// Shorten path by home path
	size_t len = strlen(home());
	if (strncmp(home(), path, len) == 0) {
		strcat(title, "~");
		path += len;
	}

	strcat(title, path);
	SDL_SetWindowTitle(win, title);
}

static void recreateImageTexture(void) {
	if (imgTex != NULL) SDL_DestroyTexture(imgTex);
	imgTex = createTexture(img->w, img->h, filter == FILTERAUTO? zoom < 1 : filter == FILTERLINEAR);
	SDL_UpdateTexture(imgTex, NULL, img->pxs + img->w*img->h*gifFrame*img->isGif, img->w*4);
}

static bool isImageAvailable(void) {
	if (!imgs.sz || waiting) return false;
	return img->loaded;
}

static void setZoom(double z, double ox, double oy) {
	double prevZoom = zoom;
	zoom = z;
	if      (zoom > conf.cam.zoomMax) zoom = conf.cam.zoomMax;
	else if (zoom < conf.cam.zoomMin) zoom = conf.cam.zoomMin;
	camx = (camx + ox)/prevZoom*zoom - ox;
	camy = (camy + oy)/prevZoom*zoom - oy;

	// Can't recreate image texture if the image isn't available
	if (!isImageAvailable()) return;
	/* TODO: recreateImageTexture() is slower with big images (for example 4285x5712), so there
	         might be a weird jump between zoom < 1 and zoom > 1. Fix this somehow? Maybe with
	         OpenGL I won't have to recreate the texture to change filtering? */
	if (filter == FILTERAUTO && (prevZoom > 1) != (zoom > 1)) recreateImageTexture();
}

static void resetCamera() {
	if (!isImageAvailable()) return;
	if (conf.img.fitOnResize != FITNONE) {
		double z = (double)img->w/img->h < (double)winw/winh?
		           (double)winh/img->h : (double)winw/img->w;
		setZoom(z > 1 && conf.img.fitOnResize == FITINT? floor(z) : z, 0, 0);
	}
	camx = camy = 0;
}

static void showImage(void) {
	showTimer = conf.img.animTime;
	gifTimer  = 0;
	gifFrame  = 0;
	updateWindowTitle();
	recreateImageTexture();
	resetCamera();
	zoomt = zoom;
	camxt = camx;
	camyt = camy;
}

static void hideImage(void (*fn)(void)) {
	if      (conf.img.animTime <= 0) fn(); // If the animation is disabled, the timer isn't needed
	else if (waiting)                fn(); // If we're still loading, the image is already hidden
	else if (img->err != NULL)       fn(); // If the image failed to load, it's already hidden
	else {
		hideTimer      = conf.img.animTime;
		runAfterHidden = fn;
	}
}

static void startLoadingImage(void) {
	waiting = true;
	if (*img->path) loadImage(img);
	else loadImageFromStdin(img);
}

static void prepareImage(void) {
	assert((size_t)imgIdx < imgs.sz);
	img = imgs.raw[imgIdx];

	if      (isImageLoading(img)) waiting = true;      // Image is already loading, wait for it
	else if (img->loaded)         showImage();         // Image is cached, just show it
	else if (img->err == NULL)    startLoadingImage(); // Start loading the image

	updateWindowTitle();
}

static void loadingEnded(void) {
	waiting = false;
	if (img->loaded) showImage();
	/* Image just finished loading, but it's not loaded. This means it was probably requested to
	   unload because the file got modified while it was being loaded, so let's just reload the
	   image */
	else if (img->err == NULL) startLoadingImage();
	else if (*img->path) error("Failed to load image \"%s\": %s", img->path, img->err);
	else                 error("Failed to load image from stdin: %s", img->err);
}

static void nextImage(int dir) {
	if (imgs.sz <= 1 || showTimer > 0 || hideTimer > 0) return;
	if (dir > 0) if (++imgIdx >= (int)imgs.sz) imgIdx = 0;
	if (dir < 0) if (imgIdx-- == 0)            imgIdx = imgs.sz - 1;
	hideImage(prepareImage);
}

static void event(SDL_Event *e) {
	switch (e->type) {
	case SDL_QUIT: quit = true; break;
	case SDL_WINDOWEVENT:
		switch (e->window.event) {
		case SDL_WINDOWEVENT_RESIZED:
			winw = e->window.data1;
			winh = e->window.data2;
			resetCamera();
			break;
		}
		break;
	case SDL_KEYDOWN:
		if (!keys[e->key.keysym.scancode]) {
			int key = e->key.keysym.sym;
			switch (key) {
			case SDLK_ESCAPE: quit = true; break;
			case SDLK_F11:    fullscreen(fullscr = !fullscr); break;
			case SDLK_LEFT:   nextImage(-1);   break;
			case SDLK_RIGHT:  nextImage(1);    break;
			case SDLK_SPACE:
				if (isImageAvailable()) {
					if (++filter >= FILTERCOUNT) filter = 0;
					filterIconTimer = FILTERICONTIME;
					recreateImageTexture();
				}
				break;
			default:
				if (key >= SDLK_1 && key <= SDLK_9) {
					double z = key - SDLK_1 + 1;
					if (keys[SDL_SCANCODE_LCTRL]) z = 1/z;
					setZoom(z, 0, 0);
				}
			}
		}
		keys[e->key.keysym.scancode] = true;
		break;
	case SDL_KEYUP:
		keys[e->key.keysym.scancode] = false;
		break;
	case SDL_MOUSEMOTION:
		if (mgrabbed) {
			camx -= e->motion.x - mx;
			camy -= e->motion.y - my;
		}
		mx = e->motion.x;
		my = e->motion.y;
		break;
	case SDL_MOUSEBUTTONUP:
		if (e->button.button != SDL_BUTTON_LEFT) break;
		if (mtimer <= conf.ctrls.doubleClickTime) {
			zoom = 1;
			resetCamera();
		}
		mtimer = 0;
		if (mgrabbed) mgrabbed = false;
		setCursor(cursorNormal);
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (e->button.button != SDL_BUTTON_LEFT) break;
		mgrabbed = true;
		setCursor(cursorMove);
		break;
	case SDL_MOUSEWHEEL:
		setZoom(zoom + (double)e->wheel.y*zoom*(e->wheel.y > 0? conf.cam.zoomIn : conf.cam.zoomOut),
		        mx - winw/2, my - winh/2);
		printf("%f\n", zoom);
		break;
	case SDL_DROPFILE:
		if (!*e->drop.file) break; // Broken on my system, so this prevents it from crashing
		imgIdx = getOrAddImage(&imgs, e->drop.file);
		prepareImage();
		free(e->drop.file);
		break;
	}
}

static void updateGif(void) {
	if ((gifTimer += dt) > img->delays[gifFrame]) {
		gifTimer = 0;
		if (++gifFrame >= img->len) gifFrame = 0;
		SDL_UpdateTexture(imgTex, NULL, img->pxs + img->w*img->h*gifFrame, img->w*4);
	}
}

static void updateCameraTransition(void) {
	double t = dt*conf.cam.damping;
	if (t > 1) t = 1;
	zoomt = lerp(zoomt, zoom, t);
	camxt = lerp(camxt, camx, t);
	camyt = lerp(camyt, camy, t);
}

static void updateViewTransition(void) {
	if (showTimer > 0) if ((showTimer -= dt) < 0) showTimer = 0;
	if (hideTimer > 0) if ((hideTimer -= dt) < 0) {
		hideTimer = 0;
		runAfterHidden();
	}
}

static void update(void) {
	mtimer += dt;
	if (filterIconTimer > 0) if ((filterIconTimer -= dt) < 0) filterIconTimer = 0;

	watchImages(&imgs);
	// If there are still no images, no updates need to be done
	if (!imgs.sz) return;
	/* If there are images but we haven't initialized the current image, let's prepare an image.
	   This situation will never happen without img being NULL because we never remove images that
	   are already loaded */
	if (img == NULL) prepareImage();

	// If we're still waiting but the image isn't loading, that means the loading has just ended
	if (waiting && !isImageLoading(img)) loadingEnded();
	/* If we're still waiting, or there's an error, we don't need to check for updating
	   image-related things */
	if (waiting)          return;
	if (img->err != NULL) return;

	if (!img->loaded) {
		/* If the image is not loaded, there's no error and it isn't currently being hidden, that
		   means it just got unloaded, probably because the file got modified. So let's reload it */
		if (hideTimer == 0) hideImage(startLoadingImage);
	} else if (img->isGif) updateGif(); // We can't update the gif unless the image is loaded
	updateCameraTransition();

	/* This function must run last because it (possibly) changes the state of the image when
	   the hide timer callback is called (loaded -> loading), so this could break updateGif,
	   because it relies on the image being already loaded */
	updateViewTransition();
}

static bool isStdinRedirectedOrPiped(void) {
	struct stat st;
	if (fstat(STDIN_FILENO, &st) != 0) return false;
	if (S_ISCHR(st.st_mode))           return false; // Terminal?
	if (S_ISFIFO(st.st_mode))          return true;  // Piping?
	return !isatty(STDIN_FILENO); // Redirecting?
}

void view(const char *browsePath, const char **paths, int count) {
	// TODO: Image rotation and flipping, but only for viewing, no modifying of the file
	// TODO: Copying images into clipboard with CTRL+C

	setup(browsePath);

	if (count > 0) {
		for (int i = 0; i < count; ++i) getOrAddImage(&imgs, paths[i]);
		bool found = searchImageByName(&imgs, *paths, &imgIdx);
		assert(found); // We just inserted it, so it must be there
		unused(found);
	} else if (isStdinRedirectedOrPiped()) imgIdx = getOrAddImage(&imgs, IMGSTDIN);
	if (imgs.sz) prepareImage();

	while (!quit) {
		// Update delta and elapsed time
		static uint64_t last, now = 0;
		if (now == 0) now = SDL_GetPerformanceCounter();
		last     = now;
		now      = SDL_GetPerformanceCounter();
		elapsed += dt = (double)(now - last)*1000/SDL_GetPerformanceFrequency();

		render();
		SDL_Event e;
		while (SDL_PollEvent(&e)) event(&e);
		update();
	}
	cleanup();
}
