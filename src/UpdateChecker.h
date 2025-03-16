#pragma once

//I'm trying to make this as easy for anyone to adapt to their own module
//So if you want to use this, feel free
#define MODULE_VERSION "1.3.0" //The version of the current build
#define MODULE_VERSION_URL "https://raw.githubusercontent.com/FredyH/GWSockets/master/version.txt" //A URL to a txt file containing only the version number of the latest version
#define MODULE_NAME "GWSockets" //The name of this program
#define MODULE_RELEASE_URL "https://github.com/FredyH/GWSockets/releases" //A URL to the latest releases

#include "GarrysMod/Lua/Interface.h"
#include "GarrysMod/Lua/Types.h"
#include <string>
#include <vector>
#include <algorithm>
using namespace GarrysMod::Lua;

namespace UpdateChecker 
{
	namespace Internal 
	{
		static std::vector<int> splitVersions(std::string versionString)
		{
			size_t start = 0;
			std::vector<int> values;
			for (size_t i = 0; i < versionString.size(); i++)
			{
				if (versionString[i] == '.' || i + 1 == versionString.size())
				{
					std::string subStr;
					if (i + 1 == versionString.size())
					{
						subStr = versionString.substr(start);
					}
					else
					{
						subStr = versionString.substr(start, i - start);
					}
					values.push_back(strtol(subStr.c_str(), NULL, 10));
					start = i + 1;
				}
				else if (i + 1 == versionString.size())
				{
					auto subStr = versionString.substr(start);
					values.push_back(strtol(subStr.c_str(), NULL, 10));
					break;
				}
			}
			return values;
		}

		static int compareVersions(std::string left, std::string right)
		{
			auto leftVersions = splitVersions(left);
			auto rightVersions = splitVersions(right);
			for (size_t i = 0; i < std::max(leftVersions.size(), rightVersions.size()); i++)
			{
				auto lValue = i < leftVersions.size() ? leftVersions[i] : 0;
				auto rValue = i < rightVersions.size() ? rightVersions[i] : 0;
				if (lValue != rValue)
				{
					return lValue < rValue ? -1 : 1;
				}
			}
			return 0;
		}

		static void runInTimer(GarrysMod::Lua::ILuaBase* LUA, double delay, GarrysMod::Lua::CFunc func)
		{
			LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
			LUA->GetField(-1, "timer");
			//In case someone removes the timer library
			if (LUA->IsType(-1, GarrysMod::Lua::Type::NIL))
			{
				LUA->Pop(2);
				return;
			}
			LUA->GetField(-1, "Simple");
			if (LUA->IsType(-1, GarrysMod::Lua::Type::NIL))
			{
				LUA->Pop(3);
				return;
			}
			LUA->PushNumber(delay);
			LUA->PushCFunction(func);
			LUA->Call(2, 0);
			LUA->Pop(2);
		}

		static void printMessage(GarrysMod::Lua::ILuaBase* LUA, const char* str, int r, int g, int b)
		{
			LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
			LUA->GetField(-1, "Color");
			LUA->PushNumber(r);
			LUA->PushNumber(g);
			LUA->PushNumber(b);
			LUA->Call(3, 1);
			int ref = LUA->ReferenceCreate();
			LUA->GetField(-1, "MsgC");
			LUA->ReferencePush(ref);
			LUA->PushString(str);
			LUA->Call(2, 0);
			LUA->Pop();
			LUA->ReferenceFree(ref);
		}

		LUA_FUNCTION(printOutdatatedVersion)
		{
			auto message = std::string("Your server is using an outdated version of ") + MODULE_NAME + "\n";
			auto urlMessage = std::string(MODULE_RELEASE_URL) + "\n";
			printMessage(LUA, message.c_str(), 255, 0, 0);
			printMessage(LUA, "Download the latest version here:\n", 255, 0, 0);
			printMessage(LUA, urlMessage.c_str(), 86, 156, 214);
			runInTimer(LUA, 300, printOutdatatedVersion);
			return 0;
		}

		LUA_FUNCTION(fetchFailed)
		{
			auto message = std::string("Failed to retrieve latest version of ") + MODULE_NAME + "\n";
			printMessage(LUA, message.c_str(), 255, 0, 0);
			return 0;
		}

		LUA_FUNCTION(fetchSuccessful)
		{
			std::string version = LUA->GetString(1);
			int httpCode = (int) LUA->GetNumber(4);
			if (httpCode != 200)
			{
				LUA->PushCFunction(fetchFailed);
				LUA->Call(0, 0);
			}
			else if (compareVersions(MODULE_VERSION, version) < 0)
			{
				LUA->PushCFunction(printOutdatatedVersion);
				LUA->Call(0, 0);
			}
			else
			{
				auto message = std::string("Your server is using the latest version of ") + MODULE_NAME + "\n";
				printMessage(LUA, message.c_str(), 0, 255, 0);
			}
			return 0;
		}

		LUA_FUNCTION(checkVersion)
		{
			LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
			LUA->GetField(-1, "http");
			LUA->GetField(-1, "Fetch");
			LUA->PushString(MODULE_VERSION_URL);
			LUA->PushCFunction(fetchSuccessful);
			LUA->PushCFunction(fetchFailed);
			LUA->Call(3, 0);
			LUA->Pop(2);
			return 0;
		}
	}

	LUA_FUNCTION(doVersionCheck) 
	{
		//The good thing here is that the timer library handles errors for us
		//So we only want errors not to happen before we pass our functions to the timer app
		Internal::runInTimer(LUA, 5, Internal::checkVersion);
		return 0;
	}
}
