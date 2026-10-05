# Test log

## 2026-09-27 — first boot with patches 0001+0002

- Host: Haiku hrev60147 (x86_64), RX 560 `1002:67ef` rev cf, HDMI 2560x1440.
- Built natively on the box (`~/haiku`, branch `radeon_hd-polaris`,
  `jam -q radeon_hd radeon_hd.accelerant`; must run via `bash -lc` so
  `LIBRARY_PATH` is set, otherwise the host `package` tool can't find libbsd).
- Installed to non-packaged, rebooted.
- **Result:** machine did not come back on the network (no ping/SSH after
  ~8 min). Screen state unknown — awaiting photo of the console / KDL.
- Recovery: boot menu → Safe mode options → *Disable user add-ons*, then
  `tools/haiku-install.sh --remove`.

## 2026-09-27 — tests 2–5

- Test 2: VESA shown. Cause: kernel `get_priority()` treats
  `/boot/system/non-packaged` like `/boot/system`, stock radeon_hd won.
  Fixed by installing to `/boot/home/config/non-packaged`.
- Test 3: our driver ran; AtomBIOS hang in `DIG1TransmitterControl`
  (8 lanes requested for HDMI). Fixed in patch 0003 (+ SetDCEClock).
- Test 4: mode set completes, black screen / monitor warning triangle.
  Register dump vs Linux: CRTC timings identical, `CRTC0_DISPOUT_ERROR_COUNT`
  = 0x19. Root cause: SetPixelClock v7 takes 100 Hz units (we passed
  10 kHz → 1/100 clock) and Polaris needs `ATOM_COMBOPHY_PLLx` per PHY.
  Fixed in patch 0006.
- Test 5 (live, `modeset_test set 1024 768 60` under fail-safe VESA):
  **picture on the HDMI monitor** — garbled (VESA app_server draws with a
  different pitch into the same VRAM) but desktop and moving cursor
  visible. DISPOUT error count 0. PLL 23 (COMBOPHY_PLL3) used.
- Open: BlankCRTC still loops 5 s (TEST_REG @ 0xC027) on the first
  power-down.

## 2026-09-28 — first working desktop

- Normal boot with patches 0001–0006: app_server runs on radeon_hd at
  2560x1440 but the monitor shows nothing. `screenmode -q 1920 1080 32 60`
  over SSH → **clear, crisp desktop** (148.5 MHz, DVI mode).
- Back to 2560x1440 (241.5 MHz) + poking `DIG3_DIG_BE_CNTL` 0x4d47 to
  DIG_MODE=3 (HDMI) with `radeon_regs` → **works**. DVI mode can't carry
  >165 MHz single link. Patch 0007 selects HDMI mode for HDMI sinks
  (EDID HDMI VSDB, DCE 5+).
- Network: DHCP gives a new address per boot (.105, .31, ...); find the
  box by scanning the local subnet for port 22.

## 2026-09-28 — controlled 2560x1440 tests (correction)

Patch 0007 did **not** take effect: `ddc2_read_edid1()` (accelerants/common)
reads only the 128-byte base block, and `edid_decode()` then parses
`num_sections` "extension" blocks from memory past the buffer. The HDMI
VSDB check therefore reads garbage; DIG3 stayed in DVI mode (0x4d47 =
0x20020800) on the working boot.

| Test | Clock | DIG mode | Result |
|---|---|---|---|
| boot (saved mode) | 241.699 MHz | DVI | works |
| `screenmode -m` Linux CVT-RB timing | 241.0 MHz (rounded) | DVI | works (59.8 Hz) |

CRTC timing registers for the 241.699 MHz mode are identical to Linux's
(H total 2720, V total 1481). Conclusion: neither HDMI mode nor the timing
was the cause of the earlier black 2560x1440 boots. Suspects: GPU state
left from a warm reboot (VBIOS does not re-POST), first mode set from the
VBIOS state (BlankCRTC timeout). Next: cold boot test.

Also: `screenmode -l` made the monitor drop out briefly; 2560x1440 is
missing from the mode list (EDID extension not read).

## 2026-09-28 — cold boot and BlankCRTC fix

- The monitor also has a DP cable from another computer. During the 5 s
  BlankCRTC timeout HDMI goes silent and the monitor auto-switches to the
  DP input — this explains the earlier "dead screen" boots.
- Cold boot with patches 0001–0007: clean desktop at 2560x1440, with the
  5 s gap.
- Patch 0008 (VGA enable around BlankCRTC, as Linux dce_v11_0): **boot
  goes straight to the desktop at 2560x1440@60**, 0 AtomBIOS timeouts,
  DISPOUT error count 0.

## 2026-09-29 — EDID extension

- Correction: 2560x1440 (60 and 75 Hz) *is* in the mode list; an earlier
  `grep | head` cut it off.
- Monitor EDID base block: 2560x1440@59.95 (241.5 MHz) and @74
  (329.79 MHz), ranges 30–113 kHz / 56–76 Hz, max 340 MHz, 1 extension.
- Patch 0009: `ddc2_read_edid1()` now reads the first extension block and
  clamps `num_sections`, fixing the out-of-bounds decode; this makes the
  HDMI VSDB detection from patch 0007 work.
- With patch 0009 the boot mode set picks HDMI (`encoder_mode_set: encoder
  mode 3, EDID sections 1, CTA data blocks 5`) and `DIG3_DIG_BE_CNTL`
  reads DIG_MODE=3.
- 2560x1440@75 (329.8 MHz) works.
- Open: after a *runtime* mode change (`screenmode`), the driver still
  requests HDMI (trace shows mode 3) and issues the same AtomBIOS call
  sequence as at boot, but DIG_MODE reads back 2 (DVI). The picture is fine
  in both cases. Suspect VBIOS state (BIOS scratch registers / device
  flags) that differs between the first and later mode sets. Matters for
  HDMI audio/infoframes later.

## 2026-09-29 — DPMS and hardware cursor

- DPMS: `tools/dpms_test cycle off 8` → monitor goes to standby (and
  auto-switches to its other input), returns to the desktop on wake. No
  AtomBIOS timeouts. After the DPMS cycle DIG_MODE is HDMI again.
- Patch 0011: hardware cursor (DCE 4+ `CUR_*` registers, 64x64 premultiplied
  ARGB at the end of the mapped frame buffer).
- First cursor build: pointer squashed to half height with noise below —
  DCE 8+ cursor surface pitch is 128 pixels, not 64. Fixed in 0011.
- Next boot: KDL (not captured; the previous_syslog ends in the app_server
  shutdown / `radeon_hd_uninit` sequence). After the hard reset the freshly
  installed accelerant was corrupt ("Bad data" from load_add_on), so
  app_server silently fell back to the **stock** accelerant → black screen.
  `tools/haiku-install.sh` now syncs; `tools/addon_check.cpp` verifies an
  accelerant loads. Check `listimage <app_server team> | grep accelerant`
  to see which accelerant is in use.
- Clean reinstall + reboot: our accelerant loaded
  (`/boot/home/config/non-packaged/.../radeon_hd.accelerant`), desktop
  2560x1440@75, **hardware cursor looks and works normally** (app_server
  uses `set_cursor_bitmap`, 33x33; `CUR_CONTROL` = 0x04000211). No
  AtomBIOS timeouts this boot.

## 2026-09-29 — open issues at end of day

- app_server crash in thread "cursor loop": `radeon_set_cursor_bitmap +0x1d0`
  (store to `gInfo->cursorHotX`) when switching a USB KVM back to the
  machine. No accelerant uninit was logged. Diagnostics build (thread id +
  gInfo traces, re-check before the store) installed; not reproduced yet.
  After a USB switch app_server sets the cursor bitmap repeatedly from two
  threads (event loop + a window thread).
- Tracker deadlock (desktop icons/right-click gone, Deskbar fine): Tracker
  threads blocked on libbe's BLooperList lock — Tracker-internal, report in
  `captures/haiku/`. Restarting Tracker recovers.
- Pending verification on next boot: HPD pin lookup fix (DP connector
  skipped, no AUX errors), shutdown fix (no crash on reboot).
- Local, not yet in `patches/`: shutdown fix, HPD pin lookup fix, cursor
  diagnostics (dev branch `radeon_hd-polaris`).

## 2026-09-30 — HPD lookup and boot glitch

- The DP connector was never skipped: `connector_pick_atom_hpdid` compared
  the GPIO table's byte offset (`usGpioPin_AIndex * 4`) with the DCE 8+
  register index, so no HPD pin ever matched. The VBIOS GPIO_Pin_LUT lists
  all HPD pins at 0x488d (shift 0/8/16/24/26/28). Fixed by comparing
  `targetReg * 4` on DCE 8+. HPD1–5 enabled by the VBIOS, only HPD3 (HDMI)
  senses a display.
- Result: `dp_setup_connectors: connector(0): nothing attached`, no AUX
  errors, no pause at boot. Encoder/transmitter tables now get the real
  HPD ID; HDMI 2560x1440 unaffected.
- Boot glitch (old splash laid out at the new pitch for ~0.5 s): the
  visible frame buffer is now cleared before the display is enabled;
  the screen blanks instead.
- Cursor crash (PC 0x1 in "cursor loop") after a KVM switch: followed an
  in-place `cp` install over the loaded accelerant/driver. The install
  script now renames new files into place. Watch whether the crash recurs.

## 2026-09-30 — Phase 3 step 0: GPU stack builds

- Installed on the Haiku machine: meson, ninja, vulkan(_devel), glslang(_devel),
  mako/pyyaml for python3.10 (HaikuPorts has them only for 3.10; Mesa will
  be configured with python3.10), zstd/expat/zlib devel.
- `tools/build-gpu-stack.sh` builds accelerant2 (pinned to 61baaa6),
  mesa-drm, libdrm2 and RadeonGfx (fork, branch `polaris`) into `~/gpu/install`.
- Fixes needed for current Haiku (patches/gpu-stack/):
  - Locks: user mutex syscalls changed (`_kern_mutex_unlock` →
    `_kern_mutex_unblock`, `switch_lock`/`sem_release` take flags).
  - Locks, ThreadLink, SADomains, RadeonGfx: `DoublyLinkedList.h` moved to
    `private/util`; `MoveFrom` is now `TakeFrom`.
  - libdrm2: `QueryInterface` gained a version argument; `ALIGN` clashes
    with `<sys/param.h>`.
- Not built: VideoStreams and RadeonGfx's display code (need
  `_kern_dup_foreign`, a syscall from X512's own Haiku tree), and
  RadeonGfx's kernel module (the server will attach to radeon_hd).
  Buffer/syncobj fd export returns B_NOT_SUPPORTED for now.
- The server is not run yet: it still contains Southern Islands init code.

## 2026-10-01 — Phase 3 step 1: render device and read-only probe

- radeon_hd patch "publish a render device": `/dev/graphics/` now has
  `radeon_hd_010000`, `radeon_hd_render_010000` and `vesa`. app_server
  still uses radeon_hd.accelerant (skips the render node); desktop normal.
- `RadeonGfx info` (fork) maps the registers read-only via
  RADEON_GET_GPU_INFO:
  - 1002:67ef rev 0xcf, DCE 11.2, 4096 MB GDDR5 (MC_SEQ_MISC0 type 5),
    256 MB visible at 0xe0000000, MMIO 256 KB, AtomBIOS 128 KB.
  - All engines idle and halted after the VBIOS: CP ME/PFP/CE and MEC
    halted, RLC off, both SDMA halted, IH ring off, VM L2 and context 0 off.
  - GB_ADDR_CONFIG 0x22011002 (= Linux POLARIS11_GB_ADDR_CONFIG_GOLDEN):
    4 pipes, 2 shader engines, 256 B interleave, 4 KB rows.
  - SE0/SH0: 7 active CUs (inactive mask 0xff80); no render backends
    disabled. Per-SE values need GRBM_GFX_INDEX writes (later).
  - VRAM at MC 0xF4_0000_0000–0xF4_FFFF_FFFF, FB offset 0, AGP and system
    aperture unset.
- Fixed before the run: ~RadeonDevice() touched registers even when not
  initialized (would have written GFX6 registers); the server and test
  modes now refuse to run without `--si`.

## 2026-10-01 — Phase 3 step 2: memory

- Patch 0012 now maps the frame buffer area with B_CLONEABLE_AREA so the
  GPU server can clone it (the accelerant uses the kernel address).
- `RadeonGfx memtest` (registers read-only):
  - VRAM 0xF4_0000_0000–0xF4_FFFF_FFFF (4096 MB), CPU visible first 256 MB.
  - Reserved for radeon_hd: 0xF4_0000_0000 +64 MB (screen) and the last
    1 MB of visible VRAM (cursor). Check against the hardware: CRTC 0
    scans out at 0xF4_0000_0000, cursor at 0xF4_0FFF_8000 — both inside.
    CRTCs 2–4 have GRPH_ENABLE set with address 0 but aren't running
    (CRTC_MASTER_EN clear); the check now skips them.
  - 4 KB / 1 MB / 16 MB visible buffers, 64 KB aligned, CPU write/read OK;
    1 GB invisible buffer at 0xF4_1000_0000; usage back to baseline after
    freeing (66568 KB visible = reservations + dummy and scratch page).
  - 4 MB of locked system memory: 593 physical runs, all below 4 GB, so the
    GART needs per-page entries.

## 2026-10-01 — Phase 3 step 3a: GART (first register writes)

- `RadeonGfx garttest` (fork): follows Linux gmc_v8_0_mc_program() and
  gart_enable() for VM context 0, keeping the VBIOS FB_LOCATION.
  - System aperture 0xF4_0000_0000–0xF4_FFFF_FFFF (was 0–0), AGP off,
    MC_VM_MX_L1_TLB_CNTL 0x503 → 0x55b (system access mode 3), L2 cache
    on with 64 KB fragments (VM_L2_CNTL 0x0c0b8602 → 0x0c0b8e03).
  - GART 512 MB at 0xFF_0000_0000, page table in VRAM at 0xF4_0400_2000;
    TLB invalidate acknowledged (VM_INVALIDATE_RESPONSE 1).
  - 256 KB of system memory mapped: 64 PTEs, all correct
    (e.g. 0xFF_0000_0000 → 0x32F61000, PTE 0x32f61067); no protection
    faults, VM_L2_STATUS 0.
  - Held 5 s: display unchanged. All saved registers restored to the VBIOS
    values afterwards.
- Translation itself is only exercised once an engine (SDMA, step 4) uses it.

## 2026-10-01 — Phase 3 step 3b: interrupt (IH) ring

- `RadeonGfx ihtest` (fork): GART on, then the IH ring (Linux
  tonga_ih_irq_init()), CPU interrupt left off (no handler in radeon_hd),
  ring polled. Source: D1 vblank (LB_INTERRUPT_MASK), acknowledged per
  vector through LB_VBLANK_STATUS.
- First two attempts: IH_RB_WPTR advanced to 0x10 but nothing reached the
  ring, with the ring in VRAM and then in GART mapped memory. The IH
  doesn't go through the GPU VM; like Linux (use_bus_addr) it needs bus
  addresses. Those two attempts sent one 16 byte vector and a write pointer
  to bus addresses 0xF4_0401_0000 / 0xFF_0000_0000, where there is no RAM.
- With a contiguous system memory ring at bus 0xb5c000: 150 vblank vectors
  in 2.00 s = 75.0 per second (the 2560x1440@75 mode), source 1 data 0,
  ring/write pointer writeback consistent (0x960). All registers restored.
- PCI command register 0x0007 (bus master already on); tools/pci_config.cpp
  reads/writes PCI config through the poke driver.

## 2026-10-01 — Phase 3 step 4, first attempt: direct SDMA firmware load

- `RadeonGfx sdmatest`: SDMA0 set up as in Linux sdma_v3_0_gfx_resume()
  with the firmware written through SDMA0_UCODE_ADDR/DATA (Linux 4.7's
  direct load). Engine unhalted (F32_CNTL 0), ring not empty, but RB_RPTR
  stayed 0; SDMA0_STATUS_REG 0x46dc7042 (RB_MC_RREQ_IDLE clear). Halted and
  restored after the 1 s timeout; display unaffected.
- Writes to SDMA0_UCODE_DATA are ignored on this card: UCODE_ADDR doesn't
  advance and the data doesn't change (reads back 0xbbbb2fbd...). Direct
  loading is locked; Linux never used it for Polaris (powerplay/SMU loads
  the SDMA, CP and RLC firmware).
- SMU state after the VBIOS (SMC indirect registers via 0x1ac/0x1ad):
  SMC_PC_C 0x2ac8 (< 0x20100: boot ROM, no SMC firmware running),
  SMC_SYSCON_CLOCK_CNTL_0 0x01000000 (clock on), SMU_FIRMWARE 0x00030006
  (protection mode), SMU_STATUS 0.
- Next: start the SMC firmware (polaris11_smc.bin, protection mode) and
  have the SMU load the SDMA firmware (Linux smu7_smumgr/polaris10_smumgr).

## 2026-10-01 — Phase 3 step 4: SMU firmware and first GPU work (SDMA)

- `RadeonGfx sdmatest` (fork) with PolarisSmu (Linux
  polaris10_start_smu_in_protection_mode() / smu7_request_smu_load_fw()):
  - SMC firmware polaris11_smc.bin 0x1d1f00 (129940 bytes), protection mode,
    hard key: accepted (SMU_STATUS 0x3 = done + pass), PC 0x2994 → 0x20498.
  - SDMA0/SDMA1 firmware (0x3a) loaded by the SMU from VRAM through a TOC:
    soft registers at 0x3fa14, UcodeLoadStatus 0x6.
  - SDMA0 ring in VRAM, VMID 0:
    1. WRITE_LINEAR to VRAM: 0xdeadbeef OK
    2. 1 MB CONST_FILL + fence OK
    3. 1 MB copy VRAM → VRAM OK
    4. 1 MB copy VRAM → system memory through the GART (0xFF_0000_0000) OK
    5. 1 MB copy system memory → VRAM OK
    6. fence + trap: IH vector source 224 (SDMA trap) OK
    7. 63 MB fill: 11.3 ms, 5.8 GB/s (boot clocks)
  - SDMA halted, registers, GART and IH restored afterwards; the SMC
    firmware keeps running until reboot. Temperature 22 °C before and after;
    screen and fan unchanged (observed).

## 2026-10-01 — Phase 3 step 5, first attempt: SMU hangs loading the MEC

- `RadeonGfx gfxtest`: second LoadUcodes in the same boot (after the SDMA
  test) with RLC, CE, PFP, ME, MEC. No response to LoadUcodes; afterwards
  SMC_RESP_0 0, PC stuck at 0x33588, UcodeLoadStatus 0x38 (CE/PFP/ME
  loaded, MEC and RLC not). CP, MEC, RLC and SDMA still halted; GART/IH
  restored; display unaffected. The SMU stays hung until reboot.
- Differences to Linux: Linux loads everything in one LoadUcodes and its
  TOC also lists MEC_JT1/JT2 (the MEC jump table, copied page aligned after
  the MEC code by amdgpu_ucode_patch_jt()), though not in the load mask.
  The loader now does exactly that; retry after a reboot.

## 2026-10-01 — Phase 3 step 5a: graphics command processor runs

- After a reboot, `RadeonGfx gfxtest` with the Linux-style firmware load:
  SMC firmware started, one LoadUcodes with RLC, CE, PFP, ME, MEC (+ JT1/JT2
  in the TOC), SDMA0, SDMA1 (mask 0x47e): UcodeLoadStatus 0x5fe (the SMU
  also marks the jump tables), no hang.
- GFX ring 0 (golden registers, RLC stop/reset/start, cp_gfx_resume,
  cp_gfx_start):
  0. clear state preamble processed
  1. SET_UCONFIG_REG SCRATCH_REG0 = 0xdeadbeef (Linux's ring test)
  2. WRITE_DATA to VRAM and to system memory through the GART
  3. EVENT_WRITE_EOP fence + interrupt: IH source 181 (CP end of pipe)
- CP, MEC and RLC halted and registers restored afterwards; 22 °C.

## 2026-10-01 — Phase 3 step 5b, attempts: repeated GFX runs

- A second gfxtest in the same boot stalled: the CP consumed the ring but
  GRBM_STATUS stayed busy (CP_BUSY, CPF_BUSY), later without EOP
  interrupts. CP_CPF_STATUS showed INTERRUPT_BUSY: restoring the VBIOS
  CP_INT_CNTL_RING0 (0x003c0000, GUI busy/idle interrupts) after a test,
  with the IH ring off, left a CP interrupt pending, surviving GRBM/SRBM
  soft resets. Fix: CP interrupts off at start and at the end.
- A firmware reload through the SMU in an already running SMU works
  (with the MEC jump table entries).
- A GFX soft reset (gfx_v8_0_soft_reset(): GMCON stall, GRBM
  RLC/GFX/CP/CPF/CPC/CPG, SRBM GRBM/SEM) during Init right after a fresh
  firmware load hung the machine twice (hard reset needed; the syslog of
  that boot lost its last minutes). Removed; Linux only resets to recover.
- Bring-up tests now write unbuffered output, captured on the
  development machine, so a hang still shows the last step.

## 2026-10-01 — Phase 3 step 5b: first shader dispatch hangs in instruction fetch

- Fresh boot, without the soft reset: tests 0–3 pass again (EOP interrupt
  arrives), no machine hang.
- Test 4 (gfx803 buffer_store shader in GTT, 16 x 64 threads,
  DISPATCH_DIRECT on the gfx ring): no output, no fence. GRBM_STATUS
  SPI_BUSY, CP_STALLED_STAT2 ME_WAITING_ON_PARTIAL_FLUSH.
- Wave state through SQ_IND_INDEX/DATA: 32 valid waves, all at their first
  instruction (PC 0xFF_0000_1000 = the shader in GTT), INST_DW0 0xff336698
  instead of the shader's 0x8e058604: the instruction fetch from system
  memory through the GART doesn't complete. No VM protection fault logged.
- Next (after a reboot to clear the waves): empty shader in VRAM, then the
  store shader in VRAM writing VRAM, then writing GTT, with a wave dump.

## 2026-10-01 — Phase 3 step 5b: first compute dispatches work (waves were in VMID 9)

- Staged test (RadeonGfx 9a595b6): 4a empty shader in VRAM OK, 4b store
  shader in VRAM writing VRAM OK (1024 values), 4c writing system memory
  through the GART: dispatch and fence complete, but the output stays 0.
  No VM fault in context 0, PTE correct (valid, system, snooped, R/W).
- Not a CPU cache problem: clflush of the output before the dispatch and
  before reading changes nothing; the GPU (CP COPY_DATA, from memory and
  through the TC L2) also reads 0 at the output.
- The CP through the TC L2 reads and writes system memory through the GART
  correctly (COPY_DATA src_sel 2, WRITE_DATA dst_sel 2): the TC L2 to GART
  path works, the shader's requests were the problem.
- `gfxtest --all-vm-contexts` (GART also mapped by VM contexts 1-15): now
  4b failed too; VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x13004001 = range
  fault, write, client TC ("TC3"), **VMID 9**, at the 4b output in VRAM.
  The waves ran in VMID 9 (stale COMPUTE_VMID; the register reads back as
  junk): VRAM worked only because disabled contexts pass addresses through,
  GART addresses went nowhere, and the earlier hang fetched garbage
  instructions the same way. SH_MEM_CONFIG was also never set for VMID 9.
- Fix: SET_SH_REG COMPUTE_VMID = 0 with every dispatch (RadeonGfx
  c12732e). 4a, 4b and 4c all pass, with and without --all-vm-contexts,
  several runs in the same boot; no hang, 22 °C.
- Next: step 6, DRM ioctl emulation for VI so that libdrm2/RADV can run.

## 2026-10-02 — Phase 3 step 6: RADV builds, Polaris server mode written

- Mesa 23.3.6 (newest release accepting libdrm_amdgpu 2.4.110), RADV only,
  ACO, no LLVM: `tools/build-mesa.sh`, fixes in `patches/gpu-stack/`
  (mesa: `major()`/`minor()` on Haiku, link libdrm2 for vk_drm_syncobj;
  libdrm: `_IOWR` fallback in libsync.h; libdrm2: project version 2.4.110,
  `amdgpu_device_get_fd()`). Builds on the i5-2320 in about 25 minutes.
- `tools/vktest/vkinfo.c`: small vulkaninfo (Vulkan-Tools isn't packaged).
- RadeonGfx `server` on Polaris (RadeonGfx c143699 and later): GART + VM
  contexts 1-15 (two level, 512 entry tables, fault interrupts), VRAM PTEs
  relative to the start of VRAM (Linux amdgpu_gmc_vram_mc2pa()), polled IH
  dispatcher (250 µs), firmware through the SMU, GFX v8 ring as a server
  unit (IB with VMID, VM flush, EOP fences), Polaris 11 tiling tables (the
  VBIOS leaves GB_TILE_MODE uninitialized), SH_MEM for all VMIDs, DRM info
  replies for VI (PCIE_EFUSE4 rev 1, external 0x5b).
- Not run on the GPU yet.

## 2026-10-02 — Phase 3 step 6: RADV sees the RX 560 (vkinfo)

- `RadeonGfx server` on Polaris starts and stops cleanly (CP/RLC halted,
  all registers restored), several runs. 14 CUs (SE0/SE1 0x7f), RB mask
  0xf, PCIE_EFUSE4 revision 1 (external 0x5b).
- vkinfo through RADV: "AMD Radeon RX 460 Graphics (RADV POLARIS11)"
  (libdrm's amdgpu.ids names 67EF:CF so), discrete GPU, Vulkan 1.3.267,
  timestamp period 40 ns, heaps 3840 MB VRAM + 256 MB visible VRAM + 512 MB
  GTT, one graphics/compute queue family. vkCreateDevice and vkDestroyDevice
  succeed; no command submission yet (server trace: INFO, GEM_CREATE,
  GEM_VA, GEM_CLOSE).
- Fixes on the way:
  - libdrm2 listed the radeon_hd display device: drmGetDevices2() now only
    returns devices with the radeon_gfx.accelerant signature.
  - RADV couldn't map visible VRAM (meta shaders failed, NULL shader
    crash): the server's frame buffer clone wasn't B_CLONEABLE_AREA.
  - Kernel panic (VMCache.cpp:1363, ASSERT UNREACHABLE) in
    amdgpu_bo_cpu_unmap(): munmap() of part of the cloned 256 MB VRAM area;
    Haiku can't split a device memory area. libdrm2 now deletes the clone
    (one clone per CPU map).
  - RADV asserted in vkDestroyDevice: radv_sqtt_finish() locks mutexes
    radv_sqtt_init() never initialized (zeroed memory is no valid mutex on
    Haiku); skipped without a thread trace buffer.
- After the panic the linker failed with "No space left on device" for
  libvulkan_radeon.so although 665 GB were free; `checkfs -c /boot` found
  no errors (4851 blocks could be freed) and the link worked afterwards.
- Next: first command submission through Vulkan (vkCmdFillBuffer, then a
  compute dispatch), read back.

## 2026-10-02 — Phase 3 step 6: first GPU work through Vulkan (vkfill)

- `tools/vktest/vkfill`: vkCmdFillBuffer on the graphics queue, fence wait,
  CPU check. All three pass, twice (server restarted in between):
  1. 4 KB in system memory (CP DMA)
  2. 1 MB in system memory (RADV compute shader)
  3. 1 MB in CPU visible VRAM (compute shader)
- The way there:
  - libdrm2 handled drmSyncobj*() (Mesa's vk_drm_syncobj) in a local stub
    with made-up handles; the server rejected the CS ("signal syncobj 2
    unknown"). drmIoctl() now sends sync object, GEM close and PRIME
    ioctls to the accelerant of the fd.
  - A rejected CS crashed the server (~CommandSubmission freed IB
    addresses that were never remapped): fixed, plus messages for every
    rejection and a chunk trace (RADEONGFX_TRACE).
  - A crashed server left the GPU writing into freed memory (machine hang
    after the core dump): crash signals now halt the CPs and the IH ring
    first. The SignaledFence singleton crashed at exit (destroyed while
    referenced): never destroyed now.
  - First accepted CS: ring fetched, nothing executed (PFP/CE "waiting on
    buffer data"). Server ring self test at start showed the cause: the GFX
    ring in system memory with CP_RB0_CNTL.MTYPE = UC never returns data;
    without MTYPE the data flowed but the CE/DE got stuck and nothing
    executed. A stuck CP survives halt/restart (gfxtest failed afterwards),
    only a reboot clears it. The server's GFX ring now lives in VRAM like
    in the bring-up tests (MTYPE UC, HDP flush before WPTR); RADV's IBs in
    system memory fetch fine.
  - VM page directory entries hold MC addresses, page table entries for
    VRAM the offset in VRAM (as Linux).
- Next: an off-screen render (triangle) read back to a PNG.

## 2026-10-02 — Phase 3 step 6 done: first rendered triangle

- `tools/vktest/vktri`: render pass into a 256x256 RGBA8 image (optimal
  tiling, VRAM), vertex colors from gl_VertexIndex, vkCmdCopyImageToBuffer
  into system memory, PNG written on the CPU. One submission (24 + 576
  dwords), fence signaled; pixels as expected (clear color 25/25/51, red top
  vertex, interpolated middle). See `docs/images/first-triangle.png`.
- The full path works: RADV (ACO shaders) -> libdrm2 -> radeon_gfx
  accelerant -> RadeonGfx server -> GFX ring (VMID 1) on the RX 560.
- Next: step 7, presenting on screen (Vulkan WSI on Haiku).

## 2026-10-04 — Phase 3 step 7: Vulkan on screen (Wayland WSI)

- Presenting through Haiku's packaged Wayland stack: `wayland`,
  `wayland_devel`, `wayland_protocols`, `wayland_server` (the in-process
  Wayland server shows a client's surfaces as native Haiku windows; it has
  wl_shm, no dmabuf).
- Mesa rebuilt with `-Dplatforms=wayland`; patches (mesa.patch):
  - the Wayland WSI uses its wl_shm path on Haiku for hardware drivers too:
    render into a device image, the WSI blits it on the GPU into host
    memory, the CPU copies that into the wl_shm buffer (SHM_MEMCPY; the
    GPU_SHM import path would need userptr buffers)
  - `major()`/`minor()` for wsi_common_wayland.c
  - os_create_anonymous_file(): /tmp when XDG_RUNTIME_DIR is unset (the
    wl_shm pool is an mmap()ed file)
- `tools/vktest/vkwl`: xdg-shell window, VK_KHR_wayland_surface, FIFO
  swapchain (5 images, B8G8R8A8_UNORM), spinning triangle (push constant).
  640x480 and 1600x1000: about 75 fps (the display's refresh rate, FIFO),
  no errors; screenshot `docs/images/first-window.png`.
- Harmless: `DMA_BUF_IOCTL_EXPORT_SYNC_FILE` (0xc0086202) on the swapchain's
  memory fd isn't implemented (WSI falls back); libdrm2's stub no longer
  waits on stdin for unknown ioctls but prints them.
- The window has no title bar (no xdg-decoration); a client that doesn't
  draw its own decorations gets a bare window.
- Glitches (frames jumping between triangle positions): Mesa only waits
  for the WSI blit before presenting with software drivers; the wl_shm
  memcpy ran before the GPU's copy into host memory had finished, showing
  older frames. Patched wsi_common.c to wait on Haiku; confirmed on screen,
  still 75 fps.
- `tools/vktest/vkrun.sh`: runs a Vulkan program from a Terminal (starts and
  stops the server, sets VK_DRIVER_FILES and LIBRARY_PATH).

## 2026-10-04 — Phase 3 step 8 started: clocks

- `RadeonGfx clocks` (read-only apart from starting the SMC firmware):
  the VBIOS PowerPlay table (ATOM_Tonga_POWERPLAYTABLE format 7.1, Polaris
  SCLK records) and the current clocks from the SMC
  (PPSMC_MSG_API_GetSclkFrequency/GetMclkFrequency):
  - engine clock levels 214, 481, 760, 1020, 1102, 1138, 1172, 1200 MHz;
    voltages 800 mV for the lowest, the others are virtual voltage IDs
    (0xff02-0xff08) to be resolved through the VBIOS (EVV/leakage)
  - memory clock levels 300 and 1500 MHz (vddci 900/1000 mV, mvdd 1000 mV)
  - overdrive limits 1800/2000 MHz, power limit 75 W
  - now: engine 135 MHz, memory 150 MHz as reported by the SMC (DPM not
    running; the GPU is at its boot clocks)
- pptable_v1_0.h from Linux: `#pragma pack(push, 1)` must come after
  atombios.h, which resets the packing at its end.
- Virtual voltages resolved (read-only): ATOM GetVoltageInfo in EVV mode
  (`GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_3`, result in 0.01 mV) for each ID
  at the lowest engine clock that uses it, +50 MHz for levels without clock
  stretching (clock stretch amount 2 in the PowerTune table), as Linux
  smu7_get_evv_voltages does:
  - engine clock 214/481/760/1020/1102/1138/1172/1200 MHz:
    800/821/825/875/956/1000/1043/1081 mV
  - memory clock 300/1500 MHz: vddc 800/850 mV
  - `Atombios::Init(area_id)` runs the VBIOS interpreter on the Polaris
    VBIOS copy (falls back to a read-only clone).
- DPM table (polaris/PolarisDpm.cpp, a port of Linux polaris10_init_smc_table
  without AVFS, clock stretching, BAPM, deep sleep, ULV and PCIe DPM), built
  read-only by `RadeonGfx clocks`:
  - boot state from the VBIOS firmware info: engine 214 MHz, memory 300 MHz,
    vddc 800 mV, vddci 875 mV, mvdd 1500 mV; link PCIe gen 2 x8
  - VDDC on SVI2, VDDCI and MVDD static (no VDDCI/MVDD control caps);
    GDDR5, 128 bit; reference clock 25 MHz
  - engine clock dividers from ATOM ComputeMemoryEnginePLL (v1.7, 100 MHz
    PLL reference: 1200 MHz = fcw 48 / 4), SCLK range table from SMU_Info
  - SMC firmware 0x1d1f00: DPM table at 0x3f294, soft registers at
    0x3fa14, MC arbiter table at 0x3f114
  - memory clock DPM only switches to the highest level (no vblank length
    check yet)
- `RadeonGfx clocks start` (watched): uploads the table (MC arbiter
  timings through ATOM DynamicMemorySettings after switching the arbiter
  from F0 to F1; MC_ARB_CG.CG_ARB_REQ reads 0 after boot, meaning F0) and
  enables engine clock DPM with SVI2 voltage control and thermal
  throttling: "DPM running", idle at level 0 (214 MHz engine, 300 MHz
  memory, 22 C), no flicker, the machine keeps running.
- Load test (watched): `vkrun.sh ./vkwl 1000 1600 1000` with
  `RadeonGfx clocks watch` alongside: the engine clock goes to 1200 MHz
  (the top level, 1081 mV) for the whole run, 26-29 C, and steps back
  down (1133, 292, 611) to 214 MHz within 2 s after the window closes.
  Still 74.9 fps (FIFO at the display's refresh rate). Memory stays at
  300 MHz without memory clock DPM.
- `RadeonGfx clocks memory` (watched): memory clock DPM on top of the
  running engine clock DPM, enabled mask = the highest level only: the
  memory clock switches from 300 to 1500 MHz within half a second and
  stays there; engine clock back at 214 MHz idle, 24 C. No flicker on
  screen during the switch.
- The server starts DPM itself now (`PolarisStartPowerManagement()` after
  the units are up: engine clock DPM, then the memory clock to its highest
  level; `RADEONGFX_DPM=0` keeps the boot clocks). It prints "DPM: already
  running" when `clocks start` already did it.
- `tools/vktest/vkbench` (VRAM fill and copy, 20 x 64 MB per submission,
  best of 3), engine 214-1200 MHz, memory 1500 MHz: fill 16.0 GB/s,
  copy 9.6 GB/s (19.2 GB/s of memory traffic). Far below the card's
  96 GB/s; to look into later (RADV's compute fill/copy path, barriers
  between the 20 operations).
- After a reboot, `vkbench` (watched):
  - `RADEONGFX_DPM=0` (boot clocks): fill 9.3 GB/s, copy 5.9 GB/s
  - server starting DPM itself from a fresh boot ("DPM: engine clock
    214-1200 MHz, memory clock 1500 MHz"): fill 25.9 GB/s, copy 13.5 GB/s,
    2.8x / 2.3x the boot clocks. Faster than the 16.0 / 9.6 GB/s with DPM
    started by `clocks start` before the server; unclear why (DPM after
    the GFX setup as in Linux, or the GPU state after many runs).

## 2026-10-04 — Phase 3 step 7: OpenGL ES through Zink

- Mesa 23.3.6 with `-Dgallium-drivers=zink -Degl=enabled -Dgles2=enabled
  -Dplatforms=wayland`. mesa.patch additions:
  - meson: DRI drivers and egl_dri2 on Haiku with the Wayland platform
    (`with_haiku_dri`, as GNU/Hurd builds egl_dri2 without DRM/KMS)
  - no hard links on BFS: the DRI megadriver is copied (build and install)
  - `major()`/`minor()` for zink_screen.c
  - EGL Wayland: Zink's kopper surface gets the EGL swap interval
    (otherwise interval 0: no vsync)
  - util_queue_finish() on a never initialized queue returns: Zink's exit
    hit a zero-filled pthread mutex in the disk cache queue, which Haiku's
    libroot asserts on (`mutex->owner == -1`)
- libdrm2: drmGetDeviceFromDevId(), drmGetDeviceNameFromFd2(),
  drmGetMagic() stubs (Mesa's loader, EGL wayland-drm).
- EGL on Wayland with `MESA_LOADER_DRIVER_OVERRIDE=zink` uses the swrast
  (wl_shm) path with kopper: Zink presents through RADV's Wayland WSI.
- `tools/vktest/glwl` (EGL + GLES2 spinning triangle; `vkrun.sh` sets the
  Zink variables): `GL_RENDERER: zink Vulkan 1.3(AMD Radeon RX 460 Graphics
  (RADV POLARIS11))`, `GL_VERSION: OpenGL ES 3.2 Mesa 23.3.6`;
  600 frames, 69 fps average (vsync, 75 Hz), clean exit.
- Server bugs found on the way:
  - RadeonServerDrm kept one buffer per CS chunk type: the first IB chunk's
    data was freed when a second IB chunk came (RADV sends a preamble IB
    and the main IB); garbage IB at address 0 after ~280 submissions.
    Now one buffer per chunk.
  - a CS with an unmapped IB aborted the server; now it is rejected
    (EINVAL to the client) before getting a sequence number
  - deadlock under FIFO presenting: Locks' Mutex::Acquire() set
    B_USER_MUTEX_LOCKED with atomic_or even while a hand-off was pending;
    current Haiku's _kern_mutex_unblock() hands the mutex over by setting
    that bit and wakes nobody if it is already set: the domain lock stayed
    locked by no one (fValue 1, three threads waiting). Acquire() now uses
    atomic_test_and_set like libroot (Locks.patch).
- vkwl (74.9 fps) and vkbench (26.0 / 13.5 GB/s) unchanged after the
  fixes.

## 2026-10-04 — glmark2 on Zink

- glmark2 (git 22c527c) built for wayland-glesv2 and wayland-gl
  (`tools/build-glmark2.sh`, glmark2.patch: evdev key codes without
  linux/input.h, no RTLD_NODELETE, desktop GL entry points from
  libGLESv2/eglGetProcAddress instead of Haiku's own libGL, a pause before
  wl_display_disconnect() because Haiku's in-process Wayland server quits a
  surface's BWindow asynchronously and the window thread crashed when the
  library was unloaded).
- `glmark2-es2-wayland --size 800x600` (vsync): all 32 scenes, score 74
  (every scene at 70-76 fps, the display's refresh rate).
- Server bugs found:
  - use after free in the interrupt thread: a FenceGroup could lose its last
    reference in another thread while Fence::Signal() ran its handler
    outside the fence's lock (crash in FenceGroup::GroupHandler::Do).
    Handlers can now keep their object alive (Handler::Retain()).
  - `--off-screen` (unthrottled, about 2700-2800 fps in the first scenes):
    hang after the bump scenes. The client waited on a syncobj; the GFX
    ring was empty (rptr == wptr), no VM fault. Most likely a lost end of
    pipe interrupt: the interrupt thread now checks the fences every 10 ms
    without interrupts and reports passed fences (untested yet).
  - the hang left the CP unusable for the next server (ring self test
    failed) because the server ran under gdb and was killed without the
    emergency stop: reboot needed.
- `tools/vktest/gdbserver.sh`: starts the server under gdb in the
  background (backtraces in ~/server-gdb.log).

## 2026-10-05 — glmark2 off-screen hangs

- GTT allocations failed with 125 of 512 MB used: ExternalAllocator's
  AllocAligned() only looked at free blocks of size + alignment - 1 (RADV
  asks for 8 MB alignment). It now takes any free block an aligned block
  fits in; failures print the pool state.
- The server now soft resets the graphics engine when it finds it busy at
  startup (gfx_v8_0_check_soft_reset/soft_reset): a hung GPU no longer
  needs a reboot (worked several times).
- Ring: Begin() now reserves room for End()'s padding (a nearly full ring
  could be overwritten by up to 3 dwords).
- `glmark2 --off-screen -b bump:bump-render=height` (about 2700 fps) hung
  the GPU intermittently (CP waiting on an end of pipe event, TC busy):
  - engine clock fixed at 1200 MHz (`clocks sclk-mask=0x80`): 17 of 17 runs
    fine; only 214 and 1200 MHz (0x81): 10 of 10 fine; all levels: 2 of
    10 hung
  - so switching through the middle levels: their EVV voltages assume the
    clock stretcher, which isn't enabled. The voltages are now resolved at
    sclk + 50 MHz for every level above 0 (as Linux does for levels without
    clock stretching): 214/481/760/1020/1102/1138/1172/1200 MHz at
    800/821/825/925/1012/1062/1112/1100 mV. Loaded into the running DPM
    (`clocks reload`, new): all levels 10 of 10 fine.
- The full off-screen run then hung in the texture scene with a strange
  ring state (CP_RB0_WPTR 1, rptr 0x6d8); not understood yet. After that
  server was stopped, the next server found the SMC firmware no longer
  running (temperature reading 298 C): reboot needed. Unclear whether the
  live voltage reload, the mask changes or the hang caused it.
- `tools/vktest/gmcase.sh`: one glmark2 case with a 20 s limit, restarts
  the server after a hang.

## 2026-10-05 — glmark2 complete on Zink

- After a reboot with the new voltages from the start:
  - Zink assertion `fence->batch_id` in fence_wait() (buffer scene,
    eglClientWaitSync): the batch state was already reset for reuse, which
    only happens after it finished; Mesa's debugoptimized build keeps
    assertions. fence_wait() now returns "done" for a reset batch
    (mesa.patch).
  - GTT and visible VRAM ran out at 64 / 96 MB used: RADV allocates every
    buffer with its max_alignment, 16 MB here (addrlib's largest tile
    alignment). The server now aligns GTT allocations to pages and VRAM to
    at most 64 KB (the alignment only matters for the GPU virtual address,
    which the client picks).
  - "No space left on device" linking Mesa once (720 GB free); a retry
    linked fine (seen before, transient).
- `glmark2-es2-wayland --off-screen`: all 32 scenes, **score 2206**
  (OpenGL ES 3.2): build 2184-2756, texture 2599-2711, shading ~2780,
  bump 2534-2763, effect2d ~3060, pulsar 2331, desktop 1347/1486, buffer
  128-205, ideas 966, jellyfish 2269, terrain 367, shadow 2131, refract
  404, conditionals/function/loop ~2860-2900 fps.
- `glmark2-wayland --off-screen` (desktop OpenGL): `GL_VERSION: 4.6
  (Compatibility Profile) Mesa 23.3.6`, all scenes, **score 2236**. One
  GL_INVALID_FRAMEBUFFER_OPERATION from glClear at start (glmark2's
  off-screen setup).
- No server errors, no GPU hang in either run.
