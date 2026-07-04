
local oldGetStartingSquad = getStartingSquad
function getStartingSquad(choice)
	if choice == modApi.constants.SQUAD_CHOICE_START then
		loadPilotsOrder()
		loadSquadSelection()
	end

	if true
		and choice ~= modApi.constants.SQUAD_CHOICE_RANDOM
		and choice ~= modApi.constants.SQUAD_CHOICE_CUSTOM
		and choice >= modApi.constants.SQUAD_CHOICE_START
		and choice <= modApi.constants.SQUAD_CHOICE_END
	then
		local index = modApi:squadChoice2Index(choice)
		local redirectedIndex = modApi.squadIndices[index]
		local squad = modApi:getSquadForChoice(choice)

		-- Guard against an out-of-sync squad list (e.g. mods disabled during
		-- memedit calibration shrink the squad set while the choice range still
		-- spans the old count): indexing nil here would throw an uncaught error
		-- and crash the hangar. Fall back to the game's default instead.
		if redirectedIndex == nil or modApi.squadKeys[index] == nil
			or squad == nil or squad.name == nil or squad.mechs == nil then
			return oldGetStartingSquad(choice)
		end

		modApi:setText(
			"TipTitle_"..modApi.squadKeys[index],
			modApi.squad_text[2 * (redirectedIndex - 1) + 1]
		)
		modApi:setText(
			"TipText_"..modApi.squadKeys[index],
			modApi.squad_text[2 * (redirectedIndex - 1) + 2]
		)

		-- Return the squad in a flat list as the game expects
		return { squad.name, squad.mechs[1], squad.mechs[2], squad.mechs[3] }
	else
		return oldGetStartingSquad(choice)
	end
end
