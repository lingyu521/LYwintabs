#pragma once

#include <windows.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <memory>
#include <cstdint>
#include <string>
#include <type_traits>

struct CoTaskMemDeleter {
    void operator()(void* value) const noexcept { CoTaskMemFree(value); }
};

using FolderId = std::unique_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>, CoTaskMemDeleter>;

inline constexpr UINT NavigationChanged = WM_APP + 1;

class ExplorerTab {
public:
    ExplorerTab();
    ~ExplorerTab();
    ExplorerTab(const ExplorerTab&) = delete;
    ExplorerTab& operator=(const ExplorerTab&) = delete;

    HRESULT Initialize(HWND parent, const RECT& bounds);
    void Resize(const RECT& bounds) const;
    void Show(bool visible) const;
    bool ContainsWindow(HWND window) const;
    bool FocusView() const;
    bool TranslateKey(MSG& message) const;

    HRESULT Browse(PCIDLIST_ABSOLUTE folder, UINT flags = SBSP_ABSOLUTE) const;
    PCIDLIST_ABSOLUTE CurrentFolder() const;
    std::uint64_t NavigationSerial() const;
    bool ReadNavigationChange();
    static HRESULT ParseFolder(const std::wstring& path, FolderId& result);

    std::wstring title = L"新标签";
    std::wstring address;
    std::wstring status;
    std::uint64_t lookupRequest = 0;
    std::uint64_t lookupNavigationSerial = 0;

private:
    class Events;
    HWND host_ = nullptr;
    Microsoft::WRL::ComPtr<IExplorerBrowser> browser_;
    Microsoft::WRL::ComPtr<Events> events_;
    DWORD eventCookie_ = 0;
    bool initialized_ = false;
    bool subscribed_ = false;
    unsigned long long appliedRevision_ = 0;
};
