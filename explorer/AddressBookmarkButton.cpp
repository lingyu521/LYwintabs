#include "AddressBookmarkButton.h"
#include "ExplorerLanguage.h"
#include "ExplorerIcons.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace {

bool ClassIs(HWND window, std::wstring_view expected)
{
    wchar_t name[64]{};
    const int length = GetClassNameW(window, name, ARRAYSIZE(name));
    return expected == std::wstring_view(name, length);
}

bool IsLeftToRight(HWND window)
{
    return !(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL);
}

RECT RectIn(HWND window, HWND parent)
{
    RECT bounds{};
    if (GetWindowRect(window, &bounds)) MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&bounds), 2);
    return bounds;
}

bool Place(HWND window, const RECT& rect, HWND order, UINT flags)
{
    return SetWindowPos(window, order, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, flags) != FALSE;
}

struct AddressControls {
    HWND rebar = nullptr;
    HWND address = nullptr;
    HWND search = nullptr;
};

AddressControls FindAddressControls(HWND root)
{
    AddressControls controls;
    EnumChildWindows(root, [](HWND window, LPARAM output) -> BOOL {
        if (!IsWindowVisible(window) || !ClassIs(window, L"Address Band Root")) return TRUE;
        const HWND parent = GetParent(window);
        if (!ClassIs(parent, L"ReBarWindow32") || !IsLeftToRight(parent) || !IsLeftToRight(window) ||
            GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
            GetWindowThreadProcessId(parent, nullptr) != GetCurrentThreadId()) return TRUE;
        HWND search = nullptr;
        while ((search = FindWindowExW(parent, search, L"UniversalSearchBand", nullptr))) {
            if (IsWindowVisible(search)) {
                *reinterpret_cast<AddressControls*>(output) = {parent, window, search};
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&controls));
    return controls;
}

bool RoomInRebar(HWND rebar, HWND address, HWND frame, const RECT& target)
{
    RECT available{};
    GetClientRect(rebar, &available);
    if (target.left < available.left || target.top < available.top ||
        target.right > available.right || target.bottom > available.bottom) return false;
    for (HWND child = GetWindow(rebar, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (child == address || child == frame || !IsWindowVisible(child)) continue;
        const RECT native = RectIn(child, rebar);
        RECT overlap{};
        if (IntersectRect(&overlap, &target, &native)) return false;
    }
    return true;
}

void DrawFallbackStar(const DRAWITEMSTRUCT& item, UINT dpi, COLORREF color)
{
    constexpr std::array<POINT, 10> outline{{{0, -100}, {29, -40}, {95, -31}, {47, 15}, {59, 81},
        {0, 50}, {-59, 81}, {-47, 15}, {-95, -31}, {-29, -40}}};
    const int width = item.rcItem.right - item.rcItem.left;
    const int height = item.rcItem.bottom - item.rcItem.top;
    const int radius = std::min({MulDiv(8, dpi, 96), width / 2 - 2, height / 2 - 2});
    if (radius <= 0) return;
    const int midX = item.rcItem.left + width / 2;
    const int midY = item.rcItem.top + height / 2;
    std::array<POINT, outline.size()> points{};
    for (size_t index = 0; index < outline.size(); ++index) {
        points[index] = {midX + MulDiv(outline[index].x, radius, 100), midY + MulDiv(outline[index].y, radius, 100)};
    }
    const HPEN pen = CreatePen(PS_SOLID, std::max(1, MulDiv(1, dpi, 96)), color);
    if (pen) {
        const auto oldPen = SelectObject(item.hDC, pen);
        const auto oldBrush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
        Polygon(item.hDC, points.data(), static_cast<int>(points.size()));
        SelectObject(item.hDC, oldBrush);
        SelectObject(item.hDC, oldPen);
        DeleteObject(pen);
    }
}

void PaintStar(HWND frame, const DRAWITEMSTRUCT& item, bool dark, bool vectorIcons)
{
    HIGHCONTRASTW contrast{sizeof(contrast)};
    const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hover = (item.itemState & ODS_HOTLIGHT) != 0;
    if (highContrast || pressed || hover || FAILED(DrawThemeParentBackground(frame, item.hDC, &item.rcItem))) {
        const COLORREF surface = highContrast ? GetSysColor(pressed ? COLOR_HIGHLIGHT : COLOR_BTNFACE) :
            (dark ? (pressed ? RGB(65, 65, 65) : RGB(48, 48, 48)) : (pressed ? RGB(220, 231, 242) : RGB(245, 245, 245)));
        const auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        SetDCBrushColor(item.hDC, surface);
        FillRect(item.hDC, &item.rcItem, brush);
    }
    const UINT dpi = GetDpiForWindow(frame);
    const COLORREF color = highContrast ? GetSysColor(pressed ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT) :
        (dark ? RGB(240, 240, 240) : RGB(66, 77, 87));
    if (!vectorIcons || !DrawExplorerIcon(item.hDC, item.rcItem, MulDiv(18, dpi, 96), ExplorerIcon::Star, color)) {
        DrawFallbackStar(item, dpi, color);
    }
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        auto focus = item.rcItem;
        InflateRect(&focus, -2, -2);
        DrawFocusRect(item.hDC, &focus);
    }
}

} // namespace

LRESULT CALLBACK AddressBookmarkButton::FrameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR reference)
{
    auto& star = *reinterpret_cast<AddressBookmarkButton*>(reference);
    if (message == WM_DRAWITEM && lParam) {
        const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item.hwndItem == star.button_) {
            PaintStar(window, item, star.dark_, star.vectorIcons_);
            return TRUE;
        }
    }
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED &&
        reinterpret_cast<HWND>(lParam) == star.button_ && star.sink_) {
        PostMessageW(star.sink_, WM_COMMAND, MAKEWPARAM(star.command_, BN_CLICKED), lParam);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        star.frame_ = nullptr;
        star.button_ = nullptr;
        RemoveWindowSubclass(window, FrameProc, subclassId);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

bool AddressBookmarkButton::CreateButton()
{
    if (frame_) return true;
    frame_ = CreateWindowExW(0, L"Static", nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, rebar_, nullptr, nullptr, nullptr);
    if (!frame_) return false;
    if (!SetWindowSubclass(frame_, FrameProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
        DestroyWindow(frame_);
        frame_ = nullptr;
        return false;
    }
    button_ = CreateWindowExW(0, L"Button", ExplorerText(L"收藏当前文件夹"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, frame_, nullptr, nullptr, nullptr);
    if (button_) return true;
    DestroyWindow(frame_);
    return false;
}

void AddressBookmarkButton::UpdateLanguage()
{
    if (button_) SetWindowTextW(button_, ExplorerText(L"收藏当前文件夹"));
}

void AddressBookmarkButton::RestoreAddress()
{
    if (!applied_) return;
    applied_ = false;
    if (GetParent(address_) == rebar_ && ClassIs(address_, L"Address Band Root")) {
        const RECT current = RectIn(address_, rebar_);
        if (EqualRect(&current, &adjusted_)) Place(address_, original_, nullptr, SWP_NOACTIVATE | SWP_NOZORDER);
    }
}

void AddressBookmarkButton::Hide()
{
    if (frame_ && IsWindowVisible(frame_)) ShowWindow(frame_, SW_HIDE);
    RestoreAddress();
}

void AddressBookmarkButton::Close()
{
    sink_ = nullptr;
    Hide();
    if (frame_) DestroyWindow(frame_);
    root_ = rebar_ = address_ = search_ = nullptr;
    nextLookup_ = 0;
}

void AddressBookmarkButton::Refresh(HWND root, HWND commandSink, WORD command, bool dark, bool vectorIcons)
{
    if (root_ != root) Close();
    root_ = root;
    sink_ = commandSink;
    command_ = command;
    const bool repaint = dark_ != dark || vectorIcons_ != vectorIcons;
    dark_ = dark;
    vectorIcons_ = vectorIcons;
    if (!root || !commandSink || IsIconic(root) || !IsLeftToRight(root)) {
        Hide();
        return;
    }

    if (!address_ || GetParent(address_) != rebar_ || GetParent(search_) != rebar_ ||
        !IsWindowVisible(address_) || !IsWindowVisible(search_) || GetAncestor(rebar_, GA_ROOT) != root) {
        Hide();
        const ULONGLONG now = GetTickCount64();
        if (now < nextLookup_) return;
        nextLookup_ = now + 800;
        const AddressControls controls = FindAddressControls(root);
        if (controls.rebar != rebar_ && frame_) DestroyWindow(frame_);
        rebar_ = controls.rebar;
        address_ = controls.address;
        search_ = controls.search;
    }
    if (!address_) return;

    const UINT dpi = GetDpiForWindow(root);
    const auto scale = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
    const RECT current = RectIn(address_, rebar_);
    const RECT search = RectIn(search_, rebar_);
    const RECT native = applied_ && EqualRect(&current, &adjusted_) ? original_ : current;
    const LONG row = native.bottom - native.top;
    const LONG middle = native.top + row / 2;
    if (IsRectEmpty(&search) || native.right > search.left || middle < search.top || middle >= search.bottom ||
        row < scale(18) || row > scale(64) || native.right - native.left < scale(150)) {
        Hide();
        return;
    }
    const int gap = scale(2);
    const int starWidth = scale(26);
    const LONG starHeight = std::min<LONG>(scale(28), row - 2 * gap);
    const LONG starTop = middle - starHeight / 2;
    const RECT slot{native.right - gap - starWidth, starTop, native.right - gap, starTop + starHeight};
    if (!RoomInRebar(rebar_, address_, frame_, slot) || !CreateButton()) {
        Hide();
        return;
    }
    RECT adjusted = native;
    adjusted.right = slot.left - gap;
    original_ = native;
    adjusted_ = adjusted;
    applied_ = EqualRect(&current, &adjusted) || Place(address_, adjusted, nullptr, SWP_NOACTIVATE | SWP_NOZORDER);
    const RECT resized = RectIn(address_, rebar_);
    if (!applied_ || !EqualRect(&resized, &adjusted)) {
        Hide();
        return;
    }
    const RECT oldSlot = RectIn(frame_, rebar_);
    const RECT child{0, 0, starWidth, starHeight};
    if (!IsVisible() || !EqualRect(&oldSlot, &slot)) {
        if (!Place(button_, child, nullptr, SWP_NOACTIVATE | SWP_NOZORDER) ||
            !Place(frame_, slot, HWND_TOP, SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
            Hide();
            return;
        }
    }
    if (repaint) InvalidateRect(button_, nullptr, FALSE);
}
