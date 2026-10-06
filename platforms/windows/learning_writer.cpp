#include "learning_writer.h"
#include "preferences.h"
#include <algorithm>
#include <chrono>
namespace myswy {
LearningWriter::LearningWriter(std::wstring path, LearningRetryPolicy policy) : path_(std::move(path)),
    epoch_(profileEpochName(path_).c_str()), policy_(policy), thread_([this] {
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
    if (!epoch_.valid() || !keySize || keySize > 63 || !textSize || textSize > 256) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.rejected; // counted without any input content
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ || count_ == events_.size()) {
            ++stats_.rejected; // full queue or shutting down; content never recorded
            return false;
        }
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
LearningWriterStats LearningWriter::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}
bool LearningWriter::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_ > 0;
}
bool LearningWriter::supersedes(DWORD revision) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_ > 0 || revision <= savedRevision_;
}
void LearningWriter::run() {
    for (;;) {
        Event event;
        ULONGLONG now = GetTickCount64();
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] {return stop_ || count_ > 0;});
        if (!count_)
            return;
        // Peek: the head stays queued until it is finished with, so a temporary
        // storage failure never loses a selection that input already confirmed.
        event = events_[first_];
        if (!attempts_)
            firstAttempt_ = now;
        lock.unlock();
        // Reload under the cross-process named lock, so another app's selection
        // or a Settings clear/import cannot be overwritten by a stale snapshot.
        ProfileUpdate outcome = ProfileUpdate::retry;
        try {
            outcome = updateProfile(path_, event.key.data(), event.keySize, event.text.data(), event.textSize,
                                    &event.epoch, event.matchingFlags);
        } catch (...) { /* Input remains usable if learning storage fails. */ }
        lock.lock();
        if (outcome == ProfileUpdate::saved || outcome == ProfileUpdate::invalidated) {
            first_ = (first_ + 1) % events_.size();
            --count_;
            attempts_ = 0;
            firstAttempt_ = 0;
            if (outcome == ProfileUpdate::saved) {
                ++stats_.saved;
                // Record what this save published so a snapshot the watcher
                // loaded before it is recognised as stale afterwards.
                LearningEpoch revision(profileRevisionName(path_).c_str());
                if (revision.valid())
                    savedRevision_ = revision.current();
            } else
                ++stats_.invalidated; // a clear/import generation moved under it
            continue;
        }
        ++attempts_;
        ++stats_.retried;
        // Bounded: the head is abandoned as soon as EITHER bound is spent, so a
        // permanently unavailable store cannot stall the queue or the unload.
        now = GetTickCount64();
        if (attempts_ >= policy_.attempts || now - firstAttempt_ >= policy_.window) {
            first_ = (first_ + 1) % events_.size();
            --count_;
            attempts_ = 0;
            firstAttempt_ = 0;
            ++stats_.exhausted;
            continue;
        }
        // Unload does not wait out the budget: once a save has actually failed
        // while stopping, the remaining events are dropped instead of each
        // repeating the same timeout. Events that still save do get drained.
        if (stop_) {
            stats_.abandoned += count_;
            count_ = 0;
            return;
        }
        // Back off outside the lock; input keeps queueing while we wait. The shift
        // is capped so a large injected attempt budget cannot shift out of range.
        const unsigned shift = std::min(attempts_ - 1, 16u);
        const auto backoff = std::min<ULONGLONG>(policy_.window,
                                                 static_cast<ULONGLONG>(policy_.backoff) << shift);
        wake_.wait_for(lock, std::chrono::milliseconds(backoff), [this] {return stop_;});
    }
}
}
