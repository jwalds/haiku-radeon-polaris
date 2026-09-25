#!/bin/bash
# Capture reference data from a working Linux (amdgpu) boot for Haiku radeon_hd
# Polaris bring-up. Run from a live Xubuntu session as:
#     sudo bash linux-capture.sh [label]
# Produces ./polaris-capture-<label>-<date>.tar.gz
#
# Nothing here writes to the GPU: register access is read-only via the
# amdgpu debugfs interface.

set -u
LABEL="${1:-native}"
OUT="polaris-capture-${LABEL}-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
cd "$OUT" || exit 1

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo"; exit 1; }
mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug

# Find the amdgpu DRI node
DRI=""
for d in /sys/kernel/debug/dri/*; do
	if [ -e "$d/amdgpu_regs" ]; then DRI="$d"; break; fi
done
[ -n "$DRI" ] || { echo "amdgpu debugfs not found (is amdgpu loaded?)"; exit 1; }
echo "Using $DRI"

PCIDEV=$(lspci -D -d 1002: | awk '/VGA|Display/ {print $1; exit}')

echo "== system info"
uname -a                          > uname.txt
lspci -nn -vvv -s "$PCIDEV"       > lspci.txt 2>&1
dmesg | grep -iE 'amdgpu|drm|atom' > dmesg-amdgpu.txt
cat "$DRI/amdgpu_firmware_info"   > firmware_info.txt 2>/dev/null
cat "$DRI/name"                   > dri_name.txt 2>/dev/null
cat "$DRI/state"                  > drm_state.txt 2>/dev/null
[ -r "$DRI/amdgpu_pm_info" ] && cat "$DRI/amdgpu_pm_info" > pm_info.txt 2>/dev/null

echo "== VBIOS"
cp "$DRI/amdgpu_vbios" vbios.rom 2>/dev/null || {
	ROM="/sys/bus/pci/devices/$PCIDEV/rom"
	echo 1 > "$ROM"; cat "$ROM" > vbios.rom; echo 0 > "$ROM"
}

echo "== connectors / EDID / modes"
for c in /sys/class/drm/card*-*; do
	n=$(basename "$c")
	{
		echo "status: $(cat "$c/status")"
		echo "enabled: $(cat "$c/enabled" 2>/dev/null)"
		echo "modes:"; cat "$c/modes" 2>/dev/null
	} > "connector-$n.txt"
	[ -s "$c/edid" ] && cp "$c/edid" "edid-$n.bin"
done
if [ -n "${SUDO_USER:-}" ] && command -v xrandr >/dev/null; then
	sudo -u "$SUDO_USER" DISPLAY="${DISPLAY:-:0}" xrandr --verbose > xrandr.txt 2>&1
fi

echo "== registers"
python3 - "$DRI/amdgpu_regs" > registers.txt <<'EOF'
import os, sys, struct
fd = os.open(sys.argv[1], os.O_RDONLY)
def rd(dw):
    try:
        return struct.unpack('<I', os.pread(fd, 4, dw * 4))[0]
    except OSError:
        return None

# dword-index ranges (Linux amdgpu naming: mmXXX = dword index)
ranges = [
    ("VGA / D*VGA_CONTROL",        0x00c0, 0x0100),
    ("MC_VM (FB/AGP/aperture)",    0x0800, 0x0820),
    ("HDP nonsurface",             0x0b00, 0x0b10),
    ("BIF CONFIG_MEMSIZE / HDP",   0x1500, 0x1540),
]
# Per-CRTC display pipe (DCP/LB/SCL/CRTC), DCE 10/11 stride
crtc_off = [0x0000, 0x0200, 0x0400, 0x2600, 0x2800, 0x2a00]
for i, off in enumerate(crtc_off):
    ranges.append(("CRTC%d pipe" % i, 0x1a00 + off, 0x1c00 + off))
# DIG front/back ends, HPD, AUX, PHY (broad sweep)
ranges.append(("DIG / HPD / AUX / PHY",  0x4800, 0x5200))
# DCCG / PLL / DMIF area
ranges.append(("DCCG / PLL",           0x0100, 0x0200))
ranges.append(("DMIF / DCFE misc",     0x0300, 0x0400))

for name, lo, hi in ranges:
    print("# %s  [0x%04x-0x%04x)" % (name, lo, hi))
    for dw in range(lo, hi):
        v = rd(dw)
        if v is None:
            print("0x%04x ERR" % dw)
        elif v != 0:
            print("0x%04x 0x%08x" % (dw, v))
EOF

cd ..
tar czf "$OUT.tar.gz" "$OUT"
echo
echo "Done: $(pwd)/$OUT.tar.gz"
