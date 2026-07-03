-- Bootstrap loader for libitbsdl.so on Linux.
-- On Linux, the in-game mod-loader UI, console, and toasts render via a native
-- C++ library (libitbsdl.so) that is both LD_PRELOAD'd for SDL interposition and
-- loaded via package.loadlib for Lua registration. This script triggers the
-- registration side (luaopen_itbsdl), which shares module globals (hook lists,
-- captured lua_State) with the LD_PRELOAD interposers.

if Platform.name ~= "linux" then
  return
end

local function load_itbsdl()
  local library = Platform.nativeLibrary("itbsdl")
  try(function()
    LOG(string.format("Loading %s...", library))
    local fn, err = package.loadlib(library, "luaopen_itbsdl")
    if not fn then
      error(string.format("package.loadlib failed: %s", tostring(err)))
    end
    fn()
    LOG(string.format("Successfully loaded %s!", library))
  end)
  :catch(function(err)
    error(string.format(
      "Failed to load %s: %s",
      library, tostring(err)
    ))
  end)
end

load_itbsdl()
