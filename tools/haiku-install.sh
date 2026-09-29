#!/bin/sh
# Install / remove test builds of radeon_hd on Haiku (non-packaged overrides).
# usage: haiku-install.sh [--no-reboot]      install from ~/haiku/generated
#        haiku-install.sh --remove            restore packaged driver

# Use the *user* non-packaged directory: the kernel's legacy driver
# get_priority() matches /boot/system as a prefix first, so drivers in
# /boot/system/non-packaged get the same priority as the packaged ones and
# the stock radeon_hd replaces ours on the next driver rescan.
NP=/boot/home/config/non-packaged/add-ons
OLD_NP=/boot/system/non-packaged/add-ons
DRV_BIN=$NP/kernel/drivers/bin
DRV_DEV=$NP/kernel/drivers/dev/graphics
ACC=$NP/accelerants
GEN=${GEN:-$HOME/haiku/generated}

# clean up installs from older versions of this script
rm -f "$OLD_NP/kernel/drivers/bin/radeon_hd" \
	"$OLD_NP/kernel/drivers/dev/graphics/radeon_hd" \
	"$OLD_NP/accelerants/radeon_hd.accelerant"

if [ "$1" = "--remove" ]; then
	rm -f "$DRV_BIN/radeon_hd" "$DRV_DEV/radeon_hd" "$ACC/radeon_hd.accelerant"
	echo "Removed test radeon_hd; reboot to use the packaged driver."
	exit 0
fi

drv=$(find "$GEN/objects" -type f -name radeon_hd -path '*kernel*' | head -1)
acc=$(find "$GEN/objects" -type f -name radeon_hd.accelerant | head -1)
[ -n "$drv" ] && [ -n "$acc" ] || { echo "Build output not found under $GEN"; exit 1; }

mkdir -p "$DRV_BIN" "$DRV_DEV" "$ACC"
cp "$drv" "$DRV_BIN/radeon_hd"
ln -sf ../../bin/radeon_hd "$DRV_DEV/radeon_hd"
cp "$acc" "$ACC/radeon_hd.accelerant"
# flush to disk: a KDL/hard reset before the next boot would otherwise
# leave a corrupt accelerant, and app_server silently falls back to the
# stock one
sync
echo "Installed:"; ls -l "$DRV_BIN/radeon_hd" "$ACC/radeon_hd.accelerant"

[ "$1" = "--no-reboot" ] || shutdown -r
