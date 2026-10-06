# Tests

The test suites run on the Haiku machine with the RX 560; the RadeonGfx
unit tests also run without the GPU.

## Setup on the Haiku machine

| In this repository | On the machine |
|---|---|
| `tools/test/` (run-tests.sh, gpumem.c, imgcmp.py, perfcheck.py, perf-floors.txt) | `~/gpu/test/` |
| `tests/reference/*.png` | `~/gpu/test/reference/` |
| `tools/vktest/` (vkinfo, vkfill, vktri, vkbench, vkhang, glwl, glref, ...) | `~/gpu/vktest/` |

Build `gpumem` and the test programs as their file headers say.

## Running

    cd ~/gpu/test
    ./run-tests.sh                  # unit selftest smoke render leak hang
    ./run-tests.sh smoke render     # some suites
    CASES="glref image-gl-msaa" ./run-tests.sh render    # some cases
    GDB=1 ./run-tests.sh leak       # server under gdb, backtraces in server.log

Results go to `~/gpu/test-results/<date-time>/`: `summary.txt`, a log per
case, `server.log`, rendered images and diff images in `render/`. The exit
code is 0 if no case failed. A case is PASS, WARN (new `[!]` lines in the
server log), or a failure: FAIL, HANG, CRASH, LEAK, SLOW (below a
performance floor) or DIFF (image differs from its reference).

Run the unit and smoke suites before committing RadeonGfx changes, all
suites before pushing.

## Reference images

`glref` renders fixed OpenGL ES scenes through Zink, `vktri` a Vulkan
triangle; rendering is bit exact from run to run. `imgcmp.py compare`
accepts at most 0.1% of the pixels differing by more than 2 in a channel.

A new or changed scene: render it, look at it (`imgcmp.py montage
review.png <images>`), and only when it's right, copy it into
`tests/reference/` (recompressed: `imgcmp.py montage <reference> <image>`).
Never update a reference just to make a DIFF go away without knowing why
the rendering changed.

## Performance floors

`tools/test/perf-floors.txt` has floors for glmark2 scenes and vkbench at
about 95% of the measured medians. Raise them when something gets faster
for good; don't lower them to make a run pass.
