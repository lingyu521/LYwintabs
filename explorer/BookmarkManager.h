#pragma once

#include <windows.h>

#include <string>

class Favorites;
struct Favorite;

enum class BookmarkDrop { Before, After, Into };

inline BookmarkDrop BookmarkDropZone(bool folder, int offset, int extent) noexcept
{
    if (folder && offset >= extent / 4 && offset < extent - extent / 4) return BookmarkDrop::Into;
    return offset < extent / 2 ? BookmarkDrop::Before : BookmarkDrop::After;
}

bool ShowBookmarkManager(HWND owner, HINSTANCE instance, const Favorites& store);
bool ShowBookmarkEdit(HWND owner, HINSTANCE instance, Favorite& bookmark);
bool ShowBookmarkFolderName(HWND owner, HINSTANCE instance, std::wstring& name);
bool ShowBookmarkAdd(HWND owner, HINSTANCE instance, const Favorites& store, Favorite bookmark);
