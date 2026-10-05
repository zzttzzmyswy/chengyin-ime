#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "myswy_ime.h"
namespace myswy {
struct Preferences {
    std::wstring font = L"Microsoft YaHei UI";
    int fontSize = 18;
    int theme = 0; // system/white/black
    int layout = 0; // vertical/horizontal
    int density = 1; // compact/comfortable
    int pageSize = 5;
    int shiftSwitch = 1; // disabled/left/both
    bool separators = true;
    bool candidatePinyin = false; // explicit opt-in, including corrected pronunciation
    bool learning = true;
    bool associations = true;
    bool defaultEnglish = false;
    bool caretFallback = true;
    bool chinesePunctuation = true;
    bool autoUpdate = true; // check releases in settings, install only on user action
    uint32_t matchingOptions = 0;
};
std::wstring userFile(const wchar_t *, bool create = false);
bool readSmallFile(const std::wstring &, std::vector<uint8_t> &, size_t limit);
bool atomicWrite(const std::wstring &, const std::vector<uint8_t> &);
Preferences loadPreferences(const std::wstring &);
bool tryLoadPreferences(const std::wstring &, Preferences &);
bool savePreferences(const std::wstring &, const Preferences &);
bool validPreferences(const Preferences &);
MyswyProfile *loadProfile(const std::wstring &); // missing=>empty, damaged=>NULL
bool saveProfile(const std::wstring &, const MyswyProfile *);
bool importProfile(const std::wstring &source, const std::wstring &target);
bool clearProfile(const std::wstring &);
// Shared only while writers/settings are alive; no key-thread file/registry I/O.
class LearningEpoch {
  public:
    explicit LearningEpoch(const wchar_t *name = L"Local\\MyswyIME.LearningGeneration");
    ~LearningEpoch();
    LearningEpoch(const LearningEpoch &) = delete;
    LearningEpoch &operator=(const LearningEpoch &) = delete;
    bool valid() const {
        return value_ != nullptr;
    }
    DWORD current() const;
    void advance(); // caller holds the cross-process preference lock
  private:
    HANDLE mapping_ = nullptr;
    volatile LONG *value_ = nullptr;
};
void notifyConfiguration();
bool updateProfile(const std::wstring &, const uint8_t *, size_t, const uint8_t *, size_t,
                   const DWORD *expectedEpoch = nullptr, uint32_t matchingFlags = 0);
struct Palette {
    COLORREF background, surface, text, muted, border, accent, selected, selectedText;
};
Palette palette(int theme);
Palette themePalette(int theme, bool systemDark, COLORREF highlight, COLORREF highlightText);
bool systemDarkTheme();
HFONT createUIFont(int size, UINT dpi = 96, const std::wstring &face = L"Microsoft YaHei UI",
                   int weight = FW_NORMAL);
UINT windowDpi(HWND);
}
