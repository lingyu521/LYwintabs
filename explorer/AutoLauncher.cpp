#include "ExplorerExtension.h"
#include "WindowsTheme.h"
#include <sddl.h>

#include <algorithm>
#include <bit>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
using Module = std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>;
using Hook = std::unique_ptr<std::remove_pointer_t<HHOOK>, decltype(&UnhookWindowsHookEx)>;

struct OwnedMutex {
    HANDLE value;
    ~OwnedMutex() { ReleaseMutex(value); }
};

bool IsExplorerFrameWindow(HWND window)
{
    wchar_t name[32]{};
    return GetClassNameW(window, name, ARRAYSIZE(name)) && !lstrcmpW(name, L"CabinetWClass");
}

bool IsExplorerWindow(HWND window)
{
    return IsWindowVisible(window) && !IsIconic(window) && IsExplorerFrameWindow(window);
}

bool NeedsAttachment(HWND window)
{
    return !GetPropW(window, BarProperty) && !GetPropW(window, SuppressedProperty);
}

struct WindowIdentity {
    HWND window;
    DWORD thread;
    DWORD process;
    bool operator==(const WindowIdentity&) const = default;
};

bool IsSameWindow(const WindowIdentity& identity)
{
    DWORD process = 0;
    return GetWindowThreadProcessId(identity.window, &process) == identity.thread && process == identity.process;
}

std::wstring ExplorerPath()
{
    wchar_t windows[MAX_PATH]{};
    const UINT length = GetSystemWindowsDirectoryW(windows, ARRAYSIZE(windows));
    return length && length < ARRAYSIZE(windows) ? std::wstring(windows) + L"\\explorer.exe" : L"";
}

std::wstring NativeImagePath(const std::wstring& path)
{
    const HANDLE rawFile = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (rawFile == INVALID_HANDLE_VALUE) return {};
    const Handle file(rawFile, CloseHandle);
    wchar_t nativePath[1024]{};
    const DWORD length = GetFinalPathNameByHandleW(file.get(), nativePath, ARRAYSIZE(nativePath), VOLUME_NAME_NT);
    return length && length < ARRAYSIZE(nativePath) ? std::wstring(nativePath, length) : std::wstring{};
}

std::wstring PluginPath()
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    const auto separator = path.find_last_of(L'\\');
    if (separator == std::wstring::npos) return {};
    path.resize(separator + 1);
    path += L"LYwintabs.Explorer.dll";
    return path;
}

std::wstring CurrentUserSid()
{
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) return {};
    const Handle token(rawToken, CloseHandle);
    DWORD bytes = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    if (!bytes) return {};
    std::vector<BYTE> user(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, user.data(), bytes, &bytes)) return {};
    PWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &text)) return {};
    const std::unique_ptr<wchar_t, decltype(&LocalFree)> sid(text, LocalFree);
    return sid.get();
}

bool IsNativeExplorer(const WindowIdentity& identity, const std::wstring& expectedPath)
{
    const Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, identity.process), CloseHandle);
    wchar_t path[1024]{};
    DWORD length = ARRAYSIZE(path);
    return process && QueryFullProcessImageNameW(process.get(), PROCESS_NAME_NATIVE, path, &length) &&
        CompareStringOrdinal(path, static_cast<int>(length), expectedPath.c_str(), -1, TRUE) == CSTR_EQUAL;
}

struct ExplorerWindows {
    std::vector<WindowIdentity> windows;

    static BOOL CALLBACK Collect(HWND window, LPARAM context) noexcept
    {
        if (!IsExplorerWindow(window) || !NeedsAttachment(window)) return TRUE;
        DWORD process = 0;
        const DWORD thread = GetWindowThreadProcessId(window, &process);
        if (!thread || !process) return TRUE;
        try {
            reinterpret_cast<ExplorerWindows*>(context)->windows.push_back({window, thread, process});
        } catch (...) {
            return FALSE;
        }
        return TRUE;
    }
};

struct PendingAttachment {
    WindowIdentity target;
    ULONGLONG until;
    Hook hook;
};

struct Retry {
    WindowIdentity target;
    ULONGLONG nextAttempt;
    unsigned delay;
};

int AttachExplorerWindows(HANDLE stop, HMODULE library, HOOKPROC callback, UINT message,
    const std::wstring& explorerPath)
{
    std::vector<PendingAttachment> pending;
    std::vector<Retry> retries;
    while (true) {
        const auto now = GetTickCount64();
        std::erase_if(pending, [now](const PendingAttachment& item) {
            return now >= item.until || !IsSameWindow(item.target) || !NeedsAttachment(item.target.window);
        });

        ExplorerWindows explorer;
        EnumWindows(ExplorerWindows::Collect, reinterpret_cast<LPARAM>(&explorer));
        std::erase_if(retries, [&explorer](const Retry& retry) {
            return std::ranges::find(explorer.windows, retry.target) == explorer.windows.end();
        });
        for (const auto& target : explorer.windows) {
            if (std::ranges::find(pending, target.thread, [](const PendingAttachment& item) {
                    return item.target.thread;
                }) != pending.end()) continue;

            auto retry = std::ranges::find(retries, target, &Retry::target);
            if (retry == retries.end()) {
                retries.push_back({target, 0, 750});
                retry = std::prev(retries.end());
            }
            if (now < retry->nextAttempt) continue;
            retry->nextAttempt = now + retry->delay;
            retry->delay = std::min(retry->delay * 2, 15000u);

            if (!IsNativeExplorer(target, explorerPath) || !IsSameWindow(target)) continue;
            Hook hook(SetWindowsHookExW(WH_GETMESSAGE, callback, library, target.thread), UnhookWindowsHookEx);
            if (hook && PostMessageW(target.window, message, 0, 0)) {
                pending.push_back({target, now + 400, std::move(hook)});
            }
        }

        const DWORD wait = WaitForSingleObject(stop, pending.empty() ? 350 : 80);
        if (wait != WAIT_TIMEOUT) return wait == WAIT_OBJECT_0 ? ERROR_SUCCESS : GetLastError();
    }
}

int StartProcess(const std::wstring& path, const wchar_t* argument = nullptr)
{
    std::wstring command;
    if (argument) command = L"\"" + path + L"\" " + argument;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.c_str(), command.empty() ? nullptr : command.data(),
            nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        return GetLastError();
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ERROR_SUCCESS;
}

void StartBackgroundTransfer(const std::wstring& pluginPath)
{
    const auto separator = pluginPath.find_last_of(L'\\');
    if (separator != std::wstring::npos) {
        const std::wstring helper = pluginPath.substr(0, separator + 1) + TransferRelativePath;
        StartProcess(helper, L"--background");
    }
}

int StopLauncher(const std::wstring& activeName, const std::wstring& stopName)
{
    const Handle active(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, activeName.c_str()), CloseHandle);
    if (!active) return GetLastError() == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : GetLastError();
    const auto until = GetTickCount64() + 5000;
    while (GetTickCount64() < until) {
        const Handle stop(OpenEventW(EVENT_MODIFY_STATE, FALSE, stopName.c_str()), CloseHandle);
        if (stop && !SetEvent(stop.get())) return GetLastError();
        const DWORD wait = WaitForSingleObject(active.get(), 100);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
            ReleaseMutex(active.get());
            return ERROR_SUCCESS;
        }
        if (wait == WAIT_FAILED) return GetLastError();
    }
    return ERROR_TIMEOUT;
}

bool DetachBar(HWND root, UINT message)
{
    const auto bar = static_cast<HWND>(GetPropW(root, BarProperty));
    if (!bar) return true;
    const DWORD thread = GetWindowThreadProcessId(root, nullptr);
    if (!thread || GetWindowThreadProcessId(bar, nullptr) != thread || GetParent(bar) != root) return false;
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(bar, message, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500, &ignored);
    return GetPropW(root, BarProperty) == nullptr;
}

struct DetachRequest {
    const std::wstring& path;
    UINT message;
    bool complete = true;

    static BOOL CALLBACK Visit(HWND root, LPARAM context) noexcept
    {
        auto& request = *reinterpret_cast<DetachRequest*>(context);
        if (!GetPropW(root, BarProperty) || !IsExplorerFrameWindow(root)) return TRUE;
        WindowIdentity owner{root, 0, 0};
        owner.thread = GetWindowThreadProcessId(root, &owner.process);
        if (owner.thread && IsNativeExplorer(owner, request.path) && IsSameWindow(owner) &&
            !DetachBar(root, request.message)) request.complete = false;
        return TRUE;
    }
};

int DetachExplorerWindows(const std::wstring& path)
{
    const UINT message = RegisterWindowMessageW(DetachMessageName);
    if (!message) return GetLastError();
    const auto nativePath = NativeImagePath(path);
    if (nativePath.empty()) return ERROR_PATH_NOT_FOUND;
    DetachRequest request{nativePath, message};
    if (!EnumWindows(DetachRequest::Visit, reinterpret_cast<LPARAM>(&request))) return GetLastError();
    return request.complete ? ERROR_SUCCESS : ERROR_BUSY;
}

int RunLauncher(std::wstring_view option)
{
    if (option == L"--settings") {
        const auto path = PluginPath();
        if (path.empty()) return ERROR_PATH_NOT_FOUND;
        const Module library(LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32), FreeLibrary);
        if (!library) return GetLastError();
        using SettingsDialog = HRESULT(WINAPI*)(HMODULE);
        const auto show = std::bit_cast<SettingsDialog>(GetProcAddress(library.get(), "ShowLYwintabsSettings"));
        return show ? show(library.get()) : ERROR_PROC_NOT_FOUND;
    }
    if (!option.empty() && option != L"--open" && option != L"--stop" && option != L"--detach") return ERROR_INVALID_PARAMETER;
    const std::wstring sid = CurrentUserSid();
    if (sid.empty()) return ERROR_ACCESS_DENIED;
    const std::wstring namespaceName = L"Local\\LYwintabs.Launcher." + sid;
    const std::wstring activeName = namespaceName + L".Active";
    const std::wstring stopName = namespaceName + L".Stop";
    if (option == L"--stop") return StopLauncher(activeName, stopName);

    const auto explorerPath = ExplorerPath();
    if (explorerPath.empty()) return ERROR_PATH_NOT_FOUND;
    if (option == L"--detach") {
        const Handle running(OpenEventW(SYNCHRONIZE, FALSE, stopName.c_str()), CloseHandle);
        const int stopped = StopLauncher(activeName, stopName);
        if (stopped != ERROR_SUCCESS) return stopped;
        int result = DetachExplorerWindows(explorerPath);
        if (result == ERROR_SUCCESS) result = SetWindowsDarkMode(false);
        if (result != ERROR_SUCCESS && running) {
            std::wstring launcher(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, launcher.data(), static_cast<DWORD>(launcher.size()));
            if (length && length < launcher.size()) {
                launcher.resize(length);
                StartProcess(launcher);
            }
        }
        return result;
    }
    SetLastError(ERROR_SUCCESS);
    const Handle active(CreateMutexW(nullptr, TRUE, activeName.c_str()), CloseHandle);
    const DWORD mutexError = GetLastError();
    if (!active) return mutexError;
    if (mutexError == ERROR_ALREADY_EXISTS) {
        if (option != L"--open") return ERROR_SUCCESS;
        StartBackgroundTransfer(PluginPath());
        return StartProcess(explorerPath);
    }
    const OwnedMutex activeUntilStopped{active.get()};

    const auto nativeExplorerPath = NativeImagePath(explorerPath);
    if (nativeExplorerPath.empty()) return ERROR_PATH_NOT_FOUND;
    const auto pluginPath = PluginPath();
    if (pluginPath.empty()) return ERROR_PATH_NOT_FOUND;
    const Module library(LoadLibraryExW(pluginPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32), FreeLibrary);
    if (!library) return GetLastError();
    const auto entryPoint = GetProcAddress(library.get(), "ExplorerAttachHook");
    if (!entryPoint) return ERROR_PROC_NOT_FOUND;
    const auto callback = std::bit_cast<HOOKPROC>(entryPoint);
    const UINT message = RegisterWindowMessageW(AttachMessageName);
    if (!message) return GetLastError();

    const Handle stop(CreateEventW(nullptr, TRUE, FALSE, stopName.c_str()), CloseHandle);
    if (!stop) return GetLastError();
    StartBackgroundTransfer(pluginPath);
    if (option == L"--open") {
        if (const int result = StartProcess(explorerPath); result != ERROR_SUCCESS) return result;
    }
    return AttachExplorerWindows(stop.get(), library.get(), callback, message, nativeExplorerPath);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
    try {
        return RunLauncher(commandLine);
    } catch (...) {
        return ERROR_OUTOFMEMORY;
    }
}
