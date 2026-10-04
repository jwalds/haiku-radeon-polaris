#!/bin/bash
# Build Mesa's RADV Vulkan driver for the RadeonGfx stack on Haiku (Phase 3
# step 6). Run after build-gpu-stack.sh.
#
# usage: build-mesa.sh [workdir]      default workdir: ~/gpu
#
# Mesa 23.3.6 is the newest release that accepts libdrm_amdgpu 2.4.110, the
# version of the stack's libdrm. Only RADV is built, with ACO (no LLVM) and no
# OpenGL. Haiku fixes are in patches/gpu-stack/mesa.patch, applied after
# cloning. The Vulkan ICD manifest is installed into <workdir>/install too;
# point the loader at it with
#   VK_DRIVER_FILES=<workdir>/install/data/vulkan/icd.d/radeon_icd.x86_64.json
set -e

WORK="${1:-$HOME/gpu}"
PATCHES="$(cd "$(dirname "$0")/../patches/gpu-stack" && pwd)"
INSTALL="$WORK/install"
MESA_TAG="${MESA_TAG:-mesa-23.3.6}"

export PKG_CONFIG_PATH="$INSTALL/lib/pkgconfig:$INSTALL/develop/lib/pkgconfig:$PKG_CONFIG_PATH"

cd "$WORK"
[ -d mesa/.git ] || git clone -q --depth 1 -b "$MESA_TAG" \
	https://gitlab.freedesktop.org/mesa/mesa.git mesa
cd mesa
if [ -f "$PATCHES/mesa.patch" ] \
	&& git apply --check "$PATCHES/mesa.patch" 2>/dev/null; then
	git apply "$PATCHES/mesa.patch"
fi

# Mako is packaged for Python 3.10 only
cat > haiku-native.ini <<NATIVE
[binaries]
python = '/boot/system/bin/python3.10'
python3 = '/boot/system/bin/python3.10'
NATIVE

[ -d build.x86_64 ] || meson setup build.x86_64 \
	--native-file haiku-native.ini \
	--prefix "$INSTALL" \
	--buildtype debugoptimized \
	-Dplatforms=wayland \
	-Dgallium-drivers= \
	-Dvulkan-drivers=amd \
	-Dllvm=disabled \
	-Dopengl=false \
	-Dgles1=disabled -Dgles2=disabled \
	-Degl=disabled -Dglx=disabled -Dgbm=disabled \
	-Dshared-glapi=disabled \
	-Dxmlconfig=disabled \
	-Dzstd=disabled \
	-Dvalgrind=disabled \
	-Dlibunwind=disabled \
	-Dbuild-tests=false
ninja -C build.x86_64
ninja -C build.x86_64 install
sync
