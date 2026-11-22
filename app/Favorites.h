#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

bool IsBookmarkPath(std::wstring_view path) noexcept;

struct Favorite {
    std::wstring path;
    std::wstring name;
    std::wstring id;
    std::wstring parentId;

    bool IsFolder() const noexcept { return path.empty(); }
};

enum class FavoriteChange { Add, Rename, Remove, Update, Move };

struct BookmarkPosition {
    std::wstring_view anchorId;
    bool after = false;
};

class Favorites {
public:
    HRESULT Initialize();
    HRESULT Load(std::vector<Favorite>& items) const;
    HRESULT Update(FavoriteChange change, const Favorite& item, std::vector<Favorite>& items,
        BookmarkPosition position = {}) const;

private:
    HRESULT Save(const std::vector<Favorite>& items) const;

    std::wstring directory_;
    std::wstring file_;
};
