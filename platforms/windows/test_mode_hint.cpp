// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the 中/英 mode hint and its preference. Read-only: no
// user preference file, profile or registration is touched.
#include "mode_hint.h"
#include "theme_art.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cwchar>
namespace chengyin {
HINSTANCE module = nullptr;
LONG objects = 0;
}
namespace {
void require(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "mode hint FAIL: %s\n", why);
        std::exit(1);
    }
}
void pump(unsigned milliseconds) {
    const DWORD until = GetTickCount() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(5);
    } while (static_cast<LONG>(GetTickCount() - until) < 0);
}
// The monitor whose work area the caret belongs to, exactly as the hint decides it.
MONITORINFO monitorFor(RECT caret) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    require(GetMonitorInfoW(MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
            "monitor for caret");
    return monitor;
}
// Owner window so the hint is an owned popup, which is what keeps the
// application's foreground window current.
HWND foregroundOwner() {
    HWND owner = CreateWindowW(L"STATIC", L"Mode hint fixture", WS_OVERLAPPEDWINDOW,
                               0, 0, 300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(owner != nullptr, "owner window");
    ShowWindow(owner, SW_SHOW);
    SetForegroundWindow(owner);
    pump(20);
    return owner;
}
}
int main() {
    using namespace chengyin;
    module = GetModuleHandleW(nullptr);
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM apartment");
    HWND owner = foregroundOwner();
    const HWND foreground = GetForegroundWindow();
    chengyin::Preferences preferences;
    preferences.fontSize = 18;
    // A caret comfortably inside the work area, so the badge has room below.
    const MONITORINFO monitor = monitorFor(RECT{0, 0, 1, 1});
    const int centreX = (monitor.rcWork.left + monitor.rcWork.right) / 2;
    const RECT middle{centreX, monitor.rcWork.top + 120, centreX + 1, monitor.rcWork.top + 140};
    {
        // 1. The glyph always names the mode the user just switched TO.
        ModeHintWindow hint;
        hint.show(owner, middle, true, preferences, GetTickCount64());
        require(hint.handle() != nullptr && hint.visible(), "English switch shows the hint");
        require(std::wcscmp(hint.glyph(), L"英") == 0, "Chinese to English shows 英");
        hint.show(owner, middle, false, preferences, GetTickCount64());
        require(std::wcscmp(hint.glyph(), L"中") == 0, "English to Chinese shows 中");
        // 5. Never activatable, never foreground: the badge must not steal focus.
        const LONG_PTR style = GetWindowLongPtrW(hint.handle(), GWL_EXSTYLE);
        require((style & WS_EX_NOACTIVATE) != 0, "hint is WS_EX_NOACTIVATE");
        require((style & WS_EX_TOOLWINDOW) != 0 && (style & WS_EX_TOPMOST) != 0,
                "hint stays out of the taskbar and above the host");
        require((style & WS_EX_TRANSPARENT) != 0, "hint is click-through");
        require(GetForegroundWindow() == foreground, "hint never takes the foreground");
        require(GetFocus() != hint.handle(), "hint never takes keyboard focus");
        // 1b. Placed below the caret when there is room.
        RECT placed{};
        GetWindowRect(hint.handle(), &placed);
        require(placed.top >= middle.bottom, "hint sits below a caret with room beneath it");
        require(placed.left >= monitor.rcWork.left && placed.right <= monitor.rcWork.right,
                "hint stays inside the work area horizontally");
        // 2. The countdown expires and the badge goes away on its own.
        require(hint.visible(), "hint is still up before its deadline");
        pump(chengyin::HintTimer::duration + 250);
        require(!hint.visible(), "hint hides when its timer expires");
        // 2b. A second switch updates the glyph and restarts the countdown, so no
        // earlier deadline can retire the newer badge.
        hint.show(owner, middle, true, preferences, GetTickCount64());
        require(hint.visible() && std::wcscmp(hint.glyph(), L"英") == 0, "second switch repaints the hint");
        pump(600);
        hint.show(owner, middle, false, preferences, GetTickCount64());
        require(hint.visible() && std::wcscmp(hint.glyph(), L"中") == 0, "switch during the countdown updates it");
        pump(500);
        require(hint.visible(), "the restarted countdown outlives the original deadline");
        pump(600);
        require(!hint.visible(), "the restarted countdown does expire");
        hint.hide();
        require(!hint.visible(), "explicit hide retires the hint");
    }
    {
        // 7. At the bottom edge the badge flips above the caret and stays wholly
        // inside the work area instead of being clipped off-screen.
        ModeHintWindow edge;
        const RECT low{centreX, monitor.rcWork.bottom - 6, centreX + 1, monitor.rcWork.bottom - 2};
        edge.show(owner, low, false, preferences, GetTickCount64());
        require(edge.handle() != nullptr, "edge hint exists");
        RECT placed{};
        GetWindowRect(edge.handle(), &placed);
        require(placed.bottom <= low.top, "hint flips above a caret at the bottom edge");
        require(placed.bottom <= monitor.rcWork.bottom && placed.top >= monitor.rcWork.top,
                "flipped hint is fully inside the work area vertically");
        require(placed.left >= monitor.rcWork.left && placed.right <= monitor.rcWork.right,
                "flipped hint is fully inside the work area horizontally");
        edge.hide();
    }
    {
        // The same pure placement the hint uses, asserted on its own so the flip
        // rule is pinned even if the window geometry shifts.
        PlacementInput input;
        input.caret = {100, 700, 101, 720};
        input.work = {0, 0, 1920, 1080};
        input.width = 40;
        input.height = 40;
        input.gap = 4;
        PlacementState state;
        const PlacementResult below = placeCandidates(input, state);
        require(below.side == Placement::below && below.top == 724, "roomy caret places the hint below");
        PlacementState edgeState;
        input.caret = {100, 1070, 101, 1078};
        const PlacementResult above = placeCandidates(input, edgeState);
        require(above.side == Placement::above && above.top + input.height <= 1070,
                "bottom-edge caret places the hint above without overlap");
        require(above.top >= 0, "flipped hint never leaves the work area");
    }
    {
        // The timer rules, without a window: restart displaces the deadline.
        HintTimer timer;
        require(!timer.visible && !timer.due(0), "a fresh timer shows nothing");
        timer.restart(1000);
        require(timer.visible && !timer.due(1899), "the hint lives until its deadline");
        require(timer.due(1900), "the hint expires exactly at its deadline");
        timer.restart(1500);
        require(timer.visible && !timer.due(1900), "restarting pushes the deadline out");
        require(timer.due(2400), "the restarted deadline still expires");
        timer.stop();
        require(!timer.visible && !timer.due(9999), "stopping retires the hint immediately");
    }
    {
        // 6. A legacy preferences.ini without ModeHint loads with the default on,
        // the new key round-trips, and an explicit off survives.
        wchar_t temporary[32768]{};
        require(GetTempPathW(32768, temporary) > 0, "temporary root");
        const std::wstring folder = std::wstring(temporary) + L"chengyin-modehint-" + std::to_wstring(GetCurrentProcessId())
                                    + L"-" + std::to_wstring(GetTickCount64());
        require(CreateDirectoryW(folder.c_str(), nullptr) != FALSE, "isolated preference folder");
        const std::wstring file = folder + L"\\preferences.ini";
        Preferences defaults;
        require(defaults.modeHint, "the new preference defaults to on");
        chengyin::Preferences saved;
        saved.modeHint = false;
        require(savePreferences(file, saved), "save the hint preference");
        auto restored = loadPreferences(file);
        require(!restored.modeHint, "an explicit off survives a reload");
        require(restored.font == saved.font && restored.pageSize == saved.pageSize,
                "the hint key does not disturb unrelated preferences");
        // Strip the key, as every pre-preview26 file would be.
        std::vector<uint8_t> bytes;
        require(readSmallFile(file, bytes, 8192), "read saved preferences");
        std::wstring content((bytes.size() - 2) / 2, L'\0');
        std::memcpy(content.data(), bytes.data() + 2, bytes.size() - 2);
        const auto at = content.find(L"ModeHint=");
        require(at != std::wstring::npos, "saved preferences carry the hint key");
        content.erase(at, content.find(L'\n', at) - at + 1);
        std::vector<uint8_t> legacy(2 + content.size() * 2);
        legacy[0] = 0xff;
        legacy[1] = 0xfe;
        std::memcpy(legacy.data() + 2, content.data(), content.size() * 2);
        require(atomicWrite(file, legacy), "write a legacy preference file");
        auto migrated = restored;
        require(tryLoadPreferences(file, migrated), "a legacy file still loads");
        require(migrated.modeHint, "a missing hint key defaults to on");
        require(migrated.font == saved.font, "the legacy file keeps its other settings");
        require(readSmallFile(file, bytes, 8192) && bytes == legacy,
                "loading a legacy file never rewrites it");
        // An explicit on also round-trips.
        saved.modeHint = true;
        require(savePreferences(file, saved), "save the hint preference back on");
        require(loadPreferences(file).modeHint, "an explicit on survives a reload");
        DeleteFileW(file.c_str());
        require(RemoveDirectoryW(folder.c_str()) != FALSE, "temporary preference folder cleaned");
    }
    DestroyWindow(owner);
    CoUninitialize();
    require(chengyin::objects == 0, "no retained module objects");
    std::puts("PASS: mode hint glyph, timer restart/expiry, non-activation, screen-edge flip, "
              "and ModeHint preference migration.");
    return 0;
}
