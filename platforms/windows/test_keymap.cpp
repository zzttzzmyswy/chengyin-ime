#include "keymap.h"
#include "candidate.h"
#include "theme_art.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <vector>

namespace myswy {
HINSTANCE module = nullptr;
LONG objects = 0;
}
namespace {
void require(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}
void type(MyswySession *session, const char *text) {
    for (; *text; ++text)
        require(myswy_session_process(session, static_cast<uint32_t>(*text), 0) & MYSWY_HANDLED, "letter consumed");
}
}
int main() {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    using namespace myswy;
    module = GetModuleHandleW(nullptr);
    require(planKey('N', 'n', false, false, false).action == Action::core, "start composition");
    require(planKey('N', 'n', true, false, false).action == Action::pass, "idle Ctrl bypass");
    require(planKey('N', 'n', true, false, true).action == Action::finish, "finish before Ctrl shortcut");
    require(planKey('N', 'n', false, true, false).action == Action::pass, "Caps Lock English");
    require(planKey('N', 'n', false, false, false, true).action == Action::pass, "English mode bypass");
    require(planKey(0x10, 0, false, false, true).action == Action::pass, "Shift alone preserves composition");
    require(planKey(0x09, 0, false, false, true).action == Action::finish, "Tab finishes raw text");
    for (uint32_t vk : {
                0x23u, 0x24u, 0x25u, 0x27u, 0x2eu, 0x21u, 0x22u
            })
        require(planKey(vk, 0, false, false, true).action == Action::core, "composition navigation stays in core");
    require(planKey('1', '!', false, false, true).punctuation == '!', "Shift+1 is punctuation");
    for (char symbol : {'^', '&', 'J', '!', '+'}) {
        require(planKey('J', symbol, false, false, false, false, false, true).action == Action::pass,
                "shifted symbol/capital passes when idle");
        require(planKey('J', symbol, false, false, true, false, false, true).action == Action::finish,
                "shifted symbol/capital finishes raw composition without conversion");
    }
    require(planKey(0xde, '\'', false, false, true).punctuation == 0, "apostrophe separates syllables");
    require(planKey(VK_TAB, 0, false, false, true, false, true).key == MYSWY_KEY_TAB,
            "Tab explicitly accepts idle association");
    for (uint32_t vk : {
                0x20u, 0x0du, 0x08u, 0x31u
            })
        require(planKey(vk, 0, false, false, true, false, true).action == Action::finish,
                "ordinary editing dismisses association and passes key");
    ShiftSwitch shift;
    constexpr uint64_t left = 0x2aULL << 16, right = 0x36ULL << 16;
    require(ShiftSwitch::matches(VK_SHIFT, left, 1)
            && !ShiftSwitch::matches(VK_SHIFT, right, 1), "physical left Shift mapping");
    shift.down(VK_SHIFT, left, 1, false);
    require(shift.up(VK_SHIFT, left, 1, false)
            && !shift.up(VK_SHIFT, left, 1, false), "single Shift toggles once");
    shift.down(VK_SHIFT, left, 1, false);
    shift.down('A', 0, 1, false);
    require(!shift.up(VK_SHIFT, left, 1, false), "Shift letter chord never toggles");
    shift.down(VK_SHIFT, left, 1, true);
    require(!shift.up(VK_SHIFT, left, 1, false), "Ctrl/Alt Shift never toggles");
    shift.down(VK_SHIFT, left | (1ULL << 30), 1, false);
    require(!shift.up(VK_SHIFT, left, 1, false), "orphan repeat never toggles");
    shift.down(VK_SHIFT, left, 1, false);
    shift.down(VK_SHIFT, left | (1ULL << 30), 1, false);
    require(!shift.up(VK_SHIFT, left, 1, false), "held Shift repeat cannot toggle");
    shift.down(VK_LSHIFT, left, 2, false);
    shift.down(VK_RSHIFT, right, 2, false);
    require(!shift.up(VK_RSHIFT, right, 2, false)
            && !shift.up(VK_LSHIFT, left, 2, false), "two Shift chord cannot toggle");
    shift.down(VK_RSHIFT, right, 1, false);
    require(!shift.up(VK_RSHIFT, right, 1, false), "right Shift is disabled by default");
    shift.down(VK_RSHIFT, right, 2, false);
    require(shift.up(VK_RSHIFT, right, 2, false), "both Shift setting supports right Shift");
    shift.down(VK_LSHIFT, left, 0, false);
    require(!shift.pending(), "disabled Shift shortcut");
    // Candidate placement is window-free, so the flip rule and its hysteresis are
    // checked without a desktop: below when it fits, above when it does not, and a
    // stable side while one line wobbles at the work-area edge.
    {
        const RECT work{0, 0, 1000, 800};
        const auto place = [work](RECT caret, int height, PlacementState &state) {
            PlacementInput input;
            input.caret = caret; input.work = work;
            input.width = 200; input.height = height; input.gap = 4;
            input.slack = 2; input.band = 30;
            return placeCandidates(input, state);
        };
        const RECT roomy{100, 100, 101, 120};
        PlacementState state;
        PlacementResult placed = place(roomy, 200, state);
        require(placed.side == Placement::below && placed.top == 124,
                "room below places the popup under the caret");
        require(placed.left == 100, "popup follows the caret horizontally");
        const RECT low{100, 750, 101, 770};
        placed = place(low, 200, state);
        require(placed.side == Placement::above && placed.top == 546,
                "no room below places the popup above the caret");
        // Candidate count changes move the height by one row; the side stays up
        // until the difference is large enough to have caused the flip.
        placed = place(low, 170, state);
        require(placed.side == Placement::above && placed.top == 576,
                "a shorter popup at the same edge keeps the upper side");
        placed = place(low, 230, state);
        require(placed.side == Placement::above && placed.top == 516,
                "a taller popup at the same edge keeps the upper side");
        // The band is the real guard: a popup that now fits below stays above
        // while the room is short of what the flip up had to recover.
        PlacementState banded;
        placed = place(RECT{100, 640, 101, 660}, 160, banded);
        require(placed.side == Placement::above && placed.top == 476,
                "a popup that does not fit below starts above");
        placed = place(RECT{100, 640, 101, 660}, 130, banded);
        require(placed.side == Placement::above,
                "fitting below by less than one row does not drop the popup back");
        placed = place(RECT{100, 640, 101, 660}, 100, banded);
        require(placed.side == Placement::below && placed.top == 664,
                "enough room below returns the popup under the caret");
        // A new line decides again from the space alone.
        placed = place(RECT{100, 750, 101, 770}, 200, banded);
        require(placed.side == Placement::above && placed.top == 546,
                "a new line near the work-area edge flips above");
        PlacementState settled;
        placed = place(RECT{100, 300, 101, 330}, 200, settled);
        require(placed.side == Placement::below && placed.top == 334,
                "a caret with room below places the popup under it");
        placed = place(RECT{100, 300, 101, 331}, 220, settled);
        require(placed.side == Placement::below && placed.top == 335,
                "a one-pixel caret-height wobble keeps the side and only moves the top");
        // A new composition starts without memory, so the first frame is decided
        // by the space alone.
        PlacementState fresh;
        placed = place(RECT{100, 100, 101, 120}, 200, fresh);
        require(placed.side == Placement::below, "a fresh composition starts below when it fits");
        placed = place(RECT{100, 700, 101, 720}, 200, fresh);
        require(placed.side == Placement::above, "a fresh composition flips up when it does not fit");
        // A popup taller than the work area is pinned inside it, not off-screen.
        placed = place(RECT{100, 790, 101, 799}, 900, fresh);
        require(placed.top == 0 && placed.side == Placement::above,
                "an oversized popup is clamped into the work area");
        // A caret may sit on a second monitor whose work area starts at 2000; the
        // popup follows that monitor instead of using the first one's origin.
        PlacementState second;
        PlacementInput other;
        other.caret = RECT{2020, 760, 2021, 780};
        other.work = RECT{2000, 0, 3000, 800};
        other.width = 200; other.height = 200; other.gap = 4; other.slack = 2; other.band = 30;
        placed = placeCandidates(other, second);
        require(placed.left == 2020 && placed.top == 556 && placed.side == Placement::above,
                "a caret near the bottom of the second monitor flips up inside that work area");
        other.caret = RECT{2020, 100, 2021, 120};
        placed = placeCandidates(other, second);
        require(placed.left == 2020 && placed.top == 124 && placed.side == Placement::below,
                "a caret with room on the second monitor places the popup under it");
        // Clamping never leaves the caret's own work area: a caret at the right
        // edge of the second monitor shifts the popup back inside it.
        other.caret = RECT{2960, 100, 2961, 120};
        placed = placeCandidates(other, second);
        require(placed.left == 2800 && placed.top == 124,
                "a popup at the right work-area edge is clamped inside that monitor");
    }
    MyswySession *session = myswy_session_new();
    require(session != nullptr, "session");
    type(session, "nihao");
    for (int i = 0; i < 10; ++i)
        require(planKey(0x20, ' ', false, false, true).key == MYSWY_KEY_ENTER, "repeated raw-space planning");
    WideText output;
    require(readText(session, MYSWY_TEXT_PREEDIT, 0, output)
            && std::wcscmp(output.data, L"nihao") == 0, "test key planning is pure");
    const KeyPlan comma = planKey(0xbc, ',', false, false, true);
    require(myswy_session_process(session, comma.key, 0) == 0, "core punctuation returns unhandled commit");
    require(readText(session, MYSWY_TEXT_COMMIT, 0, output)
            && std::wcscmp(output.data, L"你好") == 0, "commit before punctuation");
    output.data[output.length++] = static_cast<wchar_t>(comma.punctuation);
    require(std::wcscmp(output.data, L"你好,") == 0, "single ordered insertion");
    type(session, "nihao");
    require(myswy_session_process(session, MYSWY_KEY_ESCAPE, 0) == MYSWY_HANDLED, "cancel");
    require(readText(session, MYSWY_TEXT_COMMIT, 0, output) && output.length == 0, "cancel does not commit");
    myswy_session_free(session);
    const char tsv[] = "x\t\xf0\x9f\x98\x80\t1\n";
    MyswyDictionary *dict = myswy_dictionary_new_tsv(reinterpret_cast<const uint8_t *>(tsv), std::strlen(tsv));
    require(dict != nullptr, "emoji dictionary");
    session = myswy_session_new_with_dictionary(dict);
    myswy_dictionary_free(dict);
    type(session, "x");
    require(readText(session, MYSWY_TEXT_CANDIDATE, 0, output) && output.length == 2 &&
            output.data[0] == 0xd83d && output.data[1] == 0xde00, "UTF-8 to UTF-16 surrogate pair");
    myswy_session_free(session);
    session = myswy_session_new();
    type(session, "nihao");
    {
        CandidateWindow candidates;
        struct Clicks {
            int count = 0, index = -1;
            uint64_t generation = 0;
        } clicks;
        const auto clicked = [](void *target, uint64_t generation, int index) {
            auto *result = static_cast<Clicks *>(target);
            ++result->count;
            result->index = index;
            result->generation = generation;
        };
        HWND owner = CreateWindowW(L"STATIC", L"Myswy test owner", WS_OVERLAPPEDWINDOW,
                                   0, 0, 300, 200, nullptr, nullptr, module, nullptr);
        require(owner != nullptr, "candidate test owner");
        Preferences headerPrefs; headerPrefs.candidatePinyin = true;
        candidates.show(session, owner, RECT{100, 100, 101, 120}, false, &clicks, clicked, 1, headerPrefs);
        HWND popup = candidates.handle();
        if (!popup || !IsWindowVisible(popup))
            std::fprintf(stderr, "Popup=%p visible=%d error=%lu\n", popup, popup ? IsWindowVisible(popup) : 0, GetLastError());
        require(popup && IsWindowVisible(popup)
                && (GetWindowLongPtrW(popup, GWL_EXSTYLE) & WS_EX_NOACTIVATE), "passive candidate popup");
        const HWND focus = GetFocus();
        RECT stable{};
        GetWindowRect(popup, &stable);
        const DWORD gdiCold = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        // show() deliberately defers painting. Warm the persistent back buffer
        // and Windows' drawing caches. Flush batched deletions before counting;
        // a tight UpdateWindow loop does not perform the usual message-pump flush.
        for (int i = 0; i < 10; ++i) {
            candidates.show(session, owner, RECT{100, 100, 101, 120}, false, &clicks, clicked, 1, headerPrefs);
            UpdateWindow(popup);
        }
        require(GdiFlush() != FALSE, "initial GDI batch completed");
        const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 150; ++i) {
            candidates.show(session, owner, RECT{100, 100, 101, 120}, false, &clicks, clicked, 1, headerPrefs);
            UpdateWindow(popup);
            require(popup == candidates.handle()
                    && IsWindowVisible(popup), "continuous redraw retains visible window");
        }
        RECT after{};
        GetWindowRect(popup, &after);
        require(EqualRect(&stable, &after), "same candidates retain geometry");
        require(GdiFlush() != FALSE, "repaint GDI batch completed");
        const DWORD gdiAfter = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        std::printf("GDI objects: before painting=%lu, after warmup/flush=%lu, after 150 repaints/flush=%lu\n",
                    static_cast<unsigned long>(gdiCold), static_cast<unsigned long>(gdiBefore),
                    static_cast<unsigned long>(gdiAfter));
        require(gdiAfter <= gdiBefore,
                "repeated paints do not leak GDI handles");
        require(SendMessageW(popup, WM_ERASEBKGND, 0, 0) == 1, "candidate erasing is suppressed");
        const UINT clickDpi = windowDpi(popup);
        const LPARAM firstRow = MAKELPARAM(MulDiv(20, clickDpi, 96), MulDiv(52, clickDpi, 96));
        const LPARAM header = MAKELPARAM(MulDiv(20, clickDpi, 96), MulDiv(15, clickDpi, 96));
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, firstRow);
        SendMessageW(popup, WM_LBUTTONUP, 0, firstRow);
        require(clicks.count == 1 && clicks.index == 0 && clicks.generation == 1
                && GetFocus() == focus, "click without focus change");
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, firstRow);
        candidates.show(session, owner, RECT{100, 100, 101, 120}, false, &clicks, clicked, 2, headerPrefs);
        SendMessageW(popup, WM_LBUTTONUP, 0, firstRow);
        require(clicks.count == 1, "changed candidates invalidate an in-flight click");
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, header);
        SendMessageW(popup, WM_LBUTTONUP, 0, header);
        require(clicks.count == 1, "preedit row is not selectable");
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, firstRow);
        candidates.hide();
        SendMessageW(popup, WM_LBUTTONUP, 0, firstRow);
        require(clicks.count == 1 && GetCapture() != popup, "hidden candidate releases capture and cannot commit");
        // Real popup geometry: it lands on the side the placement rule chose.
        {
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            require(GetMonitorInfoW(MonitorFromWindow(popup, MONITOR_DEFAULTTONEAREST), &monitor), "popup monitor");
            const RECT work = monitor.rcWork;
            const LONG middle = (work.top + work.bottom) / 2;
            const RECT high{100, work.bottom - 200, 101, work.bottom - 180};
            candidates.show(session, owner, high, false, &clicks, clicked, 30, headerPrefs, true);
            RECT above{}; GetWindowRect(popup, &above);
            require(above.bottom <= high.top, "popup near the work-area bottom is placed above the caret");
            // A one-pixel caret-height wobble must not move the popup: above, the
            // top edge comes from the caret top, not from the caret height.
            candidates.show(session, owner, RECT{100, work.bottom - 200, 101, work.bottom - 179},
                            false, &clicks, clicked, 31, headerPrefs, true);
            RECT wobble{}; GetWindowRect(popup, &wobble);
            require(wobble.top == above.top && wobble.left == above.left,
                    "a caret-height wobble does not move the popup");
            // The drop back below happens on a caret move, not on a side flicker.
            const RECT middleCaret{100, middle, 101, middle + 20};
            candidates.show(session, owner, middleCaret, false, &clicks, clicked, 32, headerPrefs, true);
            RECT lower{}; GetWindowRect(popup, &lower);
            require(lower.top >= middleCaret.bottom, "a caret with room below is placed under it");
        }
        for (int theme : {0,1,2,10,11,12}) {
            Preferences prefs; prefs.theme = theme; prefs.density = 0;
            for (int layout : {0, 1}) for (int size : {18, 36}) {
                prefs.layout = layout; prefs.fontSize = size;
                prefs.skinDecorations=false;
                candidates.show(session, owner, RECT{100,100,101,120}, false, &clicks, clicked, 3, prefs, true);
                RECT plain{};GetClientRect(popup,&plain);
                prefs.skinDecorations=true;
                candidates.show(session, owner, RECT{100,100,101,120}, false, &clicks, clicked, 3, prefs, true);
                require(GetFocus() == focus, "theme/size/layout changes never acquire host focus");
                const UINT dpi = windowDpi(popup);
                HFONT font = createUIFont(size, dpi, prefs.font);
                HDC dc = GetDC(popup); auto old = SelectObject(dc,font); TEXTMETRICW metric{};
                GetTextMetricsW(dc,&metric); SelectObject(dc,old); DeleteObject(font); ReleaseDC(popup,dc);
                int y = MulDiv(3,static_cast<int>(dpi),96) + metric.tmHeight/2;
                if(theme==12 && layout==1) { RECT client{};GetClientRect(popup,&client);y=client.bottom/2; }
                const int before = clicks.count;
                SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20,y));
                SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(20,y));
                require(clicks.count == before + 1 && clicks.index == 0 && GetFocus() == focus,
                        "themed inline first candidate hit area follows actual font height");
                RECT rect{};GetClientRect(popup,&rect);
                if(theme>=10 && rect.right>plain.right) {
                    SendMessageW(popup,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(rect.right-10,y));
                    SendMessageW(popup,WM_LBUTTONUP,0,MAKELPARAM(rect.right-10,y));
                    if(clicks.count!=before+1) std::printf("rail failure: theme=%d layout=%d size=%d dpi=%u width=%ld plain=%ld y=%d\n",theme,layout,size,dpi,rect.right,plain.right,y);
                    require(clicks.count==before+1,"ornament rail never selects a candidate");
                }
                HRGN region = CreateRectRgn(0,0,0,0);
                const int kind = GetWindowRgn(popup,region); DeleteObject(region);
                require(visualTheme(theme) ? kind != ERROR : kind == ERROR,
                        "switching themes applies and removes the rounded window region");
            }
            prefs.layout = 0; prefs.fontSize = 18;
            candidates.show(session, owner, RECT{100,100,101,120}, false, nullptr, nullptr, 3, prefs, true);
            auto redraw = [&] { RedrawWindow(popup,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW); GdiFlush(); };
            for (int warm = 0; warm < 20; ++warm) redraw();
            const DWORD before = GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            std::vector<double> timings;
            for (int n = 0; n < 300; ++n) {
                const auto start = std::chrono::steady_clock::now(); redraw();
                timings.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());
            }
            const DWORD afterPaint = GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            require(afterPaint <= before, "each theme releases GDI/GDI+ objects after 300 warm repaints");
            std::sort(timings.begin(),timings.end()); RECT geometry{}; GetWindowRect(popup,&geometry);
            std::printf("Theme %d paint: count=%d, %ldx%ld, dpi=%u, n=300, P50/P95/P99 %.1f/%.1f/%.1f us, GDI %lu -> %lu (RedrawWindow+GdiFlush; no TSF/core/compositor)\n",
                theme,myswy_session_candidate_count(session),geometry.right-geometry.left,geometry.bottom-geometry.top,windowDpi(popup),
                timings[149],timings[284],timings[296],before,afterPaint);
        }
        {
            Preferences custom;custom.theme=13;custom.skin=builtinSkin(10);custom.density=0;
            candidates.show(session,owner,RECT{100,100,101,120},false,nullptr,nullptr,4,custom,true);
            RECT before{};GetWindowRect(popup,&before);
            custom.skin=builtinSkin(12);candidates.refreshPreferences(session,custom);
            RECT refreshed{};GetWindowRect(popup,&refreshed);
            require(refreshed.right-refreshed.left>before.right-before.left,"same custom ID updates active artwork and geometry");
            custom.skinDecorations=false;candidates.refreshPreferences(session,custom);GetWindowRect(popup,&refreshed);
            require(refreshed.right-refreshed.left<before.right-before.left,"decoration toggle removes rail without font shrink");
        }
        DestroyWindow(owner); // Windows automatically destroys its owned popup.
        require(!IsWindow(popup), "owned popup retired with owner");
        owner = CreateWindowW(L"STATIC", L"Myswy second owner", WS_OVERLAPPEDWINDOW,
                              0, 0, 300, 200, nullptr, nullptr, module, nullptr);
        candidates.show(session, owner, RECT{200, 100, 201, 120}, false);
        popup = candidates.handle();
        require(popup && IsWindowVisible(popup), "popup can be recreated after owner destruction");
        candidates.hide();
        require(!IsWindowVisible(popup), "popup hides on cleanup");
        DestroyWindow(owner);
    }
    myswy_session_free(session);
    std::puts("PASS: key planning, shortcuts, punctuation ordering, cancellation, UTF-16.");
    return 0;
}
