# Development workflow on the Haiku test machine

The RX 560 box (`192.168.137.55`) is both build host and test target.

## One-time setup (on the Haiku box)

```
cd ~ && git clone https://github.com/haiku/haiku.git
git clone https://github.com/haiku/buildtools.git    # jam
cd haiku && git checkout $(cat ~/haiku-radeon-polaris/patches/BASE_COMMIT)
git checkout -b radeon_hd-polaris
git am ~/haiku-radeon-polaris/patches/*.patch
mkdir generated && cd generated && ../configure      # native build, system gcc
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
