# Native Linux Support — Phase 4: memedit on native Linux

**Goal:** `memedit.so` builds and loads on native Linux; easyEdit runs natively; the
memedit offset table is derived from the Linux binary by a documented, repeatable tool for
the fields that are recoverable, with Proton the documented fallback for the remainder.

**Architecture:** Port `memedit-vsproject` to a Linux `cdylib`-equivalent shared object
(raw Lua C API, static PUC Lua 5.1.5). Replace the design's DWARF offset generator — which
is infeasible (see verdict below) — with an offset-derivation tool that harvests luabind
field registrations and symbols from the binary and drives a gdb-assisted manual procedure
for the rest, emitting `__addresses_linux.lua` in memedit's existing table format.

**Tech Stack:** C++ (raw Lua 5.1 C API), static PUC Lua 5.1.5, Python (`pyelftools` +
capstone) and/or gdb for offset derivation.

**Scope:** Phase 4 of 5. Depends on Phase 2 (loader boot). Independent of Phase 3.

**Codebase verified:** 2026-07-02 (`codebase-investigator` mapped memedit's offset table,
consumption, build bits, and easyEdit's dependency; binary DWARF/symbol/luabind evidence
gathered hands-on).

---

## ⚠ Feasibility verdict — memedit offsets (this replaces the design's DWARF plan)

The design proposed deriving memedit's offset table from the game binary's DWARF
`debug_info`. **This is not feasible.** Evidence from the local native binary:

- **The game's own struct layouts are absent from DWARF.** `.debug_str` contains none of
  `Pawn`/`Board`/`Tile`/`Weapon`/`SpaceDamage`; the 1621 DWARF `structure_type` DIEs all
  belong to statically-linked third-party libraries (FreeType `FT_*`, glibc `_IO_FILE`).
  The only DWARF compile units are library sources (e.g. `builds/unix/ftsystem.c`). The
  game's C++ was compiled without `-g`. `file`'s "with debug_info, not stripped" refers to
  the libraries' DWARF plus the game's *symbol table*, not the game's type layouts.
- **luabind exposes only a few field offsets.** The binary has 22 `def_readwrite`
  bindings (member-pointer offsets encoded as immediates), for classes like `PilotSkill`,
  `SpaceDamage`, `Corporation` — but **none for `Pawn`/`Board`/`Tile`/`Weapon`**, which
  bind methods, not readable fields.
- **memedit needs ~99 struct-member offsets** it cannot get either way: Pawn (37),
  SpaceDamage (35), Tile (15), Weapon (9), Board (3), Game (1), plus `vital` struct sizes
  (`__addresses.lua`). These are raw base-pointer + byte-offset values.

**Conclusion:** the offsets memedit needs are recoverable only by reverse-engineering the
Linux binary (gdb / disassembly), the same way the Windows `__addresses.lua` was produced —
not by any automated DWARF pass. Full native memedit therefore carries an ongoing,
per-game-version RE cost. This phase delivers the buildable `.so`, an assist tool that
automates the derivable portion and diagnoses missing symbols, a documented manual
procedure for the rest, and keeps **Proton as the documented fallback** for offsets not yet
derived.

**Scope decision (user-confirmed 2026-07-02):** pursue native memedit via manual/assisted
RE, accepting the ongoing per-game-version maintenance cost, rather than descoping to
Proton-only. The user chose this over the Proton-only and hybrid alternatives after the
feasibility verdict was presented. (For reference, the descope path would drop
Subcomponent B and keep only "build `memedit.so`, wire easyEdit, document memedit as
Proton-only" — Subcomponents A and C stand alone — but that is not the chosen path.)

**Two findings that de-risk the rest:**
- **easyEdit does not depend on memedit** (grep of `ITB-Easy-Edit` finds no `memedit`
  reference). easyEdit runs on native Linux with only Phases 1–3 — AC4.3 is effectively
  independent of the offset problem.
- memedit uses the **raw Lua C API** (not luabind/LuaBridge), so `memedit.so` links a
  static PUC Lua 5.1.5 like the other artifacts; the only hard part is the offset table.

**Submodules are not checked out.** `scripts/mod_loader/extensions/modLoaderExtensions/mods/{memedit,modApiExt,easyEdit}`
are empty; run `git submodule update --init --recursive` before this phase.

**Local sibling clones created during planning:**
`/var/home/displacer/Projects/clones/{memedit,memedit-vsproject,ITB-Easy-Edit}`.

---

## memedit offset table format (target output)

The generator must emit `__addresses.lua`'s structure (from `memedit/__addresses.lua`),
keyed by game version → object type → field:

```lua
["<version>"] = {
  ["pawn"] = {
    ["MaxHealth"] = { [1] = 2220; [2] = 2; [3] = 0; };  -- {offset, access, datatype}
    ["WeaponList"] = { [1] = 4; [2] = 0; [3] = 7; };
    -- ...
  },
  ["spaceDamage"] = { ["Damage"] = 8; ["AnimState"] = { [1]=52; [2]=2; [3]=0; }; },
  ["board"] = { ... }, ["tile"] = { ... }, ["weapon"] = { ... }, ["game"] = { ... },
  ["vital"] = { ["size_pawn"] = 4912; ["size_board"] = 29976; ["step_rows"] = 12; ... },
}
```
Tuple semantics (from `options.cpp:76-101`): `[1]` byte offset, `[2]` access
(0=r,1=w,2=rw,3=none), `[3]` datatype (0=int,1=uint,2=uchar,3=bool,4=double,
5=const char*,6=int list,7=void* list). Consumed at `lua_obj.cpp:49-53` (read:
`*(type*)(addr + delta)`) and `:77-83` (write). Nested structs (Tile from Board) use
`vital` sizes/deltas to compute intermediate pointers (`lua_tile.cpp:43-45`).

---

## Acceptance Criteria Coverage

### linux-port.AC4: memedit works at full parity
- **linux-port.AC4.1 Success:** `memedit.so` loads via `package.loadlib` and exposes `luaopen_memedit`.
- **linux-port.AC4.2 Success:** memedit reads and writes game-object fields correctly using the Linux offset table.
- **linux-port.AC4.3 Success:** easyEdit (which builds on memedit) functions on native Linux.
- **linux-port.AC4.4 Success:** The offset generator regenerates the table from the binary's DWARF `debug_info`.
- **linux-port.AC4.5 Failure:** A binary whose symbols the generator cannot resolve fails with a diagnostic identifying the missing symbol, not a silent wrong offset.

### linux-port.AC5: Single upstreamable codebase (completes here)
- **linux-port.AC5.1 Success:** The two hardcoded library names route through the platform module; no `.dll` literal remains in the load path.

> **AC5.1 spans Phase 1 and this phase.** Phase 1 routes the two core sites
> (`bootstrap/itb_io.lua:9`, `ftldat-rs/ftldat.lua:10`). Investigation found a **third**
> native-load site the design missed — `memedit.lua:76` in the memedit submodule — which
> Task 3 routes through the platform module. AC5.1 is only fully closed once this task
> lands; the repo-wide grep for a `.dll` literal in a load path must include the memedit
> submodule.

> **AC4.4 is reinterpreted per the verdict:** "regenerates the table from the binary" is
> honored, but from the binary's **luabind registrations + symbol table (+ gdb-assisted
> anchors)**, not DWARF `debug_info` (which lacks the game types). AC4.2 is verified for the
> fields present in the derived table; fields not yet reverse-engineered fall back to
> Proton (documented, ties to linux-port.AC7). This deviation from the design's wording is
> deliberate and evidence-backed (see verdict).

---

<!-- START_SUBCOMPONENT_A (task 1) -->
## Subcomponent A: Build `memedit.so`

<!-- START_TASK_1 -->
### Task 1: Port `memedit-vsproject` to a Linux shared object

**Verifies:** linux-port.AC4.1

**Repo:** `/var/home/displacer/Projects/clones/memedit-vsproject`

**Files:**
- Modify: `memedit/stdafx.h:13` (`#include <windows.h>` — guard out on non-Windows)
- Modify: `memedit/memedit.cpp:17-19` (`DLLEXPORT`/`__declspec(dllexport)` → visibility attr)
- Modify: `memedit/dllmain.cpp` (`DllMain` — guard out on non-Windows)
- Create: `memedit/CMakeLists.txt` (Linux build; static PUC Lua 5.1.5)
- Copy artifact into: `/var/home/displacer/Projects/clones/ITB-ModLoader/memedit.so`

**Implementation:**

The port is mechanical — the Windows-specific surface is tiny (`codebase-investigator`
confirmed only the five sites above). memedit uses the raw Lua C API, so nothing else is
platform-bound.

```cpp
// memedit.cpp — replace the DLLEXPORT macro block
#if defined(_WIN32)
#  define DLLEXPORT __declspec(dllexport)
#else
#  define DLLEXPORT __attribute__((visibility("default")))
#endif

extern "C" DLLEXPORT int luaopen_memedit(lua_State* L) { /* unchanged body */ }
```

Guard `windows.h` and `DllMain`:
```cpp
// stdafx.h
#if defined(_WIN32)
#  include <windows.h>
#endif
```
```cpp
// dllmain.cpp — compile the DllMain stub only on Windows
#if defined(_WIN32)
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
#endif
```

Add a CMake build linking a static PUC Lua 5.1.5 (the game's ABI; the vendored
`memedit/lua/include` headers are Lua 5.1 and match). Vendor the **same upstream PUC Lua
5.1.5 tarball** Phase 3 Task 1 uses. `memedit-vsproject` and `libitbsdl` (in
`ITB-ModLoader`) are separate git repos, so each carries its own physical copy of those
identical sources — they cannot share one `add_subdirectory` tree. Using the same 5.1.5
sources in both makes the two C++ artifacts link an ABI-identical Lua. Build
x86-64:
```cmake
cmake_minimum_required(VERSION 3.16)
project(memedit CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Shared canonical PUC Lua 5.1.5 static target (same one libitbsdl links, Phase 3 Task 1).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.1.5 lua51_build)  # lua51_static
file(GLOB MEMEDIT_SRC memedit/*.cpp)
add_library(memedit SHARED ${MEMEDIT_SRC})
target_include_directories(memedit PRIVATE memedit memedit/lua/include)
target_link_libraries(memedit PRIVATE lua51_static)
set_target_properties(memedit PROPERTIES OUTPUT_NAME memedit PREFIX "")  # memedit.so
```

**Verification:**
```bash
cd /var/home/displacer/Projects/clones/memedit-vsproject && cmake -B build && cmake --build build
cp build/memedit.so /var/home/displacer/Projects/clones/ITB-ModLoader/memedit.so
nm -D /var/home/displacer/Projects/clones/ITB-ModLoader/memedit.so | grep luaopen_memedit
```
Expected: builds; `luaopen_memedit` exported. In-game load is confirmed once the offset
table exists (Task 3) — memedit's `luaopen_memedit` accepts the options/offsets table.

**Commit (memedit-vsproject):** `feat: build memedit as a Linux shared object`
**Commit (ITB-ModLoader):** `build: add Linux memedit.so artifact`
<!-- END_TASK_1 -->
<!-- END_SUBCOMPONENT_A -->

<!-- START_SUBCOMPONENT_B (tasks 2-3) -->
## Subcomponent B: Offset derivation tool and Linux table

<!-- START_TASK_2 -->
### Task 2: Offset-derivation tool (luabind/symbol harvest + gdb-assisted anchors)

**Verifies:** linux-port.AC4.4 (reinterpreted), linux-port.AC4.5

**Repo:** `/var/home/displacer/Projects/clones/memedit` (checked in alongside `__addresses.lua`)

**Files:**
- Create: `tools/derive_offsets/derive_offsets.py`
- Create: `tools/derive_offsets/README.md` (the regeneration procedure)

**Implementation:**

A tool (Python 3.13 via `uv`; deps `pyelftools`, `capstone`) that derives what the binary
genuinely exposes and clearly reports what it cannot:

1. **Symbol/version anchor:** read the binary's symbol table and version string
   (`$Lua: Lua 5.1.5 …` and the game version) to key the output table.
2. **luabind field harvest:** locate `luabind::class_<T>::def_readwrite<T, FieldT>(name, &T::field)`
   call sites (symbols present for the 22 exposed fields), disassemble each caller with
   capstone, and extract the member-pointer offset immediate and the associated field-name
   string. Emit those offsets directly into the table.
3. **gdb-assisted anchors for the rest:** for fields not exposed by luabind
   (Pawn/Board/Tile/Weapon internals), the tool cannot auto-derive offsets. It emits a
   **stub entry per required field marked `TODO`**, and the README documents the
   gdb/disassembly procedure to fill each (attach to the running game or a core, use a
   known object instance, and locate the field by matching a known value / cross-checking
   against the Windows offset semantics). This is the manual step the design's
   "regeneration procedure documented for future game versions" already anticipated.
4. **AC4.5 diagnostic:** when a required symbol/field the tool expects cannot be resolved,
   it must **fail loudly naming the missing symbol/field** (non-zero exit, explicit
   message) — never emit a guessed or zero offset silently. Every field is either derived,
   an explicit `TODO`, or a named hard error.

The tool writes `__addresses_linux.lua` in the exact table format above, merging derived
values, `TODO` markers, and `vital` sizes (struct sizes are recoverable from the symbol
table / class destructors where available; otherwise `TODO`).

Follow the modern-python skill (uv/ruff/ty). This is a functional parser → property-style
tests fit (feed a small fixture ELF with a known luabind registration; assert the extracted
offset). Do not test against the full 355 MB game binary in unit tests — use a compiled
fixture.

**Verification:**
```bash
cd /var/home/displacer/Projects/clones/memedit/tools/derive_offsets
uv run derive_offsets.py "/var/mnt/.../Into the Breach/Breach" -o /tmp/claude/__addresses_linux.lua
ruff check . && ty check
uv run pytest -q          # fixture-based extractor tests
```
Expected: the 22 luabind-exposed fields are derived with real offsets; unexposed fields
appear as explicit `TODO`s; a deliberately missing anchor symbol produces a named error
(AC4.5). Lints/types clean; tests pass.

**Commit (memedit):** `feat: add Linux offset-derivation tool`
<!-- END_TASK_2 -->

<!-- START_TASK_3 -->
### Task 3: Produce and validate `__addresses_linux.lua`; wire memedit.lua

**Verifies:** linux-port.AC4.2

**Repo:** `/var/home/displacer/Projects/clones/memedit`

**Files:**
- Create: `__addresses_linux.lua` (generated + manually completed table)
- Modify: `memedit.lua` (select the platform table + the `memedit.so` name via the loader's platform module)

**Implementation:**

1. Run the Task 2 tool against the local native binary, then complete the `TODO` offsets
   for the fields memedit-dependent mods actually use, following the README procedure
   (start with the most-used: Pawn `MaxHealth`/`Team`/`Fire`/`Acid`, Board/Tile basics —
   prioritize by what your target mods read). Full completion of all ~99 is the
   ongoing-maintenance surface the verdict flags; ship a validated subset and mark the
   rest `TODO`, with Proton as the fallback for mods needing not-yet-derived fields.
2. Wire `memedit.lua` to pick `__addresses_linux.lua` on Linux and `__addresses.lua` on
   Windows, and to route the native-library load through the Phase 1 platform module
   instead of the hardcoded `memedit.dll`. **This is the third native-load site** (the
   design named only two); `memedit.lua:76` currently reads
   `package.loadlib(path.."memedit.dll", "luaopen_memedit")(options)`, where `path` is the
   memedit *mod* directory.

   **Load-location decision (resolves reviewer M1):** stage `memedit.so` at the **game
   root** like the other Linux artifacts (`install.sh` already copies every repo-root
   `.so` there), and on Linux load it by the platform name from the game cwd, dropping the
   mod-directory `path..` prefix:
   ```lua
   -- Windows keeps the existing mod-directory-relative .dll load; Linux loads ./memedit.so
   -- from the game root (where install.sh stages every native artifact).
   local lib = Platform.nativeLibrary("memedit")            -- "./memedit.so" | "memedit.dll"
   if Platform.name == "windows" then
       package.loadlib(path .. lib, "luaopen_memedit")(options)
   else
       package.loadlib(lib, "luaopen_memedit")(options)
   end
   ```
   This keeps Windows byte-for-byte (same `path.."memedit.dll"`) while giving Linux a
   cwd-relative `./memedit.so` that `dlopen` resolves (Phase 1, finding #3). Document the
   placement so the bundle/install ship `memedit.so` at the game root, not the mod dir.

**Verification (AC4.2):** in-game, exercise memedit read/write for the completed fields:
read a pawn's `MaxHealth`, write it, confirm the change takes effect and reads back. Use
the memedit/easyEdit in-game surface. A field whose Linux offset is correct round-trips;
an incorrect one reads a wrong value — so a successful round-trip on several fields
validates the derived offsets.

**Commit (memedit):** `feat: Linux offset table and platform-aware memedit load`
<!-- END_TASK_3 -->
<!-- END_SUBCOMPONENT_B -->

<!-- START_SUBCOMPONENT_C (task 4) -->
## Subcomponent C: easyEdit on native Linux

<!-- START_TASK_4 -->
### Task 4: Verify easyEdit runs natively

**Verifies:** linux-port.AC4.3

**Repo:** `/var/home/displacer/Projects/clones/ITB-Easy-Edit` (+ submodule in the game install)

**Files:** none expected (easyEdit is Lua; it has no memedit dependency)

**Implementation & verification:**

easyEdit has no `memedit` reference (confirmed), so it depends only on the loader + UI
(Phases 1–3). Ensure the submodules are populated (`git submodule update --init
--recursive`), enable easyEdit, boot the native game (Phase 3 preload in place), and verify
easyEdit's UI opens and its tools function. If easyEdit is found to touch memedit after all,
its used fields must be present in `__addresses_linux.lua` (Task 3) — treat any such field
as a prioritized `TODO`.

**Verification:** easyEdit menu opens and a representative tool works on native Linux;
`modloader.log` shows easyEdit initialized without error.

**Commit:** none (verification task) unless a Linux-specific easyEdit fix is needed.
<!-- END_TASK_4 -->
<!-- END_SUBCOMPONENT_C -->

---

## Phase 4 done when

- `memedit.so` builds (Windows bits guarded; static PUC Lua 5.1.5) and exports
  `luaopen_memedit`; loads via the platform module (linux-port.AC4.1).
- The offset-derivation tool derives the luabind-exposed fields from the binary, marks the
  rest `TODO`, and fails with a named diagnostic on an unresolved anchor
  (linux-port.AC4.4 reinterpreted, linux-port.AC4.5).
- `__addresses_linux.lua` contains validated offsets for the shipped field subset;
  memedit reads/writes those fields correctly in-game (linux-port.AC4.2).
- easyEdit runs on native Linux (linux-port.AC4.3).
- The verdict and the Proton fallback for not-yet-derived offsets are documented (ties to
  linux-port.AC7, finalized in Phase 5).

**Known limitation (accepted):** full memedit parity (all ~99 offsets) is an ongoing manual
RE effort per game version. The user accepted this cost (scope decision above); native
ships a validated subset first, with Proton as the fallback for fields not yet derived, and
the table is extended over time via the documented regen procedure.
