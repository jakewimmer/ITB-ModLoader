# libitbsdl — native Linux render/input library

Last verified: 2026-07-03

## Purpose
On Linux the loader UI (the "Configure Mods" button, console, toasts) cannot use
the Windows GDI+/opengl32 path. `libitbsdl.so` reimplements that rendering and
input on SDL2 + FreeType, plus the texture-hashing that drives `wasDrawn`/
`onModsLoaded`. It is loaded via the injected `package.loadlib` (as
`luaopen_itbsdl`) and shares the game's single Lua runtime.

## Contracts
- **Exposes**: exactly two kinds of symbols —
  - `luaopen_itbsdl` — Lua module registration (called from `itbsdl.lua`).
  - `itbsdl_dispatch_*` (`itbsdl_dispatch.h`) — the render/input entry points the
    `libitbboot` preload forwards SDL/GL calls into. These are the ONLY rendering
    symbols exported besides `luaopen_itbsdl`; `extern "C"`, default visibility, so
    libitbboot resolves them via `dlsym(RTLD_DEFAULT, ...)`.
- **Guarantees**: `itbsdl_dispatch_swapwindow` runs draw hooks but does NOT swap the
  window (the preload calls the real `SDL_GL_SwapWindow` after). `itbsdl_dispatch_
  event` returns 1 if a hook consumed the event, 0 to hand it to the game.
- **Expects**: `libitbboot.so` is preloaded and provides the game's `lua_*`
  symbols; a current GL context (resolved via `SDL_GL_GetCurrentWindow()`).

## Invariants (do not break)
- **No vendored Lua.** The library must leave every `lua_*` UNDEFINED and bind them
  to the game's runtime at load. Vendoring Lua gives a second GC over the same heap
  and corrupts it. CI (`package-linux` job) enforces this with `nm`: it fails if
  Lua internals (`luaH_free`, `sweeplist`, `luaC_step`, `luaD_call`) are defined,
  or if `lua_gettop` is not left undefined. The bundled `third_party/lua-5.1.5`
  and `LuaBridge` are compile-time headers ONLY — never link the Lua `.c` files in.
- **RGBA-order hashing.** Texture hashing assumes 8-bit `GL_RGBA`/`GL_UNSIGNED_BYTE`
  uploads (4 bytes/pixel); other formats/types are ignored. Surface hashing must
  stay in RGBA order or `wasDrawn`/`onModsLoaded` stop firing.

## Dependencies
- **Uses**: SDL2, FreeType, Fontconfig, OpenGL (GLVND); GLEW headers are bundled.
- **Used by**: `libitbboot` (forwards SDL/GL frames here); `itbsdl.lua` (registers
  the Lua module). See `itbboot/CLAUDE.md` for the forwarding rationale.
- **Boundary**: interposing SDL/GL symbols directly here does NOT work — the preload
  owns interposition. This library only exposes `itbsdl_dispatch_*`.

## Key Files
- `src/itbsdl.cpp`, `src/itbsdl.h` — Lua module + core
- `src/itbsdl_dispatch.h` — the dispatch ABI libitbboot calls
- `src/screen_gl.*`, `src/gl_interpose.cpp`, `src/text_freetype.*` — GL + text
- `CMakeLists.txt` — build; `test/` — ctest suite run in CI

## Gotchas
- C++ libs on the game's Lua must include `lua.hpp` (not bare `lua.h`) or
  `luaopen_*` segfaults on the C++/C linkage mismatch.
- Build + verify locally the way CI does: `cmake -B build -S . && cmake --build
  build && (cd build && ctest)`, then the `nm` single-runtime checks.
