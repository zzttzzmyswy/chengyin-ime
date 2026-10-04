#include "configuration.h"
namespace myswy {
namespace {
constexpr UINT kReady = WM_APP + 57;
}
ConfigurationWatcher::ConfigurationWatcher(DWORD observed, std::function<Snapshot()> load,
        std::function<void(Snapshot)> apply, const wchar_t *name): epoch_(name), apply_(std::move(apply)) {
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    window_ = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module, nullptr);
    if (!window_ || !stop_ || !epoch_.valid())
        return;
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(procedure));
    try {
        worker_ = std::thread([this, observed, load = std::move(load)]() mutable {
            while (WaitForSingleObject(stop_, 200) == WAIT_TIMEOUT) {
                const DWORD next = epoch_.current();
                if (next == observed)
                    continue;
                try {
                    auto update = load();
                    if (update) {
                        std::lock_guard<std::mutex> guard(mutex_);
                        pending_ = std::move(update);
                        PostMessageW(window_, kReady, 0, 0);
                    }
                    observed = next;
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
