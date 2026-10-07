#!/bin/sh
# overhead.sh [scene...]: per-frame overhead profile. Restarts the server
# with RADEONGFX_STATS, runs glmark2 scenes off-screen (5 s each, default
# build texture shading) and prints the FPS, the accelerant's per-call
# times of the calls that take time, then the server's per-ioctl and per-stage
# submission times (printed when it stops). Full call list: the gm-*.log
# files in /tmp/overhead.
cd "$(dirname "$0")"
OUT=/tmp/overhead
. ./common.sh
cp "$GPU/RadeonGfx/build.x86_64/radeon_gfx.accelerant/radeon_gfx.accelerant" \
	"$HOME/config/non-packaged/add-ons/accelerants/"
[ $# -eq 0 ] && set -- build texture shading

stop_server
export RADEONGFX_STATS=1
start_server || { echo "server didn't start"; tail "$SERVER_LOG"; exit 1; }
n=0
for scene in "$@"; do
	# a scene with options (name:option=value...) as glmark2 -b takes it
	n=$((n + 1))
	log="$OUT/gm-$n-${scene%%:*}.log"
	timeout 60 "$GPU/install/bin/glmark2-es2-wayland" --off-screen \
		-b "$scene:duration=5" > "$log" 2>&1
	grep "FPS" "$log"
	grep -E "^  [A-Za-z]" "$log" | grep -v " 0\.0% of the time"
done
stop_server
grep -A30 "^RADEONGFX_STATS" "$SERVER_LOG"
