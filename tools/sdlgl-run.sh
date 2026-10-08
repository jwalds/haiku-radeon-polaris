#!/bin/sh
# Runs the sdlgl test on the stack: run-sdlgl.sh [frames]
. $HOME/gpu/test/common.sh
export LIBRARY_PATH="$HOME/ioq3port/prefix/lib:$LIBRARY_PATH"
export SDL_VIDEODRIVER=wayland
ensure_server
$HOME/ioq3port/sdlgl "$@"
