#include "ExplorerExtension.h"
#include "ExplorerLanguage.h"
#include "ExplorerIcons.h"
#include "ExplorerPreferences.h"
#include "AddressBookmarkButton.h"
#include "BookmarkManager.h"
#include "Favorites.h"
#include "FolderTabs.h"
#include "Resource.h"
#include "WindowsTheme.h"

#include <commctrl.h>
#include <ole2.h>
#include <gdiplus.h>
#include <exdisp.h>
#include <roapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlguid.h>
#include <uiribbon.h>
#include <uiribbonkeydef.h>
#include <uxtheme.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cwchar>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t AttachingProperty[] = L"LYwintabs.FavoritesBar.Attaching";
constexpr wchar_t StartupKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t StartupValue[] = L"LYwintabs";
constexpr wchar_t LauncherName[] = L"LYwintabs.Launcher.exe";
constexpr wchar_t ExplorerCommandKey[] = L"Software\\Classes\\Directory\\Background\\shell\\LYwintabs.FavoritesBar";
constexpr UINT_PTR RefreshTimer = 1;
constexpr UINT OpenHiddenSettings = WM_APP + 0x470;
constexpr UINT RefreshViewLayout = WM_APP + 0x471;
constexpr WORD AddCommand = 1;
constexpr WORD MoreCommand = 2;
constexpr WORD SettingsCommand = 3;
constexpr WORD TransferCommand = 4;
constexpr WORD FirstFavorite = 100;
constexpr int SettingsInset = 31;
constexpr int BookmarkIconSize = 16;
constexpr int BookmarkInset = 10;
constexpr int BookmarkIconGap = 6;
constexpr int BookmarkChevronSpace = 16;
constexpr UINT LayoutFlags = SWP_NOACTIVATE | SWP_NOZORDER;
enum FavoriteAction : UINT { Open = 1, Edit, Remove, OpenInTab };

// Windows 10 replaces the entire file-view window when navigating. One hook per
// Explorer UI thread lets each attached bar subclass its new view before it is shown.
thread_local HHOOK viewCreationHook = nullptr;
thread_local unsigned viewCreationUsers = 0;

struct ExplorerSettings {
    DWORD appearance;
    bool startup;
    bool canSetStartup;
    bool useTabs;
    const Favorites* store;
    bool offerBookmarkAdd;
    bool syncWindowsDark;
    UiLanguage language;
    bool bookmarksChanged = false;
};

std::wstring StartupCommand(HMODULE dll)
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(dll, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return {};
    std::wstring launcher(path, length);
    const auto separators = launcher.find_last_of(L"\\/");
    if (separators == std::wstring::npos) return {};
    launcher.replace(separators + 1, std::wstring::npos, LauncherName);
    const DWORD attributes = GetFileAttributesW(launcher.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return L"\"" + launcher + L"\"";
}

LSTATUS ReadStartupCommand(wchar_t (&value)[MAX_PATH + 3])
{
    DWORD bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, StartupKey, StartupValue, RRF_RT_REG_SZ, nullptr, value, &bytes);
}

bool SaveStartupSetting(const std::wstring& command, bool enabled)
{
    wchar_t existing[MAX_PATH + 3]{};
    const LSTATUS read = ReadStartupCommand(existing);
    if (read == ERROR_SUCCESS && lstrcmpiW(command.c_str(), existing) != 0) return false;
    if (read != ERROR_FILE_NOT_FOUND && read != ERROR_SUCCESS) return false;
    if (!enabled) return read == ERROR_FILE_NOT_FOUND ||
        RegDeleteKeyValueW(HKEY_CURRENT_USER, StartupKey, StartupValue) == ERROR_SUCCESS;
    return RegSetKeyValueW(HKEY_CURRENT_USER, StartupKey, StartupValue, REG_SZ, command.c_str(),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool RequestTransferWindow(const std::wstring& path, bool activate)
{
    const UINT message = activate ? RegisterWindowMessageW(ActivateTransferMessage) : 0;
    if (activate && !message) return false;
    struct Request {
        const std::wstring& path;
        UINT message;
        std::wstring image;
        bool posted = false;
    } request{path, message, std::wstring(32768, L'\0')};

    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        if (!GetPropW(window, TransferPanelProperty)) return TRUE;
        auto& target = *reinterpret_cast<Request*>(context);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        const std::unique_ptr<void, decltype(&CloseHandle)> process(
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), CloseHandle);
        DWORD length = static_cast<DWORD>(target.image.size());
        if (!process || !QueryFullProcessImageNameW(process.get(), 0, target.image.data(), &length) ||
            CompareStringOrdinal(target.path.c_str(), -1, target.image.c_str(), -1, TRUE) != CSTR_EQUAL) return TRUE;

        if (target.message) AllowSetForegroundWindow(pid);
        target.posted = !target.message || PostMessageW(window, target.message, 0, 0) != FALSE;
        return !target.posted;
    }, reinterpret_cast<LPARAM>(&request));
    return request.posted;
}

UINT AppearanceChangedMessage()
{
    static const UINT message = RegisterWindowMessageW(L"LYwintabs.Explorer.AppearanceChanged");
    return message;
}

UINT BookmarksChangedMessage()
{
    static const UINT message = RegisterWindowMessageW(L"LYwintabs.Explorer.BookmarksChanged");
    return message;
}

bool ClassIs(HWND window, const wchar_t* expected)
{
    wchar_t name[64]{};
    return GetClassNameW(window, name, ARRAYSIZE(name)) && !lstrcmpW(name, expected);
}

void BroadcastToOtherBars(HWND source, UINT message)
{
    if (!message) return;
    struct Notification { HWND source; UINT message; } notification{source, message};
    EnumWindows([](HWND root, LPARAM context) -> BOOL {
        const auto& notice = *reinterpret_cast<const Notification*>(context);
        if (root != notice.source && ClassIs(root, L"CabinetWClass")) {
            if (const auto bar = static_cast<HWND>(GetPropW(root, BarProperty))) {
                PostMessageW(bar, notice.message, 0, 0);
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&notification));
}

RECT RectIn(HWND window, HWND parent)
{
    RECT rect{};
    GetWindowRect(window, &rect);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

bool MoveTo(HWND window, const RECT& rect, HWND insertAfter = nullptr, UINT flags = LayoutFlags)
{
    return SetWindowPos(window, insertAfter, rect.left, rect.top, rect.right - rect.left,
        rect.bottom - rect.top, flags) != FALSE;
}

bool ExplorerProcess()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!length || length == ARRAYSIZE(path)) return false;
    const std::wstring_view image(path, length);
    const auto name = image.substr(image.find_last_of(L"\\/") + 1);
    return CompareStringOrdinal(name.data(), static_cast<int>(name.size()), L"explorer.exe", -1, TRUE) == CSTR_EQUAL;
}

HWND ActiveView(HWND root)
{
    if (IsIconic(root)) return nullptr;
    HWND result = nullptr;
    EnumChildWindows(root, [](HWND child, LPARAM output) -> BOOL {
        if (!IsWindowVisible(child) || GetWindowThreadProcessId(child, nullptr) != GetCurrentThreadId() ||
            !ClassIs(child, L"SHELLDLL_DefView")) return TRUE;
        *reinterpret_cast<HWND*>(output) = child;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

struct ExplorerView {
    ComPtr<IWebBrowser2> automation;
    ComPtr<IShellBrowser> browser;
    ComPtr<IShellView> shellView;
};

bool QueryRibbon(IServiceProvider* services, IUIFramework** framework)
{
    return services && SUCCEEDED(services->QueryService(__uuidof(IUIFramework), __uuidof(IUIFramework),
        reinterpret_cast<void**>(framework))) && *framework;
}

ComPtr<IPropertyStore> RibbonProperties(const ExplorerView& explorer)
{
    for (IUnknown* candidate : {static_cast<IUnknown*>(explorer.browser.Get()),
            static_cast<IUnknown*>(explorer.automation.Get()), static_cast<IUnknown*>(explorer.shellView.Get())}) {
        if (!candidate) continue;
        ComPtr<IServiceProvider> services;
        ComPtr<IUIFramework> framework;
        candidate->QueryInterface(IID_PPV_ARGS(services.GetAddressOf()));
        if (!QueryRibbon(services.Get(), framework.ReleaseAndGetAddressOf())) {
            ComPtr<IServiceProvider> topLevel;
            if (services) services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(topLevel.GetAddressOf()));
            if (QueryRibbon(topLevel.Get(), framework.ReleaseAndGetAddressOf())) {
                services = std::move(topLevel);
            } else {
                ComPtr<IObjectWithSite> site;
                if (FAILED(candidate->QueryInterface(IID_PPV_ARGS(site.GetAddressOf()))) ||
                    FAILED(site->GetSite(IID_PPV_ARGS(services.ReleaseAndGetAddressOf()))) ||
                    !QueryRibbon(services.Get(), framework.ReleaseAndGetAddressOf())) continue;
            }
        }
        ComPtr<IUIRibbon> ribbon;
        ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(framework->GetView(0, IID_PPV_ARGS(ribbon.GetAddressOf()))) &&
            SUCCEEDED(ribbon.As(&properties))) return properties;
    }
    return {};
}

bool ReadQuickAccessDock(IPropertyStore& ribbon, ULONG& dock)
{
    PROPVARIANT current{};
    const HRESULT read = ribbon.GetValue(UI_PKEY_QuickAccessToolbarDock, &current);
    const bool valid = SUCCEEDED(read) && current.vt == VT_UI4 &&
        (current.ulVal == UI_CONTROLDOCK_TOP || current.ulVal == UI_CONTROLDOCK_BOTTOM);
    if (valid) dock = current.ulVal;
    PropVariantClear(&current);
    return valid;
}

bool WriteQuickAccessDock(IPropertyStore& ribbon, ULONG dock)
{
    PROPVARIANT docking{};
    docking.vt = VT_UI4;
    docking.ulVal = dock;
    return SUCCEEDED(ribbon.SetValue(UI_PKEY_QuickAccessToolbarDock, docking)) && SUCCEEDED(ribbon.Commit());
}

class QuickAccessDock {
public:
    bool Apply(ComPtr<IPropertyStore> ribbon, bool showRow)
    {
        if (ribbon_) ribbon = ribbon_;
        if (!ribbon) return false;
        ULONG current = 0;
        if (!ReadQuickAccessDock(*ribbon.Get(), current)) return false;
        const ULONG desired = showRow ? UI_CONTROLDOCK_BOTTOM : UI_CONTROLDOCK_TOP;
        if (ribbon_ && applied_ != current) ribbon_.Reset();
        if (current == desired) return true;
        if (!ribbon_) original_ = current;
        ribbon_ = std::move(ribbon);
        applied_ = desired;
        return WriteQuickAccessDock(*ribbon_.Get(), desired);
    }

    bool Restore()
    {
        if (!ribbon_) return true;
        ULONG current = 0;
        if (!ReadQuickAccessDock(*ribbon_.Get(), current)) return false;
        // The native toolbar can be repositioned independently while the plugin is open.
        if (current == applied_ && current != original_ && !WriteQuickAccessDock(*ribbon_.Get(), original_)) return false;
        ribbon_.Reset();
        return true;
    }

private:
    ComPtr<IPropertyStore> ribbon_;
    ULONG original_ = 0;
    ULONG applied_ = 0;
};

ExplorerView FindExplorerView(HWND root, HWND view)
{
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER,
            IID_PPV_ARGS(windows.GetAddressOf())))) return {};
    long count = 0;
    if (FAILED(windows->get_Count(&count))) return {};
    for (long index = 0; index < count; ++index) {
        VARIANT item{};
        item.vt = VT_I4;
        item.lVal = index;
        ComPtr<IDispatch> dispatch;
        ExplorerView candidate;
        if (FAILED(windows->Item(item, dispatch.GetAddressOf())) || !dispatch ||
            FAILED(dispatch.As(&candidate.automation))) continue;
        SHANDLE_PTR browserWindow = 0;
        if (FAILED(candidate.automation->get_HWND(&browserWindow)) ||
            GetAncestor(reinterpret_cast<HWND>(browserWindow), GA_ROOT) != root) continue;
        ComPtr<IServiceProvider> provider;
        if (FAILED(dispatch.As(&provider)) ||
            FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(candidate.browser.GetAddressOf()))) ||
            FAILED(candidate.browser->QueryActiveShellView(candidate.shellView.GetAddressOf()))) continue;
        HWND candidateWindow = nullptr;
        if (SUCCEEDED(candidate.shellView->GetWindow(&candidateWindow)) && candidateWindow == view) return candidate;
    }
    return {};
}

HRESULT ReadCurrentFavorite(IShellView& view, Favorite& item)
{
    FolderId folder;
    HRESULT hr = ReadCurrentFolder(view, folder);
    if (FAILED(hr)) return hr;

    PWSTR path = nullptr;
    hr = SHGetNameFromIDList(folder.get(), SIGDN_FILESYSPATH, &path);
    const std::unique_ptr<wchar_t, ShellMemory> pathText(path);
    if (FAILED(hr)) return hr;
    if (!pathText) return E_FAIL;
    item.path = pathText.get();

    PWSTR name = nullptr;
    hr = SHGetNameFromIDList(folder.get(), SIGDN_NORMALDISPLAY, &name);
    const std::unique_ptr<wchar_t, ShellMemory> nameText(name);
    item.name = SUCCEEDED(hr) && nameText && *nameText ? nameText.get() : item.path;
    return S_OK;
}

struct WinRtApartment {
    HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
    ~WinRtApartment() { if (SUCCEEDED(hr)) RoUninitialize(); }
};

bool UsesDarkAppColors()
{
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) return false;
    if (const auto dark = ReadWindowsAppsDarkMode()) return *dark;
    try {
        using namespace winrt::Windows::UI::ViewManagement;
        const auto color = UISettings().GetColorValue(UIColorType::Foreground);
        return (5 * color.G + 2 * color.R + color.B) > 8 * 128;
    } catch (const winrt::hresult_error&) {
        return false;
    }
}

struct BookmarkPalette {
    COLORREF surface;
    COLORREF divider;
    COLORREF chip;
    COLORREF folderChip;
    COLORREF hover;
    COLORREF pressed;
    COLORREF text;
    COLORREF folderIcon;
    COLORREF arrow;
    bool highContrast = false;
};

BookmarkPalette BookmarkColors(bool dark, bool topBars)
{
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        const COLORREF window = GetSysColor(COLOR_WINDOW);
        const COLORREF text = GetSysColor(COLOR_WINDOWTEXT);
        const COLORREF highlight = GetSysColor(COLOR_HIGHLIGHT);
        return {window, text, window, window, highlight, highlight, text, text, text, true};
    }
    if (topBars) {
        if (dark) return {RGB(44, 47, 53), RGB(63, 68, 76), RGB(44, 47, 53), RGB(44, 47, 53),
            RGB(57, 63, 72), RGB(67, 74, 85), RGB(224, 228, 235), RGB(178, 197, 224), RGB(186, 193, 204)};
        return {RGB(252, 253, 255), RGB(218, 224, 232), RGB(252, 253, 255), RGB(252, 253, 255),
            RGB(232, 239, 249), RGB(219, 230, 244), RGB(41, 50, 65), RGB(68, 93, 130), RGB(93, 108, 129)};
    }
    if (dark) return {RGB(58, 58, 58), RGB(77, 77, 77), RGB(58, 58, 58), RGB(68, 68, 68),
        RGB(78, 78, 78), RGB(88, 88, 88), RGB(224, 224, 224), RGB(201, 201, 201), RGB(201, 201, 201)};
    return {RGB(255, 255, 255), RGB(229, 229, 229), RGB(255, 255, 255), RGB(241, 241, 241),
        RGB(234, 234, 234), RGB(221, 221, 221), RGB(66, 66, 66), RGB(80, 80, 80), RGB(88, 88, 88)};
}

void DrawBookmarkFolder(HDC dc, int x, int y, int size, COLORREF color)
{
    using namespace Gdiplus;
    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    const REAL scale = static_cast<REAL>(size) / 24.0f;
    Matrix transform(scale, 0, 0, scale, static_cast<REAL>(x), static_cast<REAL>(y));
    graphics.SetTransform(&transform);

    // Lucide folder-open (assets/folder-open.svg), with the SVG arcs converted to cubic curves.
    GraphicsPath glyph;
    glyph.AddLine(6.0f, 14.0f, 7.5f, 11.1f);
    glyph.AddBezier(7.5f, 11.1f, 7.83f, 10.44f, 8.50f, 10.02f, 9.24f, 10.0f);
    glyph.AddLine(9.24f, 10.0f, 20.0f, 10.0f);
    glyph.AddBezier(20.0f, 10.0f, 20.62f, 10.0f, 21.20f, 10.28f, 21.58f, 10.77f);
    glyph.AddBezier(21.58f, 10.77f, 21.96f, 11.26f, 22.09f, 11.90f, 21.94f, 12.5f);
    glyph.AddLine(21.94f, 12.5f, 20.4f, 18.5f);
    glyph.AddBezier(20.4f, 18.5f, 20.17f, 19.39f, 19.37f, 20.01f, 18.45f, 20.0f);
    glyph.AddLine(18.45f, 20.0f, 4.0f, 20.0f);
    glyph.AddBezier(4.0f, 20.0f, 2.90f, 20.0f, 2.0f, 19.10f, 2.0f, 18.0f);
    glyph.AddLine(2.0f, 18.0f, 2.0f, 5.0f);
    glyph.AddBezier(2.0f, 5.0f, 2.0f, 3.90f, 2.90f, 3.0f, 4.0f, 3.0f);
    glyph.AddLine(4.0f, 3.0f, 7.9f, 3.0f);
    glyph.AddBezier(7.9f, 3.0f, 8.58f, 2.99f, 9.22f, 3.33f, 9.59f, 3.9f);
    glyph.AddLine(9.59f, 3.9f, 10.4f, 5.1f);
    glyph.AddBezier(10.4f, 5.1f, 10.77f, 5.66f, 11.40f, 6.0f, 12.07f, 6.0f);
    glyph.AddLine(12.07f, 6.0f, 18.0f, 6.0f);
    glyph.AddBezier(18.0f, 6.0f, 19.10f, 6.0f, 20.0f, 6.90f, 20.0f, 8.0f);
    glyph.AddLine(20.0f, 8.0f, 20.0f, 10.0f);
    Pen stroke(Color(GetRValue(color), GetGValue(color), GetBValue(color)), 2.0f);
    stroke.SetStartCap(LineCapRound);
    stroke.SetEndCap(LineCapRound);
    stroke.SetLineJoin(LineJoinRound);
    graphics.DrawPath(&stroke, &glyph);
}

std::wstring ButtonLabel(std::wstring_view name)
{
    std::wstring label;
    for (wchar_t character : name) {
        label += character;
        if (character == L'&') label += character;
    }
    return label;
}

using BookmarkChildren = std::unordered_map<std::wstring_view, std::vector<const Favorite*>>;
using BookmarkMenu = std::unique_ptr<std::remove_pointer_t<HMENU>, decltype(&DestroyMenu)>;

void AppendBookmarkMenu(HMENU menu, const BookmarkChildren& children, std::wstring_view folder,
    size_t first, std::vector<std::wstring>& targets)
{
    const auto group = children.find(folder);
    if (group != children.end()) {
        for (size_t index = first; index < group->second.size(); ++index) {
            const auto& item = *group->second[index];
            auto label = ButtonLabel(item.name);
            BookmarkMenu submenu(item.IsFolder() ? CreatePopupMenu() : nullptr, DestroyMenu);
            if (item.IsFolder()) {
                if (!submenu) continue;
                AppendBookmarkMenu(submenu.get(), children, item.id, 0, targets);
            }
            MENUITEMINFOW entry{sizeof(entry)};
            entry.fMask = MIIM_STRING | MIIM_DATA | (submenu ? MIIM_SUBMENU : MIIM_ID);
            entry.dwTypeData = label.data();
            entry.dwItemData = targets.size() + 1;
            entry.wID = static_cast<UINT>(entry.dwItemData);
            entry.hSubMenu = submenu.get();
            if (InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE, &entry)) {
                submenu.release();
                targets.push_back(item.id);
            }
        }
    }
    if (GetMenuItemCount(menu) == 0) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, ExplorerText(L"（空文件夹）"));
}

bool ContainsMenu(HMENU root, HMENU candidate)
{
    if (root == candidate) return true;
    const int count = GetMenuItemCount(root);
    for (int index = 0; index < count; ++index) {
        if (const HMENU child = GetSubMenu(root, index); child && ContainsMenu(child, candidate)) return true;
    }
    return false;
}

HRESULT CALLBACK AboutNotification(HWND dialog, UINT notification, WPARAM, LPARAM detail, LONG_PTR)
{
    constexpr wchar_t authorEmail[] = L"mailto:270350257@qq.com";
    if (notification == TDN_HYPERLINK_CLICKED && detail &&
        !lstrcmpW(reinterpret_cast<const wchar_t*>(detail), authorEmail)) {
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(dialog, L"open", authorEmail, nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
            MessageBoxW(dialog, ExplorerText(L"无法打开默认邮件程序。"), ExplorerText(L"关于 LYwintabs"), MB_ICONWARNING);
        }
    }
    return S_OK;
}

void ShowAbout(HWND owner)
{
    wchar_t title[96]{};
    swprintf_s(title, L"LYwintabs %ls", ExplorerVersion);
    const UiLanguage language = ReadUiLanguage();
    const auto description = ExplorerText(
        L"Windows 文件资源管理器插件\nWindows 10 标签与书签栏，Windows 11 横向书签栏", language);
    const bool english = language == UiLanguage::English;
    const wchar_t* author = english ? L"\n\nAuthor  Lingyu Wang (王凌宇)\nEmail  " : L"\n\n作者  王凌宇\n邮箱  ";
    TASKDIALOGCONFIG dialog{sizeof(dialog)};
    dialog.hwndParent = owner;
    dialog.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    HMODULE plugin = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            BarProperty, &plugin)) {
        if (const HICON appIcon = LoadIconW(plugin, MAKEINTRESOURCEW(IDI_LYWINTABS))) {
            dialog.dwFlags |= TDF_USE_HICON_MAIN;
            dialog.hMainIcon = appIcon;
        }
    }
    dialog.dwCommonButtons = TDCBF_OK_BUTTON;
    dialog.pszWindowTitle = ExplorerText(L"关于 LYwintabs");
    dialog.pszMainInstruction = title;
    const auto content = std::wstring(description) + author +
        L"<a href=\"mailto:270350257@qq.com\">270350257@qq.com</a>";
    dialog.pszContent = content.c_str();
    dialog.pfCallback = AboutNotification;
    if (const HMODULE controls = GetModuleHandleW(L"comctl32.dll")) {
        // Common Controls 5 cannot load a DLL with a static TaskDialogIndirect import.
        auto entry = GetProcAddress(controls, "TaskDialogIndirect");
        if (!entry) entry = GetProcAddress(controls, MAKEINTRESOURCEA(345));
        if (entry && SUCCEEDED(std::bit_cast<decltype(&TaskDialogIndirect)>(entry)(&dialog, nullptr, nullptr, nullptr))) return;
    }
    const auto text = std::wstring(title) + L"\n\n" + description + author + L"270350257@qq.com";
    MessageBoxW(owner, text.c_str(), dialog.pszWindowTitle, MB_OK | MB_ICONINFORMATION);
}

INT_PTR CALLBACK AppearanceDialog(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        const auto& settings = *reinterpret_cast<const ExplorerSettings*>(lParam);
        LocalizeExplorerDialog(dialog, settings.language);
        const HWND language = GetDlgItem(dialog, IDC_UI_LANGUAGE);
        ComboBox_AddString(language, L"简体中文");
        ComboBox_AddString(language, L"English");
        ComboBox_SetCurSel(language, static_cast<int>(settings.language));
        const HWND theme = GetDlgItem(dialog, IDC_EXPLORER_THEME);
        ComboBox_AddString(theme, ExplorerText(L"跟随 Windows"));
        ComboBox_AddString(theme, ExplorerText(L"深色（同步 Windows）"));
        ComboBox_SetCurSel(theme, settings.syncWindowsDark ? 1 : 0);
        for (const int control : {IDC_TAB_DENSITY, IDC_FAVORITES_DENSITY}) {
            const HWND combo = GetDlgItem(dialog, control);
            ComboBox_AddString(combo, ExplorerText(L"紧凑"));
            ComboBox_AddString(combo, ExplorerText(L"加高"));
            const DWORD compact = control == IDC_TAB_DENSITY ? CompactTabs : CompactFavorites;
            ComboBox_SetCurSel(combo, (settings.appearance & compact) ? 0 : 1);
        }
        CheckDlgButton(dialog, IDC_SHOW_FOLDER_TITLE,
            (settings.appearance & HideFolderTitle) ? BST_UNCHECKED : BST_CHECKED);
        CheckDlgButton(dialog, IDC_HIDE_QUICK_ACCESS_TOOLBAR,
            settings.useTabs && !(settings.appearance & ShowQuickAccessToolbar) ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_SHOW_BOOKMARKS_BAR,
            (settings.appearance & HideBookmarks) ? BST_UNCHECKED : BST_CHECKED);
        CheckDlgButton(dialog, IDC_START_WITH_WINDOWS, settings.startup ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(dialog, IDC_TAB_DENSITY), settings.useTabs);
        EnableWindow(GetDlgItem(dialog, IDC_SHOW_FOLDER_TITLE), settings.useTabs);
        EnableWindow(GetDlgItem(dialog, IDC_HIDE_QUICK_ACCESS_TOOLBAR), settings.useTabs);
        EnableWindow(GetDlgItem(dialog, IDC_START_WITH_WINDOWS), settings.canSetStartup);
        if (!settings.offerBookmarkAdd) ShowWindow(GetDlgItem(dialog, IDC_ADD_BOOKMARK_FROM_SETTINGS), SW_HIDE);
        return TRUE;
    }
    if (message != WM_COMMAND) return FALSE;
    auto& settings = *reinterpret_cast<ExplorerSettings*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (LOWORD(wParam) == IDC_ADD_BOOKMARK_FROM_SETTINGS && settings.offerBookmarkAdd) {
        EndDialog(dialog, IDC_ADD_BOOKMARK_FROM_SETTINGS);
        return TRUE;
    }
    if (LOWORD(wParam) == IDC_OPEN_BOOKMARK_MANAGER) {
        const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
        settings.bookmarksChanged |= ShowBookmarkManager(dialog, instance, *settings.store);
        return TRUE;
    }
    if (LOWORD(wParam) == IDC_ABOUT) {
        try { ShowAbout(dialog); } catch (...) { MessageBeep(MB_ICONWARNING); }
        return TRUE;
    }
    if (LOWORD(wParam) != IDOK) return FALSE;
    const int language = ComboBox_GetCurSel(GetDlgItem(dialog, IDC_UI_LANGUAGE));
    if (language == 0 || language == 1) settings.language = static_cast<UiLanguage>(language);
    const int theme = ComboBox_GetCurSel(GetDlgItem(dialog, IDC_EXPLORER_THEME));
    if (theme != CB_ERR) settings.syncWindowsDark = theme == 1;
    settings.appearance &= ~(CompactFavorites | HideBookmarks);
    if (ComboBox_GetCurSel(GetDlgItem(dialog, IDC_FAVORITES_DENSITY)) == 0) settings.appearance |= CompactFavorites;
    if (IsDlgButtonChecked(dialog, IDC_SHOW_BOOKMARKS_BAR) != BST_CHECKED) settings.appearance |= HideBookmarks;
    if (settings.useTabs) {
        settings.appearance &= ~(CompactTabs | HideFolderTitle | ShowQuickAccessToolbar);
        if (ComboBox_GetCurSel(GetDlgItem(dialog, IDC_TAB_DENSITY)) == 0) settings.appearance |= CompactTabs;
        if (IsDlgButtonChecked(dialog, IDC_SHOW_FOLDER_TITLE) != BST_CHECKED) settings.appearance |= HideFolderTitle;
        if (IsDlgButtonChecked(dialog, IDC_HIDE_QUICK_ACCESS_TOOLBAR) != BST_CHECKED) settings.appearance |= ShowQuickAccessToolbar;
    }
    settings.startup = IsDlgButtonChecked(dialog, IDC_START_WITH_WINDOWS) == BST_CHECKED;
    EndDialog(dialog, IDOK);
    return TRUE;
}

struct SettingsChanges {
    INT_PTR action;
    DWORD appearance;
    bool colorsChanged;
    bool bookmarksChanged;
    bool languageChanged = false;
};

SettingsChanges ShowSettingsDialog(HMODULE dll, HWND owner, const Favorites& store, DWORD appearance,
    bool useTabs, bool offerBookmarkAdd)
{
    const auto command = StartupCommand(dll);
    wchar_t previousCommand[MAX_PATH + 3]{};
    const LSTATUS read = ReadStartupCommand(previousCommand);
    const bool startup = !command.empty() && read == ERROR_SUCCESS && lstrcmpiW(command.c_str(), previousCommand) == 0;
    const bool syncWindowsDark = HasWindowsThemeRestorePoint();
    const UiLanguage language = ReadUiLanguage();
    ExplorerSettings selected{appearance, startup, !command.empty() && (startup || read == ERROR_FILE_NOT_FOUND),
        useTabs, &store, offerBookmarkAdd, syncWindowsDark, language};
    const auto result = DialogBoxParamW(dll, MAKEINTRESOURCEW(IDD_EXPLORER_SETTINGS), owner, AppearanceDialog,
        reinterpret_cast<LPARAM>(&selected));
    SettingsChanges changes{result, appearance, false, selected.bookmarksChanged};
    if (result != IDOK) return changes;

    if (selected.language != language) {
        changes.languageChanged = SaveUiLanguage(selected.language);
        if (!changes.languageChanged)
            MessageBoxW(owner, ExplorerText(L"无法保存界面语言。"), L"LYwintabs", MB_ICONWARNING);
    }
    if (selected.syncWindowsDark != syncWindowsDark) {
        const auto error = SetWindowsDarkMode(selected.syncWindowsDark);
        changes.colorsChanged = true;
        if (error != ERROR_SUCCESS) {
            const auto message = std::wstring(ExplorerText(L"无法更新 Windows 配色。错误代码 ")) + std::to_wstring(error) + L"。";
            MessageBoxW(owner, message.c_str(), L"LYwintabs", MB_ICONWARNING);
        }
    }
    if (selected.canSetStartup && selected.startup != startup && !SaveStartupSetting(command, selected.startup)) {
        MessageBoxW(owner, ExplorerText(L"无法保存开机启动设置。"), L"LYwintabs", MB_ICONWARNING);
    }
    if (selected.appearance != appearance) {
        if (RegSetKeyValueW(HKEY_CURRENT_USER, AppearanceKey, AppearanceValue, REG_DWORD,
                &selected.appearance, sizeof(selected.appearance)) == ERROR_SUCCESS)
            changes.appearance = selected.appearance;
        else
            MessageBoxW(owner, ExplorerText(L"无法保存设置。"), L"LYwintabs", MB_ICONWARNING);
    }
    if (changes.colorsChanged || changes.appearance != appearance || changes.languageChanged)
        BroadcastToOtherBars(owner, AppearanceChangedMessage());
    return changes;
}

class FavoritesBar {
public:
    FavoritesBar(HWND root, bool useTabs) noexcept : root_(root), useTabs_(useTabs) {}
    ~FavoritesBar()
    {
        if (barFont_) DeleteObject(barFont_);
        if (bookmarkGraphics_) Gdiplus::GdiplusShutdown(bookmarkGraphics_);
    }

    ULONG AddRef() noexcept { return ++references_; }
    ULONG Release() noexcept
    {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT Start()
    {
        HRESULT hr = store_.Initialize();
        if (SUCCEEDED(hr)) {
            const WinRtApartment apartment;
            if (FAILED(apartment.hr)) return apartment.hr;
            dark_ = UsesDarkAppColors();
            hr = store_.Load(items_);
        }
        if (FAILED(hr)) return hr;
        const Gdiplus::GdiplusStartupInput graphics;
        if (Gdiplus::GdiplusStartup(&bookmarkGraphics_, &graphics, nullptr) != Gdiplus::Ok) bookmarkGraphics_ = 0;
        bar_ = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 0, 0, root_, nullptr, nullptr, nullptr);
        if (!bar_) return E_FAIL;
        if (!SetWindowSubclass(bar_, BarProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
            DestroyWindow(std::exchange(bar_, nullptr));
            return E_FAIL;
        }
        AddRef(); // The bar window owns a reference until WM_NCDESTROY.
        if (!SetPropW(root_, BarProperty, bar_) || !SetTimer(bar_, RefreshTimer, 125, nullptr)) return E_FAIL;
        if (!SetWindowSubclass(root_, FrameProc, 1, reinterpret_cast<DWORD_PTR>(this))) return E_FAIL;
        if (useTabs_ && !CreateTabStrip()) return E_FAIL;
        ApplyAppearance(ReadAppearance());
        settings_ = CreateButton(SettingsCommand, ExplorerText(L"设置"));
        addBookmark_ = CreateButton(AddCommand, ExplorerText(L"添加书签"));
        transfer_ = CreateButton(TransferCommand, ExplorerText(L"局域网互传"));
        more_ = CreateButton(MoreCommand, useTabs_ ? L"»" : ExplorerText(L"更多…"));
        if (!settings_ || !addBookmark_ || !more_ || !transfer_) return E_FAIL;
        tooltip_ = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
            0, 0, 0, 0, bar_, nullptr, nullptr, nullptr);
        UpdateUtilityLabels(TTM_ADDTOOLW);
        Refresh();
        const bool laidOut = topBandsHeight_ || viewSlot_.applied;
        if (laidOut && useTabs_) WatchViewCreation();
        if (laidOut && !watchedFolder_) OpenTransfer(true);
        return laidOut ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    void Close() { if (bar_) DestroyWindow(bar_); }

private:
    bool Detach()
    {
        GUITHREADINFO gui{sizeof(gui)};
        constexpr DWORD busy = GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE | GUI_INMOVESIZE;
        if (detaching_ || !IsWindowEnabled(root_) ||
            (GetGUIThreadInfo(GetCurrentThreadId(), &gui) && (gui.flags & busy))) return false;
        detaching_ = true;
        if (quickAccess_.Restore() && SetNativeCaptionHidden(false) && RestoreTopBands() && DestroyWindow(bar_)) return true;
        detaching_ = false;
        return false;
    }

    bool CreateTabStrip()
    {
        tabHost_ = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 0, 0, bar_, nullptr, nullptr, nullptr);
        if (!tabHost_ || !SetWindowSubclass(tabHost_, TabsProc, 1, reinterpret_cast<DWORD_PTR>(this)) ||
            !tabs_.Initialize(tabHost_, root_, dark_)) return false;
        tabSettings_ = CreateWindowExW(0, L"Button", ExplorerText(L"设置"), WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, tabHost_, nullptr, nullptr, nullptr);
        return tabSettings_ != nullptr;
    }

    static LRESULT CALLBACK FrameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR context)
    {
        const ComPtr<FavoritesBar> bar(reinterpret_cast<FavoritesBar*>(context));
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, FrameProc, 1);
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (message == WM_THEMECHANGED || message == WM_SETTINGCHANGE || message == WM_SYSCOLORCHANGE ||
            message == WM_DWMCOLORIZATIONCOLORCHANGED) {
            bar->RefreshWindowsColors();
        }
        if ((message == WM_WINDOWPOSCHANGED || message == WM_SHOWWINDOW || message == WM_DPICHANGED) &&
            bar->useTabs_ && !bar->detaching_) {
            bar->QueueLayout();
        }
        return result;
    }

    void ApplyAppearance(DWORD appearance)
    {
        if ((appearance_ ^ appearance) & ShowQuickAccessToolbar) {
            quickAccessAttempts_ = 0;
            nextQuickAccessAttempt_ = 0;
        }
        appearance_ = appearance;
        tabs_.SetLayoutOptions((appearance_ & HideBookmarks) ? SettingsInset : 0, TabTopPadding());
        tabsHeight_ = 0;
        buttonsDirty_ = true;
    }

    void UpdateUtilityLabels(UINT tooltipMessage)
    {
        const UiLanguage language = ReadUiLanguage();
        const std::pair<HWND, const wchar_t*> commands[] = {
            {transfer_, L"局域网互传"}, {settings_, L"设置"}, {addBookmark_, L"添加书签"}, {tabSettings_, L"设置"},
        };
        for (const auto& [button, key] : commands) {
            if (!button) continue;
            const auto label = ExplorerText(key, language);
            SetWindowTextW(button, label);
            if (!tooltip_) continue;
            TOOLINFOW tool{sizeof(tool)};
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.hwnd = GetParent(button);
            tool.uId = reinterpret_cast<UINT_PTR>(button);
            tool.lpszText = const_cast<wchar_t*>(label);
            SendMessageW(tooltip_, tooltipMessage, 0, reinterpret_cast<LPARAM>(&tool));
        }
    }

    void ApplyLanguage()
    {
        UpdateUtilityLabels(TTM_UPDATETIPTEXTW);
        if (more_ && !useTabs_) SetWindowTextW(more_, ExplorerText(L"更多…"));
        addressStar_.UpdateLanguage();
        buttonsDirty_ = true;
    }

    int TabTopPadding() const { return (appearance_ & CompactTabs) ? 3 : 5; }

    bool SetNativeCaptionHidden(bool hidden)
    {
        if (captionHidden_ == hidden) return true;
        constexpr DWORD flags = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON;
        if (!IsWindow(root_) || FAILED(SetWindowThemeNonClientAttributes(root_, flags, hidden ? flags : 0))) return false;
        captionHidden_ = hidden;
        RedrawWindow(root_, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_NOCHILDREN);
        return true;
    }

    void OpenSettings()
    {
        HMODULE dll = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                BarProperty, &dll)) return;
        const auto changes = ShowSettingsDialog(dll, root_, store_, appearance_, useTabs_, !addressStar_.IsVisible());
        if (changes.bookmarksChanged) BookmarksEdited();
        if (!bar_) return;
        if (changes.action == IDC_ADD_BOOKMARK_FROM_SETTINGS) AddCurrentFavorite();
        if (changes.colorsChanged) RefreshWindowsColors();
        if (changes.languageChanged) ApplyLanguage();
        if (changes.appearance != appearance_) ApplyAppearance(changes.appearance);
        if (changes.languageChanged || buttonsDirty_) Refresh();
    }

    void PaintSettings(const DRAWITEMSTRUCT& item) const
    {
        RECT bounds = item.rcItem;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const auto brush = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        const auto palette = tabs_.Colors();
        SetDCBrushColor(item.hDC, pressed ? palette.pressed : palette.surface);
        FillRect(item.hDC, &bounds, brush);
        const COLORREF color = pressed && palette.highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : palette.text;
        if (!bookmarkGraphics_ || !DrawExplorerIcon(item.hDC, bounds, Scale(18), ExplorerIcon::Settings, color)) {
            const auto previousFont = SelectObject(item.hDC, BarFont());
            SetBkMode(item.hDC, TRANSPARENT);
            SetTextColor(item.hDC, color);
            DrawTextW(item.hDC, ExplorerText(L"设置"), -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (previousFont) SelectObject(item.hDC, previousFont);
        }
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            InflateRect(&bounds, -Scale(1), -Scale(1));
            DrawFocusRect(item.hDC, &bounds);
        }
    }

    void PaintSurface(HWND window, bool bookmarkBar = false) const
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        const auto palette = BookmarkColors(dark_, useTabs_);
        const auto brush = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        const COLORREF surface = bookmarkBar ? palette.surface : tabs_.Colors().surface;
        SetDCBrushColor(dc, surface);
        FillRect(dc, &paint.rcPaint, brush);
        if (!bookmarkBar) tabs_.DrawActiveTabCorners(dc);
        if (bookmarkBar && !(appearance_ & HideBookmarks)) {
            RECT divider{};
            GetClientRect(window, &divider);
            const int inset = Scale((appearance_ & CompactFavorites) ? 6 : 10);
            if (bookmarkDividerX_ > 0 && divider.bottom - buttonsTop_ > 2 * inset) {
                const RECT separator{bookmarkDividerX_, buttonsTop_ + inset,
                    bookmarkDividerX_ + Scale(1), divider.bottom - inset};
                SetDCBrushColor(dc, palette.highContrast ? palette.text :
                    dark_ ? RGB(78, 84, 96) : RGB(187, 197, 210));
                FillRect(dc, &separator, brush);
            }
            divider.top = divider.bottom - Scale(1);
            SetDCBrushColor(dc, palette.divider);
            FillRect(dc, &divider, brush);
        }
        EndPaint(window, &paint);
    }

    static LRESULT CALLBACK TabsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR context)
    {
        const ComPtr<FavoritesBar> bar(reinterpret_cast<FavoritesBar*>(context));
        if (message == WM_NCDESTROY) {
            if (bar->tabHost_ == window) {
                bar->tabHost_ = nullptr;
                bar->tabSettings_ = nullptr;
            }
            RemoveWindowSubclass(window, TabsProc, 1);
        }
        try {
            if (message == WM_ERASEBKGND) return 1;
            if (message == WM_PAINT) {
                bar->PaintSurface(window);
                return 0;
            }
            if (message == WM_DRAWITEM && lParam) {
                const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                if (item.hwndItem == bar->tabSettings_) {
                    bar->PaintSettings(item);
                    return TRUE;
                }
                if (bar->tabs_.Draw(item)) return TRUE;
            }
            if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED && lParam) {
                const HWND control = reinterpret_cast<HWND>(lParam);
                if (control == bar->tabSettings_) {
                    bar->OpenSettings();
                    return 0;
                }
                if (bar->tabs_.Command(control)) return 0;
            }
            if (message == WM_CONTEXTMENU && bar->tabs_.ContextMenu(reinterpret_cast<HWND>(wParam), lParam)) return 0;
            if (message == WM_NOTIFY && lParam && bar->tabs_.Notify(*reinterpret_cast<NMHDR*>(lParam))) return 0;
        } catch (...) {
            return 0;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK BarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR context)
    {
        const ComPtr<FavoritesBar> bar(reinterpret_cast<FavoritesBar*>(context));
        if (message == WM_NCDESTROY) {
            bar->detaching_ = true;
            KillTimer(window, RefreshTimer);
            bar->StopWatchingViewCreation();
            bar->FinishBookmarkDrag(false);
            if (GetPropW(bar->root_, BarProperty) == window) RemovePropW(bar->root_, BarProperty);
            RemoveWindowSubclass(bar->root_, FrameProc, 1);
            bar->SetNativeCaptionHidden(false);
            bar->addressStar_.Close();
            bar->quickAccess_.Restore();
            bar->bar_ = nullptr;
            bar->RestoreTopBands();
            bar->DetachView();
            bar->DetachPendingView();
            RemoveWindowSubclass(window, BarProc, 1);
            const auto result = DefSubclassProc(window, message, wParam, lParam);
            bar->Release();
            return result;
        }
        try {
            static const UINT detach = RegisterWindowMessageW(DetachMessageName);
            if (detach && message == detach && !wParam && !lParam) return bar->Detach();
            if (message == WM_ERASEBKGND) return 1;
            if (message == WM_PAINT) {
                bar->PaintSurface(window, true);
                return 0;
            }
            if (message == WM_DRAWITEM && lParam) {
                const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                if (item.CtlType == ODT_BUTTON) {
                    bar->PaintButton(item);
                    return TRUE;
                }
            }
            if ((message == WM_TIMER && wParam == RefreshTimer) || message == RefreshViewLayout) {
                if (message == RefreshViewLayout) bar->layoutQueued_ = false;
                bar->Refresh();
                return 0;
            }
            if (message == WM_SYSCOLORCHANGE) {
                InvalidateRect(window, nullptr, FALSE);
                InvalidateRect(bar->transfer_, nullptr, FALSE);
                for (const HWND button : bar->buttons_) InvalidateRect(button, nullptr, FALSE);
            }
            if (message >= 0xc000 && message == AppearanceChangedMessage()) {
                bar->ApplyAppearance(ReadAppearance());
                bar->ApplyLanguage();
                bar->RefreshWindowsColors();
                bar->Refresh();
                return 0;
            }
            if (message >= 0xc000 && message == BookmarksChangedMessage()) {
                bar->ReloadBookmarks();
                return 0;
            }
            if (message == OpenHiddenSettings && !bar->useTabs_ && (bar->appearance_ & HideBookmarks)) {
                bar->OpenSettings();
                return 1;
            }
            if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) {
                bar->OnCommand(LOWORD(wParam));
                return 0;
            }
            if (message == WM_NCHITTEST && bar->OnBookmarkStrip({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)})) {
                return HTCLIENT;
            }
            if (message == WM_RBUTTONUP) {
                POINT pointer{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(window, &pointer);
                if (bar->OnBookmarkStrip(pointer)) {
                    bar->ShowBookmarkBarMenu(pointer);
                    return 0;
                }
            }
            if (message == WM_MENURBUTTONUP && bar->BookmarkPopupRightClick(
                    reinterpret_cast<HMENU>(lParam), static_cast<UINT>(wParam))) return 0;
            if (message == WM_CONTEXTMENU) {
                const HWND source = reinterpret_cast<HWND>(wParam);
                if (bar->OnContextMenu(source, lParam)) return 0;
            }
        } catch (...) {
            // A Shell extension must never propagate C++ exceptions into Explorer.
            return 0;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    struct ViewSlot {
        RECT native{};
        RECT reserved{};
        bool applied = false;
    };

    static LRESULT CALLBACK ViewCreationProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && lParam) {
            const auto& message = *reinterpret_cast<const CWPSTRUCT*>(lParam);
            if (message.message == WM_CREATE && ClassIs(message.hwnd, L"SHELLDLL_DefView")) {
                const HWND root = GetAncestor(message.hwnd, GA_ROOT);
                const HWND bar = static_cast<HWND>(GetPropW(root, BarProperty));
                DWORD_PTR context = 0;
                if (bar && GetWindowSubclass(bar, BarProc, 1, &context)) {
                    const ComPtr<FavoritesBar> owner(reinterpret_cast<FavoritesBar*>(context));
                    owner->WatchNewView(message.hwnd);
                }
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    void WatchViewCreation()
    {
        if (!viewCreationHook)
            viewCreationHook = SetWindowsHookExW(WH_CALLWNDPROC, ViewCreationProc, nullptr, GetCurrentThreadId());
        if (viewCreationHook) {
            ++viewCreationUsers;
            watchingViewCreation_ = true;
        }
    }

    void StopWatchingViewCreation()
    {
        if (std::exchange(watchingViewCreation_, false) && --viewCreationUsers == 0)
            UnhookWindowsHookEx(std::exchange(viewCreationHook, nullptr));
    }

    static LRESULT CALLBACK ViewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR context)
    {
        if (message != WM_NCDESTROY && message != WM_WINDOWPOSCHANGING && message != WM_WINDOWPOSCHANGED)
            return DefSubclassProc(window, message, wParam, lParam);
        const ComPtr<FavoritesBar> bar(reinterpret_cast<FavoritesBar*>(context));
        if (message == WM_NCDESTROY) {
            if (bar->view_ == window) {
                bar->view_ = nullptr;
                bar->viewSlot_ = {};
                bar->tabs_.SetBrowser(nullptr);
            }
            if (bar->pendingView_ == window) {
                bar->pendingView_ = nullptr;
                bar->pendingSlot_ = {};
            }
            RemoveWindowSubclass(window, ViewProc, 1);
        }
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (bar->useTabs_ && !bar->detaching_) {
            if (message == WM_WINDOWPOSCHANGING && lParam)
                bar->ReserveBeforePlacement(window, *reinterpret_cast<WINDOWPOS*>(lParam));
            if (message == WM_WINDOWPOSCHANGED && lParam) {
                const auto flags = reinterpret_cast<const WINDOWPOS*>(lParam)->flags;
                if ((flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) ||
                    (flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE))
                    bar->QueueLayout();
            }
        }
        return result;
    }

    void WatchNewView(HWND window)
    {
        if (!useTabs_ || !bar_ || detaching_ || !reservedHeight_ || window == view_ || window == pendingView_ ||
            !ClassIs(window, L"SHELLDLL_DefView") || GetAncestor(window, GA_ROOT) != root_ ||
            GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return;
        DetachPendingView();
        if (SetWindowSubclass(window, ViewProc, 1, reinterpret_cast<DWORD_PTR>(this))) pendingView_ = window;
    }

    void QueueLayout()
    {
        if (bar_ && !layoutQueued_) layoutQueued_ = PostMessageW(bar_, RefreshViewLayout, 0, 0) != FALSE;
    }

    void ReserveBeforePlacement(HWND window, WINDOWPOS& position)
    {
        if (!reservedHeight_ || (position.flags & SWP_HIDEWINDOW)) return;
        auto* slot = window == view_ ? &viewSlot_ : (window == pendingView_ ? &pendingSlot_ : nullptr);
        if (!slot) return;
        const bool moves = !(position.flags & SWP_NOMOVE);
        const bool sizes = !(position.flags & SWP_NOSIZE);
        if (!moves && !sizes && slot->applied) return;

        const RECT current = RectIn(window, GetParent(window));
        const LONG x = moves ? position.x : current.left;
        const LONG y = moves ? position.y : current.top;
        const LONG width = sizes ? position.cx : current.right - current.left;
        const LONG viewHeight = sizes ? position.cy : current.bottom - current.top;
        // Keep the Explorer slot separate from the already-inset view to avoid applying the row twice.
        const LONG top = slot->applied && y == slot->reserved.top ? slot->native.top : y;
        const LONG bottom = sizes ? y + viewHeight :
            top + (slot->applied ? slot->native.bottom - slot->native.top : viewHeight);
        const RECT native{x, top, x + width, bottom};
        if (!CanReserve(native, reservedHeight_)) return;

        RECT reserved = native;
        reserved.top += reservedHeight_;
        if (reserved.top != y) {
            position.x = x;
            position.y = reserved.top;
            position.flags &= ~SWP_NOMOVE;
        }
        if (reserved.bottom - reserved.top != viewHeight) {
            position.cx = width;
            position.cy = reserved.bottom - reserved.top;
            position.flags &= ~SWP_NOSIZE;
        }
        *slot = {native, reserved, true};
    }

    void DetachView()
    {
        watchedShellView_.Reset();
        const HWND view = std::exchange(view_, nullptr);
        if (!view) return;
        tabs_.SetBrowser(nullptr);
        RemoveWindowSubclass(view, ViewProc, 1);
        RestoreViewSize(view, viewSlot_);
    }

    void DetachPendingView()
    {
        const HWND view = std::exchange(pendingView_, nullptr);
        if (!view) return;
        RemoveWindowSubclass(view, ViewProc, 1);
        RestoreViewSize(view, pendingSlot_);
    }

    static void RestoreViewSize(HWND view, ViewSlot& slot)
    {
        const auto previous = std::exchange(slot, {});
        if (!previous.applied) return;
        const RECT current = RectIn(view, GetParent(view));
        if (EqualRect(&current, &previous.reserved)) MoveTo(view, previous.native);
    }

    int Scale(int dip) const { return MulDiv(dip, dpi_, 96); }

    void CheckTransferFolder()
    {
        const ULONGLONG now = GetTickCount64();
        if (now < nextTransferFolderCheck_) return;
        nextTransferFolderCheck_ = now + 500;
        if (!watchedShellView_) watchedShellView_ = FindExplorerView(root_, view_).shellView;
        FolderId current;
        if (!watchedShellView_ || FAILED(ReadCurrentFolder(*watchedShellView_.Get(), current))) return;
        if (watchedFolder_ && ILIsEqual(watchedFolder_.get(), current.get())) return;
        watchedFolder_ = std::move(current);
        OpenTransfer(true);
    }

    struct NativeBand {
        HWND window = nullptr;
        LONG placedTop = 0;
        int inset = 0;
    };

    static LRESULT CALLBACK NativeBandProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR index, DWORD_PTR context)
    {
        if (message != WM_NCDESTROY && message != WM_WINDOWPOSCHANGING && message != WM_WINDOWPOSCHANGED)
            return DefSubclassProc(window, message, wParam, lParam);
        const ComPtr<FavoritesBar> bar(reinterpret_cast<FavoritesBar*>(context));
        auto& band = bar->topBands_[index];
        if (message == WM_NCDESTROY) {
            band = {};
            RemoveWindowSubclass(window, NativeBandProc, index);
            bar->QueueLayout();
        }
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (!bar->topBandsHeight_ || bar->movingTopBands_ || bar->detaching_ || band.window != window) return result;
        if (message == WM_WINDOWPOSCHANGING && lParam) {
            auto& position = *reinterpret_cast<WINDOWPOS*>(lParam);
            if (!(position.flags & (SWP_NOMOVE | SWP_HIDEWINDOW))) {
                if (index == 0 && position.y != band.placedTop) bar->nativeRibbonTop_ = position.y;
                if (const auto top = bar->NativeBandTop(index)) {
                    position.y = *top + bar->topBandsHeight_;
                    band.placedTop = position.y;
                    band.inset = bar->topBandsHeight_;
                }
                if (index == 2 && !(position.flags & SWP_NOSIZE)) {
                    RECT client{};
                    GetClientRect(bar->root_, &client);
                    position.cy = std::max<LONG>(0, client.bottom + bar->nativeBodyBottomGap_ - position.y);
                }
            }
        }
        if (message == WM_WINDOWPOSCHANGED) bar->QueueLayout();
        return result;
    }

    std::optional<LONG> NativeBandTop(size_t index) const
    {
        LONG top = nativeRibbonTop_;
        for (size_t previous = 0; previous < index; ++previous) {
            if (!topBands_[previous].window) return std::nullopt;
            const RECT rect = RectIn(topBands_[previous].window, root_);
            top += rect.bottom - rect.top;
        }
        return top;
    }

    bool FindTopBands()
    {
        HWND ribbon = nullptr;
        while ((ribbon = FindWindowExW(root_, ribbon, L"UIRibbonCommandBarDock", nullptr))) {
            const RECT rect = RectIn(ribbon, root_);
            // Explorer also creates collapsed docks along the other sides of the window.
            if ((GetWindowLongPtrW(ribbon, GWL_STYLE) & WS_VISIBLE) && !IsRectEmpty(&rect)) break;
        }
        HWND address = nullptr;
        while ((address = FindWindowExW(root_, address, L"WorkerW", nullptr)) &&
            (!(GetWindowLongPtrW(address, GWL_STYLE) & WS_VISIBLE) ||
                !FindWindowExW(address, nullptr, L"ReBarWindow32", nullptr))) {}
        const HWND body = FindWindowExW(root_, nullptr, L"ShellTabWindowClass", nullptr);
        const std::array windows{ribbon, address, body};
        if (windows[0] && windows[1] && windows[2] && windows[0] == topBands_[0].window &&
            windows[1] == topBands_[1].window && windows[2] == topBands_[2].window) return true;
        if (!RestoreTopBands() || !ribbon || !address || !body) return false;

        const RECT ribbonRect = RectIn(ribbon, root_);
        const RECT addressRect = RectIn(address, root_);
        const RECT bodyRect = RectIn(body, root_);
        if (IsRectEmpty(&ribbonRect) || ribbonRect.bottom != addressRect.top ||
            addressRect.bottom != bodyRect.top || IsRectEmpty(&bodyRect)) return false;
        RECT client{};
        if (!GetClientRect(root_, &client)) return false;
        nativeRibbonTop_ = ribbonRect.top;
        nativeBodyBottomGap_ = bodyRect.bottom - client.bottom;
        for (size_t index = 0; index < windows.size(); ++index) {
            if (!SetWindowSubclass(windows[index], NativeBandProc, index, reinterpret_cast<DWORD_PTR>(this))) {
                RestoreTopBands();
                return false;
            }
            topBands_[index].window = windows[index];
        }
        return true;
    }

    bool PositionTopBands(int height, int tabsHeight, RECT& bounds)
    {
        if (!FindTopBands()) return false;
        RECT client{};
        if (!GetClientRect(root_, &client)) return false;
        const RECT ribbon = RectIn(topBands_[0].window, root_);
        bounds = {ribbon.left, nativeRibbonTop_, ribbon.right, nativeRibbonTop_ + height};
        const auto bodyTop = NativeBandTop(2);
        const LONG bodyBottom = client.bottom + nativeBodyBottomGap_;
        if (!bodyTop || bounds.right - bounds.left < Scale(160) || bodyBottom - *bodyTop < height + Scale(52) ||
            !LayoutTabStrip(bounds.right - bounds.left, tabsHeight)) return false;

        LONG top = bounds.bottom;
        std::array<RECT, 3> desired{};
        for (size_t index = 0; index < topBands_.size(); ++index) {
            const RECT current = RectIn(topBands_[index].window, root_);
            const LONG bottom = index == 2 ? bodyBottom : top + current.bottom - current.top;
            desired[index] = {current.left, top, current.right, bottom};
            top = bottom;
        }
        movingTopBands_ = true;
        // Open space from the bottom upward before showing the new bars.
        for (size_t count = topBands_.size(); count > 0; --count) {
            const size_t index = count - 1;
            auto& band = topBands_[index];
            const RECT current = RectIn(band.window, root_);
            if (!EqualRect(&current, &desired[index]) &&
                !MoveTo(band.window, desired[index], nullptr, LayoutFlags | SWP_NOSENDCHANGING)) {
                movingTopBands_ = false;
                return false;
            }
            band.placedTop = desired[index].top;
            band.inset = height;
        }
        movingTopBands_ = false;
        topBandsHeight_ = height;
        return true;
    }

    bool RestoreTopBands()
    {
        movingTopBands_ = true;
        bool restored = true;
        for (size_t index = 0; index < topBands_.size(); ++index) {
            auto& band = topBands_[index];
            if (!band.window) continue;
            RECT rect = RectIn(band.window, root_);
            if (band.inset && rect.top == band.placedTop) {
                rect.top -= band.inset;
                if (index != 2) rect.bottom -= band.inset;
                restored = MoveTo(band.window, rect, nullptr, LayoutFlags | SWP_NOSENDCHANGING) && restored;
            }
            RemoveWindowSubclass(band.window, NativeBandProc, index);
            band = {};
        }
        movingTopBands_ = false;
        topBandsHeight_ = 0;
        return restored;
    }

    void Refresh()
    {
        if (detaching_ || refreshing_) return;
        refreshing_ = true;
        struct ResetRefreshing {
            bool& flag;
            ~ResetRefreshing() { flag = false; }
        } resetRefreshing{refreshing_};
        if (GetAncestor(root_, GA_ROOT) != root_) {
            Close();
            return;
        }
        const HWND active = ActiveView(root_);
        if (active != view_) {
            DetachView();
            if (active && SetWindowSubclass(active, ViewProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
                view_ = active;
                if (active == pendingView_) {
                    pendingView_ = nullptr;
                    viewSlot_ = std::exchange(pendingSlot_, {});
                }
            }
        }
        if (!view_ || !bar_) {
            if ((pendingView_ || topBandsHeight_) && bar_ && !IsIconic(root_)) return;
            FinishBookmarkDrag(false);
            addressStar_.Close();
            if (bar_ && IsWindowVisible(bar_)) ShowWindow(bar_, SW_HIDE);
            SetNativeCaptionHidden(false);
            return;
        }
        CheckTransferFolder();

        const UINT dpi = GetDpiForWindow(root_);
        if (dpi && dpi != dpi_) {
            dpi_ = dpi;
            buttonsDirty_ = true;
        }
        UpdateBarFont();
        const int favoritesHeight = (appearance_ & HideBookmarks) ? 0 :
            ((appearance_ & CompactFavorites) ? 26 : 42);
        int inlineTabsHeight = useTabs_ ? Scale((appearance_ & CompactTabs) ? 27 : 37) : 0;
        int height = inlineTabsHeight + Scale(favoritesHeight);
        RECT bounds{};
        if (!bar_ || !view_) return;
        if (useTabs_) SetNativeCaptionHidden((appearance_ & HideFolderTitle) != 0);
        const bool topBands = useTabs_ && PositionTopBands(height, inlineTabsHeight, bounds);
        if (topBands) {
            reservedHeight_ = 0;
            RestoreViewSize(view_, viewSlot_);
            DetachPendingView();
        } else {
            if (useTabs_) RestoreTopBands();
            const RECT view = RectIn(view_, GetParent(view_));
            if (inlineTabsHeight && !LayoutTabStrip(view.right - view.left, inlineTabsHeight)) {
                height -= inlineTabsHeight;
                inlineTabsHeight = 0;
                if (tabHost_) ShowWindow(tabHost_, SW_HIDE);
            }
            if (!ReserveViewSlot(height, bounds)) return;
        }
        addressStar_.Refresh(root_, bar_, AddCommand, dark_, bookmarkGraphics_ != 0);
        const int width = bounds.right - bounds.left;
        const RECT old = RectIn(bar_, root_);
        // Native tab activation can raise a new child even with identical bounds.
        if (!height) {
            if (IsWindowVisible(bar_)) ShowWindow(bar_, SW_HIDE);
        } else if (!IsWindowVisible(bar_) || !EqualRect(&old, &bounds) || GetTopWindow(root_) != bar_) {
            MoveTo(bar_, bounds, HWND_TOP, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        if (buttonsDirty_ || width != buttonsWidth_ || inlineTabsHeight != buttonsTop_) {
            if (favoritesHeight) {
                LayoutButtons(width, inlineTabsHeight, height);
            } else {
                FinishBookmarkDrag(false);
                ShowWindow(more_, SW_HIDE);
                ShowWindow(transfer_, SW_HIDE);
                ShowWindow(settings_, SW_HIDE);
                ShowWindow(addBookmark_, SW_HIDE);
                bookmarkDividerX_ = 0;
                for (const HWND button : buttons_) ShowWindow(button, SW_HIDE);
            }
            buttonsWidth_ = width;
            buttonsTop_ = inlineTabsHeight;
            buttonsDirty_ = false;
            InvalidateRect(bar_, nullptr, FALSE);
        }
        if (useTabs_) {
            const ULONGLONG now = GetTickCount64();
            const bool updateQuickAccess = quickAccessAttempts_ < QuickAccessMaxAttempts && now >= nextQuickAccessAttempt_;
            if (!tabs_.HasBrowser() || updateQuickAccess) {
                if (updateQuickAccess) {
                    ++quickAccessAttempts_;
                    nextQuickAccessAttempt_ = now + 600;
                }
                const auto explorer = FindExplorerView(root_, view_);
                if (!tabs_.HasBrowser()) tabs_.SetBrowser(explorer.browser.Get());
                if (updateQuickAccess && quickAccess_.Apply(RibbonProperties(explorer), (appearance_ & ShowQuickAccessToolbar) != 0)) {
                    quickAccessAttempts_ = QuickAccessMaxAttempts;
                }
            }
            tabs_.Observe();
        }
    }

    bool LayoutTabStrip(int width, int height)
    {
        if (!tabHost_ || width <= 0 || height <= 0) return false;
        const RECT bounds{0, 0, width, height};
        const RECT current = RectIn(tabHost_, bar_);
        if (!(GetWindowLongPtrW(tabHost_, GWL_STYLE) & WS_VISIBLE) || !EqualRect(&current, &bounds)) {
            if (!MoveTo(tabHost_, bounds, nullptr, LayoutFlags | SWP_SHOWWINDOW)) return false;
        }
        if (tabsWidth_ != width || tabsHeight_ != height || tabsDpi_ != dpi_) {
            if (appearance_ & HideBookmarks) {
                const int settingsTop = std::min(Scale(TabTopPadding()), std::max(0, height - Scale(17)));
                MoveTo(tabSettings_, {Scale(3), settingsTop, Scale(SettingsInset - 2), height},
                    nullptr, LayoutFlags | SWP_SHOWWINDOW);
            } else {
                ShowWindow(tabSettings_, SW_HIDE);
            }
            tabs_.Layout(width, height, dpi_);
            tabsWidth_ = width;
            tabsHeight_ = height;
            tabsDpi_ = dpi_;
        }
        return true;
    }

    void RefreshWindowsColors()
    {
        const WinRtApartment apartment;
        if (SUCCEEDED(apartment.hr)) dark_ = UsesDarkAppColors();
        tabs_.SetColors(dark_);
        if (bar_) RedrawWindow(bar_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    bool CanReserve(const RECT& slot, int height) const
    {
        return slot.bottom - slot.top >= height * 2 && slot.right - slot.left >= Scale(160);
    }

    bool ReserveViewSlot(int height, RECT& bounds)
    {
        const HWND parent = GetParent(view_);
        const RECT current = RectIn(view_, parent);
        auto& slot = viewSlot_;
        if (!slot.applied) {
            slot.native = current;
        } else if (!EqualRect(&current, &slot.reserved)) {
            // Explorer can resize the view without moving its top edge.
            const LONG top = current.top == slot.reserved.top ? slot.native.top : current.top;
            slot.native = current;
            slot.native.top = top;
        }

        if (!CanReserve(slot.native, height)) {
            reservedHeight_ = 0;
            DetachPendingView();
            DetachView();
            ShowWindow(bar_, SW_HIDE);
            return false;
        }
        RECT adjusted = slot.native;
        adjusted.top += height;
        const UINT flags = useTabs_ ? LayoutFlags | SWP_NOSENDCHANGING : LayoutFlags;
        const HWND view = view_;
        if (!EqualRect(&current, &adjusted) && !MoveTo(view, adjusted, nullptr, flags)) return false;
        if (!bar_ || view_ != view) return false;
        slot.reserved = adjusted;
        slot.applied = true;
        reservedHeight_ = height;

        bounds = {slot.native.left, slot.native.top, slot.native.right, adjusted.top};
        MapWindowPoints(parent, root_, reinterpret_cast<POINT*>(&bounds), 2);
        return true;
    }

    HWND CreateButton(WORD id, const wchar_t* label) const
    {
        const HWND button = CreateWindowExW(0, L"Button", label, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, bar_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), nullptr, nullptr);
        if (button) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(BarFont()), FALSE);
        return button;
    }

    struct BookmarkDrag {
        HWND source;
        POINT start;
        bool dragging = false;
        HWND target = nullptr;
        BookmarkDrop drop = BookmarkDrop::Before;
    };

    static LRESULT CALLBACK BookmarkButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR context)
    {
        const ComPtr<FavoritesBar> bar(reinterpret_cast<FavoritesBar*>(context));
        const bool tracking = bar->bookmarkDrag_ && bar->bookmarkDrag_->source == window;
        try {
            if (message == WM_MOUSEMOVE && !(wParam & MK_LBUTTON) && bar->hoveredBookmark_ != window) {
                TRACKMOUSEEVENT leave{sizeof(leave), TME_LEAVE, window, 0};
                if (TrackMouseEvent(&leave)) bar->SetHoveredBookmark(window);
            } else if ((message == WM_MOUSELEAVE || message == WM_NCDESTROY) && bar->hoveredBookmark_ == window) {
                bar->SetHoveredBookmark(nullptr);
            }
            if (message == WM_LBUTTONDOWN) {
                POINT pointer{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(window, &pointer);
                bar->bookmarkDrag_ = BookmarkDrag{window, pointer};
            } else if (tracking) {
                if (message == WM_MOUSEMOVE && (wParam & MK_LBUTTON) && GetCapture() == window &&
                    bar->UpdateBookmarkDrag(window, lParam)) return 0;
                if (message == WM_LBUTTONUP) {
                    if (bar->bookmarkDrag_->dragging) {
                        bar->UpdateBookmarkDrag(window, lParam);
                        bar->FinishBookmarkDrag(true);
                        return 0;
                    }
                    bar->bookmarkDrag_.reset();
                }
                if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
                    bar->FinishBookmarkDrag(false);
                    return 0;
                }
                if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED || message == WM_NCDESTROY) {
                    bar->FinishBookmarkDrag(false);
                }
            }
        } catch (...) {
            bar->FinishBookmarkDrag(false);
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, BookmarkButtonProc, subclassId);
        return DefSubclassProc(window, message, wParam, lParam);
    }

    void SetHoveredBookmark(HWND window)
    {
        const HWND previous = std::exchange(hoveredBookmark_, window);
        if (previous) InvalidateRect(previous, nullptr, FALSE);
        if (window) InvalidateRect(window, nullptr, FALSE);
    }

    bool OnBookmarkStrip(POINT pointer) const
    {
        if (!bar_ || !IsWindowVisible(bar_) || (appearance_ & HideBookmarks)) return false;
        RECT bounds{};
        if (!GetWindowRect(bar_, &bounds)) return false;
        bounds.top += buttonsTop_;
        bounds.left += bookmarkDividerX_ + Scale(1);
        return PtInRect(&bounds, pointer) != FALSE;
    }

    const Favorite* BookmarkForButton(HWND button) const
    {
        const int id = GetDlgCtrlID(button);
        if (id < FirstFavorite) return nullptr;
        const auto index = static_cast<size_t>(id - FirstFavorite);
        return index < buttons_.size() && index < rootItems_.size() && buttons_[index] == button ?
            &items_[rootItems_[index]] : nullptr;
    }

    bool UpdateBookmarkDrag(HWND window, LPARAM position)
    {
        POINT pointer{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        ClientToScreen(window, &pointer);
        auto& drag = *bookmarkDrag_;
        if (!drag.dragging) {
            if (std::abs(pointer.x - drag.start.x) < std::max(1, GetSystemMetricsForDpi(SM_CXDRAG, dpi_)) &&
                std::abs(pointer.y - drag.start.y) < std::max(1, GetSystemMetricsForDpi(SM_CYDRAG, dpi_))) return false;
            drag.dragging = true;
            SetHoveredBookmark(nullptr);
            SendMessageW(window, BM_SETSTATE, FALSE, 0);
        }
        HWND target = nullptr;
        BookmarkDrop drop = BookmarkDrop::Before;
        if (OnBookmarkStrip(pointer)) {
            for (const HWND button : buttons_) {
                RECT bounds{};
                if (!IsWindowVisible(button) || !GetWindowRect(button, &bounds)) continue;
                target = button;
                if (pointer.x < bounds.right) {
                    const Favorite* item = BookmarkForButton(button);
                    drop = pointer.x < bounds.left ? BookmarkDrop::Before :
                        BookmarkDropZone(item && item->IsFolder(), pointer.x - bounds.left, bounds.right - bounds.left);
                    break;
                }
                drop = BookmarkDrop::After;
            }
            RECT overflow{};
            if (IsWindowVisible(more_) && GetWindowRect(more_, &overflow) && PtInRect(&overflow, pointer)) target = nullptr;
        }
        if (target == window) target = nullptr;
        if (drag.target != target || drag.drop != drop) {
            if (drag.target) InvalidateRect(drag.target, nullptr, FALSE);
            drag.target = target;
            drag.drop = drop;
            if (drag.target) InvalidateRect(drag.target, nullptr, FALSE);
        }
        return true;
    }

    void FinishBookmarkDrag(bool commit)
    {
        const auto drag = std::exchange(bookmarkDrag_, std::nullopt);
        if (!drag) return;
        if (drag->target) InvalidateRect(drag->target, nullptr, FALSE);
        if (GetCapture() == drag->source) ReleaseCapture();
        if (!commit || !drag->dragging || !drag->target || !bar_) return;
        const auto* source = BookmarkForButton(drag->source);
        const auto* target = BookmarkForButton(drag->target);
        if (!source || !target) return;
        const auto anchorId = target->id;
        Favorite item = *source;
        if (drag->drop == BookmarkDrop::Into) {
            item.parentId = anchorId;
            SaveFavorite(FavoriteChange::Move, item);
        } else {
            item.parentId = target->parentId;
            SaveFavorite(FavoriteChange::Move, item, {anchorId, drag->drop == BookmarkDrop::After});
        }
    }

    void PaintButton(const DRAWITEMSTRUCT& button) const
    {
        const auto palette = BookmarkColors(dark_, useTabs_);
        const Favorite* bookmark = BookmarkForButton(button.hwndItem);
        const bool folder = bookmark && bookmark->IsFolder();
        const bool pressed = (button.itemState & ODS_SELECTED) != 0;
        const bool hovered = button.hwndItem == hoveredBookmark_ || (button.itemState & ODS_HOTLIGHT);
        const bool focus = (button.itemState & ODS_FOCUS) && !(button.itemState & ODS_NOFOCUSRECT);
        const auto brush = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        SetDCBrushColor(button.hDC, palette.surface);
        FillRect(button.hDC, &button.rcItem, brush);
        const bool transfer = button.hwndItem == transfer_;
        const bool settings = button.hwndItem == settings_;
        if (transfer || settings || button.hwndItem == addBookmark_) {
            RECT bounds = button.rcItem;
            const auto glyph = transfer && !palette.highContrast ? RGB(46, 107, 240) : palette.text;
            const auto icon = transfer ? ExplorerIcon::LanTransfer : settings ? ExplorerIcon::Settings : ExplorerIcon::Star;
            if (!bookmarkGraphics_ || !DrawExplorerIcon(button.hDC, bounds, Scale(transfer ? 20 : 18), icon, glyph)) {
                const auto previousFont = SelectObject(button.hDC, BarFont());
                SetBkMode(button.hDC, TRANSPARENT);
                SetTextColor(button.hDC, glyph);
                const auto label = transfer ? ExplorerText(L"互传") : settings ? L"\x2699" : L"\x2606";
                DrawTextW(button.hDC, label, -1, &bounds, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                if (previousFont) SelectObject(button.hDC, previousFont);
            }
            if (focus) {
                InflateRect(&bounds, -Scale(2), -Scale(2));
                DrawFocusRect(button.hDC, &bounds);
            }
            return;
        }
        SetDCBrushColor(button.hDC, pressed ? palette.pressed : hovered ? palette.hover :
            folder ? palette.folderChip : palette.chip);
        if (palette.highContrast) {
            FillRect(button.hDC, &button.rcItem, brush);
            SetDCBrushColor(button.hDC, (pressed || hovered) ? GetSysColor(COLOR_HIGHLIGHTTEXT) : palette.text);
            FrameRect(button.hDC, &button.rcItem, brush);
        } else {
            const HGDIOBJ previousBrush = SelectObject(button.hDC, brush);
            const HGDIOBJ previousPen = SelectObject(button.hDC, GetStockObject(NULL_PEN));
            const int corner = Scale(24);
            const auto& bounds = button.rcItem;
            RoundRect(button.hDC, bounds.left, bounds.top, bounds.right, bounds.bottom, corner, corner);
            SelectObject(button.hDC, previousPen);
            SelectObject(button.hDC, previousBrush);
        }

        wchar_t utilityLabel[24]{};
        if (!bookmark) GetWindowTextW(button.hwndItem, utilityLabel, ARRAYSIZE(utilityLabel));
        const wchar_t* label = bookmark ? bookmark->name.c_str() : utilityLabel;
        const COLORREF text = palette.highContrast && (pressed || hovered) ? GetSysColor(COLOR_HIGHLIGHTTEXT) : palette.text;
        RECT area = button.rcItem;
        InflateRect(&area, -Scale(bookmark ? BookmarkInset : 4), 0);
        if (folder) {
            const int size = Scale(BookmarkIconSize);
            const int iconTop = (area.top + area.bottom - size) / 2;
            const COLORREF icon = palette.highContrast ? text : palette.folderIcon;
            if (bookmarkGraphics_) {
                DrawBookmarkFolder(button.hDC, area.left, iconTop, size, icon);
            } else {
                SHSTOCKICONINFO stock{sizeof(stock)};
                if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDEROPEN, SHGSI_ICON | SHGSI_SMALLICON, &stock))) {
                    DrawIconEx(button.hDC, area.left, iconTop, stock.hIcon, size, size, 0, nullptr, DI_NORMAL);
                    DestroyIcon(stock.hIcon);
                }
            }
            area.left += Scale(BookmarkIconSize + BookmarkIconGap);
            const int centerX = area.right - Scale(4);
            const int centerY = (area.top + area.bottom) / 2;
            const int radius = Scale(3);
            const HGDIOBJ previousPen = SelectObject(button.hDC, GetStockObject(DC_PEN));
            SetDCPenColor(button.hDC, palette.highContrast ? text : palette.arrow);
            MoveToEx(button.hDC, centerX - radius, centerY - Scale(1), nullptr);
            LineTo(button.hDC, centerX, centerY + Scale(2));
            LineTo(button.hDC, centerX + radius + 1, centerY - Scale(2));
            SelectObject(button.hDC, previousPen);
            area.right -= Scale(BookmarkChevronSpace);
        }
        SetBkMode(button.hDC, TRANSPARENT);
        SetTextColor(button.hDC, text);
        const auto previous = SelectObject(button.hDC, BarFont());
        const UINT alignment = bookmark ? DT_LEFT : DT_CENTER;
        const UINT textFormat = DT_SINGLELINE | DT_VCENTER | alignment | DT_END_ELLIPSIS | DT_NOPREFIX;
        DrawTextW(button.hDC, label, -1, &area, textFormat);
        if (previous) SelectObject(button.hDC, previous);
        if (focus) {
            RECT bounds = button.rcItem;
            InflateRect(&bounds, -Scale(3), -Scale(3));
            DrawFocusRect(button.hDC, &bounds);
        }
        if (bookmarkDrag_ && bookmarkDrag_->target == button.hwndItem) {
            RECT mark = button.rcItem;
            SetDCBrushColor(button.hDC, GetSysColor(COLOR_HIGHLIGHT));
            if (bookmarkDrag_->drop == BookmarkDrop::Into) {
                FrameRect(button.hDC, &mark, brush);
                InflateRect(&mark, -1, -1);
                FrameRect(button.hDC, &mark, brush);
            } else {
                const int thickness = Scale(2);
                if (bookmarkDrag_->drop == BookmarkDrop::Before) mark.right = mark.left + thickness;
                else mark.left = mark.right - thickness;
                FillRect(button.hDC, &mark, brush);
            }
        }
    }

    HFONT BarFont() const
    {
        return barFont_ ? barFont_ : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    void UpdateBarFont()
    {
        if (barFontDpi_ == dpi_) return;
        const HFONT replacement = CreateFontW(-Scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        if (!replacement) return;

        const auto previous = std::exchange(barFont_, replacement);
        barFontDpi_ = dpi_;
        const auto parameter = reinterpret_cast<WPARAM>(replacement);
        tabs_.SetFont(replacement);
        for (const HWND button : buttons_) SendMessageW(button, WM_SETFONT, parameter, FALSE);
        for (const HWND button : {more_, transfer_, settings_, tabSettings_, addBookmark_}) {
            if (button) SendMessageW(button, WM_SETFONT, parameter, FALSE);
        }
        if (previous) DeleteObject(previous);
        tabsHeight_ = 0;
        buttonsDirty_ = true;
    }

    void LayoutButtons(int width, int top, int height)
    {
        FinishBookmarkDrag(false);
        SetHoveredBookmark(nullptr);
        const int gap = Scale(6);
        const int vertical = Scale((appearance_ & CompactFavorites) ? 3 : 6);
        const int moreWidth = Scale(useTabs_ ? 28 : 62);
        const int moreLeft = width - moreWidth - gap;
        int left = gap;
        const int toolWidth = Scale(34);
        for (const HWND tool : {transfer_, settings_, addBookmark_}) {
            if (left + toolWidth + gap <= width) {
                MoveTo(tool, {left, top + vertical, left + toolWidth, height - vertical},
                    nullptr, LayoutFlags | SWP_SHOWWINDOW);
                InvalidateRect(tool, nullptr, FALSE);
                left += toolWidth + gap;
            } else {
                ShowWindow(tool, SW_HIDE);
            }
        }
        bookmarkDividerX_ = left > gap && left + gap < width ? left : 0;
        if (bookmarkDividerX_) left += Scale(8);
        const HDC dc = GetDC(bar_);
        const HGDIOBJ oldFont = dc ? SelectObject(dc, BarFont()) : nullptr;
        const auto releaseDc = [window = bar_, oldFont](HDC drawing) {
            if (oldFont) SelectObject(drawing, oldFont);
            ReleaseDC(window, drawing);
        };
        const std::unique_ptr<std::remove_pointer_t<HDC>, decltype(releaseDc)> measurement(dc, releaseDc);
        rootItems_.clear();
        for (size_t index = 0; index < items_.size(); ++index) {
            if (items_[index].parentId.empty()) rootItems_.push_back(index);
        }
        size_t visible = 0;
        for (const size_t index : rootItems_) {
            const auto& item = items_[index];
            SIZE label{Scale(static_cast<int>(std::min(item.name.size(), size_t(25))) * 9), 0};
            if (measurement) GetTextExtentPoint32W(measurement.get(), item.name.c_str(), static_cast<int>(item.name.size()), &label);
            int padding = 2 * Scale(BookmarkInset) + Scale(2);
            if (item.IsFolder()) padding += Scale(BookmarkIconSize + BookmarkIconGap) + Scale(BookmarkChevronSpace);
            const int itemWidth = std::clamp(static_cast<int>(label.cx) + padding, Scale(item.IsFolder() ? 84 : 44), Scale(225));
            const int limit = visible + 1 == rootItems_.size() ? width - gap : moreLeft - gap;
            if (left + itemWidth > limit || visible >= 1000) break;
            if (visible == buttons_.size()) {
                const HWND button = CreateButton(static_cast<WORD>(FirstFavorite + visible), L"");
                if (!button) break;
                if (!SetWindowSubclass(button, BookmarkButtonProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
                    DestroyWindow(button);
                    break;
                }
                buttons_.push_back(button);
            }
            const HWND button = buttons_[visible++];
            SetWindowTextW(button, ButtonLabel(item.name).c_str());
            MoveTo(button, {left, top + vertical, left + itemWidth, height - vertical}, nullptr, LayoutFlags | SWP_SHOWWINDOW);
            InvalidateRect(button, nullptr, FALSE);
            left += itemWidth + gap;
        }
        while (buttons_.size() > visible) {
            DestroyWindow(buttons_.back());
            buttons_.pop_back();
        }
        if (visible < rootItems_.size() && moreLeft >= left) {
            MoveTo(more_, {moreLeft, top + vertical, width - gap, height - vertical}, nullptr, LayoutFlags | SWP_SHOWWINDOW);
        } else {
            ShowWindow(more_, SW_HIDE);
        }
    }

    void OpenFavorite(const std::wstring& path)
    {
        if (!view_) return;
        if (!IsBookmarkPath(path)) {
            MessageBoxW(root_, ExplorerText(L"这个收藏不是文件夹路径。"), L"LYwintabs", MB_ICONWARNING);
            return;
        }
        PIDLIST_ABSOLUTE raw = nullptr;
        SFGAOF attributes = 0;
        const HRESULT resolved = SHParseDisplayName(path.c_str(), nullptr, &raw, SFGAO_FOLDER | SFGAO_FILESYSTEM, &attributes);
        const FolderId folder(raw);
        if (FAILED(resolved) || !folder || (attributes & (SFGAO_FOLDER | SFGAO_FILESYSTEM)) !=
                (SFGAO_FOLDER | SFGAO_FILESYSTEM)) {
            if (bar_) MessageBoxW(root_, ExplorerText(L"无法访问这个书签的文件夹。"), L"LYwintabs", MB_ICONWARNING);
            return;
        }
        const auto active = bar_ && view_ ? FindExplorerView(root_, view_) : ExplorerView{};
        if (!active.browser) return;
        const HRESULT result = active.browser->BrowseObject(folder.get(), SBSP_ABSOLUTE | SBSP_SAMEBROWSER);
        if (FAILED(result) && bar_) MessageBoxW(root_, ExplorerText(L"资源管理器无法打开这个收藏。"), L"LYwintabs", MB_ICONWARNING);
    }

    struct BookmarkPopup {
        HMENU menu = nullptr;
        std::vector<std::wstring> targets;
        size_t contextToken = 0;
        UINT contextAction = 0;
        POINT contextPoint{};
    };

    void ShowBookmarksPopup(POINT point, std::wstring_view parent, size_t first = 0, UINT align = TPM_LEFTALIGN)
    {
        if (activeBookmarkPopup_ || !bar_) return;
        BookmarkMenu menu(CreatePopupMenu(), DestroyMenu);
        if (!menu) return;
        BookmarkPopup popup;
        popup.menu = menu.get();
        {
            BookmarkChildren children;
            for (const auto& item : items_) children[item.parentId].push_back(&item);
            AppendBookmarkMenu(menu.get(), children, parent, first, popup.targets);
        }
        activeBookmarkPopup_ = &popup;
        // Left clicks open bookmarks. Right clicks are handled by WM_MENURBUTTONUP.
        const UINT chosen = TrackPopupMenuEx(menu.get(), TPM_RETURNCMD | align, point.x, point.y, bar_, nullptr);
        activeBookmarkPopup_ = nullptr;
        if (!bar_) return;
        if (popup.contextAction) {
            ApplyBookmarkAction(popup.targets[popup.contextToken - 1], popup.contextAction, popup.contextPoint);
        } else if (chosen && chosen <= popup.targets.size()) {
            const auto bookmark = std::ranges::find(items_, popup.targets[chosen - 1], &Favorite::id);
            if (bookmark != items_.end() && !bookmark->IsFolder()) {
                const auto path = bookmark->path;
                OpenFavorite(path);
            }
        }
    }

    void OpenBookmarksMenu(HWND anchor, std::wstring_view parent, size_t first = 0)
    {
        RECT bounds{};
        if (!GetWindowRect(anchor, &bounds)) return;
        const bool overflow = anchor == more_;
        ShowBookmarksPopup({overflow ? bounds.right : bounds.left, bounds.bottom}, parent, first,
            overflow ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
    }

    bool BookmarkPopupRightClick(HMENU source, UINT position)
    {
        auto* popup = activeBookmarkPopup_;
        if (!popup || popup->contextAction || !source || !ContainsMenu(popup->menu, source)) return false;
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_DATA | MIIM_STATE;
        if (!GetMenuItemInfoW(source, position, TRUE, &info) || (info.fState & MFS_DISABLED) ||
            !info.dwItemData || info.dwItemData > popup->targets.size()) return true;
        const auto item = std::ranges::find(items_, popup->targets[info.dwItemData - 1], &Favorite::id);
        if (item == items_.end()) return true;
        POINT point{};
        if (!GetCursorPos(&point)) return true;
        const UINT action = PickBookmarkAction(item->IsFolder(), point, TPM_RECURSE);
        if (action && bar_) {
            popup->contextAction = action;
            popup->contextToken = info.dwItemData;
            popup->contextPoint = point;
            EndMenu();
        }
        return true;
    }

    void AddCurrentFavorite()
    {
        const auto active = FindExplorerView(root_, view_);
        Favorite item;
        HRESULT hr = active.shellView ? ReadCurrentFavorite(*active.shellView.Get(), item) : E_FAIL;
        if (FAILED(hr) && bar_) {
            MessageBoxW(root_, ExplorerText(L"当前文件夹无法收藏。"), L"LYwintabs", MB_ICONWARNING);
            return;
        }
        HMODULE dll = nullptr;
        if (SUCCEEDED(hr) && bar_ &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                BarProperty, &dll) && ShowBookmarkAdd(root_, dll, store_, std::move(item))) BookmarksEdited();
    }

    void SaveFavorite(FavoriteChange change, const Favorite& item, BookmarkPosition position = {})
    {
        HRESULT hr;
        {
            const WinRtApartment apartment;
            hr = SUCCEEDED(apartment.hr) ? store_.Update(change, item, items_, position) : apartment.hr;
        }
        if (FAILED(hr)) {
            const wchar_t* message = ExplorerText(L"收藏保存失败。");
            if (hr == HRESULT_FROM_WIN32(ERROR_DIR_NOT_EMPTY)) {
                message = ExplorerText(L"请先在书签管理器里移走或移除文件夹内的书签。");
            } else if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
                message = ExplorerText(L"目标文件夹中已有相同地址的书签。");
            } else if (change == FavoriteChange::Move) {
                message = ExplorerText(L"无法移动书签，请确认目标文件夹及其嵌套层数。");
            }
            if (bar_) MessageBoxW(root_, message, L"LYwintabs", MB_ICONWARNING);
            if (bar_ && change == FavoriteChange::Move) ReloadBookmarks();
            return;
        }
        buttonsDirty_ = true;
        Refresh();
        if (hr == S_OK) BroadcastToOtherBars(root_, BookmarksChangedMessage());
    }

    void ReloadBookmarks()
    {
        const WinRtApartment apartment;
        std::vector<Favorite> updated;
        if (FAILED(apartment.hr) || FAILED(store_.Load(updated))) return;
        items_.swap(updated);
        buttonsDirty_ = true;
        Refresh();
    }

    void BookmarksEdited()
    {
        if (bar_) ReloadBookmarks();
        BroadcastToOtherBars(root_, BookmarksChangedMessage());
    }

    void ShowBookmarkBarMenu(POINT pointer)
    {
        BookmarkMenu menu(CreatePopupMenu(), DestroyMenu);
        if (!menu) return;
        constexpr UINT AddFolderItem = 1;
        constexpr UINT ManagerItem = 2;
        constexpr UINT AddCurrentAddressItem = 3;
        AppendMenuW(menu.get(), MF_STRING, AddCurrentAddressItem, ExplorerText(L"添加当前地址到书签…"));
        AppendMenuW(menu.get(), MF_STRING, AddFolderItem, ExplorerText(L"添加文件夹…"));
        AppendMenuW(menu.get(), MF_STRING, ManagerItem, ExplorerText(L"书签管理器…"));
        const UINT command = TrackPopupMenu(menu.get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            pointer.x, pointer.y, 0, bar_, nullptr);
        if (!bar_ || !command) return;
        if (command == AddCurrentAddressItem) {
            AddCurrentFavorite();
            return;
        }
        HMODULE dll = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                BarProperty, &dll)) return;
        if (command == AddFolderItem) {
            Favorite folder;
            if (ShowBookmarkFolderName(root_, dll, folder.name) && bar_) SaveFavorite(FavoriteChange::Add, folder);
        } else if (command == ManagerItem && ShowBookmarkManager(root_, dll, store_)) {
            BookmarksEdited();
        }
    }

    UINT PickBookmarkAction(bool folder, POINT point, UINT extraFlags = 0) const
    {
        BookmarkMenu menu(CreatePopupMenu(), DestroyMenu);
        if (!menu || !bar_) return 0;
        AppendMenuW(menu.get(), MF_STRING, Open, folder ? ExplorerText(L"展开文件夹") : ExplorerText(L"打开"));
        if (useTabs_ && !folder) AppendMenuW(menu.get(), MF_STRING, OpenInTab, ExplorerText(L"在后台标签打开"));
        AppendMenuW(menu.get(), MF_STRING, Edit, ExplorerText(L"编辑…"));
        AppendMenuW(menu.get(), MF_STRING, Remove, folder ? ExplorerText(L"移除书签文件夹") : ExplorerText(L"移除书签"));
        return TrackPopupMenuEx(menu.get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | extraFlags,
            point.x, point.y, bar_, nullptr);
    }

    void ApplyBookmarkAction(std::wstring_view id, UINT selected, POINT expandAt)
    {
        if (!bar_ || !selected) return;
        const auto latest = std::ranges::find(items_, id, &Favorite::id);
        if (latest == items_.end()) return;
        auto item = *latest;
        if (selected == Open) {
            if (item.IsFolder()) ShowBookmarksPopup(expandAt, item.id);
            else OpenFavorite(item.path);
        } else if (selected == OpenInTab) {
            if (!item.IsFolder() && !tabs_.OpenFolderInBackground(item.path.c_str())) {
                MessageBoxW(root_, ExplorerText(L"无法在新标签中打开这个收藏。"), L"LYwintabs", MB_ICONWARNING);
            }
        } else if (selected == Edit) {
            const auto oldName = item.name;
            const auto oldPath = item.path;
            HMODULE dll = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    BarProperty, &dll) &&
                ShowBookmarkEdit(root_, dll, item) && bar_ && (item.name != oldName || item.path != oldPath)) {
                SaveFavorite(item.IsFolder() ? FavoriteChange::Rename : FavoriteChange::Update, item);
            }
        } else if (selected == Remove) {
            SaveFavorite(FavoriteChange::Remove, item);
        }
    }

    bool OnContextMenu(HWND button, LPARAM position)
    {
        const auto* item = BookmarkForButton(button);
        if (!item) return false;
        const std::wstring id = item->id;
        RECT bounds{};
        if (!GetWindowRect(button, &bounds)) return true;
        const POINT anchor{bounds.left, bounds.bottom};
        const POINT pointer = position == -1 ? anchor : POINT{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        const UINT action = PickBookmarkAction(item->IsFolder(), pointer);
        ApplyBookmarkAction(id, action, anchor);
        return true;
    }

    void OpenTransfer(bool background = false) const
    {
        if (background) {
            wchar_t current[64]{};
            DWORD bytes = sizeof(current);
            if (RegGetValueW(HKEY_CURRENT_USER, ExplorerCommandKey, L"ExplorerCommandHandler",
                    RRF_RT_REG_SZ, nullptr, current, &bytes) != ERROR_SUCCESS ||
                lstrcmpiW(current, ExplorerClassId) != 0) return;
        }
        const std::wstring classKey = std::wstring(L"Software\\Classes\\CLSID\\") + ExplorerClassId;
        DWORD pending = 0;
        DWORD bytes = sizeof(pending);
        if (RegGetValueW(HKEY_CURRENT_USER, classKey.c_str(), L"UninstallPending",
            RRF_RT_REG_DWORD, nullptr, &pending, &bytes) == ERROR_SUCCESS && pending) return;
        HMODULE dll = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(ExplorerClassId), &dll)) {
            std::wstring helper(32768, L'\0');
            const DWORD length = GetModuleFileNameW(dll, helper.data(), static_cast<DWORD>(helper.size()));
            if (length && length < helper.size()) {
                helper.resize(length);
                const auto separator = helper.find_last_of(L"\\/");
                if (separator != std::wstring::npos) {
                    helper.replace(separator + 1, std::wstring::npos, TransferRelativePath);
                    if (RequestTransferWindow(helper, !background)) return;
                    if (background) {
                        static std::atomic<ULONGLONG> lastStart = 0;
                        const ULONGLONG now = GetTickCount64();
                        auto previous = lastStart.load(std::memory_order_relaxed);
                        if ((previous && now - previous < 1000) ||
                            !lastStart.compare_exchange_strong(previous, now, std::memory_order_relaxed)) return;
                    }
                    std::wstring command;
                    if (background) command = L"\"" + helper + L"\" --background";
                    STARTUPINFOW startup{sizeof(startup)};
                    PROCESS_INFORMATION process{};
                    if (CreateProcessW(helper.c_str(), command.empty() ? nullptr : command.data(), nullptr, nullptr,
                        FALSE, 0, nullptr, nullptr, &startup, &process)) {
                        if (!background) AllowSetForegroundWindow(process.dwProcessId);
                        CloseHandle(process.hThread);
                        CloseHandle(process.hProcess);
                        return;
                    }
                }
            }
        }
        if (!background)
            MessageBoxW(root_, ExplorerText(L"无法打开局域网互传。请重新运行 LYwintabs 安装包以修复组件。"), L"LYwintabs", MB_OK | MB_ICONERROR);
    }

    void OnCommand(WORD command)
    {
        if (command == AddCommand) {
            AddCurrentFavorite();
        } else if (command == SettingsCommand) {
            OpenSettings();
        } else if (command == MoreCommand) {
            OpenBookmarksMenu(more_, {}, buttons_.size());
        } else if (command == TransferCommand) {
            OpenTransfer();
        } else if (command >= FirstFavorite) {
            const size_t index = command - FirstFavorite;
            if (index >= buttons_.size() || index >= rootItems_.size()) return;
            const auto item = items_[rootItems_[index]];
            if (item.IsFolder()) OpenBookmarksMenu(buttons_[index], item.id);
            else OpenFavorite(item.path);
        }
    }

    ModuleReference module_;
    std::atomic<ULONG> references_ = 1;
    HWND root_;
    bool useTabs_;
    HWND bar_ = nullptr;
    HWND tabHost_ = nullptr;
    HWND tabSettings_ = nullptr;
    HWND settings_ = nullptr;
    HWND addBookmark_ = nullptr;
    HWND view_ = nullptr;
    HWND pendingView_ = nullptr;
    ComPtr<IShellView> watchedShellView_;
    FolderId watchedFolder_;
    ULONGLONG nextTransferFolderCheck_ = 0;
    HWND more_ = nullptr;
    HWND transfer_ = nullptr;
    HWND tooltip_ = nullptr;
    ViewSlot viewSlot_{};
    ViewSlot pendingSlot_{};
    std::array<NativeBand, 3> topBands_{};
    LONG nativeRibbonTop_ = 0;
    LONG nativeBodyBottomGap_ = 0;
    int topBandsHeight_ = 0;
    bool movingTopBands_ = false;
    int reservedHeight_ = 0;
    bool layoutQueued_ = false;
    bool refreshing_ = false;
    bool watchingViewCreation_ = false;
    bool dark_ = false;
    bool buttonsDirty_ = true;
    bool captionHidden_ = false;
    bool detaching_ = false;
    DWORD appearance_ = 0;
    static constexpr unsigned QuickAccessMaxAttempts = 6;
    unsigned quickAccessAttempts_ = 0;
    ULONGLONG nextQuickAccessAttempt_ = 0;
    QuickAccessDock quickAccess_;
    int buttonsWidth_ = 0;
    int buttonsTop_ = 0;
    int bookmarkDividerX_ = 0;
    int tabsWidth_ = 0;
    int tabsHeight_ = 0;
    UINT tabsDpi_ = 0;
    UINT dpi_ = 96;
    UINT barFontDpi_ = 0;
    HFONT barFont_ = nullptr;
    ULONG_PTR bookmarkGraphics_ = 0;
    HWND hoveredBookmark_ = nullptr;
    FolderTabs tabs_;
    AddressBookmarkButton addressStar_;
    Favorites store_;
    std::vector<Favorite> items_;
    std::vector<size_t> rootItems_;
    std::vector<HWND> buttons_;
    std::optional<BookmarkDrag> bookmarkDrag_;
    BookmarkPopup* activeBookmarkPopup_ = nullptr;
};

bool IsExplorerFrame(HWND root)
{
    return ClassIs(root, L"CabinetWClass") && GetWindowThreadProcessId(root, nullptr) == GetCurrentThreadId() &&
        ExplorerProcess();
}

HRESULT AttachFavoritesBar(HWND root)
{
    const auto classKey = std::wstring(L"Software\\Classes\\CLSID\\") + ExplorerClassId;
    DWORD uninstalling = 0;
    DWORD size = sizeof(uninstalling);
    if (RegGetValueW(HKEY_CURRENT_USER, classKey.c_str(), L"UninstallPending", RRF_RT_REG_DWORD,
            nullptr, &uninstalling, &size) == ERROR_SUCCESS && uninstalling) return HRESULT_FROM_WIN32(ERROR_BUSY);
    if (GetPropW(root, BarProperty) || GetPropW(root, AttachingProperty)) return S_FALSE;
    if (!ActiveView(root)) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (!SetPropW(root, AttachingProperty, root)) return E_FAIL;
    struct AttachmentGuard {
        HWND root;
        ~AttachmentGuard() { RemovePropW(root, AttachingProperty); }
    } guard{root};
    ComPtr<FavoritesBar> bar;
    bar.Attach(new (std::nothrow) FavoritesBar(root, UsesPluginTabs()));
    if (!bar) return E_OUTOFMEMORY;
    try {
        const HRESULT started = bar->Start();
        if (FAILED(started)) bar->Close();
        return started;
    } catch (...) {
        bar->Close();
        throw;
    }
}

} // namespace

extern "C" HRESULT WINAPI ShowLYwintabsSettings(HMODULE dll)
{
    const WinRtApartment apartment;
    if (FAILED(apartment.hr)) return apartment.hr;
    try {
        Favorites store;
        const HRESULT initialized = store.Initialize();
        if (FAILED(initialized)) return initialized;
        const auto changes = ShowSettingsDialog(dll, nullptr, store, ReadAppearance(), UsesPluginTabs(), false);
        if (changes.bookmarksChanged) BroadcastToOtherBars(nullptr, BookmarksChangedMessage());
        return changes.action >= 0 ? S_OK : E_FAIL;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

HRESULT ToggleFavoritesBar(HWND owner)
{
    const HWND root = GetAncestor(owner, GA_ROOT);
    if (!IsExplorerFrame(root)) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if (const auto existing = static_cast<HWND>(GetPropW(root, BarProperty))) {
        if (SendMessageW(existing, OpenHiddenSettings, 0, 0)) return S_OK;
        if (!SetPropW(root, SuppressedProperty, root)) return E_FAIL;
        if (DestroyWindow(existing)) return S_OK;
        RemovePropW(root, SuppressedProperty);
        return E_FAIL;
    }
    const HRESULT attached = AttachFavoritesBar(root);
    if (attached == S_OK) RemovePropW(root, SuppressedProperty);
    return attached;
}

extern "C" LRESULT CALLBACK ExplorerAttachHook(int code, WPARAM action, LPARAM data)
{
    if (code == HC_ACTION && action == PM_REMOVE && data) {
        static const UINT attach = RegisterWindowMessageW(AttachMessageName);
        const auto& message = *reinterpret_cast<const MSG*>(data);
        if (attach && message.message == attach && !message.wParam && !message.lParam &&
            IsExplorerFrame(message.hwnd) && !GetPropW(message.hwnd, SuppressedProperty) &&
            !GetPropW(message.hwnd, BarProperty) && !GetPropW(message.hwnd, AttachingProperty)) {
            try {
                // COM owns the DLL after the launcher removes its short-lived thread hook.
                ComPtr<IClassFactory> factory;
                if (SUCCEEDED(CoGetClassObject(FavoritesExtensionId, CLSCTX_INPROC_SERVER, nullptr,
                        IID_PPV_ARGS(factory.GetAddressOf())))) {
                    AttachFavoritesBar(message.hwnd);
                }
            } catch (...) {
                // A Shell extension must never propagate exceptions into the Explorer message loop.
            }
        }
    }
    return CallNextHookEx(nullptr, code, action, data);
}
