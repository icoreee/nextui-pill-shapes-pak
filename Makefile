PAK_NAME := Shaper
PAK_ID := Shaper
PAK_TYPE := Tools
PLATFORM := tg5040

CC ?= gcc
STRIP ?=
PKG_CONFIG ?= pkg-config
APOSTROPHE_DIR ?= third_party/apostrophe
APOSTROPHE_HEADER := $(APOSTROPHE_DIR)/include/apostrophe.h
APOSTROPHE_FONT := $(APOSTROPHE_DIR)/res/font.ttf
TOOLCHAIN_IMAGE ?= ghcr.io/loveretro/tg5040-toolchain:latest
TOOLCHAIN_CC ?= /opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu-gcc
TOOLCHAIN_STRIP ?= /opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu-strip
SYSROOT ?= /opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr

SDL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf SDL2_image 2>/dev/null || if [ -d "$(SYSROOT)/include/SDL2" ]; then printf '%s' '-I$(SYSROOT)/include'; fi)
SDL_LIBS ?= $(shell $(PKG_CONFIG) --libs sdl2 SDL2_ttf SDL2_image 2>/dev/null || if [ -d "$(SYSROOT)/lib" ]; then printf '%s' '-L$(SYSROOT)/lib -lSDL2 -lSDL2_ttf -lSDL2_image'; else printf '%s' '-lSDL2 -lSDL2_ttf -lSDL2_image'; fi)

CFLAGS ?= -O2
CFLAGS += -std=gnu11 -Wall -Wextra -I$(APOSTROPHE_DIR)/include -DPILL_SHAPES_FONT_PATH=\"$(APOSTROPHE_FONT)\" $(SDL_CFLAGS)
LDFLAGS += $(SDL_LIBS) -lm -lpthread

DIST_DIR := dist
BUILD_DIR := build-ui
SRC_DIR := src
PAK_DIR := $(PAK_NAME).pak
RELEASE_ZIP := $(DIST_DIR)/$(PAK_ID).pak.zip

.PHONY: clean check-apostrophe native build device verify-device release docker-device docker-release

clean:
	rm -rf "$(BUILD_DIR)" "$(DIST_DIR)"

check-apostrophe:
	@test -f "$(APOSTROPHE_HEADER)" || { \
		echo "Missing Apostrophe. Set APOSTROPHE_DIR=/path/to/apostrophe or vendor it at third_party/apostrophe"; \
		exit 1; \
	}

native: check-apostrophe
	mkdir -p "$(BUILD_DIR)/native"
	$(CC) $(CFLAGS) "$(SRC_DIR)/pill_shapes.c" -o "$(BUILD_DIR)/native/pill-shapes" $(LDFLAGS)

build: check-apostrophe
	rm -rf "$(PAK_DIR)"
	mkdir -p "$(PAK_DIR)"
	cp "$(SRC_DIR)/launch.sh" "pak.json" "$(PAK_DIR)/"
	cp -R caps "$(PAK_DIR)/"
	$(CC) $(CFLAGS) "$(SRC_DIR)/pill_shapes.c" -o "$(PAK_DIR)/pill-shapes.elf" $(LDFLAGS)
	chmod +x "$(PAK_DIR)/launch.sh" "$(PAK_DIR)/pill-shapes.elf"

device: check-apostrophe
	rm -rf "$(PAK_DIR)"
	mkdir -p "$(PAK_DIR)"
	cp "$(SRC_DIR)/launch.sh" "pak.json" "$(PAK_DIR)/"
	cp -R caps "$(PAK_DIR)/"
	platform_cflags="$(CFLAGS)"; \
	if [ "$(PLATFORM)" = "tg5040" ]; then platform_cflags="$$platform_cflags -DPLATFORM_TG5040"; \
	else echo "Unsupported PLATFORM=$(PLATFORM). First release supports tg5040 only."; exit 1; fi; \
	$(CC) $$platform_cflags "$(SRC_DIR)/pill_shapes.c" -o "$(PAK_DIR)/pill-shapes.elf" $(LDFLAGS)
	@if [ -n "$(STRIP)" ]; then "$(STRIP)" "$(PAK_DIR)/pill-shapes.elf"; fi
	chmod +x "$(PAK_DIR)/launch.sh" "$(PAK_DIR)/pill-shapes.elf"

verify-device:
	@file "$(PAK_DIR)/pill-shapes.elf"
	@file "$(PAK_DIR)/pill-shapes.elf" | grep -Eq 'ELF.*(ARM|aarch64)' || { \
		echo "release requires a Linux ARM ELF. Build with: make device PLATFORM=$(PLATFORM) CC=\"\$${CROSS_COMPILE}gcc\""; \
		exit 1; \
	}

release: device verify-device
	mkdir -p "$(DIST_DIR)"
	rm -f "$(RELEASE_ZIP)"
	(cd "$(PAK_DIR)" && zip -qr "../$(RELEASE_ZIP)" .)
	ls -lah "$(DIST_DIR)"

docker-device:
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		-w /workspace \
		"$(TOOLCHAIN_IMAGE)" \
		make device verify-device CC="$(TOOLCHAIN_CC)" STRIP="$(TOOLCHAIN_STRIP)"

docker-release: docker-device
	mkdir -p "$(DIST_DIR)"
	rm -f "$(RELEASE_ZIP)"
	(cd "$(PAK_DIR)" && zip -qr "../$(RELEASE_ZIP)" .)
	ls -lah "$(DIST_DIR)"
