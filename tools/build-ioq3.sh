#!/bin/bash
# build-ioq3.sh: builds SDL2 (with the Wayland video driver) and the ioquake3
# fork on Haiku. Run it on the Haiku machine; the work goes to $IOQ3PORT.
# Needs: libxkbcommon_devel, wayland_devel, wayland_protocols, cmake, git.
set -e
PORT=${IOQ3PORT:-$HOME/ioq3port}
PATCHES=${PATCHES:-$(cd "$(dirname "$0")/.." && pwd)/patches/ioq3}
SDL_VERSION=2.32.8
IOQ3_URL=${IOQ3_URL:-https://github.com/jwalds/ioq3}
mkdir -p "$PORT"
cd "$PORT"

if [ ! -d SDL2-$SDL_VERSION ]; then
	curl -L -o SDL2-$SDL_VERSION.tar.gz \
		https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz
	tar xzf SDL2-$SDL_VERSION.tar.gz
	(cd SDL2-$SDL_VERSION && patch -p1 < "$PATCHES"/SDL2-$SDL_VERSION-haiku-wayland.patch)
fi
mkdir -p sdl-build
(cd sdl-build && cmake ../SDL2-$SDL_VERSION -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$PORT/prefix" -DSDL_WAYLAND=ON -DSDL_SHARED=ON \
	-DSDL_STATIC=OFF -DSDL_TEST=OFF && make -j4 && make install)

[ -d ioq3 ] || git clone "$IOQ3_URL" ioq3
# The Haiku changes are not upstream yet: apply them unless the checkout has them.
grep -q __HAIKU__ ioq3/code/qcommon/q_platform.h \
	|| git -C ioq3 am "$PATCHES"/0001-Add-a-Haiku-port.patch
mkdir -p build
(cd build && cmake ../ioq3 -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="$PORT/prefix" -DUSE_INTERNAL_SDL=OFF \
	-DUSE_OPENAL=OFF -DUSE_HTTP=OFF && make -j4)
