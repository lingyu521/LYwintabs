#include "ExplorerExtension.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlguid.h>
#include <strsafe.h>
#include <wrl/client.h>

#include <atomic>
#include <cstring>
#include <new>

namespace {

std::atomic<long> moduleReferences = 0;
constexpr wchar_t Title[] = L"LYwintabs 收藏栏";
constexpr wchar_t Help[] = L"显示或隐藏当前资源管理器窗口的 LYwintabs 横向收藏栏";
constexpr wchar_t Verb[] = L"LYwintabs.FavoritesBar";

using Microsoft::WRL::ComPtr;

template<size_t Length>
HRESULT CopyText(const wchar_t (&text)[Length], PWSTR* output)
{
    if (!output) return E_POINTER;
    *output = static_cast<PWSTR>(CoTaskMemAlloc(sizeof(text)));
    if (!*output) return E_OUTOFMEMORY;
    std::memcpy(*output, text, sizeof(text));
    return S_OK;
}

class MenuHandler final : public IShellExtInit, public IContextMenu, public IExplorerCommand, public IObjectWithSite {
public:
    IFACEMETHODIMP QueryInterface(REFIID iid, void** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IContextMenu) *result = static_cast<IContextMenu*>(this);
        else if (iid == IID_IShellExtInit) *result = static_cast<IShellExtInit*>(this);
        else if (iid == IID_IExplorerCommand) *result = static_cast<IExplorerCommand*>(this);
        else if (iid == IID_IObjectWithSite) *result = static_cast<IObjectWithSite*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }

    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE, IDataObject*, HKEY) override { return S_OK; }

    IFACEMETHODIMP QueryContextMenu(HMENU menu, UINT index, UINT firstId, UINT lastId, UINT flags) override
    {
        if ((flags & CMF_DEFAULTONLY) || firstId > lastId) return S_OK;
        if (!InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING, firstId, Title)) return E_FAIL;
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 1);
    }

    IFACEMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO command) override
    {
        if (!command) return E_INVALIDARG;
        if (IS_INTRESOURCE(command->lpVerb)) {
            if (LOWORD(command->lpVerb) != 0) return E_INVALIDARG;
        } else if (command->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) && (command->fMask & CMIC_MASK_UNICODE)) {
            const auto* extended = reinterpret_cast<const CMINVOKECOMMANDINFOEX*>(command);
            if (!extended->lpVerbW || lstrcmpiW(extended->lpVerbW, Verb)) return E_INVALIDARG;
        } else if (lstrcmpiA(command->lpVerb, "LYwintabs.FavoritesBar")) {
            return E_INVALIDARG;
        }
        try {
            return ToggleFavoritesBar(command->hwnd);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_FAIL;
        }
    }

    IFACEMETHODIMP GetCommandString(UINT_PTR id, UINT type, UINT*, LPSTR text, UINT capacity) override
    {
        if (id) return E_INVALIDARG;
        if (type == GCS_VALIDATEA || type == GCS_VALIDATEW) return S_OK;
        if (type == GCS_VERBW) return StringCchCopyW(reinterpret_cast<wchar_t*>(text), capacity, Verb);
        if (type == GCS_VERBA) return StringCchCopyA(text, capacity, "LYwintabs.FavoritesBar");
        if (type == GCS_HELPTEXTW) return StringCchCopyW(reinterpret_cast<wchar_t*>(text), capacity, Help);
        return E_NOTIMPL;
    }

    IFACEMETHODIMP SetSite(IUnknown* site) override
    {
        site_ = site;
        return S_OK;
    }

    IFACEMETHODIMP GetSite(REFIID iid, void** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        return site_ ? site_->QueryInterface(iid, result) : E_FAIL;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, PWSTR* title) override { return CopyText(Title, title); }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, PWSTR* tip) override { return CopyText(Help, tip); }
    IFACEMETHODIMP GetIcon(IShellItemArray*, PWSTR* icon) override
    {
        if (!icon) return E_POINTER;
        *icon = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(GUID* name) override
    {
        if (!name) return E_POINTER;
        *name = FavoritesExtensionId;
        return S_OK;
    }

    IFACEMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* state) override
    {
        if (!state) return E_POINTER;
        *state = ECS_ENABLED;
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override
    {
        if (!flags) return E_POINTER;
        *flags = ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** children) override
    {
        if (!children) return E_POINTER;
        *children = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP Invoke(IShellItemArray*, IBindCtx*) override
    {
        if (!site_) return E_FAIL;
        HWND owner = nullptr;
        ComPtr<IServiceProvider> services;
        ComPtr<IShellBrowser> browser;
        if (SUCCEEDED(site_.As(&services)) &&
            SUCCEEDED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(browser.GetAddressOf())))) {
            browser->GetWindow(&owner);
        }
        ComPtr<IOleWindow> window;
        if (!owner && SUCCEEDED(site_.As(&window))) window->GetWindow(&owner);
        CMINVOKECOMMANDINFO command{sizeof(CMINVOKECOMMANDINFO)};
        command.hwnd = owner;
        return InvokeCommand(&command);
    }

private:
    ModuleReference module_;
    std::atomic<ULONG> references_ = 1;
    ComPtr<IUnknown> site_;
};

class MenuClassFactory final : public IClassFactory {
public:
    IFACEMETHODIMP QueryInterface(REFIID iid, void** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *result = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* handler = new (std::nothrow) MenuHandler;
        if (!handler) return E_OUTOFMEMORY;
        const HRESULT hr = handler->QueryInterface(iid, result);
        handler->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) ++moduleReferences;
        else --moduleReferences;
        return S_OK;
    }

private:
    ModuleReference module_;
    std::atomic<ULONG> references_ = 1;
};

} // namespace

ModuleReference::ModuleReference() noexcept { ++moduleReferences; }
ModuleReference::~ModuleReference() { --moduleReferences; }

STDAPI DllCanUnloadNow()
{
    return moduleReferences == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result)
{
    if (!result) return E_POINTER;
    *result = nullptr;
    if (clsid != FavoritesExtensionId) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) MenuClassFactory;
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(iid, result);
    factory->Release();
    return hr;
}
