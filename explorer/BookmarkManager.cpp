#include "BookmarkManager.h"

#include "Favorites.h"
#include "Resource.h"

#include <commctrl.h>
#include <roapi.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace {

using Microsoft::WRL::ComPtr;
using ItemName = std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)>;
constexpr wchar_t BookmarkRootName[] = L"书签栏";
constexpr int MaxBookmarkName = 255;
constexpr int MaxBookmarkAddress = 32767;

std::wstring TrimText(std::wstring_view text)
{
    const size_t first = text.find_first_not_of(L" \t");
    if (first == std::wstring_view::npos) return {};
    return std::wstring(text.substr(first, text.find_last_not_of(L" \t") - first + 1));
}

std::wstring EditText(HWND dialog, int control)
{
    const HWND edit = GetDlgItem(dialog, control);
    const int length = GetWindowTextLengthW(edit);
    std::wstring result(static_cast<size_t>(length) + 1, L'\0');
    result.resize(GetWindowTextW(edit, result.data(), length + 1));
    return TrimText(result);
}

struct BookmarkNameDialog {
    std::wstring& name;
    const wchar_t* caption;
};

INT_PTR CALLBACK RenameBookmarkDialog(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        const auto& state = *reinterpret_cast<const BookmarkNameDialog*>(lParam);
        SetWindowTextW(dialog, state.caption);
        const HWND edit = GetDlgItem(dialog, IDC_FAVORITE_NAME);
        Edit_LimitText(edit, MaxBookmarkName);
        Edit_SetText(edit, state.name.c_str());
        Edit_SetSel(edit, 0, -1);
        SetFocus(edit);
        return FALSE;
    }
    if (message != WM_COMMAND) return FALSE;
    if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (LOWORD(wParam) != IDOK) return FALSE;

    try {
        auto name = EditText(dialog, IDC_FAVORITE_NAME);
        if (name.empty()) {
            MessageBeep(MB_ICONWARNING);
            return TRUE;
        }
        auto& state = *reinterpret_cast<BookmarkNameDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
        state.name = std::move(name);
        EndDialog(dialog, IDOK);
    } catch (...) {
        EndDialog(dialog, IDCANCEL);
    }
    return TRUE;
}

bool ShowBookmarkName(HWND owner, HINSTANCE instance, std::wstring& name, const wchar_t* caption)
{
    BookmarkNameDialog state{name, caption};
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_RENAME_FAVORITE), owner, RenameBookmarkDialog,
        reinterpret_cast<LPARAM>(&state)) == IDOK;
}

HRESULT PickBookmarkFolder(HWND owner, Favorite& item)
{
    ComPtr<IFileOpenDialog> picker;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker));
    if (FAILED(hr)) return hr;

    FILEOPENDIALOGOPTIONS options = 0;
    hr = picker->GetOptions(&options);
    if (FAILED(hr)) return hr;
    hr = picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT);
    if (FAILED(hr)) return hr;
    picker->SetTitle(L"选择书签文件夹");
    hr = picker->Show(owner);
    if (FAILED(hr)) return hr;

    ComPtr<IShellItem> selected;
    hr = picker->GetResult(&selected);
    if (FAILED(hr)) return hr;
    PWSTR path = nullptr;
    hr = selected->GetDisplayName(SIGDN_FILESYSPATH, &path);
    const ItemName pathText(path, CoTaskMemFree);
    if (FAILED(hr)) return hr;
    if (!pathText || !*pathText) return E_FAIL;

    PWSTR name = nullptr;
    hr = selected->GetDisplayName(SIGDN_NORMALDISPLAY, &name);
    const ItemName nameText(name, CoTaskMemFree);
    item.path = pathText.get();
    item.name = SUCCEEDED(hr) && nameText && *nameText ? nameText.get() : item.path;
    return S_OK;
}

std::wstring NameFromPath(std::wstring_view path)
{
    const auto end = path.find_last_not_of(L'\\');
    const auto separator = path.find_last_of(L'\\', end);
    const size_t start = separator == std::wstring_view::npos ? 0 : separator + 1;
    return std::wstring(path.substr(start, end - start + 1));
}

bool IsKnownLocalFile(const std::wstring& path)
{
    std::wstring_view drivePath = path;
    if (drivePath.starts_with(L"\\\\?\\")) drivePath.remove_prefix(4);
    if (drivePath.size() < 3 || drivePath[1] != L':' || drivePath[2] != L'\\') return false;
    const wchar_t drive[]{drivePath[0], L':', L'\\', L'\0'};
    if (GetDriveTypeW(drive) != DRIVE_FIXED) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

using FolderChildren = std::unordered_map<std::wstring_view, std::vector<size_t>>;

FolderChildren IndexFolders(const std::vector<Favorite>& items)
{
    FolderChildren children;
    for (size_t index = 0; index < items.size(); ++index) {
        if (items[index].IsFolder()) children[items[index].parentId].push_back(index);
    }
    return children;
}

bool PopulateDestinations(HWND combo, const std::vector<Favorite>& items, std::wstring_view selectedId)
{
    const int root = ComboBox_AddString(combo, BookmarkRootName);
    if (root < 0 || ComboBox_SetItemData(combo, root, 0) == CB_ERR) return false;
    int selected = root;
    const auto children = IndexFolders(items);
    const auto append = [&](auto&& self, std::wstring_view parent, const std::wstring& label) -> bool {
        const auto found = children.find(parent);
        if (found == children.end()) return true;
        for (size_t index : found->second) {
            const auto& folder = items[index];
            const std::wstring title = label + L"  ›  " + folder.name;
            const int row = ComboBox_AddString(combo, title.c_str());
            if (row < 0 || ComboBox_SetItemData(combo, row, static_cast<LPARAM>(index + 1)) == CB_ERR) return false;
            if (folder.id == selectedId) selected = row;
            if (!self(self, folder.id, title)) return false;
        }
        return true;
    };
    return append(append, {}, BookmarkRootName) && ComboBox_SetCurSel(combo, selected) != CB_ERR;
}

struct BookmarkEditor {
    Favorite& bookmark;
    const std::vector<Favorite>* destinations;
};

bool InitializeEditor(HWND dialog, const BookmarkEditor& state)
{
    const auto& bookmark = state.bookmark;
    if (state.destinations && !PopulateDestinations(GetDlgItem(dialog, IDC_BOOKMARK_DESTINATION),
            *state.destinations, bookmark.parentId)) return false;
    if (bookmark.id.empty()) SetWindowTextW(dialog, L"添加书签");
    Edit_LimitText(GetDlgItem(dialog, IDC_BOOKMARK_NAME), MaxBookmarkName);
    Edit_LimitText(GetDlgItem(dialog, IDC_BOOKMARK_ADDRESS), MaxBookmarkAddress);
    SetDlgItemTextW(dialog, IDC_BOOKMARK_NAME, bookmark.name.c_str());
    const HWND address = GetDlgItem(dialog, IDC_BOOKMARK_ADDRESS);
    Edit_SetText(address, bookmark.path.c_str());
    Edit_SetSel(address, 0, -1);
    SetFocus(address);
    return true;
}

std::wstring DestinationId(HWND dialog, const std::vector<Favorite>& items)
{
    const HWND combo = GetDlgItem(dialog, IDC_BOOKMARK_DESTINATION);
    const LRESULT data = ComboBox_GetItemData(combo, ComboBox_GetCurSel(combo));
    if (data >= 1 && static_cast<size_t>(data) <= items.size()) {
        return items[static_cast<size_t>(data) - 1].id;
    }
    return {};
}

INT_PTR CALLBACK BookmarkEditorProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG) SetWindowLongPtrW(dialog, DWLP_USER, lParam);
    auto* state = reinterpret_cast<BookmarkEditor*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!state) return FALSE;
    try {
        if (message == WM_INITDIALOG) {
            if (!InitializeEditor(dialog, *state)) EndDialog(dialog, -1);
            return FALSE;
        }
        if (message != WM_COMMAND) return FALSE;
        switch (LOWORD(wParam)) {
        case IDC_BOOKMARK_BROWSE: {
            Favorite picked;
            const HRESULT result = PickBookmarkFolder(dialog, picked);
            if (SUCCEEDED(result)) {
                SetDlgItemTextW(dialog, IDC_BOOKMARK_ADDRESS, picked.path.c_str());
                if (EditText(dialog, IDC_BOOKMARK_NAME).empty()) {
                    SetDlgItemTextW(dialog, IDC_BOOKMARK_NAME, picked.name.c_str());
                }
            } else if (result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
                MessageBoxW(dialog, L"无法选择文件夹。", L"LYwintabs", MB_ICONWARNING);
            }
            return TRUE;
        }
        case IDOK: {
            auto path = EditText(dialog, IDC_BOOKMARK_ADDRESS);
            if (path.size() > 1 && path.front() == L'"' && path.back() == L'"') {
                path = path.substr(1, path.size() - 2);
            }
            std::ranges::replace(path, L'/', L'\\');
            if (!IsBookmarkPath(path)) {
                MessageBoxW(dialog, L"请输入完整的文件夹地址，例如 C:\\项目 或 \\\\服务器\\共享。",
                    L"LYwintabs", MB_ICONWARNING);
                const HWND address = GetDlgItem(dialog, IDC_BOOKMARK_ADDRESS);
                SetFocus(address);
                Edit_SetSel(address, 0, -1);
                return TRUE;
            }
            if (IsKnownLocalFile(path)) {
                MessageBoxW(dialog, L"所填地址是文件。请选择文件夹地址。", L"LYwintabs", MB_ICONWARNING);
                const HWND address = GetDlgItem(dialog, IDC_BOOKMARK_ADDRESS);
                SetFocus(address);
                Edit_SetSel(address, 0, -1);
                return TRUE;
            }
            auto name = EditText(dialog, IDC_BOOKMARK_NAME);
            auto& bookmark = state->bookmark;
            bookmark.name = name.empty() ? NameFromPath(path) : std::move(name);
            bookmark.path = std::move(path);
            if (state->destinations) bookmark.parentId = DestinationId(dialog, *state->destinations);
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    } catch (...) {
        MessageBoxW(dialog, L"无法完成书签编辑。", L"LYwintabs", MB_ICONWARNING);
        if (message == WM_INITDIALOG) EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
}

bool EditBookmark(HWND owner, HINSTANCE instance, Favorite& bookmark,
    const std::vector<Favorite>* destinations = nullptr)
{
    BookmarkEditor state{bookmark, destinations};
    const auto resource = MAKEINTRESOURCEW(destinations ? IDD_BOOKMARK_QUICK_ADD : IDD_BOOKMARK_EDITOR);
    const auto result = DialogBoxParamW(instance, resource, owner, BookmarkEditorProc, reinterpret_cast<LPARAM>(&state));
    if (result == -1) MessageBoxW(owner, L"无法打开书签编辑窗口。", L"LYwintabs", MB_ICONWARNING);
    return result == IDOK;
}

HTREEITEM InsertFolder(HWND tree, HTREEITEM parent, const wchar_t* name, LPARAM index)
{
    TVINSERTSTRUCTW entry{};
    entry.hParent = parent;
    entry.hInsertAfter = TVI_LAST;
    entry.item.mask = TVIF_TEXT | TVIF_PARAM;
    entry.item.pszText = const_cast<LPWSTR>(name);
    entry.item.lParam = index;
    return TreeView_InsertItem(tree, &entry);
}

bool PopulateFolders(HWND tree, const std::vector<Favorite>& items, std::wstring_view selectedId,
    std::wstring_view omittedId = {})
{
    TreeView_DeleteAllItems(tree);
    const HTREEITEM root = InsertFolder(tree, TVI_ROOT, BookmarkRootName, 0);
    if (!root) return false;
    HTREEITEM selection = root;
    const auto children = IndexFolders(items);
    const auto append = [&](auto&& self, HTREEITEM parent, std::wstring_view parentId) -> void {
        const auto found = children.find(parentId);
        if (found == children.end()) return;
        for (size_t index : found->second) {
            const auto& folder = items[index];
            if (folder.id == omittedId) continue;
            const HTREEITEM branch = InsertFolder(tree, parent, folder.name.c_str(), static_cast<LPARAM>(index + 1));
            if (!branch) continue;
            if (folder.id == selectedId) selection = branch;
            self(self, branch, folder.id);
        }
    };
    append(append, root, {});
    TreeView_Expand(tree, root, TVE_EXPAND);
    TreeView_SelectItem(tree, selection);
    TreeView_EnsureVisible(tree, selection);
    return true;
}

std::wstring TreeFolderId(HWND tree, HTREEITEM item, const std::vector<Favorite>& items)
{
    TVITEMW node{};
    node.hItem = item;
    node.mask = TVIF_PARAM;
    if (!node.hItem || !TreeView_GetItem(tree, &node) ||
        node.lParam < 1 || static_cast<size_t>(node.lParam) > items.size()) return {};
    return items[static_cast<size_t>(node.lParam) - 1].id;
}

std::wstring SelectedFolderId(HWND tree, const std::vector<Favorite>& items)
{
    return TreeFolderId(tree, TreeView_GetSelection(tree), items);
}

struct MoveBookmarkDialog {
    const std::vector<Favorite>& items;
    const Favorite& item;
    std::wstring destination;
};

INT_PTR CALLBACK MoveBookmarkProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG) SetWindowLongPtrW(dialog, DWLP_USER, lParam);
    auto* state = reinterpret_cast<MoveBookmarkDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!state) return FALSE;
    const HWND tree = GetDlgItem(dialog, IDC_BOOKMARK_MOVE_TREE);
    try {
        if (message == WM_INITDIALOG) {
            if (!PopulateFolders(tree, state->items, state->item.parentId,
                    state->item.IsFolder() ? std::wstring_view(state->item.id) : std::wstring_view{})) {
                EndDialog(dialog, IDCANCEL);
            }
            return TRUE;
        }
        if (message != WM_COMMAND) return FALSE;
        switch (LOWORD(wParam)) {
        case IDOK:
            state->destination = SelectedFolderId(tree, state->items);
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    } catch (...) {
        MessageBoxW(dialog, L"无法选择目标书签文件夹。", L"LYwintabs", MB_ICONWARNING);
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
}

class BookmarkManager {
public:
    BookmarkManager(HINSTANCE instance, const Favorites& store) : instance_(instance), store_(store) {}

    bool Show(HWND owner)
    {
        if (FAILED(store_.Load(items_))) {
            MessageBoxW(owner, L"无法读取书签。", L"LYwintabs", MB_ICONWARNING);
            return false;
        }
        if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_BOOKMARK_MANAGER), owner, DialogProc,
                reinterpret_cast<LPARAM>(this)) == -1) {
            MessageBoxW(owner, L"无法打开书签管理器。", L"LYwintabs", MB_ICONWARNING);
        }
        return changed_;
    }

private:
    static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_INITDIALOG) SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        auto* manager = reinterpret_cast<BookmarkManager*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (!manager) return FALSE;
        try {
            return manager->HandleMessage(dialog, message, wParam, lParam);
        } catch (...) {
            MessageBoxW(dialog, L"无法完成书签操作。", L"LYwintabs", MB_ICONWARNING);
            if (message == WM_INITDIALOG) EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }

    INT_PTR HandleMessage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_INITDIALOG) {
            list_ = GetDlgItem(dialog, IDC_BOOKMARK_LIST);
            tree_ = GetDlgItem(dialog, IDC_BOOKMARK_TREE);
            if (!list_ || !tree_ || !InitializeList() || !RebuildView(dialog)) {
                EndDialog(dialog, -1);
                return FALSE;
            }
            return TRUE;
        }
        if (message == WM_NOTIFY && lParam && !rebuilding_) {
            const auto& notice = *reinterpret_cast<const NMHDR*>(lParam);
            if (notice.hwndFrom == tree_ && notice.code == TVN_SELCHANGEDW) {
                currentFolder_ = SelectedFolderId(tree_, items_);
                FillList(dialog);
            } else if (notice.hwndFrom == list_ && notice.code == LVN_ITEMCHANGED) {
                EnableSelectionActions(dialog);
            } else if (notice.hwndFrom == list_ && notice.code == LVN_BEGINDRAG) {
                BeginDrag(dialog, reinterpret_cast<const NMLISTVIEW*>(lParam)->iItem);
                return TRUE;
            } else if (notice.hwndFrom == list_ && notice.code == NM_RCLICK && !drag_) {
                ShowItemMenu(dialog, reinterpret_cast<const NMITEMACTIVATE*>(lParam)->iItem);
                return TRUE;
            } else if (notice.hwndFrom == list_ && notice.code == NM_CUSTOMDRAW && drag_) {
                SetWindowLongPtrW(dialog, DWLP_MSGRESULT, DrawDropMark(*reinterpret_cast<const NMLVCUSTOMDRAW*>(lParam)));
                return TRUE;
            } else if (notice.hwndFrom == list_ && (notice.code == NM_RETURN ||
                    (notice.code == NM_DBLCLK && reinterpret_cast<const NMITEMACTIVATE*>(lParam)->iItem >= 0))) {
                Activate(dialog);
                return TRUE;
            }
        }
        if (drag_) {
            if (message == WM_MOUSEMOVE || message == WM_LBUTTONUP) {
                POINT pointer{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(dialog, &pointer);
                UpdateDrag(pointer);
                if (message == WM_LBUTTONUP) FinishDrag(dialog, true);
                return TRUE;
            }
            if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED || message == WM_DESTROY ||
                (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
                FinishDrag(dialog, false);
                return message == WM_COMMAND;
            }
        }
        if (message != WM_COMMAND) return FALSE;

        switch (LOWORD(wParam)) {
        case IDOK:
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        case IDC_BOOKMARK_ADD:
            AddBookmark(dialog);
            return TRUE;
        case IDC_BOOKMARK_NEW_FOLDER:
            AddFolder(dialog);
            return TRUE;
        case IDC_BOOKMARK_EDIT:
            Edit(dialog);
            return TRUE;
        case IDC_BOOKMARK_MOVE:
            Move(dialog);
            return TRUE;
        case IDC_BOOKMARK_REMOVE:
            Remove(dialog);
            return TRUE;
        default:
            return FALSE;
        }
    }

    bool InitializeList()
    {
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        RECT bounds{};
        GetClientRect(list_, &bounds);
        const int usableWidth = bounds.right - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(list_));
        const int nameWidth = usableWidth * 2 / 5;
        wchar_t name[] = L"名称";
        wchar_t path[] = L"文件夹地址";
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = name;
        column.cx = nameWidth;
        if (ListView_InsertColumn(list_, 0, &column) == -1) return false;
        column.pszText = path;
        column.cx = usableWidth - nameWidth;
        return ListView_InsertColumn(list_, 1, &column) != -1;
    }

    bool RebuildView(HWND dialog, std::wstring_view selection = {})
    {
        rebuilding_ = true;
        const bool populated = PopulateFolders(tree_, items_, currentFolder_);
        rebuilding_ = false;
        if (!populated) return false;
        currentFolder_ = SelectedFolderId(tree_, items_);
        FillList(dialog, selection);
        return true;
    }

    void FillList(HWND dialog, std::wstring_view selection = {})
    {
        const std::wstring location = FolderLocation();
        SetDlgItemTextW(dialog, IDC_BOOKMARK_LOCATION, location.c_str());
        ListView_DeleteAllItems(list_);
        int row = 0;
        int selected = -1;
        for (size_t index = 0; index < items_.size(); ++index) {
            auto& item = items_[index];
            if (item.parentId != currentFolder_) continue;
            LVITEMW entry{};
            entry.mask = LVIF_TEXT | LVIF_PARAM;
            entry.iItem = row++;
            entry.pszText = item.name.data();
            entry.lParam = static_cast<LPARAM>(index);
            const int inserted = ListView_InsertItem(list_, &entry);
            if (inserted == -1) break;
            wchar_t folderLabel[] = L"书签文件夹";
            ListView_SetItemText(list_, inserted, 1, item.IsFolder() ? folderLabel : item.path.data());
            if (item.id == selection) selected = inserted;
        }
        if (selected >= 0) {
            ListView_SetItemState(list_, selected, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
            ListView_EnsureVisible(list_, selected, FALSE);
        }
        EnableSelectionActions(dialog);
    }

    std::wstring FolderLocation() const
    {
        std::wstring path;
        std::wstring_view parent = currentFolder_;
        while (!parent.empty()) {
            const auto folder = std::ranges::find(items_, parent, &Favorite::id);
            if (folder == items_.end()) break;
            path = L"  ›  " + folder->name + path;
            parent = folder->parentId;
        }
        return BookmarkRootName + path;
    }

    const Favorite* ItemAtRow(int row) const
    {
        LVITEMW item{};
        item.iItem = row;
        item.mask = LVIF_PARAM;
        if (item.iItem < 0 || !ListView_GetItem(list_, &item) || item.lParam < 0 ||
            static_cast<size_t>(item.lParam) >= items_.size()) return nullptr;
        return &items_[static_cast<size_t>(item.lParam)];
    }

    const Favorite* SelectedItem() const
    {
        return ItemAtRow(ListView_GetNextItem(list_, -1, LVNI_SELECTED));
    }

    struct Drag {
        std::wstring sourceId;
        int row = -1;
        HTREEITEM folder = nullptr;
        BookmarkDrop drop = BookmarkDrop::Before;
    };

    void BeginDrag(HWND dialog, int row)
    {
        const auto* item = ItemAtRow(row);
        if (!item || drag_) return;
        drag_ = Drag{item->id};
        SetCapture(dialog);
        if (GetCapture() != dialog) drag_.reset();
    }

    bool DropIntoFolder(const Favorite& source, std::wstring_view target) const
    {
        for (auto folder = target; !folder.empty();) {
            if (folder == source.id) return false;
            const auto parent = std::ranges::find(items_, folder, &Favorite::id);
            if (parent == items_.end() || !parent->IsFolder()) return false;
            folder = parent->parentId;
        }
        return true;
    }

    void UpdateDrag(POINT screen)
    {
        const auto source = std::ranges::find(items_, drag_->sourceId, &Favorite::id);
        if (source == items_.end()) return;
        int row = -1;
        HTREEITEM folder = nullptr;
        BookmarkDrop drop = BookmarkDrop::Before;
        RECT bounds{};
        GetWindowRect(tree_, &bounds);
        if (PtInRect(&bounds, screen)) {
            TVHITTESTINFO hit{screen};
            ScreenToClient(tree_, &hit.pt);
            TreeView_HitTest(tree_, &hit);
            if (hit.hItem && (hit.flags & TVHT_ONITEM)) {
                const auto destination = TreeFolderId(tree_, hit.hItem, items_);
                if (destination != source->parentId && DropIntoFolder(*source, destination)) folder = hit.hItem;
            }
        }
        GetWindowRect(list_, &bounds);
        if (PtInRect(&bounds, screen)) {
            LVHITTESTINFO hit{screen};
            ScreenToClient(list_, &hit.pt);
            row = ListView_SubItemHitTest(list_, &hit);
            RECT item{};
            if (row >= 0 && ListView_GetItemRect(list_, row, &item, LVIR_BOUNDS)) {
                const auto* target = ItemAtRow(row);
                if (!target || target == &*source) row = -1;
                else drop = BookmarkDropZone(target->IsFolder(), hit.pt.y - item.top, item.bottom - item.top);
            } else {
                row = ListView_GetItemCount(list_) - 1;
                if (!ListView_GetItemRect(list_, row, &item, LVIR_BOUNDS) || hit.pt.y < item.bottom ||
                    ItemAtRow(row) == &*source) row = -1;
                drop = BookmarkDrop::After;
            }
        }
        if (drag_->folder != folder) {
            drag_->folder = folder;
            TreeView_SelectDropTarget(tree_, folder);
        }
        if (drag_->row != row || drag_->drop != drop) {
            drag_->row = row;
            drag_->drop = drop;
            InvalidateRect(list_, nullptr, FALSE);
        }
    }

    LONG_PTR DrawDropMark(const NMLVCUSTOMDRAW& paint) const
    {
        if (paint.nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYPOSTPAINT;
        if (paint.nmcd.dwDrawStage != CDDS_POSTPAINT || drag_->row < 0) return CDRF_DODEFAULT;
        RECT item{};
        RECT client{};
        ListView_GetItemRect(list_, drag_->row, &item, LVIR_BOUNDS);
        GetClientRect(list_, &client);
        item.left = 0;
        item.right = client.right;
        const auto brush = GetSysColorBrush(COLOR_HIGHLIGHT);
        if (drag_->drop == BookmarkDrop::Into) {
            FrameRect(paint.nmcd.hdc, &item, brush);
            InflateRect(&item, -1, -1);
            FrameRect(paint.nmcd.hdc, &item, brush);
        } else {
            const int thickness = MulDiv(2, GetDpiForWindow(list_), 96);
            if (drag_->drop == BookmarkDrop::Before) item.bottom = item.top + thickness;
            else item.top = item.bottom - thickness;
            FillRect(paint.nmcd.hdc, &item, brush);
        }
        return CDRF_DODEFAULT;
    }

    void FinishDrag(HWND dialog, bool commit)
    {
        const auto drag = std::exchange(drag_, std::nullopt);
        TreeView_SelectDropTarget(tree_, nullptr);
        InvalidateRect(list_, nullptr, FALSE);
        if (GetCapture() == dialog) ReleaseCapture();
        if (!commit || (!drag->folder && drag->row < 0)) return;
        const auto source = std::ranges::find(items_, drag->sourceId, &Favorite::id);
        if (source == items_.end()) return;
        Favorite item = *source;
        std::wstring anchorId;
        if (drag->folder) {
            item.parentId = TreeFolderId(tree_, drag->folder, items_);
        } else {
            const auto* target = ItemAtRow(drag->row);
            if (!target) return;
            if (drag->drop == BookmarkDrop::Into) item.parentId = target->id;
            else anchorId = target->id;
        }
        if (Save(dialog, FavoriteChange::Move, item, {anchorId, drag->drop == BookmarkDrop::After})) {
            currentFolder_ = item.parentId;
            RebuildView(dialog, item.id);
        }
    }

    void EnableSelectionActions(HWND dialog) const
    {
        const bool hasSelection = SelectedItem() != nullptr;
        EnableWindow(GetDlgItem(dialog, IDC_BOOKMARK_EDIT), hasSelection);
        EnableWindow(GetDlgItem(dialog, IDC_BOOKMARK_MOVE), hasSelection);
        EnableWindow(GetDlgItem(dialog, IDC_BOOKMARK_REMOVE), hasSelection);
    }

    bool Save(HWND dialog, FavoriteChange change, const Favorite& item, BookmarkPosition position = {})
    {
        const HRESULT hr = store_.Update(change, item, items_, position);
        if (hr == S_OK) {
            changed_ = true;
            return true;
        }
        if (hr == S_FALSE && change == FavoriteChange::Move &&
            std::ranges::find(items_, item.id, &Favorite::id) != items_.end()) {
            RebuildView(dialog, item.id);
            return false;
        }
        const wchar_t* error = L"无法保存书签。";
        if (hr == HRESULT_FROM_WIN32(ERROR_DIR_NOT_EMPTY)) {
            error = L"请先移出这个书签文件夹中的项目，再移除文件夹。";
        } else if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) || (hr == S_FALSE && change == FavoriteChange::Add)) {
            error = item.IsFolder() ? L"这里已有同名的书签文件夹。" : L"目标书签文件夹中已有相同地址的书签。";
        } else if (hr == HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE)) {
            error = L"书签文件夹的嵌套层数已达上限。";
        } else if (hr == HRESULT_FROM_WIN32(ERROR_DIRECTORY) || hr == HRESULT_FROM_WIN32(ERROR_CIRCULAR_DEPENDENCY)
            || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
            || hr == S_FALSE) {
            error = L"书签或目标文件夹已更改。请重新选择后再试。";
        }
        if (hr != S_FALSE) {
            std::vector<Favorite> current;
            if (SUCCEEDED(store_.Load(current))) items_.swap(current);
        }
        RebuildView(dialog);
        MessageBoxW(dialog, error, L"LYwintabs", MB_ICONWARNING);
        return false;
    }

    void AddBookmark(HWND dialog)
    {
        Favorite bookmark;
        bookmark.parentId = currentFolder_;
        if (EditBookmark(dialog, instance_, bookmark) && Save(dialog, FavoriteChange::Add, bookmark)) {
            RebuildView(dialog, items_.back().id);
        }
    }

    void AddFolder(HWND dialog)
    {
        Favorite folder;
        folder.parentId = currentFolder_;
        if (ShowBookmarkFolderName(dialog, instance_, folder.name) &&
            Save(dialog, FavoriteChange::Add, folder)) {
            RebuildView(dialog, items_.back().id);
        }
    }

    void Edit(HWND dialog)
    {
        const auto* selected = SelectedItem();
        if (!selected) return;
        Favorite item = *selected;
        const auto change = item.IsFolder() ? FavoriteChange::Rename : FavoriteChange::Update;
        const bool saved = ShowBookmarkEdit(dialog, instance_, item);
        if (saved && (item.name != selected->name || item.path != selected->path) && Save(dialog, change, item)) {
            RebuildView(dialog, item.id);
        }
    }

    void Activate(HWND dialog)
    {
        const auto* selected = SelectedItem();
        if (!selected) return;
        if (selected->IsFolder()) {
            currentFolder_ = selected->id;
            RebuildView(dialog);
        } else {
            Edit(dialog);
        }
    }

    void ShowItemMenu(HWND dialog, int row)
    {
        const auto* item = ItemAtRow(row);
        if (!item) return;
        const bool folder = item->IsFolder();
        enum : UINT { Expand = 1, EditItem };
        using Menu = std::unique_ptr<std::remove_pointer_t<HMENU>, decltype(&DestroyMenu)>;
        const Menu menu(CreatePopupMenu(), DestroyMenu);
        if (!menu) return;
        ListView_SetItemState(list_, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        if (folder) AppendMenuW(menu.get(), MF_STRING, Expand, L"展开文件夹");
        AppendMenuW(menu.get(), MF_STRING, EditItem, L"编辑…");
        const DWORD position = GetMessagePos();
        const UINT action = TrackPopupMenu(menu.get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            GET_X_LPARAM(position), GET_Y_LPARAM(position), 0, dialog, nullptr);
        if (action == Expand) Activate(dialog);
        else if (action == EditItem) Edit(dialog);
    }

    void Move(HWND dialog)
    {
        const auto* selected = SelectedItem();
        if (!selected) return;
        Favorite item = *selected;
        MoveBookmarkDialog state{items_, item, {}};
        if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_BOOKMARK_MOVE), dialog, MoveBookmarkProc,
                reinterpret_cast<LPARAM>(&state)) != IDOK || state.destination == item.parentId) return;
        item.parentId = std::move(state.destination);
        if (Save(dialog, FavoriteChange::Move, item)) {
            currentFolder_ = item.parentId;
            RebuildView(dialog, item.id);
        }
    }

    void Remove(HWND dialog)
    {
        const auto* selected = SelectedItem();
        if (!selected) return;
        const Favorite item = *selected;
        if (Save(dialog, FavoriteChange::Remove, item)) RebuildView(dialog);
    }

    HINSTANCE instance_;
    const Favorites& store_;
    HWND list_ = nullptr;
    HWND tree_ = nullptr;
    std::vector<Favorite> items_;
    std::wstring currentFolder_;
    std::optional<Drag> drag_;
    bool changed_ = false;
    bool rebuilding_ = false;
};

struct WinRtApartment {
    HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
    ~WinRtApartment() { if (SUCCEEDED(hr)) RoUninitialize(); }
};

} // namespace

bool ShowBookmarkManager(HWND owner, HINSTANCE instance, const Favorites& store)
{
    try {
        const WinRtApartment apartment;
        const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES};
        if (SUCCEEDED(apartment.hr) && InitCommonControlsEx(&controls)) {
            BookmarkManager manager(instance, store);
            return manager.Show(owner);
        }
    } catch (...) {
        // Explorer dialog callbacks must not propagate failures through Windows.
    }
    MessageBoxW(owner, L"无法打开书签管理器。", L"LYwintabs", MB_ICONWARNING);
    return false;
}

bool ShowBookmarkEdit(HWND owner, HINSTANCE instance, Favorite& bookmark)
{
    return bookmark.IsFolder() ? ShowBookmarkName(owner, instance, bookmark.name, L"编辑书签文件夹") :
        EditBookmark(owner, instance, bookmark);
}

bool ShowBookmarkFolderName(HWND owner, HINSTANCE instance, std::wstring& name)
{
    return ShowBookmarkName(owner, instance, name, L"新建书签文件夹");
}

bool ShowBookmarkAdd(HWND owner, HINSTANCE instance, const Favorites& store, Favorite bookmark)
{
    try {
        const WinRtApartment apartment;
        std::vector<Favorite> items;
        if (SUCCEEDED(apartment.hr) && SUCCEEDED(store.Load(items))) {
            bookmark.id.clear();
            if (!EditBookmark(owner, instance, bookmark, &items)) return false;
            const HRESULT hr = store.Update(FavoriteChange::Add, bookmark, items);
            if (hr == S_OK) return true;
            if (hr == S_FALSE) {
                MessageBoxW(owner, L"这个地址已经收藏到所选的书签文件夹。", L"LYwintabs", MB_ICONINFORMATION);
                return false;
            }
        }
    } catch (...) {
        // Explorer dialog callbacks must not propagate failures through Windows.
    }
    MessageBoxW(owner, L"无法保存书签。", L"LYwintabs", MB_ICONWARNING);
    return false;
}
