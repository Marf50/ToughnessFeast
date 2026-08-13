// Minimal Win32 stubs for CLion / Linux indexing.
// Real builds on MSVC use the Windows SDK <Windows.h> instead.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <cstddef>
#include <cstring>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

using BOOL = int;
using BYTE = unsigned char;
using WORD = unsigned short;
using DWORD = unsigned long;
using UINT = unsigned int;
using LONG = long;
using ULONG = unsigned long;
using HANDLE = void*;
using HMODULE = void*;
using HINSTANCE = void*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPSTR = char*;
using LPCSTR = const char*;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;

#ifndef WINAPI
#define WINAPI
#endif

#ifndef APIENTRY
#define APIENTRY WINAPI
#endif

#ifndef DLL_PROCESS_ATTACH
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3
#endif

// GetModuleHandleEx
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif

inline DWORD GetModuleFileNameA(HMODULE, LPSTR buf, DWORD size)
{
    if (buf && size) buf[0] = '\0';
    return 0;
}

inline BOOL GetModuleHandleExA(DWORD, LPCSTR, HMODULE* out)
{
    if (out) *out = nullptr;
    return FALSE;
}

inline HMODULE GetModuleHandleA(LPCSTR) { return nullptr; }
inline DWORD GetTickCount() { return 0; }
