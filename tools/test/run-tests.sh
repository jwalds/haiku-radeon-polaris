#!/bin/bash
# run-tests.sh [suite...]: the RX 560 test suites on the Haiku machine.
#   unit      RadeonGfx host unit tests (meson test), no GPU
#   selftest  the bring-up tests (GART, IH, SDMA, GFX) without the server
#   smoke     Vulkan and OpenGL clients on the server: vkinfo, vkfill, vktri,
#             vkbench, glwl, glmark2 (a few scenes), each with a leak check
#   render    glref's OpenGL scenes and vktri's triangle compared with the
#             reference images in tests/reference (imgcmp.py; DIFF with a
#             diff image in render/)
#   leak      clients killed at random points, then a leak check
#   hang      hangs the GFX ring on purpose (vkhang): the GPU resumes when
#             unblocked; a hang is detected after the lockup timeout and
#             reset (device lost for the client), a shader that never ends
#             soft-recovered; a client exiting with the GPU hung is freed
#             after the reset; a server restart resets a hung GPU too
# Default: unit selftest smoke render leak hang.
#
# Every case has a time limit. On a hang the register dump (RadeonGfx info)
# goes into the case's log, the client is killed and the server restarted
# (its init soft-resets the GPU). New "[!]" lines in the server log mark a
# case WARN. A hung client's thread backtraces (gdb) go into its log too.
# Performance: cases with floors in perf-floors.txt (glmark2, vkbench) are
# SLOW if a metric is below its floor (perfcheck.py).
# After each client the server's VRAM/GTT use (gpumem) must be
# back to where it was before, else LEAK.
#
# Results: ~/gpu/test-results/<date-time>/ (summary.txt, a log per case,
# server.log). Exit code 0 if no case failed.
#
# Environment: GDB=1 runs the server under gdb (crash backtraces in
# server.log); CASES="name..." runs only these cases of the suites.

. "$(dirname "$0")/common.sh"
SUMMARY=$OUT/summary.txt
: > "$SUMMARY"

passed=0
failed=0
warned=0

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

	local perf
	perf=$(python3 "$TEST/perfcheck.py" "$TEST/perf-floors.txt" "$name" "$log")
	if [ $? != 0 ]; then
		result SLOW "$name" $seconds "$perf"
		return
	fi

	local warnings=$(tail -n +$((mark + 1)) "$SERVER_LOG" | grep -c '^\[!\]')
	if [ "$warnings" != 0 ] && [ -z "$EXPECT_WARNINGS" ]; then
		result WARN "$name" $seconds "$warnings server warnings: $(tail -n +$((mark + 1)) "$SERVER_LOG" | grep '^\[!\]' | head -1)"
		return
	fi
	result PASS "$name" $seconds "${perf:-$(grep -E "$CHECK_LINE" "$log" | tail -1)}"
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

# image_case <name> <image>: compares a rendered image with
# tests/reference/<name>.png
image_case()
{
	selected "image-$1" || return
	local name=$1 image=$2
	local reference=$TEST/reference/$name.png
	local log=$(log_for "image-$name")
	if [ ! -e "$image" ]; then
		result FAIL "image-$name" 0 "not rendered"
		return
	fi
	python3 "$TEST/imgcmp.py" compare "$image" "$reference" \
		--diff "$OUT/render/$name-diff.png" > "$log" 2>&1
	if [ $? = 0 ]; then
		result PASS "image-$name" 0 "$(cat "$log")"
	else
		result DIFF "image-$name" 0 "$(cat "$log") (see render/$name-diff.png)"
	fi
}

suite_render()
{
	mkdir -p "$OUT/render"
	CHECK_LINE='all scenes' client_case glref 60 ./glref "$OUT/render"
	local reference
	for reference in "$TEST"/reference/gl-*.png; do
		local name=$(basename "$reference" .png)
		image_case "$name" "$OUT/render/$name.png"
	done
	rm -f "$VKTEST/triangle.png"
	CHECK_LINE='triangle' client_case vktri-render 30 ./vktri
	mv "$VKTEST/triangle.png" "$OUT/render/vk-triangle.png" 2>/dev/null
	image_case vk-triangle "$OUT/render/vk-triangle.png"
}

# hang_abandon_case <name>: a client exits with the GFX ring blocked; the
# server must keep its memory until the hang reset (lockup timeout) has
# completed its submission, then free it
hang_abandon_case()
{
	selected "$1" || return
	local name=$1
	local log=$(log_for "$name")
	if ! ensure_server; then
		result FAIL "$name" 0 "server didn't start"
		return
	fi
	local mark=$(server_lines)
	local before=$(gpumem)
	local start=$(date +%s)
	(cd "$VKTEST" && timeout -k 2 20 ./vkhang abandon) > "$log" 2>&1
	local rc=$?
	local done= after
	for i in $(seq 60); do
		done=$(tail -n +$((mark + 1)) "$SERVER_LOG" | grep "submissions done after")
		[ -n "$done" ] && break
		sleep 0.5
	done
	for i in $(seq 10); do
		after=$(gpumem)
		[ "$after" = "$before" ] && break
		sleep 0.5
	done
	local seconds=$(($(date +%s) - start))
	if [ $rc != 0 ]; then
		result FAIL "$name" $seconds "exit code $rc"
	elif ! server_running; then
		result CRASH "$name" $seconds "server died (see server.log)"
	elif [ -z "$done" ]; then
		result FAIL "$name" $seconds "the hang reset didn't complete the submission"
	elif [ "$after" != "$before" ]; then
		result LEAK "$name" $seconds "before: $before, after: $after"
	else
		result PASS "$name" $seconds "reset after the lockup timeout, memory freed"
	fi
}

# hang_restart_case <name>: a server stopped with the GFX ring blocked; the
# next one must soft-reset the GPU at its start
hang_restart_case()
{
	selected "$1" || return
	local name=$1
	local log=$(log_for "$name")
	local start=$(date +%s)
	ensure_server
	(cd "$VKTEST" && timeout -k 2 20 ./vkhang abandon) > "$log" 2>&1
	stop_server
	local mark=$(server_lines)
	start_server
	local seconds=$(($(date +%s) - start))
	local reset=$(tail -n +$((mark + 1)) "$SERVER_LOG" | grep -c "earlier run.*soft reset")
	if ! server_running; then
		result FAIL "$name" $seconds "server didn't start"
	elif [ "$reset" = 0 ]; then
		result FAIL "$name" $seconds "no soft reset: the GPU wasn't blocked"
	else
		result PASS "$name" $seconds "soft reset at server start"
	fi
}

suite_hang()
{
	CHECK_LINE='resumed' client_case hang-unblock 30 ./vkhang unblock
	# no fence for the lockup timeout: GPU reset, the context guilty
	CHECK_LINE='device lost' EXPECT_WARNINGS=1 client_case hang-detect 60 \
		./vkhang detect
	CHECK_LINE='OK' client_case hang-detect-after 30 ./vkfill
	# a shader that never ends: soft recovery, no reset
	CHECK_LINE='soft recovered' EXPECT_WARNINGS=1 client_case hang-shader 60 \
		./vkhang shader
	hang_abandon_case hang-abandon
	CHECK_LINE='OK' client_case hang-abandon-after 30 ./vktri
	hang_restart_case hang-restart
	CHECK_LINE='OK' client_case hang-restart-after 30 ./vkfill
}

suites=${*:-unit selftest smoke render leak hang}
echo "results: $OUT"
for suite in $suites; do
	echo "--- $suite"
	suite_$suite
done
stop_server

echo "passed $passed (with warnings $warned), failed $failed" | tee -a "$SUMMARY"
[ $failed = 0 ]
