# Findings from the Linux (amdgpu) captures — 2026-09-27

Card: `1002:67ef` rev `0xcf`, subsystem `1787:3000`, Polaris 11 ("Baffin"),
DCE 11.2, 4 GiB VRAM, 256 MiB BAR0. BAR5 = MMIO (256K), BAR2 = doorbells.
Monitor: HDMI-A-1, native 2560x1440 (DP-1 and DVI-D-1 unused).
Linux lists 5 CRTCs for this chip.

Captures: `captures/polaris-capture-{native,1024x768}-*.tar.gz`.

| Register (dword) | Value | Consequence for radeon_hd |
|---|---|---|
| `CONFIG_MEMSIZE` 0x150a | `0x1000` (4096 MB) | Confirms reading 0x5428 on Tahiti..Polaris (patch 0001). |
| `MC_VM_FB_LOCATION` 0x809 | `0xf4fff400` | VRAM lives at MC address `0xF4_0000_0000`, not 0. Keep it (patch 0002). |
| `GRPH_PRIMARY_SURFACE_ADDRESS_HIGH` 0x1a07 | `0xf4` | High address needs 8 bits; old code masked with `0xf` (patch 0002). |
| `LB_MEMORY_CTRL` 0x1ac1 | `0x00071404` | This is where the old code wrote the desktop height — would corrupt line buffer config. `LB_DESKTOP_HEIGHT` is 0x1ac3 (patch 0001). |
| `GRPH_CONTROL` 0x1a01 | `0x8548700e` | Linux uses a tiled (2D, non-linear) scanout; we scan out linear. |
| CRTC0 block 0x1a00–0x1c00 | populated | Head in use is CRTC0; only CRTC0 changes between modes. |

Registers that change between 2560x1440 and 1024x768 (CRTC0):
`GRPH_PRIMARY_SURFACE_ADDRESS`, `GRPH_PITCH`, `GRPH_X_END/Y_END`,
`VIEWPORT_SIZE`, `CRTC_H_TOTAL`, `CRTC_H_BLANK_START_END`, `CRTC_H_SYNC_A`,
`CRTC_V_TOTAL`, `CRTC_V_BLANK_START_END`, `CRTC_V_SYNC_A`,
`DPG_PIPE_ARBITRATION_CONTROL1`, `FMT_BIT_DEPTH_CONTROL`, `OUT_ROUND_CONTROL`.

Also found: the Avivo registers written by `radeon_gpu_mc_halt/resume`
(`AVIVO_D1CRTC_CONTROL` 0x6080, `AVIVO_D2CRTC_UPDATE_LOCK` 0x68e8,
`AVIVO_D2GRPH_PRIMARY_SURFACE_ADDRESS` 0x6910, ...) land on DCE 11.2 CRTC0
colour-space registers (`INPUT_CSC_C31_C32`, `COMM_MATRIXA_TRANS_C13_C14`).
Patch 0002 skips memory controller reprogramming on VI+.

2560x1440@60 is ~241.5 MHz: fine for HDMI (≤ 340 MHz TMDS) but above
single-link DVI (165 MHz), so the encoder must be driven in HDMI mode.
