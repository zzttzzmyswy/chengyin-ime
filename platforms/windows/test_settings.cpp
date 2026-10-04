#include "settings.h"
#include "preferences.h"
#include "learning_writer.h"
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
    require(myswy::savePreferences(preferences, prefs), "save Unicode preferences");
    auto restored = myswy::loadPreferences(preferences);
    require(restored.font == prefs.font && restored.fontSize == 23 && restored.theme == 2 && restored.layout == 1
            && restored.density == 0 && restored.pageSize == 7 && restored.shiftSwitch == 2
            && restored.candidatePinyin && !restored.separators && !restored.learning && !restored.associations
            && restored.defaultEnglish && !restored.caretFallback, "all preferences survive reload");
    require(myswy::readSmallFile(preferences, before, 8192), "preferences snapshot");
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
        // Independent workers must merge disk state, and drain before unloading.
        myswy::LearningWriter first(profilePath), second(profilePath);
        for (int i = 0; i < 20; ++i) {
            require(first.enqueue(bytes("nihao"), 5, bytes("你好"), 6), "first writer queue");
            require(second.enqueue(bytes("nihao"), 5, bytes("拟好"), 6), "second writer queue");
        }
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
    auto *learned = myswy::loadProfile(profilePath);
    require(learned && myswy_profile_count(learned) == 2, "concurrent workers preserve both choices");
    require(myswy::readSmallFile(profilePath, before, 2 * 1024 * 1024), "learning snapshot");
    require(myswy::saveProfile(source, learned), "backup learning profile");
    myswy_profile_free(learned);
    held = CreateFileW(profilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
    require(held != INVALID_HANDLE_VALUE, "hold learning file");
    require(!myswy::updateProfile(profilePath, bytes("hao"), 3, bytes("好"), 3),
            "locked profile cannot be replaced");
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
    write(profilePath, "damaged");
    require(!myswy::loadProfile(profilePath), "damaged profile does not silently reset");
    require(!myswy::updateProfile(profilePath, bytes("hao"), 3, bytes("好"), 3),
            "record preserves damaged profile for recovery");
    write(profilePath, "");
    require(!myswy::loadProfile(profilePath), "empty existing profile is damaged");
    DeleteFileW(preferences.c_str());
    DeleteFileW(profilePath.c_str());
    DeleteFileW(scelPath.c_str());
    DeleteFileW(source.c_str());
    DeleteFileW(target.c_str());
    require(RemoveDirectoryW(folder.c_str()) != FALSE, "temporary files cleaned");
    std::puts("PASS: validated atomic vocabulary import, invalid/failure recovery and ownership.");
    return 0;
}
