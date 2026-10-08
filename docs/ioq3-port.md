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

As for the other GL clients (see `tools/test/common.sh`): the RadeonGfx
server must run, with the Zink environment set, and
`LIBRARY_PATH` must start with the SDL prefix and the stack's lib directory,
so `libEGL`/`libGLESv2` are the stack's. `tools/vktest/sdlgl.c` is the
minimal SDL check (window, desktop GL context, fixed-function triangle,
input events).

## Status

- SDL2 Wayland on the stack: works. `sdlgl` gets `GL_RENDERER: zink Vulkan
  1.3(AMD Radeon RX 460 Graphics (RADV POLARIS11))`, `GL_VERSION: 4.6
  (Compatibility Profile) Mesa 23.3.6` and runs 300 frames at 66 fps with
  no errors apart from the known unimplemented ioctl messages.
- ioq3 builds. It has not run yet: no game data (`baseq3/pak0.pk3`) on the
  machine.

## Open points

- Game data, then the first run with `+set r_renderer opengl1` and
  `opengl2` (`SDL_VIDEODRIVER=wayland`).
- Input: check that Haiku's Wayland server offers `wl_seat` keyboard and
  pointer, and that relative mouse (pointer constraints) works for mouse
  look.
- Audio: SDL's Haiku audio driver (`SDL_AUDIODRIVER=haiku`) or OpenAL.
- Fullscreen and mode switching under Haiku's Wayland server.
- Possible hits of unimplemented DRM calls (`DrmSyncobjSignal`,
  `TimelineSignal`, `Query`, sync files) once real frames are rendered.
