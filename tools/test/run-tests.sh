#!/bin/bash
# run-tests.sh [suite...]: the RX 560 test suites on the Haiku machine.
#   unit      RadeonGfx host unit tests (meson test), no GPU
#   selftest  the bring-up tests (GART, IH, SDMA, GFX) without the server
#   smoke     Vulkan and OpenGL clients on the server: vkinfo, vkfill, vktri,
#             vkbench, glwl, glmark2 (a few scenes), each with a leak check
#   leak      clients killed at random points, then a leak check
# Default: unit selftest smoke leak.
#
# Every case has a time limit. On a hang the register dump (RadeonGfx info)
# goes into the case's log, the client is killed and the server restarted
# (its init soft-resets the GPU). New "[!]" lines in the server log mark a
# case WARN. A hung client's thread backtraces (gdb) go into its log too.
# After each client the server's VRAM/GTT use (gpumem) must be
# back to where it was before, else LEAK.
#
# Results: ~/gpu/test-results/<date-time>/ (summary.txt, a log per case,
# server.log). Exit code 0 if no case failed.
#
# Environment: GDB=1 runs the server under gdb (crash backtraces in
# server.log); CASES="name..." runs only these cases of the suites.

GPU=${GPU:-$HOME/gpu}
RG=$GPU/RadeonGfx/build.x86_64/RadeonGfx
VKTEST=$GPU/vktest
TEST=$GPU/test
OUT=${OUT:-$GPU/test-results/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$OUT"
SUMMARY=$OUT/summary.txt
SERVER_LOG=$OUT/server.log
: > "$SUMMARY"
: > "$SERVER_LOG"

export MESA_LOADER_DRIVER_OVERRIDE=zink
export LIBGL_DRIVERS_PATH="$GPU/install/lib/dri"
export VK_DRIVER_FILES="$GPU/install/data/vulkan/icd.d/radeon_icd.x86_64.json"
export LIBRARY_PATH="$GPU/install/lib:%A/lib:$HOME/config/non-packaged/lib:$HOME/config/lib:/boot/system/non-packaged/lib:/boot/system/lib"

passed=0
failed=0
warned=0

# team ids of the processes whose ps line matches the regular expression
pid_of()
{
	ps | grep -E "$1" | awk '{for (i = 1; i < NF; i++) if ($i ~ /^[0-9]+$/ && $(i + 1) ~ /^[0-9]+$/) {print $i; break}}'
}

server_running()
{
	[ -n "$(pid_of '^[^ ]*[R]adeonGfx server')" ]
}

start_server()
{
	echo "=== server start $(date +%T)" >> "$SERVER_LOG"
	if [ -n "$GDB" ]; then
		# backtraces of a crash go into the server log
		gdb -batch -ex "handle SIGTERM nostop noprint pass" \
			-ex "handle SIGINT nostop noprint pass" \
			-ex "handle SIGSTOP nostop noprint pass" \
			-ex "handle SIGCONT nostop noprint pass" \
			-ex run -ex bt -ex "thread apply all bt 20" \
			--args "$RG" server >> "$SERVER_LOG" 2>&1 < /dev/null &
	else
		"$RG" server >> "$SERVER_LOG" 2>&1 &
	fi
	for i in $(seq 100); do
		tail -n 5 "$SERVER_LOG" | grep -q "Polaris ready\|init failed" && break
		sleep 0.2
	done
	tail -n 5 "$SERVER_LOG" | grep -q "Polaris ready"
}

stop_server()
{
	local pid
	for pid in $(pid_of '^[^ ]*[R]adeonGfx server'); do
		kill -INT $pid
	done
	for i in $(seq 50); do
		server_running || return 0
		sleep 0.2
	done
	for pid in $(pid_of '^[^ ]*[R]adeonGfx server') $(pid_of '[g]db -batch'); do
		kill -9 $pid
	done
	sleep 1
	echo "[runner] server killed (didn't stop on SIGINT)" >> "$SERVER_LOG"
}

ensure_server()
{
	server_running && return 0
	start_server
}

server_lines()
{
	wc -l < "$SERVER_LOG"
}

# log_for <name>: the case's log file, name-2.log etc. for repeated cases
log_for()
{
	local log=$OUT/$1.log n=2
	while [ -e "$log" ]; do
		log=$OUT/$1-$n.log
		n=$((n + 1))
	done
	echo "$log"
}

# wait_for <pid> <seconds>: false if the process still runs after that
wait_for()
{
	local ticks=$(($2 * 10))
	while kill -0 $1 2>/dev/null; do
		[ $ticks = 0 ] && return 1
		ticks=$((ticks - 1))
		sleep 0.1
	done
	return 0
}

gpumem()
{
	# the accelerant and libdrm2 print to stdout too
	"$TEST/gpumem" 2>/dev/null | grep '^vram='
}

selected()
{
	[ -z "$CASES" ] || [[ " $CASES " == *" $1 "* ]]
}

result()
{
	# result <PASS|FAIL|WARN|HANG|LEAK|CRASH|SKIP> <name> <seconds> [detail]
	printf "%-5s %-28s %6ss  %s\n" "$1" "$2" "$3" "$4" | tee -a "$SUMMARY"
	case $1 in
		PASS) passed=$((passed + 1)) ;;
		WARN) warned=$((warned + 1)); passed=$((passed + 1)) ;;
		SKIP) ;;
		*) failed=$((failed + 1)) ;;
	esac
}

# client_case <name> <time limit> <command...>: runs a client on the server
client_case()
{
	selected "$1" || return
	local name=$1 limit=$2
	shift 2
	local log=$(log_for "$name")
	if ! ensure_server; then
		result FAIL "$name" 0 "server didn't start"
		return
	fi
	local mark=$(server_lines)
	local before=$(gpumem)
	local start=$(date +%s)
	(cd "$VKTEST" && exec "$@") > "$log" 2>&1 &
	local pid=$! rc
	if wait_for $pid $limit; then
		wait $pid
		rc=$?
	else
		# where the client hangs, then the GPU state
		echo "=== hang: client threads" >> "$log"
		ps -as | awk -v id=$pid '/^-+$/ {show = 0}
			/^[^ ]/ {for (i = 1; i < NF; i++) if ($i == id && $(i + 1) ~ /^[0-9]+$/) show = 1}
			show' >> "$log"
		echo "=== hang: client backtraces" >> "$log"
		gdb -batch -p $pid -ex "thread apply all bt 25" >> "$log" 2>&1 < /dev/null
		kill -9 $pid 2>/dev/null
		wait $pid 2>/dev/null
		rc=124
	fi
	local seconds=$(($(date +%s) - start))

	if [ $rc = 124 ]; then
		echo "=== hang: RadeonGfx info" >> "$log"
		"$RG" info >> "$log" 2>&1
		result HANG "$name" $seconds "$(grep -E 'GRBM_STATUS ' "$log" | head -1)"
		stop_server
		return
	fi
	if ! server_running; then
		result CRASH "$name" $seconds "server died (see server.log)"
		return
	fi
	if [ $rc != 0 ]; then
		result FAIL "$name" $seconds "exit code $rc"
		return
	fi

	# the server frees a client's buffers when its team is gone
	local after
	for i in $(seq 10); do
		after=$(gpumem)
		[ "$after" = "$before" ] && break
		sleep 0.5
	done
	if [ "$after" != "$before" ]; then
		result LEAK "$name" $seconds "before: $before, after: $after"
		return
	fi

	local warnings=$(tail -n +$((mark + 1)) "$SERVER_LOG" | grep -c '^\[!\]')
	if [ "$warnings" != 0 ]; then
		result WARN "$name" $seconds "$warnings server warnings: $(tail -n +$((mark + 1)) "$SERVER_LOG" | grep '^\[!\]' | head -1)"
		return
	fi
	result PASS "$name" $seconds "$(grep -E "$CHECK_LINE" "$log" | tail -1)"
}

# standalone_case <name> <time limit> <RadeonGfx arguments...>: no server
standalone_case()
{
	selected "$1" || return
	local name=$1 limit=$2
	shift 2
	local log=$(log_for "$name")
	local start=$(date +%s)
	timeout -k 2 "$limit" "$RG" "$@" > "$log" 2>&1
	local rc=$?
	local seconds=$(($(date +%s) - start))
	if [ $rc = 124 ] || [ $rc = 137 ]; then
		echo "=== hang: RadeonGfx info" >> "$log"
		"$RG" info >> "$log" 2>&1
		result HANG "$name" $seconds
	elif [ $rc != 0 ]; then
		result FAIL "$name" $seconds "exit code $rc: $(grep -E '\[!\]|FAIL' "$log" | head -1)"
	else
		result PASS "$name" $seconds
	fi
}

suite_unit()
{
	local log=$OUT/unit.log
	local start=$(date +%s)
	meson test -C "$GPU/RadeonGfx/build.x86_64" > "$log" 2>&1
	local rc=$?
	local seconds=$(($(date +%s) - start))
	local counts=$(grep -E '^(Ok|Fail|Timeout):' "$log" | tr -s ' ' | tr '\n' ' ')
	if [ $rc = 0 ]; then
		result PASS unit $seconds "$counts"
	else
		result FAIL unit $seconds "$counts"
	fi
}

suite_selftest()
{
	stop_server
	standalone_case selftest-gart 60 garttest
	standalone_case selftest-ih 60 ihtest
	standalone_case selftest-sdma 60 sdmatest
	standalone_case selftest-gfx 60 gfxtest
}

suite_smoke()
{
	CHECK_LINE='created|OK|ok|GB/s|frames|Score' client_case vkinfo 30 ./vkinfo
	CHECK_LINE='OK|ok' client_case vkfill 30 ./vkfill
	CHECK_LINE='OK|ok|png' client_case vktri 30 ./vktri
	CHECK_LINE='GB/s' client_case vkbench 60 ./vkbench
	CHECK_LINE='frames|fps|FPS' client_case glwl 60 ./glwl 300
	CHECK_LINE='Score' client_case glmark2-offscreen 120 \
		"$GPU/install/bin/glmark2-es2-wayland" --off-screen \
		-b build -b texture -b shading -b refract -b terrain
	CHECK_LINE='Score' client_case glmark2-windowed 120 \
		"$GPU/install/bin/glmark2-es2-wayland" -b build:duration=3 \
		-b texture:duration=3
}

# kill_case <name> <delay> <command...>: kills the client after delay
# seconds; the server must free all of its memory
kill_case()
{
	selected "$1" || return
	local name=$1 delay=$2
	shift 2
	local log=$(log_for "$name")
	if ! ensure_server; then
		result FAIL "$name" 0 "server didn't start"
		return
	fi
	local mark=$(server_lines)
	local before=$(gpumem)
	(cd "$VKTEST" && exec "$@") > "$log" 2>&1 &
	local pid=$!
	sleep "$delay"
	kill -9 $pid 2>/dev/null
	wait $pid 2>/dev/null
	local after
	for i in $(seq 20); do
		after=$(gpumem)
		[ "$after" = "$before" ] && break
		sleep 0.5
	done
	if ! server_running; then
		result CRASH "$name" "$delay" "server died (see server.log)"
	elif [ "$after" != "$before" ]; then
		result LEAK "$name" "$delay" "before: $before, after: $after"
	elif [ "$(tail -n +$((mark + 1)) "$SERVER_LOG" | grep -c '^\[!\]')" != 0 ]; then
		result WARN "$name" "$delay" "$(tail -n +$((mark + 1)) "$SERVER_LOG" | grep '^\[!\]' | head -1)"
	else
		result PASS "$name" "$delay" "killed after $delay s, memory freed"
	fi
}

suite_leak()
{
	kill_case kill-vkbench 1.5 ./vkbench
	kill_case kill-glwl 2 ./glwl 100000
	kill_case kill-glmark2 3 "$GPU/install/bin/glmark2-es2-wayland" \
		--off-screen -b texture:duration=30
	kill_case kill-glmark2-late 6 "$GPU/install/bin/glmark2-es2-wayland" \
		--off-screen -b refract:duration=30
}

suites=${*:-unit selftest smoke leak}
echo "results: $OUT"
for suite in $suites; do
	echo "--- $suite"
	suite_$suite
done
stop_server

echo "passed $passed (with warnings $warned), failed $failed" | tee -a "$SUMMARY"
[ $failed = 0 ]
