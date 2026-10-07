#include "settings.h"
#include "preferences.h"
#include <shlobj.h>
#include <commdlg.h>
#include <algorithm>
#include <cwchar>
#include <cstring>

namespace chengyin {
namespace {
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
struct ImportLock {
    HANDLE h = CreateMutexW(nullptr, FALSE, L"Local\\ChengyinIME.DictionaryImport");
    bool owned = false;
    ImportLock() {
        if (h) {
            const DWORD result = WaitForSingleObject(h, 5000);
            owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }
    ~ImportLock() {
        if (owned)
            ReleaseMutex(h);
        if (h)
            CloseHandle(h);
    }
};
constexpr size_t kMaximum = 64 * 1024 * 1024;
// preview24 and earlier stored user vocabulary, preferences and learning under
// the former project name. Adopt that directory on first use so an upgrade
// neither loses data nor silently starts from an empty profile.
constexpr wchar_t kLegacyFolderName[] = L"MyswyIME";
bool directoryExists(const std::wstring &path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}
// Re-points `path` at the legacy directory when only that one exists. Never
// moves or copies: the old directory stays readable by an older preview, and
// a failure simply leaves the new (empty) directory in place.
void adoptLegacySettingsFolder(std::wstring &path) {
    if (directoryExists(path))
        return;
    const auto slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return;
    const std::wstring legacy = path.substr(0, slash + 1) + kLegacyFolderName;
    if (directoryExists(legacy))
        path = legacy;
}
}
std::wstring customDictionaryPath(bool create) {
#ifdef CHENGYIN_ISOLATED_UI_TEST
    wchar_t temporary[MAX_PATH] {};
    if (!GetTempPathW(MAX_PATH, temporary)) return {};
    std::wstring path = std::wstring(temporary) + L"Chengyin-UI-fixture-" + std::to_wstring(GetCurrentProcessId());
#else
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
        return {};
    std::wstring path(folder);
    CoTaskMemFree(folder);
    path += L"\\ChengyinIME";
    // Checked before the create below so a first run after an upgrade keeps
    // reading and writing the adopted directory rather than a fresh empty one.
    adoptLegacySettingsFolder(path);
#endif
    if (create && !CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return {};
    if (create && (GetFileAttributesW(path.c_str())&FILE_ATTRIBUTE_REPARSE_POINT))
        return {};
    return path + L"\\dictionary.custom";
}
bool readDictionaryFile(const std::wstring &path, std::vector<uint8_t> &output) {
    Handle file;
    file.h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file.h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.h, &size) || size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(kMaximum))
        return false;
    output.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < output.size()) {
        DWORD read = 0;
        const DWORD count = static_cast<DWORD>(std::min<size_t>(output.size() - offset, 1024 * 1024));
        if (!ReadFile(file.h, output.data() + offset, count, &read, nullptr) || !read)
            return false;
        offset += read;
    }
    return true;
}
ChengyinDictionary *loadDictionaryBytes(const std::vector<uint8_t> &bytes) {
    auto *dictionary = chengyin_dictionary_new_import(bytes.data(), bytes.size());
    if (dictionary || bytes.size() > kMaximum || bytes.empty() || bytes[0] == 0x40 || bytes[0] == 0xff
            || bytes[0] == 'M')
        return dictionary;
    // Sogou's legacy text export is GBK. Conversion lives in the Windows
    // adapter; the shared parser only accepts unambiguous Unicode encodings.
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(bytes.data()),
                            static_cast<int>(bytes.size()), nullptr, 0) > 0)
        return nullptr;
    const int count = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(bytes.data()),
                                          static_cast<int>(bytes.size()), nullptr, 0);
    if (count <= 0 || static_cast<size_t>(count) > kMaximum / 2 - 1)
        return nullptr;
    std::vector<wchar_t> wide(static_cast<size_t>(count));
    if (MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(bytes.data()),
                            static_cast<int>(bytes.size()), wide.data(), count) != count)
        return nullptr;
    std::vector<uint8_t> unicode{0xff, 0xfe};
    unicode.reserve(2 + wide.size() * 2);
    for (wchar_t unit : wide) {
        unicode.push_back(static_cast<uint8_t>(unit));
        unicode.push_back(static_cast<uint8_t>(unit >> 8));
    }
    return chengyin_dictionary_new_import(unicode.data(), unicode.size());
}
bool installCustomDictionary(const std::wstring &source, const std::wstring &target, bool append) {
    if (target.empty())
        return false;
    ImportLock lock;
    if (!lock.owned)
        return false;
    std::vector<uint8_t> bytes;
    if (!readDictionaryFile(source, bytes))
        return false;
    if (source.size() >= 5 && _wcsicmp(source.c_str() + source.size() - 5, L".scel") == 0 &&
            (bytes.size() < 4 || bytes[0] != 0x40 || bytes[1] != 0x15 || bytes[2] != 0 || bytes[3] != 0))
        return false;
    auto *dictionary = loadDictionaryBytes(bytes);
    if (!dictionary)
        return false;
    if (append) {
        ChengyinDictionary *base = nullptr;
        const DWORD attributes = GetFileAttributesW(target.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            std::vector<uint8_t> old;
            if (readDictionaryFile(target, old))
                base = loadDictionaryBytes(old);
        } else if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            const HMODULE instance = GetModuleHandleW(nullptr);
            const HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(101), RT_RCDATA);
            const HGLOBAL loaded = resource ? LoadResource(instance, resource) : nullptr;
            const auto *data = loaded ? static_cast<const uint8_t *>(LockResource(loaded)) : nullptr;
            if (data)
                base = chengyin_dictionary_new_binary(data, SizeofResource(instance, resource));
        }
        auto *merged = base ? chengyin_dictionary_merge(base, dictionary) : nullptr;
        chengyin_dictionary_free(base);
        chengyin_dictionary_free(dictionary);
        dictionary = merged;
        if (!dictionary)
            return false;
    }
    const int binarySize = chengyin_dictionary_binary(dictionary, nullptr, 0);
    bool valid = binarySize > 0;
    if (valid) {
        bytes.resize(static_cast<size_t>(binarySize));
        valid = chengyin_dictionary_binary(dictionary, bytes.data(), bytes.size()) == binarySize;
    }
    chengyin_dictionary_free(dictionary);
    if (!valid)
        return false;
    // CREATE_NEW makes concurrent settings windows safe. Rename is atomic and
    // preserves the last valid dictionary if validation/write/rename fails.
    const std::wstring temporary = target + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                   std::to_wstring(GetTickCount64());
    Handle file;
    file.h = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    if (file.h == INVALID_HANDLE_VALUE)
        return false;
    bool success = true;
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const DWORD count = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
        if (!WriteFile(file.h, bytes.data() + offset, count, &written, nullptr) || !written) {
            success = false;
            break;
        }
        offset += written;
    }
    if (success)
        success = FlushFileBuffers(file.h) != FALSE;
    CloseHandle(file.h);
    file.h = INVALID_HANDLE_VALUE;
    if (success)
        success = MoveFileExW(temporary.c_str(), target.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!success)
        DeleteFileW(temporary.c_str());
    else
        notifyConfiguration();
    return success;
}
}
