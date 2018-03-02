solution "GWSockets"

	language "C++"
	location ( "solutions/" .. os.target() .. "-" .. _ACTION )
	flags { "NoPCH", "NoImportLib"}
	optimize "On"
	vectorextensions "SSE"
	symbols "On"
	floatingpoint "Fast"
	editandcontinue "Off"
	targetdir ( "out/" .. os.target() .. "/" )
	includedirs {	"include/",
					"src/"
					}

	if os.target() == "macosx" or os.target() == "linux" then

		buildoptions{ "-std=c++11 -fPIC" }
		linkoptions{ "-fPIC -static-libstdc++" }

	end
	
	configurations
	{ 
		"Release"
	}
	platforms
	{
		"x32"
	}
	
	configuration "Release"
		defines { "NDEBUG" }
		optimize "On"
		floatingpoint "Fast"

		if os.target() == "windows" then

			defines{ "WIN32" }

		elseif os.target() == "linux" then

			defines{ "LINUX" }

		end
	project "GWSockets"
		defines{ "GMMODULE" }
		files{ "src/**.*" }
		kind "SharedLib"
		libdirs { "libs/" .. os.target() }
		local platform
		if os.target() == "windows" then
			platform = "win32"
		elseif os.target() == "macosx" then
			platform = "osx"
		elseif os.target() == "linux" then
			platform = "linux"
		else
			error "Unsupported platform."
		end
		targetname( "gmsv_gwsockets_" .. platform)
		targetprefix ("")
		targetextension (".dll")
		targetdir("out/" .. os.target())
		links { "ssleay32", "libeay32", "boost_system" }
		