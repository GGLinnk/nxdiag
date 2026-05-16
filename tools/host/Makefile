# Host (PC) tooling for NX Diag.
#
#   make / make run   build + run the UI preview in an SDL2 window
#   make headless     run 240 frames with no window, dump build/preview.ppm
#   make gen          render PPM stills + a walk-through clip into dist/frames/
#   make assets       gen, then convert the stills to dist/*.png
#   make video        gen, then encode the walk-through to dist/showcase.mp4
#   make clean        remove build/ and dist/
#
# UI mock only: tools/host/{switch,arm_neon,deko3d,json-c} stub the libnx
# surface with dummy data. The asset generator drives the views through the
# nxdisplaylib Runner (step / select / savePpm); dist/ is never committed.

CXX      ?= g++
APP      := ../../source
LIB      := ../../libs/nxdisplaylib

# Single source of truth: read the version straight from the device Makefile.
APP_VERSION := $(shell sed -n 's/^APP_VERSION[[:space:]]*:=[[:space:]]*//p' ../../Makefile)

CXXFLAGS := -std=gnu++17 -O2 -Wall -pthread -DNXD_HOST \
            -DAPP_VERSION=\"$(APP_VERSION)\" \
            -I. -I$(LIB)/host -I$(LIB)/include -I$(APP) \
            $(shell pkg-config --cflags sdl2)
LDLIBS   := $(shell pkg-config --libs sdl2) -pthread

LIBSRC := $(LIB)/source/gfx.cpp $(LIB)/source/font.cpp \
          $(LIB)/source/runner.cpp $(LIB)/source/widgets.cpp \
          $(LIB)/host/host_backend.cpp
APPSRC    := $(wildcard $(APP)/*.cpp)
APPNOMAIN := $(filter-out $(APP)/main.cpp,$(APPSRC))
DEPS      := switch.h arm_neon.h deko3d.h json-c/json.h

HOSTBIN := build/nxdiag-host
GENBIN  := build/nxdiag-gen

$(HOSTBIN): $(LIBSRC) $(APPSRC) mock.cpp $(DEPS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(LIBSRC) $(APPSRC) mock.cpp $(LDLIBS)

$(GENBIN): $(LIBSRC) $(APPNOMAIN) mock.cpp gen.cpp $(DEPS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(LIBSRC) $(APPNOMAIN) mock.cpp gen.cpp $(LDLIBS)

run: $(HOSTBIN)
	./$(HOSTBIN)

headless: $(HOSTBIN)
	NXD_HOST_FRAMES=240 NXD_HOST_DUMP=build/preview.ppm ./$(HOSTBIN)

gen: $(GENBIN)
	@mkdir -p dist/frames
	NXD_HOST_HEADLESS=1 ./$(GENBIN) dist/frames

assets: gen
	@for f in dist/frames/still_*.ppm; do \
	    ffmpeg -y -loglevel error -i "$$f" "dist/$$(basename $${f%.ppm}).png"; \
	done
	@echo "stills -> dist/*.png"

video: gen
	@ffmpeg -y -loglevel error -framerate 30 -i dist/frames/clip_%05d.ppm \
	    -vf "scale=1280:-1:flags=lanczos" -c:v libx264 -pix_fmt yuv420p \
	    dist/showcase.mp4
	@echo "walk-through -> dist/showcase.mp4"

clean:
	rm -rf build dist

.PHONY: run headless gen assets video clean
