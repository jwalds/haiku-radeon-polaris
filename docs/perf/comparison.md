# Performance: Haiku against Linux on the same machine

Machine: Core i5-2320, Radeon RX 560 (Polaris 11, 4 GB GDDR5), DPM on both
sides (engine 214-1200 MHz, memory 1500 MHz under load).

## 2026-10-07: first comparison

Linux: Ubuntu 26.04 live, kernel 7.0, Mesa 26.0.3, glmark2 2023.01 on a
headless Weston (Wayland, like Haiku). Raw logs in `linux-2026-10-07/`
(`tools/linux-bench.sh`; Zink on Xorg fell back to software rendering
there, "DRI3 not available", so Wayland it is). Haiku: RadeonGfx + Mesa
23.3.6 Zink, numbers of the test runner on 2026-10-06.

glmark2-es2-wayland --off-screen, FPS:

| scene   | Linux radeonsi | Linux Zink | Haiku Zink | Haiku / Linux Zink |
|---------|---------------:|-----------:|-----------:|-------------------:|
| build   | 7263 | 5010 | 2678 | 53% |
| texture | 6670 | 4460 | 2596 | 58% |
| shading | 7090 | 4426 | 2669 | 60% |
| refract | 1029 |  677 |  404 | 60% |
| terrain |  536 |  378 |  365 | 97% |
| full benchmark score | 4991 | 3314 | 2206 (2026-10-05) | 67% |

vkbench (64 MB in VRAM, 20 times):

|      | Linux RADV | Haiku RADV | Haiku / Linux |
|------|-----------:|-----------:|--------------:|
| fill | 85.6 GB/s | 25.8 GB/s | 30% |
| copy | 41.3 GB/s | 13.5 GB/s | 33% |

What it says:
- The GPU's memory bandwidth on Haiku is a third of Linux' (vkbench is
  plain memory traffic, independent of the Mesa version and of per-frame
  overhead). The clock targets are the same, so it's our setup: the memory
  clock actually reached, the memory controller's arbiter/sequencer
  timings for 1500 MHz, the VM/L2/TLB configuration (fragment size), or
  clock gating. Linux' register values idle and under load are in
  `linux-2026-10-07/regs-*.txt` (register list `reglist.txt`); the same
  list read on Haiku shows the differences.
- terrain (shader bound) is as fast as on Linux: the engine clock and the
  shader array are fine.
- The fast scenes (build/texture/shading, 0.2-0.4 ms per frame) reach
  53-60% of Linux Zink: per-frame overhead (submission through the
  server, fences) and Mesa 23.3 against 26.0 both count; bandwidth may too.
- radeonsi is 1.5x Zink on Linux: native OpenGL would be the long-term
  gain (a radeonsi port on the same winsys emulation).

Next: the Haiku register dump of the same list (idle and under load) and
the differences; then per-frame overhead (Haiku Zink vs Linux Zink with
the same Mesa version).

## 2026-10-07: the memory bandwidth found

Haiku's and Linux' registers under load (`RadeonGfx regs reglist.txt`,
`haiku-2026-10-07/regs-*.txt`): 20 of 587 differ. The memory sequencer
timings were the same as Linux' at 1500 MHz, but the MC arbiter DRAM
timings weren't (MC_ARB_DRAM_TIMING 0xa72e9322 against 0x221e0e12): the
arbiter table in SMC RAM (`RadeonGfx clocks`) held garbage (0xffd0f2c9,
0xdf1ab50e, ...). It comes from the VBIOS (DynamicMemorySettings), run by
our AtomBIOS interpreter, which allocated a table call's work space with
malloc where Linux uses kcalloc: the table computed with leftover memory.
Fixed (RadeonGfx 6d1b1ab, with two smaller deviations from Linux' atom.c).
The DPM voltages, also computed by VBIOS tables, came out unchanged.

vkbench: fill 25.8 -> **82.6 GB/s** (Linux 85.6), copy 13.5 -> **39.8 GB/s**
(Linux 41.3).

Full glmark2 off-screen after the fix (`haiku-2026-10-07/full-zink-fixed.log`),
FPS, Haiku Zink / Linux Zink / Linux radeonsi:

| scene | Haiku | Linux Zink | Linux radeonsi | Haiku / Linux Zink |
|---|---:|---:|---:|---:|
| build use-vbo=false | 2185 | 1873 | 2257 | 117% |
| build use-vbo=true | 2772 | 4986 | 7099 | 56% |
| texture (3 filters) | ~2759 | ~4440 | ~6730 | 62% |
| shading (4) | ~2766 | ~4430 | ~6970 | 62% |
| bump (3) | ~2770 | ~4225 | ~6590 | 66% |
| effect2d 3x3 / 5x5 | 3056 / 3067 | 3959 / 1975 | 5476 / 3667 | 77% / 155% |
| pulsar | 2359 | 4057 | 5950 | 58% |
| desktop blur / shadow | 1355 / 1492 | 1059 / 1798 | 1713 / 2635 | 128% / 83% |
| buffer (3) | 139 / 127 / 221 | 606 / 380 / 606 | 659 / 389 / 674 | 23-36% |
| ideas | 946 | 1709 | 2607 | 55% |
| jellyfish | 2338 | 3000 | 4238 | 78% |
| terrain | 786 | 386 | 532 | 204% |
| shadow | 2153 | 2887 | 4085 | 75% |
| refract | 1151 | 698 | 1028 | 165% |
| conditionals/function/loop (8) | ~2850 | ~4470 | ~6740 | 64% |
| **score** | **2259** | 3314 | 4991 | 68% |

What's left:
- GPU-bound scenes (terrain, refract, 5x5 blur, desktop blur) are now
  faster than on Linux (with Mesa 26.0's Zink; radeonsi too for terrain and
  refract).
- About 20 scenes stop at 2750-2850 FPS: a fixed cost of about 355 us per
  frame against about 225 us for Zink on Linux. The ~130 us more per frame
  is the submission path (server round trips, fences): the next target.
- The buffer scenes (glBufferSubData/glMapBuffer every frame) reach 23-36%
  of Linux: the CPU path into GPU buffers (mapping type of GTT/VRAM, round
  trips per map) is slow: the second target.

## 2026-10-07: per-frame overhead

The ~130 us per frame more than Linux, measured with call statistics in the
accelerant and the server (`RADEONGFX_STATS`, `tools/test/overhead.sh`).
glmark2 build, per frame (one submission, one timeline wait, one syncobj
transfer), client time:

| | CS submit | timeline wait | transfer | build FPS |
|---|---:|---:|---:|---:|
| start | 87.5 us | 45 us | 36 us | ~3000 |
| one-way transfer | 87 us | 50 us | 4.3 us | 3340 |
| optimized server and accelerant (-O2) | 56 us | 49 us | 4.2 us | 3704 |
| page tables without VRAM reads | 56 us | 41 us | 4.1 us | 3718 |
| optimized libdrm, libaccelerant, glmark2 | 56 us | 39 us | 4.4 us | 4345 |
| reached timeline points answered in the client | 61 us | 0.3 us | 4.0 us | 5305 |

What each step was (RadeonGfx 7c3a903..073712d):
- SYNCOBJ_TRANSFER is one-way: the server handles a client thread's
  messages in order and replies only when asked (`NeedsReply()`).
- The server, the accelerant and their libraries were built with -O0
  (meson's default `debug`): `debugoptimized` is the default now. libdrm2,
  accelerant2 and glmark2 too (Mesa was built optimized already; Linux'
  glmark2 is a distribution build).
- Remapping a submission's IBs read the page directory and the GART table
  (both in VRAM, uncached reads over PCIe) and switched into the memory
  manager's domain per page: 15 -> 2.5 us per submission.
- Zink waits for a timeline point every frame, nearly always one reached
  already. The server publishes each syncobj's last signaled point in a
  per-team area, the accelerant answers such waits without a round trip (as
  Linux' ioctl returns at once). Points above 0 only: they never go back to
  unsignaled.

The faster frames uncovered two memory problems on the way:
- `AMDGPU_INFO_MEMORY` returned fixed numbers (a 3 GB GTT heap where the
  GART has 512 MB). Now the memory manager's sizes and usage.
- A Zink 23.3 bug (`patches/gpu-stack/mesa.patch`, zink_bo.c): a freed
  slab entry remembers the batch state that last used it, not the batch.
  With batches completing quickly Zink reuses the same batch state every
  frame, so the entry looks in use by the current batch whenever the
  allocator checks, and `pb_slabs_reclaim()` gives up after two such
  entries: every allocation took a new 2 MB slab until the GTT was full (the
  full benchmark aborted in the buffer scenes). The check now uses the
  batch state's submit counter, as `zink_bo_has_usage()` does.

Full glmark2 off-screen (`haiku-2026-10-07/full-zink-overhead.log`), FPS:

| scene | Haiku before | Haiku now | Linux Zink | Linux radeonsi | now / Linux Zink |
|---|---:|---:|---:|---:|---:|
| build use-vbo=false / true | 2185 / 2772 | 3637 / 5368 | 1873 / 4986 | 2257 / 7099 | 194% / 108% |
| texture (3 filters) | ~2759 | ~5254 | ~4440 | ~6730 | 118% |
| shading (4) | ~2766 | ~5286 | ~4430 | ~6970 | 119% |
| bump (3) | ~2770 | ~5262 | ~4225 | ~6590 | 125% |
| effect2d 3x3 / 5x5 | 3056 / 3067 | 5554 / 5493 | 3959 / 1975 | 5476 / 3667 | 140% / 278% |
| pulsar | 2359 | 4777 | 4057 | 5950 | 118% |
| desktop blur / shadow | 1355 / 1492 | 2114 / 2500 | 1059 / 1798 | 1713 / 2635 | 200% / 139% |
| buffer (3) | 139 / 127 / 221 | 459 / 453 / 530 | 606 / 380 / 606 | 659 / 389 / 674 | 76% / 119% / 87% |
| ideas | 946 | 1588 | 1709 | 2607 | 93% |
| jellyfish | 2338 | 4106 | 3000 | 4238 | 137% |
| terrain | 786 | 852 | 386 | 532 | 221% |
| shadow | 2153 | 3680 | 2887 | 4085 | 127% |
| refract | 1151 | 1151 | 698 | 1028 | 165% |
| conditionals/function/loop (8) | ~2850 | ~5550 | ~4470 | ~6740 | 124% |
| **score** | **2259** | **4221** | 3314 | 4991 | 127% |

Haiku's Zink (Mesa 23.3) is now faster than Linux' (Mesa 26.0) in all but
three scenes, at 85% of Linux radeonsi's score.

What's left:
- The buffer scenes with map (76-87% of Linux Zink): the CPU path into GPU
  buffers.
- A submission still costs the client ~56 us (Linux: a few us): the server
  handles it in ~30 us (parse 6, remap 2.5, schedule 8, ring 6), the rest
  is the round trip. An asynchronous submission (no reply) would need the
  sequence number and errors handled in the client.
- Polaris has no interrupts here: the server polls the IH ring every 250
  us. Waits for points not yet reached see that latency.
