return {
	id = "linux_smoke",
	name = "Linux Smoke Test",
	version = "1.0.0",
	modApiVersion = "2.8.4",
	init = function(self)
		modApi.events.onModsLoaded:subscribe(function()
			LOG("LINUX_SMOKE_MOD_RAN")
		end)
	end,
	load = function(self) end,
}
