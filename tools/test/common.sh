# common.sh: settings and helpers of run-tests.sh and soak.sh (sourced).
# Set OUT before sourcing to choose the results directory.

GPU=${GPU:-$HOME/gpu}
RG=$GPU/RadeonGfx/build.x86_64/RadeonGfx
VKTEST=$GPU/vktest
TEST=$GPU/test
OUT=${OUT:-$GPU/test-results/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$OUT"
SERVER_LOG=$OUT/server.log
: > "$SERVER_LOG"

export MESA_LOADER_DRIVER_OVERRIDE=zink
export LIBGL_DRIVERS_PATH="$GPU/install/lib/dri"
export VK_DRIVER_FILES="$GPU/install/data/vulkan/icd.d/radeon_icd.x86_64.json"
export LIBRARY_PATH="$GPU/install/lib:%A/lib:$HOME/config/non-packaged/lib:$HOME/config/lib:/boot/system/non-packaged/lib:/boot/system/lib"

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
