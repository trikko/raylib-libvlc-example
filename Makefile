TARGET  = raylib-libvlc-example
CFLAGS ?= -O2 -Wall

PKGS    = raylib glib-2.0
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))

# libvlc doesn't always ship a .pc file
ifeq ($(shell pkg-config --exists libvlc && echo yes),yes)
CFLAGS += $(shell pkg-config --cflags libvlc)
LDLIBS += $(shell pkg-config --libs libvlc)
else ifeq ($(shell uname -s),Darwin)
# On macOS libvlc comes with VLC.app (brew install --cask vlc)
VLC_DIR ?= /Applications/VLC.app/Contents/MacOS
CFLAGS += -I$(VLC_DIR)/include
LDLIBS += -L$(VLC_DIR)/lib -Wl,-rpath,$(VLC_DIR)/lib -lvlc
else
LDLIBS += -lvlc
endif
LDLIBS += -lm

ifeq ($(OS),Windows_NT)
TARGET := $(TARGET).exe
endif

$(TARGET): main.c
	$(CC) $(CFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f $(TARGET)

.PHONY: clean
