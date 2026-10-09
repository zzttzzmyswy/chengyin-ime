// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "chengyin_ime.h"
#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>

namespace chengyin {
// Identity of the custom vocabulary file for the reload decision. A missing file
// carries no size or time, so equality is only meaningful while both exist.
struct DictionaryStamp {
    bool exists = false;
    DWORD sizeLow = 0;
    DWORD sizeHigh = 0;
    FILETIME written{};
    bool same(const DictionaryStamp &other) const {
        return exists == other.exists && sizeLow == other.sizeLow && sizeHigh == other.sizeHigh
               && written.dwLowDateTime == other.written.dwLowDateTime
               && written.dwHighDateTime == other.written.dwHighDateTime;
    }
};
DictionaryStamp readDictionaryStamp(const std::wstring &path);

// Bounded retry policy for one custom vocabulary that could not be loaded. The
// accepted stamp is deliberately not advanced on failure, so an unchanged file is
// retried instead of being reported as already loaded (review R09). Once the fast
// budget is spent the source keeps probing at `window` intervals, so a file that
// becomes readable without a new timestamp still recovers instead of staying stale.
struct DictionaryRetryPolicy {
    unsigned attempts = 8;
    ULONGLONG window = 30000;
    unsigned backoff = 200; // doubling, starting from this many milliseconds
};

// One shared immutable dictionary snapshot plus per-consumer handles. A snapshot is
// replaced only after a successful load; a failed load keeps the previous vocabulary
// usable and leaves the wanted target pending for a later retry.
//
// Threading: every method is internally serialized. No method runs on the key path,
// and file I/O happens only when a load is actually due.
class DictionarySource {
  public:
    // Produces the wanted custom vocabulary, or null when it cannot be read/parsed.
    using Loader = std::function<ChengyinDictionary *()>;
    // Built-in base, used when no custom vocabulary is wanted.
    using Fallback = ChengyinDictionary *(*)();
    // Counts only; no path, file name or vocabulary content.
    struct Stats {
        std::uint64_t loaded = 0, transient = 0, exhausted = 0;
    };

    DictionarySource(Loader loader, Fallback fallback, DictionaryRetryPolicy policy = {});
    ~DictionarySource();
    DictionarySource(const DictionarySource &) = delete;
    DictionarySource &operator=(const DictionarySource &) = delete;

    // Returns a new consumer handle for the wanted target, or null when no
    // vocabulary is available at all. `renewed`, when given, reports whether this
    // call admitted a new snapshot, so a caller that merely re-queried an
    // already-accepted target can skip republishing it.
    ChengyinDictionary *acquire(bool custom, const DictionaryStamp &stamp, bool *renewed = nullptr);
    // Retries an unfulfilled target whose backoff has elapsed, returning true only
    // when a new snapshot was admitted. A still-failing target returns false and
    // leaves the old snapshot alone, so a retry never disturbs active input.
    bool retry(bool custom, const DictionaryStamp &stamp);
    // Frees a handle from acquire(), or one registered with adopt().
    void release(ChengyinDictionary *owned);
    // Counts an externally created handle as a consumer of this snapshot.
    void adopt(ChengyinDictionary *owned);
    // True while an unfulfilled target is waiting for its next attempt, so the
    // owner's worker can poll `retry` without touching the disk first.
    bool pending() const;
    Stats stats() const;

  private:
    bool due(bool custom, const DictionaryStamp &stamp, ULONGLONG now) const;
    bool load(bool custom, const DictionaryStamp &stamp, ULONGLONG now);
    void clearPending();

    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    Loader loader_;
    Fallback fallback_;
    DictionaryRetryPolicy policy_;
    ChengyinDictionary *snapshot_ = nullptr;
    size_t users_ = 0;
    bool custom_ = false;        // the accepted target is the custom file
    DictionaryStamp accepted_{}; // stamp of the accepted custom file
    // The target that most recently failed, kept so its backoff is remembered rather
    // than re-reading the file on every query.
    bool pending_ = false;
    bool pendingCustom_ = false;
    DictionaryStamp pendingStamp_{};
    unsigned attempts_ = 0;
    ULONGLONG firstAttempt_ = 0;
    ULONGLONG retryAt_ = 0;
    Stats stats_{};
};
}
