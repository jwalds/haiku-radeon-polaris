#!/bin/bash
# Build glmark2 (Wayland, OpenGL ES 2 and desktop OpenGL through EGL) for
# testing Zink on the RadeonGfx stack. Run after build-mesa.sh.
#
# usage: build-glmark2.sh [workdir]      default workdir: ~/gpu
# run:   vkrun.sh <workdir>/install/bin/glmark2-es2-wayland [--off-screen]
set -e

WORK="${1:-$HOME/gpu}"
PATCHES="$(cd "$(dirname "$0")/../patches/gpu-stack" && pwd)"
INSTALL="$WORK/install"

export PKG_CONFIG_PATH="$INSTALL/lib/pkgconfig:$INSTALL/develop/lib/pkgconfig:$PKG_CONFIG_PATH"

cd "$WORK"
[ -d glmark2/.git ] || git clone -q --depth 1 \
	https://github.com/glmark2/glmark2.git glmark2
cd glmark2
if git apply --check "$PATCHES/glmark2.patch" 2>/dev/null; then
	git apply "$PATCHES/glmark2.patch"
fi
[ -d build.x86_64 ] || meson setup build.x86_64 --prefix "$INSTALL" \
	-Dflavors=wayland-glesv2,wayland-gl
ninja -C build.x86_64
ninja -C build.x86_64 install
