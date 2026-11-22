#include "WindowsTheme.h"
#include "ExplorerPreferences.h"

#include <array>

namespace {

constexpr wchar_t ColorsKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t RestoreValue[] = L"WindowsThemeRestorePoint";
constexpr std::array ModeNames{L"SystemUsesLightTheme", L"AppsUseLightTheme"};
constexpr DWORD Dark = 0;
constexpr DWORD Light = 1;
constexpr DWORD Missing = 2;
constexpr DWORD Unchanged = 3;
using Modes = std::array<DWORD, ModeNames.size()>;

LSTATUS ReadMode(const wchar_t* name, DWORD& value)
{
    DWORD size = sizeof(value);
    const auto result = RegGetValueW(HKEY_CURRENT_USER, ColorsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (result == ERROR_FILE_NOT_FOUND) {
        value = Missing;
        return ERROR_SUCCESS;
    }
    return result == ERROR_SUCCESS && value > Light ? ERROR_INVALID_DATA : result;
}

LSTATUS WriteMode(const wchar_t* name, DWORD value)
{
    if (value == Missing) {
        const auto result = RegDeleteKeyValueW(HKEY_CURRENT_USER, ColorsKey, name);
        return result == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : result;
    }
    return RegSetKeyValueW(HKEY_CURRENT_USER, ColorsKey, name, REG_DWORD, &value, sizeof(value));
}

LSTATUS ReadRestorePoint(std::optional<Modes>& point)
{
    Modes saved{};
    DWORD size = sizeof(saved);
    const auto result = RegGetValueW(HKEY_CURRENT_USER, AppearanceKey, RestoreValue,
        RRF_RT_REG_BINARY, nullptr, &saved, &size);
    if (result == ERROR_FILE_NOT_FOUND) {
        point.reset();
        return ERROR_SUCCESS;
    }
    if (result != ERROR_SUCCESS) return result;
    if (size != sizeof(saved)) return ERROR_INVALID_DATA;
    for (const DWORD mode : saved) {
        if (mode != Light && mode != Missing && mode != Unchanged) return ERROR_INVALID_DATA;
    }
    point = saved;
    return ERROR_SUCCESS;
}

LSTATUS SaveRestorePoint(const std::optional<Modes>& point)
{
    if (point) {
        return RegSetKeyValueW(HKEY_CURRENT_USER, AppearanceKey, RestoreValue,
            REG_BINARY, &*point, sizeof(Modes));
    }
    const auto result = RegDeleteKeyValueW(HKEY_CURRENT_USER, AppearanceKey, RestoreValue);
    return result == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : result;
}

LSTATUS RestoreMode(const wchar_t* name, DWORD saved, bool& changed)
{
    if (saved == Unchanged) return ERROR_SUCCESS;
    DWORD actual = Missing;
    const auto read = ReadMode(name, actual);
    if (read == ERROR_INVALID_DATA || read == ERROR_UNSUPPORTED_TYPE) return ERROR_SUCCESS;
    if (read != ERROR_SUCCESS || actual != Dark) return read;
    const auto result = WriteMode(name, saved);
    if (result == ERROR_SUCCESS) changed = true;
    return result;
}

LSTATUS ChangeModes(bool enabled, bool& changed)
{
    std::optional<Modes> oldPoint;
    if (const auto read = ReadRestorePoint(oldPoint); read != ERROR_SUCCESS) return read;
    if (!enabled) {
        if (!oldPoint) return ERROR_SUCCESS;
        for (size_t index = 0; index < ModeNames.size(); ++index) {
            const auto result = RestoreMode(ModeNames[index], (*oldPoint)[index], changed);
            if (result != ERROR_SUCCESS) return result;
        }
        return SaveRestorePoint(std::nullopt);
    }

    Modes before{};
    auto saved = oldPoint.value_or(Modes{Unchanged, Unchanged});
    for (size_t index = 0; index < ModeNames.size(); ++index) {
        const auto result = ReadMode(ModeNames[index], before[index]);
        if (result != ERROR_SUCCESS) return result;
        if (before[index] != Dark) saved[index] = before[index];
    }
    // Record both previous values before touching either Windows setting.
    if (const auto result = SaveRestorePoint(saved); result != ERROR_SUCCESS) return result;
    for (size_t index = 0; index < ModeNames.size(); ++index) {
        if (before[index] == Dark) continue;
        DWORD current = Missing;
        auto result = ReadMode(ModeNames[index], current);
        if (result == ERROR_SUCCESS) {
            result = current == before[index] ? WriteMode(ModeNames[index], Dark) : ERROR_RETRY;
        }
        if (result != ERROR_SUCCESS) {
            bool rolledBack = true;
            for (size_t applied = 0; applied < index; ++applied) {
                if (before[applied] != Dark && RestoreMode(ModeNames[applied], before[applied], changed) != ERROR_SUCCESS) {
                    rolledBack = false;
                }
            }
            if (rolledBack) SaveRestorePoint(oldPoint);
            return result;
        }
        changed = true;
    }
    return ERROR_SUCCESS;
}

} // namespace

std::optional<bool> ReadWindowsAppsDarkMode()
{
    DWORD mode = Missing;
    if (ReadMode(ModeNames[1], mode) != ERROR_SUCCESS) return std::nullopt;
    return mode == Dark;
}

bool HasWindowsThemeRestorePoint()
{
    std::optional<Modes> point;
    return ReadRestorePoint(point) == ERROR_SUCCESS && point.has_value();
}

LSTATUS SetWindowsDarkMode(bool enabled)
{
    const HANDLE lock = CreateMutexW(nullptr, FALSE, L"Local\\LYwintabs.WindowsThemeChange");
    if (!lock) return GetLastError();
    const DWORD wait = WaitForSingleObject(lock, 1500);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        const auto error = wait == WAIT_FAILED ? GetLastError() : ERROR_BUSY;
        CloseHandle(lock);
        return error;
    }
    bool changed = false;
    const auto result = ChangeModes(enabled, changed);
    ReleaseMutex(lock);
    CloseHandle(lock);
    if (changed) {
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &ignored);
    }
    return result;
}
