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
        candidates.show(session, owner, RECT{100, 100, 101, 120}, false, &clicks, clicked, 1);
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
            candidates.show(session, owner, RECT{100, 100, 101, 120}, false, &clicks, clicked, 1);
            UpdateWindow(popup);
        }
        require(GdiFlush() != FALSE, "initial GDI batch completed");
        const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 150; ++i) {
            candidates.show(session, owner, RECT{100, 100, 101, 120}, false, &clicks, clicked, 1);
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
        const LPARAM firstRow = MAKELPARAM(20, 52);
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, firstRow);
        SendMessageW(popup, WM_LBUTTONUP, 0, firstRow);
        require(clicks.count == 1 && clicks.index == 0 && clicks.generation == 1
                && GetFocus() == focus, "click without focus change");
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, firstRow);
        candidates.show(session, owner, RECT{100, 100, 101, 120}, false, &clicks, clicked, 2);
        SendMessageW(popup, WM_LBUTTONUP, 0, firstRow);
        require(clicks.count == 1, "changed candidates invalidate an in-flight click");
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 15));
        SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(20, 15));
        require(clicks.count == 1, "preedit row is not selectable");
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, firstRow);
        candidates.hide();
        SendMessageW(popup, WM_LBUTTONUP, 0, firstRow);
        require(clicks.count == 1 && GetCapture() != popup, "hidden candidate releases capture and cannot commit");
        for (int theme = 0; theme < 6; ++theme) {
            Preferences prefs; prefs.theme = theme; prefs.density = 0;
            for (int layout : {0, 1}) for (int size : {18, 36}) {
                prefs.layout = layout; prefs.fontSize = size;
                candidates.show(session, owner, RECT{100,100,101,120}, false, &clicks, clicked, 3, prefs, true);
                require(GetFocus() == focus, "theme/size/layout changes never acquire host focus");
                const UINT dpi = windowDpi(popup);
                HFONT font = createUIFont(size, dpi, prefs.font);
                HDC dc = GetDC(popup); auto old = SelectObject(dc,font); TEXTMETRICW metric{};
                GetTextMetricsW(dc,&metric); SelectObject(dc,old); DeleteObject(font); ReleaseDC(popup,dc);
                const int y = MulDiv(3,static_cast<int>(dpi),96) + themeBannerHeight(visualTheme(theme),dpi,true) + metric.tmHeight/2;
                const int before = clicks.count;
                SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20,y));
                SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(20,y));
                require(clicks.count == before + 1 && clicks.index == 0 && GetFocus() == focus,
                        "themed inline first candidate hit area follows banner and actual font height");
                if (visualTheme(theme) >= 3) {
                    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20,15));
                    SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(20,15));
                    require(clicks.count == before + 1, "decorative banner cannot select a candidate");
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
