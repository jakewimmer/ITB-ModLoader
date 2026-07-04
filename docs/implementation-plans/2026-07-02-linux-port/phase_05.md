# Native Linux Support — Phase 5: Packaging, CI, and uninstall

**Goal:** A reproducible Linux release bundle is produced by CI alongside the Windows zip;
installing it and setting the `LD_PRELOAD` launch option boots the loader on a fresh native
install; uninstall restores a vanilla game; and the Proton fallback is documented.

**Architecture:** A Linux release-assembly script mirrors `_release.bat` but ships plain
`.so`s plus a launcher/README (no proxy-DLL rename dance — Linux injects via `LD_PRELOAD`).
A Linux CI job builds the Rust and C/C++ artifacts and assembles the bundle. A Linux
`uninstall.sh` mirrors `uninstall_modloader.bat`.

**Tech Stack:** Bash, GitHub Actions (`ubuntu-latest`), the Phase 1–4 build toolchains
(cargo, cmake/SDL2/FreeType), the Steam launch-option mechanism.

**Scope:** Phase 5 of 5. Depends on Phases 1–4 (all artifacts exist and build).

**Codebase verified:** 2026-07-02 (read `_release.bat`, `package.bat`,
`uninstall_modloader.bat`, `.github/workflows/package.yml`).

---

## Codebase verification findings (read before implementing)

1. **Windows packaging** (`_release.bat`): assembles `release/` with the proxy + real DLLs
   (`lua5.1.dll` + `lua5.1-original.dll`, `SDL2.dll` + `SDL2-original.dll`, `opengl32.dll`),
   `ftldat.dll`, `itb_io.dll`, `README.md`→`MODLOADER_README.txt`,
   `uninstall_modloader.bat`, and the `mods/`, `scripts/`, `resources/` trees. `package.bat`
   calls `_release.bat` then zips to `ITB-ModLoader-#.#.#.zip`.

2. **Windows uninstall** (`uninstall_modloader.bat`): deletes loader files, then restores
   `lua5.1-original.dll`→`lua5.1.dll`, `SDL2-original.dll`→`SDL2.dll`,
   `resources/resource.dat.bak`→`resource.dat`, `scripts/modloader.lua.bak`→`modloader.lua`.
   **The `-original` rename is a Windows proxy-DLL mechanism** (the loader replaces
   `lua5.1.dll`/`SDL2.dll` with proxies and keeps the originals). **Linux has no
   equivalent** — it injects via `LD_PRELOAD=libitbsdl.so` and loads `.so`s by name, so the
   Linux bundle ships no `-original` files and the uninstall has no rename-back step for
   them (only `modloader.lua.bak` and `resource.dat.bak` are restored — Phase 2 install.sh
   already creates those).

3. **CI** (`.github/workflows/package.yml`): a single `workflow_dispatch` job on
   `windows-latest`, `actions/checkout@v4` (submodules recursive), `CALL _release.bat`,
   `actions/upload-artifact@v4` of `release/*`. No tests run (Phase 1 finding). Match this
   style (tag-pinned actions, `workflow_dispatch`) for the Linux job to keep the PR
   cohesive and upstreamable; SHA-pinning/`zizmor` hardening is noted as optional, not
   imposed on this repo's convention.

4. **Linux artifacts to ship** (from Phases 1–4): `itb_io.so`, `ftldat.so`,
   `libitbsdl.so`, `memedit.so` (all staged at the repo root by their phases), plus
   `install.sh` (Phase 2), the `scripts/`, `mods/`, `resources/` trees, and a
   launcher/README. No `SDL2`/`lua5.1`/`opengl32` `.so`s are shipped — the game already
   dynamically links the system SDL2/GL, and `libitbsdl.so` links its own SDL2/FreeType.

---

## Acceptance Criteria Coverage

### linux-port.AC6: Packaging, deployment, and uninstall
- **linux-port.AC6.1 Success:** CI produces a Linux bundle (`.so`s, `scripts/`, launcher wrapper) alongside the Windows zip.
- **linux-port.AC6.2 Success:** Installing the bundle and setting the `LD_PRELOAD` launch option boots the loader on a fresh native install.
- **linux-port.AC6.3 Success:** Uninstall restores the empty `scripts/modloader.lua`, restores `resource.dat.bak`, and removes the `.so`s, leaving a vanilla game.
- **linux-port.AC6.4 Failure:** Running the game without the `LD_PRELOAD` option degrades gracefully (loader either self-reports the missing preload or the game runs vanilla) rather than crashing.

### linux-port.AC7: Proton baseline documented
- **linux-port.AC7.1 Success:** Documentation states the native-vs-Proton trade-off and keeps Proton as the fallback path.

### linux-port.AC5: Single upstreamable codebase (Windows-parity gate)
- **linux-port.AC1.5 Regression:** On Windows the loader still loads the `.dll`s and behaves byte-for-byte as before.
- **linux-port.AC5.2 Regression:** The existing in-game test suites (`tests/`) pass on both Windows and Linux.

> AC1.5/AC5.2 are *implemented* in Phase 1 (guarded, additive changes); Task 7 here is the
> manual-QA **gate** that actually runs a Windows/Proton reference to confirm them before
> the upstream PR.

---

<!-- START_SUBCOMPONENT_A (tasks 1-2) -->
## Subcomponent A: Linux release assembly and uninstall

<!-- START_TASK_1 -->
### Task 1: Linux release-assembly script

**Verifies:** Setup for linux-port.AC6.1

**Files:**
- Create: `_release_linux.sh` (repo root; mirrors `_release.bat`)

**Implementation:**

Assemble a `release-linux/` directory with the Linux artifacts and trees. `set -euo
pipefail`; lint with `shellcheck`/`shfmt`.

```bash
#!/usr/bin/env bash
# Shared release-assembly for the Linux bundle (mirrors _release.bat).
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$repo_dir/release-linux"

rm -rf "$out"
mkdir -p "$out"

# Native artifacts (staged at repo root by Phases 1–4).
for so in itb_io.so ftldat.so libitbsdl.so memedit.so; do
	cp "$repo_dir/$so" "$out/$so"
done

# Loader trees and docs.
cp -r "$repo_dir/scripts" "$out/scripts"
cp -r "$repo_dir/mods" "$out/mods"
cp -r "$repo_dir/resources" "$out/resources"
cp "$repo_dir/README.md" "$out/MODLOADER_README.txt"
cp "$repo_dir/install.sh" "$out/install.sh"
cp "$repo_dir/uninstall.sh" "$out/uninstall.sh"     # Task 2
cp "$repo_dir/LAUNCHER_README.md" "$out/LAUNCHER_README.md"  # Task 3

echo "Assembled Linux bundle at $out"
```

**Verification:**
```bash
shellcheck _release_linux.sh && shfmt -d _release_linux.sh
./_release_linux.sh
ls release-linux/  # the four .so's, scripts/, mods/, resources/, install.sh, uninstall.sh, READMEs
```
Expected: lints clean; bundle contains all artifacts.

**Commit:** `build: add Linux release-assembly script`
<!-- END_TASK_1 -->

<!-- START_TASK_2 -->
### Task 2: Linux uninstall script

**Verifies:** linux-port.AC6.3

**Files:**
- Create: `uninstall.sh` (repo root)

**Implementation:**

Mirror `uninstall_modloader.bat` minus the Windows proxy-DLL rename dance (finding #2).
Restore `scripts/modloader.lua` from `.bak` (back to empty), restore `resource.dat.bak`,
and remove the `.so`s and loader trees. Takes the game directory as an argument.

```bash
#!/usr/bin/env bash
# Removes the ITB mod loader from a native Linux Into the Breach directory.
set -euo pipefail

if [[ $# -ne 1 ]]; then
	echo "Usage: $0 <path-to-Into the Breach directory>" >&2
	exit 1
fi
game_dir="$1"

# Remove native artifacts and logs.
for f in itb_io.so ftldat.so libitbsdl.so memedit.so modloader.log MODLOADER_README.txt; do
	rm -f "$game_dir/$f"
done

# Remove the installed loader tree.
rm -rf "$game_dir/scripts/mod_loader"
rm -rf "$game_dir/resources/mods"

# Restore vanilla archive and entry point.
if [[ -f "$game_dir/resources/resource.dat.bak" ]]; then
	rm -f "$game_dir/resources/resource.dat"
	mv "$game_dir/resources/resource.dat.bak" "$game_dir/resources/resource.dat"
fi
if [[ -f "$game_dir/scripts/modloader.lua.bak" ]]; then
	mv -f "$game_dir/scripts/modloader.lua.bak" "$game_dir/scripts/modloader.lua"
fi

echo "Uninstalled. Remember to clear the LD_PRELOAD Steam launch option."
```

**Verification:**
```bash
shellcheck uninstall.sh && shfmt -d uninstall.sh
GAME="/var/mnt/.../Into the Breach"
./install.sh "$GAME" && ./uninstall.sh "$GAME"
test ! -f "$GAME/itb_io.so" && test ! -f "$GAME/libitbsdl.so"
test ! -s "$GAME/scripts/modloader.lua"      # restored to empty (0 bytes)
test ! -f "$GAME/resources/resource.dat.bak" # restored (no leftover .bak)
```
Expected: after install+uninstall the game is vanilla — `.so`s gone, `modloader.lua`
empty, `resource.dat` restored. Launching the game (without preload) runs stock.

**Commit:** `build: add Linux uninstall script`
<!-- END_TASK_2 -->
<!-- END_SUBCOMPONENT_A -->

<!-- START_SUBCOMPONENT_B (task 3) -->
## Subcomponent B: Launcher and graceful degradation

<!-- START_TASK_3 -->
### Task 3: Launcher/README and preload-absence handling

**Verifies:** linux-port.AC6.2, linux-port.AC6.4

**Files:**
- Create: `LAUNCHER_README.md` (repo root; shipped in the bundle)
- Modify (loader): the Linux `libitbsdl` bootstrap guard (Phase 3 Task 2) to self-report a
  missing preload

**Implementation:**

1. **Launcher/README** documents the Steam launch option:
   ```
   LD_PRELOAD="$PWD/libitbsdl.so" %command%
   ```
   `%command%` is Steam's placeholder for the game invocation; `$PWD` resolves to the game
   directory Steam runs from. Include the install/uninstall steps and the note that the
   `.so`s must be in the game directory (install.sh places them).

2. **Graceful degradation (AC6.4).** Two failure modes to handle:
   - **No `LD_PRELOAD` but loader installed:** the game runs, `modloader.lua` requires the
     loader, and Phase 3's `bootstrap/itbsdl.lua` `package.loadlib`s `libitbsdl.so`. Without
     the preload the SDL interposers never install, so `sdl.drawHook`/`eventHook` won't
     fire — the UI won't render, but the loader must not crash. Make the Linux `itbsdl`
     bootstrap detect that the interposers are inactive (e.g. a flag the `SDL_GL_SwapWindow`
     interposer sets in a module global vs. what the `package.loadlib` side sees) and
     `LOG` a clear, actionable message ("libitbsdl.so is not LD_PRELOADed; set the launch
     option — UI disabled") rather than erroring. Mods without UI still load.
   - **`.so` present but not preloaded** is the same path; the self-report covers it.

**Verification:**
```bash
GAME="/var/mnt/.../Into the Breach"
# AC6.2: fresh install + preload boots the loader UI
./install.sh "$GAME"
( cd "$GAME" && LD_PRELOAD="$PWD/libitbboot.so" ./Breach )  # UI renders
# AC6.4: no preload — game boots, loader self-reports, no crash
( cd "$GAME" && ./Breach )
grep -n "not LD_PRELOADed" ~/.local/share/IntoTheBreach/log.txt
```
Expected: with preload the UI renders; without preload the game runs and the log carries the
missing-preload notice — no crash.

**Commit:** `feat: Linux launcher docs and missing-preload self-report`
<!-- END_TASK_3 -->
<!-- END_SUBCOMPONENT_B -->

<!-- START_SUBCOMPONENT_C (task 4) -->
## Subcomponent C: Linux CI

<!-- START_TASK_4 -->
### Task 4: Linux CI job builds artifacts and assembles the bundle

**Verifies:** linux-port.AC6.1

**Files:**
- Modify: `.github/workflows/package.yml` (add a `package-linux` job)

**Implementation:**

Add a Linux job alongside the existing Windows `package` job, matching the repo's
convention (tag-pinned actions, `workflow_dispatch`). The job builds the Rust `.so`s
(`ftldat`, `itb_io`), the C++ `.so`s (`libitbsdl`, `memedit`), stages them at the repo
root, runs `_release_linux.sh`, and uploads the bundle. `ubuntu-latest` ships cargo,
cmake, and rustup; install SDL2/FreeType/Fontconfig dev packages via apt.

```yaml
  package-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install build dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libsdl2-dev libfreetype6-dev libfontconfig1-dev cmake

      - name: Build Rust artifacts
        run: |
          # ftldat-rs and itb-io-rs are separate repos; check them out and build.
          # (Artifact-flow decision — see note below.)
          cargo build --release --manifest-path ftldat-rs/Cargo.toml
          cargo build --release --manifest-path itb-io-rs/Cargo.toml
          cp ftldat-rs/target/release/libftldat.so ftldat.so
          cp itb-io-rs/target/release/libitb_io.so itb_io.so

      - name: Build C/C++ artifacts
        run: |
          cmake -B linux/libitbsdl/build -S linux/libitbsdl && cmake --build linux/libitbsdl/build
          cp linux/libitbsdl/build/libitbsdl.so libitbsdl.so
          cmake -B memedit-vsproject/build -S memedit-vsproject && cmake --build memedit-vsproject/build
          cp memedit-vsproject/build/memedit.so memedit.so

      - name: Assemble Linux bundle
        run: ./_release_linux.sh

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: ITB-ModLoader-Linux-${{ github.event.inputs.version }}
          path: release-linux/*
          retention-days: 7
          if-no-files-found: error
```

**Artifact-flow decision (design open question — resolve here):** `ftldat-rs`/`itb-io-rs`
are separate repos and `libitbsdl`/`memedit` come from this repo's subtree + a sibling repo.
The committed-binary convention (Phase 1, finding #9) means the `.so`s can be committed to
the repo root and the CI job would then only *repackage* them. Choose one and state it in
the workflow: either (a) CI checks out the sibling repos and builds from source (shown
above — reproducible, no committed binaries), or (b) CI repackages committed `.so`s
(simplest, matches the Windows committed-DLL convention). Recommend (a) for the Rust crates
(clean source build) and committing the C++ `.so`s only if the CI C++ build proves flaky.
Whichever is chosen, the Windows `package` job is untouched.

**Verification:**
```bash
actionlint .github/workflows/package.yml
```
Then trigger the workflow (`workflow_dispatch`) and confirm both the Windows zip and the
`ITB-ModLoader-Linux-*` bundle are produced as artifacts.

**Commit:** `ci: add Linux packaging job`
<!-- END_TASK_4 -->
<!-- END_SUBCOMPONENT_C -->

<!-- START_SUBCOMPONENT_D (tasks 5-7) -->
## Subcomponent D: Proton baseline, fresh-install, and Windows-parity verification

<!-- START_TASK_5 -->
### Task 5: Document the native-vs-Proton trade-off

**Verifies:** linux-port.AC7.1

**Files:**
- Create: `docs/linux-proton-vs-native.md`
- Modify: `README.md` (link the doc from a short Linux section)

**Implementation:**

Write a concise, factual comparison (follow the technical-writing / humanizer skills — no
promotional language):
- **Proton:** the Windows build + loader under Proton is WineHQ Platinum with working mods;
  zero native code to maintain. The fallback for anyone who does not want native artifacts,
  and specifically for memedit fields not yet reverse-engineered (Phase 4 verdict).
- **Native:** avoids the Wine layer; ships the `.so`s and the `LD_PRELOAD` launch option.
  Costs: the render/input library, the savedata-path branch, and — the main ongoing cost —
  memedit's per-game-version offset RE (Phase 4).
- State plainly that Proton remains the documented fallback and native is the default this
  work enables.

**Verification:** the doc states the trade-off and keeps Proton as the fallback; README
links it.

**Commit:** `docs: native-vs-Proton trade-off and Proton fallback`
<!-- END_TASK_5 -->

<!-- START_TASK_6 -->
### Task 6: Fresh-install end-to-end verification

**Verifies:** linux-port.AC6.2, linux-port.AC6.3 (integration)

**Files:** none (operational)

**Implementation & verification:**

On a clean copy of the native game (restore from the Phase 2 backup, or a second install):
1. Download/unpack the CI Linux bundle (Task 4 artifact).
2. `./install.sh "$GAME"`, set the `LD_PRELOAD` launch option, launch — the loader boots
   and the UI renders (AC6.2).
3. `./uninstall.sh "$GAME"` — confirm the game is vanilla (`.so`s gone, `modloader.lua`
   empty, `resource.dat` restored) and launches stock (AC6.3).

Record results in `test-requirements.md`.

**Commit:** none (verification task).
<!-- END_TASK_6 -->

<!-- START_TASK_7 -->
### Task 7: Windows-regression QA gate (AC1.5, AC5.2)

**Verifies:** linux-port.AC1.5, linux-port.AC5.2 (gating verification for the upstream PR)

**Files:** none (manual QA; recorded in `test-requirements.md`)

**Implementation & verification:**

The Linux changes are additive and guarded (`Platform.name == "windows"` early-returns;
the platform module resolves the unchanged `.dll` names on Windows), so no Windows code
path is modified. But the design's DoD notes no Windows reference install exists, and the
plan otherwise asserts Windows parity only by inspection. Before opening the upstream PR,
run an actual Windows (or Proton) reference to close AC1.5/AC5.2 as **manual-QA gated**, not
inspection-satisfied:

1. On a Windows or Proton install with the loader built from this branch, boot to
   `onModsLoaded` and confirm the loader loads `itb_io.dll`/`ftldat.dll` and the UI behaves
   as before (AC1.5).
2. Run the in-game `tests/` suites (including the new `platform` suite from Phase 1 Task 4)
   via the in-game test console and confirm all pass (AC5.2). Run the same suites on native
   Linux for cross-platform confirmation.

Record both runs (Windows/Proton and Linux) in `test-requirements.md`. This is a gating
step for the upstream contribution — the Windows-parity ACs are not considered met until it
passes.

**Commit:** none (QA gate).
<!-- END_TASK_7 -->
<!-- END_SUBCOMPONENT_D -->

---

## Phase 5 done when

- `_release_linux.sh` assembles the Linux bundle (four `.so`s, `scripts/`, `mods/`,
  `resources/`, `install.sh`, `uninstall.sh`, launcher README); lints clean.
- The CI `package-linux` job builds the artifacts and uploads the bundle alongside the
  Windows zip (linux-port.AC6.1); `actionlint` clean; Windows job untouched.
- A fresh install + `LD_PRELOAD` launch option boots the loader (linux-port.AC6.2);
  running without the preload self-reports and does not crash (linux-port.AC6.4).
- `uninstall.sh` restores a vanilla game (linux-port.AC6.3).
- The native-vs-Proton trade-off is documented with Proton as the fallback
  (linux-port.AC7.1).
- The Windows-regression QA gate (Task 7) has run: a Windows/Proton reference boots to
  `onModsLoaded` unchanged and the `tests/` suites pass on both Windows/Proton and Linux
  (linux-port.AC1.5, linux-port.AC5.2). These ACs are manual-QA gated, not
  inspection-satisfied, and must pass before the upstream PR.
