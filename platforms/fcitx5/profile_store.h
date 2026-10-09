// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "chengyin_ime.h"

namespace chengyin {
// Bounded retry budget for one failed save. The shape and the defaults match the
// Windows writer (`platforms/windows/preferences.h`): at most this many attempts,
// never longer than this window measured from the first failure, backing off by
// doubling from backoffMs. Injectable so a regression can spend a whole budget in
// milliseconds instead of waiting out the production window.
struct LearningRetryPolicy {
    unsigned attempts = 4;
    uint64_t windowMs = 30000;
    unsigned backoffMs = 50;
};

// Counters that never hold input content, so they are safe to log or print.
struct ProfileStoreStats {
    uint64_t saved = 0;      // confirmed selections serialized into the file
    uint64_t superseded = 0; // dropped because reload() replaced the file first
    uint64_t rejected = 0;   // never queued: full queue, stopping, or writes disabled
    uint64_t retried = 0;    // publish attempts that failed against storage
    uint64_t exhausted = 0;  // batches whose retry budget ran out
    uint64_t dropped = 0;    // selections in those batches, plus those still queued at stop
};

// Disk half of the Fcitx learning path: one worker thread with its own profile
// copy, a bounded queue of confirmed selections, and an atomic replace (temporary
// file, fsync, rename, directory fsync). The input thread only copies bytes into
// the queue -- reading, serialization, fsync and rename never run there.
//
// Linux runs one plugin process, so unlike Windows this class needs no
// cross-process lock and no destructive-generation or revision channels: the
// engine that owns this store is its only writer, and reload() is how a clear or
// an import happens.
//
// Queue bookkeeping and file I/O use separate mutexes on purpose. The state mutex
// only ever covers counters and the queue, so enqueue() never waits on a disk
// operation; the I/O mutex serializes one save against reload(), which is what
// keeps a save that was already in flight when the user cleared or imported from
// publishing the content that replaced it.
class ProfileStore final {
public:
    // Reads the file before the worker starts, so the caller can build its own
    // master profile from the very revision this store adopted. An empty `path`
    // means there is nowhere to persist to: no read, no write, available() false.
    explicit ProfileStore(std::string path, LearningRetryPolicy policy = {});
    ~ProfileStore();
    ProfileStore(const ProfileStore &) = delete;
    ProfileStore &operator=(const ProfileStore &) = delete;

    // False when there is nowhere to persist to. That is not the same as the file
    // being damaged; `warning()` says which.
    bool available() const { return !path_.empty(); }
    // The file's own bytes when they were usable, empty when it was absent or
    // unusable. Hand this to chengyin_profile_new.
    const std::vector<uint8_t> &initialBytes() const { return adopted_; }
    // Why this store started without a usable file, without any spelling or text.
    // Empty when the file was valid or simply absent.
    const std::string &warning() const { return warning_; }
    ProfileStoreStats stats() const;
    // Last failure, without any spelling or text. Empty when nothing failed.
    std::string lastError() const;

    // One host-confirmed selection. Never touches the disk and never waits for the
    // worker. Returns false when the event was refused.
    bool enqueue(const uint8_t *key, size_t keySize, const uint8_t *text, size_t textSize, uint32_t flags);

    // Re-reads the file and adopts it, dropping whatever is still queued: deleting
    // the file is how a user clears learning and replacing it is how a user imports
    // one, so selections confirmed against the previous content must never be
    // replayed onto the new one. `adopted` receives the bytes the caller should
    // adopt, empty when the file is absent or unusable. Returns true when the store
    // is writable afterwards.
    bool reload(std::vector<uint8_t> &adopted);

    // Waits until nothing is queued and no batch is still being published, bounded
    // by timeoutMs. False means the bound was reached, not that data was lost: the
    // caller's own in-memory profile still has every selection.
    bool flush(uint64_t timeoutMs);

private:
    // CHENGYIN_MAX_INPUT_BYTES and CHENGYIN_MAX_TEXT_BYTES, plus the NUL the ABI
    // getters reserve.
    struct Event {
        std::array<uint8_t, 64> key{};
        std::array<uint8_t, 257> text{};
        size_t keySize = 0;
        size_t textSize = 0;
        uint32_t flags = 0;
    };
    // What the target path turned out to hold.
    //
    // `Damaged` and `Blocked` are deliberately different, and the difference is
    // about data safety rather than error wording: a regular file this store could
    // not read or could not parse is presumed to hold the user's own learning data,
    // so it is never replaced. A path that is not a regular file cannot hold a
    // profile at all, so persistence is attempted and fails at publish time
    // instead -- the bounded-retry path, which cannot lose anything either, since
    // renaming a file over a directory always fails.
    enum class FileState { Ready, Absent, Damaged, Blocked };
    // Outcome of one publish attempt. `Superseded` is not a failure: reload()
    // already discarded those selections on purpose.
    enum class SaveOutcome { Saved, Failed, Superseded };
    struct Shared;
    struct ReadResult {
        FileState state = FileState::Absent;
        std::vector<uint8_t> bytes;
    };
    // Bounded read: regular files only, at most MaxProfileBytes, validated through
    // the ABI so both this store and its caller start from bytes the ABI accepted.
    static ReadResult read(const std::string &path);
    // Worker body. A member rather than a free function because the destructor
    // joins the thread before any member is destroyed, so it may safely touch
    // `shared_`, `path_` and `policy_`.
    void run();
    // Publishes one serialized profile as a temporary file plus fsync plus rename
    // plus directory fsync. `generation` is the value the batch was collected
    // under; a mismatch means reload() replaced the file meanwhile, so the batch is
    // dropped rather than resurrecting it.
    SaveOutcome save(const std::vector<uint8_t> &bytes, uint64_t generation, std::string &error);

    std::string path_;
    LearningRetryPolicy policy_;
    std::shared_ptr<Shared> shared_;
    std::vector<uint8_t> adopted_;
    std::string warning_;
    // Declared last, so it is destroyed first and the join in the destructor body
    // is the only thing that ever touches a stopped worker.
    std::thread worker_;
};
} // namespace chengyin
