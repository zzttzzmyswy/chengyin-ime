#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "chengyin_ime.h"
#include "skin.h"
namespace chengyin {
struct Preferences {
    std::wstring font = L"Microsoft YaHei UI";
    int fontSize = 18;
    int theme = 0; // system/white/black
    std::wstring skinFile; // managed immutable .cyskin basename; no external paths
    std::shared_ptr<const Skin> skin;
    bool skinDecorations = true;
    int layout = 0; // vertical/horizontal
    int density = 1; // compact/comfortable
    int pageSize = 5;
    int shiftSwitch = 1; // disabled/left/both
    bool separators = true;
    bool candidatePinyin = false; // explicit opt-in, including corrected pronunciation
    bool learning = true;
    bool associations = true;
    bool defaultEnglish = false;
    // A brief 中/英 badge near the caret after the user switches mode.
    bool modeHint = true;
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
ChengyinProfile *loadProfile(const std::wstring &); // missing=>empty, damaged=>NULL
bool saveProfile(const std::wstring &, const ChengyinProfile *);
bool importProfile(const std::wstring &source, const std::wstring &target);
bool clearProfile(const std::wstring &);
// Shared only while writers/settings are alive; no key-thread file/registry I/O.
class LearningEpoch {
  public:
    explicit LearningEpoch(const wchar_t *name);
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
// Normalize path spelling and isolate both channels to one profile path.
// The generation moves only on clear/import and means "discard everything".
// The revision moves on every successful ordinary save and means "reload";
// keeping them separate is what stops normal learning from invalidating the
// queued events of another application.
std::wstring profileEpochName(const std::wstring &path);
std::wstring profileRevisionName(const std::wstring &path);
std::wstring configurationEpochName();
void notifyConfiguration();
// Result of applying one queued learning event.
enum class ProfileUpdate {
    saved,       // persisted; the revision has advanced
    invalidated, // a clear/import generation moved: intentional no-op
    retry,       // storage failed; the event is intact and must be retried
};
ProfileUpdate updateProfile(const std::wstring &, const uint8_t *, size_t, const uint8_t *, size_t,
                            const DWORD *expectedEpoch = nullptr, uint32_t matchingFlags = 0);
// Bounded retry budget for one queued event: an event is abandoned as soon as
// either bound is spent, so a permanently unavailable store cannot stall the
// queue or the unload path indefinitely.
struct LearningRetryPolicy {
    unsigned attempts = 4;
    ULONGLONG window = 30000;
    unsigned backoff = 50; // doubling, starting from this many milliseconds
};
// Statistics deliberately carry no input content.
struct LearningWriterStats {
    std::uint64_t saved = 0,     // persisted
        invalidated = 0,         // dropped because a clear/import generation moved
        rejected = 0,            // never queued: full queue or malformed event
        retried = 0,             // failed attempts on storage that was unavailable
        exhausted = 0,           // retry budget spent on unavailable storage
        abandoned = 0;           // still unsaved when shutdown arrived
};
struct Palette {
    COLORREF background, surface, text, muted, border, accent, selected, selectedText;
};
Palette palette(int theme);
Palette palette(const Preferences &);
Palette themePalette(int theme, bool systemDark, COLORREF highlight, COLORREF highlightText);
bool systemDarkTheme();
HFONT createUIFont(int size, UINT dpi = 96, const std::wstring &face = L"Microsoft YaHei UI",
                   int weight = FW_NORMAL);
UINT windowDpi(HWND);
}
