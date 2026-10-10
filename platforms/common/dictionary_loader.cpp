// SPDX-License-Identifier: GPL-3.0-or-later
#include "dictionary_loader.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <iterator>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace chengyin {
namespace {
constexpr size_t MaxDictionaryBytes = 64 * 1024 * 1024;
// Every attached file together, on top of the base one. The core refuses a merged
// lexicon past 64 MiB too, but only after every file has been imported; bounding
// the raw bytes here keeps the worker's peak honest.
constexpr size_t MaxAttachedBytes = 64 * 1024 * 1024;

class File {
public:
    explicit File(const std::string &path) : fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK)) {}
    ~File() { if (fd >= 0) { ::close(fd); } }
    const int fd;
};

// Why one path could not be read. A code rather than a sentence, because the base
// lexicon and an attached entry word the same failure differently: the base path's
// wording is part of what it has always reported, while an attached entry names
// itself and then states the reason.
enum class ReadError { NotAbsolute, Missing, NotRegular, TooLarge, Unreadable };

// The base lexicon's wording, unchanged from before attached entries existed.
const char *const kBaseNotAbsolute = "词典路径必须是绝对路径";
const char *const kBaseMissing = "无法打开词典文件，请检查路径和权限";
const char *const kBaseNotRegular = "词典必须是普通文件";
const char *const kBaseTooLarge = "词典超过 64 MiB 上限";
const char *const kBaseUnreadable = "读取词典失败";
const char *const kInvalid = "词典无效：请检查 UTF-8、TSV 格式、重复项及大小限制";

// The attached entries' wording, one fixed phrase per reason. None of them can
// quote file content.
const char *const kEntryNotAbsolute = "路径必须是绝对路径";
const char *const kEntryMissing = "文件不存在";
const char *const kEntryNotRegular = "不是普通文件";
const char *const kEntryTooLarge = "文件超过 64 MiB 上限";
const char *const kEntryUnreadable = "无法读取文件";
const char *const kEntryUnrecognized = "格式无法识别或内容无效";
const char *const kEntryOverLimit = "合并后超过 250000 条";
// The core's one-shot merge takes 1..=64 handles and the base lexicon holds one of
// those slots, so at most 63 attached entries can take part. The settings list is
// allowed 64 entries; enabling all of them is what this refuses, by name and with a
// reason, rather than quietly merging the first 63.
const char *const kEntryTooManyEnabled = "同时启用的附加词库最多 63 个（基础词库占用一个合并名额）";

std::string baseReason(ReadError error) {
    switch (error) {
    case ReadError::NotAbsolute: return kBaseNotAbsolute;
    case ReadError::Missing: return kBaseMissing;
    case ReadError::NotRegular: return kBaseNotRegular;
    case ReadError::TooLarge: return kBaseTooLarge;
    case ReadError::Unreadable: break;
    }
    return kBaseUnreadable;
}

std::string entryReason(ReadError error) {
    switch (error) {
    case ReadError::NotAbsolute: return kEntryNotAbsolute;
    case ReadError::Missing: return kEntryMissing;
    case ReadError::NotRegular: return kEntryNotRegular;
    case ReadError::TooLarge: return kEntryTooLarge;
    case ReadError::Unreadable: break;
    }
    return kEntryUnreadable;
}

// The name an error message and the settings page may use for one entry. An empty
// configured name falls back to the file name, which is what the Windows page
// shows as well.
std::string displayName(const DictionarySource &entry) {
    if (!entry.name.empty()) { return entry.name; }
    const auto slash = entry.path.find_last_of('/');
    return slash == std::string::npos ? entry.path : entry.path.substr(slash + 1);
}

// A message the user can act on: which entry, and which of the fixed reasons. Only
// the display name is quoted -- never a path, never a byte of the file -- so a
// rejected lexicon cannot leak its content into a log or a panel.
std::string entryError(const DictionarySource &entry, const std::string &reason) {
    return "附加词库“" + displayName(entry) + "”：" + reason;
}

// A read that was superseded, as opposed to one that failed. The two are told apart
// at the point the bytes are handed back, because an empty file and a cancelled
// read both look empty otherwise.
struct ReadResult {
    std::string bytes;
    bool cancelled = false;
};

// Read one file with the same restrictions the base lexicon has always had:
// absolute path only, regular files only (O_NONBLOCK plus fstat keeps a FIFO or a
// device from blocking the worker), bounded size, and a cancellation check per
// chunk so a superseded request stops early instead of finishing work nobody wants.
ReadResult readBytes(const std::string &path, const std::atomic<bool> &stopping,
                     const std::atomic<uint64_t> &latest, uint64_t generation) {
    if (path.empty() || path.front() != '/' || path.find('\0') != std::string::npos) {
        throw ReadError::NotAbsolute;
    }
    File file(path);
    if (file.fd < 0) { throw ReadError::Missing; }
    struct stat info {};
    if (::fstat(file.fd, &info) != 0 || !S_ISREG(info.st_mode)) { throw ReadError::NotRegular; }
    if (info.st_size < 0 || static_cast<uint64_t>(info.st_size) > MaxDictionaryBytes) {
        throw ReadError::TooLarge;
    }
    ReadResult result;
    result.bytes.reserve(static_cast<size_t>(info.st_size));
    std::array<char, 32768> buffer;
    for (;;) {
        if (stopping.load() || latest.load() != generation) {
            result.bytes.clear();
            result.cancelled = true;
            return result;
        }
        const auto count = ::read(file.fd, buffer.data(), buffer.size());
        if (count == 0) { break; }
        if (count < 0) {
            if (errno == EINTR) { continue; }
            throw ReadError::Unreadable;
        }
        if (static_cast<size_t>(count) > MaxDictionaryBytes - result.bytes.size()) {
            throw ReadError::TooLarge;
        }
        result.bytes.append(buffer.data(), static_cast<size_t>(count));
    }
    return result;
}

// A file that exists, is a regular file, is within the size limit, and yet yields no
// bytes at all. Distinct from a cancelled read, which is handled before this.
std::string translate(ReadError error, bool base) { return base ? baseReason(error) : entryReason(error); }

// An owned core handle, freed unless it is released. Declared here so a throw
// anywhere below unwinds every handle the attempt had already built.
class Owned final {
public:
    explicit Owned(ChengyinDictionary *handle = nullptr) : handle_(handle) {}
    ~Owned() { if (handle_) { chengyin_dictionary_free(handle_); } }
    Owned(Owned &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    Owned &operator=(Owned &&other) noexcept {
        std::swap(handle_, other.handle_);
        return *this;
    }
    Owned(const Owned &) = delete;
    Owned &operator=(const Owned &) = delete;
    ChengyinDictionary *get() const { return handle_; }
    ChengyinDictionary *release() { return std::exchange(handle_, nullptr); }
private:
    ChengyinDictionary *handle_;
};

size_t enabledCount(const std::vector<DictionarySource> &entries) {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(),
                                             [](const DictionarySource &entry) { return entry.enabled; }));
}
} // namespace

DictionaryLoader::DictionaryLoader(Publish publish)
    : publish_(std::move(publish)), worker_([this] { run(); }) {}
DictionaryLoader::~DictionaryLoader() { stop(); }

void DictionaryLoader::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true);
    }
    changed_.notify_one();
    if (worker_.joinable()) { worker_.join(); }
}

void DictionaryLoader::request(uint64_t generation, std::string path,
                               std::vector<DictionarySource> entries) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_.store(generation);
        pending_ = Request{generation, std::move(path), std::move(entries)};
    }
    changed_.notify_one();
}

void DictionaryLoader::retire(DictionaryPtr dictionary) {
    if (!dictionary) { return; }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retired_.push_back(std::move(dictionary));
    }
    changed_.notify_one();
}

DictionaryPtr DictionaryLoader::load(const Request &request) {
    // Base lexicon first, exactly as before: an empty path is the bundled demo.
    Owned base;
    if (request.path.empty()) {
        base = Owned(chengyin_dictionary_new_demo());
    } else {
        ReadResult read;
        try {
            read = readBytes(request.path, stopping_, latest_, request.generation);
        } catch (const ReadError &error) {
            throw std::runtime_error(translate(error, true));
        }
        if (read.cancelled) { return {}; }
        base = Owned(chengyin_dictionary_new_tsv(reinterpret_cast<const uint8_t *>(read.bytes.data()),
                                                 read.bytes.size()));
    }
    if (!base.get()) { throw std::runtime_error(kInvalid); }
    // The row count of the base lexicon alone, which is what the one-line log after a
    // successful load reports alongside the attached count.
    const int32_t baseEntries = chengyin_dictionary_entry_count(base.get());

    // The handles merge_all() takes, base first, then every enabled entry in the
    // order the configuration lists them.
    std::vector<const ChengyinDictionary *> handles{base.get()};
    std::vector<Owned> attached;
    size_t attachedBytes = 0;
    for (const auto &entry : request.entries) {
        // A disabled entry is neither read nor validated: its file may be absent or
        // broken, and that is not an error. This is the Windows adapter's rule too.
        if (!entry.enabled) { continue; }
        if (handles.size() == kMaxMergedDictionaries) {
            // More enabled entries than the core's one-shot merge accepts. Nothing is
            // dropped silently: the whole request is refused and says so.
            throw std::runtime_error(entryError(entry, kEntryTooManyEnabled));
        }
        ReadResult read;
        try {
            read = readBytes(entry.path, stopping_, latest_, request.generation);
        } catch (const ReadError &error) {
            throw std::runtime_error(entryError(entry, entryReason(error)));
        }
        if (read.cancelled) { return {}; }
        if (read.bytes.empty()) { throw std::runtime_error(entryError(entry, kEntryUnrecognized)); }
        if (read.bytes.size() > MaxAttachedBytes - attachedBytes) {
            throw std::runtime_error(entryError(entry, kEntryTooLarge));
        }
        attachedBytes += read.bytes.size();
        ChengyinDictionary *handle = chengyin_dictionary_new_import(
            reinterpret_cast<const uint8_t *>(read.bytes.data()), read.bytes.size());
        // The raw bytes go before the next file is read, so the worker's peak is one
        // file's bytes plus every handle's compiled form, not every file's bytes.
        std::string().swap(read.bytes);
        if (!handle) { throw std::runtime_error(entryError(entry, kEntryUnrecognized)); }
        handles.push_back(handle);
        attached.emplace_back(handle);
        if (stopping_.load() || latest_.load() != request.generation) { return {}; }
    }

    // With nothing attached the base handle is already the answer, so no union is
    // built and the single-lexicon case costs exactly what it used to.
    if (attached.empty()) {
        return std::make_shared<DictionarySnapshot>(base.release(), request.path, request.entries, 0, baseEntries);
    }
    Owned merged(chengyin_dictionary_merge_all(handles.data(), handles.size()));
    if (!merged.get()) {
        // Every handle was live and the union still refused, which leaves the core's
        // own documented entry ceiling as the cause. It is attributed to the last
        // enabled entry, the one that pushed the union over.
        const auto last = std::find_if(request.entries.rbegin(), request.entries.rend(),
                                       [](const DictionarySource &entry) { return entry.enabled; });
        throw std::runtime_error(entryError(*last, kEntryOverLimit));
    }
    // The union is a fresh handle over its own compiled data, so the source handles
    // are released here -- after the snapshot exists, because building it is the one
    // remaining step that can throw.
    return std::make_shared<DictionarySnapshot>(merged.release(), request.path, request.entries,
                                                enabledCount(request.entries), baseEntries);
}

void DictionaryLoader::run() {
    std::vector<DictionaryPtr> retired;
    for (;;) {
        std::optional<Request> request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto ready = [this] { return stopping_.load() || pending_ || !retired_.empty(); };
            if (retired.empty()) { changed_.wait(lock, ready); }
            else { changed_.wait_for(lock, std::chrono::milliseconds(250), ready); }
            if (stopping_.load()) { return; }
            request.swap(pending_);
            std::move(retired_.begin(), retired_.end(), std::back_inserter(retired));
            retired_.clear();
        }
        // Active sessions retain a snapshot until AFTER releasing their Rust Arc.
        // Therefore the final, potentially expensive dictionary drop happens here.
        retired.erase(std::remove_if(retired.begin(), retired.end(),
            [](const auto &entry) { return entry.use_count() == 1; }), retired.end());
        if (!request) { continue; }
        // Recorded before the load begins, so an observer can tell "the worker has
        // taken this request up" from "it is still queued" without guessing at a
        // delay.
        started_.store(request->generation);
        DictionaryPtr dictionary;
        std::string error;
        try { dictionary = load(*request); }
        catch (const std::exception &exception) { error = exception.what(); }
        catch (...) { error = "词典加载异常"; }
        if (!stopping_.load() && latest_.load() == request->generation) {
            publish_(request->generation, std::move(dictionary), std::move(error));
        }
    }
}
} // namespace chengyin
