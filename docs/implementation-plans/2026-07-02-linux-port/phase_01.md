# Native Linux Support — Phase 1: Platform seam and native-library builds

**Goal:** The loader selects native libraries by platform, and the Rust libraries
(`ftldat`, `itb_io`) build and load as Linux `.so`s.

**Architecture:** A new Lua platform-detection module maps logical library names to
per-OS filenames. The two hardcoded `.dll` load sites route through it. The two Rust
crates gain Linux `cdylib` builds; `itb-io-rs` additionally resolves the native-Linux
save directory.

**Tech Stack:** PUC Lua 5.1.5 (the game's embedded runtime), Rust (`cdylib` via `mlua`
0.8.3 `lua51`/`vendored`), in-game `Tests.Runner` test framework.

**Scope:** Phase 1 of 5.

**Codebase verified:** 2026-07-02 (two `codebase-investigator` passes + hands-on binary
inspection of the local native install).

---

## Codebase verification findings (read before implementing)

These corrections to the design plan were confirmed against the code and the local
native binary. **They change how the tasks below are written — do not fall back to the
design's original wording where it conflicts.**

1. **The game embeds PUC-Rio Lua 5.1.5, not LuaJIT.** The binary's ident string is
   `$Lua: Lua 5.1.5 Copyright (C) 1994-2012 Lua.org, PUC-Rio $`, and PUC-only symbols
   (`lua_getfenv`, `lua_setfenv`, `luaL_openlib`) are present while no `luaJIT_*`/`lj_*`
   symbols exist. **Consequence:** `jit.os` (named in the design) is unavailable. OS
   detection must use `package.config:sub(1,1)` — the directory separator, `\` on
   Windows and `/` on Linux/macOS. The Phase 1B investigator confirmed no `jit.*` or
   `package.config` usage exists in the codebase today.

2. **The game does not export its Lua C symbols dynamically.** All 247 `lua_*`/`luaL_*`
   symbols live in the static `.symtab` (type `T`) but none appear in `.dynsym`. On
   Windows the DLLs import Lua from the shipped `lua5.1.dll`; on Linux there is no such
   shared library — Lua is statically baked into the executable. **Consequence:** a
   `package.loadlib`'d `.so` cannot resolve `lua_*` against the game at load time. The
   `.so` must **statically link its own PUC Lua 5.1** (mlua `vendored` feature). This is
   ABI-safe precisely because the game is PUC 5.1.5: `mlua::Lua::init_from_ptr(L)` wraps
   the game's live `lua_State` and the vendored functions operate on it correctly. This
   is the same pattern `itb-io-rs` already uses in its dev configuration.

3. **`dlopen` does not search the working directory for a bare filename.** On Windows
   `LoadLibrary("itb_io.dll")` searches the app directory; on Linux `dlopen("itb_io.so")`
   with no slash searches only `LD_LIBRARY_PATH` and system directories — **not** the
   game's cwd. **Consequence:** the Linux filename the platform module returns must be
   `./`-prefixed (`./itb_io.so`) so `dlopen` resolves it relative to the game directory.

4. **Two hardcoded `.dll` load sites exist, and only two:**
   - `scripts/mod_loader/bootstrap/itb_io.lua:9` — `package.loadlib("itb_io.dll", "luaopen_itb_io")()`
   - `scripts/mod_loader/ftldat-rs/ftldat.lua:10` — `package.loadlib("ftldat.dll", "luaopen_ftldat")()`
   Both are wrapped in `try{}:catch{}` blocks that `error()` with a message naming the
   file. (The design cited `ftldat-rs/ftldat.lua:10` as a standalone path; the file
   actually lives under `scripts/mod_loader/ftldat-rs/`.)

5. **Savedata delegation lives at `bootstrap/itb_io.lua:341-356`** (`Directory.savedata()`
   → `factory.save_data_directory()`), not `modapi/savedata.lua:333` as the design
   stated. No Lua change is needed there — the native filename change is transparent to it.

6. **`itb-io-rs` is already Linux-ready structurally.** `Cargo.toml` declares
   `crate-type = ["cdylib", "staticlib", "rlib"]` and `src/lib.rs:8` exports
   `luaopen_itb_io` registering the global `itb_io`. Its `save_data_directory()`
   (`src/path_filter.rs:35`) currently probes: Windows `Documents/My Games/Into The Breach`,
   the Proton path `./../../steamapps/compatdata/590380/pfx/`, then `./user`. The
   native-Linux branch is the real work here.

7. **`ftldat-rs` master dropped its Lua binding.** Current `Cargo.toml` is
   `crate-type = ["lib"]` with no `luaopen_ftldat` (the binding moved to `itb-rs-lua`,
   which exports `luaopen_itb_rs` / `itb_rs.ftldat` — a different contract). The shipped
   `ftldat.dll` still exports `luaopen_ftldat` and registers the global `ftldat` with
   `read_package`/`new_package`, matching what `ftldat.lua` expects. To preserve the
   Windows contract byte-for-byte, the Linux `ftldat.so` must also export `luaopen_ftldat`.
   The binding + `cdylib` crate-type + `mlua` dep existed at commit `1fa1f56^1`
   (`src/lua_exports.rs`, `src/lib.rs`); restore them rather than adopting `itb-rs-lua`.

8. **Native save directory is `~/.local/share/IntoTheBreach`.** The local native install
   writes `profile_*`, `settings.lua`, and `io_test.txt` there — the standard
   `$XDG_DATA_HOME/IntoTheBreach`. Distinct from the Proton `compatdata` path. The
   `directories` crate (already an `itb-io-rs` dependency) yields it via
   `ProjectDirs::from("", "", "IntoTheBreach").data_dir()`.

9. **Compiled artifacts are committed at the repo root** (`itb_io.dll`, `ftldat.dll`,
   `SDL2.dll`, `lua5.1.dll`, …). The Linux `.so`s follow the same convention (committed
   at the repo root); CI builds are a Phase 5 concern.

**Local sibling clones (created during planning, needed by the executor):**
- `/var/home/displacer/Projects/clones/ftldat-rs` (from `github.com/itb-community/ftldat-rs`)
- `/var/home/displacer/Projects/clones/itb-io-rs` (from `github.com/itb-community/itb-io-rs`)
- `/var/home/displacer/Projects/clones/itb-rs-lua` (reference for the current binding split)

---

## Acceptance Criteria Coverage

This phase implements and tests:

### linux-port.AC1: The loader boots on the native Linux game
- **linux-port.AC1.2 Success:** The platform module returns `.so` names on Linux and `.dll` names on Windows.
- **linux-port.AC1.3 Success:** `ftldat.so` and `itb_io.so` load via `package.loadlib` and register their `luaopen_*` entries.
- **linux-port.AC1.4 Failure:** A missing native `.so` produces a clear error naming the missing library, not a bare `package.loadlib` failure.
- **linux-port.AC1.5 Regression:** On Windows the loader still loads the `.dll`s and behaves byte-for-byte as before.

### linux-port.AC5: Single upstreamable codebase
- **linux-port.AC5.1 Success:** The two hardcoded library names route through the platform module; no `.dll` literal remains in the load path.
- **linux-port.AC5.2 Regression:** The existing in-game test suites (`tests/`) pass on both Windows and Linux.

> `linux-port.AC1.1` (boots to `onModsLoaded`) and the `itb-io-rs` savedata behavior
> (`linux-port.AC2.3`) are exercised end-to-end in Phase 2; Phase 1 delivers the
> platform seam and the loadable artifacts they depend on.

---

<!-- START_SUBCOMPONENT_A (tasks 1-4) -->
## Subcomponent A: Lua platform seam

<!-- START_TASK_1 -->
### Task 1: Create the platform-detection module

**Verifies:** linux-port.AC1.2, linux-port.AC5.1

**Files:**
- Create: `scripts/mod_loader/bootstrap/platform.lua`
- Modify: `scripts/mod_loader/bootstrap/__scripts.lua:2-14` (insert `"platform"` before `"itb_io"`)

**Implementation:**

The module detects the host OS from `package.config` and maps logical library names
(`itb_io`, `ftldat`, `itbsdl`, `memedit`) to platform-specific filenames. It must expose
a global (bootstrap modules define globals, e.g. `factory`, `File`, `GetParentPath`), so
define a global `Platform` table. Linux filenames are `./`-prefixed (finding #3);
`itbsdl` maps to `libitbsdl.so` on Linux and has no Windows loadlib target (the Windows
proxy provides `sdl.*`), so return `nil` for `itbsdl` on Windows.

```lua
-- Detects the host platform and maps logical native-library names to per-OS filenames.
-- The game embeds PUC Lua 5.1.5, so jit.os is unavailable; OS is inferred from the
-- directory separator reported by package.config (finding: no jit.* in this runtime).
Platform = {}

-- package.config's first line is the directory separator: "\" on Windows, "/" elsewhere.
local separator = package.config:sub(1, 1)
if separator == "\\" then
	Platform.name = "windows"
else
	-- macOS is treated as a future extension; it shares POSIX "/" and would need a
	-- "macos" branch with .dylib names. Linux is the supported non-Windows target.
	Platform.name = "linux"
end

-- logical name -> { windows = <filename or nil>, linux = <filename or nil> }
-- On Linux, dlopen(3) does not search the working directory for a bare name, so names
-- are prefixed with "./" to resolve relative to the game directory (finding).
local LIBRARY_NAMES = {
	itb_io  = { windows = "itb_io.dll",  linux = "./itb_io.so" },
	ftldat  = { windows = "ftldat.dll",  linux = "./ftldat.so" },
	itbsdl  = { windows = nil,           linux = "./libitbsdl.so" },
	memedit = { windows = "memedit.dll", linux = "./memedit.so" },
}

--- Returns the platform-specific filename for a logical native-library name.
-- @param logical_name one of "itb_io", "ftldat", "itbsdl", "memedit"
-- @return the filename to pass to package.loadlib for the current platform
function Platform.nativeLibrary(logical_name)
	local entry = LIBRARY_NAMES[logical_name]
	if entry == nil then
		error(string.format("Unknown native library %q", tostring(logical_name)))
	end

	local filename = entry[Platform.name]
	if filename == nil then
		error(string.format(
			"Native library %q is not available on platform %q",
			logical_name, Platform.name
		))
	end

	return filename
end
```

Then register it in the bootstrap load order **before `itb_io`** so it is available when
the loadlib sites run. In `scripts/mod_loader/bootstrap/__scripts.lua`, the `scripts`
list currently is `{"security", "assert", "classes", "try_catch", "itb_io", "io", ...}`.
Insert `"platform"` immediately before `"itb_io"`:

```lua
local scripts = {
	"security",
	"assert",
	"classes",
	"try_catch",
	"platform",
	"itb_io",
	"io",
	"utils",
	"event",
	"modApi",
	"constants",
	"binarySearch",
	"class_iteration",
}
```

**Testing:** Covered by the in-game testsuite in Task 4 (this task's behavior is not
independently runnable outside the game). Do not add a standalone test — there is no
standalone Lua runner in this project (finding: tests are in-game only).

**Verification:**
- Confirm `scripts/mod_loader/bootstrap/platform.lua` exists and `"platform"` precedes
  `"itb_io"` in `bootstrap/__scripts.lua`.
- Full behavioral verification happens via Task 4's testsuite when the game runs.

**Commit:** `feat: add platform-detection module for native library names`
<!-- END_TASK_1 -->

<!-- START_TASK_2 -->
### Task 2: Route itb_io load through the platform module

**Verifies:** linux-port.AC1.3, linux-port.AC1.4, linux-port.AC5.1

**Files:**
- Modify: `scripts/mod_loader/bootstrap/itb_io.lua:2-20` (the `lazy_load()` function)

**Implementation:**

Replace the hardcoded `"itb_io.dll"` in `lazy_load()` with the resolved name from
`Platform.nativeLibrary("itb_io")`, and use that same variable in the `LOG` and the
`error()` message so a missing/failed library names the actual file the loader tried
(linux-port.AC1.4). The registration (`factory = itb_io; itb_io = nil`) is unchanged —
`itb_io.so` exports `luaopen_itb_io` and registers the same global.

Current lines 2-20:

```lua
local function lazy_load()
	if factory ~= nil then
		return
	end

	try(function()
		LOG("Loading itb_io.dll...")
		package.loadlib("itb_io.dll", "luaopen_itb_io")()
		factory = itb_io
		itb_io = nil
		LOG("Successfully loaded itb_io.dll!")
	end)
	:catch(function(err)
		error(string.format(
				"Failed to load itb_io.dll: %s",
				tostring(err)
		))
	end)
end
```

Replace with:

```lua
local function lazy_load()
	if factory ~= nil then
		return
	end

	local library = Platform.nativeLibrary("itb_io")
	try(function()
		LOG(string.format("Loading %s...", library))
		package.loadlib(library, "luaopen_itb_io")()
		factory = itb_io
		itb_io = nil
		LOG(string.format("Successfully loaded %s!", library))
	end)
	:catch(function(err)
		error(string.format(
				"Failed to load %s: %s",
				library, tostring(err)
		))
	end)
end
```

**Testing:** Behavior is exercised when the loader boots (Phase 2, linux-port.AC1.1) and
by the platform-module testsuite (Task 4). No isolated test — the loadlib site requires
the real game process.

**Verification:**
- Grep confirms no `"itb_io.dll"` string literal remains in `bootstrap/itb_io.lua`.
- On Windows the resolved name is `itb_io.dll` (unchanged behavior, linux-port.AC1.5).

**Commit:** `refactor: route itb_io load through platform module`
<!-- END_TASK_2 -->

<!-- START_TASK_3 -->
### Task 3: Route ftldat load through the platform module

**Verifies:** linux-port.AC1.3, linux-port.AC1.4, linux-port.AC5.1

**Files:**
- Modify: `scripts/mod_loader/ftldat-rs/ftldat.lua:3-21` (the `lazy_load()` function)

**Implementation:**

Same transformation as Task 2, for the ftldat site. Current lines (3-21) hardcode
`"ftldat.dll"` in the `LOG`, `package.loadlib`, and `error()` calls; the registration is
`ftldat_rs = ftldat; ftldat = nil`. Replace the hardcoded filename with
`Platform.nativeLibrary("ftldat")`:

```lua
local function lazy_load()
	if ftldat_rs ~= nil then
		return
	end

	local library = Platform.nativeLibrary("ftldat")
	try(function()
		LOG(string.format("Loading %s...", library))
		package.loadlib(library, "luaopen_ftldat")()
		ftldat_rs = ftldat
		ftldat = nil
		LOG(string.format("Successfully loaded %s!", library))
	end)
	:catch(function(err)
		error(string.format(
				"Failed to load %s: %s",
				library, tostring(err)
		))
	end)
end
```

`ftldat.lua` is loaded via `scripts/mod_loader/__scripts.lua:15` (`ftldat-rs/__scripts`),
which runs after `bootstrap/__scripts` (line 2), so `Platform` is already defined.

**Testing:** Exercised at boot (Phase 2) and by Task 4's testsuite. No isolated test.

**Verification:**
- Grep confirms no `"ftldat.dll"` string literal remains in `ftldat-rs/ftldat.lua`.
- Grep for `%.dll"` under `scripts/mod_loader/` returns no matches in the two core load
  paths (partial linux-port.AC5.1). The memedit submodule
  (`scripts/mod_loader/extensions/modLoaderExtensions/mods/memedit/memedit.lua:76`) still
  contains a `memedit.dll` literal — that third site is routed in Phase 4 Task 3, which
  fully closes AC5.1. Do not treat AC5.1 as complete at end of Phase 1.

**Commit:** `refactor: route ftldat load through platform module`
<!-- END_TASK_3 -->

<!-- START_TASK_4 -->
### Task 4: Add an in-game testsuite for the platform module

**Verifies:** linux-port.AC1.2, linux-port.AC5.2

**Files:**
- Create: `scripts/mod_loader/tests/platform.lua`
- Modify: `scripts/mod_loader/tests/__scripts.lua` (register the new suite)
- Modify: `scripts/mod_loader/tests/main.lua` (add the suite to the run set)

**Implementation:**

Follow the existing test pattern (see `scripts/mod_loader/tests/modApi.lua` and
`scripts/mod_loader/tests/base.lua`): a testsuite built with `Tests.Testsuite()`, whose
tests are functions using `Assert.Equals` / `Assert.True` (from
`scripts/mod_loader/bootstrap/assert.lua`). Read `tests/modApi.lua` and one entry in
`tests/__scripts.lua` / `tests/main.lua` first to match registration exactly — mirror how
an existing suite is wired, do not invent a new mechanism.

Tests must verify the AC-relevant behavior of `Platform.nativeLibrary`:
- **linux-port.AC1.2:** For the current `Platform.name`, `Platform.nativeLibrary("itb_io")`
  returns `itb_io.dll` on Windows and `./itb_io.so` on Linux; likewise `ftldat`. Assert
  against the value expected for `Platform.name` so the same suite passes on both OSes.
- Unknown logical name raises an error (wrap the call in `pcall` and assert it failed) —
  supports the clear-error requirement (linux-port.AC1.4).
- `itbsdl` raises on Windows (no target) and returns `./libitbsdl.so` on Linux.

Because `Platform.name` is fixed per host, write the assertions to branch on
`Platform.name` so the suite is a genuine regression check on both platforms
(linux-port.AC5.2).

**Testing:** This task *is* the test. Verify tests exist and are registered in the run set.

**Verification:**
- Launch the game with the loader, open the in-game test console (see
  `scripts/mod_loader/modui/tests_console.lua`), run the suites, and confirm the
  `platform` suite passes. Per project convention this is the only way to run tests
  (there is no CI test run).

**Commit:** `test: add platform-detection module testsuite`
<!-- END_TASK_4 -->
<!-- END_SUBCOMPONENT_A -->

<!-- START_SUBCOMPONENT_B (tasks 5-6) -->
## Subcomponent B: Rust Linux builds

> These tasks are performed in the sibling repos, not in `ITB-ModLoader`. The build
> outputs (`itb_io.so`, `ftldat.so`) are copied into the `ITB-ModLoader` repo root,
> mirroring the committed-`.dll` convention (finding #9). Commits for the crate source
> changes land in their respective repos; the copied `.so` binaries are committed in
> `ITB-ModLoader`.

<!-- START_TASK_5 -->
### Task 5: Build `ftldat.so` (restore the Lua binding + cdylib)

**Verifies:** linux-port.AC1.3

**Repo:** `/var/home/displacer/Projects/clones/ftldat-rs`

**Files:**
- Modify: `Cargo.toml` (`crate-type`, add `mlua` dependency)
- Create: `src/lua_exports.rs` (restore from commit `1fa1f56^1`)
- Modify: `src/lib.rs` (re-add `luaopen_ftldat` and `mod lua_exports;`)
- Copy artifact into: `/var/home/displacer/Projects/clones/ITB-ModLoader/ftldat.so`

**Implementation:**

The Lua binding was removed from `ftldat-rs` master when the crate was split
(commit `1fa1f56`, "feature/crate-sans-lua"). Restore it so the crate again produces a
`luaopen_ftldat` entry point registering the global `ftldat` with `read_package` /
`new_package` — the exact contract `scripts/mod_loader/ftldat-rs/ftldat.lua` expects.

1. Recover the pre-split binding source and study it:
   ```bash
   cd /var/home/displacer/Projects/clones/ftldat-rs
   git show '1fa1f56^1:src/lua_exports.rs'   # the LuaUserData impl + init(lua)
   git show '1fa1f56^1:src/lib.rs'           # the luaopen_ftldat entry point
   ```
   Re-create `src/lua_exports.rs` from that content, adapting to the current
   `package`/`entry` module API if signatures drifted (verify against the current
   `src/package.rs` / `src/prelude.rs`). The registration exposes, at minimum:
   `new_package`, `read_package`, and the `Package` userdata methods used by
   `ftldat.lua` (`read_content_as_string`, `read_content_as_byte_array`,
   `put_entry_from_string`, `put_entry_from_byte_array`, `to_file`, etc. — reconcile the
   method list against `scripts/mod_loader/ftldat-rs/ftldat.lua`).

2. Re-add to `src/lib.rs` the module declaration and entry point:
   ```rust
   mod lua_exports;

   #[no_mangle]
   pub extern "C" fn luaopen_ftldat(lua_state: *mut mlua::lua_State) -> i32 {
       // The game owns the lua_State for the process lifetime; treat it as 'static.
       let lua = unsafe { mlua::Lua::init_from_ptr(lua_state) }.into_static();
       let ftldat = lua_exports::init(&lua)
           .expect("Failed to initialize ftldat module export table");
       lua.globals().set("ftldat", ftldat).unwrap();
       0
   }
   ```

3. Update `Cargo.toml`:
   ```toml
   [lib]
   name = "ftldat"
   crate-type = ["cdylib", "rlib"]

   [dependencies]
   mlua = { version = "0.8.3", features = ["lua51", "vendored"] }
   byteorder = { version = "1.4.3" }
   thiserror = "1.0.32"
   memmap2 = "0.5.10"
   ```
   `vendored` statically links a PUC Lua 5.1 matching the game (finding #2). Keeping
   `rlib` preserves the crate's usability as a dependency; adding `cdylib` produces the
   loadable `.so`.

4. Build and stage the artifact (game is x86-64 — build the 64-bit host target, not the
   32-bit Windows target the old `build.sh` used):
   ```bash
   cargo build --lib --release
   cp target/release/libftldat.so \
      /var/home/displacer/Projects/clones/ITB-ModLoader/ftldat.so
   ```
   Note the `.so` is `libftldat.so` in `target/`; it is staged as `ftldat.so` (no `lib`
   prefix) to match the name the platform module resolves (`./ftldat.so`).

**Testing:**
- `cargo test` in `ftldat-rs` must pass (the crate already has archive round-trip tests
  under `tests/`; the restored binding adds no new pure logic to test — the Lua surface
  is verified in-game). Run it to confirm the restored code compiles and existing tests
  are green.

**Verification:**
```bash
cd /var/home/displacer/Projects/clones/ftldat-rs && cargo test
nm -D /var/home/displacer/Projects/clones/ITB-ModLoader/ftldat.so | grep luaopen_ftldat
```
Expected: tests pass; `luaopen_ftldat` appears as an exported (`T`) dynamic symbol.

**Commit (ftldat-rs):** `feat: restore luaopen_ftldat binding and cdylib output`
**Commit (ITB-ModLoader):** `build: add Linux ftldat.so artifact`
<!-- END_TASK_5 -->

<!-- START_TASK_6 -->
### Task 6: Build `itb_io.so` and add the native-Linux savedata branch

**Verifies:** linux-port.AC1.3 (and prepares linux-port.AC2.3, exercised in Phase 2)

**Repo:** `/var/home/displacer/Projects/clones/itb-io-rs`

**Files:**
- Modify: `src/path_filter.rs:35` (`save_data_directory()` — add the native-Linux candidate)
- Copy artifact into: `/var/home/displacer/Projects/clones/ITB-ModLoader/itb_io.so`

**Implementation:**

`itb-io-rs` already builds as a `cdylib` exporting `luaopen_itb_io` (finding #6), so no
`Cargo.toml` change is needed for the artifact. The work is adding a native-Linux save
directory candidate to `save_data_directory()`, distinct from the existing Proton
`compatdata` path.

The native game writes to `~/.local/share/IntoTheBreach` (finding #8 — verified against
the local install, which contains `profile_*`, `settings.lua`, `io_test.txt`). The
`directories` crate (already a dependency) resolves it. Add this candidate to the
`candidates` vector in `save_data_directory()` **before** the Proton and `./user`
fallbacks so a genuine native install resolves to the correct location, while Proton
installs still fall through to the compatdata path:

```rust
// Native Linux (XDG data dir): ~/.local/share/IntoTheBreach
if let Some(proj_dirs) = directories::ProjectDirs::from("", "", "IntoTheBreach") {
    candidates.push(proj_dirs.data_dir().to_path_buf());
}
```

Place it in the candidate list immediately after the Windows `document_dir` push and
before the Proton `compatdata` push. The existing `is_save_data_location_valid` filter
already selects the first candidate that exists, so ordering gives native precedence
without breaking Proton (a native path won't exist under Proton and vice versa).

**Testing:**
- `itb-io-rs` has an existing test
  `dir_returned_by_save_data_directory_should_be_valid_save_data_location`
  (`src/path_filter.rs:107`). Run `cargo test`; it must remain green. Do not mock the
  filesystem — the existing test resolves a real directory, matching the project's
  approach. If the added branch needs coverage, assert that on a host where
  `~/.local/share/IntoTheBreach` exists the returned path equals it; otherwise leave the
  existing test as the guard (surface to the user if stronger isolation is wanted rather
  than introducing filesystem mocks).

**Verification:**
```bash
cd /var/home/displacer/Projects/clones/itb-io-rs
cargo build --lib --release
cp target/release/libitb_io.so \
   /var/home/displacer/Projects/clones/ITB-ModLoader/itb_io.so
cargo test
nm -D /var/home/displacer/Projects/clones/ITB-ModLoader/itb_io.so | grep luaopen_itb_io
```
Expected: build + tests pass; `luaopen_itb_io` is an exported dynamic symbol.

**Commit (itb-io-rs):** `feat: resolve native Linux XDG save directory`
**Commit (ITB-ModLoader):** `build: add Linux itb_io.so artifact`
<!-- END_TASK_6 -->
<!-- END_SUBCOMPONENT_B -->

---

## Phase 1 done when

- On Windows the platform module returns `.dll` names and loader behavior is unchanged
  (linux-port.AC1.5): `Platform.nativeLibrary("itb_io") == "itb_io.dll"`.
- On Linux the loader resolves `./itb_io.so` / `./ftldat.so` and both export their
  `luaopen_*` entries (linux-port.AC1.3).
- No `.dll` string literal remains in either **core** load site (`bootstrap/itb_io.lua`,
  `ftldat-rs/ftldat.lua`) — **AC5.1 is only partially closed here.** A third native-load
  site exists in the memedit submodule (`memedit.lua:76`); it is routed through the
  platform module in Phase 4 Task 3, which fully closes linux-port.AC5.1.
- The `platform` in-game testsuite passes; the existing `tests/` suites still pass
  (linux-port.AC5.2 — note the Windows run is manual-QA gated per Phase 5 Task 7).
- `cargo test` passes in both `ftldat-rs` and `itb-io-rs`; `ftldat.so` and `itb_io.so`
  are staged at the `ITB-ModLoader` repo root.

**Not yet validated in this phase:** actually booting the game to `onModsLoaded`
(linux-port.AC1.1) and confirming saves persist (linux-port.AC2.3) — these require the
full boot path and are verified in Phase 2.
