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

		buildoptions{ "-m32 -std=c++11 -fPIC" }
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
		defines{ "GMMODULE", "BOOST_ALL_NO_LIB" }
		files{ "src/**.*" }
		kind "SharedLib"
		libdirs { "libs/" .. os.target() }
		local platform
		if os.target() == "windows" then
			platform = "win32"
			links { "libcrypto", "libssl", "boost_system", "crypt32" }
		elseif os.target() == "macosx" then
			platform = "osx"
			links { "ssl", "boost_system" }
		elseif os.target() == "linux" then
			platform = "linux"
			links { "ssl", "boost_system", "pthread", "dl", "crypto" }
		else
			error "Unsupported platform."
		end
		targetname( "gmsv_gwsockets_" .. platform)
		targetprefix ("")
		targetextension (".dll")
		targetdir("out/" .. os.target())