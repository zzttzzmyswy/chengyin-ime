#pragma once
#include "common.h"
#include "preferences.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>
namespace myswy {
// Bounded event queue. No disk access or profile serialization on the key thread.
// A failed save keeps the event at the head and retries it under a bounded
// budget; only shutdown abandons the remaining queue. The destructor joins
// before the TIP can unload.
class LearningWriter {
  public:
    // The retry budget is injectable so regressions exercise recovery and
    // exhaustion deterministically instead of waiting out the production budget.
    explicit LearningWriter(std::wstring path, LearningRetryPolicy policy = {});
    ~LearningWriter();
    bool enqueue(const uint8_t *, size_t, const uint8_t *, size_t, uint32_t matchingFlags = 0);
    LearningWriterStats stats() const;
    // True while a confirmed selection is still queued or being saved. The disk
    // snapshot is older than it, so a plain reload must not adopt that snapshot
    // over the choice the user already made.
    bool pending() const;
  private:
    void run();
    struct Event {
        std::array<uint8_t, 64> key{};
        std::array<uint8_t, 257> text{};
        size_t keySize = 0, textSize = 0;
        DWORD epoch = 0;
        uint32_t matchingFlags = 0;
    };
    ModuleLifetime lifetime_;
    std::wstring path_;
    LearningEpoch epoch_;
    LearningRetryPolicy policy_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::array<Event, 128> events_{};
    size_t first_ = 0, count_ = 0;
    bool stop_ = false;
    // Retry state for the head event, and counters that never hold input text.
    unsigned attempts_ = 0;
    ULONGLONG firstAttempt_ = 0;
    mutable LearningWriterStats stats_{};
    std::thread thread_;
};
}
