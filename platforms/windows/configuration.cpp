// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration.h"
namespace chengyin {
namespace {
constexpr UINT kReady = WM_APP + 57;
struct FileStamp {
    bool exists = false;
    FILETIME modified{};
    DWORD high = 0, low = 0;
    bool operator==(const FileStamp &other) const {
        return exists == other.exists && modified.dwHighDateTime == other.modified.dwHighDateTime
            && modified.dwLowDateTime == other.modified.dwLowDateTime && high == other.high && low == other.low;
    }
};
FileStamp stamp(const std::wstring &path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return {};
    return {true, data.ftLastWriteTime, data.nFileSizeHigh, data.nFileSizeLow};
}
}
ConfigurationWatcher::ConfigurationWatcher(DWORD observed, std::function<Snapshot()> load,
        std::function<void(Snapshot)> apply, const wchar_t *name, std::wstring preferencesPath,
        std::wstring revisionName, std::function<bool()> due):
        epoch_(name ? name : configurationEpochName().c_str()),
        revision_(revisionName.empty() ? nullptr : revisionName.c_str()), apply_(std::move(apply)),
        due_(std::move(due)) {
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    window_ = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module, nullptr);
    if (!window_ || !stop_ || (!epoch_.valid() && preferencesPath.empty()))
        return;
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(procedure));
    try {
        worker_ = std::thread([this, observed, load = std::move(load), path = std::move(preferencesPath)]() mutable {
            auto previous = stamp(path);
            bool initial = !path.empty();
            DWORD observedRevision = revision_.valid() ? revision_.current() : 0;
            ULONGLONG retryAt = 0;
            while (WaitForSingleObject(stop_, 200) == WAIT_TIMEOUT) {
                const DWORD next = epoch_.current();
                const DWORD nextRevision = revision_.valid() ? revision_.current() : 0;
                const auto current = stamp(path);
                // An ordinary learning revision is additive: it must trigger a
                // reload even when the configuration generation and the
                // preference file are both unchanged.
                const bool revised = nextRevision != observedRevision;
                // A retry that is due must reload even though neither the file nor
                // any generation changed; otherwise an unfulfilled vocabulary would
                // wait for an unrelated event before it is ever retried (review R09).
                const bool due = due_ && due_();
                if ((!initial && next == observed && current == previous && !revised && !due)
                        || GetTickCount64() < retryAt)
                    continue;
                try {
                    auto update = load();
                    if (update) {
                        update->learningRevision = nextRevision;
                        update->hasRevision = revision_.valid();
                        std::lock_guard<std::mutex> guard(mutex_);
                        pending_ = std::move(update);
                        PostMessageW(window_, kReady, 0, 0);
                        if (pending_->preferencesValid || path.empty()) {
                            previous = current;
                            initial = false;
                        } else {
                            // Temporary access/parse failures retain the old snapshot and retry.
                            retryAt = GetTickCount64() + 1000;
                        }
                    }
                    observed = next;
                    observedRevision = nextRevision;
                } catch (...) { /* Preserve the active snapshot; retry on the next wake. */ }
            }
        });
    } catch (...) {}
}
ConfigurationWatcher::~ConfigurationWatcher() {
    if (stop_)
        SetEvent(stop_);
    if (worker_.joinable())
        worker_.join();
    if (window_) {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
    }
    if (stop_)
        CloseHandle(stop_);
}
LRESULT CALLBACK ConfigurationWatcher::procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto *self = reinterpret_cast<ConfigurationWatcher *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == kReady && self) {
        Snapshot next;
        {
            std::lock_guard<std::mutex> guard(self->mutex_);
            next = std::move(self->pending_);
        }
        auto apply = self->apply_;
        if (next)
            apply(std::move(next));
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
}
