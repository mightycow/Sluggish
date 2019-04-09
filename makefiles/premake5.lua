--[[

Currently used premake build:
premake 5.0.0-alpha10

Only 1 toolchain is supported: Visual C++ on Windows

--]]

-- relative to the LUA script
path_root = ".."
path_src = path_root.."/code"
path_make = path_root.."/makefiles"
path_build = path_root.."/.build"
path_bin = path_root.."/.bin"
path_libs = "../libs"

-- relative to the makefile
make_path_src = "../../code"
make_path_bin = "bin"

-- environment variables
envvar_appdir  = "SLUGGISH_APP_DIR"
abs_path_app = string.format("$(%s)", envvar_appdir)

extra_warnings = 1

local function GetBinDirName()

	return "%{cfg.platform}/%{cfg.buildcfg}"

end

local function CreateExeCopyPostBuildCommand(exeNameNoExt)

	local make_path_exe = string.format("%s/%s/%s.exe", make_path_bin, GetBinDirName(), exeNameNoExt)

	return string.format("copy \"%s\" \"%s\"", make_path_exe, abs_path_app)

end

local function CreatePdbCopyPostBuildCommand(exeNameNoExt)

	local make_path_pdb = string.format("%s/%s/%s.pdb", make_path_bin, GetBinDirName(), exeNameNoExt)

	return string.format("copy \"%s\" \"%s\"", make_path_pdb, abs_path_app)

end

local function ApplyProjectSettings(exeNameNoExt)

	--
	-- General
	--
	
	filter { }
	
	language "C++"

	location ( path_make.."/".._ACTION )

	rtti "Off"
	exceptionhandling "On"
	flags { "NoPCH", "StaticRuntime", "NoManifest", "NoNativeWChar" }

	filter "configurations:debug"
		defines { "DEBUG", "_DEBUG" }
		flags { }

	filter "configurations:release"
		defines { "NDEBUG" }
		flags -- others: NoIncrementalLink NoCopyLocal NoImplicitLink NoBufferSecurityChecks
		{
			"NoMinimalRebuild",
			"OptimizeSize",
			"NoFramePointer",
			"EnableSSE2",
			"FloatFast",
			"MultiProcessorCompile",
			"NoRuntimeChecks"
		}
	
	filter {  }

	--
	-- Visual C++
	--

	-- Some build options:
	-- /GT  => Support Fiber-Safe Thread-Local Storage
	-- /GS- => Buffer Security Check disabled
	-- /GL  => Whole Program Optimization
	-- /Zi  => Debug info, but not for edit and continue
	-- /Os  => Favor size over speed
	-- /Gm  => Enable Minimal Rebuild

	filter "action:vs*"
		symbols "On"
		editandcontinue "Off"
		defines { "_CRT_SECURE_NO_WARNINGS", "WIN32", "_WIN32" }
		if extra_warnings == 1 then
			flags { "ExtraWarnings" }
		end

	filter { "action:vs*", "configurations:debug" }
		buildoptions { "/Gm" }
		linkoptions { "" }

	filter { "action:vs*", "configurations:release" }
		flags { "LinkTimeOptimization" } -- I had no success with GCC's -flto
		buildoptions { "/GL"  }
		linkoptions { "" }

	filter { }

	targetname(exeNameNoExt)

	files
	{
		path_src.."/*.cpp",
		path_src.."/*.hpp"
	}

	-- copy the binaries over to the data install
	-- it seems that "filter" doesn't work with "prebuildcommands", "postbuildcommands"
	postbuildcommands { path.translate(CreateExeCopyPostBuildCommand(exeNameNoExt), "\\") }
	postbuildcommands { path.translate(CreatePdbCopyPostBuildCommand(exeNameNoExt), "\\") }

	-- create VC++ debug settings
	filter "action:vs*"
		local abs_path_exe = string.format("%s\\%s.exe", abs_path_app, exeNameNoExt)
		debugcommand(abs_path_exe)
		-- debugargs { "" }
		debugdir(abs_path_app)

	filter { "action:vs*", "configurations:release" }
		linkoptions { "/OPT:REF", "/OPT:ICF" }

end

local function AddProjectFolder(folderName)

	filter { }

	includedirs { make_path_src.."/"..folderName }
	
	files
	{
		path_src.."/"..folderName.."/*.cpp",
		path_src.."/"..folderName.."/*.hpp",
		path_src.."/"..folderName.."/*.h"
	}

end

solution "Sluggish"

	location ( path_make.."/".._ACTION )
	platforms { "x64" }
	configurations { "debug", "release" }
	
	project "fontgen"
		kind "ConsoleApp"
		ApplyProjectSettings("fontgen")
		AddProjectFolder("generator")
		
	project "fontrender"
		kind "ConsoleApp"
		ApplyProjectSettings("fontrender")
		AddProjectFolder("renderer_sw")

	project "fontrendergl"
		kind "ConsoleApp"
		ApplyProjectSettings("fontrendergl")
		AddProjectFolder("renderer_gl")
		filter {  }
		includedirs { path_libs.."/SDL2/include", path_libs.."/GLEW/include" }
		libdirs { path_libs.."/SDL2/lib", path_libs.."/GLEW/lib" }
		links { "SDL2", "opengl32", "glew32" }
