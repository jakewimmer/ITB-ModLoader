local testsuite = Tests.Testsuite()
testsuite.name = "Platform tests"

-- Test that Platform.nativeLibrary returns the correct filename for the current platform
function testsuite.test_itb_io_library_name()
	if Platform.name == "windows" then
		Assert.Equals("itb_io.dll", Platform.nativeLibrary("itb_io"))
	else
		Assert.Equals("./itb_io.so", Platform.nativeLibrary("itb_io"))
	end
	return true
end

function testsuite.test_ftldat_library_name()
	if Platform.name == "windows" then
		Assert.Equals("ftldat.dll", Platform.nativeLibrary("ftldat"))
	else
		Assert.Equals("./ftldat.so", Platform.nativeLibrary("ftldat"))
	end
	return true
end

function testsuite.test_itbsdl_library_name()
	if Platform.name == "windows" then
		-- On Windows, itbsdl is not available via loadlib
		local ok = pcall(function() Platform.nativeLibrary("itbsdl") end)
		Assert.False(ok)
	else
		Assert.Equals("./libitbsdl.so", Platform.nativeLibrary("itbsdl"))
	end
	return true
end

function testsuite.test_memedit_library_name()
	if Platform.name == "windows" then
		Assert.Equals("memedit.dll", Platform.nativeLibrary("memedit"))
	else
		Assert.Equals("./memedit.so", Platform.nativeLibrary("memedit"))
	end
	return true
end

-- Test that unknown library names raise an error
function testsuite.test_unknown_library_raises_error()
	local ok = pcall(function() Platform.nativeLibrary("unknown_library") end)
	Assert.False(ok)
	return true
end

return testsuite
