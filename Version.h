#pragma once
#include "LuaEngine/include/lua.hpp"

#define TwinkieVersion "v1.0.0"

class Versioning
{
public:
	const char* const TwinkieVer = TwinkieVersion;
	const char* const LuaVer = LUA_VERSION_MAJOR "." LUA_VERSION_MINOR "." LUA_VERSION_RELEASE;

	Versioning() {}
};