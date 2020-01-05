function os.winSdkVersion()
	local reg_arch = iif( os.is64bit(), "\\Wow6432Node\\", "\\" )
	local sdk_version = os.getWindowsRegistry( "HKLM:SOFTWARE" .. reg_arch .."Microsoft\\Microsoft SDKs\\Windows\\v10.0\\ProductVersion" )
	if sdk_version ~= nil then return sdk_version end
end

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

		buildoptions { "-std=c++11 -fPIC" }
		linkoptions { " -fPIC" }

	end
	
	configurations { "Release" }
	platforms { "x86", "x86_64" }
	
	defines { "NDEBUG" }
	optimize "On"
	floatingpoint "Fast"

	if os.target() == "windows" then
		defines{ "WIN32" }
	elseif os.target() == "linux" then
		defines{ "LINUX" }
	end

	local platform
	if os.target() == "windows" then
		platform = "win"
		links { "libcrypto", "libssl", "boost_system", "crypt32" }
	elseif os.target() == "macosx" then
		platform = "osx"
		links { "ssl", "boost_system" }
	elseif os.target() == "linux" then
		platform = "linux"
		links { "ssl", "crypto", "boost_system", "pthread", "dl" }
	else
		error "Unsupported platform."
	end
	
	

	filter "platforms:x86"
		architecture "x86"
		libdirs { "lib/" .. os.target() }
		if os.target() == "windows" then
			targetname( "gmsv_gwsockets_" .. platform .. "32")
		else
			targetname( "gmsv_gwsockets_" .. platform)
		end
	filter "platforms:x86_64"
		architecture "x86_64"
		libdirs { "lib64/" .. os.target() }
		targetname( "gmsv_gwsockets_" .. platform .. "64")
	filter {"system:windows", "action:vs*"}
		systemversion((os.winSdkVersion() or "10.0.16299") .. ".0")
		toolset "v141"

	project "GWSockets"
		defines{ "GMMODULE", "BOOST_ALL_NO_LIB" }
		files{ "src/**.*" }
		kind "SharedLib"
		targetprefix ("")
		targetextension (".dll")
		targetdir("out/" .. os.target())
