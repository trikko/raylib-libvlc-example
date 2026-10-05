TARGET  = raylib-libvlc-example
CFLAGS ?= -O2 -Wall

PKGS    = raylib glib-2.0
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))

# libvlc doesn't always ship a .pc file
CFLAGS += $(shell pkg-config --cflags libvlc 2>/dev/null)
LDLIBS += $(shell pkg-config --libs libvlc 2>/dev/null || echo -lvlc)
LDLIBS += -lm

ifeq ($(OS),Windows_NT)
TARGET := $(TARGET).exe
endif

$(TARGET): main.c
	$(CC) $(CFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f $(TARGET)

.PHONY: clean
