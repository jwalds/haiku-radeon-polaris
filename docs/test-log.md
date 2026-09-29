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
  box by scanning 192.168.137.0/24 for port 22.

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
