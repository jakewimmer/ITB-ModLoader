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

-- On Linux the native libraries load via package.loadlib. The game's embedded
-- Lua ships a stub package.loadlib that always errors "dynamic libraries not
-- enabled" (built without LUA_DL); libitbboot.so, preloaded via the LD_PRELOAD
-- launch option, replaces it with a working dlopen-backed loader. Both are
-- functions, so probe the behaviour: load a path that cannot exist and check
-- whether the error is the stock "not enabled" message (preload absent) or a
-- dlopen error (preload active). Always true on Windows.
local preloadActive
function Platform.nativePreloadActive()
	if preloadActive ~= nil then
		return preloadActive
	end
	if Platform.name ~= "linux" then
		preloadActive = true
	elseif type(package.loadlib) ~= "function" then
		preloadActive = false
	else
		local _, err = package.loadlib("/nonexistent-itbboot-preload-probe.so", "x")
		preloadActive = not (type(err) == "string"
			and err:find("dynamic libraries not enabled"))
	end
	return preloadActive
end

-- Actionable one-line hint pointing at the Steam launch option, for logging when
-- the preload is missing.
function Platform.preloadHint()
	return "libitbboot.so is not LD_PRELOADed -- set the Steam launch option "
		.. "'LD_PRELOAD=\"$PWD/libitbboot.so\" %command%'. Native features are disabled."
end
