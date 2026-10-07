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
