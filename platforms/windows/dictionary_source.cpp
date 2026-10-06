#include "dictionary_source.h"
#include <algorithm>
#include <utility>

namespace myswy {
DictionaryStamp readDictionaryStamp(const std::wstring &path) {
    DictionaryStamp result;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return result;
    result.exists = true;
    result.sizeLow = data.nFileSizeLow;
    result.sizeHigh = data.nFileSizeHigh;
    result.written = data.ftLastWriteTime;
    return result;
}

DictionarySource::DictionarySource(Loader loader, Fallback fallback, DictionaryRetryPolicy policy)
    : loader_(std::move(loader)), fallback_(fallback), policy_(policy) {}

DictionarySource::~DictionarySource() {
    if (snapshot_)
        myswy_dictionary_free(snapshot_);
}

MyswyDictionary *DictionarySource::acquire(bool custom, const DictionaryStamp &stamp, bool *renewed) {
    AcquireSRWLockExclusive(&lock_);
    const ULONGLONG now = GetTickCount64();
    const bool admitted = due(custom, stamp, now) && load(custom, stamp, now);
    if (renewed)
        *renewed = admitted;
    MyswyDictionary *result = snapshot_ ? myswy_dictionary_clone(snapshot_) : nullptr;
    if (result)
        ++users_;
    ReleaseSRWLockExclusive(&lock_);
    return result;
}

bool DictionarySource::retry(bool custom, const DictionaryStamp &stamp) {
    AcquireSRWLockExclusive(&lock_);
    const ULONGLONG now = GetTickCount64();
    // Only a retry that is due may touch the disk, so a poll that arrives early costs
    // nothing and a target that is still failing is never re-read ahead of its
    // backoff. A false return leaves the old snapshot in place.
    const bool admitted = due(custom, stamp, now) && load(custom, stamp, now);
    ReleaseSRWLockExclusive(&lock_);
    return admitted;
}

void DictionarySource::release(MyswyDictionary *owned) {
    myswy_dictionary_free(owned);
    MyswyDictionary *retired = nullptr;
    AcquireSRWLockExclusive(&lock_);
    // The shared snapshot is dropped once no activated service uses it, matching the
    // DLL-scoped ownership the service had before this type existed.
    if (users_ && --users_ == 0)
        retired = std::exchange(snapshot_, nullptr);
    ReleaseSRWLockExclusive(&lock_);
    if (retired)
        myswy_dictionary_free(retired);
}

void DictionarySource::adopt(MyswyDictionary *owned) {
    if (!owned)
        return;
    AcquireSRWLockExclusive(&lock_);
    ++users_;
    ReleaseSRWLockExclusive(&lock_);
}

bool DictionarySource::pending() const {
    AcquireSRWLockShared(const_cast<SRWLOCK *>(&lock_));
    const bool value = pending_;
    ReleaseSRWLockShared(const_cast<SRWLOCK *>(&lock_));
    return value;
}

DictionarySource::Stats DictionarySource::stats() const {
    AcquireSRWLockShared(const_cast<SRWLOCK *>(&lock_));
    const Stats copy = stats_;
    ReleaseSRWLockShared(const_cast<SRWLOCK *>(&lock_));
    return copy;
}

bool DictionarySource::due(bool custom, const DictionaryStamp &stamp, ULONGLONG now) const {
    if (!snapshot_)
        return true;
    // An earlier attempt already failed on exactly this target: wait out its backoff
    // instead of re-reading the file on every query.
    if (pending_ && pendingCustom_ == custom && (!custom || pendingStamp_.same(stamp)))
        return now >= retryAt_;
    // Nothing failed: the accepted snapshot already is the wanted target.
    return !(custom == custom_ && (!custom || accepted_.same(stamp)));
}

bool DictionarySource::load(bool custom, const DictionaryStamp &stamp, ULONGLONG now) {
    MyswyDictionary *next = custom && loader_ ? loader_() : (fallback_ ? fallback_() : nullptr);
    if (next) {
        MyswyDictionary *old = std::exchange(snapshot_, next);
        if (old)
            myswy_dictionary_free(old);
        // Only an admitted target advances what counts as loaded. Because a failed
        // custom load never gets here, an unchanged file stays retryable instead of
        // being treated as already loaded (review R09).
        custom_ = custom;
        accepted_ = custom ? stamp : DictionaryStamp{};
        clearPending();
        ++stats_.loaded;
        return true;
    }
    // A wanted custom vocabulary could not be read or parsed. The last valid
    // snapshot stays active; only a first failure has none yet, so the built-in
    // base is admitted there to keep input usable while the retry runs.
    bool admitted = false;
    if (!snapshot_ && fallback_) {
        snapshot_ = fallback_();
        if (snapshot_) {
            custom_ = false;
            accepted_ = DictionaryStamp{};
            admitted = true;
        }
    }
    ++stats_.transient;
    // A different target starts its own budget rather than inheriting the last one's.
    if (!(pending_ && pendingCustom_ == custom && (!custom || pendingStamp_.same(stamp)))) {
        attempts_ = 0;
        firstAttempt_ = now;
    }
    pending_ = true;
    pendingCustom_ = custom;
    pendingStamp_ = custom ? stamp : DictionaryStamp{};
    ++attempts_;
    if (attempts_ < policy_.attempts && now - firstAttempt_ < policy_.window) {
        // Doubling backoff inside the fast budget; the shift is capped so a large
        // injected budget cannot shift out of range.
        const unsigned shift = std::min(attempts_ - 1, 16u);
        retryAt_ = now
                   + std::min<ULONGLONG>(policy_.window,
                                         static_cast<ULONGLONG>(policy_.backoff) << shift);
    } else {
        // The fast budget is spent, but the failure may still be a temporary lock:
        // keep probing slowly instead of reporting this target as loaded.
        ++stats_.exhausted;
        retryAt_ = now + policy_.window;
    }
    return admitted;
}

void DictionarySource::clearPending() {
    pending_ = false;
    pendingCustom_ = false;
    pendingStamp_ = DictionaryStamp{};
    attempts_ = 0;
    firstAttempt_ = 0;
    retryAt_ = 0;
}
}
