#pragma once

#include <windows.h>

enum class ExplorerIcon { Star, Settings, LanTransfer };

bool DrawExplorerIcon(HDC dc, const RECT& bounds, int preferredSize, ExplorerIcon icon, COLORREF color);
