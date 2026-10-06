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
// Vertical side of the caret the popup is placed on.
enum class Placement { below, above };
struct PlacementInput {
    RECT caret{};
    RECT work{};
    int width = 0, height = 0, gap = 0;
    int slack = 0;  // caret-top movement that invalidates the sticky side
    int band = 0;   // extra room below needed before leaving the upper side
};
// Memory for one composition: the side chosen so far together with the caret it
// was chosen for.
struct PlacementState {
    Placement side = Placement::below;
    LONG caretTop = 0;
    bool valid = false;
};
struct PlacementResult {
    int left = 0, top = 0;
    Placement side = Placement::below;
};
// Window-free so the flip and hysteresis rules are unit testable.
PlacementResult placeCandidates(const PlacementInput &input, PlacementState &state);
class CandidateWindow {
  public:
    using Choice = void (*)(void *, uint64_t, int);
    ~CandidateWindow();
    void show(MyswySession *, HWND owner, RECT caret, bool limited,
              void *target = nullptr, Choice choice = nullptr, uint64_t generation = 0,
              const Preferences &preferences = Preferences{}, bool inlineEditable = false);
    void hide();
    void refreshPreferences(MyswySession *, const Preferences &);
    HWND handle() const { return hwnd_; }
  private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    void paint();
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT smallFont_ = nullptr;
    HFONT boldPinyinFont_ = nullptr;
    HFONT footerFont_ = nullptr;
    HDC buffer_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ originalBitmap_ = nullptr;
    int bufferWidth_ = 0, bufferHeight_ = 0;
    WideText rows_[10] {};
    WideText pinyin_[9] {};
    uint8_t marks_[9][256] {};
    RECT items_[9] {}, footerRect_{};
    RECT textRects_[9]{}, pinyinRects_[9]{}, contentRect_{};
    WideText footer_{};
    bool previous_ = false, next_ = false;
    bool association_ = false;
    int count_ = 0;
    int selected_ = -1;
    int rowHeight_ = 28;
    int padding_ = 10;
    int rail_ = 0;
    UINT fontDpi_ = 0;
    Preferences preferences_{};
    int fontSize_ = 0;
    std::wstring fontFace_;
    int theme_ = -1;
    Palette colors_{};
    int hover_ = -1, headerHeight_ = 32, footerHeight_ = 24;
    int shapeWidth_ = 0, shapeHeight_ = 0, shapeRadius_ = -1;
    int numberWidth_ = 0, scrollOffset_ = 0, scrollMaximum_ = 0;
    bool inlineEditable_ = false, showPinyin_ = false;
    PlacementState placement_{};
    HMONITOR placementMonitor_ = nullptr;
    RECT requestedCaret_{};
    bool limited_ = false;
    void *target_ = nullptr;
    Choice choice_ = nullptr;
    uint64_t generation_ = 0, pressedGeneration_ = 0;
    int pressed_ = -1;
    int hit(LPARAM) const;
};
}
