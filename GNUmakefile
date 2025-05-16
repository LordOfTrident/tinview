OBJDIR   := obj
SRCDIR   := src
BAKEDDIR := baked

APPINSTALL  := /usr/bin/tinview
MANINSTALL  := /usr/share/man/man1/tinview.1
XDGINSTALL  := /usr/share/applications/tinview.desktop
ICONINSTALL := /usr/share/pixmaps/tinview.png

OUT   := tinview
SRC   := $(wildcard $(SRCDIR)/*.c)
DEP   := $(wildcard $(SRCDIR)/*.h)
OBJ   := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRC))
BAKED := $(wildcard $(BAKEDDIR)/*.png)
INC   := $(patsubst $(BAKEDDIR)/%.png,$(SRCDIR)/baked_%.inc,$(BAKED))

CFLAGS = -pedantic -Wpedantic -Wshadow -Wvla -Wuninitialized -Wundef -Wno-deprecated-declarations \
         -Wall -Wextra -std=c99 -I./lib -D_POSIX_C_SOURCE -D_DEFAULT_SOURCE
LDFLAGS = -lm -lSDL2

.PHONY: release debug clean install uninstall all

release: CFLAGS += -DNDEBUG -g0 -O2 -flto -Wl,--gc-sections
release: $(OUT)

debug: CFLAGS  += -Werror -g -Og
debug: LDFLAGS += -fsanitize=address
debug: $(OUT)

$(OUT): $(INC) $(OBJDIR) $(OBJ) $(SRC)
	$(CC) -o $(OUT) $(CFLAGS) $(OBJ) $(LDFLAGS)

$(SRCDIR)/baked_%.inc: $(BAKEDDIR)/%.png
	xxd -i $< $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(DEP) $(INC)
	$(CC) -c $< $(CFLAGS) -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean: $(OBJDIR)
	rm -f -r $(OBJDIR)/*
	rm -f $(OUT)
	rm -f $(INC)

install: release
	cp $(OUT) $(APPINSTALL)
	cp tinview.1 $(MANINSTALL)
	cp tinview.desktop $(XDGINSTALL)
	cp baked/icon.png $(ICONINSTALL)

uninstall:
	rm $(APPINSTALL) $(MANINSTALL) $(XDGINSTALL) $(ICONINSTALL)

all:
	@echo debug, release, clean, install, uninstall
