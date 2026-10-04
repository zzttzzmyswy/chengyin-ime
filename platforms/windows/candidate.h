#pragma once
#include "common.h"
#include "myswy_ime.h"
#include "preferences.h"

namespace myswy {
struct WideText {
    wchar_t data[MYSWY_MAX_TEXT_BYTES + 65] {};
    int length = 0;
};
bool readText(MyswySession *session, uint32_t field, size_t index, WideText &out);
class CandidateWindow {
  public:
    using Choice = void (*)(void *, uint64_t, int);
    ~CandidateWindow();
    void show(MyswySession *, HWND owner, RECT caret, bool limited,
              void *target = nullptr, Choice choice = nullptr, uint64_t generation = 0,
              const Preferences &preferences = Preferences{});
    void hide();
  private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    void paint();
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT smallFont_ = nullptr;
    HDC buffer_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ originalBitmap_ = nullptr;
    int bufferWidth_ = 0, bufferHeight_ = 0;
    WideText rows_[10] {};
    WideText pinyin_[9] {};
    RECT items_[9] {}, footerRect_{};
    WideText footer_{};
    bool previous_ = false, next_ = false;
    bool association_ = false;
    int count_ = 0;
    int selected_ = -1;
    int rowHeight_ = 28;
    int padding_ = 10;
    UINT fontDpi_ = 0;
    Preferences preferences_{};
    int fontSize_ = 0;
    std::wstring fontFace_;
    int theme_ = -1;
    Palette colors_{};
    int hover_ = -1, headerHeight_ = 32, footerHeight_ = 24;
    void *target_ = nullptr;
    Choice choice_ = nullptr;
    uint64_t generation_ = 0, pressedGeneration_ = 0;
    int pressed_ = -1;
    int hit(LPARAM) const;
};
}
