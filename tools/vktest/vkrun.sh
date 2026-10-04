#!/bin/bash
# Runs a Vulkan program on the RX 560 from a Terminal:
#   vkrun.sh ./vkwl [args]
# Starts the RadeonGfx server if it isn't running (and stops it again at
# the end), and points the program at RADV and libdrm2 in ~/gpu/install.
GPU=${GPU:-$HOME/gpu}
cd "$(dirname "$0")"

started=
if ! ps | grep -q "[R]adeonGfx server"; then
	"$GPU/RadeonGfx/build.x86_64/RadeonGfx" server > /tmp/radeongfx-server.log 2>&1 &
	started=$!
	for i in $(seq 100); do
		grep -q "Polaris ready\|failed" /tmp/radeongfx-server.log && break
		sleep 0.2
	done
	if ! grep -q "Polaris ready" /tmp/radeongfx-server.log; then
		echo "RadeonGfx server didn't start, see /tmp/radeongfx-server.log"
		kill -INT $started 2>/dev/null
		exit 1
	fi
fi

VK_DRIVER_FILES="$GPU/install/data/vulkan/icd.d/radeon_icd.x86_64.json" \
LIBRARY_PATH="$GPU/install/lib:%A/lib:$HOME/config/non-packaged/lib:$HOME/config/lib:/boot/system/non-packaged/lib:/boot/system/lib" \
	"$@"
status=$?

if [ -n "$started" ]; then
	kill -INT $started
	wait $started
fi
exit $status
