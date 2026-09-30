#!/bin/bash
# Build the RadeonGfx GPU stack on Haiku (Phase 3), based on X512's build
# notes (https://gist.github.com/X547/292d5893da00098a924a8a0aa02d6a28).
#
# usage: build-gpu-stack.sh [workdir]      default workdir: ~/gpu
#
# Clones missing repositories, then builds and installs into <workdir>/install:
# accelerant2, libdrm (mesa-drm), libdrm2, VideoStreams, RadeonGfx and its
# kernel module. Mesa is not built yet.
set -e

WORK="${1:-$HOME/gpu}"
INSTALL="$WORK/install"
RADEONGFX_REPO="${RADEONGFX_REPO:-https://github.com/jwalds/RadeonGfx.git}"
RADEONGFX_BRANCH="${RADEONGFX_BRANCH:-polaris}"

export PKG_CONFIG_PATH="$INSTALL/lib/pkgconfig:$INSTALL/develop/lib/pkgconfig:$PKG_CONFIG_PATH"
export PATH="$INSTALL/bin:$PATH"

mkdir -p "$WORK"
cd "$WORK"

clone() {
	# clone <dir> <url> [git clone options]
	local dir=$1 url=$2; shift 2
	[ -d "$dir/.git" ] || git clone -q "$@" "$url" "$dir"
}

build_package() {
	local name=$1; shift
	local buildDir="$name/build.$(getarch)"
	rm -rf "$buildDir"
	mkdir "$buildDir"
	echo "=== $name"
	(cd "$buildDir" && meson setup .. -Dprefix="$INSTALL" "$@" && ninja install)
}

clone Locks https://github.com/X547/Locks.git
clone SADomains https://github.com/X547/SADomains.git
clone ThreadLink https://github.com/X547/ThreadLink.git
clone VideoStreams https://github.com/X547/VideoStreams.git
clone RadeonGfx "$RADEONGFX_REPO" -b "$RADEONGFX_BRANCH"
clone libdrm https://github.com/X547/mesa-drm.git --depth 1
clone libdrm2 https://github.com/X547/libdrm2.git
clone accelerant2 https://github.com/X547/accelerant2.git

mkdir -p SADomains/subprojects RadeonGfx/subprojects libdrm2/subprojects
ln -sfn ../../Locks SADomains/subprojects/Locks
ln -sfn ../../Locks RadeonGfx/subprojects/Locks
ln -sfn ../../SADomains RadeonGfx/subprojects/SADomains
ln -sfn ../../ThreadLink RadeonGfx/subprojects/ThreadLink
ln -sfn ../../Locks libdrm2/subprojects/Locks
ln -sfn ../../ThreadLink libdrm2/subprojects/ThreadLink

build_package accelerant2
build_package libdrm -Dintel=disabled
build_package libdrm2
build_package VideoStreams
build_package RadeonGfx
echo "=== radeon_gfx kernel module"
(cd RadeonGfx/kernel/radeon_gfx && make)

echo "Built into $INSTALL"
