#pragma once

#include <windows.h>

#include <optional>

std::optional<bool> ReadWindowsAppsDarkMode();
bool HasWindowsThemeRestorePoint();
LSTATUS SetWindowsDarkMode(bool enabled);
