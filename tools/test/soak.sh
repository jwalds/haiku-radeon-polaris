#!/bin/bash
# soak.sh [minutes]: a long run of the RX 560 stack under load (default
# 60 minutes), looking for leaks, drift and rare failures.
#
# Load, all at the same time:
#   - glmark2 off-screen with five scenes over and over (--run-forever)
#   - client churn: vkfill, vktri, glref, glwl (120 frames), vkbench and
#     vkinfo one after the other; every SOAK_KILL_EVERY rounds (5) a glwl
#     killed with SIGKILL after 1-4 s; every 10th round glref's images
#     compared with the references
#   - every SOAK_HANG_EVERY minutes (10; 0: never) a GPU hang (vkhang
#     detect: lockup timeout, reset, device lost); glmark2 is restarted
#     after it, as it lost its device too
# Every SOAK_CHECKPOINT_EVERY minutes (10) the load pauses: an idle sample
# and a short benchmark alone (vkbench, glmark2 build/texture/refract) in
# checkpoints/, for drift and memory trends without the noise of the load.
# Every 30 s samples.csv gets the server's areas, memory, threads,
# semaphores and ports, the system's memory, semaphores and ports, the GPU
# memory in use (gpumem), the server's warnings and the client counts.
# Idle samples (no client) after a warm-up with all of the load and at the
# end must match (with the server's thread, area, semaphore and port lists
# for what grew); soak-report.py compares them and the trends and writes
# report.txt.
#
# Results: ~/gpu/test-results/soak-<date-time>/. Exit code 0 if the
# report found nothing.

MINUTES=${1:-60}
HANG_EVERY=${SOAK_HANG_EVERY:-10}
CHECKPOINT_EVERY=${SOAK_CHECKPOINT_EVERY:-10}
KILL_EVERY=${SOAK_KILL_EVERY:-5}
OUT=${OUT:-${GPU:-$HOME/gpu}/test-results/soak-$(date +%Y%m%d-%H%M%S)}
. "$(dirname "$0")/common.sh"

SAMPLES=$OUT/samples.csv
EVENTS=$OUT/events.log
GLMARK2=$GPU/install/bin/glmark2-es2-wayland
START=$(date +%s)
: > "$EVENTS"
mkdir -p "$OUT/render" "$OUT/failures"

server_pid()
{
	pid_of '^[^ ]*[R]adeonGfx server' | head -1
}

# sample <label>: one line of samples.csv
sample()
{
	local pid=$(server_pid)
	local areas alloc threads sems ports
	read areas alloc <<< "$(listarea $pid 2>/dev/null | awk '
		$1 ~ /^[0-9]+$/ && NF >= 10 {n++; a += strtonum("0x" $(NF - 5))}
		END {print n + 0, int(a / 1024)}')"
	threads=$(ps | awk -v id=$pid '{for (i = 1; i < NF; i++)
		if ($i == id) {print $(i + 1); exit}}')
	sems=$(listsem $pid 2>/dev/null | awk 'f {n++} /^---/ {f = 1} END {print n + 0}')
	ports=$(listport $pid 2>/dev/null | awk 'f {n++} /^---/ {f = 1} END {print n + 0}')
	local sysSems=$(listsem 2>/dev/null | awk '/^sem:/ {gsub(",", ""); print $5; exit}')
	local sysPorts=$(listport 2>/dev/null | awk '/^port:/ {gsub(",", ""); print $5; exit}')
	# "... bytes free (used/max <used> / <max>)"
	local sysUsed=$(sysinfo -mem | awk 'match($0, /used\/max +([0-9]+)/, m) {
		print int(m[1] / 1024); exit}')
	# other places memory can go: app_server (the clients' windows) and the
	# kernel (drivers)
	local appServer=$(listarea $(pid_of '^/boot/system/servers/[a]pp_server' | head -1) 2>/dev/null \
		| awk '$1 ~ /^[0-9]+$/ && NF >= 10 {a += strtonum("0x" $(NF - 5))} END {print int(a / 1024)}')
	local kernel=$(listarea 1 2>/dev/null \
		| awk '$1 ~ /^[0-9]+$/ && NF >= 10 {a += strtonum("0x" $(NF - 5))} END {print int(a / 1024)}')
	local vram vis gtt
	read vram vis gtt <<< "$(gpumem | sed 's/[a-z_]*=//g')"
	local warnings=$(grep -c '^\[!\]' "$SERVER_LOG")
	local clients=$(wc -l < "$EVENTS")
	local failures=$(awk '$4 != 0' "$EVENTS" | wc -l)
	echo "$(date +%T),$(($(date +%s) - START)),$1,$areas,$alloc,$threads,$sems,$ports,$sysUsed,$sysSems,$sysPorts,$appServer,$kernel,$vram,$vis,$gtt,$warnings,$clients,$failures" \
		>> "$SAMPLES"
}

# idle_dump <label>: the server's threads, areas and semaphores, for what
# an idle comparison finds grown
idle_dump()
{
	local pid=$(server_pid)
	ps -a $pid > "$OUT/$1-threads.txt" 2>&1
	listarea $pid > "$OUT/$1-areas.txt" 2>&1
	listsem $pid > "$OUT/$1-sems.txt" 2>&1
	listport $pid > "$OUT/$1-ports.txt" 2>&1
}

# run_client <name> <time limit> <command...>: one line in events.log;
# the log of a failed run is kept in failures/
run_client()
{
	local name=$1 limit=$2
	shift 2
	local log=$OUT/last-$name.log
	local start=$(date +%s)
	(cd "$VKTEST" && timeout -k 2 "$limit" "$@") > "$log" 2>&1
	local rc=$?
	echo "$(date +%T) $(($(date +%s) - START)) $name $rc $(($(date +%s) - start))" \
		>> "$EVENTS"
	if [ $rc != 0 ]; then
		cp "$log" "$OUT/failures/$(date +%H%M%S)-$name.log"
		echo "$(date +%T) [!] $name failed: exit code $rc"
	fi
	return $rc
}

event()
{
	# event <name> <0|1>: a check that isn't a client run
	echo "$(date +%T) $(($(date +%s) - START)) $1 $2 0" >> "$EVENTS"
	[ "$2" = 0 ] || echo "$(date +%T) [!] $1 failed"
}

# checkpoint <n>: the load paused: an idle sample, then a short benchmark
# alone (no other client), for drift and memory trends with little noise
checkpoint()
{
	sleep 5
	sample idle-$1
	mkdir -p "$OUT/checkpoints"
	(cd "$VKTEST" && timeout 30 ./vkbench) > "$OUT/checkpoints/$1-vkbench.log" 2>&1
	(cd "$VKTEST" && timeout 60 "$GLMARK2" --off-screen -b build:duration=5 \
		-b texture:duration=5 -b refract:duration=5) \
		> "$OUT/checkpoints/$1-glmark2.log" 2>&1
	echo "$(date +%T) checkpoint $1"
}

LONG_PID=
start_long_client()
{
	echo "=== glmark2 start $(date +%T)" >> "$OUT/glmark2.log"
	(cd "$VKTEST" && exec "$GLMARK2" --off-screen --run-forever -b build \
		-b texture -b shading -b refract -b terrain) >> "$OUT/glmark2.log" 2>&1 &
	LONG_PID=$!
}

stop_long_client()
{
	[ -n "$LONG_PID" ] || return
	kill $LONG_PID 2>/dev/null
	wait_for $LONG_PID 5 || kill -9 $LONG_PID 2>/dev/null
	wait $LONG_PID 2>/dev/null
	LONG_PID=
}

churn_round()
{
	local round=$1
	run_client vkfill 30 ./vkfill
	run_client vktri 30 ./vktri
	run_client glref 60 ./glref "$OUT/render"
	run_client glwl 60 ./glwl 120
	run_client vkbench 60 ./vkbench
	run_client vkinfo 30 ./vkinfo
	if [ $((round % 10)) = 0 ]; then
		local reference ok=0
		for reference in "$TEST"/reference/gl-*.png; do
			python3 "$TEST/imgcmp.py" compare \
				"$OUT/render/$(basename "$reference")" "$reference" \
				> /dev/null 2>&1 || ok=1
		done
		event images $ok
	fi
	if [ $((round % KILL_EVERY)) = 0 ]; then
		(cd "$VKTEST" && exec ./glwl 100000) > /dev/null 2>&1 &
		local pid=$!
		sleep $((1 + RANDOM % 4))
		kill -9 $pid 2>/dev/null
		wait $pid 2>/dev/null
		event kill-glwl 0
	fi
}

echo "soak: $MINUTES minutes, results in $OUT"
echo "time,elapsed,label,server_areas,server_alloc_kb,server_threads,server_sems,server_ports,sys_used_kb,sys_sems,sys_ports,app_server_kb,kernel_kb,vram,vis_vram,gtt,server_warnings,clients,failures" \
	> "$SAMPLES"
if ! start_server; then
	echo "[!] the server didn't start"
	exit 1
fi

# Warm-up with everything of the soak once (several clients at a time, a
# kill, a hang reset), so that pools and lazily started threads reach
# their high-water mark before the idle reference
start_long_client
churn_round 0
KILL_EVERY_SAVED=$KILL_EVERY
KILL_EVERY=1
churn_round 1
KILL_EVERY=$KILL_EVERY_SAVED
if [ "$HANG_EVERY" != 0 ]; then
	run_client vkhang-detect 60 ./vkhang detect
fi
stop_long_client
sleep 5
sample idle-start
idle_dump idle-start
checkpoint 0

sampler()
{
	while [ ! -e "$OUT/stop" ]; do
		sample run
		sleep 30
	done
}
sampler &
SAMPLER_PID=$!

start_long_client
DEADLINE=$((START + MINUTES * 60))
LAST_HANG=$(date +%s)
LAST_CHECKPOINT=$(date +%s)
CHECKPOINT=0
round=1
while [ $(date +%s) -lt $DEADLINE ]; do
	if ! kill -0 $LONG_PID 2>/dev/null; then
		event glmark2-died 1
		cp "$OUT/glmark2.log" "$OUT/failures/$(date +%H%M%S)-glmark2.log"
		start_long_client
	fi
	churn_round $round
	if [ "$HANG_EVERY" != 0 ] \
		&& [ $(($(date +%s) - LAST_HANG)) -ge $((HANG_EVERY * 60)) ]; then
		run_client vkhang-detect 60 ./vkhang detect
		LAST_HANG=$(date +%s)
		# glmark2 lost its device as well
		stop_long_client
		start_long_client
	fi
	if [ $(($(date +%s) - LAST_CHECKPOINT)) -ge $((CHECKPOINT_EVERY * 60)) ]; then
		stop_long_client
		CHECKPOINT=$((CHECKPOINT + 1))
		checkpoint $CHECKPOINT
		LAST_CHECKPOINT=$(date +%s)
		start_long_client
	fi
	if ! server_running; then
		event server-died 1
		break
	fi
	round=$((round + 1))
done

stop_long_client
touch "$OUT/stop"
wait $SAMPLER_PID
checkpoint $((CHECKPOINT + 1))
sleep 5
sample idle-end
idle_dump idle-end
stop_server
echo "soak: $round rounds, $(wc -l < "$EVENTS") client runs"
python3 "$TEST/soak-report.py" "$OUT" | tee "$OUT/report.txt"
