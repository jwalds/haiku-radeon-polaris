#!/bin/sh
# overhead.sh [scene...]: per-frame overhead profile. Restarts the server
# with RADEONGFX_STATS, runs glmark2 scenes off-screen (5 s each, default
# build texture shading) and prints the FPS, the accelerant's per-call
# times of the per-frame calls, then the server's per-ioctl and per-stage
# submission times (printed when it stops). Full call list: the gm-*.log
# files in /tmp/overhead.
cd "$(dirname "$0")"
OUT=/tmp/overhead . ./common.sh
cp "$GPU/RadeonGfx/build.x86_64/radeon_gfx.accelerant/radeon_gfx.accelerant" \
	"$HOME/config/non-packaged/add-ons/accelerants/"
[ $# -eq 0 ] && set -- build texture shading

stop_server
export RADEONGFX_STATS=1
start_server || { echo "server didn't start"; tail "$SERVER_LOG"; exit 1; }
for scene in "$@"; do
	timeout 60 "$GPU/install/bin/glmark2-es2-wayland" --off-screen \
		-b "$scene:duration=5" > "$OUT/gm-$scene.log" 2>&1
	grep "FPS" "$OUT/gm-$scene.log"
	grep -E "CsSubmit|SyncobjTimelineWait|SyncobjTransfer|CtxRaw" "$OUT/gm-$scene.log"
done
stop_server
grep -A30 "^RADEONGFX_STATS" "$SERVER_LOG"
