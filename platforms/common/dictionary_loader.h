// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "chengyin_ime.h"

namespace chengyin {
// The core's own one-shot merge takes 1..=64 live handles, and the base lexicon
// always holds one of those slots, so at most this many attached entries can take
// part in a merge. The settings list itself may carry kMaxEntries entries; a list
// longer than that, or one whose enabled entries would need a 65th handle, is
// refused with a message rather than silently trimmed.
inline constexpr size_t kMaxMergedDictionaries = 64;
inline constexpr size_t kMaxEnabledEntries = kMaxMergedDictionaries - 1;
inline constexpr size_t kMaxEntries = 64;

// One lexicon attached to the base one. `name` is the display name the settings
// page shows and the only part of an entry an error message may quote; empty
// means the file name stands in for it. A disabled entry is remembered but never
// opened: its file may be absent or broken, and that is not an error -- the same
// rule the Windows adapter follows.
struct DictionarySource {
    std::string name;
    std::string path;
    bool enabled = true;
    bool operator==(const DictionarySource &other) const {
        return name == other.name && path == other.path && enabled == other.enabled;
    }
    bool operator!=(const DictionarySource &other) const { return !(*this == other); }
};

struct DictionarySnapshot {
    // `entryList` is the attached list exactly as it was requested, disabled
    // entries included, so the owner can tell "the same configuration" from "a new
    // one" and can show the configuration that is actually in use. `attached` is
    // how many of those entries took part in the merge and `baseEntries` how many
    // rows the base lexicon alone holds, which is what lets the one line logged
    // after a successful load name base, attached count and merged total.
    DictionarySnapshot(ChengyinDictionary *handle, std::string source,
                       std::vector<DictionarySource> entryList, size_t attached, int32_t baseEntries)
        : dictionary(handle, chengyin_dictionary_free), path(std::move(source)),
          entries(std::move(entryList)), attachedCount(attached), baseCount(baseEntries) {}
    std::unique_ptr<ChengyinDictionary, decltype(&chengyin_dictionary_free)> dictionary;
    const std::string path;
    const std::vector<DictionarySource> entries;
    const size_t attachedCount;
    const int32_t baseCount;
};
using DictionaryPtr = std::shared_ptr<const DictionarySnapshot>;

// One worker, one pending request: rapid config changes never spawn extra threads.
// Publish runs on the worker; the owner must marshal it to its event loop.
class DictionaryLoader {
public:
    using Publish = std::function<void(uint64_t, DictionaryPtr, std::string)>;
    explicit DictionaryLoader(Publish publish);
    ~DictionaryLoader();
    DictionaryLoader(const DictionaryLoader &) = delete;
    DictionaryLoader &operator=(const DictionaryLoader &) = delete;
    // The base lexicon plus every attached entry. The whole request is validated
    // off the input thread. Any enabled entry that cannot be read, cannot be
    // imported, or would push the union past the core's ceiling fails the request
    // as a whole, which is what leaves the caller's previous lexicon untouched.
    void request(uint64_t generation, std::string path, std::vector<DictionarySource> entries = {});
    void retire(DictionaryPtr dictionary);
    void stop();
    // The generation the worker has actually taken up, or zero when it is idle.
    // This is what lets a test establish that a request is genuinely in flight
    // before superseding it, instead of sleeping and hoping it was.
    uint64_t started() const { return started_.load(); }
private:
    struct Request {
        uint64_t generation;
        std::string path;
        std::vector<DictionarySource> entries;
    };
    DictionaryPtr load(const Request &request);
    void run();
    Publish publish_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<Request> pending_;
    std::vector<DictionaryPtr> retired_;
    std::atomic<uint64_t> latest_{0};
    std::atomic<uint64_t> started_{0};
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};
} // namespace chengyin
