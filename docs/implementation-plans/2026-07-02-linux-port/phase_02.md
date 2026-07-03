# Native Linux Support — Phase 2: Bootstrap and headless boot on Linux

**Goal:** The loader installs onto the native Linux game, boots to `onModsLoaded`, loads
a non-UI mod, repacks `resource.dat`, and resolves savedata — and a spike confirms the
Phase 3 SDL-interception mechanism (frame hook fires, `lua_State` is reachable).

**Architecture:** Installation mirrors the Windows convention (overwrite
`scripts/modloader.lua`, preserve `.bak`; back up `resource.dat`). Boot is verified
operationally by observing `modloader.log`. The spike is a throwaway `LD_PRELOAD` `.so`
that interposes `SDL_GL_SwapWindow` and reaches the game's `lua_State` captured at
`package.loadlib` registration.

**Tech Stack:** Bash (install script), the Phase 1 `.so`s (`itb_io.so`, `ftldat.so`),
C (spike interposer, `dlsym(RTLD_NEXT, …)`), the native x86-64 PUC Lua 5.1.5 game binary.

**Scope:** Phase 2 of 5. Depends on Phase 1 (platform seam + `.so` artifacts).

**Codebase verified:** 2026-07-02 (`codebase-investigator` on install/resource/savedata +
hands-on inspection of the local native binary and install).

---

## Codebase verification findings (read before implementing)

1. **There is no install script — only uninstall.** The repo has `_release.bat`,
   `package.bat`, and `uninstall_modloader.bat`. Installation is performed by copying the
   repo's shipped files (the `scripts/` tree, the native artifacts) into the game
   directory and overwriting `scripts/modloader.lua`. `uninstall_modloader.bat:12-22`
   documents the exact layout to reproduce:
   - `scripts\modloader.lua` is the installed entry point; the original is preserved as
     `scripts\modloader.lua.bak`.
   - `resources\resource.dat.bak` is the vanilla archive backup; on uninstall the
     modified `resource.dat` is deleted and the `.bak` renamed back.
   Phase 2 therefore needs a Linux `install.sh` (the Phase 5 packaging expands on it).

2. **The installed entry point is one line.** The repo's own
   `scripts/modloader.lua` is `require("scripts/mod_loader/__scripts")`. The vanilla
   native game's `scripts/modloader.lua` is **empty (0 bytes)** — confirmed on the local
   install. So installing overwrites an empty file, and the `.bak` it preserves is empty
   (uninstall restores emptiness).

3. **`resource.dat` handling:**
   - Opened at `scripts/mod_loader/modapi/init.lua:75` — `self.resource = FtlDat("resources/resource.dat")`.
   - Backup/restore logic at `modapi/init.lua:44-71` (creates `resource.dat.bak` if
     absent; restores from it when the archive was previously modified by the loader).
   - Written back at `scripts/mod_loader/modapi/ftldat.lua:119` — `self.resource:write("resources/resource.dat")`.
   - A `ModLoaderSignature` entry (`modapi/ftldat.lua:114-117`) marks a loader-modified
     archive. `FtlDat` itself is the Phase 1 `ftldat.so` (via `ftldat.lua`).
   - Local install: `resources/resource.dat` is ~355 MB; no `.bak` present yet.

4. **Savedata chain:** `Directory.savedata()` (`bootstrap/itb_io.lua:341-356`) →
   `factory.save_data_directory()` (the Phase 1 `itb_io.so` native call) → consumed by
   `modapi/savedata.lua:5-6` `getDirectory()` → profile read/write at
   `modapi/savedata.lua:64-69` (`profile_<name>/saveData.lua`). The native path resolves
   to `~/.local/share/IntoTheBreach` (Phase 1, finding #8).

5. **Boot observability:** `LOG(...)` (`mod_loader/mod_loader.lua:30-32`) writes to
   `modloader.log` (`logger_basic.lua:11`) in the save directory, via the `File:append`
   path (`bootstrap/itb_io.lua:184-196`). `modloader.log` in
   `~/.local/share/IntoTheBreach/` is the primary signal for headless boot success.

6. **`onModsLoaded` fires at `mod_loader/mod_loader.lua:762`** —
   `modApi.events.onModsLoaded:dispatch()`, after all enabled mods initialize and
   finalization completes. The event is created at `bootstrap/modApi.lua:24`.

7. **A bundled non-UI extension exists:**
   `scripts/mod_loader/extensions/modLoaderExtensions/scripts/init.lua` declares
   `mod_loader_extensions` with empty `init`/`load` and `isExtension = true`. The repo's
   `mods/` directory holds only a `put_mods_here` placeholder. Phase 2's non-UI mod test
   uses a minimal purpose-built mod dropped into `mods/`.

8. **SDL interception surface (spike-relevant), from the native binary's dynamic imports:**
   - `SDL_GL_SwapWindow` is imported → interposable for the per-frame hook.
   - `SDL_PollEvent` is imported → interposable for input **observation**.
   - `SDL_PushEvent`, `SDL_PeepEvents`, `SDL_WaitEvent`, `SDL_PumpEvents` are **not
     imported.** The game pulls input only through `SDL_PollEvent`. **Consequence for
     Phase 3:** input injection cannot go through `SDL_PushEvent` (the design's plan);
     it must be handled inside the `SDL_PollEvent` interceptor. The Phase 2 spike only
     needs `SDL_GL_SwapWindow`, but record this so Phase 3 is scoped correctly.
   - `SDL_StartTextInput` is imported (relevant to console text entry in Phase 3).
   - `luabind::get_main_thread(lua_State*)` is defined at fixed address `0xa15988`, but
     it takes an `L` argument, so it is not a zero-arg global accessor. The primary route
     to the game's `lua_State` is **capture at `luaopen_*(L)` registration** (store `L`
     in a module global the interceptor reads); fixed-address symbols
     (`luaL_newstate` @ `0x9f536a`) are a fallback. The binary is non-PIE (`EXEC`), so
     fixed addresses are stable across runs.

9. **(Revision 2026-07-02, discovered during execution — overrides all conflicting task
   wording in Phases 2–5.) `package.loadlib` is compiled out of the native Linux binary.**
   Confirmed three ways: the runtime error
   `package.loadlib failed: dynamic libraries not enabled; check your Lua installation`
   (`~/.local/share/IntoTheBreach/log.txt:64`); the Lua loadlib stub string present in
   `Breach` (embedded Lua built without `LUA_DL_DLOPEN`); the binary importing only
   `dlsym`, never `dlopen`. On Windows this works because the game ships a `lua5.1.dll`
   built with dynamic loading; the Linux build left it out. **No `.so` can be loaded from
   Lua as originally designed.**

   **Approved architecture change (user sign-off 2026-07-02): LD_PRELOAD bootstrap.**
   A new preloaded shared object (`libitbboot.so`, built from `itbboot/`, staged at the
   repo root like the other artifacts — `install.sh`'s glob already ships it) must:
   - obtain the game's `lua_State` **before** `scripts/modloader.lua` executes. The
     binary is non-PIE (`EXEC`), unstripped, with full `.debug_info`: the game's global
     state pointer and all static `lua_*` functions have stable, extractable addresses.
     Provide a repeatable extraction script (`nm`/`gdb` against the game binary) that
     generates a header of addresses; commit the generator, not only its output.
   - inject a `dlopen(3)`+`dlsym(3)`-based replacement for `package.loadlib` into the
     game's Lua, with PUC semantics (returns the entry function; on failure
     `nil, message, "open"|"init"`), so the Phase 1 Lua load sites run unchanged.
   - keep an `SDL_GL_SwapWindow` interposer logging a periodic frame counter plus the
     captured `L` — this retains the original spike's two Phase-3 de-risking proofs
     (frame hook fires; the game's `lua_State` is reachable from the interposer).
   - timing: interposing `fopen`/`fopen64` and injecting when the game first opens a
     path under `scripts/` is the suggested hook (script loading happens after the Lua
     state exists and before `modloader.lua` runs); another hook is fine if proven.
   - calling convention for the injected function: either call the game's own static
     `lua_*` functions at extracted addresses (preferred — zero ABI risk), or statically
     link vendored PUC Lua 5.1.5 and operate on the game's `L` (the finding-#2 pattern,
     ABI-compatible in principle but unproven against this binary). The bootstrap is
     itself the test of the chosen route; record which one worked.
   - **Consequences:** every native launch from Phase 2 onward prepends
     `LD_PRELOAD="$PWD/libitbboot.so"` (Tasks 2–5 below inherit this into their launch
     commands). Phase 3 already required a preload for `libitbsdl.so`; it may fold the
     bootstrap into that object or keep both preloaded — decided in Phase 3. Task 6
     below is rewritten: instead of a throwaway spike, build the real bootstrap library
     (it subsumes the spike's proofs), and it must run **before** Tasks 2–5.

10. **(Same revision) vanilla Linux boot lacks the `sdl` Lua global** that
   `bootstrap/utils.lua:1` expects (on Windows the proxy DLL provides `sdl.*` before
   scripts run). Commit `9f686b7` added a placeholder `bootstrap/sdl.lua` stub so
   headless boot can proceed; Phase 3's real `sdl` table must replace/populate it (the
   stub must defer to a real implementation when present).

**Local install path (for hands-on verification):**
`/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach/`

---

## Acceptance Criteria Coverage

### linux-port.AC1: The loader boots on the native Linux game
- **linux-port.AC1.1 Success:** Launching the native game with the loader installed runs the loader to `onModsLoaded` without error.
- **linux-port.AC1.4 Failure:** A missing native `.so` produces a clear error naming the missing library, not a bare `package.loadlib` failure.

### linux-port.AC2: Mods load and resource.dat/savedata work
- **linux-port.AC2.1 Success:** A mod with no in-game UI initializes and runs on native Linux.
- **linux-port.AC2.2 Success:** The loader reads and repacks `resource.dat` via `ftldat.so`; the game reads the repacked archive.
- **linux-port.AC2.3 Success:** Savedata resolves to the native-Linux location the game actually uses; profiles/saves persist across restarts.
- **linux-port.AC2.4 Failure:** A savedata path containing non-ASCII characters does not crash the loader.

> This is an infrastructure/operational phase: verification is by running the real game
> and observing `modloader.log`, `resource.dat`, and savedata — not by unit tests (the
> project has no standalone Lua runner; tests are in-game). The spike (Task 6) carries no
> AC of its own; it de-risks Phase 3.

---

## Prerequisites for the executor

- Phase 1 complete: `itb_io.so` and `ftldat.so` staged at the `ITB-ModLoader` repo root
  and loading via the platform module.
- **Back up the game first** (design constraint — savegames untouched):
  ```bash
  GAME="/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach"
  cp -a "$GAME" "$GAME.backup-preloader"
  ```
- For headless runs, `xvfb-run` (package `xvfb`) provides a virtual display. The game
  also needs Steam context; create `$GAME/steam_appid.txt` containing `590380` so
  `Breach` can be launched directly outside the Steam client for testing.

---

<!-- START_SUBCOMPONENT_A (task 1) -->
## Subcomponent A: Linux install

<!-- START_TASK_1 -->
### Task 1: Linux install script

**Verifies:** Setup for linux-port.AC1.1, linux-port.AC2.2, linux-port.AC2.3 (no AC of its own)

**Files:**
- Create: `install.sh` (repo root)

**Implementation:**

Mirror the Windows install layout revealed by `uninstall_modloader.bat` (finding #1),
adapted to Linux. The script takes the game directory as an argument, copies the shipped
files in, overwrites the entry point (preserving `.bak`), and backs up `resource.dat`.
Bootstrap requirement: `set -euo pipefail`; lint with `shellcheck` and `shfmt -d`.

```bash
#!/usr/bin/env bash
# Installs the ITB mod loader into a native Linux Into the Breach directory.
set -euo pipefail

if [[ $# -ne 1 ]]; then
	echo "Usage: $0 <path-to-Into the Breach directory>" >&2
	exit 1
fi

game_dir="$1"
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -f "$game_dir/Breach" ]]; then
	echo "Error: '$game_dir' does not look like an Into the Breach install (no Breach binary)." >&2
	exit 1
fi

# Preserve the vanilla entry point once (do not clobber an existing backup).
if [[ ! -f "$game_dir/scripts/modloader.lua.bak" ]]; then
	cp "$game_dir/scripts/modloader.lua" "$game_dir/scripts/modloader.lua.bak"
fi

# Back up the vanilla archive once, so uninstall can restore a clean game.
if [[ ! -f "$game_dir/resources/resource.dat.bak" ]]; then
	cp "$game_dir/resources/resource.dat" "$game_dir/resources/resource.dat.bak"
fi

# Install the loader scripts, mod/resource overlays, and native artifacts.
cp -r "$repo_dir/scripts/." "$game_dir/scripts/"
cp -r "$repo_dir/mods/." "$game_dir/mods/"
cp -r "$repo_dir/resources/." "$game_dir/resources/"

# Copy every native artifact present at the repo root. In Phase 2 that is itb_io.so and
# ftldat.so; Phases 3–4 add libitbsdl.so and memedit.so, and this same glob ships them.
shopt -s nullglob
so_files=("$repo_dir"/*.so)
if [[ ${#so_files[@]} -eq 0 ]]; then
	echo "Error: no .so artifacts found in $repo_dir (build Phase 1 first)." >&2
	exit 1
fi
cp "${so_files[@]}" "$game_dir/"

echo "Installed. For the in-game UI (Phase 3+), set the Steam launch option:"
echo '  LD_PRELOAD="$PWD/libitbsdl.so" %command%'
```

Notes for the executor:
- **`install.sh` is the single source of truth used by both this phase's smoke test and
  the Phase 5 bundle.** It copies *every* `.so` at the repo root via a glob, so it ships
  two artifacts in Phase 2 and all four once Phases 3–4 stage `libitbsdl.so`/`memedit.so`
  — no Phase 5 edit to this copy logic is needed.
- `cp -r "$repo_dir/scripts/."` overlays the loader's `scripts/mod_loader/` tree and the
  one-line `scripts/modloader.lua` onto the game's `scripts/`, exactly replacing the
  empty vanilla `modloader.lua` (finding #2). The `mods/` overlay carries the Task 5
  smoke mod (and any shipped mods); the `resources/` overlay carries the loader's resource
  files (it contains no `resource.dat`, so it does not clobber the backed-up archive).
- The `.so`s are copied to the game root; the platform module returns `./itb_io.so` /
  `./ftldat.so`, which `dlopen` resolves against the game's cwd (Phase 1, finding #3).
- `libitbsdl.so` and the `LD_PRELOAD` launch option are **not exercised** in Phase 2 — the
  loader boots to `onModsLoaded` and repacks `resource.dat` without the render library
  (which does not exist until Phase 3). `install.sh` ships it automatically once it is
  built; Phase 3/5 use the preload.

**Verification:**
```bash
shellcheck install.sh && shfmt -d install.sh
GAME="/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach"
./install.sh "$GAME"
test -f "$GAME/scripts/modloader.lua.bak"
test -f "$GAME/resources/resource.dat.bak"
grep -q 'require("scripts/mod_loader/__scripts")' "$GAME/scripts/modloader.lua"
test -f "$GAME/itb_io.so" && test -f "$GAME/ftldat.so"
```
Expected: lints clean; all `test`/`grep` checks pass.

**Commit:** `feat: add Linux install script`
<!-- END_TASK_1 -->
<!-- END_SUBCOMPONENT_A -->

<!-- START_SUBCOMPONENT_B (tasks 2-5) -->
## Subcomponent B: Headless boot and data-path verification

> These are operational verifications against the real game. Each states an exact
> observation to confirm. Where a step needs an interactive/graphical session, it is
> called out; the manual portions are captured in `test-requirements.md` for human QA.

<!-- START_TASK_2 -->
### Task 2: Boot the native game to `onModsLoaded`

**Verifies:** linux-port.AC1.1, linux-port.AC1.4

**Files:** none (operational; may add a one-off marker subscriber, see below)

**Implementation & verification:**

1. Add a temporary boot marker so success is unambiguous in the log. In a scratch mod
   (Task 5 creates one) or via the bundled extension, subscribe to `onModsLoaded` and
   `LOG("LINUX_BOOT_OK")`. Alternatively rely on existing end-of-boot log lines — inspect
   `modloader.log` after a run to identify the line the loader already prints once
   `onModsLoaded` has dispatched (`mod_loader.lua:762`).

2. Launch headless:
   ```bash
   GAME="/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach"
   echo 590380 > "$GAME/steam_appid.txt"
   ( cd "$GAME" && xvfb-run -a ./Breach ) &
   # allow boot, then inspect the log the loader writes to the save dir
   ```

3. Confirm boot reached `onModsLoaded`:
   ```bash
   grep -n "LINUX_BOOT_OK\|onModsLoaded\|Successfully loaded" ~/.local/share/IntoTheBreach/modloader.log
   ```
   Expected: the log shows `itb_io.so` and `ftldat.so` loaded successfully (Phase 1 log
   lines) and the boot marker / onModsLoaded milestone, with no Lua traceback.

4. **linux-port.AC1.4 check** — clear error on a missing `.so`: temporarily rename the
   staged library and boot again:
   ```bash
   mv "$GAME/itb_io.so" "$GAME/itb_io.so.hidden"
   ( cd "$GAME" && xvfb-run -a ./Breach ) ; # observe failure
   grep -n "Failed to load ./itb_io.so" ~/.local/share/IntoTheBreach/modloader.log
   mv "$GAME/itb_io.so.hidden" "$GAME/itb_io.so"
   ```
   Expected: the error message names `./itb_io.so` (from the Phase 1 Task 2 change), not a
   bare `package.loadlib` failure.

**Commit:** none required (verification task). If a permanent boot-marker subscriber is
added for CI smoke-testing, commit it as `test: add Linux boot marker`.
<!-- END_TASK_2 -->

<!-- START_TASK_3 -->
### Task 3: Verify `resource.dat` read/repack via `ftldat.so`

**Verifies:** linux-port.AC2.2

**Files:** none (operational)

**Implementation & verification:**

The loader opens `resources/resource.dat` (`modapi/init.lua:75`), writes a
`ModLoaderSignature` entry, and repacks (`modapi/ftldat.lua:119`) during boot. After a
successful boot (Task 2):

1. Confirm the backup was created and the archive was rewritten:
   ```bash
   GAME="/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach"
   test -f "$GAME/resources/resource.dat.bak"          # vanilla backup exists
   # resource.dat mtime should be newer than the .bak after a loader repack
   [ "$GAME/resources/resource.dat" -nt "$GAME/resources/resource.dat.bak" ] && echo "repacked"
   ```
2. Confirm the game reads the repacked archive: the boot completing to `onModsLoaded`
   (Task 2) with mod content present is the functional signal. For a direct archive check,
   confirm the signature entry is present using a short `ftldat` round-trip (the crate's
   own API), or observe `modloader.log` lines from `modapi/ftldat.lua` reporting the
   signature write.

Expected: `resource.dat.bak` exists; `resource.dat` is newer (repacked) and the game
boots reading it. No archive-corruption error in `modloader.log`.

**Commit:** none (verification task).
<!-- END_TASK_3 -->

<!-- START_TASK_4 -->
### Task 4: Verify savedata resolution and persistence (incl. non-ASCII)

**Verifies:** linux-port.AC2.3, linux-port.AC2.4

**Files:** none (operational)

**Implementation & verification:**

1. **Native path (AC2.3):** confirm the loader resolves the save directory to
   `~/.local/share/IntoTheBreach` and persists a profile across restarts:
   ```bash
   ls ~/.local/share/IntoTheBreach/          # profile_*, settings.lua, modloader.log
   grep -n "IntoTheBreach" ~/.local/share/IntoTheBreach/modloader.log
   ```
   Boot once, create/observe a profile, quit, boot again, and confirm the profile and
   `modloader.log` persist (not recreated empty). The Phase 1 `itb_io.so` native branch
   (Task 6 there) is what makes this path resolve.

2. **Non-ASCII (AC2.4):** the resolved save path can contain non-ASCII characters (e.g. a
   non-ASCII Linux username → `/home/José/.local/share/…`). Since the machine's path is
   ASCII, exercise the code path with an overridden `HOME` pointing at a non-ASCII
   directory so `directories`/`itb_io` resolves through it:
   ```bash
   NON_ASCII_HOME="/tmp/claude/itb-café-home"
   mkdir -p "$NON_ASCII_HOME"
   GAME="/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach"
   ( cd "$GAME" && HOME="$NON_ASCII_HOME" xvfb-run -a ./Breach ) &
   grep -n "café" "$NON_ASCII_HOME/.local/share/IntoTheBreach/modloader.log"
   ```
   Expected: the loader resolves and writes under the non-ASCII path without a crash or
   Lua traceback (the log exists and boot proceeds). If a crash surfaces, it is an
   `itb-io-rs` path-handling bug — file it against Phase 1 Task 6 rather than working
   around it here.

**Commit:** none (verification task).
<!-- END_TASK_4 -->

<!-- START_TASK_5 -->
### Task 5: A non-UI mod loads and runs

**Verifies:** linux-port.AC2.1

**Files:**
- Create: `mods/linux_smoke/scripts/init.lua` (a minimal headless test mod)

**Implementation:**

Create a minimal mod with no in-game UI whose `init`/`load` run during boot and whose
`onModsLoaded` subscriber writes a distinctive log line. Match the mod structure the
loader expects (compare against the bundled
`extensions/modLoaderExtensions/scripts/init.lua` and the modding API — read one working
mod init before writing this). Sketch:

```lua
return {
	id = "linux_smoke",
	name = "Linux Smoke Test",
	version = "1.0.0",
	modApiVersion = "2.8.4",
	init = function(self)
		modApi.events.onModsLoaded:subscribe(function()
			LOG("LINUX_SMOKE_MOD_RAN")
		end)
	end,
	load = function(self) end,
}
```

Place it under `mods/` (the loader scans `mods/` — the repo ships only a `put_mods_here`
placeholder there). Enable it in the loader's mod configuration for the test run.

**Verification:**
```bash
grep -n "LINUX_SMOKE_MOD_RAN" ~/.local/share/IntoTheBreach/modloader.log
```
Expected: the line is present, proving a user mod initialized and its `onModsLoaded`
handler ran on native Linux.

**Commit:** `test: add headless Linux smoke-test mod`
<!-- END_TASK_5 -->
<!-- END_SUBCOMPONENT_B -->

<!-- START_SUBCOMPONENT_C (task 6) -->
## Subcomponent C: `libitbboot.so` — LD_PRELOAD bootstrap (REVISED per finding #9)

> **Revised 2026-07-02 (finding #9).** Formerly a throwaway spike; now the real bootstrap
> library that makes native `.so` loading possible at all, because the game's Lua has
> `package.loadlib` compiled out. **Task 6 must be completed and verified BEFORE Tasks
> 2–5** — the loader cannot boot without it. Phase-2 execution order: Task 1 (install) →
> Task 6 (bootstrap) → Tasks 2–5 (boot verification).

<!-- START_TASK_6 -->
### Task 6: `libitbboot.so` — inject a `dlopen`-based `package.loadlib`

**Verifies:** Unblocks linux-port.AC1.1, AC1.3, AC1.4 natively (makes the Phase 1 Lua load
sites functional); retains the Phase-3 de-risking proofs (frame hook fires; `lua_State`
reachable from the interposer). **Runs BEFORE Tasks 2–5** — they cannot pass without it.

**Files:**
- Create: `itbboot/itbboot.c` (the preloaded bootstrap; shipped)
- Create: `itbboot/build.sh` (64-bit `cc -shared -fPIC -O2 … -ldl`)
- Create: `itbboot/gen_lua_addrs.sh` (extracts the game's `lua_State` global and static
  `lua_*`/`luaL_*` addresses from the unstripped binary via `nm`/`objdump`/`gdb` into a
  committed `itbboot/lua_addrs.h`; commit the generator, not just its output)
- Copy artifact into: `/var/home/displacer/Projects/clones/ITB-ModLoader/libitbboot.so`

**Implementation:**

The game embeds PUC Lua 5.1.5 with `package.loadlib` stubbed out ("dynamic libraries not
enabled"); only `dlsym` is imported, not `dlopen`. The binary is non-PIE (`EXEC`),
unstripped, with full `.debug_info`, so the game's Lua state and its static `lua_*`
functions have stable, extractable addresses. Build a preloaded `.so` that installs a
working `package.loadlib` before `scripts/modloader.lua` runs.

1. **Extract addresses** (repeatable, committed generator `gen_lua_addrs.sh`). Recover the
   global/main `lua_State` (or the `luabind::get_main_thread` accessor @ `0xa15988`, which
   takes `L`) and the static `lua_*`/`luaL_*` the injected loader needs
   (`lua_pushcclosure`, `lua_setfield`, `lua_getfield`, `lua_pushstring`, `lua_pushnil`,
   `lua_gettop`/`lua_settop`, `lua_error`, …). Emit `itbboot/lua_addrs.h`. Addresses are
   stable because the binary is non-PIE; the extractor makes them re-derivable if the game
   updates. `luaL_newstate` @ `0x9f536a` is a documented fallback anchor.

2. **Hook a pre-`modloader.lua` moment.** Interpose `fopen`/`fopen64`; on the first open of
   a path under `scripts/`, obtain `L` and register the replacement once. Any hook firing
   after the Lua state exists and before `modloader.lua` executes is acceptable — record
   which one is used.

3. **Inject a PUC-semantics `loadlib`.** Set `package.loadlib` in the game's Lua to a C
   closure that `dlopen`s the path (`RTLD_NOW`), `dlsym`s the requested entry symbol, and
   on success pushes it as a C function (so the existing
   `package.loadlib(name, "luaopen_x")()` sites work unchanged); on failure returns
   `nil, <message>, "open"|"init"` per PUC `loadlib`. Prefer calling the game's own static
   `lua_*` at extracted addresses (zero ABI risk); statically-linked vendored PUC 5.1.5 on
   the game's `L` is the fallback (finding #2 pattern). The bootstrap is its own proof —
   record which route worked.

4. **Retain the Phase-3 proofs.** Keep an `SDL_GL_SwapWindow` interposer (real fetched via
   `dlsym(RTLD_NEXT, …)`) that periodically logs a frame counter and the captured `L` (a
   global shared with the loadlib injector), preserving both spike proofs: the frame hook
   fires, and the game's `lua_State` is reachable from the interposer. Record (finding #8)
   that only `SDL_PollEvent` — not `SDL_PushEvent` — is imported, so Phase 3 interposes
   `SDL_PollEvent` for both observation and injection.

Build:
```bash
#!/usr/bin/env bash
set -euo pipefail
cc -shared -fPIC -O2 -o libitbboot.so itbboot.c -ldl
```

**Verification:**
```bash
GAME="/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach"
cp itbboot/libitbboot.so "$GAME/libitbboot.so"
echo 590380 > "$GAME/steam_appid.txt"
( cd "$GAME" && LD_PRELOAD="$PWD/libitbboot.so" ./Breach ) &   # headless per env (cage / DISPLAY=:0)
# The Phase 1 load sites now succeed instead of the loadlib error:
grep -n "Successfully loaded ./itb_io.so\|Successfully loaded ./ftldat.so" ~/.local/share/IntoTheBreach/log.txt
grep -c "dynamic libraries not enabled" ~/.local/share/IntoTheBreach/log.txt   # expect 0
```
Expected: the injected `loadlib` loads `itb_io.so`/`ftldat.so`; the "dynamic libraries not
enabled" error is gone; the SDL frame-hook line shows a non-null captured `L`. This
directly unblocks Tasks 2–5, whose launch commands all gain
`LD_PRELOAD="$PWD/libitbboot.so"`.

**Confirmed L-capture mechanism (execution note, 2026-07-02 — supersedes the stack-scan
heuristic that segfaulted).** No plain `lua_State*` global exists (DWARF shows only
luabind typeinfo; the game holds `L` inside a C++ member, not statically locatable). Use a
**one-shot inline hook on `lua_getfield` @ `0x9f273c`** instead:
- Its prologue is `55 / 48 89 e5 / 48 83 ec 40 / 48 89 7d d8` — the first parameter is in
  `RDI` at entry (`mov %rdi,-0x28(%rbp)`), i.e. `RDI == L` for every call.
- In the LD_PRELOAD constructor, `mprotect` the page containing `0x9f273c` to `RWX`, save
  the first **12 bytes** (offsets 0–11 end exactly on an instruction boundary), and
  overwrite them with `48 B8 <abs64 stub> FF E0` (`movabs rax,stub; jmp rax`). The stub
  saves `RDI`→`g_L`, restores the original 12 bytes, then `jmp 0x9f273c` to re-run the real
  function unhooked (restore-and-restart; no trampoline relocation). Early Lua init is
  single-threaded, so the restore race is benign.
- **Inject from a libc interposer, not from inside the hook** (reentrancy): the game
  imports both `fopen` and `open`. Interpose `fopen`/`fopen64`/`open`; on the first call
  with `g_L != NULL && !g_injected` for a path under `scripts/`, call `inject(g_L)` and set
  `g_injected`. This runs on the libc stack (outside any `lua_*` call), so calling
  `lua_setfield` is safe.
- `inject(L)`: with fixed-address function pointers (declare them to the `lua_addrs.h`
  addresses), `lua_getfield(L, LUA_GLOBALSINDEX /* -10002 */, "package")`, push the C
  closure via `lua_pushcclosure(L, our_loadlib, 0)`, `lua_setfield(L, -2, "loadlib")`, then
  pop. `our_loadlib(L)`: `lua_tolstring` args → `dlopen(path, RTLD_NOW)` → `dlsym(h, sym)`;
  success `lua_pushcclosure(L, entry, 0); return 1`; failure
  `lua_pushnil; lua_pushstring(msg); lua_pushstring("open"|"init"); return 3`.
- If `mprotect(RWX)` on `.text` is refused by kernel hardening, fall back to locating the
  C++ member holding `L` under gdb at runtime (break at `lua_getfield`, read `$rdi`, find
  the data address that stores it) and bake that address into `lua_addrs.h` for a
  read-only capture. Record which route worked.

**Commit:** `feat: add LD_PRELOAD bootstrap injecting dlopen-based package.loadlib`
<!-- END_TASK_6 -->
<!-- END_SUBCOMPONENT_C -->

---

## Execution finding 2026-07-02 — Tasks 2–5 depend on Phase 3 (`libitbsdl.so`)

Task 1 (`install.sh`) and Task 6 (`libitbboot.so`) are **done and verified**: the LD_PRELOAD
bootstrap's injected `package.loadlib` loads `itb_io.so` on the native game
(`log.txt:64 Successfully loaded ./itb_io.so!`, zero "dynamic libraries not enabled").

However, **finding #10's premise is wrong**: a minimal `sdl` stub does **not** let the loader
boot to `onModsLoaded`. `scripts/mod_loader/__scripts.lua:7` requires `sdlext/extensions`
on the **critical boot path** (before `ui`, `modapi`, `mod_loader`), and that module calls
`sdl.resourceDat("resources/resource.dat")`, `sdl.rgb`, `sdl.rgba` at **require time**. The
loader uses the full `sdl.*` surface (`rect`, `rgb`, `rgba`, `mouse`, `text`, `events`,
`surface`, `resourceDat`, `drawHook`, `font`, …) and `os.*` extensions (`os.mtime`) during
boot — i.e. the entire Windows proxy API. On Windows that API is supplied by the proxy DLL
loaded before scripts run; on Linux the equivalent is **Phase 3's `libitbsdl.so`**. So the
loader cannot reach `onModsLoaded` (nor Task 3's `resource.dat` repack, which flows through
`sdl.resourceDat`) until `libitbsdl.so` exists.

**Consequence (sequencing):** Phase 2 Tasks 2–5 (boot to `onModsLoaded`, `resource.dat`
repack, savedata persistence, non-UI smoke mod) are **operational verifications that require
Phase 3**. They are deferred and run as an integrated boot verification after Phase 3 lands.
The placeholder `scripts/mod_loader/bootstrap/sdl.lua` (commit `9f686b7`) must be removed or
made to defer to the real `sdl` table in Phase 3 — a preloaded `libitbsdl.so` registers
`sdl`/`os` before scripts, so the bootstrap stub should not shadow it.

## Execution finding 2026-07-02 — single shared Lua runtime (supersedes finding #2's vendored approach)

Finding #2 (echoed in Task 6 step 3 and at lines 117, 487–488) assumed each native `.so`
would **statically vendor** its own PUC Lua 5.1 and operate on the game's `L` via
`Lua::init_from_ptr`. That is unsafe: `ftldat.so`, `itb_io.so`, and `libitbsdl.so` each
carried a full Lua 5.1 (`nm` showed `luaH_free`/`sweeplist`/`singlestep`/`luaC_step` as
defined `t` symbols — ~128 internal Lua symbols per `.so`). All operate on the game's **one**
shared `lua_State`. During the `resource.dat` repack, ftldat's own vendored garbage collector
ran against the shared state and swept/freed objects the game allocated through its `l_alloc`
→ glibc heap corruption → `SIGABRT`, just short of `onModsLoaded`.

**Architecture that replaces it: one Lua runtime — the game's — shared by all.**

1. **`libitbboot.so` re-exports the game's Lua C API as absolute dynamic symbols.** The game
   is non-PIE (`ET_EXEC`), unstripped: every `lua_*`/`luaL_*`/`luaopen_*` lives at a fixed
   address in `.symtab`. `itbboot/gen_lua_exports.sh` extracts all 123 of them and emits
   `itbboot/lua_exports.S`, one absolute symbol each:

   ```
   .globl NAME
   .type  NAME, @function
   .set   NAME, 0xADDR
   ```

   Linked into `libitbboot.so` (default visibility) these land in `.dynsym` as **ABS (type
   `A`)** definitions. Because they are absolute, the dynamic linker does **not** add
   libitbboot's load base — the value is the game's real runtime address (no ASLR slide).
   Verified: `nm -D libitbboot.so | grep ' A lua_getfield'` → `9f273c` (identical to the
   inline hook address, so exported symbol and hook refer to the same function). `build.sh`
   regenerates `lua_exports.S` from `$GAME/Breach` when `GAME` is set, else uses the committed
   copy. **Absolute symbols worked; no trampolines were needed.**

2. **The three mod `.so`s reference Lua as undefined, resolved at load.** `ftldat-rs` and
   `itb-io-rs` switch mlua from `["lua51","vendored"]` to `["lua51","module"]`; `libitbsdl`
   drops `lua51_static` from its CMake link (headers stay on the include path). Post-change
   `nm` evidence — zero vendored runtime, undefined `lua_*` instead:
   - `libftldat.so`: 0 `luaH_free`/`sweeplist`/`luaC_step`, 52 undefined `lua_*`/`luaL_*`,
     still exports `luaopen_ftldat`.
   - `libitb_io.so`: 0 vendored, 52 undefined, exports `luaopen_itb_io`.
   - `libitbsdl.so`: 0 vendored, 45 undefined, exports `luaopen_itbsdl` +
     `SDL_GL_SwapWindow`/`SDL_PollEvent`, no `libSDL2` `NEEDED`, no mangled `_Z…lua_`.
   Every undefined `lua_*` in all three is covered by libitbboot's 123 exports (`comm` diff
   empty). At load, `our_loadlib` `dlopen`s each with `RTLD_NOW | RTLD_GLOBAL`, so any
   unresolved `lua_*` would abort the `dlopen`; none did.

3. **Boot result.** With `LD_PRELOAD="./libitbboot.so:./libitbsdl.so"` the game loads all
   three `.so`s (`itbboot.log`: `luaopen_itbsdl`/`luaopen_itb_io`/`luaopen_ftldat` all
   resolved), completes the `resource.dat` repack through ftldat with **no `SIGABRT` and no
   heap corruption**, and runs cleanly into its main loop (`Running Game!` /
   `Generating world…`). The pre-fix build crashed at the ftldat GC during repack; that crash
   is eliminated. `onModsLoaded` did **not** fire in the headless `cage` run: it is gated on
   `scripts/mod_loader/modui/root.lua:250` firing `onInitialLoadingFinished` only once the
   main-menu robot background (`img/main_menus/bg3.png`) `wasDrawn()`, which the headless
   backend did not reach within the run window. This is a UI-render gate in the headless
   environment, not a symbol-resolution or heap failure — no new crash appeared.

## Phase 2 done when

- `install.sh` installs the loader onto the native game (entry point overwritten, `.bak`
  and `resource.dat.bak` preserved); lints clean.
- `libitbboot.so` is `LD_PRELOAD`ed and injects a working `package.loadlib`; the "dynamic
  libraries not enabled" error no longer appears and `itb_io.so`/`ftldat.so` load
  (finding #9; unblocks linux-port.AC1.3).
- The game boots to `onModsLoaded` (headless via `cage`/`DISPLAY=:0`), with
  `itb_io.so`/`ftldat.so` loaded and no Lua traceback (linux-port.AC1.1).
- A missing `.so` yields an error naming `./itb_io.so` (linux-port.AC1.4).
- `resource.dat` is backed up and repacked, and the game reads it (linux-port.AC2.2).
- Savedata resolves to `~/.local/share/IntoTheBreach`, persists across restarts
  (linux-port.AC2.3), and a non-ASCII `HOME` does not crash the loader (linux-port.AC2.4).
- The non-UI smoke mod's `onModsLoaded` handler runs (linux-port.AC2.1).
- The bootstrap confirms the frame hook fires and the captured `lua_State` is reachable,
  fixing the Phase 3 mechanism.
