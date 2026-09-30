# Phase 2 plan: 2D acceleration

## What the desktop does today

app_server does not use the accelerant 2D hooks any more:

- All drawing (text, fills, gradients, bitmaps) is done in software by
  `Painter` (AGG) into a back buffer in system RAM (`MallocBuffer`, B_RGBA32).
- The only path to the screen is `HWInterface::_CopyBackToFront()`, a CPU copy
  of each dirty rectangle into the frame buffer (mapped write-combined).
- `AccelerantHWInterface` no longer fetches `B_FILL_RECTANGLE`,
  `B_SCREEN_TO_SCREEN_BLIT`, `B_INVERT_RECTANGLE` or the engine hooks.

So implementing the classic accelerant 2D hooks in radeon_hd.accelerant would
change nothing on screen. Any gain has to come from a small app_server change
that hands work to the GPU.

## What the GPU offers

Polaris has no 2D engine. The two usable engines are:

| Engine | Use | Needs |
|---|---|---|
| SDMA (sdma_v3_0) | copy, constant fill, sub-window (rect) copy | SDMA microcode, ring buffer, fence |
| GFX/CP | everything else (shaders) | CP/MEC microcode, RLC, shader programs; far larger |

SDMA is the right engine for 2D: it does rectangle copies and fills with no
shaders. Firmware: `polaris11_sdma.bin`, `polaris11_sdma1.bin` (linux-firmware),
loaded directly through `SDMA0_UCODE_ADDR`/`SDMA0_UCODE_DATA`.

## Steps

Each step ends in something testable on the Haiku machine.

0. **Measure first.** Time `_CopyBackToFront` vs. `Painter` work in app_server
   for common actions (window drag, scrolling, resizing at 2560x1440). If the
   copy is a small share, GPU offload gives little and the plan changes.
1. **SDMA bring-up (kernel driver).** Load the SDMA0 firmware, set up a ring
   buffer and write-back/fence area in VRAM, unhalt the engine, submit a NOP and
   a fence write. Test tool: `sdma_test` via a private ioctl.
2. **VRAM operations.** `SDMA_OP_CONST_FILL` and `SDMA_OP_COPY` sub-window
   (rect) copy inside VRAM. Test: fill/copy rectangles on the visible screen.
3. **GART.** VM context 0 page table in VRAM, so SDMA can read pages in system
   RAM. Test: copy a locked RAM buffer to the screen.
4. **Accelerated back-to-front copy.** Allocate the app_server back buffer as a
   locked area, register its pages with the GART, and add a private accelerant
   hook `radeon_copy_to_front(rects)` that app_server calls instead of the CPU
   copy, then waits on the fence. Fall back to the CPU path if the hook is
   missing. This is the app_server change; it stays in this repo.
5. **Optional: GPU fills/scrolls.** Once content lives in VRAM, `CopyRegion`
   (scrolling, window moves) could use in-VRAM rect copies. This needs a VRAM
   back buffer and a bigger app_server change; decide after step 4.

## Risks

- A hung SDMA engine hangs the machine; keep every submission behind a
  timeout and reset the engine on failure.
- GART mistakes cause GPU page faults or memory corruption; start with
  read-only mappings.
- The app_server change is outside radeon_hd; keep it minimal and optional.
- Full 3D (Mesa/RadeonSI) is a separate, much larger project and not in scope.
