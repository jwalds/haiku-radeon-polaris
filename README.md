# haiku-radeon-polaris

Patches that add AMD Polaris support (Radeon RX 460/560, Polaris 11,
display engine DCE 11.2) to Haiku's `radeon_hd` driver.

**Status:** native mode setting works — tested on a Radeon RX 560
(`1002:67ef`, rev `0xcf`) over HDMI at 2560x1440@60 on Haiku R1/beta6+
(hrev60147). There is no 2D/3D acceleration yet.

> These patches were developed with AI assistance. Haiku does not accept
> AI-generated contributions (see `AGENTS.md` in the Haiku tree), so they
> are not intended for upstream submission as-is.

## Phases

- **Phase 1 – mode setting in `radeon_hd`**
  - 1a: enable Polaris 11 PCI IDs, fix chip names and VRAM size ✅
  - 1b: single display at native resolution (DCE 11.2 registers, memory
    controller, pixel clock/PLL, HDMI transmitter, BlankCRTC) ✅
  - 1c: multiple displays (CRTC 1–4)
  - 1d: DPMS, hardware cursor, brightness, HDMI audio, full EDID parsing

Known issues: after a runtime mode change the output falls back from HDMI
to DVI signalling (picture unaffected); the DisplayPort path doesn't read
EDID extension blocks yet.

- **Phase 2 – acceleration:** firmware loading, GART/VM, interrupts, SDMA,
  then Mesa through an amdgpu-style interface.

See `docs/test-log.md` for test results and `docs/findings-linux-capture.md`
for register findings.

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
over the system `radeon_hd`), then reboot:

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
