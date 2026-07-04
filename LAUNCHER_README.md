# Into the Breach Mod Loader — Linux (native)

This bundle runs the mod loader on the **native Linux** build of Into the Breach
(not the Windows build under Proton). It ships plain `.so` libraries and injects
via an `LD_PRELOAD` launch option.

## Install

1. Extract this bundle somewhere, open a terminal in it, and run:

   ```bash
   ./install.sh "/path/to/Into the Breach"
   ```

   For a default Steam install the path is typically
   `~/.local/share/Steam/steamapps/common/Into the Breach` (or under another
   Steam library you configured). The installer copies the loader files and native
   `.so`s into the game directory and backs up the files it replaces.

2. In Steam, set the game's launch option (Properties → General → Launch Options):

   ```
   LD_PRELOAD="$PWD/libitbboot.so" %command%
   ```

   `%command%` is Steam's placeholder for the game invocation; `$PWD` resolves to
   the game directory Steam launches from, where `install.sh` placed the `.so`s.

3. Launch the game. The mod loader UI (the "Configure Mods" button, console, and
   toasts) appears on the main menu.

## Why `LD_PRELOAD`

The native game embeds a Lua runtime with `package.loadlib` compiled out, so the
loader cannot load its native libraries on its own. `libitbboot.so`, preloaded via
the launch option, restores `package.loadlib` and installs the SDL/OpenGL hooks the
UI renders through. The other libraries (`libitbsdl.so`, `itb_io.so`, `ftldat.so`,
`memedit.so`) are then loaded on demand by the loader — only `libitbboot.so` is
preloaded.

## Running without the launch option (graceful degradation)

If you launch the game without the `LD_PRELOAD` option, the loader detects the
missing preload and logs an actionable message (`~/.local/share/IntoTheBreach/log.txt`):

```
libitbboot.so is not LD_PRELOADed -- set the Steam launch option ...
```

The game still runs; the loader UI is simply disabled. It does not crash.

## Uninstall

```bash
./uninstall.sh "/path/to/Into the Breach"
```

This restores the vanilla `resource.dat` and `modloader.lua` from the backups the
installer made and removes the native `.so`s. Afterward, clear the `LD_PRELOAD`
launch option in Steam. Profiles, savegames, and anything in `mods/` are left
untouched.

## Proton fallback

If you prefer not to run native artifacts, the Windows build of the loader runs
under Proton (WineHQ Platinum). See `docs/linux-proton-vs-native.md` for the
trade-off. Native is the default this bundle enables; Proton remains supported.
