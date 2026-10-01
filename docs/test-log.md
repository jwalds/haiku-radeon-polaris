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
