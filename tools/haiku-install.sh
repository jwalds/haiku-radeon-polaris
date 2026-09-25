#!/bin/sh
# Install / remove test builds of radeon_hd on Haiku (non-packaged overrides).
# usage: haiku-install.sh [--no-reboot]      install from ~/haiku/generated
#        haiku-install.sh --remove            restore packaged driver

NP=/boot/system/non-packaged/add-ons
DRV_BIN=$NP/kernel/drivers/bin
DRV_DEV=$NP/kernel/drivers/dev/graphics
ACC=$NP/accelerants
GEN=${GEN:-$HOME/haiku/generated}

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
echo "Installed:"; ls -l "$DRV_BIN/radeon_hd" "$ACC/radeon_hd.accelerant"

[ "$1" = "--no-reboot" ] || shutdown -r
