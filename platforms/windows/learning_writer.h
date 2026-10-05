#pragma once
#include "common.h"
#include "preferences.h"
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>
namespace myswy {
// Bounded event queue. No disk access or profile serialization on the key thread.
// The destructor drains and joins before the TIP can unload.
class LearningWriter {
  public:
    explicit LearningWriter(std::wstring path);
    ~LearningWriter();
    bool enqueue(const uint8_t *, size_t, const uint8_t *, size_t, uint32_t matchingFlags = 0);
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
    std::mutex mutex_;
    std::condition_variable wake_;
    std::array<Event, 128> events_{};
    size_t first_ = 0, count_ = 0;
    bool stop_ = false;
    std::thread thread_;
};
}
