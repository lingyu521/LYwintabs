#include "FolderTabs.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {

constexpr int TabTextInset = 7;
constexpr int TabIconSize = 16;
constexpr int TabIconGap = 4;

COLORREF Blend(COLORREF base, COLORREF foreground, int percent)
{
    const auto mix = [percent](int background, int ink) {
        return (background * (100 - percent) + ink * percent + 50) / 100;
    };
    return RGB(mix(GetRValue(base), GetRValue(foreground)), mix(GetGValue(base), GetGValue(foreground)),
        mix(GetBValue(base), GetBValue(foreground)));
}

void DrawFolder(HDC dc, int x, int y, UINT dpi, COLORREF color)
{
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const POINT outline[]{{x + scale(1), y + scale(3)}, {x + scale(6), y + scale(3)},
        {x + scale(8), y + scale(5)}, {x + scale(15), y + scale(5)}, {x + scale(15), y + scale(13)},
        {x + scale(1), y + scale(13)}, {x + scale(1), y + scale(3)}};
    const auto oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, color);
    Polyline(dc, outline, ARRAYSIZE(outline));
    SelectObject(dc, oldPen);
}

std::wstring FolderLabel(PCIDLIST_ABSOLUTE folder)
{
    PWSTR name = nullptr;
    const HRESULT result = SHGetNameFromIDList(folder, SIGDN_NORMALDISPLAY, &name);
    const std::unique_ptr<wchar_t, ShellMemory> text(name);
    return SUCCEEDED(result) && text && *text.get() ? text.get() : L"文件夹";
}

void Place(HWND window, int x, int y, int width, int height)
{
    SetWindowPos(window, nullptr, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
}

TOOLINFOW TabTool(HWND container, HWND button)
{
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = container;
    tool.uId = reinterpret_cast<UINT_PTR>(button);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.lpszText = LPSTR_TEXTCALLBACKW;
    return tool;
}

} // namespace

HRESULT ReadCurrentFolder(IShellView& view, FolderId& id)
{
    ComPtr<IFolderView> folderView;
    ComPtr<IPersistFolder2> folder;
    HRESULT result = view.QueryInterface(IID_PPV_ARGS(folderView.GetAddressOf()));
    if (SUCCEEDED(result)) result = folderView->GetFolder(IID_PPV_ARGS(folder.GetAddressOf()));
    if (FAILED(result)) return result;
    PIDLIST_ABSOLUTE pidl = nullptr;
    result = folder->GetCurFolder(&pidl);
    if (FAILED(result) || !pidl) {
        CoTaskMemFree(pidl);
        return FAILED(result) ? result : E_FAIL;
    }
    id.reset(pidl);
    return S_OK;
}

bool FolderTabs::Initialize(HWND container, HWND root, bool dark)
{
    parent_ = container;
    root_ = root;
    dark_ = dark;
    touch_ = (GetSystemMetrics(SM_DIGITIZER) & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH)) != 0;
    add_ = Button(L"+");
    more_ = Button(L"»");
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        0, 0, 0, 0, parent_, nullptr, nullptr, nullptr);
    if (tooltip_) SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 480);
    return add_ && more_;
}

FolderTabs::~FolderTabs()
{
    if (tooltip_ && IsWindow(tooltip_)) DestroyWindow(tooltip_);
}

HWND FolderTabs::Button(const wchar_t* label)
{
    const HWND button = CreateWindowExW(0, L"Button", label, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, parent_, nullptr, nullptr, nullptr);
    if (!button) return nullptr;
    if (!SetWindowSubclass(button, TabButtonProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
        DestroyWindow(button);
        return nullptr;
    }
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    return button;
}

bool FolderTabs::Insert(FolderId folder, size_t position)
{
    auto title = FolderLabel(folder.get());
    const bool shiftActive = !tabs_.empty() && position <= active_;
    auto& tab = *tabs_.insert(tabs_.begin() + position, Tab{std::move(folder), std::move(title), nullptr, nullptr});
    if (shiftActive) ++active_;
    tab.select = Button(tab.title.c_str());
    tab.close = Button(L"×");
    if (!tab.select || !tab.close) {
        Remove(position);
        return false;
    }
    if (tooltip_) {
        auto tool = TabTool(parent_, tab.select);
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    return true;
}

void FolderTabs::Observe()
{
    const auto browser = browser_;
    if (!browser || !IsWindow(parent_)) return;
    ComPtr<IShellView> view;
    FolderId current;
    if (FAILED(browser->QueryActiveShellView(view.GetAddressOf())) || !view ||
        FAILED(ReadCurrentFolder(*view.Get(), current))) return;

    if (tabs_.empty()) {
        if (Insert(std::move(current), 0)) Layout(width_, height_, dpi_);
        return;
    }
    if (pending_) {
        const auto next = *pending_;
        if (ILIsEqual(current.get(), tabs_[next.destination].folder.get())) {
            pending_.reset();
            Commit(next.destination, next.action);
            return;
        }
        if (GetTickCount64() < next.deadline) return;
        CancelNavigation(next);
    }
    auto& tab = tabs_[active_];
    if (!ILIsEqual(current.get(), tab.folder.get())) {
        auto title = FolderLabel(current.get());
        tab.folder = std::move(current);
        if (title != tab.title) {
            tab.title = std::move(title);
            SetWindowTextW(tab.select, tab.title.c_str());
            Layout(width_, height_, dpi_);
        }
    }
}

void FolderTabs::Activate(size_t destination, Action action)
{
    if (pending_ || destination >= tabs_.size() || destination == active_) return;
    Observe();
    if (ILIsEqual(tabs_[active_].folder.get(), tabs_[destination].folder.get())) {
        Commit(destination, action);
        return;
    }
    const Navigation navigation{destination, GetTickCount64() + 8000, action};
    const auto browser = browser_;
    const FolderId target(ILCloneFull(tabs_[destination].folder.get()));
    if (!browser || !target) {
        CancelNavigation(navigation);
        return;
    }
    pending_ = navigation;
    const HRESULT result = browser->BrowseObject(target.get(),
        SBSP_SAMEBROWSER | SBSP_ABSOLUTE | SBSP_CREATENOHISTORY);
    if (FAILED(result)) {
        if (pending_) CancelNavigation(navigation);
    } else {
        Observe();
    }
}

void FolderTabs::NewTab()
{
    if (pending_ || tabs_.empty()) return;
    PIDLIST_ABSOLUTE id = nullptr;
    const auto result = SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &id);
    FolderId folder(id);
    const size_t position = tabs_.size();
    if (SUCCEEDED(result) && folder && Insert(std::move(folder), position)) Activate(position, Action::OpenNew);
}

void FolderTabs::Duplicate(size_t index)
{
    if (pending_ || index >= tabs_.size()) return;
    Observe();
    FolderId folder(ILCloneFull(tabs_[index].folder.get()));
    if (folder && Insert(std::move(folder), index + 1)) Activate(index + 1, Action::OpenNew);
}

void FolderTabs::CloseTab(size_t index)
{
    if (pending_ || index >= tabs_.size()) return;
    if (tabs_.size() == 1) {
        PostMessageW(root_, WM_CLOSE, 0, 0);
    } else if (index == active_) {
        Activate(index + 1 < tabs_.size() ? index + 1 : index - 1, Action::CloseActive);
    } else {
        Remove(index);
        Layout(width_, height_, dpi_);
    }
}

void FolderTabs::Remove(size_t index)
{
    const HWND label = tabs_[index].select;
    const HWND close = tabs_[index].close;
    if (drag_ && (drag_->label == label || drag_->target == label)) FinishDrag(false);
    tabs_.erase(tabs_.begin() + index);
    if (index < active_) --active_;
    if (tooltip_ && label) {
        auto tool = TabTool(parent_, label);
        SendMessageW(tooltip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    if (label) DestroyWindow(label);
    if (close) DestroyWindow(close);
}

void FolderTabs::Commit(size_t destination, Action action)
{
    const size_t previous = active_;
    active_ = destination;
    if (action == Action::CloseActive) Remove(previous);
    Layout(width_, height_, dpi_);
}

void FolderTabs::CancelNavigation(const Navigation& navigation)
{
    pending_.reset();
    if (navigation.action == Action::OpenNew) Remove(navigation.destination);
    Layout(width_, height_, dpi_);
    MessageBeep(MB_ICONWARNING);
}

void FolderTabs::SetLayoutOptions(int leadingWidthDip, int topPaddingDip)
{
    if (leadingWidthDip_ == leadingWidthDip && topPaddingDip_ == topPaddingDip) return;
    leadingWidthDip_ = leadingWidthDip;
    topPaddingDip_ = topPaddingDip;
    Layout(width_, height_, dpi_);
}

void FolderTabs::Layout(int width, int height, UINT dpi)
{
    if (!add_ || !more_ || width <= 0 || height <= 0 || !dpi) return;
    if (drag_) FinishDrag(false);
    width_ = width;
    height_ = height;
    dpi_ = dpi;
    const auto scale = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
    const int gap = scale(5);
    const int contentHeight = scale(17);
    const int top = std::clamp(scale(topPaddingDip_), 0, std::max(0, height - contentHeight));
    const int left = std::clamp(scale(leadingWidthDip_), 0, width) + gap;
    const int action = scale(29);
    const int close = scale(24);
    const int iconSpace = scale(TabIconSize) + scale(TabIconGap);
    const int labelPadding = close + 2 * scale(TabTextInset) + iconSpace + scale(2);
    const int minimum = scale(84);
    const int maximum = scale(240);
    const int space = std::max(0, width - left - gap - action);
    const auto fitCount = [&](int room) {
        if (room < labelPadding + scale(10) + gap) return size_t{0};
        return std::min(tabs_.size(), static_cast<size_t>(std::max(1, room / (minimum + gap))));
    };
    const bool overflow = fitCount(space) < tabs_.size();
    const int available = std::max(0, space - (overflow ? action : 0));
    const size_t visible = fitCount(available);
    const size_t first = active_ >= visible && visible ? active_ - visible + 1 : 0;
    std::vector<int> preferredWidths(visible, minimum);
    int widthLimit = maximum;
    if (visible) {
        if (const HDC dc = GetDC(parent_)) {
            const auto oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            for (size_t offset = 0; offset < visible; ++offset) {
                const auto& title = tabs_[first + offset].title;
                SIZE text{};
                GetTextExtentPoint32W(dc, title.c_str(), static_cast<int>(title.size()), &text);
                preferredWidths[offset] = std::clamp(static_cast<int>(text.cx) + labelPadding, minimum, maximum);
            }
            if (oldFont) SelectObject(dc, oldFont);
            ReleaseDC(parent_, dc);
        }
        const int budget = available - static_cast<int>(visible) * gap;
        int lower = std::min(minimum, budget / static_cast<int>(visible));
        // Find the widest limit that fits every visible tab, leaving names below the limit untouched.
        while (lower < widthLimit) {
            const int limit = lower + (widthLimit - lower + 1) / 2;
            const int total = std::accumulate(preferredWidths.begin(), preferredWidths.end(), 0,
                [limit](int sum, int width) { return sum + std::min(width, limit); });
            if (total <= budget) lower = limit;
            else widthLimit = limit - 1;
        }
    }
    int x = left;
    for (size_t index = 0; index < tabs_.size(); ++index) {
        const auto& tab = tabs_[index];
        if (index >= first && index < first + visible) {
            const int tabWidth = std::min(preferredWidths[index - first], widthLimit);
            Place(tab.select, x, top, tabWidth - close, height - top);
            Place(tab.close, x + tabWidth - close, top, close, height - top);
            InvalidateRect(tab.select, nullptr, FALSE);
            InvalidateRect(tab.close, nullptr, FALSE);
            x += tabWidth + gap;
        } else {
            ShowWindow(tab.select, SW_HIDE);
            ShowWindow(tab.close, SW_HIDE);
        }
    }
    const int right = width - gap;
    if (x + action + (overflow ? action : 0) <= right) {
        Place(add_, x, top, action, height - top);
        x += action;
    } else {
        ShowWindow(add_, SW_HIDE);
    }
    EnableWindow(add_, !tabs_.empty());
    if (overflow && x + action <= right) Place(more_, x, top, action, height - top);
    else ShowWindow(more_, SW_HIDE);
}

LRESULT CALLBACK FolderTabs::TabButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR context)
{
    auto& owner = *reinterpret_cast<FolderTabs*>(context);
    const bool ownsDrag = owner.drag_ && owner.drag_->label == window;
    switch (message) {
    case WM_LBUTTONDOWN:
        if (!owner.pending_ && owner.tabs_.size() > 1 &&
            std::ranges::find(owner.tabs_, window, &Tab::select) != owner.tabs_.end()) {
            POINT pointer{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(window, &pointer);
            owner.drag_ = Drag{window, pointer.x};
        }
        break;
    case WM_MOUSEMOVE:
        if (ownsDrag && (wParam & MK_LBUTTON) && GetCapture() == window && owner.UpdateDrag(window, lParam)) {
            return 0;
        }
        if (!(wParam & MK_LBUTTON) && owner.hovered_ != window) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            if (TrackMouseEvent(&track)) owner.SetHoveredControl(window);
        }
        break;
    case WM_MOUSELEAVE:
        if (owner.hovered_ == window) owner.SetHoveredControl(nullptr);
        break;
    case WM_LBUTTONUP:
        if (ownsDrag) {
            if (owner.drag_->started) {
                owner.UpdateDrag(window, lParam);
                owner.FinishDrag(true);
                return 0;
            }
            owner.drag_.reset();
        }
        break;
    case WM_KEYDOWN:
        if (ownsDrag && wParam == VK_ESCAPE) {
            owner.FinishDrag(false);
            return 0;
        }
        break;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (ownsDrag) owner.FinishDrag(false);
        break;
    case WM_SHOWWINDOW:
        if (ownsDrag && !wParam) owner.FinishDrag(false);
        if (!wParam && owner.hovered_ == window) owner.SetHoveredControl(nullptr);
        break;
    case WM_NCDESTROY:
        if (ownsDrag) owner.FinishDrag(false);
        if (owner.hovered_ == window) owner.SetHoveredControl(nullptr);
        RemoveWindowSubclass(window, TabButtonProc, subclassId);
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

bool FolderTabs::UpdateDrag(HWND window, LPARAM position)
{
    POINT pointer{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
    ClientToScreen(window, &pointer);
    const auto source = std::ranges::find(tabs_, window, &Tab::select);
    if (source == tabs_.end()) return drag_->started;

    if (!drag_->started) {
        const int threshold = GetSystemMetricsForDpi(SM_CXDRAG, dpi_);
        if (pending_ || std::abs(pointer.x - drag_->startX) < std::max(1, threshold)) return false;
        drag_->started = true;
        if (tooltip_) SendMessageW(tooltip_, TTM_ACTIVATE, FALSE, 0);
        SendMessageW(window, BM_SETSTATE, FALSE, 0);
        InvalidateTab(window);
    }

    HWND target = nullptr;
    bool afterTarget = false;
    RECT strip{};
    if (!pending_ && GetWindowRect(parent_, &strip)) {
        InflateRect(&strip, 0, MulDiv(8, dpi_, 96));
        if (PtInRect(&strip, pointer)) {
            for (auto tab = tabs_.begin(); tab != tabs_.end(); ++tab) {
                if (tab == source || !IsWindowVisible(tab->select)) continue;
                RECT label{};
                RECT close{};
                if (!GetWindowRect(tab->select, &label) || !GetWindowRect(tab->close, &close)) continue;
                const LONG midpoint = label.left + (close.right - label.left) / 2;
                if (tab < source && pointer.x < midpoint) {
                    target = tab->select;
                    break;
                }
                if (tab > source && pointer.x >= midpoint) {
                    target = tab->select;
                    afterTarget = true;
                }
            }
        }
    }
    if (target != drag_->target) {
        InvalidateTab(drag_->target);
        drag_->target = target;
        drag_->afterTarget = afterTarget;
        InvalidateTab(target);
    }
    return true;
}

void FolderTabs::FinishDrag(bool drop)
{
    const auto drag = std::exchange(drag_, std::nullopt);
    if (!drag) return;
    if (drag->started) {
        if (tooltip_) SendMessageW(tooltip_, TTM_ACTIVATE, TRUE, 0);
        InvalidateTab(drag->label);
        InvalidateTab(drag->target);
        if (drop && drag->target && !pending_) {
            const auto source = std::ranges::find(tabs_, drag->label, &Tab::select);
            const auto target = std::ranges::find(tabs_, drag->target, &Tab::select);
            if (source != tabs_.end() && target != tabs_.end() && source != target) {
                const HWND activeLabel = tabs_[active_].select;
                if (source < target) std::rotate(source, source + 1, target + 1);
                else std::rotate(target, source, source + 1);
                active_ = static_cast<size_t>(std::ranges::find(tabs_, activeLabel, &Tab::select) - tabs_.begin());
                Layout(width_, height_, dpi_);
            }
        }
    }
    if (GetCapture() == drag->label) ReleaseCapture();
}

void FolderTabs::InvalidateTab(HWND label) const
{
    if (!label) return;
    const auto tab = std::ranges::find(tabs_, label, &Tab::select);
    if (tab == tabs_.end()) return;
    InvalidateRect(tab->select, nullptr, FALSE);
    InvalidateRect(tab->close, nullptr, FALSE);
}

void FolderTabs::SetHoveredControl(HWND control)
{
    const HWND previous = std::exchange(hovered_, control);
    for (const HWND changed : {previous, control}) {
        if (!changed) continue;
        const auto tab = std::ranges::find_if(tabs_, [changed](const Tab& t) {
            return t.select == changed || t.close == changed;
        });
        if (tab == tabs_.end()) InvalidateRect(changed, nullptr, FALSE);
        else InvalidateTab(tab->select);
    }
}

void FolderTabs::ShowAllTabs()
{
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (size_t index = 0; index < tabs_.size(); ++index) {
        std::wstring escaped;
        for (const wchar_t character : tabs_[index].title) {
            escaped += character;
            if (character == L'&') escaped += character;
        }
        AppendMenuW(menu, MF_STRING | (index == active_ ? MF_CHECKED : 0), index + 1, escaped.c_str());
    }
    RECT rect{};
    GetWindowRect(more_, &rect);
    const UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN,
        rect.right, rect.bottom, 0, parent_, nullptr);
    DestroyMenu(menu);
    if (chosen && chosen <= tabs_.size() && IsWindow(parent_)) Activate(chosen - 1);
}

bool FolderTabs::Command(HWND source)
{
    if (!source) return false;
    if (source == add_) {
        NewTab();
        return true;
    }
    if (source == more_) {
        if (!pending_) ShowAllTabs();
        return true;
    }
    for (size_t index = 0; index < tabs_.size(); ++index) {
        if (source == tabs_[index].select) {
            Activate(index);
            return true;
        }
        if (source == tabs_[index].close) {
            CloseTab(index);
            return true;
        }
    }
    return false;
}

bool FolderTabs::ContextMenu(HWND source, LPARAM point)
{
    const auto tab = std::ranges::find_if(tabs_, [source](const Tab& item) { return item.select == source; });
    if (tab == tabs_.end()) return false;
    const size_t index = static_cast<size_t>(tab - tabs_.begin());
    const HMENU menu = CreatePopupMenu();
    if (!menu) return true;
    AppendMenuW(menu, MF_STRING, 1, L"复制标签");
    AppendMenuW(menu, MF_STRING, 2, L"关闭标签");
    RECT bounds{};
    GetWindowRect(source, &bounds);
    const UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        point == -1 ? bounds.left : GET_X_LPARAM(point), point == -1 ? bounds.bottom : GET_Y_LPARAM(point),
        0, parent_, nullptr);
    DestroyMenu(menu);
    if (IsWindow(parent_)) {
        if (chosen == 1) Duplicate(index);
        if (chosen == 2) CloseTab(index);
    }
    return true;
}

bool FolderTabs::Notify(NMHDR& message)
{
    if (message.hwndFrom != tooltip_ || message.code != TTN_GETDISPINFOW) return false;
    const HWND source = reinterpret_cast<HWND>(message.idFrom);
    const auto tab = std::ranges::find_if(tabs_, [source](const Tab& item) { return item.select == source; });
    if (tab == tabs_.end()) return false;
    PWSTR path = nullptr;
    const HRESULT result = SHGetNameFromIDList(tab->folder.get(), SIGDN_FILESYSPATH, &path);
    const std::unique_ptr<wchar_t, ShellMemory> location(path);
    tooltipText_ = SUCCEEDED(result) && location ? location.get() : FolderLabel(tab->folder.get());
    reinterpret_cast<NMTTDISPINFOW&>(message).lpszText = tooltipText_.data();
    return true;
}

bool FolderTabs::OpenFolderInBackground(const wchar_t* path)
{
    if (pending_ || tabs_.empty()) return false;
    PIDLIST_ABSOLUTE pidl = nullptr;
    SFGAOF attributes = 0;
    const HRESULT result = SHParseDisplayName(path, nullptr, &pidl, SFGAO_FOLDER, &attributes);
    FolderId location(pidl);
    if (FAILED(result) || !location || !(attributes & SFGAO_FOLDER) ||
        !Insert(std::move(location), tabs_.size())) return false;
    Layout(width_, height_, dpi_);
    return true;
}

void FolderTabs::SetColors(bool dark, std::optional<CaptionColors> caption)
{
    const bool changed = dark_ != dark || caption_ != caption;
    dark_ = dark;
    caption_ = caption;
    if (changed && parent_) RedrawWindow(parent_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

TabPalette FolderTabs::Colors() const
{
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        const auto surface = caption_ ? caption_->surface : GetSysColor(COLOR_WINDOW);
        const auto highlight = GetSysColor(COLOR_HIGHLIGHT);
        const auto text = caption_ ? caption_->text : GetSysColor(COLOR_WINDOWTEXT);
        return {surface, surface, highlight, surface, highlight, text, text, GetSysColor(COLOR_HIGHLIGHTTEXT), highlight, true};
    }
    const TabPalette themed = dark_ ?
        TabPalette{RGB(32, 32, 32), RGB(32, 32, 32), RGB(58, 58, 58), RGB(49, 49, 49), RGB(68, 68, 68),
            RGB(201, 201, 201), RGB(198, 198, 198), RGB(240, 240, 240), RGB(80, 80, 80)} :
        TabPalette{RGB(246, 246, 246), RGB(231, 231, 231), RGB(255, 255, 255), RGB(221, 221, 221), RGB(210, 210, 210),
            RGB(66, 66, 66), RGB(80, 80, 80), RGB(32, 32, 32), RGB(200, 200, 200)};
    if (caption_) {
        const auto [surface, text] = *caption_;
        if (dark_) {
            const auto passiveText = Blend(text, surface, 18);
            return {surface, surface, themed.selected, Blend(surface, text, 16), Blend(surface, text, 24),
                passiveText, passiveText, themed.selectedText, themed.selectedBorder};
        }
        return {surface, Blend(surface, text, 11), themed.selected, Blend(surface, text, 16), Blend(surface, text, 22),
            text, text, themed.selectedText, themed.selectedBorder};
    }
    return themed;
}

bool FolderTabs::Draw(const DRAWITEMSTRUCT& item) const
{
    const HWND button = item.hwndItem;
    const auto tab = std::ranges::find_if(tabs_, [button](const Tab& t) { return t.select == button || t.close == button; });
    if (tab == tabs_.end() && button != add_ && button != more_) return false;
    const bool hasTab = tab != tabs_.end();
    const bool selected = hasTab && static_cast<size_t>(tab - tabs_.begin()) == active_;
    const bool textTab = hasTab && tab->select == button;
    const bool dragging = hasTab && drag_ && drag_->started && drag_->label == tab->select;
    const bool hovered = hovered_ == button || (hasTab && (hovered_ == tab->select || hovered_ == tab->close));
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const auto palette = Colors();
    const bool usePressedFill = dragging || (!hasTab && pressed);
    COLORREF fill = hasTab ? palette.inactive : palette.surface;
    if (usePressedFill) fill = palette.pressed;
    else if (selected) fill = palette.selected;
    else if (hovered && !disabled) fill = palette.hover;
    const bool selectedFill = selected && !usePressedFill;
    const COLORREF color = selectedFill || (palette.highContrast && usePressedFill) ? palette.selectedText : palette.text;
    const auto brush = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    const int state = SaveDC(item.hDC);
    if (state) IntersectClipRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
    SetDCBrushColor(item.hDC, palette.surface);
    FillRect(item.hDC, &item.rcItem, brush);

    RECT shape = item.rcItem;
    const int radius = MulDiv(hasTab ? 20 : 10, dpi_, 96);
    if (hasTab) {
        if (textTab) shape.right += radius;
        else shape.left -= radius;
        if (state) shape.bottom += radius / 2;
    }
    SetDCBrushColor(item.hDC, fill);
    const auto oldBrush = SelectObject(item.hDC, brush);
    const auto oldPen = SelectObject(item.hDC, GetStockObject(selected ? DC_PEN : NULL_PEN));
    if (selected) SetDCPenColor(item.hDC, palette.selectedBorder);
    RoundRect(item.hDC, shape.left, shape.top, shape.right, shape.bottom, radius, radius);
    if (hasTab && !textTab && (hovered_ == button || pressed)) {
        SelectObject(item.hDC, GetStockObject(NULL_PEN));
        RECT close = item.rcItem;
        const int size = std::min<int>(close.right - close.left, MulDiv(19, dpi_, 96));
        close.left += (close.right - close.left - size) / 2;
        close.top += (close.bottom - close.top - size) / 2;
        const COLORREF closeFill = palette.highContrast ? fill : Blend(fill, color, pressed ? 22 : 15);
        SetDCBrushColor(item.hDC, closeFill);
        Ellipse(item.hDC, close.left, close.top, close.left + size, close.top + size);
    }
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);

    wchar_t commandText[8]{};
    if (!textTab) GetWindowTextW(button, commandText, ARRAYSIZE(commandText));
    const wchar_t* text = textTab ? tab->title.c_str() : commandText;
    RECT label = item.rcItem;
    if (textTab) InflateRect(&label, -MulDiv(TabTextInset, dpi_, 96), 0);
    if (textTab) {
        const int size = MulDiv(TabIconSize, dpi_, 96);
        const auto icon = selectedFill || palette.highContrast ? color : palette.icon;
        DrawFolder(item.hDC, label.left, (label.top + label.bottom - size) / 2, dpi_, icon);
        label.left += size + MulDiv(TabIconGap, dpi_, 96);
    }
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? GetSysColor(COLOR_GRAYTEXT) : color);
    const auto oldFont = SelectObject(item.hDC, GetStockObject(DEFAULT_GUI_FONT));
    const UINT alignment = textTab ? DT_LEFT : DT_CENTER;
    if (!hasTab || textTab || selected || hovered || focused || pressed || touch_ || palette.highContrast) {
        DrawTextW(item.hDC, text, -1, &label, alignment | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(item.hDC, oldFont);
    if (focused && !(item.itemState & ODS_NOFOCUSRECT)) {
        InflateRect(&label, -2, -2);
        DrawFocusRect(item.hDC, &label);
    }
    if (tab != tabs_.end() && drag_ && drag_->target == tab->select && textTab != drag_->afterTarget) {
        RECT marker = item.rcItem;
        const int thickness = std::max(1, MulDiv(2, dpi_, 96));
        if (drag_->afterTarget) marker.left = marker.right - thickness;
        else marker.right = marker.left + thickness;
        FillRect(item.hDC, &marker, GetSysColorBrush(COLOR_HIGHLIGHT));
    }
    if (state) RestoreDC(item.hDC, state);
    return true;
}
