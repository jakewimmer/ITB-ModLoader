# Native Linux Support — Manual Verification Plan (Pending Gates)

Companion to `test-requirements.md`. That file maps every acceptance criterion to a
verification and holds the full per-AC procedures and the Manual-QA log. This file is
the focused, ready-to-run plan for the criteria that are **still pending** after the
Phase 1–5 automated-coverage validation of 2026-07-03. Criteria already verified
in-game (AC1.1, AC2.1, AC6.2, AC6.3, AC6.4, the setbitmap color parity, and the
single-runtime `nm` check) are recorded in the Manual-QA log and are not repeated here.

## Coverage-validation result (2026-07-03)

Automated criteria in scope: AC1.2, AC4.4, AC4.5, AC5.1, AC6.1, AC6.3.

| AC | Automated check | Status |
|---|---|---|
| AC1.2 | `scripts/mod_loader/tests/platform.lua` (registered `main.lua:25`) | PASS (in-game suite; asserts `.so`/`.dll` per `Platform.name`) |
| AC4.4 | `derive_offsets` `pytest` (15 tests) | PASS with caveat — see gap note |
| AC4.5 | `derive_offsets` `pytest` missing-symbol diagnostic | **GAP — no such test** |
| AC5.1 | `rg` structural `.dll`-literal check | PASS (only a code comment matches; loads route through `Platform.nativeLibrary`) |
| AC6.1 | `actionlint` / `shellcheck` / `shfmt` | PASS (lints clean; artifact upload is a CI/manual step) |
| AC6.3 | `shellcheck` / `shfmt` on `uninstall.sh` | PASS (lints clean; file-state round-trip verified in-game) |

C++ `ctest` (`linux/libitbsdl/build`): `alloc_guard_test` PASS, `setbitmap_rgba_test`
PASS (2/2).

**Gaps to feed back to the implementer:**

- **AC4.5 has no automated test for its actual failure mode.** The criterion requires
  that *a binary whose anchor symbols cannot be resolved* fails with a diagnostic
  naming the missing symbol. The only "missing" tests
  (`test_derive_offsets_missing_binary`, `test_offset_extractor_missing_binary`) assert
  `FileNotFoundError` for a **nonexistent file path** — a different failure than a
  present ELF lacking an anchor symbol. Needed: a fixture ELF (valid header, symbol
  table present, a required anchor absent) asserting a non-zero exit and an error
  message naming the unresolved symbol/field.
- **AC4.4 extraction assertion is weaker than `test-requirements.md` describes.** The
  doc says the fixture "asserts the extracted offset." In fact the fixture
  (`conftest.py::test_elf_data`) is a bare ELF header with no sections/symbols, and
  `test_offset_extractor_extract_offsets` only asserts `isinstance(offsets, dict)`.
  The offset *values* asserted in `test_offset_extraction.py` are hand-fed into
  `generate_addresses_table` — that verifies table serialization, not extraction from
  an ELF. Extraction against a real luabind registration is exercised only against the
  355 MB game binary, which is a manual/environment run. Recommended: add a fixture ELF
  carrying one known luabind `def_readwrite` registration and assert the derived offset.

Note: `ty check` in `derive_offsets` could not run (`ty` binary not on PATH in this
environment); `ruff check` passed. Types were not verified here.

## Prerequisites

```bash
GAME="/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach"
LOG="$HOME/.local/share/IntoTheBreach/log.txt"
```

- A **backed-up game install** (`cp -a "$GAME" "$GAME.backup-preloader"`).
- An **interactive graphical session** (the pending gates need visual confirmation and
  in-mission state; none run under `xvfb`).
- `echo 590380 > "$GAME/steam_appid.txt"` to launch `Breach` outside Steam.
- **Initialized submodules** (`git submodule update --init --recursive`) for memedit /
  easyEdit.
- For the parity/regression gates: a **Windows or Proton reference install** with the
  loader built from this branch.
- The preload entrypoint is **`libitbboot.so`** (it forwards SDL/GL interposition into
  `libitbsdl.so`). Launch with `LD_PRELOAD="$PWD/libitbboot.so" %command%`. (Older
  `test-requirements.md` procedures name `libitbsdl.so` directly — treat `libitbboot.so`
  as authoritative; see commit `10ff445`.)

## Pending gate 1 — AC4.2: memedit read/write round-trip

Blocked until an in-mission calibration session derives `__addresses_linux.lua`; the
table ships empty by design, so only fields you have reverse-engineered are in scope.

| Step | Action | Expected |
|---|---|---|
| 1 | Launch interactively: `( cd "$GAME" && LD_PRELOAD="$PWD/libitbboot.so" ./Breach )` with memedit enabled | Game boots; `log.txt` shows `memedit.so` loaded, no load error |
| 2 | Start a mission so a pawn exists; open the memedit/easyEdit surface | Surface is available in-game |
| 3 | For a calibrated field (e.g. Pawn `MaxHealth`), read the current value | Value matches what the game shows |
| 4 | Write a new value, then read it back | Read-back equals the written value; in-game effect visible (e.g. health bar changes) |
| 5 | Repeat for several fields (`Team`, `Fire`, `Acid`) | Each round-trips cleanly |

Pass: every calibrated field round-trips (write takes effect, reads back the written
value). A field that reads garbage means a wrong derived offset. Uncalibrated fields
are out of scope and fall back to Proton (AC7).

## Pending gate 2 — AC4.3: easyEdit on native Linux

| Step | Action | Expected |
|---|---|---|
| 1 | `git submodule update --init --recursive`; enable easyEdit | easyEdit present in config |
| 2 | Boot interactively with the `libitbboot.so` preload | `log.txt` shows easyEdit initialized, no error |
| 3 | Open the easyEdit menu | Menu renders |
| 4 | Use one representative tool | Tool performs its action in-game |

Pass: menu opens, a representative tool works, log shows easyEdit init without error.

## Pending gate 3 — AC1.5: Windows/Proton regression (upstream-PR gate)

Requires a Windows or Proton reference install; no such machine is available here.

| Step | Action | Expected |
|---|---|---|
| 1 | Build/install this branch's loader on the reference install; launch | Loader loads `itb_io.dll` and `ftldat.dll` (loader-log lines) |
| 2 | Let boot complete | Boot reaches `onModsLoaded` |
| 3 | Exercise the mod-config UI against a pre-change baseline | Behavior unchanged; no new errors |

Pass: `.dll`s load, boots to `onModsLoaded`, UI matches baseline. Record in the QA log.

## Pending gate 4 — AC5.2: in-game test suites on both platforms (upstream-PR gate)

| Step | Action | Expected |
|---|---|---|
| 1 | On native Linux, boot the loader; open the test console (`scripts/mod_loader/modui/tests_console.lua`); run all `tests/` suites including `platform` | Every suite green, no failed assertion |
| 2 | On the Windows/Proton reference, run the same suites | Every suite green, no failed assertion |

Pass: all suites pass on both platforms. The `platform` suite (AC1.2) is the
cross-platform branch check; it exercises the Windows arm only when run on Windows.

## Pending gate 5 — AC3.4: FreeType-vs-GDI+ text parity (upstream-PR gate)

Color parity is already covered automatically (`setbitmap_rgba_test`, verified green).
The **text/layout** half needs a side-by-side visual comparison against a Windows/Proton
reference and is still pending.

| Step | Action | Expected |
|---|---|---|
| 1 | Launch Linux with the `libitbboot.so` preload; open a text-heavy loader screen | Text renders via FreeType |
| 2 | Open the same screen on the Windows/Proton reference | Reference rendering available |
| 3 | Compare line height, glyph spacing, and outline | Legible; matches reference within the Phase 3 Task 4 adjustments — no clipped, overlapping, or missing glyphs |

Pass: Linux text is legible and layout matches the reference within noted adjustments.

## Criteria with no automated coverage (by design — runtime-only)

These are correctly Human/Manual because their core claim is a live-process behavior;
each is already documented in `test-requirements.md` and most are verified in the QA
log. Listed here so the "no automated test" set is explicit and intentional:
AC1.1, AC1.3, AC1.4, AC2.1, AC2.2, AC2.3, AC2.4, AC3.1, AC3.2, AC3.3, AC3.5, AC4.1,
AC6.2, AC6.4, AC7.1. (AC1.3, AC2.3, AC4.1 carry automated *supporting* checks —
`cargo test` in the external crates and `nm -D` symbol-export checks — but not a check
of the runtime registration itself.)
