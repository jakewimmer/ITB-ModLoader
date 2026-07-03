# Native Linux Support — Test Requirements

Maps every acceptance criterion in `docs/design-plans/2026-07-02-linux-port.md`
(`linux-port.AC1.1` … `linux-port.AC7.1`) to a concrete verification. Automated
criteria name the test type, command, and producing phase/task. Human/Manual
criteria state why automation is not available, a step-by-step procedure, and the
exact observation that confirms a pass.

## Summary

The project has no standalone Lua test runner. Lua tests run only in-game through
the `Tests.Runner` console (`scripts/mod_loader/tests/`). CI does not run tests; it
only builds and packages. Consequently most loader-level criteria are verified by
running the real game and observing `~/.local/share/IntoTheBreach/modloader.log` or
the UI. The genuinely automated checks are: the Rust crates' `cargo test`
(`ftldat-rs`, `itb-io-rs`), the offset-derivation tool's `pytest` (fixture-based),
the in-game `platform` testsuite, and shell/workflow linting
(`shellcheck`/`shfmt`/`actionlint`).

**Counts (26 criteria):**

| Verification type | Count | Criteria |
|---|---|---|
| Automated | 6 | AC1.2, AC4.4, AC4.5, AC5.1, AC6.1, AC6.3 |
| Human/Manual | 20 | AC1.1, AC1.3, AC1.4, AC1.5, AC2.1, AC2.2, AC2.3, AC2.4, AC3.1, AC3.2, AC3.3, AC3.4, AC3.5, AC4.1, AC4.2, AC4.3, AC5.2, AC6.2, AC6.4, AC7.1 |

"Automated" means the criterion's core claim is confirmed by a test or lint that
runs without human judgment. Several Human/Manual criteria also carry automated
supporting checks (symbol-export checks with `nm -D`, `cargo test`); those are noted
but do not by themselves confirm the criterion, because the criterion's core claim
is a runtime behavior of the live game.

**Prerequisites for manual testing:**

- **Backed-up game install** (design constraint — savegames untouched):
  `cp -a "$GAME" "$GAME.backup-preloader"`.
- **`xvfb` (`xvfb-run`)** for headless boot and data-path checks (AC1.*, AC2.*).
- **`steam_appid.txt` containing `590380`** in the game directory, so `Breach` can be
  launched directly outside the Steam client.
- **An interactive graphical session** for all UI/console/toast/text/resize checks
  (AC3.*), the fresh-install UI boot (AC6.2), and graceful degradation (AC6.4).
  These cannot run under `xvfb` because they require visual confirmation.
- **A Windows or Proton reference install** with the loader built from this branch,
  for the byte-for-byte parity gate (AC1.5), the FreeType-vs-GDI+ text parity check
  (AC3.4), and the cross-platform test-suite run (AC5.2). Gates the upstream PR.
- **A non-ASCII home directory** (e.g. `/tmp/claude/itb-café-home`) for AC2.4.
- **Initialized submodules** (`git submodule update --init --recursive`) so
  `memedit`, `modApiExt`, and `easyEdit` are present for AC4.*.

**Common shell variable used below:**

```bash
GAME="/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach"
LOG="$HOME/.local/share/IntoTheBreach/modloader.log"
```

---

## AC1 — The loader boots on the native Linux game

### linux-port.AC1.1

> **Success:** Launching the native game with the loader installed runs the loader
> to `onModsLoaded` without error.

- **Verification type:** Human/Manual.
- **Why not automated:** Reaching `onModsLoaded` requires the real game process to
  execute the full bootstrap and mod-init path. There is no standalone Lua runner;
  the only signal is the log the running game writes. The run is headless-capable
  under `xvfb`, but it is still a live-process observation, not a test assertion.
- **Produced by:** Phase 2, Task 2.
- **Procedure:**
  1. Install the loader: `./install.sh "$GAME"`.
  2. `echo 590380 > "$GAME/steam_appid.txt"`.
  3. Launch headless: `( cd "$GAME" && xvfb-run -a ./Breach ) &`; allow boot to
     complete, then stop the process.
  4. Inspect the log:
     `grep -nE "onModsLoaded|Successfully loaded|LINUX_BOOT_OK" "$LOG"`.
- **Pass observation:** The log shows `itb_io.so` and `ftldat.so` loaded
  successfully and the end-of-boot `onModsLoaded` milestone (or the `LINUX_BOOT_OK`
  marker from a temporary subscriber), with **no Lua traceback** anywhere in the log.

### linux-port.AC1.2

> **Success:** The platform module returns `.so` names on Linux and `.dll` names on
> Windows.

- **Verification type:** Automated (in-game testsuite).
- **Test type:** In-game `Tests.Runner` testsuite.
- **Test file / command:** `scripts/mod_loader/tests/platform.lua`, registered in
  `scripts/mod_loader/tests/__scripts.lua` and `tests/main.lua`. Run from the in-game
  test console (`scripts/mod_loader/modui/tests_console.lua`). The suite asserts
  `Platform.nativeLibrary("itb_io")` is `itb_io.dll` on Windows and `./itb_io.so` on
  Linux (likewise `ftldat`/`itbsdl`), branching on `Platform.name` so the same suite
  passes on both hosts.
- **Produced by:** Phase 1, Task 4.
- **Note:** The suite is a real automated assertion, but it is triggered from the
  in-game console (there is no CI test run). A green `platform` suite on the current
  host confirms this criterion for that host; running it on both a Windows/Proton
  reference and native Linux confirms both branches.

### linux-port.AC1.3

> **Success:** `ftldat.so` and `itb_io.so` load via `package.loadlib` and register
> their `luaopen_*` entries.

- **Verification type:** Human/Manual (with automated supporting checks).
- **Why not automated:** "Register their `luaopen_*` entries" means the game calls
  `luaopen_ftldat`/`luaopen_itb_io` against its live `lua_State` and the resulting
  globals appear. That registration only happens inside the running game; `cargo
  test` and `nm -D` confirm the artifact builds and exports the symbol, but not that
  the live loader registered it.
- **Automated supporting checks:**
  - `cd /var/home/displacer/Projects/clones/ftldat-rs && cargo test`
  - `cd /var/home/displacer/Projects/clones/itb-io-rs && cargo test`
  - `nm -D "$GAME/ftldat.so" | grep luaopen_ftldat`
  - `nm -D "$GAME/itb_io.so" | grep luaopen_itb_io`
- **Produced by:** Phase 1, Tasks 5–6 (artifacts + `cargo test`); registration
  exercised at boot in Phase 2, Task 2.
- **Procedure:**
  1. Run the two `cargo test` commands above; both must pass.
  2. Run the two `nm -D` commands; each must list its `luaopen_*` symbol.
  3. Boot the game headless (AC1.1 procedure) and inspect the log:
     `grep -nE "Successfully loaded ./itb_io.so|Successfully loaded ./ftldat.so" "$LOG"`.
- **Pass observation:** `cargo test` green in both crates; both `luaopen_*` symbols
  are exported; the log shows both `.so`s loaded successfully with no load error.

### linux-port.AC1.4

> **Failure:** A missing native `.so` produces a clear error naming the missing
> library, not a bare `package.loadlib` failure.

- **Verification type:** Human/Manual (with automated supporting check).
- **Why not automated:** The clear-error message is produced by the `catch` block at
  the live `package.loadlib` site during boot. The in-game `platform` suite asserts
  that an unknown logical name raises (supporting), but the "missing file names the
  file" behavior is only observable when the loader actually attempts the load.
- **Automated supporting check:** The `platform` testsuite (Phase 1, Task 4) wraps
  `Platform.nativeLibrary("unknown")` in `pcall` and asserts it errors.
- **Produced by:** Phase 1, Task 2 (error message wiring); Phase 2, Task 2
  (observation).
- **Procedure:**
  1. Hide the staged library: `mv "$GAME/itb_io.so" "$GAME/itb_io.so.hidden"`.
  2. Boot headless: `( cd "$GAME" && xvfb-run -a ./Breach )`; let it fail.
  3. `grep -n "Failed to load ./itb_io.so" "$LOG"`.
  4. Restore: `mv "$GAME/itb_io.so.hidden" "$GAME/itb_io.so"`.
- **Pass observation:** The log contains an error naming `./itb_io.so`
  (e.g. `Failed to load ./itb_io.so: ...`), not a bare `package.loadlib` stack
  message.

### linux-port.AC1.5

> **Regression:** On Windows the loader still loads the `.dll`s and behaves
> byte-for-byte as before.

- **Verification type:** Human/Manual.
- **Why not automated:** No Windows CI exists and no Windows reference install is
  set up. Byte-for-byte behavior parity is confirmed by running the loader on an
  actual Windows or Proton install and observing that behavior is unchanged. This is
  a gating step for the upstream PR.
- **Produced by:** Phase 5, Task 7 (Windows-regression QA gate). The change is
  additive and guarded (`Platform.name == "windows"` resolves the unchanged `.dll`
  names) — implemented in Phase 1.
- **Procedure:**
  1. On a Windows or Proton install with the loader built from this branch, launch
     the game.
  2. Confirm the loader loads `itb_io.dll` and `ftldat.dll` (loader log lines) and
     boots to `onModsLoaded`.
  3. Exercise the mod-config UI and confirm behavior matches a pre-change baseline.
- **Pass observation:** The loader loads the `.dll`s, boots to `onModsLoaded`, and
  the UI behaves as before with no new errors. Recorded in this file's manual-QA log.

---

## AC2 — Mods load and resource.dat/savedata work

### linux-port.AC2.1

> **Success:** A mod with no in-game UI initializes and runs on native Linux.

- **Verification type:** Human/Manual.
- **Why not automated:** Mod init/load and the `onModsLoaded` subscriber run only
  inside the live loader boot; the confirming signal is a log line the running mod
  writes. No standalone runner exists.
- **Produced by:** Phase 2, Task 5 (`mods/linux_smoke/scripts/init.lua`).
- **Procedure:**
  1. Ensure `mods/linux_smoke` is installed and enabled in the loader config.
  2. Boot headless (AC1.1 procedure).
  3. `grep -n "LINUX_SMOKE_MOD_RAN" "$LOG"`.
- **Pass observation:** The line `LINUX_SMOKE_MOD_RAN` is present, proving a user mod
  initialized and its `onModsLoaded` handler ran on native Linux.

### linux-port.AC2.2

> **Success:** The loader reads and repacks `resource.dat` via `ftldat.so`; the game
> reads the repacked archive.

- **Verification type:** Human/Manual.
- **Why not automated:** Repacking happens during live boot (`modapi/init.lua:75`,
  `modapi/ftldat.lua:119`), and "the game reads the repacked archive" is confirmed by
  the game booting successfully against it. Filesystem timestamps and the backup file
  are inspected after a real run.
- **Produced by:** Phase 2, Task 3.
- **Procedure:**
  1. Boot headless (AC1.1 procedure) so the loader writes the `ModLoaderSignature`
     entry and repacks.
  2. `test -f "$GAME/resources/resource.dat.bak"` (vanilla backup exists).
  3. `[ "$GAME/resources/resource.dat" -nt "$GAME/resources/resource.dat.bak" ] && echo repacked`.
  4. Confirm boot reached `onModsLoaded` (AC1.1) with mod content present, and no
     archive-corruption error in the log.
- **Pass observation:** `resource.dat.bak` exists; `resource.dat` is newer than the
  backup (repacked); the game boots reading it with no corruption error in the log.

### linux-port.AC2.3

> **Success:** Savedata resolves to the native-Linux location the game actually uses;
> profiles/saves persist across restarts.

- **Verification type:** Human/Manual (with automated supporting check).
- **Why not automated:** "Persist across restarts" requires two live boots of the
  game with a profile written between them. `cargo test` in `itb-io-rs` verifies the
  path resolves to a valid location, but not cross-restart persistence in the real
  game.
- **Automated supporting check:** `cd /var/home/displacer/Projects/clones/itb-io-rs
  && cargo test` (`dir_returned_by_save_data_directory_should_be_valid_save_data_location`).
- **Produced by:** Phase 1, Task 6 (native XDG branch); Phase 2, Task 4 (observation).
- **Procedure:**
  1. Boot headless once; create or observe a profile.
  2. `ls ~/.local/share/IntoTheBreach/` — confirm `profile_*`, `settings.lua`,
     `modloader.log`.
  3. Quit, boot again, and re-list the directory.
- **Pass observation:** The save directory is `~/.local/share/IntoTheBreach`, and the
  profile and `modloader.log` persist across the restart (not recreated empty).

### linux-port.AC2.4

> **Failure:** A savedata path containing non-ASCII characters does not crash the
> loader.

- **Verification type:** Human/Manual.
- **Why not automated:** The path-encoding behavior must be exercised end-to-end
  through `itb-io-rs` and the loader in a live boot; the confirming signal is that the
  game writes its log under the non-ASCII path without a crash.
- **Produced by:** Phase 2, Task 4.
- **Procedure:**
  1. `NON_ASCII_HOME="/tmp/claude/itb-café-home"; mkdir -p "$NON_ASCII_HOME"`.
  2. `( cd "$GAME" && HOME="$NON_ASCII_HOME" xvfb-run -a ./Breach ) &`; let it boot,
     then stop it.
  3. `grep -n "café" "$NON_ASCII_HOME/.local/share/IntoTheBreach/modloader.log"`.
- **Pass observation:** The log file exists under the non-ASCII path and boot
  proceeds with no crash or Lua traceback. (A crash here is an `itb-io-rs`
  path-handling bug against Phase 1, Task 6.)

---

## AC3 — The in-game loader UI renders and takes input

> All AC3 criteria require an interactive graphical session and visual confirmation.
> They cannot run under `xvfb` (there is no display to observe) and the `sdl.*`
> surface has no in-game automated tests (it needs the live GL context and window).
> All are produced/verified by Phase 3, Task 6.

### linux-port.AC3.1

> **Success:** `sdl.drawHook` fires `onFrameDrawStart`/`onFrameDrawn` each frame; the
> mod-config menu renders.

- **Verification type:** Human/Manual.
- **Why not automated:** Frame rendering and menu drawing require the live OpenGL
  context and a visible window; there is no headless or assertion-based path.
- **Produced by:** Phase 3, Task 6.
- **Procedure:**
  1. Stage `libitbsdl.so` into `$GAME` and launch interactively:
     `( cd "$GAME" && LD_PRELOAD="$PWD/libitbsdl.so" ./Breach )`.
  2. Open the mod-config menu.
  3. Add a temporary frame counter `LOG` in an `onFrameDrawn` subscriber and watch
     `modloader.log`.
- **Pass observation:** The mod-config menu renders on screen, and the frame-counter
  log line advances every frame (confirming `onFrameDrawStart`/`onFrameDrawn` fire).

### linux-port.AC3.2

> **Success:** The console toggles on backquote via `sdl.eventHook` and accepts typed
> input.

- **Verification type:** Human/Manual.
- **Why not automated:** Requires live keyboard input through the `SDL_PollEvent`
  interceptor plus `SDL_StartTextInput`, and visual confirmation of the console and
  typed characters.
- **Produced by:** Phase 3, Task 6.
- **Procedure:**
  1. Launch interactively with the preload (AC3.1 step 1).
  2. Press the backquote (`` ` ``) key.
  3. Type several characters.
- **Pass observation:** The console opens on backquote and closes on a second press;
  typed characters appear in the console input line.

### linux-port.AC3.3

> **Success:** Toast notifications render.

- **Verification type:** Human/Manual.
- **Why not automated:** Toast rendering is a visual event on the live GL surface.
- **Produced by:** Phase 3, Task 6.
- **Procedure:**
  1. Launch interactively with the preload.
  2. Trigger an action that raises a toast notification.
- **Pass observation:** The toast notification appears on screen and then dismisses.

### linux-port.AC3.4

> **Success:** UI text renders legibly via FreeType (visual parity check against
> Windows).

- **Verification type:** Human/Manual.
- **Why not automated:** Legibility and layout parity are visual judgments;
  FreeType metrics differ from GDI+, so this requires side-by-side comparison against
  a Windows/Proton reference. No automated metric captures "legible."
- **Produced by:** Phase 3, Task 6 (parity fixes land in Phase 3, Task 4).
- **Procedure:**
  1. Launch interactively with the preload; open a text-heavy loader screen.
  2. Launch the same screen on a Windows or Proton reference.
  3. Compare line height, glyph spacing, and outline against the reference.
- **Pass observation:** UI text is legible on Linux and matches the Windows/Proton
  reference layout within the adjustments noted in Phase 3, Task 4 (no clipped,
  overlapping, or missing glyphs).

### linux-port.AC3.5

> **Edge:** Resizing the game window repositions the loader UI correctly
> (`onGameWindowResized`).

- **Verification type:** Human/Manual.
- **Why not automated:** Window resizing and correct UI repositioning are visual and
  require a live resizable window.
- **Produced by:** Phase 3, Task 6.
- **Procedure:**
  1. Launch interactively with the preload.
  2. Resize the game window (drag a corner, or toggle windowed/fullscreen).
- **Pass observation:** The loader UI repositions to the new dimensions correctly
  (`onGameWindowResized` dispatched at `root.lua:269`), with no misplaced or
  off-screen elements.

---

## AC4 — memedit works at full parity

### linux-port.AC4.1

> **Success:** `memedit.so` loads via `package.loadlib` and exposes
> `luaopen_memedit`.

- **Verification type:** Human/Manual (with automated supporting check).
- **Why not automated:** "Exposes `luaopen_memedit`" is checkable with `nm -D`
  (automated), but "loads via `package.loadlib`" is an in-game load through
  `memedit.lua` against the live `lua_State`, confirmed only at runtime.
- **Automated supporting check:**
  `nm -D /var/home/displacer/Projects/clones/ITB-ModLoader/memedit.so | grep luaopen_memedit`.
- **Produced by:** Phase 4, Task 1 (build + export); Phase 4, Task 3 (in-game load).
- **Procedure:**
  1. Run the `nm -D` check; `luaopen_memedit` must be exported.
  2. Boot the game with memedit enabled and inspect the log for a successful memedit
     load with no load error.
- **Pass observation:** `luaopen_memedit` is exported; the running loader loads
  `memedit.so` and the memedit surface is available in-game with no error in the log.

### linux-port.AC4.2

> **Success:** memedit reads and writes game-object fields correctly using the Linux
> offset table.

- **Verification type:** Human/Manual.
- **Why not automated:** Correct reads/writes require live game objects in memory;
  a correct offset round-trips a field value, a wrong offset reads garbage. This is
  confirmed by exercising memedit against real game state in-game. Only the fields
  present (non-`TODO`) in `__addresses_linux.lua` are in scope; fields not yet
  reverse-engineered fall back to Proton (AC7).
- **Produced by:** Phase 4, Task 3.
- **Procedure:**
  1. Boot the game with memedit enabled and reach in-game state with a pawn present.
  2. Using the memedit/easyEdit in-game surface, read a pawn's `MaxHealth`.
  3. Write a new value, then read it back; observe the in-game effect.
- **Pass observation:** For each validated field (e.g. Pawn `MaxHealth`, `Team`,
  `Fire`, `Acid`), the write takes effect and reads back the written value — a clean
  round-trip on several fields validates the derived offsets.

### linux-port.AC4.3

> **Success:** easyEdit (which builds on memedit) functions on native Linux.

- **Verification type:** Human/Manual.
- **Why not automated:** easyEdit is a live in-game authoring UI; functioning is
  confirmed by opening it and using a tool. (Investigation found easyEdit has no
  direct memedit dependency, so it needs only Phases 1–3.)
- **Produced by:** Phase 4, Task 4.
- **Procedure:**
  1. `git submodule update --init --recursive` so easyEdit is present.
  2. Enable easyEdit, boot the native game with the Phase 3 preload in place.
  3. Open the easyEdit menu and use a representative tool.
- **Pass observation:** The easyEdit menu opens, a representative tool works, and
  `modloader.log` shows easyEdit initialized without error.

### linux-port.AC4.4

> **Success:** The offset generator regenerates the table from the binary's DWARF
> `debug_info`.

- **Verification type:** Automated (with manual full-table completion).
- **Reinterpretation (evidence-backed, per Phase 4 verdict):** The game's own struct
  layouts are **absent from DWARF** — `.debug_str` contains none of
  `Pawn`/`Board`/`Tile`/`Weapon`/`SpaceDamage`, and all DWARF `structure_type` DIEs
  belong to statically-linked third-party libraries. The offset table is therefore
  derived from the binary's **luabind `def_readwrite` registrations + symbol table +
  gdb-assisted reverse engineering**, not DWARF. The tool automates the derivable
  portion (the 22 luabind-exposed fields) and marks the rest `TODO`; completing those
  `TODO`s is manual RE.
- **Test type:** `pytest` (fixture-based) for the extraction logic; a tool run
  against the real binary for the derivable subset.
- **Test file / command:**
  - `cd /var/home/displacer/Projects/clones/memedit/tools/derive_offsets && uv run pytest -q`
    (feeds a small fixture ELF with a known luabind registration and asserts the
    extracted offset; does **not** parse the 355 MB game binary in unit tests).
  - `uv run derive_offsets.py "$GAME/Breach" -o /tmp/claude/__addresses_linux.lua`
    then `ruff check . && ty check`.
- **Produced by:** Phase 4, Task 2.
- **Pass criteria:** `pytest` passes; the 22 luabind-exposed fields are derived with
  real offsets against the binary; unexposed fields appear as explicit `TODO`s; lints
  and types are clean. Full completion of all ~99 offsets is ongoing manual RE (ships
  a validated subset with Proton as the fallback for the remainder).

### linux-port.AC4.5

> **Failure:** A binary whose symbols the generator cannot resolve fails with a
> diagnostic identifying the missing symbol, not a silent wrong offset.

- **Verification type:** Automated.
- **Test type:** `pytest` (fixture-based).
- **Test file / command:**
  `cd /var/home/displacer/Projects/clones/memedit/tools/derive_offsets && uv run pytest -q`.
  A fixture with a deliberately missing anchor symbol asserts the tool exits non-zero
  with an explicit message naming the missing symbol/field. Every field is either
  derived, an explicit `TODO`, or a named hard error — never a guessed or zero offset.
- **Produced by:** Phase 4, Task 2.
- **Pass criteria:** The missing-anchor fixture produces a non-zero exit and an error
  message naming the unresolved symbol; the test asserting this passes.

---

## AC5 — Single upstreamable codebase

### linux-port.AC5.1

> **Success:** The two hardcoded library names route through the platform module; no
> `.dll` literal remains in the load path.

- **Verification type:** Automated (structural search).
- **Test type:** Shell / `ripgrep` structural check across the three native-load
  sites.
- **Test file / command:**
  ```bash
  rg -n '"\S*\.dll"' \
    scripts/mod_loader/bootstrap/itb_io.lua \
    scripts/mod_loader/ftldat-rs/ftldat.lua \
    scripts/mod_loader/extensions/modLoaderExtensions/mods/memedit/memedit.lua
  ```
  Expected: no match in any load path (the memedit site keeps a Windows-only
  `path.."memedit.dll"` that is guarded by `Platform.name == "windows"`; confirm the
  Linux branch loads via `Platform.nativeLibrary("memedit")`).
- **Produced by:** Phase 1, Tasks 2–3 (the two core sites) and Phase 4, Task 3 (the
  third memedit site). AC5.1 is only fully closed once Phase 4, Task 3 lands.
- **Pass criteria:** No unguarded `.dll` string literal remains in any of the three
  load sites; all resolve their filename through `Platform.nativeLibrary`.

### linux-port.AC5.2

> **Regression:** The existing in-game test suites (`tests/`) pass on both Windows
> and Linux.

- **Verification type:** Human/Manual (in-game testsuite run on both platforms).
- **Why not automated:** The suites run only through the in-game `Tests.Runner`
  console — there is no CI test run. Confirming "on both Windows and Linux" requires a
  Windows or Proton reference run in addition to the native Linux run. This gates the
  upstream PR.
- **Produced by:** Phase 1, Task 4 (adds the `platform` suite); Phase 5, Task 7
  (Windows/Proton reference run).
- **Procedure:**
  1. On native Linux, boot the loader, open the in-game test console
     (`scripts/mod_loader/modui/tests_console.lua`), and run all `tests/` suites
     including `platform`.
  2. On a Windows or Proton reference install with the loader from this branch, run
     the same suites.
- **Pass observation:** Every suite passes on both platforms with no failed
  assertion. Both runs recorded in this file's manual-QA log.

---

## AC6 — Packaging, deployment, and uninstall

### linux-port.AC6.1

> **Success:** CI produces a Linux bundle (`.so`s, `scripts/`, launcher wrapper)
> alongside the Windows zip.

- **Verification type:** Automated (CI + lint), with a manual artifact-presence
  confirmation.
- **Test type:** GitHub Actions workflow + shell/workflow linting.
- **Test file / command:**
  - `actionlint .github/workflows/package.yml` (validate the added `package-linux`
    job).
  - `shellcheck _release_linux.sh && shfmt -d _release_linux.sh` (bundle-assembly
    script).
  - Trigger the workflow (`workflow_dispatch`); the `package-linux` job builds the
    four `.so`s, runs `_release_linux.sh`, and uploads with
    `if-no-files-found: error` (so an empty bundle fails the job automatically).
- **Produced by:** Phase 5, Task 1 (`_release_linux.sh`) and Phase 5, Task 4 (CI job).
- **Pass criteria:** `actionlint`/`shellcheck`/`shfmt` clean; the dispatched workflow
  succeeds and uploads both the Windows zip and the `ITB-ModLoader-Linux-*` bundle as
  artifacts. Manual confirmation: the Linux artifact is present in the run's artifact
  list and contains the four `.so`s, `scripts/`, `mods/`, `resources/`, `install.sh`,
  `uninstall.sh`, and the launcher README.

### linux-port.AC6.2

> **Success:** Installing the bundle and setting the `LD_PRELOAD` launch option boots
> the loader on a fresh native install.

- **Verification type:** Human/Manual.
- **Why not automated:** Confirming the loader UI boots requires an interactive
  graphical session on a clean game copy; there is no assertion-based path for "the UI
  renders."
- **Produced by:** Phase 5, Task 3 (launcher docs) and Phase 5, Task 6 (fresh-install
  end-to-end).
- **Procedure:**
  1. Start from a clean copy of the native game (restore from the Phase 2 backup or a
     second install).
  2. Unpack the CI Linux bundle; run `./install.sh "$GAME"`.
  3. Set the Steam launch option `LD_PRELOAD="$PWD/libitbsdl.so" %command%` (or launch
     directly with the same preload), and start the game interactively.
- **Pass observation:** The loader boots and the in-game UI renders on a fresh
  install.

### linux-port.AC6.3

> **Success:** Uninstall restores the empty `scripts/modloader.lua`, restores
> `resource.dat.bak`, and removes the `.so`s, leaving a vanilla game.

- **Verification type:** Automated (shell file-state assertions), with a manual
  launch-stock confirmation.
- **Test type:** Shell script assertions plus `shellcheck`/`shfmt` on the uninstall
  script.
- **Test file / command:**
  ```bash
  shellcheck uninstall.sh && shfmt -d uninstall.sh
  ./install.sh "$GAME" && ./uninstall.sh "$GAME"
  test ! -f "$GAME/itb_io.so" && test ! -f "$GAME/libitbsdl.so"
  test ! -s "$GAME/scripts/modloader.lua"       # restored to empty (0 bytes)
  test ! -f "$GAME/resources/resource.dat.bak"  # restored (no leftover .bak)
  ```
- **Produced by:** Phase 5, Task 2.
- **Pass criteria:** Lints clean; after install+uninstall the `.so`s are gone,
  `modloader.lua` is empty (0 bytes), and `resource.dat` is restored with no leftover
  `.bak`. Manual confirmation: launching the game (without the preload) runs the stock
  game.

### linux-port.AC6.4

> **Failure:** Running the game without the `LD_PRELOAD` option degrades gracefully
> (loader either self-reports the missing preload or the game runs vanilla) rather
> than crashing.

- **Verification type:** Human/Manual.
- **Why not automated:** Graceful degradation is a runtime behavior of the live game
  with the loader installed but no preload; the confirming signal is a log line and
  the absence of a crash, observed from a real run.
- **Produced by:** Phase 5, Task 3.
- **Procedure:**
  1. With the loader installed but **no** `LD_PRELOAD` set, launch the game:
     `( cd "$GAME" && ./Breach )`.
  2. `grep -n "not LD_PRELOADed" "$LOG"`.
- **Pass observation:** The game runs (no crash), and the log carries the
  missing-preload notice (e.g. `libitbsdl.so is not LD_PRELOADed; ... UI disabled`).
  Non-UI mods still load.

---

## AC7 — Proton baseline documented

### linux-port.AC7.1

> **Success:** Documentation states the native-vs-Proton trade-off and keeps Proton
> as the fallback path.

- **Verification type:** Human/Manual (documentation review).
- **Why not automated:** This is a documentation-content requirement; confirming the
  trade-off is stated and Proton is kept as the fallback is an editorial review, not a
  test.
- **Produced by:** Phase 5, Task 5 (`docs/linux-proton-vs-native.md`, linked from
  `README.md`).
- **Procedure:**
  1. Open `docs/linux-proton-vs-native.md`.
  2. Confirm it states the Proton trade-off (WineHQ Platinum, no native code to
     maintain) and the native trade-off (no Wine layer, at the cost of the render/input
     library, the savedata branch, and per-game-version memedit offset RE).
  3. Confirm it names Proton as the documented fallback — including for memedit fields
     not yet reverse-engineered — and that `README.md` links the doc.
- **Pass observation:** The doc states the native-vs-Proton trade-off and keeps Proton
  as the fallback; `README.md` links it.

---

## Manual-QA log

Record each manual/reference run here as it is performed (date, platform, result).

| Date | Criterion | Platform | Result | Notes |
|---|---|---|---|---|
| | | | | |
