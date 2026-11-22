#pragma once

#include <windows.h>

class AddressBookmarkButton {
public:
    AddressBookmarkButton() = default;
    ~AddressBookmarkButton() { Close(); }
    AddressBookmarkButton(const AddressBookmarkButton&) = delete;
    AddressBookmarkButton& operator=(const AddressBookmarkButton&) = delete;

    void Refresh(HWND root, HWND commandSink, WORD command, bool dark, bool vectorIcons);
    void Close();
    bool IsVisible() const noexcept { return frame_ && IsWindowVisible(frame_); }

private:
    static LRESULT CALLBACK FrameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR reference);
    bool CreateButton();
    void Hide();
    void RestoreAddress();

    HWND root_ = nullptr;
    HWND sink_ = nullptr;
    HWND rebar_ = nullptr;
    HWND address_ = nullptr;
    HWND search_ = nullptr;
    HWND frame_ = nullptr;
    HWND button_ = nullptr;
    RECT original_{};
    RECT adjusted_{};
    ULONGLONG nextLookup_ = 0;
    WORD command_ = 0;
    bool applied_ = false;
    bool dark_ = false;
    bool vectorIcons_ = false;
};
