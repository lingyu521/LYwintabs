#pragma once

#include <windows.h>

enum class UiLanguage : DWORD { Chinese, English };

UiLanguage ReadUiLanguage() noexcept;
bool SaveUiLanguage(UiLanguage language) noexcept;
const wchar_t* ExplorerText(const wchar_t* chinese, UiLanguage language) noexcept;
const wchar_t* ExplorerText(const wchar_t* chinese) noexcept;
void LocalizeExplorerDialog(HWND dialog, UiLanguage language) noexcept;
