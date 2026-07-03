#!/usr/bin/env bash
# Build libitbboot.so - the LD_PRELOAD bootstrap.
#
# libitbboot does two jobs:
#   1. Injects a dlopen-based package.loadlib (hook + libc interposers, itbboot.c).
#   2. Re-exports the game's Lua 5.1 C API as absolute dynamic symbols
#      (lua_exports.S) so the native mod .so's (ftldat, itb_io, libitbsdl) resolve
#      their undefined lua_* to the game's single shared Lua runtime.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

# Regenerate lua_exports.S from the game binary when GAME is set and present;
# otherwise use the committed lua_exports.S (addresses are stable, non-PIE).
if [[ -n "${GAME:-}" && -f "$GAME/Breach" ]]; then
	./gen_lua_exports.sh "$GAME/Breach"
fi

if [[ ! -f lua_exports.S ]]; then
	echo "Error: lua_exports.S missing; run gen_lua_exports.sh <Breach> first" >&2
	exit 1
fi

# Compile the bootstrap plus the absolute Lua re-exports with default visibility
# so the lua_* symbols land in .dynsym and satisfy the mods' undefined refs.
cc -shared -fPIC -O2 -o libitbboot.so itbboot.c lua_exports.S -ldl

echo "Built libitbboot.so"
ls -lh libitbboot.so
