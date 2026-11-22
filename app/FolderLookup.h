#pragma once

#include "ExplorerTab.h"

#include <cstdint>
#include <vector>

inline constexpr UINT FolderLookupComplete = WM_APP + 3;

struct FolderLookupResult {
    std::uint64_t request;
    FolderId folder;
    HRESULT hr;
};

class FolderLookup {
public:
    void Attach(HWND window);
    HRESULT Submit(std::uint64_t request, std::wstring path) const;
    std::vector<FolderLookupResult> TakeResults() const;
    void Detach();

private:
    struct Inbox;
    struct Job;
    static unsigned __stdcall Resolve(void* context) noexcept;
    std::shared_ptr<Inbox> inbox_;
};
