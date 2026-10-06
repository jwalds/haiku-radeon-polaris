# Phase 3 plan: 3D acceleration on Polaris (RadeonGfx route)

Status: proposed, 2026-09-30. Nothing here touches the GPU yet.
Code: [jwalds/RadeonGfx](https://github.com/jwalds/RadeonGfx), branch `polaris`.

## Starting point: X512's RadeonGfx stack

Source: <https://github.com/X547/RadeonGfx> (last commit 2023-01, no LICENSE
file), build notes: <https://gist.github.com/X547/292d5893da00098a924a8a0aa02d6a28>.

| Piece | What it does | State for Polaris |
|---|---|---|
| `kernel/radeon_gfx` | minimal kernel driver (derived from radeon_hd): maps MMIO, VRAM, shared_info | usable as-is; our radeon_hd kernel driver exposes the same areas |
| `RadeonGfx` server | userland process that owns the GPU: memory manager (VRAM + GTT), IH ring (polled), rings, fences, command submission, amdgpu DRM ioctl emulation | written for Southern Islands (GFX6, DMA v1, GMC 6, DCE 6); no Polaris units |
| `radeon_gfx.accelerant` | client proxy: libdrm calls go to the server over ThreadLink | generic |
| `libdrm2`, `mesa-drm`, `accelerant2` | libdrm/libdrm_amdgpu for Haiku; loads the device's accelerant and calls `instantiate_accelerant` | generic, 2023 |
| `VideoStreams`, `VideoStreamsWsi` | buffer sharing + Vulkan WSI layer for presenting | tied to RadeonGfx's own DCE 6 display code |
| Mesa RADV (+ Zink for OpenGL) | the actual Vulkan driver | upstream RADV winsys is unmodified; X512's old `main-rebased` branch is gone |

Server size: about 6,000 lines of C++ excluding register headers.
Components that are SI-specific and must be written for Polaris (GFX8/VI):

- GMC v8 (VM contexts, GART page table, system aperture) — and it must keep
  the VBIOS memory controller setup (see patch 0002).
- IH v3 (Tonga-style interrupt ring).
- SDMA v3 (replaces DMA v1).
- GFX v8: RLC, CP (PFP/ME/CE), MEC for compute, golden registers.
- Firmware loading with amdgpu's firmware headers (SI firmware is raw).
  Files from linux-firmware: `polaris11_{ce,pfp,me,mec,mec2,rlc,sdma,sdma1}.bin`,
  later `polaris11_smc.bin` / `polaris11_k_smc.bin` for clocks.
- DRM info replies: family VI, Polaris 11 chip IDs, GB_ADDR_CONFIG, tiling
  mode tables, CU info.

## How it fits with our driver

- **Display stays in radeon_hd.** Our accelerant in app_server keeps mode
  setting, cursor and DPMS. The server's DCE 6 display code is left out.
- **The server attaches to the radeon_hd device** and clones its register
  area, like the radeon_hd accelerant does. Open design question (step 1):
  how libdrm2 and the server find the device. libdrm2 loads the accelerant
  named by the device's signature and needs `instantiate_accelerant`;
  options are a second device node, or exporting that entry point from
  radeon_hd.accelerant. app_server scans `/dev/graphics` too, so a second
  node must not be picked up as a display.
- **Shared resources:** the VRAM allocator must reserve the frame buffer
  and the cursor image; only the server may touch GRBM/SRBM index registers.

## Steps

Each step ends in a test on the Haiku machine. Steps 1–3 cannot hang the GPU.

0. **Build.** Install meson/ninja/vulkan/glslang packages; build RadeonGfx
   and its helper libraries unchanged, to validate the toolchain.
1. **Attach, read-only.** Server opens the radeon_hd device, maps MMIO,
   reports GPU config (shader engines, CUs, VRAM) from registers. Decide the
   device-node question.
2. **Memory.** VRAM allocator around the frame buffer/cursor; GTT buffers
   (locked areas + physical addresses).
3. **GART/VM (GMC v8)** and **IH ring**, without reprogramming the MC.
4. **SDMA v3:** firmware, ring, fence write, fill/copy test. First real GPU
   work; hang risk from here on (every wait gets a timeout).
5. **GFX v8:** RLC + CP firmware, gfx ring NOP + fence, then a compute
   dispatch that writes a buffer.
6. **DRM emulation for VI** + libdrm2 + RADV: `vulkaninfo`, then an (done 2026-10-02)
   off-screen render read back to a PNG.
7. **Presenting:** Vulkan WSI on Haiku (VideoStreamsWsi, or a copy into a
   BBitmap first), then Zink for OpenGL. (done 2026-10-04: Vulkan through
   the Wayland WSI, OpenGL ES 3.2 through Zink and EGL on Wayland. Not done:
   desktop OpenGL through Haiku's BGLView.)
8. **Clocks/power:** SMC firmware so the GPU leaves boot clocks. (done
   2026-10-04: `RadeonGfx clocks start` and `clocks memory` enable engine
   clock DPM 214-1200 MHz with SVI2 voltage control and thermal throttling,
   and the memory clock at 1500 MHz. Not done: starting it automatically,
   AVFS, clock stretching, BAPM/power limit, deep sleep, ULV, PCIe link DPM,
   memory clock switching with a vblank length check.)

## Quality: tests before new features

Started 2026-10-06, before Quake III and further features. Four layers, the
cheap deterministic ones run most often:

1. **Host unit tests** (no GPU, seconds, every build): meson `test()`
   targets in RadeonGfx (`meson test -C build.x86_64`).
   - Sync: Locks Mutex/RecursiveLock, Fence, FenceGroup, Syncobj; signal,
     cancel, OnSignal before/after signaling, Retain/holder lifetime,
     multi-threaded stress with a timeout. (Would have caught the Mutex
     lost wakeup, the FenceGroup use-after-free and the OnSignal deadlock.)
   - ExternalAllocator: aligned allocation, fragmentation, free/merge,
     exhaustion, a randomized test against a reference model.
   - RingBuffer space accounting and wraparound on a memory-backed ring.
   - CS chunk parsing (RadeonServerDrm) through a loopback.
   - Golden files: PowerPlay parsing and the SMU74 DPM table built from a
     saved VBIOS dump, compared byte for byte; needs register/SMC access
     behind an interface.
2. **Server self-tests** (GPU, about a minute): `RadeonGfx selftest` with a
   pass/fail exit code: GFX/SDMA ring and IB execution, fences and EOP
   interrupts, VM fault detection, DPM sanity (SMC running, all sclk levels
   reachable), recovery from a deliberately hung ring, and a leak check
   (GTT/VRAM back to the baseline after each client).
3. **Functional API tests** (GPU, about 10 minutes): vkfill/vktri/glwl
   output compared against reference images, vkbench and fixed glmark2
   scenes with score floors (off-screen and windowed), must-pass subsets of
   dEQP-VK and dEQP-GLES2/3 from VK-GL-CTS. Must-pass and known-failure
   lists are kept in this repo.
4. **Stress and soak** (on demand, an hour or more): glmark2
   `--run-forever` with DPM, clients killed at random points plus the leak
   check, repeated server restarts, several clients at once.

Runner: `tools/test/run-all.sh [unit|smoke|func|soak]` on the Haiku
machine, with a timeout per case; on a hang it saves the `info` register
dump, kills the client, restarts the server (soft reset) and continues. It
writes a summary that goes into test-log.md. Gate: unit + smoke before each
RadeonGfx commit, functional tests before each push.

Order:

1. Unit test harness; Sync and allocator tests. (done 2026-10-06: 29
   tests, two new bugs fixed, see test-log)
2. Runner with watchdog; `selftest`; leak check. (done 2026-10-06:
   `tools/test/run-tests.sh` with unit, selftest, smoke and leak suites,
   16 cases in about 2.5 minutes; the self-test is the runner's selftest
   suite of the existing bring-up commands rather than a new RadeonGfx
   command. The hang suite (vkhang) blocks the GFX ring on purpose:
   the GPU resumes when unblocked, the server survives a client that exits
   with it blocked, and a server restart recovers it. Hang detection and
   recovery in the running server as Linux does it (done 2026-10-06):
   lockup timeout 10 s, soft recovery (killing the hung VMID's waves),
   otherwise GFX reset with the context guilty, VK_ERROR_DEVICE_LOST in
   RADV, new clients unaffected.)
3. Image comparisons for vkfill, vktri and glwl; glmark2 score floors.
4. VK-GL-CTS built for Haiku, must-pass lists.
5. PowerPlay/DPM golden files; stress and soak suite.

## Risks

- Months of work; steps 4–5 are where the GPU can hang and need reboots.
- The stack is from 2023 and Mesa has moved on; RADV may need newer ioctls
  than the emulation provides. Pick a Mesa version close to the stack first.
- RadeonGfx has no license file; fine for personal use, needs clarifying
  before publishing modified copies of its code.
- GPU at boot clocks until step 8, so early performance is not meaningful.

## Open issues

- **GFX command ring in system memory doesn't work** (found 2026-10-02,
  see test-log). Linux keeps the ring in GTT; RadeonGfx now puts it in
  CPU-visible VRAM (`RadeonRingBufferGfxV8::RingDomain()`), which works.
  Observed with the ring in GTT (mapped through the GART, snooped PTEs):
  - `CP_RB0_CNTL.MTYPE = 3` (UC): the CP fetches up to WPTR, but PFP and CE
    wait on buffer data forever; no VM fault.
  - MTYPE 0 (Linux' value): data arrives (PFP header shows the last NOP),
    but nothing visibly executes (SET_UCONFIG_REG, WRITE_DATA, EOP fence
    all missing), CE stuck in `CE_WAITING_ON_DE_COUNTER_UNDERFLOW`.
  - The CP stays stuck through halt/restart; only a reboot clears it, so
    each variant needs a fresh boot.
  - The CP reads system memory fine otherwise: COPY_DATA from GTT, and
    RADV's IBs in GTT. Ideas: CPU cache/snoop coherency of the ring writes
    (try clflush before WPTR), MTYPE 2 (CC), the rptr write-back address,
    or the CE needing its own setup (Linux' gfx_v8_0_cp_gfx_start() order).
