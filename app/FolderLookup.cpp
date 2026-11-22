#include "FolderLookup.h"

#include <process.h>

#include <mutex>
#include <utility>

struct FolderLookup::Inbox {
    std::mutex lock;
    HWND window = nullptr;
    std::vector<FolderLookupResult> ready;
    size_t pending = 0;
};

struct FolderLookup::Job {
    std::shared_ptr<Inbox> inbox;
    std::uint64_t request;
    std::wstring path;
};

void FolderLookup::Attach(HWND window)
{
    inbox_ = std::make_shared<Inbox>();
    inbox_->window = window;
}

HRESULT FolderLookup::Submit(std::uint64_t request, std::wstring path) const
{
    auto job = std::make_unique<Job>(inbox_, request, std::move(path));
    std::lock_guard guard(inbox_->lock);
    if (inbox_->pending >= 16) return HRESULT_FROM_WIN32(ERROR_BUSY);
    // A dedicated thread starts with a fresh COM apartment. Shared pool threads
    // can arrive with apartment state established by unrelated code.
    auto* context = job.release();
    const auto worker = _beginthreadex(nullptr, 0, Resolve, context, 0, nullptr);
    if (!worker) {
        delete context;
        return HRESULT_FROM_WIN32(ERROR_NO_SYSTEM_RESOURCES);
    }
    ++inbox_->pending;
    CloseHandle(reinterpret_cast<HANDLE>(worker));
    return S_OK;
}

unsigned __stdcall FolderLookup::Resolve(void* context) noexcept
{
    const std::unique_ptr<Job> job(static_cast<Job*>(context));
    auto& inbox = *job->inbox;
    FolderLookupResult result{job->request, nullptr, CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)};
    if (SUCCEEDED(result.hr)) {
        result.hr = ExplorerTab::ParseFolder(job->path, result.folder);
        CoUninitialize();
    }

    std::lock_guard guard(inbox.lock);
    --inbox.pending;
    if (!inbox.window) return 0;
    try {
        inbox.ready.push_back(std::move(result));
        PostMessageW(inbox.window, FolderLookupComplete, 0, 0);
    } catch (const std::bad_alloc&) {
        // No exception may leave the Win32 thread entry point.
    }
    return 0;
}

std::vector<FolderLookupResult> FolderLookup::TakeResults() const
{
    std::lock_guard guard(inbox_->lock);
    std::vector<FolderLookupResult> ready;
    ready.swap(inbox_->ready);
    return ready;
}

void FolderLookup::Detach()
{
    if (!inbox_) return;
    std::lock_guard guard(inbox_->lock);
    inbox_->window = nullptr;
    inbox_->ready.clear();
}
