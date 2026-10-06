#include "settings.h"
#include "preferences.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace myswy {
namespace {
constexpr size_t kMaximum = 64 * 1024 * 1024;
constexpr uint8_t kMagic[] = {'C','Y','L','I','B',1,0,0};
struct ImportGuard {
    HANDLE handle = CreateMutexW(nullptr, FALSE, L"Local\\MyswyIME.DictionaryImport");
    bool held = false;
    ImportGuard() {
        DWORD result = handle ? WaitForSingleObject(handle, 5000) : WAIT_FAILED;
        held = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
    ~ImportGuard() {
        if (held) ReleaseMutex(handle);
        if (handle) CloseHandle(handle);
    }
};
bool managed(const std::vector<uint8_t> &data) {
    return data.size() >= sizeof(kMagic) && std::memcmp(data.data(), kMagic, sizeof(kMagic)) == 0;
}
bool parse(const std::vector<uint8_t> &data, std::vector<DictionaryEntry> &entries) {
    entries.clear();
    if (!managed(data)) {
        auto *dict = loadDictionaryBytes(data);
        if (!dict) return false;
        int size = myswy_dictionary_binary(dict, nullptr, 0);
        DictionaryEntry legacy{L"旧版自定义词库", true, {}};
        if (size > 0) {
            legacy.binary.resize(static_cast<size_t>(size));
            size = myswy_dictionary_binary(dict, legacy.binary.data(), legacy.binary.size());
        }
        myswy_dictionary_free(dict);
        if (size <= 0) return false;
        entries.push_back(std::move(legacy));
        return true;
    }
    size_t at = sizeof(kMagic);
    auto integer = [&](uint32_t &value) {
        if (data.size() - at < 4) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(data[at++]) << (i * 8);
        return true;
    };
    uint32_t count = 0;
    if (!integer(count) || count > 64) return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t enabled = 0, nameSize = 0, binarySize = 0;
        if (!integer(enabled) || !integer(nameSize) || !integer(binarySize) || enabled > 1 || !nameSize
            || nameSize > 1024 || nameSize > data.size() - at) return false;
        int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       reinterpret_cast<const char *>(data.data() + at), nameSize, nullptr, 0);
        if (units <= 0) return false;
        DictionaryEntry entry;
        entry.enabled = enabled != 0;
        entry.name.resize(static_cast<size_t>(units));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(data.data() + at),
                            nameSize, entry.name.data(), units);
        if (entry.name.find_first_of(L"\0\r\n\t", 0, 4) != std::wstring::npos) return false;
        at += nameSize;
        if (!binarySize || binarySize > data.size() - at) return false;
        entry.binary.assign(data.begin() + at, data.begin() + at + binarySize);
        at += binarySize;
        auto *dictionary = myswy_dictionary_new_binary(entry.binary.data(), entry.binary.size());
        if (!dictionary) return false;
        myswy_dictionary_free(dictionary);
        entries.push_back(std::move(entry));
    }
    return at == data.size();
}
bool store(const std::wstring &path, const std::vector<DictionaryEntry> &entries) {
    std::vector<uint8_t> data(std::begin(kMagic), std::end(kMagic));
    auto integer = [&](uint32_t value) {
        for (int i = 0; i < 4; ++i) data.push_back(static_cast<uint8_t>(value >> (i * 8)));
    };
    integer(static_cast<uint32_t>(entries.size()));
    for (const auto &entry : entries) {
        int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, entry.name.data(),
                                      static_cast<int>(entry.name.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0 || size > 1024 || entry.binary.size() > kMaximum) return false;
        std::string name(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, entry.name.data(), static_cast<int>(entry.name.size()),
                            name.data(), size, nullptr, nullptr);
        integer(entry.enabled ? 1 : 0);
        integer(static_cast<uint32_t>(name.size()));
        integer(static_cast<uint32_t>(entry.binary.size()));
        data.insert(data.end(), name.begin(), name.end());
        data.insert(data.end(), entry.binary.begin(), entry.binary.end());
        if (data.size() > kMaximum) return false;
    }
    if (!atomicWrite(path, data)) return false;
    notifyConfiguration();
    return true;
}
}
bool readDictionaryLibrary(const std::wstring &path, std::vector<DictionaryEntry> &entries) {
    entries.clear();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    std::vector<uint8_t> bytes;
    return readDictionaryFile(path, bytes) && parse(bytes, entries);
}
bool addDictionaryLibrary(const std::wstring &source, const std::wstring &target) {
    ImportGuard guard;
    if (!guard.held || target.empty()) return false;
    std::vector<DictionaryEntry> entries;
    if (!readDictionaryLibrary(target, entries) || entries.size() >= 64) return false;
    std::vector<uint8_t> bytes;
    if (!readDictionaryFile(source, bytes)) return false;
    auto *dictionary = loadDictionaryBytes(bytes);
    if (!dictionary) return false;
    int size = myswy_dictionary_binary(dictionary, nullptr, 0);
    DictionaryEntry entry;
    entry.name = source.substr(source.find_last_of(L"\\/") + 1);
    if (size > 0) {
        entry.binary.resize(static_cast<size_t>(size));
        size = myswy_dictionary_binary(dictionary, entry.binary.data(), entry.binary.size());
    }
    myswy_dictionary_free(dictionary);
    if (size <= 0) return false;
    // Reimporting identical bytes is harmless and does not add duplicate rows.
    for (const auto &existing : entries)
        if (existing.binary == entry.binary) return true;
    const auto originalName = entry.name;
    for (unsigned suffix = 2; std::any_of(entries.begin(), entries.end(), [&](const auto &existing) { return existing.name == entry.name; }); ++suffix)
        entry.name = originalName + L" (" + std::to_wstring(suffix) + L")";
    entries.push_back(std::move(entry));
    return store(target, entries);
}
bool changeDictionaryLibrary(const std::wstring &target, const DictionaryEntry &expected, bool remove) {
    ImportGuard guard;
    std::vector<DictionaryEntry> entries;
    if (!guard.held || !readDictionaryLibrary(target, entries)) return false;
    auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return entry.name == expected.name && entry.binary == expected.binary && entry.enabled == expected.enabled;
    });
    if (found == entries.end()) return false; // stale settings window
    if (remove) entries.erase(found);
    else found->enabled = !found->enabled;
    return store(target, entries);
}
MyswyDictionary *loadEffectiveDictionary(const std::vector<uint8_t> &bytes, MyswyDictionary *base) {
    if (!managed(bytes)) return loadDictionaryBytes(bytes); // preserve old configuration until first edit
    std::vector<DictionaryEntry> entries;
    if (!base || !parse(bytes, entries)) return nullptr;
    // The base and every enabled library are compiled into one union in a single
    // pass. Merging pair by pair would rebuild the whole dictionary once per
    // library, so a library holding many vocabularies would pay a much higher
    // peak for the same result (review R12). The base stays the first element so
    // an all-disabled library still yields exactly the built-in vocabulary.
    std::vector<MyswyDictionary *> parts{base};
    for (const auto &entry : entries) {
        if (!entry.enabled) continue;
        auto *dictionary = myswy_dictionary_new_binary(entry.binary.data(), entry.binary.size());
        if (!dictionary) {
            for (size_t i = 1; i < parts.size(); ++i) myswy_dictionary_free(parts[i]);
            return nullptr;
        }
        parts.push_back(dictionary);
    }
    // The clone keeps the caller's `base` ownership contract intact, and avoids a
    // pointless rebuild for the common all-disabled library.
    MyswyDictionary *result = parts.size() == 1
        ? myswy_dictionary_clone(base)
        : myswy_dictionary_merge_all(parts.data(), parts.size());
    for (size_t i = 1; i < parts.size(); ++i) myswy_dictionary_free(parts[i]);
    return result;
}
}
