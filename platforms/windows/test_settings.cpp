#include "settings.h"
#include "preferences.h"
#include "learning_writer.h"
#include "configuration.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <cwchar>
namespace myswy {
HINSTANCE module = nullptr;
LONG objects = 0;
}
namespace {
void require(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "settings FAIL: %s\n", message);
        std::exit(1);
    }
}
void write(const std::wstring &path, const char *text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    require(file != INVALID_HANDLE_VALUE, "create isolated fixture");
    DWORD size = static_cast<DWORD>(std::strlen(text)), written = 0;
    require(WriteFile(file, text, size, &written, nullptr) && written == size, "write fixture");
    CloseHandle(file);
}
// R07 at the consumer boundary: two already-running applications must both adopt
// an ordinary selection saved by a third one, on the revision channel only.
void concurrentRevisionConsumers(const std::wstring &folder) {
    const auto path = folder + L"\\consumers.profile";
    const auto generationName = myswy::profileEpochName(path);
    const auto revisionName = myswy::profileRevisionName(path);
    myswy::LearningEpoch generation(generationName.c_str());
    myswy::LearningEpoch revision(revisionName.c_str());
    require(generation.valid() && revision.valid(), "both consumer channels exist");
    const auto generationBefore = generation.current();
    const auto revisionBefore = revision.current();
    std::atomic<unsigned> adopted{0};
    DWORD seen[2]{};
    auto load = [&] {
        auto snapshot = std::make_shared<myswy::ConfigurationUpdate>();
        snapshot->hasRevision = true;
        snapshot->profile = myswy::loadProfile(path);
        return snapshot;
    };
    auto pump = [] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    };
    {
        myswy::ConfigurationWatcher first(0, load, [&](auto s) {
            seen[0] = s->learningRevision;
            ++adopted;
        }, nullptr, std::wstring{}, revisionName);
        myswy::ConfigurationWatcher second(0, load, [&](auto s) {
            seen[1] = s->learningRevision;
            ++adopted;
        }, nullptr, std::wstring{}, revisionName);
        require(first.valid() && second.valid(), "two live revision consumers");
        const uint8_t key[] = "nihao", text[] = "你好";
        require(myswy::updateProfile(path, key, 5, text, 6) == myswy::ProfileUpdate::saved,
                "a third application saves an ordinary selection");
        const auto published = revision.current();
        require(published != revisionBefore, "the ordinary save publishes a revision");
        const auto until = GetTickCount64() + 4000;
        while (adopted < 2 && GetTickCount64() < until) {
            pump();
            Sleep(5);
        }
        require(adopted >= 2, "both running applications adopt the ordinary revision");
        require(seen[0] == published && seen[1] == published, "each consumer sees the published revision");
        require(generation.current() == generationBefore,
                "an ordinary selection never moves the destructive generation");
    }
    // A destructive clear must still reach both, and must be distinguishable from
    // the additive revision above.
    const auto clearRevision = revision.current();
    require(myswy::clearProfile(path), "clear publishes a destructive generation");
    require(generation.current() != generationBefore, "clear moves the generation consumers watch");
    require(revision.current() == clearRevision, "a clear does not republish an ordinary revision");
    DeleteFileW(path.c_str());
}
}
int main() {
    wchar_t temporary[32768] {};
    require(GetTempPathW(32768, temporary) > 0, "temporary root");
    const std::wstring folder = std::wstring(temporary) + L"myswy-settings-" + std::to_wstring(
                                    GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    require(CreateDirectoryW(folder.c_str(), nullptr) != FALSE, "isolated folder");
    const auto source = folder + L"\\source.tsv", target = folder + L"\\dictionary.custom";
    write(source, "ni\tfirst\t10\n");
    require(myswy::installCustomDictionary(source, target), "valid import");
    std::vector<uint8_t> before;
    require(myswy::readDictionaryFile(target, before), "read imported dictionary");
    MyswyDictionary *owned = myswy::loadDictionaryBytes(before);
    require(owned != nullptr, "load valid import");
    MyswyDictionary *reference = myswy_dictionary_clone(owned);
    myswy_dictionary_free(owned);
    MyswySession *session = myswy_session_new_with_dictionary(reference);
    myswy_dictionary_free(reference);
    require(session != nullptr, "reference survives dictionary handle release");
    myswy_session_free(session);
    write(source, "ni\tinvalid\t0\n");
    require(!myswy::installCustomDictionary(source, target), "invalid vocabulary rejected");
    std::vector<uint8_t> after;
    require(myswy::readDictionaryFile(target, after)
            && before == after, "invalid import preserves previous bytes");
    write(source, "ni\tsecond\t10\n");
    HANDLE held = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    require(held != INVALID_HANDLE_VALUE, "hold last valid dictionary");
    require(!myswy::installCustomDictionary(source, target), "failed atomic rename rejects import");
    CloseHandle(held);
    require(myswy::readDictionaryFile(target, after)
            && before == after, "failed rename preserves old dictionary");
    require(myswy::installCustomDictionary(source, target), "retry imports after lock release");
    require(myswy::readDictionaryFile(target, after) && before != after, "atomic replacement updates dictionary");
    // Sogou GBK text conversion and UTF-16LE import exercise the Windows adapter.
    std::vector<uint8_t> gbk{'\'', 'c', 'e', '\'', 's', 'h', 'i', ' ', 0xb2, 0xe2, 0xca, 0xd4};
    auto *converted = myswy::loadDictionaryBytes(gbk);
    require(converted && myswy_dictionary_entry_count(converted) == 1, "GBK Sogou text import");
    MyswySession *gbkSession = myswy_session_new_with_dictionary(converted);
    myswy_dictionary_free(converted);
    for (char c : std::string("ceshi"))
        myswy_session_process(gbkSession, static_cast<uint32_t>(c), 0);
    uint8_t chinese[257] {};
    require(myswy_session_text(gbkSession, MYSWY_TEXT_CANDIDATE, 0, chinese, sizeof(chinese)) > 0 &&
            std::strcmp(reinterpret_cast<const char *>(chinese), "测试") == 0, "GBK is converted without mojibake");
    myswy_session_free(gbkSession);
    // A self-authored SCEL fixture; no third-party vocabulary is redistributed.
    std::vector<uint8_t> scel(0x1540, 0);
    const uint8_t signature[] = {0x40, 0x15, 0, 0, 0x44, 0x43, 0x53, 1};
    std::copy(std::begin(signature), std::end(signature), scel.begin());
    scel[0x120] = 1;
    auto half = [&](uint16_t n) {
        scel.push_back(static_cast<uint8_t>(n));
        scel.push_back(static_cast<uint8_t>(n >> 8));
    };
    scel.insert(scel.end(), {4, 0, 0, 0});
    const wchar_t *syllables[] = {L"ce", L"shi", L"ci", L"ku"};
    for (uint16_t i = 0; i < 4; ++i) {
        half(static_cast<uint16_t>(i * 10));
        half(static_cast<uint16_t>(std::wcslen(syllables[i]) * 2));
        for (const wchar_t *c = syllables[i]; *c; ++c)
            half(static_cast<uint16_t>(*c));
    }
    half(1);
    half(8);
    for (uint16_t i = 0; i < 4; ++i)
        half(static_cast<uint16_t>(i * 10));
    const wchar_t *phrase = L"测试词库";
    half(8);
    for (const wchar_t *c = phrase; *c; ++c)
        half(static_cast<uint16_t>(*c));
    half(10);
    half(800);
    scel.insert(scel.end(), 8, 0);
    const auto scelPath = folder + L"\\source.scel";
    HANDLE scelFile = CreateFileW(scelPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    require(scelFile != INVALID_HANDLE_VALUE, "create SCEL fixture");
    DWORD written = 0;
    require(WriteFile(scelFile, scel.data(), static_cast<DWORD>(scel.size()), &written, nullptr)
            && written == scel.size(), "write SCEL fixture");
    CloseHandle(scelFile);
    require(myswy::installCustomDictionary(scelPath, target, true), "append SCEL to existing snapshot");
    require(myswy::readDictionaryFile(target, after), "read merged snapshot");
    converted = myswy::loadDictionaryBytes(after);
    require(converted && myswy_dictionary_entry_count(converted) == 2, "append preserves earlier entry");
    myswy_dictionary_free(converted);
    DeleteFileW(target.c_str());
    require(myswy::installCustomDictionary(scelPath, target, true), "append to bundled vocabulary");
    require(myswy::readDictionaryFile(target, after), "read bundled plus custom");
    converted = myswy::loadDictionaryBytes(after);
    require(converted
            && myswy_dictionary_entry_count(converted) == 87541, "bundled vocabulary preserved on first append");
    myswy_dictionary_free(converted);
    before = after;
    // Corrupt source cannot replace or partially merge a dictionary.
    write(scelPath, "broken scel");
    require(!myswy::installCustomDictionary(scelPath, target, true), "damaged SCEL rejected");
    require(myswy::readDictionaryFile(target, after)
            && before == after, "damaged SCEL preserves merged snapshot");
    const auto preferences = folder + L"\\preferences.ini", profilePath = folder + L"\\learning.profile";
    myswy::Preferences prefs;
    require(!prefs.candidatePinyin, "new settings default to no candidate pinyin");
    prefs.font = L"微软雅黑";
    prefs.fontSize = 23;
    prefs.theme = 2;
    prefs.layout = 1;
    prefs.density = 0;
    prefs.pageSize = 7;
    prefs.shiftSwitch = 2;
    prefs.candidatePinyin = true;
    prefs.separators = false;
    prefs.learning = false;
    prefs.associations = false;
    prefs.defaultEnglish = true;
    prefs.caretFallback = false;
    prefs.matchingOptions = MYSWY_MATCHING_MASK;
    require(myswy::savePreferences(preferences, prefs), "save Unicode preferences");
    auto restored = myswy::loadPreferences(preferences);
    require(restored.font == prefs.font && restored.fontSize == 23 && restored.theme == 2 && restored.layout == 1
            && restored.density == 0 && restored.pageSize == 7 && restored.shiftSwitch == 2
            && restored.candidatePinyin && !restored.separators && !restored.learning && !restored.associations
            && restored.defaultEnglish && !restored.caretFallback && restored.matchingOptions==MYSWY_MATCHING_MASK, "all preferences survive reload");
    require(myswy::readSmallFile(preferences, before, 8192), "preferences snapshot");
    for (wchar_t theme : {L'3',L'4',L'5',L'6'}) {
        std::wstring legacy((before.size()-2)/2,L'\0');
        std::memcpy(legacy.data(),before.data()+2,before.size()-2);
        const auto at = legacy.find(L"Theme=2");
        require(at != std::wstring::npos, "locate legacy theme fixture");
        legacy[at+6] = theme;
        std::vector<uint8_t> legacyBytes = before;
        std::memcpy(legacyBytes.data()+2,legacy.data(),legacy.size()*2);
        require(myswy::atomicWrite(preferences,legacyBytes), "write isolated legacy theme fixture");
        auto migrated = restored;
        const bool loaded = myswy::tryLoadPreferences(preferences,migrated);
        if (theme == L'6')
            require(!loaded && migrated.theme == 2, "unknown theme rejected without changing output preferences");
        else
            require(loaded && migrated.theme == 0 && migrated.font == prefs.font && migrated.fontSize == 23
                    && migrated.layout == 1 && migrated.density == 0 && migrated.pageSize == 7
                    && migrated.shiftSwitch == 2 && migrated.defaultEnglish && !migrated.learning
                    && !migrated.associations && !migrated.separators && !migrated.caretFallback,
                    "retired theme falls back to system while preserving unrelated settings");
        require(myswy::readSmallFile(preferences,after,8192) && after == legacyBytes,
                "legacy theme loading never rewrites user configuration");
    }
    require(myswy::atomicWrite(preferences,before), "restore valid isolated preferences");
    {
        std::wstring legacy((before.size()-2)/2,L'\0');
        std::memcpy(legacy.data(),before.data()+2,before.size()-2);
        const auto at=legacy.find(L"MatchingOptions=");
        require(at!=std::wstring::npos,"matching option fixture");
        const auto end=legacy.find(L'\n',at);
        legacy.erase(at,end-at+1);
        const auto pinyinAt = legacy.find(L"ShowCandidatePinyin=");
        require(pinyinAt != std::wstring::npos, "new explicit pinyin preference fixture");
        legacy.erase(pinyinAt, legacy.find(L'\n', pinyinAt) - pinyinAt + 1);
        std::vector<uint8_t> bytes(2+legacy.size()*2); bytes[0]=0xff; bytes[1]=0xfe;
        std::memcpy(bytes.data()+2,legacy.data(),legacy.size()*2);
        require(myswy::atomicWrite(preferences,bytes),"legacy preference without matching switches");
        auto old=restored;
        require(myswy::tryLoadPreferences(preferences,old) && old.matchingOptions==0 && !old.candidatePinyin && old.font==prefs.font,
            "legacy matching disabled with other settings preserved");
        prefs.matchingOptions=1u<<31;
        require(!myswy::savePreferences(preferences,prefs),"unknown matching flags cannot save");
        require(myswy::readSmallFile(preferences,after,8192) && after==bytes,"invalid matching flags preserve file");
        prefs.matchingOptions=MYSWY_MATCHING_MASK;
        require(myswy::atomicWrite(preferences,before),"restore complete preference fixture");
    }
    prefs.fontSize = 100;
    require(!myswy::savePreferences(preferences, prefs), "out of bounds preference rejected");
    require(myswy::readSmallFile(preferences, after, 8192) && before == after, "invalid settings preserve file");
    prefs.fontSize = 18;
    prefs.font = std::wstring(1, static_cast<wchar_t>(0xd800));
    require(!myswy::savePreferences(preferences, prefs), "invalid UTF-16 font rejected");
    write(preferences, "broken preferences");
    require(myswy::loadPreferences(preferences).font == L"Microsoft YaHei UI",
            "damaged preferences use defaults");
    auto *empty = myswy::loadProfile(profilePath);
    require(empty && myswy_profile_count(empty) == 0, "absent profile uses empty state");
    myswy_profile_free(empty);
    const auto bytes = [](const char *t) {
        return reinterpret_cast<const uint8_t *>(t);
    };
    {
        const auto otherPath = folder + L"\\other.profile";
        auto alias = otherPath;
        std::replace(alias.begin(), alias.end(), L'\\', L'/');
        require(myswy::profileEpochName(alias) == myswy::profileEpochName(otherPath), "path separator aliases share epoch");
        CharUpperBuffW(alias.data(), static_cast<DWORD>(alias.size()));
        require(myswy::profileEpochName(alias) == myswy::profileEpochName(otherPath), "case aliases share epoch");
        require(!myswy::profileEpochName(source).empty()
            && myswy::profileEpochName(source) != myswy::profileEpochName(otherPath), "different profiles have independent epochs");
        HANDLE gate = CreateMutexW(nullptr, FALSE, L"Local\\MyswyIME.UserPreferences");
        require(gate && WaitForSingleObject(gate, 5000) == WAIT_OBJECT_0, "hold isolated-profile writer gate");
        {
            myswy::LearningWriter clearedWriter(source), preservedWriter(otherPath);
            require(clearedWriter.enqueue(bytes("nihao"), 5, bytes("你好"), 6), "queue profile to clear");
            require(preservedWriter.enqueue(bytes("nihao"), 5, bytes("拟好"), 6), "queue independent profile");
            require(myswy::clearProfile(source), "clear first profile only");
            ReleaseMutex(gate);
        }
        CloseHandle(gate);
        auto *first = myswy::loadProfile(source), *second = myswy::loadProfile(otherPath);
        require(first && myswy_profile_count(first) == 0 && second && myswy_profile_count(second) == 1,
            "clear invalidates only the selected profile's pending events");
        myswy_profile_free(first); myswy_profile_free(second);
        DeleteFileW(otherPath.c_str());
    }
    {
        // Independent workers must merge disk state, and drain before unloading.
        myswy::LearningWriter first(profilePath), second(profilePath);
        for (int i = 0; i < 20; ++i) {
            require(first.enqueue(bytes("nihao"), 5, bytes("你好"), 6), "first writer queue");
            require(second.enqueue(bytes("nihao"), 5, bytes("拟好"), 6), "second writer queue");
        }
    }
    {
        // These cases deliberately clear and rewrite their target, so they use a
        // dedicated path rather than the fixture later cases assert on.
        const auto revisionPath = folder + L"\\revision.profile";
        {
            // An ordinary save must publish the revision channel and leave the
            // destructive generation untouched, so other applications reload rather
            // than discarding the events they already queued.
            myswy::LearningEpoch generation(myswy::profileEpochName(revisionPath).c_str());
            myswy::LearningEpoch revision(myswy::profileRevisionName(revisionPath).c_str());
            require(generation.valid() && revision.valid(), "isolated profile exposes both learning channels");
            require(myswy::profileEpochName(revisionPath) != myswy::profileRevisionName(revisionPath),
                    "generation and revision are distinct objects");
            const DWORD generationBefore = generation.current(), revisionBefore = revision.current();
            require(myswy::updateProfile(revisionPath, bytes("hao"), 3, bytes("好"), 3, &generationBefore)
                    == myswy::ProfileUpdate::saved, "ordinary selection is saved");
            require(generation.current() == generationBefore,
                    "ordinary learning never advances the destructive generation");
            require(revision.current() != revisionBefore, "ordinary learning publishes a new revision");
        }
        {
            // A clear/import is destructive: it advances the generation, so a queued
            // event captured before it must be dropped rather than retried.
            myswy::LearningEpoch generation(myswy::profileEpochName(revisionPath).c_str());
            const DWORD before = generation.current();
            require(myswy::clearProfile(revisionPath), "clear advances the destructive generation");
            require(generation.current() != before, "clear moves the generation other applications observe");
            require(myswy::updateProfile(revisionPath, bytes("hao"), 3, bytes("好"), 3, &before)
                    == myswy::ProfileUpdate::invalidated,
                    "an event captured before a clear is dropped instead of retried");
        }
        {
            // Every retry re-checks the generation, so a clear that lands between
            // two attempts still wins over the queued event.
            myswy::LearningEpoch generation(myswy::profileEpochName(revisionPath).c_str());
            const DWORD stale = generation.current();
            require(myswy::clearProfile(revisionPath), "clear between attempts");
            require(myswy::updateProfile(revisionPath, bytes("hao"), 3, bytes("好"), 3, &stale)
                    == myswy::ProfileUpdate::invalidated,
                    "a later retry notices the clear that happened after enqueue");
        }
        // The revision channel must be independent per profile path, like the
        // generation: a sibling profile's learning cannot wake this one.
        require(!myswy::profileRevisionName(revisionPath).empty()
                && myswy::profileRevisionName(revisionPath) != myswy::profileRevisionName(profilePath),
                "different profiles have independent revisions");
        DeleteFileW(revisionPath.c_str());
    }
    {
        // A failed save must be retried and must reach disk once the store frees
        // up, and retrying must not learn the same choice twice: the bytes must
        // equal a single clean save of that one selection.
        const auto retryPath = folder + L"\\retry.profile";
        auto *emptyRetry = myswy_profile_new(nullptr, 0);
        require(emptyRetry && myswy::saveProfile(retryPath, emptyRetry), "empty retry fixture");
        myswy_profile_free(emptyRetry);
        require(myswy::updateProfile(retryPath, bytes("hao"), 3, bytes("好"), 3)
                == myswy::ProfileUpdate::saved, "reference single save");
        std::vector<uint8_t> reference;
        require(myswy::readSmallFile(retryPath, reference, 2 * 1024 * 1024), "reference bytes");
        auto *reset = myswy_profile_new(nullptr, 0);
        require(reset && myswy::saveProfile(retryPath, reset), "reset retry fixture");
        myswy_profile_free(reset);
        HANDLE locked = CreateFileW(retryPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "make the retry target unwritable");
        {
            // A generous budget: the point of this case is recovery after the
            // store frees up, not exhaustion.
            myswy::LearningRetryPolicy policy;
            policy.attempts = 200;
            policy.window = 5000;
            policy.backoff = 10;
            myswy::LearningWriter writer(retryPath, policy);
            require(writer.enqueue(bytes("hao"), 3, bytes("好"), 3), "queue onto an unavailable store");
            // Wait for an observed failed attempt, not for success: while the file
            // is locked, success is impossible by construction.
            const auto retriedBy = GetTickCount64() + 5000;
            while (writer.stats().retried == 0 && GetTickCount64() < retriedBy)
                Sleep(10);
            require(writer.stats().retried > 0, "a failed save is counted as a retry");
            require(writer.stats().saved == 0, "the event stays queued while storage is unavailable");
            CloseHandle(locked);
            locked = INVALID_HANDLE_VALUE;
            const auto savedBy = GetTickCount64() + 5000;
            while (writer.stats().saved == 0 && GetTickCount64() < savedBy)
                Sleep(10);
            require(writer.stats().saved == 1, "the queued event recovers once storage is writable");
        }
        if (locked != INVALID_HANDLE_VALUE)
            CloseHandle(locked);
        std::vector<uint8_t> recovered;
        require(myswy::readSmallFile(retryPath, recovered, 2 * 1024 * 1024)
                && recovered == reference, "recovery learns the selection exactly once");
        DeleteFileW(retryPath.c_str());
    }
    {
        // The retry budget is bounded in attempts as well as time, and the queue
        // reports exhaustion without ever recording input content.
        const auto bounded = folder + L"\\bounded.profile";
        auto *seed = myswy::loadProfile(bounded);
        require(seed && myswy::saveProfile(bounded, seed), "seed bounded fixture");
        myswy_profile_free(seed);
        HANDLE locked = CreateFileW(bounded.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "lock the bounded target");
        {
            // A short injectable budget keeps the regression quick while still
            // exercising the real retry path.
            myswy::LearningRetryPolicy policy;
            policy.attempts = 2;
            policy.window = 400;
            policy.backoff = 20;
            myswy::LearningWriter writer(bounded, policy);
            require(writer.enqueue(bytes("hao"), 3, bytes("好"), 3), "queue against a wedged store");
            const auto until = GetTickCount64() + 10000;
            while (writer.stats().exhausted == 0 && GetTickCount64() < until)
                Sleep(10);
            const auto stats = writer.stats();
            require(stats.exhausted == 1, "the retry budget is bounded and observable");
            require(stats.retried == policy.attempts && stats.saved == 0,
                    "every attempt is counted and an exhausted event is never counted as saved");
        }
        CloseHandle(locked);
        DeleteFileW(bounded.c_str());
    }
    {
        // A local selection that has not reached disk yet must be retained rather
        // than overwritten by the older snapshot still on disk. While storage is
        // unavailable the writer reports pending, so a reader knows not to adopt
        // that stale snapshot; once storage frees, the selection lands.
        const auto racePath = folder + L"\\race.profile";
        auto *seed = myswy_profile_new(nullptr, 0);
        require(seed && myswy::saveProfile(racePath, seed), "seed pending-race fixture");
        myswy_profile_free(seed);
        HANDLE held3 = CreateFileW(racePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held3 != INVALID_HANDLE_VALUE, "lock the pending-race target");
        {
            myswy::LearningWriter writer(racePath, {200, 5000, 10});
            require(writer.enqueue(bytes("hao"), 3, bytes("好"), 3), "queue a selection that cannot be saved yet");
            const auto until = GetTickCount64() + 3000;
            while (writer.stats().retried == 0 && GetTickCount64() < until)
                Sleep(5);
            require(writer.pending(), "an unsaved selection keeps the writer pending");
            // The disk snapshot still predates the selection, which is exactly why
            // a plain reload must not adopt it.
            auto *stale = myswy::loadProfile(racePath);
            require(stale && myswy_profile_count(stale) == 0, "the disk snapshot is older than the queued choice");
            myswy_profile_free(stale);
            CloseHandle(held3);
            held3 = INVALID_HANDLE_VALUE;
            const auto savedBy = GetTickCount64() + 5000;
            while (writer.stats().saved == 0 && GetTickCount64() < savedBy)
                Sleep(5);
            require(writer.stats().saved == 1 && !writer.pending(), "the retained selection reaches disk");
        }
        if (held3 != INVALID_HANDLE_VALUE)
            CloseHandle(held3);
        auto *landed = myswy::loadProfile(racePath);
        require(landed && myswy_profile_count(landed) == 1, "the local selection was not lost to the stale snapshot");
        myswy_profile_free(landed);
        DeleteFileW(racePath.c_str());
    }
    {
        // Queue overflow is counted, and overload never blocks or corrupts input.
        const auto full = folder + L"\\full.profile";
        {
        myswy::LearningWriter writer(full);
        int accepted = 0;
        for (int i = 0; i < 200; ++i) {
            if (writer.enqueue(bytes("nihao"), 5, bytes("你好"), 6))
                ++accepted;
        }
        require(accepted > 0 && accepted < 200, "the queue is bounded and rejects overflow");
        require(writer.stats().rejected == static_cast<std::uint64_t>(200 - accepted),
                "rejected events are counted without their content");
        }
        DeleteFileW(full.c_str());
    }
    {
        // Unloading must not wait out a retry budget per queued event.
        const auto wedged = folder + L"\\wedged.profile";
        auto *seed = myswy::loadProfile(wedged);
        require(seed && myswy::saveProfile(wedged, seed), "seed wedged fixture");
        myswy_profile_free(seed);
        HANDLE locked = CreateFileW(wedged.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "lock the wedged target");
        const auto started = GetTickCount64();
        {
            myswy::LearningWriter writer(wedged);
            for (int i = 0; i < 8; ++i)
                require(writer.enqueue(bytes("nihao"), 5, bytes("你好"), 6), "queue onto a wedged store");
        }
        require(GetTickCount64() - started < 8000, "unload abandons a wedged store promptly");
        CloseHandle(locked);
        DeleteFileW(wedged.c_str());
    }
    {
        // A clear must invalidate writes accepted before the clear, including queued events.
        HANDLE gate = CreateMutexW(nullptr, FALSE, L"Local\\MyswyIME.UserPreferences");
        require(gate && WaitForSingleObject(gate, 5000) == WAIT_OBJECT_0, "block writer before disk lock");
        {
            myswy::LearningWriter writer(source);
            require(writer.enqueue(bytes("nihao"), 5, bytes("你好"), 6), "queue before clear");
            require(myswy::clearProfile(source), "clear while old event queued");
            ReleaseMutex(gate);
        }
        CloseHandle(gate);
        auto *cleared = myswy::loadProfile(source);
        require(cleared && myswy_profile_count(cleared) == 0, "pending old event cannot resurrect cleared learning");
        myswy_profile_free(cleared);
    }
    require(myswy::objects == 0, "worker module lifetimes drained");
    concurrentRevisionConsumers(folder);
    require(myswy::objects == 0, "revision consumers released");
    auto *learned = myswy::loadProfile(profilePath);
    require(learned && myswy_profile_count(learned) == 2, "concurrent workers preserve both choices");
    require(myswy::readSmallFile(profilePath, before, 2 * 1024 * 1024), "learning snapshot");
    require(myswy::saveProfile(source, learned), "backup learning profile");
    myswy_profile_free(learned);
    held = CreateFileW(profilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
    require(held != INVALID_HANDLE_VALUE, "hold learning file");
    require(myswy::updateProfile(profilePath, bytes("hao"), 3, bytes("好"), 3) == myswy::ProfileUpdate::retry,
            "locked profile cannot be replaced and stays retryable");
    CloseHandle(held);
    require(myswy::readSmallFile(profilePath, after, 2 * 1024 * 1024)
            && before == after, "failed profile save preserves file");
    require(myswy::clearProfile(profilePath) && myswy::clearProfile(profilePath), "clear learning is repeatable");
    require(myswy::importProfile(source, profilePath), "restore validated backup");
    write(source, "broken learning");
    require(!myswy::importProfile(source, profilePath), "damaged backup rejected");
    require(myswy::readSmallFile(profilePath, after, 2 * 1024 * 1024)
            && before == after, "damaged backup preserves learning");
    write(source, "");
    require(!myswy::importProfile(source, profilePath), "empty backup cannot replace learning");
    require(myswy::readSmallFile(profilePath, after, 2 * 1024 * 1024)
            && before == after, "empty backup preserves learning");
    {
        auto *large = myswy_profile_new(nullptr, 0);
        require(large != nullptr, "large learning fixture");
        std::string key, prefix;
        for (int i = 0; i < 21; ++i) key += "shi";
        for (int i = 0; i < 84; ++i) prefix += "字";
        for (int i = 0; i < 8192; ++i) {
            wchar_t tail = static_cast<wchar_t>(0x4e00 + i);
            char utf8[4]{};
            const int n = WideCharToMultiByte(CP_UTF8, 0, &tail, 1, utf8, sizeof(utf8), nullptr, nullptr);
            const auto word = prefix + std::string(utf8, static_cast<size_t>(n));
            require(n == 3 && myswy_profile_record(large, bytes(key.c_str()), key.size(), bytes(word.c_str()), word.size()) == 0,
                    "8192 maximum string records");
        }
        require(myswy_profile_count(large) == 8192 && myswy_profile_binary(large, nullptr, 0) > 2 * 1024 * 1024,
                "expanded profile exceeds previous byte limit");
        require(myswy::saveProfile(source, large), "save expanded profile");
        myswy_profile_free(large);
        require(myswy::importProfile(source, profilePath), "import expanded profile with statistics");
        large = myswy::loadProfile(profilePath);
        require(large && myswy_profile_count(large) == 8192, "reload full expanded profile");
        myswy_profile_free(large);
        auto *feedback = myswy_profile_new(nullptr, 0);
        require(feedback && myswy_profile_record(feedback, bytes("shi"), 3, bytes("士"), 3) == 0,
                "hit rate feedback fixture");
        for (int i = 0; i < 16; ++i)
            require(myswy_profile_record_selection(feedback, bytes("shi"), 3, bytes("是"), 3, 0) == 0,
                    "accepted selection opportunity");
        require(myswy_profile_count(feedback) == 1 && myswy::saveProfile(source, feedback),
                "low hit rate forgotten and persisted");
        myswy_profile_free(feedback);
        require(myswy::importProfile(source, profilePath), "restore feedback statistics");
    }
    write(profilePath, "damaged");
    require(!myswy::loadProfile(profilePath), "damaged profile does not silently reset");
    require(myswy::updateProfile(profilePath, bytes("hao"), 3, bytes("好"), 3) == myswy::ProfileUpdate::retry,
            "record preserves damaged profile for recovery and stays retryable");
    write(profilePath, "");
    require(!myswy::loadProfile(profilePath), "empty existing profile is damaged");
    DeleteFileW(preferences.c_str());
    const auto library = folder + L"\\library.custom";
    write(source, "ni\tcustom-one\t10\n");
    require(myswy::addDictionaryLibrary(source, library), "add independent library entry");
    require(myswy::addDictionaryLibrary(source, library), "duplicate library import is idempotent");
    std::vector<myswy::DictionaryEntry> entries;
    require(myswy::readDictionaryLibrary(library, entries) && entries.size() == 1 && entries[0].enabled,
            "one named enabled library entry");
    const auto firstEntry = entries[0];
    write(source, "hao\tcustom-two\t10\n");
    require(myswy::addDictionaryLibrary(source, library), "second import retained separately");
    require(myswy::readDictionaryLibrary(library, entries) && entries.size() == 2, "two imports remain individually manageable");
    require(myswy::changeDictionaryLibrary(library, firstEntry, false), "disable one entry");
    require(!myswy::changeDictionaryLibrary(library, firstEntry, true), "stale UI cannot delete changed entry");
    require(myswy::readDictionaryFile(library, after), "read atomic library container");
    auto *base = myswy_dictionary_new_demo();
    auto *effective = myswy::loadEffectiveDictionary(after, base);
    require(effective != nullptr, "enabled entries merge with built-in dictionary");
    session = myswy_session_new_with_dictionary(effective);
    myswy_dictionary_free(effective);
    myswy_session_process(session, 'n', 0); myswy_session_process(session, 'i', 0);
    uint8_t candidate[64]{};
    require(myswy_session_text(session, MYSWY_TEXT_CANDIDATE, 0, candidate, sizeof(candidate)) > 1
            && std::strcmp(reinterpret_cast<const char *>(candidate), "custom-one"), "disabled entry absent; built-in survives");
    myswy_session_free(session);
    require(myswy::readDictionaryLibrary(library, entries) && !entries[0].enabled, "disabled state persists");
    const auto disabled = entries[0];
    require(myswy::changeDictionaryLibrary(library, disabled, false), "re-enable independently");
    require(myswy::readDictionaryLibrary(library, entries) && entries[0].enabled, "enabled state persists");
    require(myswy::changeDictionaryLibrary(library, entries[0], true), "delete first entry");
    require(myswy::readDictionaryLibrary(library, entries) && entries.size() == 1
            && myswy::changeDictionaryLibrary(library, entries[0], true), "delete last entry retains managed container");
    require(myswy::readDictionaryFile(library, after), "empty library remains valid");
    effective = myswy::loadEffectiveDictionary(after, base);
    require(effective != nullptr, "empty library uses built-in vocabulary");
    myswy_dictionary_free(effective); myswy_dictionary_free(base);
    before = after; write(source, "ni\tbad\t0\n");
    require(!myswy::addDictionaryLibrary(source, library) && myswy::readDictionaryFile(library, after) && before == after,
            "invalid import preserves complete library");
    write(library, "CYLIB\x01");
    require(!myswy::readDictionaryLibrary(library, entries) && !myswy::addDictionaryLibrary(source, library),
            "damaged library is preserved for recovery");
    DeleteFileW(library.c_str());
    DeleteFileW(profilePath.c_str());
    DeleteFileW(scelPath.c_str());
    DeleteFileW(source.c_str());
    DeleteFileW(target.c_str());
    require(RemoveDirectoryW(folder.c_str()) != FALSE, "temporary files cleaned");
    std::puts("PASS: validated atomic vocabulary import, invalid/failure recovery and ownership.");
    return 0;
}
