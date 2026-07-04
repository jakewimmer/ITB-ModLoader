#!/usr/bin/env bash
# Shared release-assembly for the Linux bundle (mirrors _release.bat).
#
# Ships plain .so's plus the loader trees, install/uninstall scripts, and READMEs.
# Unlike Windows there is no proxy-DLL rename dance: Linux injects via LD_PRELOAD of
# libitbboot.so (which also provides package.loadlib on the native game's Lua).
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$repo_dir/release-linux"

# Copy a directory tree into the bundle, excluding VCS/editor/tooling metadata and
# *.bak backups (shipping scripts/modloader.lua.bak would let install.sh overwrite
# the game's freshly-made backup). Extra per-tree exclude patterns may be passed as
# additional arguments. tar (not cp -r) also skips the non-regular files some
# environments inject into the tree.
copy_tree() {
	local src="$1" dst="$2"
	shift 2
	local excludes=(
		--exclude='.git' --exclude='.gitignore' --exclude='.gitmodules'
		--exclude='.claude' --exclude='.mcp.json'
		--exclude='.idea' --exclude='.vscode'
		--exclude='*.bak'
	)
	local pat
	for pat in "$@"; do
		excludes+=(--exclude="$pat")
	done
	mkdir -p "$dst"
	tar -C "$src" "${excludes[@]}" -cf - . | tar -C "$dst" -xf -
}

rm -rf "$out"
mkdir -p "$out"

# Native artifacts (staged at repo root by Phases 1-4). libitbboot.so is the
# LD_PRELOAD entry point and MUST ship; the others load via package.loadlib.
for so in libitbboot.so itb_io.so ftldat.so libitbsdl.so memedit.so; do
	if [[ ! -f "$repo_dir/$so" ]]; then
		echo "Error: missing native artifact $so (build the phases first)." >&2
		exit 1
	fi
	cp "$repo_dir/$so" "$out/$so"
done

# Loader trees and docs. linux_smoke is an in-repo integration-test mod, not for
# end users, so it is excluded from the shipped mods tree.
copy_tree "$repo_dir/scripts" "$out/scripts"
copy_tree "$repo_dir/mods" "$out/mods" 'linux_smoke'
copy_tree "$repo_dir/resources" "$out/resources"
cp "$repo_dir/README.md" "$out/MODLOADER_README.txt"
cp "$repo_dir/install.sh" "$out/install.sh"
cp "$repo_dir/uninstall.sh" "$out/uninstall.sh"
cp "$repo_dir/LAUNCHER_README.md" "$out/LAUNCHER_README.md"

echo "Assembled Linux bundle at $out"
