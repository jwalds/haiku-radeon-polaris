#!/bin/bash
# Starts the RadeonGfx server on Polaris, runs vkinfo against RADV, then stops
# the server cleanly (SIGINT). Logs: ~/server.log, ~/vkinfo.log
GPU=${GPU:-$HOME/gpu}
cd /tmp
"$GPU/RadeonGfx/build.x86_64/RadeonGfx" server > ~/server.log 2>&1 &
pid=$!
for i in $(seq 100); do
	grep -q "Polaris ready\|failed" ~/server.log && break
	kill -0 $pid 2>/dev/null || break
	sleep 0.2
done
cat ~/server.log
if grep -q "Polaris ready" ~/server.log; then
	VK_DRIVER_FILES="$GPU/install/data/vulkan/icd.d/radeon_icd.x86_64.json" \
	LIBRARY_PATH="$GPU/install/lib:$LIBRARY_PATH" \
		timeout 60 "$GPU/vktest/vkinfo" > ~/vkinfo.log 2>&1
	echo "vkinfo exit=$?" >> ~/vkinfo.log
	cat ~/vkinfo.log
	echo "--- server after vkinfo:"
	tail -n +$(($(wc -l < ~/vkinfo.log) * 0 + 1)) ~/server.log | sed -n '/Polaris ready/,$p' | tail -n +2
fi
kill -INT $pid 2>/dev/null
for i in $(seq 50); do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
kill -0 $pid 2>/dev/null && echo "[!] server still running (pid $pid)"
echo "--- server shutdown:"
sed -n '/Polaris ready/,$p' ~/server.log | grep -E "GFX|GART|SMU|server|\[!\]"
sync
