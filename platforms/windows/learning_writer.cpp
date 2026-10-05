#include "learning_writer.h"
#include "preferences.h"
#include <algorithm>
namespace myswy {
LearningWriter::LearningWriter(std::wstring path) : path_(std::move(path)), thread_([this] {
    run();
}) {}
LearningWriter::~LearningWriter() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable())
        thread_.join();
}
bool LearningWriter::enqueue(const uint8_t *key, size_t keySize, const uint8_t *text, size_t textSize, uint32_t matchingFlags) {
    if (!epoch_.valid() || !keySize || keySize > 63 || !textSize || textSize > 256)
        return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ || count_ == events_.size())
            return false;
        auto &event = events_[(first_ + count_) % events_.size()];
        std::copy_n(key, keySize, event.key.begin());
        std::copy_n(text, textSize, event.text.begin());
        event.epoch = epoch_.current();
        event.matchingFlags = matchingFlags;
        event.keySize = keySize;
        event.textSize = textSize;
        ++count_;
    }
    wake_.notify_one();
    return true;
}
void LearningWriter::run() {
    for (;;) {
        Event event;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] {return stop_ || count_ > 0;});
            if (!count_)
                return;
            event = events_[first_];
            first_ = (first_ + 1) % events_.size();
            --count_;
        }
        // Reload under the cross-process named lock, so another app's selection
        // or a Settings clear/import cannot be overwritten by a stale snapshot.
        bool saved = false;
        try {
            saved = updateProfile(path_, event.key.data(), event.keySize, event.text.data(), event.textSize,
                                  &event.epoch, event.matchingFlags);
        } catch (...) { /* Input remains usable if learning storage fails. */ }
        if (!saved) {
            std::lock_guard<std::mutex> lock(mutex_);
            // On shutdown don't repeat the five-second lock timeout for every
            // queued event when another process/storage is unavailable.
            if (stop_)
                return;
        }
    }
}
}
