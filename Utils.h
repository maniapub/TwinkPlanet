#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <Windows.h>

#ifndef I_UTILS
#define I_UTILS

template <typename T>
std::string ToHex(T Value)
{
    static_assert(std::is_integral<T>::value, "T must be an integral type");

    std::stringstream StringStream;
    StringStream << std::hex << std::uppercase << Value;
    return "0x" + StringStream.str();
}

// Reads an RCDATA resource baked directly into this DLL (see Twinkie.rc/Resource.h) - fonts and
// media get embedded this way instead of requiring them to be manually placed in
// Documents\TwinkPlanet\, so a fresh install just works. GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
// resolves to whichever module this inline function actually executes from (our own DLL), not the
// host .exe, regardless of which .cpp it gets instantiated in.
inline bool LoadEmbeddedResource(int ResourceId, const unsigned char** OutData, size_t* OutSize)
{
    HMODULE Self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        (LPCSTR)&LoadEmbeddedResource, &Self))
        return false;

    HRSRC ResInfo = FindResource(Self, MAKEINTRESOURCE(ResourceId), RT_RCDATA);
    if (!ResInfo) return false;

    HGLOBAL ResHandle = LoadResource(Self, ResInfo);
    if (!ResHandle) return false;

    *OutData = (const unsigned char*)LockResource(ResHandle);
    *OutSize = (size_t)SizeofResource(Self, ResInfo);
    return (*OutData != nullptr && *OutSize > 0);
}

#endif