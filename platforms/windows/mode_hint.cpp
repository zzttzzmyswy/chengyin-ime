// SPDX-License-Identifier: GPL-3.0-or-later
#include "mode_hint.h"
#include "theme_art.h"
#include <algorithm>
#include <cwchar>
namespace chengyin {
namespace {
constexpr UINT_PTR kTimer = 1;
struct DpiContext {
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiContext() { if (previous) SetThreadDpiAwarenessContext(previous); }
};
}
ModeHintWindow::~ModeHintWindow() {
    if (hwnd_)
        DestroyWindow(hwnd_);
    if (font_)
        DeleteObject(font_);
    UnregisterClassW(kModeHintClass, module);
}
void ModeHintWindow::hide() {
    timer_.stop();
    if (hwnd_) {
        KillTimer(hwnd_, kTimer);
        ShowWindow(hwnd_, SW_HIDE);
    }
}
void ModeHintWindow::elapsed(ULONGLONG now) {
    // Re-checked rather than assumed: a timer armed by an earlier switch can be
    // delivered after a later one already pushed the deadline out.
    if (timer_.due(now))
        hide();
}
void ModeHintWindow::show(HWND owner, RECT caret, bool english, const Preferences &preferences, ULONGLONG now) {
    // The hint is a transient courtesy and never a reason to fail input.
    if (owner && !IsWindow(owner))
        owner = nullptr;
    // A host may virtualize caret coordinates; convert them before switching this
    // thread's DPI context, exactly as the candidate popup does.
    if (owner && !AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(owner), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        POINT start{caret.left, caret.top}, end{caret.right, caret.bottom};
        LogicalToPhysicalPointForPerMonitorDPI(owner, &start);
        LogicalToPhysicalPointForPerMonitorDPI(owner, &end);
        caret = {start.x, start.y, end.x, end.y};
    }
    DpiContext context;
    preferences_ = preferences;
    colors_ = palette(preferences);
    text_[0] = english ? L'英' : L'中';
    if (!hwnd_) {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.hInstance = module;
        cls.lpfnWndProc = procedure;
        cls.lpszClassName = kModeHintClass;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        // WS_EX_TRANSPARENT passes every mouse message to the window underneath,
        // so the hint can never swallow a click. WS_EX_NOACTIVATE plus creating
        // it as an owned popup is what keeps the foreground window unchanged.
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED
                                | WS_EX_TRANSPARENT, kModeHintClass, L"澄音 中英文状态",
                                WS_POPUP, caret.left, caret.bottom, 1, 1, owner, nullptr, module, this);
        if (!hwnd_)
            return;
        // A layered window that never sets its alpha is not composited at all.
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    }
    if (reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT)) != owner)
        SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    const HMONITOR caretMonitor = MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(caretMonitor, &monitor)) {
        // Without a work area there is nowhere honest to place the hint.
        hide();
        return;
    }
    // The window adopts the caret's monitor before its DPI is read, so a switch
    // across monitors scales the glyph for the display it actually lands on.
    if (MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST) != caretMonitor)
        SetWindowPos(hwnd_, nullptr, monitor.rcWork.left, monitor.rcWork.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    const UINT dpi = windowDpi(hwnd_);
    auto scale = [dpi](int n) {
        return MulDiv(n, static_cast<int>(dpi), 96);
    };
    if (fontDpi_ != dpi || fontSize_ != preferences.fontSize || fontFace_ != preferences.font) {
        HFONT font = createUIFont(preferences.fontSize, dpi, preferences.font);
        if (font) {
            if (font_)
                DeleteObject(font_);
            font_ = font;
        }
        fontDpi_ = dpi;
        fontSize_ = preferences.fontSize;
        fontFace_ = preferences.font;
    }
    const int padding = scale(6);
    int width = 0, height = 0;
    {
        HDC dc = GetDC(hwnd_);
        HGDIOBJ old = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW metric{};
        SIZE extent{};
        GetTextMetricsW(dc, &metric);
        GetTextExtentPoint32W(dc, text_, 1, &extent);
        SelectObject(dc, old);
        ReleaseDC(hwnd_, dc);
        // One glyph reads as a square badge rather than a narrow slab.
        width = std::max(static_cast<int>(extent.cx), static_cast<int>(metric.tmHeight)) + padding * 2;
        height = static_cast<int>(metric.tmHeight) + padding * 2;
    }
    PlacementInput placement;
    placement.caret = caret;
    placement.work = monitor.rcWork;
    placement.width = width;
    placement.height = height;
    placement.gap = scale(4);
    placement.slack = std::max(1, scale(2));
    // The hint re-decides its side on every switch; the candidate window's sticky
    // memory exists to stop a keystroke-sized wobble from flipping a long-lived
    // popup, which would only make a two-second badge drift.
    placement_ = {};
    const PlacementResult placed = placeCandidates(placement, placement_);
    RECT before{};
    GetWindowRect(hwnd_, &before);
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_SHOWWINDOW;
    if (before.left == placed.left && before.top == placed.top)
        flags |= SWP_NOMOVE;
    if (before.right - before.left == width && before.bottom - before.top == height)
        flags |= SWP_NOSIZE;
    SetWindowPos(hwnd_, HWND_TOPMOST, placed.left, placed.top, width, height, flags);
    const int radius = scale(4);
    HRGN shape = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2);
    if (!SetWindowRgn(hwnd_, shape, TRUE) && shape)
        DeleteObject(shape);
    InvalidateRect(hwnd_, nullptr, FALSE);
    // Restarting the same timer both replaces the text and resets the countdown,
    // so a rapid 中→英→中 never leaves an earlier deadline running.
    timer_.restart(now);
    SetTimer(hwnd_, kTimer, HintTimer::duration, nullptr);
}
LRESULT CALLBACK ModeHintWindow::procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(l);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto *self = reinterpret_cast<ModeHintWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCDESTROY && self) {
        self->hwnd_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_TIMER && self && w == kTimer) {
        self->elapsed(GetTickCount64());
        return 0;
    }
    if ((message == WM_THEMECHANGED || message == WM_SETTINGCHANGE || message == WM_SYSCOLORCHANGE) && self) {
        self->colors_ = palette(self->preferences_);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_PAINT && self) {
        self->paint();
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
void ModeHintWindow::paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    const UINT dpi = windowDpi(hwnd_);
    drawThemeSurface(dc, client, colors_, visualTheme(preferences_.theme), dpi, MulDiv(4, static_cast<int>(dpi), 96));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colors_.text);
    HGDIOBJ old = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(dc, text_, 1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    EndPaint(hwnd_, &ps);
}
}
