# haiku-radeon-polaris

Patches that add AMD Polaris support (Radeon RX 460/560, Polaris 11,
display engine DCE 11.2) to Haiku's `radeon_hd` driver.

**Status:** native mode setting, DPMS and a hardware cursor work — tested
on a Radeon RX 560 (`1002:67ef`, rev `0xcf`) over HDMI at 2560x1440@60 and
@75 on Haiku R1/beta6+ (hrev60147). There is no 2D/3D acceleration yet.

> These patches were developed with AI assistance. Haiku does not accept
> AI-generated contributions (see `AGENTS.md` in the Haiku tree), so they
> are not intended for upstream submission as-is.

## Phases

- **Phase 1 – mode setting in `radeon_hd`**
  - 1a: enable Polaris 11 PCI IDs, fix chip names and VRAM size ✅
  - 1b: single display at native resolution (DCE 11.2 registers, memory
    controller, pixel clock/PLL, HDMI transmitter, BlankCRTC) ✅
  - 1c: multiple displays (CRTC 1–4)
  - 1d: DPMS ✅, hardware cursor ✅, first EDID extension block ✅;
    open: brightness, HDMI audio

Known issues: after a runtime mode change the output falls back from HDMI
to DVI signalling (picture unaffected); the DisplayPort path doesn't read
EDID extension blocks yet.

- **Phase 2 – acceleration:** not planned for the desktop. app_server
  renders in software and doesn't use accelerant 2D hooks, so the desktop
  is left to app_server. See `docs/phase2-plan.md` for the shelved SDMA
  plan. 3D (Mesa) would be a separate project.

See `docs/test-log.md` for test results and `docs/findings-linux-capture.md`
for register findings.

## Patches

| # | Patch |
|---|---|
| 0001 | DCE 10/11 register fixes (chip names, VRAM size, CRTC offsets, LUT) |
| 0002 | keep the VBIOS memory controller setup on Volcanic Islands+ |
| 0003 | SetDCEClock on DCE 11.2, single-link HDMI transmitter |
| 0004 | SetPixelClock v7 units and per-PHY PLLs |
| 0005 | BlankCRTC VGA workaround (no 5 s delay) |
| 0006 | read the first EDID extension block (shared DDC code) |
| 0007 | HDMI encoder mode for HDMI sinks |
| 0008 | hardware cursor (DCE 4+) |
| 0009 | skip empty DisplayPort connectors (HPD) |
| 0010 | enable the Polaris 11 PCI IDs |
| 0011 | optional: debug register dump and traces |

Patch numbers in `docs/test-log.md` refer to the development order and
don't match this list.

## Supported hardware

Polaris 11 PCI IDs enabled by the patches: `67e0 67e1 67e3 67e7 67e8 67e9
67eb 67ef 67ff` (RX 460/560, Radeon Pro WX 4100/4130/4170, ...).
Only `67ef` has been tested. Check yours with `listdev`.

## Build and install (on the Haiku machine)

You need Haiku x86_64 with the development tools (`gcc`, `jam`, `git`).

```sh
cd ~
git clone https://github.com/jwalds/haiku-radeon-polaris.git
git clone https://github.com/haiku/haiku.git

# apply the patches on top of the Haiku revision they were made for
cd ~/haiku
git checkout -b radeon_hd-polaris $(cat ~/haiku-radeon-polaris/patches/BASE_COMMIT)
git am ~/haiku-radeon-polaris/patches/*.patch

# configure a native build and build only the driver and accelerant
./configure
jam -q radeon_hd radeon_hd.accelerant
```

`git am` needs a git identity (`git config --global user.name/user.email`).
If you build over SSH, run the commands in a login shell (`bash -l`),
otherwise the build tools can't find their libraries.

Install the build to the user non-packaged directories (they take priority
over the system `radeon_hd`), then reboot. If the patched accelerant fails
to load, app_server silently falls back to the stock one (black screen on
Polaris); `tools/addon_check.cpp` checks that it loads.

```sh
sh ~/haiku-radeon-polaris/tools/haiku-install.sh --no-reboot
shutdown -r
```

This installs:

- `/boot/home/config/non-packaged/add-ons/kernel/drivers/bin/radeon_hd`
  (+ link in `.../drivers/dev/graphics/`)
- `/boot/home/config/non-packaged/add-ons/accelerants/radeon_hd.accelerant`

Do not use `/boot/system/non-packaged`: the kernel ranks it the same as the
system directory, and the stock `radeon_hd` replaces the patched one.

## Removing it / recovery

```sh
sh ~/haiku-radeon-polaris/tools/haiku-install.sh --remove
shutdown -r
```

If the screen stays black: at boot hold **Shift**, choose *Safe mode
options* → *Disable user add-ons* (skips the patched driver) or *Use
fail-safe video mode*.

## Repository layout

- `patches/` – patch series against Haiku (`patches/BASE_COMMIT`)
- `tools/` – install script, Linux register capture, and debug tools
  (`radeon_regs` register peek/poke, `modeset_test`)
- `docs/` – coding style, findings, test log
- `captures/` – register dumps from Linux (amdgpu) and Haiku

## Coding style

All driver code follows the existing `radeon_hd` conventions, see
`docs/coding-style.md`.
