# Bootstrap: platform detection & native-library loading

Last verified: 2026-07-03

## Purpose
Loads the loader's native libraries (`itb_io`, `ftldat`, `itbsdl`, `memedit`) in a
way that works on both Windows and native Linux from one codebase. All per-OS
differences in native loading funnel through `platform.lua` so the rest of the
loader stays platform-agnostic.

## Contracts
- **Exposes** (`platform.lua`, global `Platform`):
  - `Platform.name` — `"windows"` or `"linux"`, inferred from `package.config`'s
    separator (the game embeds PUC Lua 5.1.5, so `jit.os` does not exist).
  - `Platform.nativeLibrary(logical)` — maps a logical name to the per-OS filename
    to pass to `package.loadlib`. Errors on an unknown name or an OS with no entry.
  - `Platform.nativePreloadActive()` — on Linux, probes whether the `libitbboot.so`
    LD_PRELOAD is present (real `package.loadlib`) vs absent (stock stub). Always
    `true` on Windows. Result is memoized.
  - `Platform.preloadHint()` — actionable one-line message naming the Steam launch
    option, for logging when the preload is missing.
- **Guarantees**: If `nativePreloadActive()` is false, native loaders self-report
  via `LOG(Platform.preloadHint())` and return/error cleanly — the game still runs
  with native features disabled (graceful degradation, never a crash).

## Adding a native library
1. Add a row to `LIBRARY_NAMES` in `platform.lua` with the `windows`/`linux`
   filenames (`nil` where a platform has no build).
2. On Linux the filename MUST be `./`-prefixed — `dlopen(3)` does not search the
   working directory for a bare name.
3. Load it via `Platform.nativeLibrary(...)` + `package.loadlib`, gated on
   `Platform.nativePreloadActive()`. Follow `itb_io.lua` / `itbsdl.lua`.

## Dependencies
- **Used by**: `itb_io.lua`, `ftldat-rs/ftldat.lua`, `itbsdl.lua`, and the memedit
  extension — every native-library loader.
- **Provided by**: `libitbboot.so` on Linux supplies the working `package.loadlib`
  these loaders call (see `itbboot/CLAUDE.md`).

## Invariants
- Load order in `__scripts.lua` is fixed: `platform` before `itbsdl`/`itb_io`
  (they call `Platform`), and `itbsdl` before `itb_io`.
- `itbsdl.lua` is Linux-only (Windows renders the loader UI natively); it early-
  returns when `Platform.name ~= "linux"`.

## Key Files
- `platform.lua` — `Platform` table (detection, name mapping, preload probe)
- `itb_io.lua`, `itbsdl.lua` — native loaders using the platform contract
- `__scripts.lua` — bootstrap require order

## Gotchas
- `nativePreloadActive()` distinguishes preload states by matching the stock
  error string `"dynamic libraries not enabled"`; if the game's Lua build changes
  that message, the probe must be updated.
- Tests: `scripts/mod_loader/tests/platform.lua`.
