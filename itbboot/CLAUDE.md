# libitbboot — LD_PRELOAD bootstrap (Linux)

Last verified: 2026-07-03

## Purpose
The native Linux Breach binary embeds PUC Lua 5.1.5 built without `LUA_DL_DLOPEN`:
`package.loadlib` is a stub that errors "dynamic libraries not enabled", and the
binary imports `dlsym` but never `dlopen`. So no `.so` can be loaded from Lua as
the loader expects. `libitbboot.so`, preloaded via the Steam launch option
`LD_PRELOAD="$PWD/libitbboot.so" %command%`, fixes this. It is the ONLY library
that is preloaded; everything else loads on demand through it.

## Contracts (what this preload provides)
- A working `dlopen`-backed `package.loadlib` injected into the game's globals,
  with PUC semantics (`nil, msg, "open"|"init"` on failure).
- The game's own Lua 5.1 C API re-exported as absolute dynamic symbols
  (`lua_exports.S`), so the native mod `.so`s (`ftldat`, `itb_io`, `libitbsdl`)
  resolve their undefined `lua_*` to the game's single shared Lua runtime.
- SDL/GL interposers (`SDL_GL_SwapWindow`, `SDL_PollEvent`, `glBindTexture`,
  `glTexImage2D`, `glDrawArrays`, `glDrawElements`) that forward into libitbsdl's
  `itbsdl_dispatch_*` exports (resolved lazily via `dlsym(RTLD_DEFAULT, ...)` once
  libitbsdl loads mid-run).

## How it captures Lua state
A one-shot inline hook on `lua_getfield` (fixed address, from `lua_addrs.h`)
stashes `L` (in RDI on entry), restores the original prologue, and re-runs the
real function unhooked. Injection happens from the `fopen`/`open` libc interposers
on the first `scripts/` open — NOT from inside the hook (calling back into Lua
during the hook is unsafe reentrancy).

## Invariants (do not break)
- **Single Lua runtime.** libitbboot is the sole provider of `lua_*` to all native
  libs. No native lib may vendor its own Lua (multi-GC corrupts the heap). CI
  asserts libitbsdl leaves `lua_*` undefined; keep it that way for any new lib.
- **Non-PIE fixed addresses.** The `lua_getfield` and re-exported `lua_*` addresses
  are absolute (ET_EXEC, no ASLR slide). They are game-version-specific — regenerate
  `lua_addrs.h` / `lua_exports.S` with `gen_lua_addrs.sh` / `gen_lua_exports.sh`
  against a new `Breach` binary. `build.sh` regenerates exports when `GAME` is set.
- **Capture-stub 16-byte alignment.** The asm stub in `itbboot.c` must keep an ODD
  number of `push`es so RSP ≡ 0 (mod 16) at the `call` (SysV AMD64 ABI). Currently
  9 pushes. Adding/removing a push breaks alignment.
- **Prologue self-check.** The hook only patches inside the `Breach` process and
  only after verifying the expected 12-byte prologue; both guards must remain.
- The `.text` page is left RWX for the process lifetime because the restore path
  may rewrite the prologue — this is intentional, not a leak to "harden away".

## Dependencies
- **Used by**: the Lua bootstrap loaders (see `scripts/mod_loader/bootstrap/`) which
  call the injected `package.loadlib`; libitbsdl, whose `itbsdl_dispatch_*` this
  forwards to (see `linux/libitbsdl/CLAUDE.md`).

## Key Files
- `itbboot.c` — loadlib injection, `lua_getfield` hook, SDL/GL forwarding shims
- `lua_exports.S` / `gen_lua_exports.sh` — absolute `lua_*` re-exports
- `lua_addrs.h` / `gen_lua_addrs.sh` — fixed game addresses used by the hook
- `build.sh` — compiles `libitbboot.so`

## Gotchas
- Diagnostics go to `~/.local/share/IntoTheBreach/itbboot.log` (via the real
  `fopen`, to avoid reentering the interposer), falling back to stderr.
- Interposers deliberately live here, not in libitbsdl: interposing from the big
  dual-loaded libitbsdl never fired during real rendering; the thin first-loaded
  preload does. Do not move them back.
