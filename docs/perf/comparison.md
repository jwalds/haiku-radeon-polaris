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
