#!/bin/sh
# Runs ioquake3 on the stack: run-ioq3.sh <renderer> [extra arguments]
. $HOME/gpu/test/common.sh
P=${IOQ3PORT:-$HOME/ioq3port}
export LIBRARY_PATH="$P/prefix/lib:$LIBRARY_PATH"
export SDL_VIDEODRIVER=wayland
ensure_server
R=$1; shift
cd $P/build/Release
./ioquake3 +set fs_basepath $P/data +set cl_renderer $R +set s_initsound 0 \
	+set r_swapInterval 0 +set com_maxfps 0 +set r_fullscreen 0 +set r_mode 3 "$@"
