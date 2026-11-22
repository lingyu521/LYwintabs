#include "ExplorerTab.h"

#include <new>

namespace {

std::wstring FolderName(PCIDLIST_ABSOLUTE folder, SIGDN format)
{
    PWSTR name = nullptr;
    const HRESULT hr = SHGetNameFromIDList(folder, format, &name);
    const std::unique_ptr<wchar_t, CoTaskMemDeleter> buffer(name);
    return SUCCEEDED(hr) && buffer ? std::wstring(buffer.get()) : std::wstring{};
}

} // namespace

// The sink owns its snapshot and never calls back into a tab or mutates the window.
// ExplorerBrowser can complete the initial browse inline and later browses asynchronously.
class ExplorerTab::Events final : public IExplorerBrowserEvents {
public:
    explicit Events(HWND receiver) : receiver_(receiver) {}

    IFACEMETHODIMP QueryInterface(REFIID iid, void** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IExplorerBrowserEvents) return E_NOINTERFACE;
        *result = static_cast<IExplorerBrowserEvents*>(this);
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&references_); }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG remaining = InterlockedDecrement(&references_);
        if (!remaining) delete this;
        return remaining;
    }

    IFACEMETHODIMP OnNavigationPending(PCIDLIST_ABSOLUTE) override
    {
        ++navigationSerial;
        return S_OK;
    }
    IFACEMETHODIMP OnViewCreated(IShellView*) override { return S_OK; }

    IFACEMETHODIMP OnNavigationComplete(PCIDLIST_ABSOLUTE folder) override
    {
        if (!receiver_) return S_OK;
        FolderId copy(ILCloneFull(folder));
        if (!copy) return E_OUTOFMEMORY;
        location = std::move(copy);
        Notify(false);
        return S_OK;
    }

    IFACEMETHODIMP OnNavigationFailed(PCIDLIST_ABSOLUTE) override
    {
        if (receiver_) Notify(true);
        return S_OK;
    }

    void Detach() noexcept { receiver_ = nullptr; }

    FolderId location;
    unsigned long long revision = 0;
    std::uint64_t navigationSerial = 0;
    bool failed = false;

private:
    void Notify(bool navigationFailed)
    {
        failed = navigationFailed;
        ++revision;
        PostMessageW(receiver_, NavigationChanged, 0, 0);
    }

    LONG references_ = 1;
    HWND receiver_;
};

ExplorerTab::ExplorerTab() = default;

ExplorerTab::~ExplorerTab()
{
    if (events_) events_->Detach();
    if (subscribed_) browser_->Unadvise(eventCookie_);
    if (initialized_) browser_->Destroy();
    browser_.Reset();
    events_.Reset();
    if (host_) DestroyWindow(host_);
}

HRESULT ExplorerTab::Initialize(HWND parent, const RECT& bounds)
{
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    host_ = CreateWindowExW(0, L"Static", nullptr,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        bounds.left, bounds.top, width, height, parent, nullptr, nullptr, nullptr);
    if (!host_) return HRESULT_FROM_WIN32(GetLastError());

    HRESULT hr = CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(browser_.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return hr;

    const RECT client{0, 0, width, height};
    const FOLDERSETTINGS settings{FVM_DETAILS, FWF_SHOWSELALWAYS};
    hr = browser_->Initialize(host_, &client, &settings);
    if (FAILED(hr)) return hr;
    initialized_ = true;

    hr = browser_->SetOptions(EBO_NOPERSISTVIEWSTATE);
    if (FAILED(hr)) return hr;

    events_.Attach(new (std::nothrow) Events(parent));
    if (!events_) return E_OUTOFMEMORY;
    hr = browser_->Advise(events_.Get(), &eventCookie_);
    subscribed_ = SUCCEEDED(hr);
    return hr;
}

void ExplorerTab::Resize(const RECT& bounds) const
{
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    SetWindowPos(host_, nullptr, bounds.left, bounds.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    browser_->SetRect(nullptr, RECT{0, 0, width, height});
}

void ExplorerTab::Show(bool visible) const
{
    ShowWindow(host_, visible ? SW_SHOWNA : SW_HIDE);
}

bool ExplorerTab::ContainsWindow(HWND window) const
{
    return window && (window == host_ || IsChild(host_, window));
}

bool ExplorerTab::FocusView() const
{
    Microsoft::WRL::ComPtr<IShellView> view;
    if (FAILED(browser_->GetCurrentView(IID_PPV_ARGS(view.GetAddressOf())))) return false;
    view->UIActivate(SVUIA_ACTIVATE_FOCUS);
    if (!ContainsWindow(GetFocus())) {
        HWND window = nullptr;
        if (SUCCEEDED(view->GetWindow(&window)) && window) SetFocus(window);
    }
    return ContainsWindow(GetFocus());
}

bool ExplorerTab::TranslateKey(MSG& message) const
{
    Microsoft::WRL::ComPtr<IShellView> view;
    return SUCCEEDED(browser_->GetCurrentView(IID_PPV_ARGS(view.GetAddressOf()))) &&
        view->TranslateAccelerator(&message) == S_OK;
}

HRESULT ExplorerTab::Browse(PCIDLIST_ABSOLUTE folder, UINT flags) const
{
    return browser_->BrowseToIDList(folder, flags);
}

PCIDLIST_ABSOLUTE ExplorerTab::CurrentFolder() const
{
    return events_->location.get();
}

std::uint64_t ExplorerTab::NavigationSerial() const
{
    return events_->navigationSerial;
}

bool ExplorerTab::ReadNavigationChange()
{
    if (appliedRevision_ == events_->revision) return false;
    appliedRevision_ = events_->revision;
    const bool failed = events_->failed;
    const FolderId location(events_->location ? ILCloneFull(events_->location.get()) : nullptr);
    if (location) {
        address = FolderName(location.get(), SIGDN_FILESYSPATH);
        if (address.empty()) address = FolderName(location.get(), SIGDN_DESKTOPABSOLUTEPARSING);
        title = FolderName(location.get(), SIGDN_NORMALDISPLAY);
        if (title.empty()) title = address.empty() ? L"文件夹" : address;
    }
    status = failed ? L"无法打开目标文件夹。" : L"";
    return true;
}

HRESULT ExplorerTab::ParseFolder(const std::wstring& path, FolderId& result)
{
    if (path.empty()) return E_INVALIDARG;
    PIDLIST_ABSOLUTE folder = nullptr;
    SFGAOF attributes = 0;
    const HRESULT hr = SHParseDisplayName(path.c_str(), nullptr, &folder, SFGAO_FOLDER, &attributes);
    FolderId parsed(folder);
    if (FAILED(hr)) return hr;
    if (!(attributes & SFGAO_FOLDER)) return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    result = std::move(parsed);
    return S_OK;
}
