local scripts = {
	"security",
	"assert",
	"classes",
	"try_catch",
	"platform",
	"itb_io",
	"io",
	"sdl",
	"utils",
	"event",
	"modApi",
	"constants",
	"binarySearch",
	"class_iteration",
}

local rootpath = GetParentPath(...)
for i, filepath in ipairs(scripts) do
	require(rootpath..filepath)
end
