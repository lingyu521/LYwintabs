#pragma once

#include <shlobj.h>
#include <wrl/client.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

struct ShellMemory {
    void operator()(void* pointer) const noexcept { CoTaskMemFree(pointer); }
};

using FolderId = std::unique_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>, ShellMemory>;

HRESULT ReadCurrentFolder(IShellView& view, FolderId& id);

struct CaptionColors {
    COLORREF surface;
    COLORREF text;
    bool operator==(const CaptionColors&) const = default;
};

struct TabPalette {
    COLORREF surface;
    COLORREF inactive;
    COLORREF selected;
    COLORREF hover;
    COLORREF pressed;
    COLORREF text;
    COLORREF icon;
    COLORREF selectedText;
    COLORREF selectedBorder;
    bool highContrast = false;
};

class FolderTabs {
public:
    bool Initialize(HWND container, HWND root, bool dark);
    ~FolderTabs();
    void SetBrowser(IShellBrowser* browser) { browser_ = browser; }
    bool HasBrowser() const { return browser_ != nullptr; }
    void Observe();
    void SetLayoutOptions(int leadingWidthDip, int topPaddingDip);
    void Layout(int width, int height, UINT dpi);
    bool Command(HWND source);
    bool ContextMenu(HWND source, LPARAM point);
    bool Notify(NMHDR& message);
    bool OpenFolderInBackground(const wchar_t* path);
    bool Draw(const DRAWITEMSTRUCT& item) const;
    void SetColors(bool dark, std::optional<CaptionColors> caption);
    TabPalette Colors() const;

private:
    struct Tab {
        FolderId folder;
        std::wstring title;
        HWND select;
        HWND close;
    };

    enum class Action { Select, CloseActive, OpenNew };

    struct Navigation {
        size_t destination;
        ULONGLONG deadline;
        Action action;
    };

    struct Drag {
        HWND label;
        LONG startX;
        HWND target = nullptr;
        bool started = false;
        bool afterTarget = false;
    };

    bool Insert(FolderId folder, size_t position);
    void Activate(size_t destination, Action action = Action::Select);
    void NewTab();
    void Duplicate(size_t index);
    void CloseTab(size_t index);
    void Remove(size_t index);
    void Commit(size_t destination, Action action);
    void CancelNavigation(const Navigation& navigation);
    void ShowAllTabs();
    HWND Button(const wchar_t* label);
    static LRESULT CALLBACK TabButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR context);
    bool UpdateDrag(HWND window, LPARAM pointer);
    void FinishDrag(bool drop);
    void InvalidateTab(HWND label) const;
    void SetHoveredControl(HWND control);

    Microsoft::WRL::ComPtr<IShellBrowser> browser_;
    std::vector<Tab> tabs_;
    std::optional<Navigation> pending_;
    std::optional<Drag> drag_;
    HWND parent_ = nullptr;
    HWND root_ = nullptr;
    HWND add_ = nullptr;
    HWND more_ = nullptr;
    HWND tooltip_ = nullptr;
    HWND hovered_ = nullptr;
    std::wstring tooltipText_;
    size_t active_ = 0;
    int width_ = 0;
    int height_ = 0;
    int leadingWidthDip_ = 0;
    int topPaddingDip_ = 5;
    UINT dpi_ = 96;
    bool dark_ = false;
    bool touch_ = false;
    std::optional<CaptionColors> caption_;
};
