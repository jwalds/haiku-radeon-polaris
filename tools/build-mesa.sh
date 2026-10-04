#!/bin/bash
# Build Mesa's RADV Vulkan driver and Zink (OpenGL and OpenGL ES on Vulkan)
# for the RadeonGfx stack on Haiku (Phase 3 steps 6 and 7). Run after
# build-gpu-stack.sh.
#
# usage: build-mesa.sh [workdir]      default workdir: ~/gpu
#
# Mesa 23.3.6 is the newest release that accepts libdrm_amdgpu 2.4.110, the
# version of the stack's libdrm. RADV is built with ACO (no LLVM); OpenGL comes
# from Zink through EGL on Wayland (libEGL, libGLESv2, lib/dri/zink_dri.so).
# Haiku fixes are in patches/gpu-stack/mesa.patch, applied after cloning.
# The Vulkan ICD manifest is installed into <workdir>/install too; point the
# loader at it with
#   VK_DRIVER_FILES=<workdir>/install/data/vulkan/icd.d/radeon_icd.x86_64.json
# and EGL at Zink with
#   MESA_LOADER_DRIVER_OVERRIDE=zink LIBGL_DRIVERS_PATH=<workdir>/install/lib/dri
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
	-Dgallium-drivers=zink \
	-Dvulkan-drivers=amd \
	-Dllvm=disabled \
	-Dopengl=true \
	-Dgles1=disabled -Dgles2=enabled \
	-Degl=enabled -Dglx=disabled -Dgbm=disabled \
	-Dshared-glapi=enabled \
	-Dxmlconfig=disabled \
	-Dzstd=disabled \
	-Dvalgrind=disabled \
	-Dlibunwind=disabled \
	-Dbuild-tests=false
ninja -C build.x86_64
ninja -C build.x86_64 install
sync
