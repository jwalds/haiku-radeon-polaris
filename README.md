# haiku-radeon-polaris

Bring-up of AMD Polaris (Radeon RX 560, Polaris 11 / DCE 11.2) support in
Haiku's `radeon_hd` driver.

## Decision

Extend the existing `radeon_hd` driver for display/modesetting. Its mode
setting is AtomBIOS-driven and already handles the table revisions Polaris
uses (SetPixelClock v7, DIGxEncoderControl v5, UNIPHYTransmitterControl v6,
SetDCEClock v2); most CRTC0/MC registers sit at the same offsets as
Evergreen. Acceleration (GFX8 CP/SDMA, firmware, GPUVM, power management) is
a separate, much larger effort and should not be bolted onto the accelerant.

## Phases

- **Phase 0 – baseline:** UEFI framebuffer/VESA works; Linux reference captures
  (`docs/linux-register-dump.md`).
- **Phase 1 – modesetting in radeon_hd**
  - 1a: enable Polaris 11 IDs, fix chip-name table and VRAM-size register ✅ patch 0001
  - 1b: single head on CRTC0 at native resolution ✅ patches 0001–0008 —
    **working: HDMI 2560x1440@60 on RX 560 (1002:67ef), 2026-09-28**
  - 1c: CRTC1–5 / multi-head
  - 1d: DPMS, hardware cursor, brightness, more Polaris IDs
  - Open items: EDID extension blocks not read (common `ddc2_read_edid1`
    out-of-bounds parse; 2560x1440 missing from mode list, patch 0007's HDMI
    detection unreliable), skip DP AUX reads when nothing is plugged in,
    `screenmode -l` causes a brief monitor drop-out, HDMI audio
- **Phase 2 – acceleration** (later): firmware loading, GART/VM, IH, SDMA,
  then Mesa via an amdgpu-style interface (coordinate with X512's RadeonGfx).

## Layout

- `patches/` – `git format-patch` series against upstream Haiku
  (`patches/BASE_COMMIT`).
- `tools/` – helper scripts (Linux capture, Haiku build/install).
- `docs/` – notes, register findings, test logs.
- `captures/` – Linux reference dumps (tarballs).

## Coding style

All driver code follows the existing radeon_hd conventions — see
`docs/coding-style.md`.

## Working layout (minibook `Documents\haiku_gfx`)

```
haiku_gfx/
  haiku-radeon-polaris/   this repo (docs, tools, patches)  -> GitHub
  haiku/                  sparse Haiku checkout, branch radeon_hd-polaris (edit here)
  .keys/                  SSH keys (never committed)
```

Edit in `haiku/`, commit, then `tools/export-patches.sh` to refresh
`patches/`, and `tools/sync-to-haiku.sh` to push both trees to the Haiku box
for building/testing. See `docs/haiku-dev-workflow.md`.
