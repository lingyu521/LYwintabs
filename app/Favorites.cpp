#include "Favorites.h"

#include <objbase.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace {

using namespace winrt::Windows::Data::Json;

constexpr DWORD LegacyMaxFileSize = 4 * 1024 * 1024;
// Version 2 stores each entry's ID and parent, so a full legacy file needs room to migrate.
constexpr DWORD MaxFileSize = 3 * LegacyMaxFileSize;
constexpr int MaxBookmarkDepth = 16;

struct CloseFile {
    void operator()(HANDLE file) const noexcept
    {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
};

using File = std::unique_ptr<void, CloseFile>;

bool IsText(const std::wstring& text)
{
    return !text.empty() && text.find(L'\0') == std::wstring::npos;
}

std::wstring LegacyId(std::wstring_view path)
{
    // Hash the original UTF-16 address so the ID stays consistent without retaining the old path after editing.
    std::uint64_t hash = 14695981039346656037ull;
    for (const wchar_t unit : path) {
        hash = (hash ^ static_cast<std::uint16_t>(unit)) * 1099511628211ull;
    }
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring id = L"legacy:";
    for (int shift = 60; shift >= 0; shift -= 4) id += digits[(hash >> shift) & 0xf];
    return id;
}

bool IsDrivePath(std::wstring_view path)
{
    return path.size() >= 3 && path[1] == L':' && path[2] == L'\\' &&
        ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'));
}

bool IsPathTail(std::wstring_view path)
{
    return path.find_first_of(L"<>:\"|?*/") == std::wstring_view::npos;
}

bool IsUncShare(std::wstring_view path)
{
    if (!IsPathTail(path)) return false;
    const auto serverEnd = path.find(L'\\');
    if (!serverEnd || serverEnd == std::wstring_view::npos) return false;
    const auto server = path.substr(0, serverEnd);
    path.remove_prefix(serverEnd + 1);
    const auto share = path.substr(0, path.find(L'\\'));
    return !share.empty() && server != L"." && server != L".." && share != L"." && share != L"..";
}

HRESULT ValidateHierarchy(const std::vector<Favorite>& items)
{
    std::unordered_map<std::wstring_view, const Favorite*> byId;
    byId.reserve(items.size());
    for (const auto& item : items) {
        if (!IsText(item.id) || !IsText(item.name) || (!item.IsFolder() && !IsText(item.path)) ||
            !byId.emplace(item.id, &item).second) return E_INVALIDARG;
    }
    for (const auto& item : items) {
        std::wstring_view seen[MaxBookmarkDepth]{item.id};
        auto parentId = std::wstring_view(item.parentId);
        for (int depth = 1; !parentId.empty(); ++depth) {
            const auto parent = byId.find(parentId);
            if (parent == byId.end() || !parent->second->IsFolder()) return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
            if (std::find(seen, seen + depth, parentId) != seen + depth) {
                return HRESULT_FROM_WIN32(ERROR_CIRCULAR_DEPENDENCY);
            }
            if (depth >= MaxBookmarkDepth) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
            seen[depth] = parentId;
            parentId = parent->second->parentId;
        }
    }
    return S_OK;
}

HRESULT AssignId(Favorite& item)
{
    GUID guid{};
    const HRESULT status = CoCreateGuid(&guid);
    if (FAILED(status)) return status;
    wchar_t value[39]{};
    if (!StringFromGUID2(guid, value, std::size(value))) return E_FAIL;
    item.id = value;
    return S_OK;
}

bool HasAddress(const std::vector<Favorite>& items, const std::wstring& path, const std::wstring& parentId,
    std::wstring_view exceptId = {})
{
    return !path.empty() && std::ranges::any_of(items, [&](const Favorite& existing) {
        return existing.id != exceptId && existing.parentId == parentId && existing.path == path;
    });
}

} // namespace

bool IsBookmarkPath(std::wstring_view path) noexcept
{
    if (std::ranges::any_of(path, [](wchar_t c) { return c < L' ' || c == 0x7f; })) return false;
    if (path.starts_with(L"\\\\?\\")) {
        path.remove_prefix(4);
        if (IsDrivePath(path)) return IsPathTail(path.substr(3));
        if (path.size() >= 4 && CompareStringOrdinal(path.data(), 4, L"UNC\\", 4, TRUE) == CSTR_EQUAL) {
            return IsUncShare(path.substr(4));
        }
        // A volume GUID path is also a filesystem path. Other extended device namespaces are unsupported.
        if (path.size() >= 45 && CompareStringOrdinal(path.data(), 6, L"Volume", 6, TRUE) == CSTR_EQUAL &&
            path[6] == L'{' && path[43] == L'}' && path[44] == L'\\') {
            wchar_t guidText[39]{};
            std::ranges::copy(path.substr(6, 38), guidText);
            GUID volume{};
            return SUCCEEDED(IIDFromString(guidText, &volume)) && IsPathTail(path.substr(45));
        }
        return false;
    }
    if (path.starts_with(L"\\\\")) return IsUncShare(path.substr(2));
    return IsDrivePath(path) && IsPathTail(path.substr(3));
}

HRESULT Favorites::Initialize()
{
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!length) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    std::wstring profile(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", profile.data(), length);
    if (!copied || copied >= length) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    profile.resize(copied);
    directory_ = profile + L"\\LYwintabs";
    file_ = directory_ + L"\\favorites.json";
    return S_OK;
}

HRESULT Favorites::Load(std::vector<Favorite>& items) const
{
    if (file_.empty()) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    File file(CreateFileW(file_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return HRESULT_FROM_WIN32(error);
        items.clear();
        return S_OK;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size)) return HRESULT_FROM_WIN32(GetLastError());
    if (size.QuadPart <= 0 || size.QuadPart > MaxFileSize) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    std::string utf8(static_cast<size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    if (!ReadFile(file.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &bytesRead, nullptr)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (bytesRead != utf8.size()) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);

    try {
        std::string_view input(utf8);
        if (input.starts_with("\xef\xbb\xbf")) input.remove_prefix(3);
        const auto root = JsonObject::Parse(winrt::to_hstring(input));
        const double version = root.GetNamedNumber(L"version");
        if (version != 1 && version != 2) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        if (version == 1 && utf8.size() > LegacyMaxFileSize) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const auto array = root.GetNamedArray(L"items");
        std::vector<Favorite> loaded;
        loaded.reserve(array.Size());
        for (const auto& value : array) {
            const auto entry = value.GetObject();
            Favorite item;
            item.path = std::wstring(entry.GetNamedString(L"path"));
            item.name = std::wstring(entry.GetNamedString(L"name"));
            if (version == 1) {
                if (!IsText(item.path)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                item.id = LegacyId(item.path);
            } else {
                item.id = std::wstring(entry.GetNamedString(L"id"));
                item.parentId = std::wstring(entry.GetNamedString(L"parentId"));
            }
            loaded.push_back(std::move(item));
        }
        if (FAILED(ValidateHierarchy(loaded))) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        items = std::move(loaded);
        return S_OK;
    } catch (const winrt::hresult_error& e) {
        return e.code();
    }
}

HRESULT Favorites::Save(const std::vector<Favorite>& items) const
{
    std::string utf8;
    try {
        JsonObject root;
        JsonArray entries;
        for (const auto& item : items) {
            JsonObject entry;
            entry.SetNamedValue(L"id", JsonValue::CreateStringValue(item.id));
            entry.SetNamedValue(L"parentId", JsonValue::CreateStringValue(item.parentId));
            entry.SetNamedValue(L"path", JsonValue::CreateStringValue(item.path));
            entry.SetNamedValue(L"name", JsonValue::CreateStringValue(item.name));
            entries.Append(entry);
        }
        root.SetNamedValue(L"version", JsonValue::CreateNumberValue(2));
        root.SetNamedValue(L"items", entries);
        utf8 = winrt::to_string(root.Stringify());
    } catch (const winrt::hresult_error& e) {
        return e.code();
    }
    if (utf8.size() > MaxFileSize) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

    const auto temporary = file_ + L".tmp";
    File file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());

    DWORD written = 0;
    HRESULT hr = S_OK;
    if (!WriteFile(file.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
    } else if (written != utf8.size()) {
        hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    } else if (!FlushFileBuffers(file.get())) {
        hr = HRESULT_FROM_WIN32(GetLastError());
    }
    file.reset();
    if (SUCCEEDED(hr) && !MoveFileExW(temporary.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
    }
    if (FAILED(hr)) DeleteFileW(temporary.c_str());
    return hr;
}

HRESULT Favorites::Update(FavoriteChange change, const Favorite& item, std::vector<Favorite>& items,
    BookmarkPosition position) const
{
    if (file_.empty()) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    if (!CreateDirectoryW(directory_.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Opening the lock file with exclusive sharing serializes updates to this particular store.
    const auto lockPath = directory_ + L"\\favorites.lock";
    File lock;
    for (int attempt = 0; attempt < 15; ++attempt) {
        lock.reset(CreateFileW(lockPath.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        if (lock.get() != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_SHARING_VIOLATION) return HRESULT_FROM_WIN32(GetLastError());
        Sleep(20);
    }
    if (lock.get() == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(ERROR_BUSY);

    std::vector<Favorite> updated;
    const HRESULT read = Load(updated);
    if (FAILED(read)) return read;
    if (change == FavoriteChange::Add) {
        if (!item.IsFolder() && !IsBookmarkPath(item.path)) return E_INVALIDARG;
        if (HasAddress(updated, item.path, item.parentId)) {
            items = std::move(updated);
            return S_FALSE;
        }
        Favorite added = item;
        const HRESULT id = AssignId(added);
        if (FAILED(id)) return id;
        updated.push_back(std::move(added));
    } else {
        if (item.id.empty() && (item.path.empty() ||
                (change != FavoriteChange::Rename && change != FavoriteChange::Remove))) return E_INVALIDARG;
        const auto found = item.id.empty() ? std::ranges::find_if(updated, [&](const Favorite& existing) {
            return existing.parentId.empty() && existing.path == item.path;
        }) : std::ranges::find(updated, item.id, &Favorite::id);
        if (found == updated.end()) {
            items = std::move(updated);
            return S_FALSE;
        }
        switch (change) {
        case FavoriteChange::Rename:
            found->name = item.name;
            break;
        case FavoriteChange::Remove:
            if (std::ranges::find(updated, found->id, &Favorite::parentId) != updated.end()) {
                return HRESULT_FROM_WIN32(ERROR_DIR_NOT_EMPTY);
            }
            updated.erase(found);
            break;
        case FavoriteChange::Update:
            if (found->IsFolder() || !IsBookmarkPath(item.path)) return E_INVALIDARG;
            if (HasAddress(updated, item.path, found->parentId, found->id)) {
                return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            }
            found->name = item.name;
            found->path = item.path;
            break;
        case FavoriteChange::Move: {
            if (HasAddress(updated, found->path, item.parentId, found->id)) {
                return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            }
            auto insertion = updated.end();
            if (!position.anchorId.empty()) {
                insertion = std::ranges::find(updated, position.anchorId, &Favorite::id);
                if (insertion == updated.end() || insertion->parentId != item.parentId) {
                    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                }
                if (position.after) ++insertion;
            }
            if (found->parentId == item.parentId && (insertion == found || insertion == std::next(found))) {
                items = std::move(updated);
                return S_FALSE;
            }
            auto offset = insertion - updated.begin();
            if (found < insertion) --offset;
            Favorite moved = std::move(*found);
            moved.parentId = item.parentId;
            updated.erase(found);
            updated.insert(updated.begin() + offset, std::move(moved));
            break;
        }
        default:
            return E_INVALIDARG;
        }
    }
    const HRESULT hierarchy = ValidateHierarchy(updated);
    if (FAILED(hierarchy)) return hierarchy;
    const HRESULT saved = Save(updated);
    if (SUCCEEDED(saved)) items = std::move(updated);
    return saved;
}
