#include "keymap.h"
#include "candidate.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>

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
        require(planKey(0x20, ' ', false, false, true).key == MYSWY_KEY_SPACE, "repeated test key");
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
        HWND popup = FindWindowW(L"Myswy.Candidates.Preview1", nullptr);
        require(popup && IsWindowVisible(popup)
                && (GetWindowLongPtrW(popup, GWL_EXSTYLE) & WS_EX_NOACTIVATE), "passive candidate popup");
        const HWND focus = GetFocus();
        RECT stable{};
        GetWindowRect(popup, &stable);
        const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 150; ++i) {
            candidates.show(session, owner, RECT{100, 100, 101, 120}, false, &clicks, clicked, 1);
            UpdateWindow(popup);
            require(popup == FindWindowW(L"Myswy.Candidates.Preview1", nullptr)
                    && IsWindowVisible(popup), "continuous redraw retains visible window");
        }
        RECT after{};
        GetWindowRect(popup, &after);
        require(EqualRect(&stable, &after), "same candidates retain geometry");
        require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdiBefore + 2,
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
        DestroyWindow(owner); // Windows automatically destroys its owned popup.
        require(!IsWindow(popup), "owned popup retired with owner");
        owner = CreateWindowW(L"STATIC", L"Myswy second owner", WS_OVERLAPPEDWINDOW,
                              0, 0, 300, 200, nullptr, nullptr, module, nullptr);
        candidates.show(session, owner, RECT{200, 100, 201, 120}, false);
        popup = FindWindowW(L"Myswy.Candidates.Preview1", nullptr);
        require(popup && IsWindowVisible(popup), "popup can be recreated after owner destruction");
        candidates.hide();
        require(!IsWindowVisible(popup), "popup hides on cleanup");
        DestroyWindow(owner);
    }
    myswy_session_free(session);
    std::puts("PASS: key planning, shortcuts, punctuation ordering, cancellation, UTF-16.");
    return 0;
}
