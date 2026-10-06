#!/bin/bash
# linux-bench.sh: the benchmarks of the Haiku test runner on Linux (Xubuntu
# live session, X11) on the same machine, for comparison:
#   - glmark2-es2 off-screen with the runner's five scenes, the full
#     default benchmark, and windowed (vsync) build and texture scenes
#   - with radeonsi (Mesa's native OpenGL) and with Zink on RADV (the
#     layering the Haiku stack uses)
#   - vkbench (tools/vktest/vkbench.c) if it is next to this script
# GPU clocks are sampled every second; versions and DPM state are saved.
#
# usage: bash linux-bench.sh        (installs packages with sudo apt)
# Results: ./linux-bench-<date-time>/ and a .tar.gz of it.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$PWD/linux-bench-$(date +%Y%m%d-%H%M%S)
mkdir -p "$OUT"
cd "$OUT" || exit 1

echo "== packages"
sudo apt-get update -qq
sudo apt-get install -y -qq glmark2-es2 glmark2 mesa-utils vulkan-tools \
	gcc libvulkan-dev > apt.log 2>&1 || echo "[!] apt failed, see apt.log"

echo "== system"
uname -a > system.txt
lsb_release -a >> system.txt 2>&1
glxinfo -B > glxinfo.txt 2>&1
MESA_LOADER_DRIVER_OVERRIDE=zink glxinfo -B > glxinfo-zink.txt 2>&1
vulkaninfo --summary > vulkaninfo.txt 2>&1
CARD=$(ls -d /sys/class/drm/card*/device 2>/dev/null | while read d; do
	[ -e "$d/pp_dpm_sclk" ] && echo "$d" && break; done)
echo "card: $CARD" >> system.txt
for f in pp_dpm_sclk pp_dpm_mclk pp_dpm_pcie power_dpm_force_performance_level \
		pp_power_profile_mode; do
	echo "--- $f" >> dpm.txt
	cat "$CARD/$f" >> dpm.txt 2>&1
done
grep -E "OpenGL renderer|OpenGL version|OpenGL core" glxinfo*.txt

# current engine and memory clock, every second, while the benchmarks run
(while true; do
	echo "$(date +%T) sclk $(grep '\*' "$CARD/pp_dpm_sclk" | awk '{print $2}')" \
		"mclk $(grep '\*' "$CARD/pp_dpm_mclk" | awk '{print $2}')" \
		"temp $(cat "$CARD"/hwmon/hwmon*/temp1_input 2>/dev/null)"
	sleep 1
done) > clocks.log 2>&1 &
SAMPLER=$!

run()
{
	# run <name> <driver: radeonsi|zink> <glmark2 arguments...>
	local name=$1 driver=$2
	shift 2
	echo "== $name ($driver)"
	if [ "$driver" = zink ]; then
		MESA_LOADER_DRIVER_OVERRIDE=zink glmark2-es2 "$@" > "$name-$driver.log" 2>&1
	else
		glmark2-es2 "$@" > "$name-$driver.log" 2>&1
	fi
	grep -E "FPS|Score" "$name-$driver.log" | sed 's/^/  /'
}

for driver in radeonsi zink; do
	run five "$driver" --off-screen -b build -b texture -b shading -b refract \
		-b terrain
	run windowed "$driver" -b build:duration=3 -b texture:duration=3
	run full "$driver" --off-screen
done

if [ -e "$HERE/vktest/vkbench.c" ]; then
	echo "== vkbench"
	gcc -O2 -o vkbench "$HERE/vktest/vkbench.c" -lvulkan && ./vkbench > vkbench.log 2>&1
	grep "GB/s" vkbench.log | sed 's/^/  /'
fi

kill $SAMPLER
cd ..
tar czf "$OUT.tar.gz" "$(basename "$OUT")"
echo "results: $OUT.tar.gz"
