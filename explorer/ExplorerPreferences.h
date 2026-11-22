#pragma once

#include <windows.h>

#include <bit>

inline constexpr wchar_t AppearanceKey[] = L"Software\\LYwintabs\\Explorer";
inline constexpr wchar_t AppearanceValue[] = L"Appearance";

enum AppearanceOption : DWORD {
    CompactTabs = 1,
    CompactFavorites = 2,
    ShowFolderTitle = 4,
    ShowQuickAccessToolbar = 8,
    HideBookmarks = 16,
};

inline DWORD ReadAppearance()
{
    constexpr DWORD legacyPluginTheme = 32 | 64;
    DWORD appearance = 0;
    DWORD bytes = sizeof(appearance);
    return RegGetValueW(HKEY_CURRENT_USER, AppearanceKey, AppearanceValue, RRF_RT_REG_DWORD,
        nullptr, &appearance, &bytes) == ERROR_SUCCESS ? appearance & ~legacyPluginTheme : 0;
}

inline bool UsesPluginTabs()
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    using VersionQuery = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto query = ntdll ? std::bit_cast<VersionQuery>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW version{sizeof(version)};
    return query && query(&version) == 0 && version.dwMajorVersion == 10 && version.dwBuildNumber < 22000;
}
