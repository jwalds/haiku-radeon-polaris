# Coding style

**Rule:** all code follows the conventions already used in the `radeon_hd`
driver and accelerant (which follow the Haiku coding guidelines,
https://www.haiku-os.org/development/coding-guidelines). When in doubt, copy
the style of the surrounding code.

## Naming

| Kind | Convention | Examples from radeon_hd |
|---|---|---|
| Functions | `lower_snake_case`, prefixed by subsystem | `display_crtc_lock()`, `radeon_gpu_mc_setup()`, `pll_set()`, `encoder_assign_crtc()` |
| Local variables, parameters, struct members | `camelCase` | `crtcID`, `tableMajor`, `fbAddress`, `regs->grphEnable` |
| Globals | `g` prefix | `gInfo`, `gDisplay`, `gConnector`, `gAtomContext` |
| File statics | `s` prefix | `sEngineToken` |
| Constants / const tables | `k` prefix | `kSupportedDevices` |
| Enum values | `UPPER_SNAKE`, family prefix | `RADEON_POLARIS11` |
| Register defines | `UPPER_SNAKE` with family prefix, byte offsets | `EVERGREEN_GRPH_ENABLE`, `NI_INPUT_CSC_CONTROL`, `VOL_CRTC1_REGISTER_OFFSET` |
| AMD-derived dword-index registers | family prefix + Linux `mm` name | `POL_mmDC_GPIO_HPD_A` |

Register headers per display generation: `evergreen_reg.h` (DCE 4),
`ni_reg.h` (5), `si_reg.h` (6), `sea_reg.h` (8), `vol_reg.h` (10),
`car_reg.h` (11.0), `pol_reg.h` (11.2). Put new registers in the header of the
first generation that introduced them.

## Formatting

- Tabs, 4 columns wide; lines ≤ 100 columns (radeon_hd keeps ~80).
- Return type on its own line; function body brace on its own line.
  Control statement braces on the same line.
- Two blank lines between functions; `//	#pragma mark -` sections.
- Pointers/references bind to the type: `uint8* rom`, `radeon_shared_info &info`
  (the latter is how existing code writes `info` references).
- Chipset/DCE checks use the existing style:
  `if (info.chipsetID >= RADEON_CEDAR)`, `if (info.dceMajor >= 5)`.
  Per-CRTC selection uses `switch (crtcID)` like `init_registers()`.
- Logging: `TRACE("%s: ...\n", __func__)` for debug, `ERROR(...)` for failures;
  use `B_PRIu32`/`B_PRIX32` format macros.
- Comments: `//`; markers are `// TODO:` (not FIXME).
- Return `status_t` (`B_OK`, `B_ERROR`, ...) from fallible functions.

## Commits

Haiku style: `radeon_hd: short summary`, blank line, bullet list of changes.
