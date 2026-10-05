#!/bin/bash
# gmcase.sh <glmark2 args...>: runs glmark2-es2-wayland for up to 20 s; on a
# hang kills it and restarts the server (its init resets the GPU)
cd ~/gpu/vktest
pid_of() { ps | grep -E "$1" | awk "{for(i=1;i<=NF;i++) if (\$i ~ /^[0-9]+$/ && \$(i+1) ~ /^[0-9]+$/) {print \$i; break}}"; }
[ -n "$(pid_of "[R]adeonGfx server")" ] || ./gdbserver.sh > /dev/null
GM=${GM:-$HOME/gpu/install/bin/glmark2-es2-wayland}
timeout 20 ./vkrun.sh $GM "$@" > ~/gmcase.log 2>&1
rc=$?
grep -E "FPS|Score" ~/gmcase.log | tail -3
if [ $rc = 124 ]; then
	echo "HANG: $(~/gpu/RadeonGfx/build.x86_64/RadeonGfx info 2>&1 | grep -E "GRBM_STATUS |STALLED")"
	for p in $(pid_of "[g]lmark2"); do kill -9 $p; done
	kill -TERM $(pid_of "[R]adeonGfx server"); sleep 4
	for p in $(pid_of "[g]db -batch") $(pid_of "[R]adeonGfx server"); do kill -9 $p; done
	./gdbserver.sh | grep -E "soft reset|ready"
fi
