#!/bin/bash
# Build the RadeonGfx GPU stack on Haiku (Phase 3), based on X512's build
# notes (https://gist.github.com/X547/292d5893da00098a924a8a0aa02d6a28).
#
# usage: build-gpu-stack.sh [workdir]      default workdir: ~/gpu
#
# Clones missing repositories, then builds and installs into <workdir>/install:
# accelerant2, libdrm (mesa-drm), libdrm2 and RadeonGfx (server and client
# accelerant). Mesa is not built yet. VideoStreams is not built: it needs a
# syscall that isn't in upstream Haiku, and RadeonGfx is built without its
# display code (meson option 'display').
#
# Fixes for the 2023 helper libraries against current Haiku are in
# patches/gpu-stack/<name>.patch and applied after cloning.
set -e

WORK="${1:-$HOME/gpu}"
PATCHES="$(cd "$(dirname "$0")/../patches/gpu-stack" && pwd)"
INSTALL="$WORK/install"
RADEONGFX_REPO="${RADEONGFX_REPO:-https://github.com/jwalds/RadeonGfx.git}"
RADEONGFX_BRANCH="${RADEONGFX_BRANCH:-polaris}"

export PKG_CONFIG_PATH="$INSTALL/lib/pkgconfig:$INSTALL/develop/lib/pkgconfig:$PKG_CONFIG_PATH"
export PATH="$INSTALL/bin:$PATH"
# libdrm2 includes <libdrm/drm.h> from the installed libdrm headers
export CFLAGS="-I$INSTALL/develop/headers $CFLAGS"
export CXXFLAGS="-I$INSTALL/develop/headers $CXXFLAGS"

mkdir -p "$WORK"
cd "$WORK"

clone() {
	# clone <dir> <url> [git clone options]
	local dir=$1 url=$2; shift 2
	[ -d "$dir/.git" ] || git clone -q "$@" "$url" "$dir"
}

apply_patch() {
	# apply patches/gpu-stack/<dir>.patch unless it is already applied
	local dir=$1 patch="$PATCHES/$1.patch"
	[ -f "$patch" ] || return 0
	if git -C "$dir" apply --reverse --check "$patch" 2>/dev/null; then
		return 0
	fi
	git -C "$dir" apply "$patch"
	echo "patched $dir"
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
clone RadeonGfx "$RADEONGFX_REPO" -b "$RADEONGFX_BRANCH"
clone libdrm https://github.com/X547/mesa-drm.git --depth 1
clone libdrm2 https://github.com/X547/libdrm2.git
clone accelerant2 https://github.com/X547/accelerant2.git
# libdrm2 and RadeonGfx use the interface from before "adjust interface
# declarations" (796cc4c)
git -C accelerant2 checkout -q 61baaa6

for dir in Locks ThreadLink SADomains libdrm libdrm2; do
	apply_patch $dir
done

mkdir -p SADomains/subprojects RadeonGfx/subprojects libdrm2/subprojects
ln -sfn ../../Locks SADomains/subprojects/Locks
ln -sfn ../../Locks RadeonGfx/subprojects/Locks
ln -sfn ../../SADomains RadeonGfx/subprojects/SADomains
ln -sfn ../../ThreadLink RadeonGfx/subprojects/ThreadLink
ln -sfn ../../Locks libdrm2/subprojects/Locks
ln -sfn ../../ThreadLink libdrm2/subprojects/ThreadLink

build_package accelerant2
build_package libdrm -Dintel=false
build_package libdrm2
build_package RadeonGfx
# RadeonGfx's own kernel module (kernel/radeon_gfx) is not built: the server
# attaches to the radeon_hd driver instead.

echo "Built into $INSTALL"
