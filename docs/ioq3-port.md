# ioquake3 on Haiku through the RadeonGfx stack

Goal: run the ioq3 fork (github.com/jwalds/ioq3) on Haiku with its
rendering going through RadeonGfx -> RADV -> Zink -> EGL on Wayland, not
through Haiku's software GL kit.

## Approach

ioq3 uses SDL2 for the window, input and audio, and both renderers load
desktop OpenGL through `SDL_GL_GetProcAddress`. Haiku's SDL2 package only has
the native `BWindow`/`BGLView` video driver. We build SDL2 2.32.8 with its
Wayland video driver as well (`patches/ioq3/SDL2-2.32.8-haiku-wayland.patch`)
and select it with `SDL_VIDEODRIVER=wayland`. SDL then creates a Wayland
surface on Haiku's in-process Wayland server and an EGL context on top of
it. With `MESA_LOADER_DRIVER_OVERRIDE=zink` the context is a desktop OpenGL
4.6 compatibility context, which covers both the GL1 (fixed function) and the
GL2 renderer.

The SDL patch only changes the Haiku branch of SDL's CMake file (it also
checks for EGL and Wayland and adds `src/core/unix`) and passes an explicit
second argument to `BUrl`, whose one-argument constructor is ambiguous with
current Haiku headers. The native Haiku driver stays built and is the
default when `SDL_VIDEODRIVER` is not set.

## ioq3 changes (`patches/ioq3/0001-Add-a-Haiku-port.patch`)

- `q_platform.h`: a Haiku block (OS string, x86/x86_64, endianness, `.so`).
- `cmake/platforms/haiku.cmake`: links `libnetwork` (sockets, `getaddrinfo`).
- `code/tools/lcc/src/dag.c`: `kill()` renamed, it clashed with `<signal.h>`.

The fork builds completely (client, dedicated server, both renderers, game
modules as native libraries and as QVMs) with OpenAL and HTTP downloads
switched off for now.

## Building

`tools/build-ioq3.sh` on the Haiku machine builds SDL2 into a private prefix
and then the game against it. Needs `libxkbcommon_devel`, `wayland_devel`,
`wayland_protocols`, `cmake` and `git`.

## Running

The scripts take the game data from `$IOQ3PORT/data/baseq3` (retail
`pak0.pk3`..`pak8.pk3`) and the SDL prefix from `$IOQ3PORT` (default
`~/ioq3port`):

- `tools/ioq3-run.sh <opengl1|opengl2> [arguments]` starts the RadeonGfx
  server if needed, sets the Zink environment (as `tools/test/common.sh`),
  puts the SDL prefix and the stack's lib directory first in
  `LIBRARY_PATH` (so `libEGL` is the stack's) and runs `ioquake3` with
  `SDL_VIDEODRIVER=wayland`. The renderer is the cvar `cl_renderer`.
- `tools/ioq3-gdb.sh` the same under gdb, with a backtrace of a crash.
- `tools/ioq3-fps.sh` frame rate of the first frames on q3dm1.
- `tools/vktest/sdlgl.c` (`tools/sdlgl-run.sh`) is the minimal SDL check.

Example, a screenshot of q3dm1 and quit:
`ioq3-run.sh opengl2 +devmap q3dm1 +wait 600 +screenshot shot +wait 50 +quit`.

## Status (2026-10-08)

- Both renderers run on the stack and draw q3dm1 correctly
  (`docs/images/ioq3-gl1-q3dm1.png`, `ioq3-gl2-q3dm1.png`): `GL_RENDERER:
  zink Vulkan 1.3 (AMD Radeon RX 460 Graphics (RADV POLARIS11))`; GL1 gets a
  4.6 compatibility context, GL2 a 4.6 core context.
- Frame rate standing at the spawn point of q3dm1, 640x480 windowed, swap
  interval 0, no sound: GL1 about 136 fps, GL2 about 143 fps (CPU at
  1.8-2.1 GHz, timed to the second, so rough).
- Shutdown: `wl_display_disconnect` crashed in libwayland-client right after
  the window surface was destroyed (Haiku's in-process Wayland server quits
  the window asynchronously, as in glwl and glmark2). Fixed in the SDL patch:
  a roundtrip and 200 ms before disconnecting. Both renderers now quit
  cleanly and leave no process behind.
- The bot match, sound and input have not been tried. A listing of the
  globals Haiku's Wayland server advertises (`WAYLAND_DEBUG=1`): `wl_shm`,
  `wl_compositor`, `wl_subcompositor`, `wp_viewporter`, `wl_output`,
  `wl_data_device_manager`, `wl_seat` (pointer and keyboard), `xdg_wm_base`,
  `zwp_text_input_manager_v3`, and KDE's server decoration manager. No
  `zwp_relative_pointer_manager_v1` and no `zwp_pointer_constraints_v1`.

## Open points

- Mouse look: SDL's relative mouse mode on Wayland needs the relative
  pointer and pointer constraints protocols, which the server lacks. Test
  with a real mouse; the fix is either those protocols in the server or a
  warp-based fallback in SDL.
- Keyboard and menu input with the real keyboard (the Compose file
  message `xkbcommon: couldn't find a Compose file` is harmless).
- Audio: SDL's Haiku audio driver (`SDL_AUDIODRIVER=haiku`) or OpenAL.
- Fullscreen and mode switching under Haiku's Wayland server.
- A bot match for a few minutes and the unimplemented DRM calls
  (`DrmSyncobjSignal`, `TimelineSignal`, `Query`, sync files), a leak check
  of GTT/VRAM over several map changes, the full stack test runner.
