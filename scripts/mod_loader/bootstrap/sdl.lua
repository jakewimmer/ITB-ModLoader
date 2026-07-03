-- SDL proxy for the modloader bootstrap.
-- On Windows, this functionality likely comes from native SDL bindings or the
-- itb_io DLL. On Linux, we provide a minimal Lua wrapper that returns screen
-- dimensions. This is sufficient for phase 2 (no rendering), but will be
-- superseded by libitbsdl.so in phase 3.

sdl = {}

function sdl.screen()
	-- Return a minimal screen object with width and height methods.
	-- The actual screen dimensions are obtained at runtime. For now,
	-- return placeholder values that match common display resolutions.
	return {
		w = function() return 1280 end,
		h = function() return 720 end,
	}
end

return sdl
