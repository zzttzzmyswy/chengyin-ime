#include "preferences.h"
#include "settings.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <sstream>
#include <map>
namespace myswy {
namespace {
struct Lock {
    HANDLE handle = CreateMutexW(nullptr, FALSE, L"Local\\MyswyIME.UserPreferences");
    bool held = false;
    Lock() {
        if (handle) {
            DWORD result = WaitForSingleObject(handle, 5000);
            held = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }
    ~Lock() {
        if (held)
            ReleaseMutex(handle);
        if (handle)
            CloseHandle(handle);
    }
};
}
std::wstring userFile(const wchar_t *name, bool create) {
    auto path = customDictionaryPath(create);
    const auto slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring{} :
           path.substr(0, slash + 1) + name;
}
bool readSmallFile(const std::wstring &path, std::vector<uint8_t> &bytes, size_t limit) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    DWORD count = 0;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && static_cast<ULONGLONG>(size.QuadPart) <= limit;
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)
             && count == bytes.size();
    }
    CloseHandle(file);
    return ok;
}
bool atomicWrite(const std::wstring &path, const std::vector<uint8_t> &bytes) {
    if (path.empty())
        return false;
    std::wstring temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int n = 0; n < 16 && file == INVALID_HANDLE_VALUE; ++n) {
        temporary = path + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(
                        GetCurrentThreadId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(n);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD count = 0;
    bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)
              && count == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok)
        ok = MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok)
        DeleteFileW(temporary.c_str());
    return ok;
}
bool validPreferences(const Preferences &p) {
    return p.fontSize >= 12 && p.fontSize <= 32 && p.theme >= 0 && p.theme <= 2 && p.layout >= 0 && p.layout <= 1
           && p.density >= 0 && p.density <= 1 && (p.pageSize == 5 || p.pageSize == 7 || p.pageSize == 9)
           && p.shiftSwitch >= 0 && p.shiftSwitch <= 2 && !p.font.empty() && p.font.size() < LF_FACESIZE
           && p.font.find_first_of(L"\r\n\t=\0", 0, 5) == std::wstring::npos
           && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, p.font.data(), static_cast<int>(p.font.size()), nullptr,
                                  0, nullptr, nullptr) > 0;
}
bool tryLoadPreferences(const std::wstring &path, Preferences &result) {
    Preferences p;
    std::vector<uint8_t> bytes;
    if (!readSmallFile(path, bytes, 8192) || bytes.size() < 2 || bytes[0] != 0xff || bytes[1] != 0xfe
            || bytes.size() % 2)
        return false;
    std::wstring content((bytes.size() - 2) / 2, L'\0');
    std::memcpy(content.data(), bytes.data() + 2, bytes.size() - 2);
    std::wistringstream stream(content);
    std::wstring line;
    std::map<std::wstring, std::wstring> values;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        const auto at = line.find(L'=');
        if (at != std::wstring::npos) {
            if (!values.emplace(line.substr(0, at), line.substr(at + 1)).second)
                return false;
        }
    }
    if (values[L"Version"] != L"1")
        return false;
    if (values.count(L"Font"))
        p.font = values[L"Font"];
    auto integer = [&](const wchar_t *key, int fallback) {
        auto at = values.find(key);
        if (at == values.end())
            return fallback;
        wchar_t *end = nullptr;
        long n = std::wcstol(at->second.c_str(), &end, 10);
        return !at->second.empty() && end && *end == 0 && n >= 0 && n <= 100 ? static_cast<int>(n) : -1;
    };
    p.fontSize = integer(L"FontSize", p.fontSize);
    p.theme = integer(L"Theme", p.theme);
    p.layout = integer(L"Layout", p.layout);
    p.density = integer(L"Density", p.density);
    p.pageSize = integer(L"PageSize", p.pageSize);
    p.shiftSwitch = integer(L"ShiftSwitch", p.shiftSwitch);
    auto flag = [&](const wchar_t *key, bool & target) {
        int n = integer(key, target ? 1 : 0);
        if (n < 0 || n > 1)
            return false;
        target = n != 0;
        return true;
    };
    bool ok = flag(L"Separators", p.separators) && flag(L"CandidatePinyin", p.candidatePinyin)
              && flag(L"Learning", p.learning)
              && flag(L"Associations", p.associations) && flag(L"DefaultEnglish", p.defaultEnglish)
              && flag(L"CaretFallback", p.caretFallback) && flag(L"ChinesePunctuation", p.chinesePunctuation);
    if (!ok || !validPreferences(p))
        return false;
    result = std::move(p);
    return true;
}
Preferences loadPreferences(const std::wstring &path) {
    Preferences p;
    tryLoadPreferences(path, p);
    return p;
}
bool savePreferences(const std::wstring &path, const Preferences &p) {
    if (!validPreferences(p))
        return false;
    std::wostringstream s;
    s << L"Version=1\nFont=" << p.font << L"\nFontSize=" << p.fontSize << L"\nTheme=" << p.theme
      << L"\nLayout=" << p.layout << L"\nDensity=" << p.density << L"\nPageSize=" << p.pageSize << L"\nShiftSwitch="
      << p.shiftSwitch
      << L"\nSeparators=" << p.separators << L"\nCandidatePinyin=" << p.candidatePinyin << L"\nLearning=" <<
      p.learning
      << L"\nAssociations=" << p.associations << L"\nDefaultEnglish=" << p.defaultEnglish << L"\nCaretFallback=" <<
      p.caretFallback << L"\nChinesePunctuation=" << p.chinesePunctuation << L"\n";
    const auto text = s.str();
    std::vector<uint8_t> bytes(2 + text.size()*sizeof(wchar_t));
    bytes[0] = 0xff;
    bytes[1] = 0xfe;
    std::memcpy(bytes.data() + 2, text.data(), text.size()*sizeof(wchar_t));
    Lock lock;
    const bool ok = lock.held && atomicWrite(path, bytes);
    if (ok)
        notifyConfiguration();
    return ok;
}
MyswyProfile *loadProfile(const std::wstring &path) {
    if (path.empty())
        return myswy_profile_new(nullptr, 0);
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND
               || error == ERROR_PATH_NOT_FOUND ? myswy_profile_new(nullptr, 0) : nullptr;
    }
    std::vector<uint8_t> bytes;
    return readSmallFile(path, bytes, 2 * 1024 * 1024)
           && !bytes.empty() ? myswy_profile_new(bytes.data(), bytes.size()) : nullptr;
}
bool saveProfile(const std::wstring &path, const MyswyProfile *p) {
    int size = myswy_profile_binary(p, nullptr, 0);
    if (size <= 0 || size > 2 * 1024 * 1024)
        return false;
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    return myswy_profile_binary(p, bytes.data(), bytes.size()) == size && atomicWrite(path, bytes);
}
LearningEpoch::LearningEpoch(const wchar_t *name) {
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LONG),
                                  name);
    if (mapping_)
        value_ = static_cast<volatile LONG *>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LONG)));
}
LearningEpoch::~LearningEpoch() {
    if (value_)
        UnmapViewOfFile(const_cast<LONG *>(value_));
    if (mapping_)
        CloseHandle(mapping_);
}
DWORD LearningEpoch::current() const {
    return value_ ? static_cast<DWORD>(InterlockedCompareExchange(value_, 0, 0)) : 0;
}
void LearningEpoch::advance() {
    if (value_)
        InterlockedIncrement(value_);
}
void notifyConfiguration() {
    LearningEpoch epoch(L"Local\\MyswyIME.ConfigurationGeneration");
    epoch.advance();
}
bool updateProfile(const std::wstring &path, const uint8_t *key, size_t keySize, const uint8_t *text,
                   size_t textSize, const DWORD *expectedEpoch) {
    Lock lock;
    if (!lock.held)
        return false;
    LearningEpoch epoch;
    if (expectedEpoch && !epoch.valid())
        return false;
    if (expectedEpoch && epoch.current() != *expectedEpoch)
        return true; // invalidated event: intentional no-op
    auto *profile = loadProfile(path);
    if (!profile)
        return false;
    bool ok = myswy_profile_record(profile, key, keySize, text, textSize) == 0 && saveProfile(path, profile);
    myswy_profile_free(profile);
    return ok;
}
bool importProfile(const std::wstring &source, const std::wstring &target) {
    std::vector<uint8_t> bytes;
    if (!readSmallFile(source, bytes, 2 * 1024 * 1024) || bytes.empty())
        return false;
    auto *profile = myswy_profile_new(bytes.data(), bytes.size());
    if (!profile)
        return false;
    Lock lock;
    LearningEpoch epoch;
    bool ok = lock.held && epoch.valid() && saveProfile(target, profile);
    if (ok) {
        epoch.advance();
        notifyConfiguration();
    }
    myswy_profile_free(profile);
    return ok;
}
bool clearProfile(const std::wstring &path) {
    Lock lock;
    if (!lock.held)
        return false;
    LearningEpoch epoch;
    if (!epoch.valid())
        return false;
    bool ok = DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    if (ok) {
        epoch.advance();
        notifyConfiguration();
    }
    return ok;
}
UINT windowDpi(HWND hwnd) {
    using Function = UINT(WINAPI *)(HWND);
    auto address = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    Function function = nullptr;
    std::memcpy(&function, &address, sizeof(function));
    UINT dpi = function && hwnd ? function(hwnd) : 96;
    return dpi ? dpi : 96;
}
namespace {
int CALLBACK foundFont(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM found) {
    *reinterpret_cast<bool *>(found) = true;
    return 0;
}
bool availableFont(const std::wstring &name) {
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    std::wcsncpy(query.lfFaceName, name.c_str(), LF_FACESIZE - 1);
    HDC dc = GetDC(nullptr);
    bool found = false;
    if (dc) {
        EnumFontFamiliesExW(dc, &query, foundFont, reinterpret_cast<LPARAM>(&found), 0);
        ReleaseDC(nullptr, dc);
    }
    return found;
}
}
HFONT createUIFont(int size, UINT dpi, const std::wstring &face, int weight) {
    std::wstring chosen = face;
    if (!availableFont(chosen)) {
        for (const wchar_t *fallback : {
                    L"Microsoft YaHei UI", L"Microsoft YaHei", L"Noto Sans CJK SC", L"Segoe UI"
                }) {
            if (availableFont(fallback)) {
                chosen = fallback;
                break;
            }
        }
    }
    return CreateFontW(-MulDiv(size, static_cast<int>(dpi), 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, chosen.c_str());
}
Palette palette(int theme) {
    HIGHCONTRASTW contrast{sizeof(contrast), 0, nullptr};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
            && (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT),
                GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT)};
    }
    if (theme == 0)
        return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT),
                GetSysColor(COLOR_GRAYTEXT), GetSysColor(COLOR_WINDOWFRAME), GetSysColor(COLOR_HIGHLIGHT),
                GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT)};
    if (theme == 2)
        return {RGB(32, 32, 32), RGB(32, 32, 32), RGB(245, 245, 245), RGB(190, 190, 190), RGB(100, 100, 100),
                GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT)};
    return {RGB(240, 240, 240), RGB(255, 255, 255), RGB(0, 0, 0), RGB(100, 100, 100), RGB(100, 100, 100),
            GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT)};
}
}
