// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile_store.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace chengyin {
namespace {
// The ABI's own ceiling, restated so this file cannot silently drift from it.
constexpr uint64_t MaxProfileBytes = 4 * 1024 * 1024;
// Bounded queue. A confirmed selection that arrives when this many are already
// waiting is refused and counted rather than growing without limit: the queue
// exists to decouple input from the disk, not to buffer an unbounded backlog.
constexpr size_t MaxQueuedEvents = 128;
// Learning data is the user's own text, so the directory and the file are private.
constexpr mode_t DirectoryMode = 0700;
constexpr mode_t FileMode = 0600;
} // namespace

// State shared with the worker. Held through a shared_ptr because the owner's
// destructor stops the thread on its own thread while the thread's closure must
// keep the state alive until it has really returned.
struct ProfileStore::Shared {
    // The worker's own profile, replaced only by reload(). Touched only under
    // `io`, which is also what every save holds, so a swap can never interleave
    // with a serialization.
    ChengyinProfile *profile = nullptr;
    // Queue and counters. Held for bookkeeping only, never across a disk
    // operation, which is what keeps enqueue() off the disk's critical path.
    mutable std::mutex state;
    std::condition_variable changed;
    std::deque<Event> queue;
    bool stopping = false;
    // False while publishing would replace a file this process could not read.
    bool writable = true;
    // True from the moment a batch is taken until its outcome is recorded, so
    // flush() can tell "nothing queued yet" from "nothing left to do".
    bool saving = false;
    // Bumped by every reload(); a batch collected under an older value describes
    // selections made against content the user has since replaced.
    uint64_t generation = 0;
    ProfileStoreStats stats;
    std::string lastError;
    // Serializes a whole save against a reload. Never held while `state` is held
    // and never held across enqueue(), so a slow fsync cannot block input.
    std::mutex io;
};

ProfileStore::ReadResult ProfileStore::read(const std::string &path) {
    ReadResult result;
    // O_NONBLOCK plus fstat is what keeps a FIFO or a device from blocking this
    // caller: without it, an open() on a writerless FIFO never returns.
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        // Only "not there" is a fresh start. Anything else -- permissions, a
        // symlink loop, a directory -- is `Blocked`: no profile can be read, and
        // the path must not be silently replaced either.
        result.state = errno == ENOENT ? FileState::Absent : FileState::Blocked;
        return result;
    }
    struct stat info {};
    if (::fstat(fd, &info) != 0) {
        ::close(fd);
        result.state = FileState::Blocked;
        return result;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<uint64_t>(info.st_size) > MaxProfileBytes) {
        // A directory, FIFO or device at the profile path, or a file past the
        // ABI's own ceiling: nothing here is a profile this process may adopt.
        ::close(fd);
        result.state = FileState::Blocked;
        return result;
    }
    result.bytes.reserve(static_cast<size_t>(info.st_size));
    std::array<char, 32768> buffer{};
    for (;;) {
        const auto count = ::read(fd, buffer.data(), buffer.size());
        if (count == 0) { break; }
        if (count < 0) {
            if (errno == EINTR) { continue; }
            ::close(fd);
            result.state = FileState::Damaged;
            result.bytes.clear();
            return result;
        }
        result.bytes.insert(result.bytes.end(), buffer.data(), buffer.data() + count);
    }
    ::close(fd);
    // An empty file is not a valid profile (from_binary rejects it), but it is an
    // ordinary "nothing learned yet" state rather than damage, so it is adopted as
    // an empty profile instead of disabling writes.
    if (result.bytes.empty()) {
        result.state = FileState::Absent;
        return result;
    }
    // Validation happens once, here, so the caller's master profile and the
    // worker's both start from bytes this ABI accepted. A file that fails its own
    // checksum is damage, never a fresh start.
    if (ChengyinProfile *validated = chengyin_profile_new(result.bytes.data(), result.bytes.size())) {
        chengyin_profile_free(validated);
        result.state = FileState::Ready;
        return result;
    }
    result.state = FileState::Damaged;
    result.bytes.clear();
    return result;
}

ProfileStore::ProfileStore(std::string path, LearningRetryPolicy policy)
    : path_(std::move(path)), policy_(policy) {
    if (path_.empty()) { return; }
    shared_ = std::make_shared<Shared>();
    const auto loaded = read(path_);
    switch (loaded.state) {
    case FileState::Ready:
        adopted_ = loaded.bytes;
        break;
    case FileState::Damaged:
        // Refuse to write for the rest of the process: the file is left exactly as
        // it is, so the user can inspect or repair what it holds.
        shared_->writable = false;
        warning_ = "学习档案无法读取（可能已损坏），本次运行不写入，原文件保持不变";
        break;
    case FileState::Blocked:
        // Nothing could be read, but the path is not a profile either, so writing
        // is attempted: publishing then fails and is reported, which is the
        // bounded-retry path rather than silent data loss.
        warning_ = "学习档案路径不是可读的普通文件，本次运行将尝试写入";
        break;
    case FileState::Absent:
        break;
    }
    shared_->profile = chengyin_profile_new(adopted_.empty() ? nullptr : adopted_.data(), adopted_.size());
    if (!shared_->profile) {
        shared_->writable = false;
        adopted_.clear();
        warning_ = "学习档案无法载入，本次运行不写入";
    }
    worker_ = std::thread([this] { run(); });
}

ProfileStore::~ProfileStore() {
    if (!shared_) { return; }
    {
        std::lock_guard<std::mutex> lock(shared_->state);
        shared_->stopping = true;
    }
    shared_->changed.notify_all();
    // The worker abandons a failing batch as soon as it sees `stopping`, and its
    // backoff wait ends on the same flag, so unloading waits for at most the save
    // in progress -- never for the rest of the retry window and never for the
    // queue. Whatever is left is counted rather than persisted.
    if (worker_.joinable()) { worker_.join(); }
    {
        std::lock_guard<std::mutex> lock(shared_->state);
        shared_->stats.dropped += shared_->queue.size();
        shared_->queue.clear();
    }
    if (shared_->profile) {
        chengyin_profile_free(shared_->profile);
        shared_->profile = nullptr;
    }
}

ProfileStoreStats ProfileStore::stats() const {
    if (!shared_) { return {}; }
    std::lock_guard<std::mutex> lock(shared_->state);
    return shared_->stats;
}

std::string ProfileStore::lastError() const {
    if (!shared_) { return {}; }
    std::lock_guard<std::mutex> lock(shared_->state);
    return shared_->lastError;
}

bool ProfileStore::enqueue(const uint8_t *key, size_t keySize, const uint8_t *text, size_t textSize,
                           uint32_t flags) {
    if (!shared_ || !key || !text || keySize == 0 || keySize > CHENGYIN_MAX_INPUT_BYTES ||
        textSize == 0 || textSize > CHENGYIN_MAX_TEXT_BYTES) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(shared_->state);
        if (shared_->stopping || !shared_->writable || shared_->queue.size() == MaxQueuedEvents) {
            // Counted without ever copying the content, so no counter and no log
            // line can leak what was typed.
            ++shared_->stats.rejected;
            return false;
        }
        Event &event = shared_->queue.emplace_back();
        std::memcpy(event.key.data(), key, keySize);
        std::memcpy(event.text.data(), text, textSize);
        event.keySize = keySize;
        event.textSize = textSize;
        event.flags = flags;
    }
    shared_->changed.notify_one();
    return true;
}

bool ProfileStore::reload(std::vector<uint8_t> &adopted) {
    adopted.clear();
    if (!shared_) { return false; }
    // The read happens under `io`, so it sees a save that is already in flight
    // either wholly before or wholly after: a save that completes first is
    // adopted here, and one that arrives later sees a newer generation and is
    // dropped. Reading outside the lock would let a stale batch publish over the
    // content just adopted.
    std::lock_guard<std::mutex> ioLock(shared_->io);
    const auto loaded = read(path_);
    if (loaded.state == FileState::Ready) { adopted = loaded.bytes; }
    ChengyinProfile *replacement =
        chengyin_profile_new(adopted.empty() ? nullptr : adopted.data(), adopted.size());
    if (!replacement) {
        // The ABI refused the replacement. Keep the profile in hand rather than
        // adopting something unusable, and stop writing so the rejected content
        // is not published over.
        adopted.clear();
    }
    {
        // The queue goes whatever the file turned out to be: those selections were
        // confirmed against content the user has just deleted or replaced.
        std::lock_guard<std::mutex> lock(shared_->state);
        shared_->queue.clear();
        ++shared_->generation;
        shared_->lastError.clear();
        warning_.clear();
        if (!replacement) {
            shared_->writable = false;
            warning_ = "学习档案内容无效，本次运行不写入";
        } else if (loaded.state == FileState::Damaged) {
            shared_->writable = false;
            warning_ = "学习档案无法读取（可能已损坏），本次运行不写入，原文件保持不变";
        } else {
            shared_->writable = true;
            if (loaded.state == FileState::Blocked) {
                warning_ = "学习档案路径不是可读的普通文件，本次运行将尝试写入";
            }
        }
    }
    if (replacement) {
        chengyin_profile_free(shared_->profile);
        shared_->profile = replacement;
    }
    adopted_ = adopted;
    return shared_->writable;
}

bool ProfileStore::flush(uint64_t timeoutMs) {
    if (!shared_) { return true; }
    std::unique_lock<std::mutex> lock(shared_->state);
    return shared_->changed.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
        return shared_->queue.empty() && !shared_->saving;
    });
}

ProfileStore::SaveOutcome ProfileStore::save(const std::vector<uint8_t> &bytes, uint64_t generation,
                                             std::string &error) {
    // Called with `io` held, which is also where reload() bumps the generation, so
    // a mismatch here means the user cleared or imported while this batch was
    // waiting its turn. That is not a failure: the batch describes selections made
    // against content that no longer exists and must not be resurrected.
    {
        std::lock_guard<std::mutex> lock(shared_->state);
        if (generation != shared_->generation) { return SaveOutcome::Superseded; }
    }
    const auto fail = [&error](const char *what) {
        error = std::string("学习档案写入失败：") + what + "（本次选择仅保留在内存）";
        return SaveOutcome::Failed;
    };
    const std::filesystem::path target(path_);
    const auto directory = target.parent_path();
    std::error_code createError;
    std::filesystem::create_directories(directory, createError);
    if (createError) { return fail("无法创建目录"); }
    // Unconditional, so a directory left behind by an older build or created under
    // a looser umask still ends up private.
    if (::chmod(directory.c_str(), DirectoryMode) != 0) { return fail("无法设置目录权限"); }
    std::string temporary;
    int fd = -1;
    for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
        temporary = path_ + ".tmp-" + std::to_string(::getpid()) + "-" + std::to_string(attempt);
        fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, FileMode);
        if (fd < 0 && errno != EEXIST) { return fail("无法创建临时文件"); }
    }
    if (fd < 0) { return fail("无法创建临时文件"); }
    // Every step is checked, and any failure abandons the temporary rather than
    // publishing a partial profile over a good one.
    size_t written = 0;
    while (written < bytes.size()) {
        const auto count = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (count < 0 && errno == EINTR) { continue; }
        if (count <= 0) {
            ::close(fd);
            ::unlink(temporary.c_str());
            return fail("写入不完整");
        }
        written += static_cast<size_t>(count);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(temporary.c_str());
        return fail("刷盘失败");
    }
    // O_CREAT already applied FileMode, but a filesystem that ignored it must not
    // leave the profile readable by anyone else.
    if (::fchmod(fd, FileMode) != 0) {
        ::close(fd);
        ::unlink(temporary.c_str());
        return fail("无法设置文件权限");
    }
    if (::close(fd) != 0) {
        ::unlink(temporary.c_str());
        return fail("关闭临时文件失败");
    }
    if (::rename(temporary.c_str(), path_.c_str()) != 0) {
        ::unlink(temporary.c_str());
        // A directory occupying the target path lands here, which is what makes
        // "the store is a directory" a bounded-retry failure instead of damage.
        return fail("原子替换失败");
    }
    const int directoryFd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directoryFd < 0) { return fail("无法打开目录"); }
    const bool synced = ::fsync(directoryFd) == 0;
    const bool closed = ::close(directoryFd) == 0;
    if (!synced || !closed) { return fail("目录刷盘失败"); }
    return SaveOutcome::Saved;
}

void ProfileStore::run() {
    const std::shared_ptr<Shared> &shared = shared_;
    for (;;) {
        std::vector<Event> batch;
        uint64_t generation = 0;
        bool write = false;
        {
            std::unique_lock<std::mutex> lock(shared->state);
            shared->changed.wait(lock, [&shared] { return shared->stopping || !shared->queue.empty(); });
            // Coalescing: everything queued right now becomes one serialization and
            // one rename, which is what keeps a burst of selections from costing a
            // file write each.
            while (!shared->queue.empty()) {
                batch.push_back(std::move(shared->queue.front()));
                shared->queue.pop_front();
            }
            if (batch.empty()) {
                // Only reachable on a stop request: the wait above returns either
                // with a non-empty queue or with `stopping`.
                shared->changed.notify_all();
                return;
            }
            generation = shared->generation;
            write = shared->writable;
            shared->saving = true;
        }
        SaveOutcome outcome = SaveOutcome::Superseded;
        std::string error;
        if (write) {
            // Applied and serialized exactly ONCE. A retry republishes these bytes;
            // re-recording the batch on every attempt would count the same
            // selection into the profile as many times as the store was retried.
            std::vector<uint8_t> bytes;
            {
                std::lock_guard<std::mutex> ioLock(shared->io);
                for (const auto &event : batch) {
                    // A pair the ABI itself refuses can never be stored, so it is
                    // skipped rather than spending the retry budget on it.
                    chengyin_profile_record_selection(shared->profile, event.key.data(), event.keySize,
                                                      event.text.data(), event.textSize, event.flags);
                }
                const int32_t size = chengyin_profile_binary(shared->profile, nullptr, 0);
                if (size <= 0 || static_cast<uint64_t>(size) > MaxProfileBytes) {
                    outcome = SaveOutcome::Failed;
                    error = "学习档案序列化失败（本次选择仅保留在内存）";
                } else {
                    bytes.resize(static_cast<size_t>(size));
                    if (chengyin_profile_binary(shared->profile, bytes.data(), bytes.size()) != size) {
                        outcome = SaveOutcome::Failed;
                        error = "学习档案序列化失败（本次选择仅保留在内存）";
                    }
                }
            }
            if (!bytes.empty()) {
                const auto firstAttempt = std::chrono::steady_clock::now();
                for (unsigned attempts = 0;; ++attempts) {
                    {
                        std::lock_guard<std::mutex> ioLock(shared->io);
                        outcome = save(bytes, generation, error);
                    }
                    if (outcome != SaveOutcome::Failed) { break; }
                    // One storage failure. The budget is spent as soon as EITHER
                    // bound is reached, so a permanently unusable store cannot
                    // stall this queue forever.
                    const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - firstAttempt)
                                           .count();
                    bool stopping = false;
                    {
                        std::lock_guard<std::mutex> lock(shared->state);
                        ++shared->stats.retried;
                        stopping = shared->stopping;
                    }
                    if (stopping || attempts + 1 >= policy_.attempts ||
                        static_cast<uint64_t>(spent) >= policy_.windowMs) {
                        break;
                    }
                    // Backoff outside every lock and interruptible, so unloading is
                    // never made to wait out this store's retry window.
                    const unsigned shift = std::min(attempts, 16u);
                    const auto backoff = std::min<uint64_t>(
                        policy_.windowMs,
                        static_cast<uint64_t>(policy_.backoffMs) << shift);
                    std::unique_lock<std::mutex> lock(shared->state);
                    if (shared->changed.wait_for(lock, std::chrono::milliseconds(backoff),
                                                 [&shared] { return shared->stopping; })) {
                        break;
                    }
                }
            }
        } else {
            outcome = SaveOutcome::Failed;
            error = "学习档案不可用，本次选择仅保留在内存";
        }
        {
            std::lock_guard<std::mutex> lock(shared->state);
            if (outcome == SaveOutcome::Saved) {
                shared->stats.saved += batch.size();
                shared->lastError.clear();
            } else if (outcome == SaveOutcome::Superseded) {
                // Not a loss and not a persistence failure: reload() discarded these
                // selections deliberately, so they are counted separately.
                shared->stats.superseded += batch.size();
            } else {
                shared->stats.dropped += batch.size();
                ++shared->stats.exhausted;
                if (!error.empty()) { shared->lastError = std::move(error); }
            }
            shared->saving = false;
        }
        shared->changed.notify_all();
    }
}
} // namespace chengyin
