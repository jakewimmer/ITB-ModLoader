# Native Linux Support for ITB-ModLoader Design

## Summary

ITB-ModLoader currently ships as Windows-only, with all of its native components
compiled as DLLs and loaded through a handful of hardcoded filenames. This design
adds native Linux support without forking the codebase: a new platform-detection
module resolves logical library names (`itb_io`, `ftldat`, `itbsdl`, `memedit`) to
`.so` or `.dll` filenames depending on OS, so the existing `package.loadlib` call
sites need only route through it instead of hardcoding an extension. The two Rust
native libraries (`ftldat-rs`, `itb-io-rs`) get Linux builds of their existing
crates, and a new library, `libitbsdl.so`, is built to replace the role the Windows
proxy DLLs play in exposing SDL2 rendering/input hooks and FreeType text rendering
to Lua — it is loaded twice from a single compiled object, once via `LD_PRELOAD` so
it can intercept SDL2 calls, and once via `package.loadlib` so it can register its
Lua API, with the two roles sharing state through the module's own globals. A
fourth component, `memedit.so`, is recompiled from the existing memedit sources,
but instead of the Windows table's hardcoded memory offsets, a new generator derives
offsets from the Linux binary's DWARF debug symbols, making it more resilient to
game updates.

The work is staged as five phases that build outward from the core: platform
detection and native library builds, headless boot with mods loading but no UI,
the SDL2/FreeType render-and-input library, memedit/easyEdit parity, and finally
packaging/CI/uninstall. Each phase is scoped to keep Windows behavior byte-for-byte
unchanged, since this is designed as an upstreamable contribution rather than a
Linux-only fork. A Proton-based comparison is documented alongside the native
approach so the trade-off between the two is explicit, and Proton remains the
zero-effort fallback if native support proves too costly to maintain.

## Definition of Done

1. Hands-on research answers whether the native Linux Breach binary can host the
   modloader (injection route into the statically-linked Lua, SDL2 hook route,
   ftldat/itb_io recompilation, memedit portability), each verdict backed by
   experiments against the local install at
   `/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach/`.
2. A Proton-baseline comparison makes the native-vs-Proton effort trade-off explicit.
3. The output is a design plan in `docs/design-plans/` ready to hand to
   `start-implementation-plan` — including per-component architecture, phasing,
   and open risks.
4. memedit gets a real feasibility verdict (the unstripped binary may help),
   not a hand-wave.

Scope and constraints confirmed with the user:

- **Target:** native Linux build first; Proton route included only as comparison baseline.
- **Scope:** everything that ships — core loader, injection layer, ftldat.dll,
  itb_io.dll, modApiExt, easyEdit, and memedit.
- **Upstream:** designed as an upstreamable contribution to itb-community —
  Windows must keep working, platform differences abstracted.
- **Method:** hands-on experimentation against the local Linux install is allowed
  (LD_PRELOAD shims, gdb); game files backed up first, savegames untouched.
- **Environment:** no Windows reference install exists yet; the user is open to
  setting up Windows/Proton + modloader for behavioral comparison if needed.
- **No known prior art**; no toolchain constraints; no pre-made architectural decisions.

## Acceptance Criteria

### linux-port.AC1: The loader boots on the native Linux game
- **linux-port.AC1.1 Success:** Launching the native game with the loader installed runs the loader to `onModsLoaded` without error.
- **linux-port.AC1.2 Success:** The platform module returns `.so` names on Linux and `.dll` names on Windows.
- **linux-port.AC1.3 Success:** `ftldat.so` and `itb_io.so` load via `package.loadlib` and register their `luaopen_*` entries.
- **linux-port.AC1.4 Failure:** A missing native `.so` produces a clear error naming the missing library, not a bare `package.loadlib` failure.
- **linux-port.AC1.5 Regression:** On Windows the loader still loads the `.dll`s and behaves byte-for-byte as before.

### linux-port.AC2: Mods load and resource.dat/savedata work
- **linux-port.AC2.1 Success:** A mod with no in-game UI initializes and runs on native Linux.
- **linux-port.AC2.2 Success:** The loader reads and repacks `resource.dat` via `ftldat.so`; the game reads the repacked archive.
- **linux-port.AC2.3 Success:** Savedata resolves to the native-Linux location the game actually uses; profiles/saves persist across restarts.
- **linux-port.AC2.4 Failure:** A savedata path containing non-ASCII characters does not crash the loader.

### linux-port.AC3: The in-game loader UI renders and takes input
- **linux-port.AC3.1 Success:** `sdl.drawHook` fires `onFrameDrawStart`/`onFrameDrawn` each frame; the mod-config menu renders.
- **linux-port.AC3.2 Success:** The console toggles on backquote via `sdl.eventHook` and accepts typed input.
- **linux-port.AC3.3 Success:** Toast notifications render.
- **linux-port.AC3.4 Success:** UI text renders legibly via FreeType (visual parity check against Windows).
- **linux-port.AC3.5 Edge:** Resizing the game window repositions the loader UI correctly (`onGameWindowResized`).

### linux-port.AC4: memedit works at full parity
- **linux-port.AC4.1 Success:** `memedit.so` loads via `package.loadlib` and exposes `luaopen_memedit`.
- **linux-port.AC4.2 Success:** memedit reads and writes game-object fields correctly using the Linux offset table.
- **linux-port.AC4.3 Success:** easyEdit (which builds on memedit) functions on native Linux.
- **linux-port.AC4.4 Success:** The offset generator regenerates the table from the binary's DWARF `debug_info`.
- **linux-port.AC4.5 Failure:** A binary whose symbols the generator cannot resolve fails with a diagnostic identifying the missing symbol, not a silent wrong offset.

### linux-port.AC5: Single upstreamable codebase
- **linux-port.AC5.1 Success:** The two hardcoded library names route through the platform module; no `.dll` literal remains in the load path.
- **linux-port.AC5.2 Regression:** The existing in-game test suites (`tests/`) pass on both Windows and Linux.

### linux-port.AC6: Packaging, deployment, and uninstall
- **linux-port.AC6.1 Success:** CI produces a Linux bundle (`.so`s, `scripts/`, launcher wrapper) alongside the Windows zip.
- **linux-port.AC6.2 Success:** Installing the bundle and setting the `LD_PRELOAD` launch option boots the loader on a fresh native install.
- **linux-port.AC6.3 Success:** Uninstall restores the empty `scripts/modloader.lua`, restores `resource.dat.bak`, and removes the `.so`s, leaving a vanilla game.
- **linux-port.AC6.4 Failure:** Running the game without the `LD_PRELOAD` option degrades gracefully (loader either self-reports the missing preload or the game runs vanilla) rather than crashing.

### linux-port.AC7: Proton baseline documented
- **linux-port.AC7.1 Success:** Documentation states the native-vs-Proton trade-off and keeps Proton as the fallback path.

## Glossary

- **Into the Breach (ITB)**: The turn-based strategy game this modloader injects into. Ships a native Linux binary via Steam in addition to Windows.
- **package.loadlib / luaopen_\***: Lua's built-in mechanism for loading a native shared library and calling its registration function (`luaopen_<name>`) to expose C/Rust functions to Lua scripts. Already used for the existing DLLs; this design routes it through platform-specific filenames.
- **LuaJIT / jit.os / package.config**: The Lua runtime ITB embeds. `jit.os` and `package.config` are LuaJIT introspection values used to detect the host OS and path-separator convention at runtime, forming the basis of the new platform-detection module.
- **ftldat**: The archive format/library used to read and repack `resource.dat`, the game's packed asset archive. Maintained as a separate Rust project (`ftldat-rs`) and compiled to `ftldat.dll`/`ftldat.so`.
- **itb_io**: The native I/O library handling filesystem operations the loader needs beyond stock Lua, including resolving the savedata directory. Maintained as a separate Rust project (`itb-io-rs`).
- **resource.dat**: The game's packed asset archive that the loader reads and repacks (via `ftldat`) to inject mod content.
- **memedit**: A library that reads and writes the game's in-memory object fields directly, using a table of hardcoded memory offsets. Used by mods needing runtime access to game state beyond the scripted API.
- **easyEdit**: A higher-level mod-authoring tool built on top of memedit.
- **modApiExt**: An extension layer over the core modding API that ships as part of the loader.
- **SDL2**: The cross-platform windowing/input/graphics library the game links against. The design intercepts specific SDL2 calls (`SDL_GL_SwapWindow` for frame-end, `SDL_PollEvent`/`SDL_PushEvent` for input) to drive the loader's per-frame render and input hooks on Linux.
- **LD_PRELOAD**: A dynamic-linker environment variable that loads a shared library before others and lets its symbols override matching symbols in later-loaded libraries — the mechanism used to intercept SDL2 calls in the game binary without modifying it.
- **FreeType**: The font-rendering library used to draw the loader's UI text on Linux, replacing the Windows path's use of GDI+.
- **Proton**: Valve's Windows-compatibility layer (built on Wine) that lets Windows games, including the Windows build of ITB and this loader, run on Linux. Kept as the documented fallback if native support isn't pursued.
- **WineHQ Platinum**: The top compatibility rating in the WineHQ AppDB, meaning a game runs perfectly under Wine/Proton out of the box with no tweaks needed.
- **DWARF / debug_info**: A standard debugging data format embedded in compiled binaries that records symbol names, types, and (with unstripped binaries) enough information to locate fields — used here to derive memedit's offset table instead of hardcoding it.
- **pyelftools**: A Python library for parsing ELF binaries, proposed as the tool for extracting DWARF debug info in the offset generator.
- **cdylib**: The Rust crate-type that compiles to a C-compatible dynamic library (`.so`/`.dll`), the output type needed for `package.loadlib` to load it.
- **luabind**: A C++ library that binds Lua to C++ objects; referenced here for its main-thread accessor, one candidate route for the SDL interposer to reach the game's `lua_State`.
- **non-PIE (EXEC) binary**: An executable compiled without position-independent code, so it loads at a fixed memory address every run — relevant because it means memedit/interposer offsets can be hardcoded by absolute address rather than computed at runtime.
- **XDG Base Directory Specification**: The Linux convention for where user config/data/cache files live (e.g. `~/.local/share`), used to map the Windows "known folder" concept to Linux paths.
- **compatdata**: Proton's per-game data directory (holding its virtual Windows profile, including save games); distinct from where a native Linux build writes saves, which this design must resolve separately.
- **Loader lifecycle hooks** (`onModsLoaded`, `onFrameDrawStart`/`onFrameDrawn`, `onGameWindowResized`): Named events the loader fires at specific points (mods finished loading, each frame render, window resize) that mods and the loader's own UI subscribe to.

## Architecture

The port keeps a single codebase supporting Windows, Linux, and (by extension)
macOS. The mod-loading model is unchanged; platform differences are confined to
which native artifacts load and under what filenames.

**Bootstrap is identical to Windows and requires no code injection.** The vanilla
game ships an empty `scripts/modloader.lua` that it executes at startup as its
built-in mod entry point, and it loads scripts loose from disk rather than from
`resource.dat` (verified: `resource.dat` contains zero copies of `scripts/game.lua`;
the on-disk `scripts/` holds 237 files). The loader installs by overwriting that
file with `require("scripts/mod_loader/__scripts")`, exactly as on Windows.

**Native artifacts.** Four native components, all loaded the way the loader already
loads native code (`package.loadlib`), plus one interposer:

- `ftldat.so` / `itb_io.so` — recompiled Rust (from the separate `ftldat-rs` and
  `itb-io-rs` repos), loaded via `package.loadlib` as today.
- `libitbsdl.so` — new. The SDL2 + FreeType replacement for the Windows proxy DLLs'
  Lua extension functions. Loaded **twice from one copy**: `LD_PRELOAD`'d so its SDL
  interceptors run, and `package.loadlib`'d by `modloader.lua` so it registers the
  `sdl.*` Lua API and hands the interceptor the game's `lua_State`. Because it is a
  single loaded object, the interceptors and the registered Lua callbacks share
  state through the module's own globals.
- `memedit.so` — recompiled from `memedit-vsproject`, with a regenerated offset table.

**Platform seam.** A new module loaded before `bootstrap/itb_io.lua` detects the OS
(`jit.os` / `package.config`) and maps logical library names to platform filenames.
The two hardcoded names at `bootstrap/itb_io.lua:9` and `ftldat-rs/ftldat.lua:10`
route through it. Windows behavior is byte-for-byte unchanged.

**Render/input path.** The loader's per-frame render and input hooks originate from
native functions `sdl.drawHook(fn)` (`modui/root.lua:226`) and `sdl.eventHook(fn)`
(`modui/root.lua:327`). On Windows the proxy provides these by intercepting SDL's
frame-swap and event loop. On Linux, `libitbsdl.so` reproduces them by interposing
`SDL_GL_SwapWindow` and `SDL_PollEvent`/`SDL_PushEvent` — clean, because SDL2 is
dynamically linked (54 imported `SDL_*` symbols).

### Native library contract (`libitbsdl.so`)

The Lua side must not change, so the `.so` reproduces the Windows proxy's Lua API:

```
sdl.drawHook(fn)    -- register per-frame callback; invoked as fn(screen)
                    -- each SDL_GL_SwapWindow; drives onFrameDrawStart/onFrameDrawn
sdl.eventHook(fn)   -- register input callback; fed events from SDL_PollEvent/PushEvent
sdl.*               -- surface/rect/text drawing primitives; text via FreeType
os.listdirs(path)   -- reimplemented on POSIX (removed by security.lua on load)
os.listfiles(path)  -- reimplemented on POSIX
os.getKnownFolder(id) -- reimplemented / mapped to XDG base directories
luaopen_itbsdl(L)   -- registers the above into L, stores L for the interceptors
```

The `.so` links its own SDL2 for symbol resolution but operates on the game's live
window/GL context obtained at the interception point.

## Existing Patterns

The design follows patterns already present in the loader:

- **Native loading via `package.loadlib`.** `bootstrap/itb_io.lua:9` and
  `ftldat-rs/ftldat.lua:10` already load native libraries this way and register a
  `luaopen_*` entry. `itb_io.so`, `ftldat.so`, and `libitbsdl.so` reuse this exact
  mechanism — only the filename becomes platform-dependent.
- **Native-provided savedata path.** `modapi/savedata.lua:333` delegates the
  savedata directory to the native layer (`factory.save_data_directory()`). The
  Linux path is therefore an `itb-io-rs` concern, not a Lua change. `itb-io-rs`
  already contains Linux-aware path handling (`src/path_filter.rs`).
- **Cross-platform path splitting.** `__scripts.lua:29-35` already matches both
  separators (`[\\/]`), with tests in `tests/modApi.lua:15-49`. No path-separator
  work is needed in Lua.
- **Bootstrap by file overwrite.** The Windows install overwrites
  `scripts/modloader.lua` and preserves `modloader.lua.bak`
  (`uninstall_modloader.bat` restores it). Linux uses the same convention.

New patterns introduced, with justification:

- **Platform detection module.** None exists today (confirmed). Required to select
  native filenames. Kept minimal: a single logical-name → filename mapping.
- **DWARF-derived offset generation for memedit.** The Windows table hardcodes
  offsets. The Linux binary is unstripped with `debug_info`, so offsets are derived
  from symbols instead — more update-resilient than hardcoding.

## Implementation Phases

<!-- START_PHASE_1 -->
### Phase 1: Platform seam and native-library builds

**Goal:** The loader selects native libraries by platform, and the Rust libraries
build for Linux.

**Components:**
- New platform-detection module in `scripts/mod_loader/bootstrap/` — maps logical
  names (`itb_io`, `ftldat`, `itbsdl`, `memedit`) to per-OS filenames; loaded before
  `bootstrap/itb_io.lua`.
- Route `bootstrap/itb_io.lua:9` and `ftldat-rs/ftldat.lua:10` through the module.
- `ftldat-rs` (separate repo/fork): Linux `cdylib` build; confirm Lua binding
  (`luaopen_ftldat`) and `crate-type`.
- `itb-io-rs` (separate repo/fork): Linux `cdylib` build; add a native-Linux
  savedata-path branch to `save_data_directory()` distinct from the existing Proton
  path; verify against where the native game writes saves.

**Dependencies:** None (first phase).

**Done when:** On Windows the platform module returns `.dll` names and behavior is
unchanged; on Linux the loader loads `itb_io.so` and `ftldat.so`; `cargo test`
passes for both crates. Covers `linux-port.AC1`, `linux-port.AC5`.
<!-- END_PHASE_1 -->

<!-- START_PHASE_2 -->
### Phase 2: Bootstrap and headless boot on Linux

**Goal:** The loader boots on the native game and loads non-UI mods.

**Components:**
- Linux installer/packaging step overwriting the empty `scripts/modloader.lua` and
  preserving `modloader.lua.bak` (mirrors the Windows convention).
- Verify `resource.dat` read/write via `ftldat.so` and savedata resolution via
  `itb_io.so` on the native binary.
- Confirmation spike: an `LD_PRELOAD` stub interposing `SDL_GL_SwapWindow` that fires
  each frame and reaches the game's `lua_State` (via luabind's main-thread accessor at
  its fixed address, or captured at registration). Settles the Phase 3 mechanism.

**Dependencies:** Phase 1.

**Done when:** The loader boots to `onModsLoaded` on the native game; a mod with no
in-game UI loads and runs; `resource.dat` repacks correctly; the spike confirms the
frame hook fires and `lua_State` is reachable. Covers `linux-port.AC1`,
`linux-port.AC2`.
<!-- END_PHASE_2 -->

<!-- START_PHASE_3 -->
### Phase 3: `libitbsdl.so` — SDL2 + FreeType extension/render library

**Goal:** The in-game mod-loader UI, console, and toasts render and take input on
native Linux.

**Components:**
- `libitbsdl.so` (new; a Linux counterpart to `DLL-Extensions`/`IntoTheBreachLua`,
  location TBD — new repo or `linux/` subtree). Implements the native library
  contract above: `sdl.drawHook`/`sdl.eventHook` via SDL2 interception,
  `sdl.*` drawing primitives, text via FreeType, POSIX `os.listdirs`/`os.listfiles`/
  `os.getKnownFolder`, and `luaopen_itbsdl`.
- Dual-use wiring: `LD_PRELOAD` interceptors and the `package.loadlib` registration
  share state through the module's globals.

**Dependencies:** Phase 2 (spike result fixes the interception points).

**Done when:** The mod-config menu, console (backquote toggle), and toast UI render
and accept input on native Linux; `onFrameDrawStart`/`onFrameDrawn` fire each frame;
text renders legibly. Covers `linux-port.AC3`.
<!-- END_PHASE_3 -->

<!-- START_PHASE_4 -->
### Phase 4: memedit at full parity

**Goal:** memedit and easyEdit work on native Linux.

**Components:**
- `memedit.so`: compile `memedit-vsproject` for Linux (guard `windows.h`, swap
  `__declspec(dllexport)` for `__attribute__((visibility("default")))`); load via
  `package.loadlib`.
- Offset generator: a script parsing the Linux binary's DWARF `debug_info` (e.g.
  `pyelftools`) to emit a Linux offset table replacing the hardcoded Windows
  `__addresses.lua`. Both the generated table and the generator are checked in.
- Regeneration procedure documented for future game versions.

**Dependencies:** Phase 2 (loader boot); independent of Phase 3.

**Done when:** memedit-dependent mods and easyEdit function on native Linux;
regenerating offsets for a new game version is a documented, repeatable step. Covers
`linux-port.AC4`.
<!-- END_PHASE_4 -->

<!-- START_PHASE_5 -->
### Phase 5: Packaging, CI, and uninstall

**Goal:** A reproducible Linux release package and a clean uninstall.

**Components:**
- Cross-platform packaging producing a Linux bundle (`.so`s, `scripts/`, launcher
  wrapper) alongside the Windows zip; factor shared logic out of `_release.bat`.
- Linux CI job building the Rust and C/C++ artifacts and assembling the bundle.
- Launcher wrapper setting the Steam launch option
  `LD_PRELOAD="$PWD/libitbsdl.so" %command%`.
- Linux uninstall mirroring `uninstall_modloader.bat`: restore
  `scripts/modloader.lua` from `.bak`, restore `resource.dat.bak`, remove `.so`s.

**Dependencies:** Phases 1–4.

**Done when:** A fresh Linux install from the package boots the loader and runs mods;
uninstall restores the vanilla game; CI produces the Linux bundle. Covers
`linux-port.AC6`.
<!-- END_PHASE_5 -->

## Additional Considerations

**Multi-repo coordination.** Work spans four repos, each needing a fork:
`ITB-ModLoader` (Lua, packaging, shipped artifacts), `ftldat-rs`, `itb-io-rs`, and
the memedit repos. `ITB-ModLoader` ships the *compiled* `.so`s, so artifact flow from
the dependency forks into this repo must be defined (CI build vs. committed binaries).

**Font parity.** FreeType metrics differ from Windows GDI+. Text layout in the
loader UI needs visual QA against the Windows reference; a Windows/Proton install may
be stood up for comparison.

**Reaching `lua_State` from the interposer.** The main risk in Phase 3, front-loaded
into the Phase 2 spike. The binary is non-PIE (`EXEC`) with a fixed-address,
unstripped symbol table (`luaL_newstate` at `0x9f536a`, luabind main-thread accessor
present), so a fixed-address route exists if registration-time capture is
insufficient.

**Native savedata path.** Must match where the native game actually writes saves
(distinct from the Proton `compatdata` path already in `itb-io-rs`). Verified early
in Phase 1/2 to avoid profile/savedata breakage.

**Proton remains the documented fallback.** The Windows game + loader under Proton is
WineHQ Platinum with working mods and stays documented as the zero-effort option.

**Git workflow.** Work proceeds on `feature/linux-support` on the fork
(`origin` = `jakewimmer/ITB-ModLoader`); the upstream PR to `itb-community` is opened
only when the product is complete.
