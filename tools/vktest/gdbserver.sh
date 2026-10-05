#!/bin/bash
# starts the RadeonGfx server under gdb in the background (backtraces of a
# crash in ~/server-gdb.log); vkrun.sh then uses it
cd ~/gpu/RadeonGfx
nohup gdb -batch -ex "handle SIGTERM nostop noprint pass" -ex "handle SIGINT nostop noprint pass" -ex "handle SIGSTOP nostop noprint pass" -ex "handle SIGCONT nostop noprint pass" -ex run -ex bt -ex "thread apply all bt 15" --args build.x86_64/RadeonGfx server > ~/server-gdb.log 2>&1 < /dev/null &
for i in $(seq 100); do grep -q "Polaris ready" ~/server-gdb.log && break; sleep 0.2; done
grep -E "DPM|ready" ~/server-gdb.log
