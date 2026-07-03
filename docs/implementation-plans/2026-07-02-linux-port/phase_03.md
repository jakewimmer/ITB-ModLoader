# Native Linux Support — Phase 3: `libitbsdl.so` (SDL2 + FreeType render/input library)

**Goal:** The in-game mod-loader UI, console, and toasts render and take input on native
Linux, by reproducing the Windows proxy's `sdl.*` / `os.*` Lua API in a new
`libitbsdl.so`.

**Architecture:** One C++ shared object is loaded twice from a single file: `LD_PRELOAD`'d
so its `SDL_GL_SwapWindow` / `SDL_PollEvent` interposers run, and `package.loadlib`'d so
`luaopen_itbsdl(L)` registers the `sdl`/`os` tables and captures the game's `lua_State`.
Because the same file is loaded twice, the two roles share module globals (the draw/event
hook lists and the captured `L`). Screen drawing uses the game's live OpenGL context
(fixed-function GL, exactly as the Windows proxy); text uses FreeType in place of GDI+.

**Tech Stack:** C++ (ported from `DLL-Extensions`), SDL2, OpenGL (libGL), FreeType,
LuaBridge (header-only, reused from the proxy), a statically-linked PUC Lua 5.1.5 (matching
the game's ABI), `dlsym(RTLD_NEXT, …)` for interposition.

**Scope:** Phase 3 of 5. Depends on Phase 2 (the spike fixes the interception + `lua_State`
capture mechanism). Independent of Phase 4.

**Codebase verified:** 2026-07-02 (`codebase-investigator` mapped the full `sdl.*`/`os.*`
usage in the loader and the proxy implementation; proxy render path inspected directly).

---

## Why the dual-load is necessary (read first)

On Windows the proxy is a single preloaded DLL that hooks `luaL_newstate` and calls
`installFunctions(L)` to inject the `sdl`/`os` tables (`DLL-Extensions/lua-hooks.cc:8-14`).
That approach is **impossible on Linux**: the game statically links PUC Lua 5.1.5 and does
**not** export `luaL_newstate` (or any `lua_*`) in its dynamic symbol table (Phase 1,
finding #2), so an `LD_PRELOAD` library cannot interpose the game's internal
`luaL_newstate` call. The game *does* export its SDL imports dynamically, so `LD_PRELOAD`
*can* interpose `SDL_GL_SwapWindow`/`SDL_PollEvent`.

Therefore the registration side must be triggered from Lua: `modloader.lua` (or an early
bootstrap module) calls `package.loadlib("./libitbsdl.so", "luaopen_itbsdl")`, which hands
the game's `L` to the library. The Phase 2 spike proved that the `dlopen` of the same path
already `LD_PRELOAD`'d returns the same object, so the captured `L` and the SDL
interposers share module globals. **Registering the `sdl` table before the loader UI first
uses it** means `luaopen_itbsdl` must run early in bootstrap (before `modui/__scripts`).

Because the library statically links its own PUC Lua 5.1.5 (to resolve `lua_*`), and the
game *is* PUC 5.1.5, operating on the game's `L` through the vendored functions is
ABI-safe — the same argument as the Rust `.so`s in Phase 1.

---

## Contract to reproduce (from the proxy)

The library must register exactly the API the loader consumes. Source of truth:
`DLL-Extensions/api.md` (documented contract), `lua-functions.cc:140-341` (registration),
`sdl-utils.cpp` / `sdl-utils.h` (implementations), `sdl-hooks.cpp` (interception),
`os.cc` (filesystem). Loader usage was confirmed in `ITB-ModLoader`:

- **`sdl.drawHook(fn)`** — `modui/root.lua:226`. `fn(screen)` runs each frame; drives
  `onFrameDrawStart:dispatch(screen)` / `onFrameDrawn:dispatch(screen)` (`root.lua:308,312`)
  and `onGameWindowResized` (dispatched at `root.lua:269` when `screen:w()/h()` change).
  The library calls `screen:begin()`/`finish()` around the hook — the Lua `fn` must not.
- **`sdl.eventHook(fn)`** — `modui/root.lua:327`. `fn(event)` returns `true` to consume.
  `event` exposes `:type()`, `:keycode()`, `:x()`, `:y()`, `:wheel()`, `:mousebutton()`,
  `:textinput()`. Console toggles on `SDLKeycodes.BACKQUOTE` (`root.lua:316`); text entry
  needs `sdl.events.textinput` and `SDL_StartTextInput` (imported by the game).
- **`sdl.screen()`** with `w`, `h`, `begin`, `finish`, `blit`, `blitRect`, `drawrect`,
  `clip`, `unclip`, `mask`, `unmask`, `clearmask`, `getClipRect`.
- **Surfaces:** `sdl.surface(path)`, `surfaceFromBlob`, `text`, `outlined`, `scaled`,
  `multiply`, `grayscale`, `colormapped`, `screenshot`; surface `:w() :h() :wasDrawn()`
  and `.x .y` (populated after `wasDrawn`).
- **Color/rect/font/text:** `sdl.rgb`, `sdl.rgba`, `sdl.color`, `sdl.rect`,
  `sdl.textsettings` (`antialias`, `color`, `outlineWidth`, `outlineColor`), `sdl.font`,
  `sdl.filefont`, `sdl.filefontFromBlob`, `sdl.text`.
- **Misc:** `sdl.timer`, `sdl.mouse.x/y`, `sdl.events.*` (quit, keydown, keyup,
  mousemotion, mousebuttondown, mousebuttonup, mousewheel, textinput), `sdl.clipboard`,
  `sdl.resourceDat`, `sdl.blobFromFile`, `sdl.blobFromResourceDat`.
- **`os` additions:** `os.log`, `os.isshiftdown`, `os.mtime`, `os.getKnownFolder`,
  `os.mkdir`, `os.messagebox`, `os.listall`, `os.listfiles`, `os.listdirs`.
  `bootstrap/security.lua` later nils `os.listdirs`, `os.listfiles`, `os.remove`,
  `os.getKnownFolder` — so these must exist at bootstrap time but need not survive it.

**Render path is portable:** the proxy's `Screen`/`Surface` already draw with
fixed-function OpenGL (`glGenTextures`/`glTexImage2D`/textured quads/`glReadPixels`,
window via `SDL_GL_GetCurrentWindow()` — `sdl-utils.cpp:33,654-690,719-792`). This ports
to Linux essentially unchanged. The only genuinely new work: **text** (GDI+ →
FreeType, `sdl-utils.h:53-102`), **filesystem** (`os.cc` Win32 → POSIX/XDG), **messagebox**
(→ `SDL_ShowSimpleMessageBox`), and **interception glue** (`HOOK_SDL` proxy trampolines →
`dlsym(RTLD_NEXT)`). Image decoding for `sdl.surface(path)` — confirm what the proxy uses
(`sdl-utils.cpp` surface-from-file) and match it (e.g. stb_image, header-only, portable).

**Proxy source location:** all `DLL-Extensions/…` paths in this phase refer to the clone at
`/var/home/displacer/Projects/clones/DLL-Extensions` (cloned during planning from
`github.com/itb-community/DLL-Extensions`). Its `LuaBridge/` headers are reused verbatim.

---

## Execution finding 2026-07-02 — build environment + integration with Phase 2

**Build toolchain — RESOLVED via Homebrew (Linuxbrew), 2026-07-02.** The host is immutable
ostree/Bazzite (no `cmake`, no system `-devel` headers), but Homebrew is installed at
`/home/linuxbrew/.linuxbrew` (user-writable, no root/reboot). Installed:
`cmake` (4.3.4), `sdl2` (2.32.70, via `sdl2-compat` = SDL2 ABI over SDL3), `pkgconf`;
`freetype` (26.6.20) and `fontconfig` (2.18.1) were already present. `g++` 16.1.1 is on the
host. Build with `PATH` and `PKG_CONFIG_PATH` pointed at the brew prefix:
`export PATH=/home/linuxbrew/.linuxbrew/bin:$PATH` and
`export PKG_CONFIG_PATH=/home/linuxbrew/.linuxbrew/lib/pkgconfig:/home/linuxbrew/.linuxbrew/share/pkgconfig`.
`brew install` needs network + writes under `/home/linuxbrew` and `~/.cache/Homebrew`, both
outside the command sandbox — run those with the sandbox disabled.

- **SDL2 at build time is only for headers/symbols.** `libitbsdl.so` interposes
  `SDL_GL_SwapWindow`/`SDL_PollEvent` and *calls* a few SDL fns
  (`SDL_GL_GetCurrentWindow`, `SDL_GetModState`, `SDL_StartTextInput`,
  `SDL_ShowSimpleMessageBox`). Those must resolve at runtime against the **game's** bundled
  `libSDL2-2.0.so.0` (in `linux_x64/`), NOT brew's. So do **not** add a `DT_NEEDED` on brew's
  libSDL2 (that would load a second SDL2 → two event queues/contexts). Leave the SDL_*
  references undefined and let them bind from the game's already-loaded SDL2 (linked via
  LD_PRELOAD global scope), e.g. link with `-Wl,--unresolved-symbols=ignore-all` for the SDL
  symbols or simply don't pass `-lSDL2`. Verify the built `.so` has **no** `libSDL2` in
  `readelf -d` NEEDED. (sdl2-compat headers are standard SDL2 headers — fine to compile
  against.)
- **OpenGL:** the host has runtime `libGL.so.1` / `libOpenGL.so.0` (GLVND) but **no
  `GL/gl.h`**. Use the proxy's bundled GLEW headers (`DLL-Extensions/glew/glew.h` +
  `glew.c`) for GL declarations rather than system GL headers, and link GL via GLVND
  (`OpenGL::OpenGL`, i.e. `libOpenGL.so.0`) — adjust the Task-1 `find_package(OpenGL)` to the
  GLVND target if the legacy `libGL.so` dev symlink is absent.
- **FreeType/Fontconfig** are genuine runtime deps of `libitbsdl.so` (the game doesn't
  provide them). Locally they load from brew paths; **Phase 5 packaging must bundle them (or
  static-link)** so the release doesn't depend on the user's brew. Flag for Phase 5.
- Phase 5 CI must replicate this toolchain (brew, or distro `-devel` packages in the CI image).

**Integration with the Phase 2 bootstrap (`libitbboot.so`):**
- `libitbsdl`'s registration side is triggered by `package.loadlib("./libitbsdl.so",
  "luaopen_itbsdl")`. That works only because Phase 2's `libitbboot.so` injects a working
  `package.loadlib` first (the game's native `loadlib` is stubbed, finding #9). `libitbboot`
  injects at the first `scripts/` open, before `bootstrap/itbsdl.lua` runs — ordering holds.
- **Two preloaded libraries both interposing `SDL_GL_SwapWindow` would double-run the frame
  hook.** `libitbboot`'s SDL interposer was only the Phase-2 proof; in Phase 3 **remove it**
  so only `libitbsdl` interposes SDL, and `libitbboot` stays a pure loadlib-injector. Launch
  preloads both, `libitbboot` first, using relative slash-bearing paths from the game cwd
  (`LD_PRELOAD="./libitbboot.so:./libitbsdl.so"`) — absolute paths break because the game
  dir contains spaces (Phase 2 launch note).
- The placeholder `bootstrap/sdl.lua` (commit `9f686b7`) must be removed/neutralized so it
  does not shadow the real `sdl` table `luaopen_itbsdl` registers.

## Acceptance Criteria Coverage

### linux-port.AC3: The in-game loader UI renders and takes input
- **linux-port.AC3.1 Success:** `sdl.drawHook` fires `onFrameDrawStart`/`onFrameDrawn` each frame; the mod-config menu renders.
- **linux-port.AC3.2 Success:** The console toggles on backquote via `sdl.eventHook` and accepts typed input.
- **linux-port.AC3.3 Success:** Toast notifications render.
- **linux-port.AC3.4 Success:** UI text renders legibly via FreeType (visual parity check against Windows).
- **linux-port.AC3.5 Edge:** Resizing the game window repositions the loader UI correctly (`onGameWindowResized`).

> The `sdl.*` surface has no in-game automated tests (it requires the live GL context and
> window). Verification is operational: run the game and observe the UI. Task 6 defines
> the concrete observations; `test-requirements.md` captures the manual QA.

---

<!-- START_SUBCOMPONENT_A (task 1) -->
## Subcomponent A: Project skeleton and build

<!-- START_TASK_1 -->
### Task 1: Create the `libitbsdl` source tree and build

**Verifies:** Setup (no AC of its own)

**Files:**
- Create: `linux/libitbsdl/` (new subtree in this repo — keeps the single upstreamable
  codebase; the `.so` is staged at the repo root like the other artifacts)
- Create: `linux/libitbsdl/CMakeLists.txt`
- Create: `linux/libitbsdl/src/itbsdl.cpp` (entry point + module globals; filled in by later tasks)
- Create: `linux/libitbsdl/third_party/lua-5.1.5/` — vendored PUC Lua 5.1.5 sources + a
  `CMakeLists.txt` producing a static `lua51_static` target (see step 1 below)
- Vendor: `linux/libitbsdl/third_party/LuaBridge/` headers (copy from
  `/var/home/displacer/Projects/clones/DLL-Extensions/LuaBridge/`)

**Implementation:**

Establish a CMake build producing `libitbsdl.so`, linking SDL2, OpenGL, FreeType, and a
statically-linked PUC Lua 5.1.5 (the game's ABI — do not dynamically link a system Lua).
LuaBridge is header-only and reused verbatim from the proxy. Target x86-64 (the game is
64-bit). Compile position-independent (`-fPIC`) since it is a shared object.

1. **Vendor PUC Lua 5.1.5 as a static target (shared with Phase 4).** Download the PUC
   Lua 5.1.5 sources into `third_party/lua-5.1.5/` and add a `CMakeLists.txt` there that
   compiles the core `.c` files (excluding `lua.c`/`luac.c`) into a static library target
   `lua51_static` with `POSITION_INDEPENDENT_CODE ON`. Phase 4 Task 1 vendors the **same
   5.1.5 sources** for `memedit.so` in its own repo (`libitbsdl` and `memedit-vsproject`
   are separate git repos, so each carries its own physical copy — they cannot share one
   `add_subdirectory` tree). Vendor the identical upstream 5.1.5 tarball in both so the two
   C++ artifacts link an ABI-identical Lua.

2. **The top-level `CMakeLists.txt` lists only `itbsdl.cpp` in this task.** Each later task
   adds its own source file to the `add_library` list when it creates that file (Task 2
   adds `interpose.cpp`, Task 3 `screen_gl.cpp`, Task 4 `text_freetype.cpp`, Task 5
   `os_posix.cpp`). This keeps every task's build green in listed order — do not list a
   source before the task that creates it.

```cmake
cmake_minimum_required(VERSION 3.16)
project(itbsdl CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Freetype REQUIRED)

add_subdirectory(third_party/lua-5.1.5)  # produces static target `lua51_static`

# Sources grow as later tasks land: Task 2 adds interpose.cpp, Task 3 screen_gl.cpp,
# Task 4 text_freetype.cpp, Task 5 os_posix.cpp. Task 1 lists only itbsdl.cpp.
add_library(itbsdl SHARED
    src/itbsdl.cpp
)
target_include_directories(itbsdl PRIVATE
    src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LuaBridge
    ${SDL2_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(itbsdl PRIVATE
    lua51_static ${SDL2_LIBRARIES} OpenGL::GL ${FREETYPE_LIBRARIES} dl)

# Output as libitbsdl.so; staged to repo root by the packaging step.
set_target_properties(itbsdl PROPERTIES OUTPUT_NAME itbsdl PREFIX "lib")
```

Provide an initial `src/itbsdl.cpp` with the shared module globals and a stub
`luaopen_itbsdl` so the tree builds before later tasks flesh it out:

```cpp
// Shared module state. Because the same .so is loaded via LD_PRELOAD and package.loadlib,
// these globals are shared between the SDL interposers and the Lua-registered callbacks.
#include <lua.hpp>
#include <vector>

lua_State *g_lua = nullptr;                  // captured at luaopen_itbsdl
std::vector<void *> g_draw_hooks;            // typed properly in Task 2
std::vector<void *> g_event_hooks;

extern "C" int luaopen_itbsdl(lua_State *L) {
    g_lua = L;
    // install_sdl_namespace(L); install_os_namespace(L);  // wired up in Tasks 2–5
    return 0;
}
```

**Verification:**
```bash
cd linux/libitbsdl && cmake -B build -S . && cmake --build build
file build/libitbsdl.so   # ELF 64-bit shared object
nm -D build/libitbsdl.so | grep -E "luaopen_itbsdl|SDL_GL_SwapWindow"
```
Expected: builds; `luaopen_itbsdl` is exported. (`SDL_GL_SwapWindow` becomes an exported
definition once Task 2 lands.)

**Commit:** `build: scaffold libitbsdl (SDL2/GL/FreeType/Lua) build`
<!-- END_TASK_1 -->
<!-- END_SUBCOMPONENT_A -->

<!-- START_SUBCOMPONENT_B (task 2) -->
## Subcomponent B: Dual-load core — interception + registration

<!-- START_TASK_2 -->
### Task 2: SDL interposition, hook lists, and Lua registration

**Verifies:** linux-port.AC3.1, linux-port.AC3.2, linux-port.AC3.5 (mechanism); completes
after Subcomponents C/D provide the drawing/text the hooks invoke.

**Files:**
- Create: `linux/libitbsdl/src/interpose.cpp`
- Modify: `linux/libitbsdl/src/itbsdl.cpp` (register `sdl.drawHook`/`sdl.eventHook`/`sdl.events`)
- Modify (loader): add the `package.loadlib` of `libitbsdl` early in bootstrap — see below

**Implementation:**

1. **Interpose `SDL_GL_SwapWindow`** — port `DLL-Extensions/sdl-hooks.cpp:17-33`, replacing
   the proxy trampoline with `dlsym(RTLD_NEXT, …)`. Each frame: if draw hooks exist,
   build a `Screen`, `begin()`, invoke each hook with it, `finishWithoutSwapping()`, then
   call the real swap.

```cpp
#define _GNU_SOURCE
#include <dlfcn.h>
#include <SDL2/SDL.h>
// module globals + Screen/DrawHook/EventHook declared in itbsdl.h

extern "C" void SDL_GL_SwapWindow(SDL_Window *window) {
    static void (*real)(SDL_Window *) =
        (void (*)(SDL_Window *))dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");

    if (g_lua && !g_draw_hooks.empty()) {
        Screen screen;                 // wraps SDL_GL_GetCurrentWindow(), Task 3
        screen.begin();
        for (auto it = g_draw_hooks.rbegin(); it != g_draw_hooks.rend(); ++it)
            (*it)->draw(screen);       // calls the Lua fn(screen) via LuaBridge/LuaRef
        screen.finishWithoutSwapping();
    }
    real(window);
}
```

2. **Interpose `SDL_PollEvent`** — port `sdl-hooks.cpp:35-59`. Loop: pull a real event;
   feed it to each event hook; if a hook returns `true` (consumed), keep pulling; if none
   consume, return the event to the game. **Input injection note (Phase 2, finding #8):**
   the game imports only `SDL_PollEvent` (not `SDL_PushEvent`), so any events the loader
   needs to *inject* into the game must be surfaced through this interceptor's return path
   rather than pushed to the SDL queue.

```cpp
extern "C" int SDL_PollEvent(SDL_Event *evt) {
    static int (*real)(SDL_Event *) =
        (int (*)(SDL_Event *))dlsym(RTLD_NEXT, "SDL_PollEvent");

    if (!g_lua || g_event_hooks.empty() || evt == nullptr)
        return real(evt);

    for (;;) {
        int ret = real(evt);
        if (ret == 0) return 0;
        Event e; e.raw = *evt;
        bool handled = false;
        for (auto *hook : g_event_hooks)
            if (hook->handle(e)) { handled = true; break; }
        if (!handled) return 1;        // pass unconsumed event to the game
    }
}
```

3. **Register `sdl.drawHook`/`sdl.eventHook`/`sdl.events`** and the hook-lifetime
   semantics: the constructor pushes the hook onto `g_draw_hooks`/`g_event_hooks`; the
   `__gc`/destructor removes it (the proxy relies on Lua GC — "store the result in a
   global or the hook is removed"; reproduce that). Use LuaBridge exactly as
   `lua-functions.cc` does for `DrawHook`/`EventHook`/`Event` and the `events` namespace
   constants.

4. **Load the library from Lua early.** The `sdl` table must exist before the loader UI
   uses it. Add a bootstrap step that runs before `modui/__scripts` (loaded at
   `scripts/mod_loader/__scripts.lua:12`). On Linux only (guard with `Platform.name ==
   "linux"`), `package.loadlib(Platform.nativeLibrary("itbsdl"), "luaopen_itbsdl")()`.
   Place this in a new `scripts/mod_loader/bootstrap/itbsdl.lua` registered in
   `bootstrap/__scripts.lua` after `platform` and before the modui load, mirroring the
   `itb_io.lua` lazy-load pattern. On Windows this is a no-op (the proxy already provides
   `sdl`), so the module returns early when `Platform.name == "windows"`.

**Verification (mechanism):**
```bash
nm -D linux/libitbsdl/build/libitbsdl.so | grep -E "SDL_GL_SwapWindow|SDL_PollEvent"
```
Expected: both are exported (so `LD_PRELOAD` overrides the game's imports). Full
behavioral verification is Task 6 (needs the game running with Subcomponents C/D present).

**Commit:** `feat: SDL interposition, hook lists, and sdl.drawHook/eventHook registration`
<!-- END_TASK_2 -->
<!-- END_SUBCOMPONENT_B -->

<!-- START_SUBCOMPONENT_C (task 3) -->
## Subcomponent C: OpenGL screen and surfaces

<!-- START_TASK_3 -->
### Task 3: Port `Screen`, `Surface`, `Rect`, `Color`, `Timer`, `mouse`

**Verifies:** linux-port.AC3.1, linux-port.AC3.3

**Files:**
- Create: `linux/libitbsdl/src/screen_gl.cpp` (+ header)
- Modify: `linux/libitbsdl/src/itbsdl.cpp` (register these classes)

**Implementation:**

Port the OpenGL rendering from `DLL-Extensions/sdl-utils.cpp` — it is already
platform-agnostic fixed-function GL and needs little change:
- `glTexture()` texture upload (`sdl-utils.cpp:33-63`), `Screen::begin/finish/
  finishWithoutSwapping` (`:685-717`) setting up the ortho projection over
  `SDL_GL_GetDrawableSize`, `Screen::blit/blitRect/drawrect/clip/mask` (`:719-800`),
  `screenshot` via `glReadPixels` (`:654-690`).
- `Surface` from image file/blob (upload to GL texture, `wasDrawn` bookkeeping via
  `texturesMap`/`lastFrameMap`, `.x/.y` populated on draw), and the derived transforms
  `outlined`, `scaled`, `multiply`, `grayscale`, `colormapped`. Match the proxy's
  `wasDrawn` mechanism (it correlates textures drawn in the previous frame — needed by
  mods, and by the loader's `repairIcon:wasDrawn()`-style checks).
- `Rect` (`contains`/`intersects`/`getIntersect`/`getUnion`), `Color`
  (`rgb`/`rgba`/statics), `Timer` (`elapsed`/`reset`), `sdl.mouse.x/y`.

Register all of them into the `sdl` namespace with the **exact** LuaBridge signatures from
`lua-functions.cc:143-324` (do not rename methods — the loader binds to these names).

Confirm and match the image decoder the proxy uses for `sdl.surface(path)` (inspect
`sdl-utils.cpp` surface-from-file); if it is stb_image or SDL_image, use the same so PNG
decoding and premultiplication match Windows pixel-for-pixel.

**Verification:** Compiles and links against SDL2/GL. Behavioral verification (menu and
toasts actually rendering) is Task 6. A useful intermediate check: load the library in a
minimal GL harness and blit a test surface + `drawrect` to confirm the GL path draws.

**Commit:** `feat: port OpenGL Screen/Surface/Rect/Color/Timer to libitbsdl`
<!-- END_TASK_3 -->
<!-- END_SUBCOMPONENT_C -->

<!-- START_SUBCOMPONENT_D (task 4) -->
## Subcomponent D: FreeType text

<!-- START_TASK_4 -->
### Task 4: `Font`/`FileFont`/`TextSettings`/`sdl.text` via FreeType

**Verifies:** linux-port.AC3.4

**Files:**
- Create: `linux/libitbsdl/src/text_freetype.cpp` (+ header)
- Modify: `linux/libitbsdl/src/itbsdl.cpp` (register font/text classes)

**Implementation:**

Replace the GDI+ text path (`DLL-Extensions/sdl-utils.h:53-102`) with FreeType. Provide:
- `sdl.font(name, size)` — resolve a system font by family name. Use Fontconfig
  (`FcNameParse`/`FcFontMatch`) to map a family name to a file, then load with FreeType.
  (Fontconfig is the standard Linux name→file resolver; add it to the build if used.)
- `sdl.filefont(path, size)` and `sdl.filefontFromBlob(blob, size)` — load a TTF/OTF from
  a path or an in-memory blob (`FT_New_Face` / `FT_New_Memory_Face`). The loader loads
  fonts from `resource.dat` blobs (`sdlext/extensions.lua:79-82`), so the blob path must
  work.
- `sdl.textsettings` with `antialias`, `color`, `outlineWidth`, `outlineColor`.
- `sdl.text(font, settings, utf8string)` — rasterize a UTF-8 string into an RGBA
  `Surface` (a GL texture, so it composes with Subcomponent C's `blit`). Implement:
  shape left-to-right using glyph advances; render each glyph with
  `FT_Load_Char(..., FT_LOAD_RENDER)` (or `FT_LOAD_TARGET_MONO` when `antialias == false`);
  blend into an RGBA buffer at the pen position using ascent for the baseline; when
  `outlineWidth > 0`, use an `FT_Stroker` pass in `outlineColor` under the fill. Expose
  font `ascent`/`descent` metrics (the loader uses text height for layout).

**Font parity note (design risk):** FreeType metrics differ from GDI+. After rendering
works, compare text layout against a Windows/Proton reference (line height, glyph
spacing) and adjust baseline/advance rounding to match. This is the AC3.4 "visual parity"
check and is verified in Task 6.

**Verification:** Rasterize a known string to a surface and dump it to PNG in a harness;
confirm glyphs, antialiasing, and outline render. Legibility/parity is confirmed in-game
(Task 6).

**Commit:** `feat: FreeType text rendering for libitbsdl`
<!-- END_TASK_4 -->
<!-- END_SUBCOMPONENT_D -->

<!-- START_SUBCOMPONENT_E (task 5) -->
## Subcomponent E: POSIX `os.*`, blobs, and resourceDat

<!-- START_TASK_5 -->
### Task 5: Port `os.*` filesystem/util functions and blob/resourceDat

**Verifies:** Bootstrap prerequisites for linux-port.AC3.* (the loader lists mods, reads
fonts/images from `resource.dat`, etc. during UI construction)

**Files:**
- Create: `linux/libitbsdl/src/os_posix.cpp`
- Modify: `linux/libitbsdl/src/itbsdl.cpp` (register the `os` additions, `resourceDat`, blobs)

**Implementation:**

Port `DLL-Extensions/os.cc` and the blob/resourceDat classes, replacing Win32 with POSIX:
- `os.listall`/`os.listfiles`/`os.listdirs(dir)` — non-recursive enumeration via
  `opendir`/`readdir`/`stat` (replacing `FindFirstFile`). Return Lua array tables exactly
  as the proxy's `listDirectory*` do.
- `os.getKnownFolder(id)` — map the Windows known-folder ids the loader passes to XDG
  paths (e.g. documents/data → `$XDG_DATA_HOME`, i.e. `~/.local/share`). Enumerate which
  ids the loader actually requests and map those; error clearly on an unmapped id.
- `os.mkdir` (`mkdir(2)` + `errno`), `os.mtime` (`stat` mtime), `os.log` (append to
  `log.txt` in the save dir), `os.isshiftdown` (`SDL_GetModState() & KMOD_SHIFT`),
  `os.messagebox` (`SDL_ShowSimpleMessageBox`).
- `sdl.resourceDat(path)` + `:reload()`, `sdl.blobFromFile`, `sdl.blobFromResourceDat` —
  the archive reader. This overlaps `ftldat`; match the proxy's `ResourceDatFile`/`Blob`
  behavior (it reads entries out of `resource.dat` for fonts/images). Reuse the proxy's
  implementation adjusted for endianness/paths; it is plain file I/O.

Do **not** work around `security.lua` nilling `os.listdirs`/`listfiles`/`remove`/
`getKnownFolder` — provide them; the loader uses them during bootstrap and security
removes them afterward by design.

**Verification:** Unit-exercise the enumeration and known-folder mapping in a small C++
harness (`os.listdirs("mods")` returns the expected entries; `getKnownFolder` returns
`~/.local/share/...`). Full path is exercised in Task 6.

**Commit:** `feat: POSIX os.* filesystem, known-folder, blob/resourceDat for libitbsdl`
<!-- END_TASK_5 -->
<!-- END_SUBCOMPONENT_E -->

<!-- START_SUBCOMPONENT_F (task 6) -->
## Subcomponent F: In-game integration verification

<!-- START_TASK_6 -->
### Task 6: Verify UI, console, toasts, text, and resize in-game

**Verifies:** linux-port.AC3.1, linux-port.AC3.2, linux-port.AC3.3, linux-port.AC3.4, linux-port.AC3.5

**Files:** none (operational). Requires an interactive (non-headless) session — the
checks are visual.

**Implementation & verification:**

Stage the built `libitbsdl.so` at the repo root and into the game dir, then launch with
the preload set:
```bash
GAME="/var/mnt/2891f9ef-34b4-4d43-9dbf-c91b2ac9c36e/SteamLibrary/steamapps/common/Into the Breach"
cp linux/libitbsdl/build/libitbsdl.so "$GAME/"
( cd "$GAME" && LD_PRELOAD="$PWD/libitbsdl.so" ./Breach )
```

Confirm each AC by observation:
- **AC3.1** — the mod-config menu renders; `onFrameDrawStart`/`onFrameDrawn` fire each
  frame (add a temporary frame counter `LOG` in a `onFrameDrawn` subscriber and confirm
  it advances in `modloader.log`).
- **AC3.2** — pressing backquote toggles the console (`onConsoleToggled`), and typed
  characters appear (exercises `SDL_PollEvent` interception + `sdl.events.textinput` +
  `SDL_StartTextInput`).
- **AC3.3** — trigger a toast notification and confirm it renders.
- **AC3.4** — UI text is legible; compare layout against a Windows/Proton reference and
  note any metric adjustments made in Task 4.
- **AC3.5** — resize the game window; the loader UI repositions correctly
  (`onGameWindowResized` dispatched at `root.lua:269`).

Record results in the manual QA section of `test-requirements.md`.

**Commit:** none (verification task). Any parity fixes land as follow-up commits to
Tasks 3–5.
<!-- END_TASK_6 -->
<!-- END_SUBCOMPONENT_F -->

---

## Phase 3 done when

- `libitbsdl.so` builds (SDL2 + OpenGL + FreeType + static PUC Lua 5.1.5) and exports
  `luaopen_itbsdl`, `SDL_GL_SwapWindow`, `SDL_PollEvent`.
- Loaded via `LD_PRELOAD` + `package.loadlib`, it registers the full `sdl.*`/`os.*`
  contract and shares `lua_State`/hook-list state across both roles.
- In-game: the mod-config menu, console (backquote toggle + typed input), and toasts
  render and take input; `onFrameDrawStart`/`onFrameDrawn` fire each frame; text renders
  legibly via FreeType; window resize repositions the UI (linux-port.AC3.1–AC3.5).
- Windows is untouched (the loader's `sdl` still comes from the proxy; the Linux
  `package.loadlib` step is guarded by `Platform.name == "linux"`).
