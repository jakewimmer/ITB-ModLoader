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

echo "Installed. To run the game with module loading, set the environment:"
echo "  LD_PRELOAD=\"./libitbboot.so\" ./Breach"
