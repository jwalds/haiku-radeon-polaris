# Capturing reference data on Linux (Xubuntu live)

The Linux `amdgpu` driver drives the RX 560 correctly, so a snapshot of its
display registers is our "known good" reference when radeon_hd misbehaves.

## Before you start

- Boot the Xubuntu live image with the RX 560 as the only GPU and the monitor
  you'll use under Haiku on the same port.
- Secure Boot doesn't matter here: we read registers through amdgpu's own
  debugfs files, not `/dev/mem`.
- The script only **reads** registers. Reading a few display registers
  (LUT/AUX data ports) bumps an internal index; this is harmless.

## Steps

1. Copy `tools/linux-capture.sh` to the live session (USB stick, or
   `wget https://raw.githubusercontent.com/jwalds/haiku-radeon-polaris/main/tools/linux-capture.sh`).
2. At the monitor's native resolution, run:

   ```
   sudo bash linux-capture.sh native
   ```

3. Switch to a low mode and capture again (this diff shows exactly which
   registers a mode change touches):

   ```
   xrandr                                   # note your output name, e.g. DisplayPort-0
   xrandr --output DisplayPort-0 --mode 1024x768
   sudo bash linux-capture.sh 1024x768
   xrandr --output DisplayPort-0 --auto
   ```

4. If you have a second monitor, plug it in, enable it, and run
   `sudo bash linux-capture.sh dual`.
5. Send me the resulting `polaris-capture-*.tar.gz` files (attach them in the
   chat, or copy them to the Haiku machine's `~/captures/`).

## What's in a capture

| File | Purpose |
|---|---|
| `vbios.rom` | The card's AtomBIOS — lets us run/inspect command tables offline |
| `lspci.txt` | Exact PCI ID, revision, BAR layout |
| `registers.txt` | Non-zero display/MC register values (dword index + value) |
| `edid-*.bin`, `connector-*.txt`, `xrandr.txt` | Monitor EDID and modes |
| `dmesg-amdgpu.txt`, `firmware_info.txt`, `drm_state.txt` | Driver's view of connectors, clocks, firmware |

## Optional: umr

If the `umr` package is available (`sudo apt install umr`), these give named
register dumps that are easier to read:

```
sudo umr --list-blocks > umr-blocks.txt
sudo umr -s dce112 > umr-dce112.txt      # whole display block, named
```
