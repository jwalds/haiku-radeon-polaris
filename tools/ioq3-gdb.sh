#!/bin/sh
# Runs ioquake3 under gdb and prints a backtrace of a crash: gdb-ioq3.sh <renderer> [arguments]
. $HOME/gpu/test/common.sh
P=${IOQ3PORT:-$HOME/ioq3port}
export LIBRARY_PATH="$P/prefix/lib:$LIBRARY_PATH"
export SDL_VIDEODRIVER=wayland
ensure_server
R=$1; shift
cd $P/build/Release
gdb -batch -ex "handle SIGTERM nostop noprint pass" -ex run -ex bt -ex "thread apply all bt 12" \
	--args ./ioquake3 +set fs_basepath $P/data +set cl_renderer $R +set s_initsound 0 \
	+set r_swapInterval 0 +set com_maxfps 0 +set r_fullscreen 0 +set r_mode 3 "$@" < /dev/null
