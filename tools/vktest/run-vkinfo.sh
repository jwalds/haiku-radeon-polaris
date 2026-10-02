#!/bin/bash
# Starts the RadeonGfx server on Polaris, runs vkinfo (or VKTEST with
# VKTEST_ARGS) against RADV, then stops
# the server cleanly (SIGINT). Logs: ~/server.log, ~/vkinfo.log
GPU=${GPU:-$HOME/gpu}
cd "$GPU/vktest"
"$GPU/RadeonGfx/build.x86_64/RadeonGfx" server > ~/server.log 2>&1 &
pid=$!
for i in $(seq 100); do
	grep -q "Polaris ready\|failed" ~/server.log && break
	kill -0 $pid 2>/dev/null || break
	sleep 0.2
done
cat ~/server.log
if grep -q "Polaris ready" ~/server.log; then
	# libdrm2 replaces libdrm (only for vkinfo); VKINFO_ENV adds variables, VKINFO_WRAP a wrapper (gdb)
	env VK_DRIVER_FILES="$GPU/install/data/vulkan/icd.d/radeon_icd.x86_64.json" \
		LIBRARY_PATH="$GPU/install/lib:%A/lib:$HOME/config/non-packaged/lib:$HOME/config/lib:/boot/system/non-packaged/lib:/boot/system/lib" \
		$VKINFO_ENV $VKINFO_WRAP "$GPU/vktest/${VKTEST:-vkinfo}" $VKTEST_ARGS > ~/vkinfo.log 2>&1 &
	vk=$!
	for i in $(seq 100); do kill -0 $vk 2>/dev/null || break; sleep 0.2; done
	if kill -0 $vk 2>/dev/null; then
		echo "[!] vkinfo hangs, killed" >> ~/vkinfo.log
		kill -9 $vk
	fi
	wait $vk 2>/dev/null
	echo "vkinfo exit=$?" >> ~/vkinfo.log
	cat ~/vkinfo.log
	echo "--- server after vkinfo:"
	sed -n '/Polaris ready/,$p' ~/server.log | tail -n +2
fi
kill -INT $pid 2>/dev/null
for i in $(seq 50); do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
kill -0 $pid 2>/dev/null && echo "[!] server still running (pid $pid)"
echo "--- server shutdown:"
sed -n '/Polaris ready/,$p' ~/server.log | grep -E "GFX|GART|SMU|server|\[!\]"
sync
