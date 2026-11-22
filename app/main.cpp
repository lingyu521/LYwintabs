#include "ExplorerTab.h"
#include "Favorites.h"
#include "FolderLookup.h"
#include "Resource.h"

#include <commctrl.h>
#include <roapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t WindowClass[] = L"LYwintabs.MainWindow";
constexpr wchar_t AppName[] = L"LYwintabs";
constexpr UINT LayoutChanged = WM_APP + 2;

enum Command : WORD {
    TabBar = 100,
    Address,
    NewTab,
    CloseTab,
    Back,
    Forward,
    Up,
    Go,
    NextTab,
    PreviousTab,
    FocusAddress,
    ToggleFocus,
    AddFavorite,
    FavoriteItem,
    MoreFavorites
};

enum FavoriteAction : UINT { OpenHere = 1, OpenInTab, Rename, Remove };

constexpr ACCEL Shortcuts[] = {
    {FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, PreviousTab},
    {FVIRTKEY | FCONTROL, VK_TAB, NextTab},
    {FVIRTKEY | FCONTROL, 'T', NewTab},
    {FVIRTKEY | FCONTROL, 'W', CloseTab},
    {FVIRTKEY | FCONTROL, VK_F4, CloseTab},
    {FVIRTKEY | FCONTROL, 'L', FocusAddress},
    {FVIRTKEY | FALT, 'D', FocusAddress},
    {FVIRTKEY | FALT, VK_LEFT, Back},
    {FVIRTKEY | FALT, VK_RIGHT, Forward},
    {FVIRTKEY | FALT, VK_UP, Up},
    {FVIRTKEY, VK_F6, ToggleFocus},
};

FolderId KnownFolder(REFKNOWNFOLDERID id)
{
    PIDLIST_ABSOLUTE folder = nullptr;
    SHGetKnownFolderIDList(id, KF_FLAG_DONT_VERIFY, nullptr, &folder);
    return FolderId(folder);
}

std::wstring FailureText(const wchar_t* message, HRESULT hr)
{
    wchar_t code[16]{};
    swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(hr));
    return std::wstring(message) + L"（" + code + L"）";
}

std::wstring ReadText(HWND edit)
{
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(edit)) + 1, L'\0');
    text.resize(GetWindowTextW(edit, text.data(), static_cast<int>(text.size())));
    return text;
}

std::wstring Trim(std::wstring text)
{
    const auto start = text.find_first_not_of(L" \t");
    if (start == std::wstring::npos) return {};
    return text.substr(start, text.find_last_not_of(L" \t") - start + 1);
}

std::wstring ControlLabel(const std::wstring& text)
{
    std::wstring escaped;
    for (const wchar_t character : text) {
        escaped += character;
        if (character == L'&') escaped += character;
    }
    return escaped;
}

INT_PTR CALLBACK RenameDialog(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG) {
        auto* name = reinterpret_cast<std::wstring*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        const HWND edit = GetDlgItem(window, IDC_FAVORITE_NAME);
        Edit_SetText(edit, name->c_str());
        Edit_LimitText(edit, 255);
        Edit_SetSel(edit, 0, -1);
        SetFocus(edit);
        return FALSE;
    }
    if (message != WM_COMMAND) return FALSE;
    if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (LOWORD(wParam) != IDOK) return FALSE;
    auto input = Trim(ReadText(GetDlgItem(window, IDC_FAVORITE_NAME)));
    if (input.empty()) {
        MessageBeep(MB_ICONWARNING);
        return TRUE;
    }
    *reinterpret_cast<std::wstring*>(GetWindowLongPtrW(window, DWLP_USER)) = std::move(input);
    EndDialog(window, IDOK);
    return TRUE;
}

class App {
public:
    int Run(HINSTANCE instance, int show, int argumentCount, wchar_t* const* arguments)
    {
        instance_ = instance;
        HRESULT bookmarksLoaded = favoriteStore_.Initialize();
        if (SUCCEEDED(bookmarksLoaded)) bookmarksLoaded = favoriteStore_.Load(favorites_);
        KeepRootBookmarks();
        const WNDCLASSEXW windowClass{
            sizeof(WNDCLASSEXW), 0, WindowProc, 0, 0, instance,
            nullptr, LoadCursorW(nullptr, IDC_ARROW), GetSysColorBrush(COLOR_BTNFACE),
            nullptr, WindowClass, nullptr
        };
        if (!RegisterClassExW(&windowClass)) return StartupFailure();

        const UINT systemDpi = GetDpiForSystem();
        constexpr DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        RECT bounds{0, 0, MulDiv(1000, systemDpi, 96), MulDiv(630, systemDpi, 96)};
        AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, systemDpi);
        if (!CreateWindowExW(0, WindowClass, AppName, style,
                CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
                nullptr, nullptr, instance, this)) return StartupFailure();

        auto shortcuts = std::to_array(Shortcuts);
        accelerators_ = CreateAcceleratorTableW(shortcuts.data(), static_cast<int>(shortcuts.size()));
        if (!accelerators_) {
            DestroyWindow(window_);
            return StartupFailure();
        }
        lookup_.Attach(window_);

        inCommand_ = true;
        const HRESULT first = AddInitialTab(argumentCount > 1 ? arguments[1] : nullptr, FOLDERID_Profile);
        const HRESULT second = AddInitialTab(argumentCount > 2 ? arguments[2] : nullptr, FOLDERID_Documents);
        if (tabs_.empty()) {
            const auto text = FailureText(L"无法创建 Windows Shell 视图。", first);
            MessageBoxW(window_, text.c_str(), AppName, MB_OK | MB_ICONERROR);
            DestroyWindow(window_);
            DestroyAcceleratorTable(std::exchange(accelerators_, nullptr));
            return 1;
        }
        Activate(0, false);
        if (FAILED(second)) SetStatus(FailureText(L"无法创建第二个标签。", second));
        if (FAILED(bookmarksLoaded)) SetStatus(FailureText(L"无法读取收藏。", bookmarksLoaded));
        ShowWindow(window_, show);
        FocusActiveView();
        FinishCommand();

        MSG message{};
        BOOL result = FALSE;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (Pretranslate(message)) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (window_) DestroyWindow(window_);
        DestroyAcceleratorTable(std::exchange(accelerators_, nullptr));
        return result < 0 ? 1 : static_cast<int>(message.wParam);
    }

private:
    static int StartupFailure()
    {
        MessageBoxW(nullptr, L"无法创建 LYwintabs 主窗口或快捷键。", AppName, MB_OK | MB_ICONERROR);
        return 1;
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            auto* app = static_cast<App*>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams);
            app->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        return app ? app->OnMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message) {
        case WM_CREATE:
            dpi_ = GetDpiForWindow(window_);
            if (!CreateControls()) return -1;
            UpdateFont();
            Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto& limits = *reinterpret_cast<MINMAXINFO*>(lParam);
            RECT minimum{0, 0, Scale(590), Scale(330)};
            AdjustWindowRectExForDpi(&minimum, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
            limits.ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
            return 0;
        }
        case WM_SIZE:
            if (tabBar_ && wParam != SIZE_MINIMIZED) RequestLayout();
            return 0;
        case LayoutChanged:
            if (!IsIconic(window_)) RequestLayout();
            return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam);
            UpdateFont();
            const auto& bounds = *reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, bounds.left, bounds.top,
                bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_NOZORDER);
            RequestLayout();
            return 0;
        }
        case WM_NOTIFY: {
            const auto* notice = reinterpret_cast<const NMHDR*>(lParam);
            if (notice->hwndFrom == tabBar_ && notice->code == TCN_SELCHANGE && !inCommand_) {
                inCommand_ = true;
                Activate(TabCtrl_GetCurSel(tabBar_), false);
                FinishCommand();
                return 0;
            }
            break;
        }
        case WM_COMMAND:
            if (!inCommand_ && HIWORD(wParam) <= 1) {
                HandleCommand(LOWORD(wParam), reinterpret_cast<HWND>(lParam));
            }
            return 0;
        case WM_CONTEXTMENU:
            if (!inCommand_ && Active() && FavoriteContextMenu(reinterpret_cast<HWND>(wParam), lParam)) return 0;
            break;
        case FolderLookupComplete:
            if (inCommand_) {
                lookupResultsPending_ = true;
            } else {
                inCommand_ = true;
                ApplyLookupResults();
                FinishCommand();
            }
            return 0;
        case NavigationChanged:
            if (inCommand_) {
                navigationRefreshPending_ = true;
            } else {
                inCommand_ = true;
                RefreshNavigation();
                FinishCommand();
            }
            return 0;
        case WM_CLOSE:
            if (inCommand_) {
                closeRequested_ = true;
            } else {
                inCommand_ = true;
                lookup_.Detach();
                tabs_.clear();
                DestroyWindow(window_);
            }
            return 0;
        case WM_DESTROY:
            inCommand_ = true;
            lookup_.Detach();
            tabs_.clear();
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY: {
            const HWND window = std::exchange(window_, nullptr);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            if (font_) DeleteObject(std::exchange(font_, nullptr));
            return DefWindowProcW(window, message, wParam, lParam);
        }
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    HWND Control(DWORD exStyle, const wchar_t* type, const wchar_t* text, DWORD style, WORD id = 0) const
    {
        return CreateWindowExW(exStyle, type, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), instance_, nullptr);
    }

    bool CreateControls()
    {
        tabBar_ = Control(0, WC_TABCONTROLW, L"", WS_TABSTOP | TCS_TABS, TabBar);
        newTab_ = Control(0, L"Button", L"新建标签", WS_TABSTOP, NewTab);
        closeTab_ = Control(0, L"Button", L"关闭标签", WS_TABSTOP, CloseTab);
        back_ = Control(0, L"Button", L"后退", WS_TABSTOP, Back);
        forward_ = Control(0, L"Button", L"前进", WS_TABSTOP, Forward);
        up_ = Control(0, L"Button", L"上一级", WS_TABSTOP, Up);
        addressLabel_ = Control(0, L"Static", L"地址", SS_CENTERIMAGE);
        address_ = Control(WS_EX_CLIENTEDGE, L"Edit", L"", WS_TABSTOP | ES_AUTOHSCROLL, Address);
        go_ = Control(0, L"Button", L"转到", WS_TABSTOP, Go);
        addFavorite_ = Control(0, L"Button", L"收藏当前", WS_TABSTOP, AddFavorite);
        moreFavorites_ = Control(0, L"Button", L"更多…", WS_TABSTOP, MoreFavorites);
        status_ = Control(0, L"Static", L"", SS_CENTERIMAGE | SS_ENDELLIPSIS | SS_NOPREFIX);
        if (!tabBar_ || !newTab_ || !closeTab_ || !back_ || !forward_ || !up_ || !addressLabel_ ||
            !address_ || !go_ || !addFavorite_ || !moreFavorites_ || !status_) return false;
        SendMessageW(address_, EM_SETLIMITTEXT, 32767, 0);
        Edit_SetCueBannerText(address_, L"输入文件夹路径");
        return true;
    }

    int Scale(int value) const { return MulDiv(value, dpi_, 96); }

    void UpdateFont()
    {
        NONCLIENTMETRICSW metrics{sizeof(NONCLIENTMETRICSW)};
        if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0, dpi_)) return;
        const HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
        if (!font) return;
        for (HWND control : {tabBar_, newTab_, closeTab_, back_, forward_, up_, addressLabel_, address_, go_,
                addFavorite_, moreFavorites_, status_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        for (HWND button : favoriteButtons_) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (font_) DeleteObject(font_);
        font_ = font;
    }

    void Layout()
    {
        RECT client{};
        GetClientRect(window_, &client);
        const int margin = Scale(8);
        const int gap = Scale(6);
        const int row = Scale(28);
        const int end = client.right - margin;
        const int tabButtonWidth = Scale(84);
        const int closeX = end - tabButtonWidth;
        const int newX = closeX - gap - tabButtonWidth;
        MoveWindow(tabBar_, margin, margin, std::max(0, newX - gap - margin), Scale(31), TRUE);
        MoveWindow(newTab_, newX, margin, tabButtonWidth, row, TRUE);
        MoveWindow(closeTab_, closeX, margin, tabButtonWidth, row, TRUE);

        const int navY = margin + Scale(31) + gap;
        const int navButtonWidth = Scale(56);
        const int addressLabelWidth = Scale(29);
        int x = margin;
        for (HWND button : {back_, forward_, up_}) {
            MoveWindow(button, x, navY, navButtonWidth, row, TRUE);
            x += navButtonWidth + gap;
        }
        MoveWindow(addressLabel_, x, navY, addressLabelWidth, row, TRUE);
        x += addressLabelWidth + gap;
        const int goX = end - navButtonWidth;
        MoveWindow(address_, x, navY, std::max(0, goX - gap - x), row, TRUE);
        MoveWindow(go_, goX, navY, navButtonWidth, row, TRUE);

        const int favoritesY = navY + row + gap;
        LayoutFavorites(margin, end, favoritesY, row, gap);
        const int viewTop = favoritesY + row + gap;
        const int statusHeight = Scale(25);
        const int statusTop = std::max(viewTop, static_cast<int>(client.bottom) - statusHeight);
        viewBounds_ = {margin, viewTop, std::max(end, margin), statusTop};
        MoveWindow(status_, margin, statusTop, std::max(0, end - margin), statusHeight, TRUE);
        for (const auto& tab : tabs_) tab->Resize(viewBounds_);
    }

    void LayoutFavorites(int left, int right, int y, int height, int gap)
    {
        const int addWidth = Scale(84);
        const int moreWidth = Scale(64);
        MoveWindow(addFavorite_, left, y, addWidth, height, TRUE);
        const int moreX = right - moreWidth;
        int x = left + addWidth + gap;

        const HDC dc = GetDC(window_);
        const HGDIOBJ oldFont = dc && font_ ? SelectObject(dc, font_) : nullptr;
        size_t visible = 0;
        for (const auto& favorite : favorites_) {
            SIZE size{};
            if (dc) GetTextExtentPoint32W(dc, favorite.name.c_str(), static_cast<int>(favorite.name.size()), &size);
            const int width = std::clamp(static_cast<int>(size.cx) + Scale(18), Scale(50), Scale(224));
            const int availableEnd = visible + 1 == favorites_.size() ? right : moreX - gap;
            if (x + width > availableEnd) break;
            if (visible == favoriteButtons_.size()) {
                HWND button = Control(0, L"Button", L"", WS_TABSTOP, FavoriteItem);
                if (!button) break;
                SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
                favoriteButtons_.push_back(button);
            }
            const HWND button = favoriteButtons_[visible++];
            SetWindowTextW(button, ControlLabel(favorite.name).c_str());
            MoveWindow(button, x, y, width, height, TRUE);
            x += width + gap;
        }
        if (oldFont) SelectObject(dc, oldFont);
        if (dc) ReleaseDC(window_, dc);
        while (favoriteButtons_.size() > visible) {
            if (GetFocus() == favoriteButtons_.back()) SetFocus(addFavorite_);
            DestroyWindow(favoriteButtons_.back());
            favoriteButtons_.pop_back();
        }
        MoveWindow(moreFavorites_, moreX, y, moreWidth, height, TRUE);
        const bool overflow = visible < favorites_.size();
        if (!overflow && GetFocus() == moreFavorites_) SetFocus(addFavorite_);
        ShowWindow(moreFavorites_, overflow ? SW_SHOWNA : SW_HIDE);
    }

    void RequestLayout()
    {
        // SetRect can enter Shell code while Windows is dispatching window messages.
        if (inCommand_) {
            layoutPending_ = true;
            return;
        }
        inCommand_ = true;
        Layout();
        FinishCommand();
    }

    ExplorerTab* Active() const
    {
        return active_ >= 0 && active_ < static_cast<int>(tabs_.size()) ? tabs_[active_].get() : nullptr;
    }

    HRESULT AddInitialTab(const wchar_t* path, REFKNOWNFOLDERID defaultFolder)
    {
        auto folder = path ? FolderId{} : KnownFolder(defaultFolder);
        if (!path && !folder) folder = KnownFolder(FOLDERID_ComputerFolder);
        return AddTab(std::move(folder), path);
    }

    HRESULT AddTab(FolderId folder, const wchar_t* path = nullptr)
    {
        auto tab = std::make_unique<ExplorerTab>();
        const HRESULT hr = tab->Initialize(window_, viewBounds_);
        if (FAILED(hr)) return hr;
        const int index = static_cast<int>(tabs_.size());
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = tab->title.data();
        if (TabCtrl_InsertItem(tabBar_, index, &item) == -1) return E_FAIL;
        tabs_.push_back(std::move(tab));

        if (path) {
            ResolvePath(index, path);
        } else if (folder) {
            Browse(index, folder.get(), SBSP_ABSOLUTE, L"无法打开目标文件夹。");
        } else {
            tabs_[index]->status = L"请在地址栏输入文件夹路径。";
        }
        return S_OK;
    }

    void Activate(int index, bool focusView)
    {
        if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
        if (Active() && index != active_) Active()->Show(false);
        active_ = index;
        TabCtrl_SetCurSel(tabBar_, index);
        Active()->Show(true);
        SetWindowTextW(address_, Active()->address.c_str());
        SetWindowTextW(status_, Active()->status.c_str());
        EnableWindow(addFavorite_, !Active()->address.empty());
        if (focusView) FocusActiveView();
    }

    void CloseActiveTab()
    {
        if (tabs_.size() == 1) {
            lookup_.Detach();
            closeRequested_ = true;
            return;
        }
        const int closed = active_;
        auto tab = std::move(tabs_[closed]);
        tabs_.erase(tabs_.begin() + closed);
        active_ = -1;
        tab->Show(false);
        TabCtrl_DeleteItem(tabBar_, closed);
        tab.reset();
        Activate(std::min(closed, static_cast<int>(tabs_.size()) - 1), true);
    }

    void FocusAddressBar() const
    {
        SetFocus(address_);
        Edit_SetSel(address_, 0, -1);
    }

    void FocusActiveView() const
    {
        if (!Active() || !Active()->FocusView()) FocusAddressBar();
    }

    void CycleTabs(int direction)
    {
        const int count = static_cast<int>(tabs_.size());
        if (count < 2) return;
        const HWND focus = GetFocus();
        Activate((active_ + count + direction) % count, focus != address_ && focus != tabBar_);
        if (focus == address_) FocusAddressBar();
    }

    void SetTabStatus(int index, std::wstring text)
    {
        auto& tab = *tabs_[index];
        tab.status = std::move(text);
        if (index == active_) SetWindowTextW(status_, tab.status.c_str());
    }

    void SetStatus(std::wstring text) { SetTabStatus(active_, std::move(text)); }

    void Browse(int index, PCIDLIST_ABSOLUTE folder, UINT flags, const wchar_t* error)
    {
        tabs_[index]->lookupRequest = 0;
        const HRESULT hr = tabs_[index]->Browse(folder, flags);
        UpdateNavigation(index);
        if (FAILED(hr)) SetTabStatus(index, FailureText(error, hr));
    }

    void BrowseAddress()
    {
        auto input = Trim(ReadText(address_));
        if (input.size() >= 2 && input.front() == L'"' && input.back() == L'"') {
            input = input.substr(1, input.size() - 2);
        }
        ResolvePath(active_, std::move(input));
    }

    void ResolvePath(int index, std::wstring path)
    {
        auto& tab = *tabs_[index];
        tab.lookupRequest = 0;
        if (path.empty()) {
            SetTabStatus(index, L"请输入文件夹路径。");
            return;
        }
        const auto request = ++lastLookupRequest_;
        const HRESULT hr = lookup_.Submit(request, std::move(path));
        if (FAILED(hr)) {
            SetTabStatus(index, FailureText(L"暂时无法解析路径，请稍后重试。", hr));
            return;
        }
        tab.lookupRequest = request;
        tab.lookupNavigationSerial = tab.NavigationSerial();
        SetTabStatus(index, L"正在打开文件夹…");
    }

    void ApplyLookupResults()
    {
        for (auto& result : lookup_.TakeResults()) {
            const auto target = std::ranges::find_if(tabs_, [&](const auto& tab) {
                return tab->lookupRequest == result.request;
            });
            if (target == tabs_.end()) continue;
            const int index = static_cast<int>(target - tabs_.begin());
            auto& tab = **target;
            tab.lookupRequest = 0;
            if (tab.NavigationSerial() != tab.lookupNavigationSerial) continue;
            if (FAILED(result.hr)) {
                SetTabStatus(index, FailureText(L"路径无效或当前不可访问。", result.hr));
                continue;
            }
            const auto* current = tab.CurrentFolder();
            SetTabStatus(index, L"");
            if (current && ILIsEqual(result.folder.get(), current)) {
                if (index == active_) SetWindowTextW(address_, tab.address.c_str());
            } else {
                Browse(index, result.folder.get(), SBSP_ABSOLUTE, L"无法打开目标文件夹。");
            }
        }
    }

    void UpdateNavigation(int index)
    {
        auto& tab = *tabs_[index];
        if (!tab.ReadNavigationChange()) return;
        if (tab.lookupNavigationSerial != tab.NavigationSerial()) tab.lookupRequest = 0;
        if (tab.lookupRequest) tab.status = L"正在打开文件夹…";
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = tab.title.data();
        TabCtrl_SetItem(tabBar_, index, &item);
        if (index == active_) {
            SetWindowTextW(address_, tab.address.c_str());
            SetWindowTextW(status_, tab.status.c_str());
            EnableWindow(addFavorite_, !tab.address.empty());
        }
    }

    void RefreshNavigation()
    {
        for (int index = 0; index < static_cast<int>(tabs_.size()); ++index) UpdateNavigation(index);
    }

    void FinishCommand()
    {
        inCommand_ = false;
        if (closeRequested_) {
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return;
        }
        if (std::exchange(navigationRefreshPending_, false)) PostMessageW(window_, NavigationChanged, 0, 0);
        if (std::exchange(lookupResultsPending_, false)) PostMessageW(window_, FolderLookupComplete, 0, 0);
        if (std::exchange(layoutPending_, false)) PostMessageW(window_, LayoutChanged, 0, 0);
    }

    size_t FavoriteIndex(HWND sender) const
    {
        const auto found = std::ranges::find(favoriteButtons_, sender);
        return found == favoriteButtons_.end() ? favorites_.size() : static_cast<size_t>(found - favoriteButtons_.begin());
    }

    void ChangeFavorite(FavoriteChange change, const Favorite& item)
    {
        const HRESULT hr = favoriteStore_.Update(change, item, favorites_);
        if (FAILED(hr)) {
            SetStatus(FailureText(L"无法保存收藏。", hr));
            return;
        }
        KeepRootBookmarks();
        Layout();
        if (hr == S_FALSE && change == FavoriteChange::Add) {
            const auto found = std::ranges::find(favorites_, item.path, &Favorite::path);
            const auto index = static_cast<size_t>(found - favorites_.begin());
            SetFocus(index < favoriteButtons_.size() ? favoriteButtons_[index] : moreFavorites_);
            SetStatus(L"此文件夹已收藏。");
        } else if (hr == S_FALSE) {
            SetStatus(L"此收藏已在其他窗口移除。");
        } else {
            SetStatus(change == FavoriteChange::Remove ? L"已移除收藏。" : L"收藏已保存。");
        }
    }

    void KeepRootBookmarks()
    {
        std::erase_if(favorites_, [](const Favorite& item) { return item.IsFolder() || !item.parentId.empty(); });
    }

    void HandleFavoriteAction(size_t index, UINT action)
    {
        if (index >= favorites_.size()) return;
        auto item = favorites_[index];
        switch (action) {
        case OpenHere:
            ResolvePath(active_, std::move(item.path));
            break;
        case OpenInTab: {
            const HRESULT hr = AddTab({}, item.path.c_str());
            if (SUCCEEDED(hr)) Activate(static_cast<int>(tabs_.size()) - 1, true);
            else SetStatus(FailureText(L"无法新建标签。", hr));
            break;
        }
        case Rename: {
            const INT_PTR renamed = DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_RENAME_FAVORITE), window_,
                RenameDialog, reinterpret_cast<LPARAM>(&item.name));
            if (renamed == IDOK) ChangeFavorite(FavoriteChange::Rename, item);
            if (renamed == -1) SetStatus(L"无法打开收藏重命名窗口。");
            break;
        }
        case Remove:
            SetFocus(addFavorite_);
            ChangeFavorite(FavoriteChange::Remove, item);
            break;
        }
    }

    static void AddFavoriteCommands(HMENU menu, UINT firstId)
    {
        AppendMenuW(menu, MF_STRING, firstId, L"打开");
        AppendMenuW(menu, MF_STRING, firstId + 1, L"在新标签打开");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, firstId + 2, L"重命名");
        AppendMenuW(menu, MF_STRING, firstId + 3, L"移除收藏");
    }

    bool FavoriteContextMenu(HWND sender, LPARAM point)
    {
        const size_t index = FavoriteIndex(sender);
        if (index >= favorites_.size()) return false;
        inCommand_ = true;
        const HMENU menu = CreatePopupMenu();
        if (menu) {
            AddFavoriteCommands(menu, OpenHere);
            RECT bounds{};
            GetWindowRect(sender, &bounds);
            const int x = point == -1 ? bounds.left : GET_X_LPARAM(point);
            const int y = point == -1 ? bounds.bottom : GET_Y_LPARAM(point);
            const UINT action = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                x, y, 0, window_, nullptr);
            DestroyMenu(menu);
            if (action) HandleFavoriteAction(index, action);
        }
        FinishCommand();
        return true;
    }

    void ShowMoreFavorites()
    {
        const HMENU menu = CreatePopupMenu();
        if (!menu) return;
        const size_t first = favoriteButtons_.size();
        for (size_t index = first; index < favorites_.size(); ++index) {
            const HMENU submenu = CreatePopupMenu();
            if (!submenu) break;
            AddFavoriteCommands(submenu, static_cast<UINT>((index - first) * 4 + 1));
            if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), ControlLabel(favorites_[index].name).c_str())) {
                DestroyMenu(submenu);
                break;
            }
        }
        RECT bounds{};
        GetWindowRect(moreFavorites_, &bounds);
        const UINT selected = TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
            bounds.right, bounds.bottom, 0, window_, nullptr);
        DestroyMenu(menu);
        if (selected) HandleFavoriteAction(first + (selected - 1) / 4, (selected - 1) % 4 + OpenHere);
    }

    void HandleCommand(WORD command, HWND sender = nullptr)
    {
        if (!Active()) return;
        inCommand_ = true;
        switch (command) {
        case NewTab: {
            const auto* current = Active()->CurrentFolder();
            auto folder = current ? FolderId(ILCloneFull(current)) : KnownFolder(FOLDERID_Profile);
            const HRESULT hr = AddTab(std::move(folder));
            if (SUCCEEDED(hr)) Activate(static_cast<int>(tabs_.size()) - 1, true);
            else SetStatus(FailureText(L"无法新建标签。", hr));
            break;
        }
        case CloseTab: CloseActiveTab(); break;
        case NextTab: CycleTabs(1); break;
        case PreviousTab: CycleTabs(-1); break;
        case Back: Browse(active_, nullptr, SBSP_NAVIGATEBACK, L"没有可打开的后退位置。"); break;
        case Forward: Browse(active_, nullptr, SBSP_NAVIGATEFORWARD, L"没有可打开的前进位置。"); break;
        case Up: {
            const auto* current = Active()->CurrentFolder();
            FolderId parent(current ? ILCloneFull(current) : nullptr);
            if (parent && ILRemoveLastID(parent.get())) {
                Browse(active_, parent.get(), SBSP_ABSOLUTE, L"无法打开上一级文件夹。");
            }
            break;
        }
        case Go: BrowseAddress(); break;
        case AddFavorite:
            UpdateNavigation(active_);
            if (!Active()->address.empty()) {
                ChangeFavorite(FavoriteChange::Add, Favorite{Active()->address, Active()->title});
            }
            break;
        case FavoriteItem: HandleFavoriteAction(FavoriteIndex(sender), OpenHere); break;
        case MoreFavorites: ShowMoreFavorites(); break;
        case FocusAddress: FocusAddressBar(); break;
        case ToggleFocus:
            if (Active()->ContainsWindow(GetFocus())) FocusAddressBar();
            else FocusActiveView();
            break;
        }
        FinishCommand();
    }

    bool Pretranslate(MSG& message)
    {
        if (message.hwnd != window_ && !IsChild(window_, message.hwnd)) return false;
        if (message.message < WM_KEYFIRST || message.message > WM_KEYLAST) return false;
        if (TranslateAcceleratorW(window_, accelerators_, &message)) return true;
        if (message.hwnd == address_ && message.message == WM_KEYDOWN) {
            if (message.wParam == VK_RETURN) {
                if (!(message.lParam & (1LL << 30))) HandleCommand(Go);
                return true;
            }
            if (message.wParam == VK_ESCAPE && Active()) {
                Active()->lookupRequest = 0;
                SetWindowTextW(address_, Active()->address.c_str());
                SetStatus(L"");
                return true;
            }
            if (message.wParam == 'A' && GetKeyState(VK_CONTROL) < 0) {
                Edit_SetSel(address_, 0, -1);
                return true;
            }
        }
        if (Active() && Active()->ContainsWindow(message.hwnd)) return Active()->TranslateKey(message);
        return IsDialogMessageW(window_, &message) != FALSE;
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND tabBar_ = nullptr;
    HWND newTab_ = nullptr;
    HWND closeTab_ = nullptr;
    HWND back_ = nullptr;
    HWND forward_ = nullptr;
    HWND up_ = nullptr;
    HWND addressLabel_ = nullptr;
    HWND address_ = nullptr;
    HWND go_ = nullptr;
    HWND addFavorite_ = nullptr;
    HWND moreFavorites_ = nullptr;
    HWND status_ = nullptr;
    HFONT font_ = nullptr;
    HACCEL accelerators_ = nullptr;
    UINT dpi_ = 96;
    RECT viewBounds_{};
    std::vector<std::unique_ptr<ExplorerTab>> tabs_;
    Favorites favoriteStore_;
    std::vector<Favorite> favorites_;
    std::vector<HWND> favoriteButtons_;
    FolderLookup lookup_;
    std::uint64_t lastLookupRequest_ = 0;
    int active_ = -1;
    bool inCommand_ = false;
    bool closeRequested_ = false;
    bool navigationRefreshPending_ = false;
    bool lookupResultsPending_ = false;
    bool layoutPending_ = false;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    const HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"无法初始化 Windows OLE。", AppName, MB_OK | MB_ICONERROR);
        return 1;
    }
    const INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);
    const HRESULT winrt = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(winrt)) {
        MessageBoxW(nullptr, L"无法初始化 Windows Runtime。", AppName, MB_OK | MB_ICONERROR);
        OleUninitialize();
        return 1;
    }

    int argumentCount = 0;
    auto* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    int result;
    {
        App app;
        result = app.Run(instance, show, argumentCount, arguments);
    }
    LocalFree(arguments);
    RoUninitialize();
    OleUninitialize();
    return result;
}
