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
struct DictionarySnapshot {
    DictionarySnapshot(ChengyinDictionary *handle, std::string source)
        : dictionary(handle, chengyin_dictionary_free), path(std::move(source)) {}
    std::unique_ptr<ChengyinDictionary, decltype(&chengyin_dictionary_free)> dictionary;
    const std::string path;
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
    void request(uint64_t generation, std::string path);
    void retire(DictionaryPtr dictionary);
    void stop();
private:
    struct Request { uint64_t generation; std::string path; };
    DictionaryPtr load(const Request &request);
    void run();
    Publish publish_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<Request> pending_;
    std::vector<DictionaryPtr> retired_;
    std::atomic<uint64_t> latest_{0};
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};
} // namespace chengyin
