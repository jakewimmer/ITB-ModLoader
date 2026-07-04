#!/usr/bin/env bash
# Removes the ITB mod loader from a native Linux Into the Breach directory.
#
# Mirrors uninstall_modloader.bat minus the Windows proxy-DLL rename dance (Linux
# ships no -original files). Restores the vanilla resource.dat and modloader.lua
# from the backups install.sh created, and removes the native artifacts.
set -euo pipefail

if [[ $# -ne 1 ]]; then
	echo "Usage: $0 <path-to-Into the Breach directory>" >&2
	exit 1
fi
game_dir="$1"

if [[ ! -f "$game_dir/Breach" ]]; then
	echo "Error: '$game_dir' does not look like an Into the Breach install (no Breach binary)." >&2
	exit 1
fi

# Remove native artifacts, logs, and the shipped readme.
for f in libitbboot.so itb_io.so ftldat.so libitbsdl.so memedit.so \
	modloader.log MODLOADER_README.txt LAUNCHER_README.md; do
	rm -f "$game_dir/$f"
done

# Remove the installed loader tree.
rm -rf "$game_dir/scripts/mod_loader"
rm -rf "$game_dir/resources/mods"

# Restore the vanilla archive and entry point from the install-time backups.
if [[ -f "$game_dir/resources/resource.dat.bak" ]]; then
	rm -f "$game_dir/resources/resource.dat"
	mv "$game_dir/resources/resource.dat.bak" "$game_dir/resources/resource.dat"
fi
if [[ -f "$game_dir/scripts/modloader.lua.bak" ]]; then
	mv -f "$game_dir/scripts/modloader.lua.bak" "$game_dir/scripts/modloader.lua"
fi

echo "Uninstalled. Remember to clear the LD_PRELOAD Steam launch option."
