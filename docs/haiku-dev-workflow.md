# Development workflow on the Haiku test machine

The RX 560 box (`192.168.137.55`) is both build host and test target.

Source of truth is the minibook folder `haiku_gfx/`; the Haiku box holds full
clones that we push to.

## Sync (from the minibook)

```
haiku-radeon-polaris/tools/sync-to-haiku.sh      # default user@192.168.137.55
```

First run clones `haiku` and `buildtools` on the box and allows pushes into
the checked-out branch; later runs just push `radeon_hd-polaris` and `main`.

## One-time build setup (on the Haiku box)

```
cd ~/haiku && mkdir -p generated && cd generated && ../configure   # native build, system gcc
```

## Build just the driver + accelerant

```
cd ~/haiku/generated
jam -q radeon_hd radeon_hd.accelerant
```

## Install (non-packaged overrides the system copy)

`tools/haiku-install.sh` copies the two binaries to:

- `/boot/system/non-packaged/add-ons/kernel/drivers/bin/radeon_hd`
  (+ symlink in `.../drivers/dev/graphics/`)
- `/boot/system/non-packaged/add-ons/accelerants/radeon_hd.accelerant`

then reboots. `tools/haiku-install.sh --remove` restores the stock setup.

## If the screen stays black

SSH still works, so: `tools/haiku-install.sh --remove && shutdown -r`.
At the console: hold **Shift** at boot → *Safe mode options* →
*Disable user add-ons* (ignores non-packaged drivers), or
*Use fail-safe video mode*.

## Logs

`/var/log/syslog` (look for `radeon_hd:`), plus
`/var/log/previous_syslog` after a reboot.
