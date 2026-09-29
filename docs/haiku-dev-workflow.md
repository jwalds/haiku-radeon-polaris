# Development workflow

The setup used during development: a workstation holds the two working
trees side by side, and a Haiku machine with the Polaris card is both the
build host and the test target.

```
workspace/
  haiku-radeon-polaris/   this repository
  haiku/                  Haiku checkout, branch radeon_hd-polaris
  .keys/                  SSH keys (not part of any repository)
```

**Rule: only the person at the Haiku machine reboots it.** Builds are
installed with `tools/haiku-install.sh --no-reboot`.

## Sync to the Haiku machine

```
HAIKU_IP=<address> haiku-radeon-polaris/tools/sync-to-haiku.sh
```

The first run clones Haiku on the test machine (reusing a local clone in
`~/haiku-build/haiku` if there is one) and allows pushes into the checked
out branch; later runs push `radeon_hd-polaris` and `main`. If the machine
gets its address by DHCP, find it by scanning the subnet for port 22.

## One-time build setup (on the Haiku machine)

```
cd ~/haiku && ./configure        # native build, system gcc
```

## Build just the driver + accelerant

```
cd ~/haiku
jam -q radeon_hd radeon_hd.accelerant
```

Build commands over SSH must run in a login shell (`bash -lc`), otherwise
`LIBRARY_PATH` is unset and the host `package` tool fails to find libbsd.

## Install (user non-packaged overrides the system copy)

`tools/haiku-install.sh` copies the two binaries to:

- `/boot/home/config/non-packaged/add-ons/kernel/drivers/bin/radeon_hd`
  (+ symlink in `.../drivers/dev/graphics/`)
- `/boot/home/config/non-packaged/add-ons/accelerants/radeon_hd.accelerant`

and syncs the disk. Not `/boot/system/non-packaged`: the kernel's
`get_priority()` (legacy_drivers.cpp) tests `/boot/system` before
`/boot/system/non-packaged`, so both get priority 0 and the packaged
radeon_hd wins on the next rescan ("devfs: reload driver" in syslog).

If the patched accelerant fails to load, app_server silently falls back to
the stock one. Check with `tools/addon_check.cpp`, and see which accelerant
is in use with `listimage <app_server team> | grep accelerant`.

## Debug tools (build on the Haiku machine with g++)

- `tools/radeon_regs.cpp` – read/write/dump registers (dword index).
- `tools/modeset_test.cpp` – set a mode through the accelerant without
  app_server (use in fail-safe video mode).
- `tools/dpms_test.cpp` – query/set DPMS through app_server.
- `tools/addon_check.cpp` – check that an accelerant loads.

## If the screen stays black

SSH still works, so: `tools/haiku-install.sh --remove`, then reboot.
At the console: hold **Shift** at boot → *Safe mode options* →
*Disable user add-ons* (ignores non-packaged drivers), or
*Use fail-safe video mode*.

## Logs

`/var/log/syslog` (look for `radeon_hd:`), plus
`/var/log/previous_syslog` after a reboot.
