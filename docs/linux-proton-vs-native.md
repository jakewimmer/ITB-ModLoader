# Linux: Proton vs. native

The mod loader runs on Linux two ways. Both are supported; this documents the
trade-off so you can pick one.

## Proton (fallback)

Run the **Windows** build of the game and the Windows mod loader through Proton.
Into the Breach is rated WineHQ Platinum, and the loader's Windows DLLs work under
Proton with no native code involved. This is the fallback path: it needs no native
`.so`s, and it covers memedit fields whose Linux offsets have not been derived yet
(see the memedit note below).

- Install the Windows loader zip into the game directory as on Windows.
- Set the game to run with a Proton version in Steam.

## Native (default this enables)

Run the **native Linux** build directly, with the loader's `.so` libraries and an
`LD_PRELOAD` launch option (see `LAUNCHER_README.md`). This avoids the Wine
translation layer.

What native adds, and its costs:

- A native render/input library (`libitbsdl.so`) reimplements the loader UI on
  SDL2 + FreeType instead of the Windows GDI+/opengl32 path.
- A save-data path branch selects the Linux XDG location.
- The main ongoing cost is **memedit**: its field offsets are ABI-specific and are
  derived per game version by running memedit's in-game calibration on Linux (the
  Windows offset table does not transfer). Until a field is calibrated on Linux,
  mods that need it should use the Proton path. See the memedit submodule's
  `LINUX_OFFSETS.md` for the calibration procedure.

## Recommendation

Use **native** for the lighter footprint and no Wine layer. Use **Proton** if you
want zero native artifacts, or if you depend on a memedit field not yet calibrated
on Linux. Proton remains the documented fallback.
