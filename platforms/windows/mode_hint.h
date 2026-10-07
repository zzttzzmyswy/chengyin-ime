#pragma once
#include "candidate.h"
namespace chengyin {
// Public so the native fixtures can locate this popup the way they already
// locate the candidate window.
inline constexpr wchar_t kModeHintClass[] = L"Chengyin.ModeHint.Preview1";
// Window-free hint lifetime: a repeated switch pushes the deadline out instead
// of stacking timers, so "update the text and restart the countdown" is decided
// somewhere testable rather than only inside a message loop.
struct HintTimer {
    static constexpr unsigned duration = 900;
    bool visible = false;
    ULONGLONG deadline = 0;
    void restart(ULONGLONG now) {
        visible = true;
        deadline = now + duration;
    }
    void stop() {
        visible = false;
    }
    bool due(ULONGLONG now) const {
        return visible && now >= deadline;
    }
};
// A one-character 中/英 status popup shown next to the caret after the user
// switches input mode. It never activates and never takes the foreground, is
// click-through, and does no COM or user-file I/O on this path; the window lives
// on the TSF thread exactly like the candidate window.
class ModeHintWindow {
  public:
    ~ModeHintWindow();
    void show(HWND owner, RECT caret, bool english, const Preferences &, ULONGLONG now);
    void elapsed(ULONGLONG now); // the WM_TIMER path
    void hide();
    HWND handle() const { return hwnd_; }
    bool visible() const { return hwnd_ && IsWindowVisible(hwnd_) != FALSE; }
    const wchar_t *glyph() const { return text_; }
  private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    void paint();
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    UINT fontDpi_ = 0;
    int fontSize_ = 0;
    std::wstring fontFace_;
    wchar_t text_[2] = L"中";
    Preferences preferences_{};
    Palette colors_{};
    // Placement is re-decided on every switch, so the hint keeps no hysteresis.
    PlacementState placement_{};
    HintTimer timer_{};
};
}
