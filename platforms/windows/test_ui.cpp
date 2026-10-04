// Read-only native UI regression. No Save/import/clear action is dispatched.
#include "settings.h"
#include <commctrl.h>
#include "preferences.h"
#include "candidate.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
namespace myswy {
HINSTANCE module = nullptr;
LONG objects = 0;
}
namespace {
std::wstring captureDirectory;
int stage = 0;
void require(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "UI FAIL: %s\n", why);
        std::exit(1);
    }
}
void capture(HWND window, const wchar_t *name) {
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    GdiFlush();
    if (captureDirectory.empty())
        return;
    // Let queued child/exposure paints and Wine/X11 presentation finish. The
    // inspection timer is paused during capture to avoid reentering its stages.
    DWORD started = GetTickCount();
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        GdiFlush();
        Sleep(5);
    } while (GetTickCount() - started < 200);
    RECT rect{};
    GetWindowRect(window, &rect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ old = SelectObject(memory, bitmap);
    require(BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY) != FALSE,
            "capture native paint");
    SelectObject(memory, old);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, height, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    require(GetDIBits(memory, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS) == height,
            "read native pixels");
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    auto path = captureDirectory + L"\\" + name + L".bmp";
    HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    DWORD count = 0;
    require(output != INVALID_HANDLE_VALUE && WriteFile(output, &file, sizeof(file), &count, nullptr)
            && WriteFile(output, &info.bmiHeader, sizeof(info.bmiHeader), &count, nullptr)
            && WriteFile(output, pixels.data(), static_cast<DWORD>(pixels.size()), &count, nullptr), "save UI capture");
    CloseHandle(output);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}
HWND embedded[3] {};
void choosePage(HWND window, int page) {
    HWND tab = GetDlgItem(window, 10);
    require(tab && SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0) == 6, "six native tabs accessible");
    SendMessageW(tab, TCM_SETCURSEL, page, 0);
    NMHDR note{tab, 10, TCN_SELCHANGE};
    SendMessageW(window, WM_NOTIFY, 10, reinterpret_cast<LPARAM>(&note));
}
void checkLayout(HWND pane) {
    struct Box {
        HWND window;
        RECT rect;
        bool label;
    };
    std::vector<Box> boxes;
    for (HWND child = GetWindow(pane, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsWindowVisible(child))
            continue;
        wchar_t kind[64] {};
        GetClassNameW(child, kind, 64);
        if (!std::wcscmp(kind, L"Button") && (GetWindowLongPtrW(child, GWL_STYLE)&BS_TYPEMASK) == BS_GROUPBOX)
            continue;
        RECT rect{};
        GetWindowRect(child, &rect);
        MapWindowPoints(nullptr, pane, reinterpret_cast<POINT *>(&rect), 2);
        require(rect.left >= 0 && rect.right > rect.left && rect.bottom > rect.top, "nonempty control geometry");
        for (const auto &other : boxes) {
            RECT overlap{};
            if (IntersectRect(&overlap, &rect, &other.rect)) {
                std::fprintf(stderr, "Overlap stage=%d id=%d (%ld,%ld,%ld,%ld) and id=%d (%ld,%ld,%ld,%ld)\n", stage,
                             GetDlgCtrlID(child), rect.left, rect.top, rect.right, rect.bottom, GetDlgCtrlID(other.window),
                             other.rect.left, other.rect.top, other.rect.right, other.rect.bottom);
                require(false, "labels and controls never overlap");
            }
        }
        if (!std::wcscmp(kind, L"Static")) {
            wchar_t text[4096] {};
            GetWindowTextW(child, text, 4096);
            HDC dc = GetDC(child);
            HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0)));
            RECT needed{0, 0, rect.right - rect.left, 0};
            DrawTextW(dc, text, -1, &needed, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old);
            ReleaseDC(child, dc);
            require(needed.bottom <= rect.bottom - rect.top, "label fits allocated height");
        }
        boxes.push_back({child, rect, !std::wcscmp(kind, L"Static")});
    }
}
void CALLBACK inspect(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    HWND window = FindWindowW(L"Myswy.Settings", nullptr);
    require(window != nullptr, "settings created");
    HWND pane = FindWindowExW(window, nullptr, L"Myswy.Settings.Content", nullptr);
    require(pane && GetDlgItem(window, 100) && GetDlgItem(window, 101), "apply and defaults available");
    require(IsWindowVisible(pane), "content pane is visible");
    RECT paneRect{};
    GetWindowRect(pane, &paneRect);
    POINT point{paneRect.left + 20, paneRect.top + 20};
    ScreenToClient(window, &point);
    require(ChildWindowFromPointEx(window, point, CWP_SKIPINVISIBLE) == pane,
            "content pane stays above tab background");
    if (stage < 6) {
        choosePage(window, stage);
        const int expected[] {204, 301, 401, 501, 701, 601};
        require(GetDlgItem(pane, expected[stage]), "all settings groups available");
        if (stage == 4) {
            for (int i = 0; i < 3; ++i)
                embedded[i] = GetDlgItem(pane, 701 + i);
            require(embedded[0] && embedded[1] && embedded[2], "embedded Edit/RichEdit/password controls");
            require(GetWindowLongPtrW(embedded[2], GWL_STYLE)&ES_PASSWORD, "password style preserved");
            SetWindowTextW(embedded[0], L"retained test text");
        }
        checkLayout(pane);
        RedrawWindow(pane, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        HDC dc = GetDC(pane);
        require(GetPixel(dc, 18, 66) == GetSysColor(COLOR_BTNFACE),
                "native group interiors repaint with dialog background");
        ReleaseDC(pane, dc);
        const wchar_t *names[] {L"settings-input", L"settings-candidates", L"settings-dictionary", L"settings-learning", L"settings-input-test", L"settings-about"};
        capture(window, names[stage]);
    } else if (stage < 12) {
        // Exercise every page at a smaller size and 150% DPI, including wrapping/scroll.
        RECT next{10, 10, 1090, 910};
        SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&next));
        choosePage(window, stage - 6);
        checkLayout(pane);
        SendMessageW(pane, WM_VSCROLL, SB_BOTTOM, 0);
        checkLayout(pane);
        if (stage == 10) {
            for (int i = 0; i < 3; ++i)
                require(GetDlgItem(pane, 701 + i) == embedded[i], "input controls survive tab and DPI changes");
            wchar_t text[64] {};
            GetWindowTextW(embedded[0], text, 64);
            require(!std::wcscmp(text, L"retained test text"), "test text survives relayout");
            capture(window, L"settings-input-test-144dpi");
        }
    } else
        DestroyWindow(window);
    ++stage;
    if (stage < 13)
        require(SetTimer(nullptr, timer, 100, inspect) != 0, "next UI inspection");
}
void inspectTestpad() {
    wchar_t executable[32768] {};
    require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "locate testpad executable");
    std::wstring path = executable;
    path = path.substr(0, path.find_last_of(L'\\') + 1) + L"myswy_settings.exe";
    std::wstring command = L"\"" + path + L"\" --input-test";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                           &startup, &process) != FALSE, "start native TSF input test application");
    struct Search {
        DWORD process;
        HWND window = nullptr;
    } search{process.dwProcessId};
    for (int i = 0; i < 120; ++i) {
        EnumWindows([](HWND window, LPARAM target) -> BOOL {
            auto *search = reinterpret_cast<Search *>(target);
            DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
            wchar_t name[64] {}; GetClassNameW(window, name, 64);
            if (pid == search->process && std::wcscmp(name, L"Myswy.Settings") == 0) search->window = window;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        HWND pane = search.window ? FindWindowExW(search.window, nullptr, L"Myswy.Settings.Content",
                    nullptr) : nullptr;
        if (pane && IsWindowVisible(search.window) && GetDlgItem(pane, 701) && GetDlgItem(pane, 702)
                && GetDlgItem(pane, 703))
            break;
        if (WaitForSingleObject(process.hProcess, 25) == WAIT_OBJECT_0)
            break;
    }
    HWND pane = search.window ? FindWindowExW(search.window, nullptr, L"Myswy.Settings.Content",
                nullptr) : nullptr;
    bool valid = pane && IsWindowVisible(search.window) && GetDlgItem(pane, 701) && GetDlgItem(pane, 702)
                 && GetDlgItem(pane, 703)
                 && (GetWindowLongPtrW(GetDlgItem(pane, 703), GWL_STYLE)&ES_PASSWORD);
    if (valid)
        capture(search.window, L"settings-process-input-test");
    if (search.window)
        PostMessageW(search.window, WM_CLOSE, 0, 0);
    const bool exited = WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0;
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(process.hProcess, &exitCode);
    if (!valid || !exited || exitCode != 0)
        std::fprintf(stderr, "Testpad: window=%p visible=%d valid=%d exited=%d exit=%lu\n",
                     search.window, search.window ? IsWindowVisible(search.window) : 0,
                     valid, exited, static_cast<unsigned long>(exitCode));
    if (!exited) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    require(valid && exited && exitCode == 0,
            "new testpad name, Edit/RichEdit/password controls and clean TSF process exit");
}

}
int wmain(int argc, wchar_t **argv) {
    myswy::module = GetModuleHandleW(nullptr);
    if (argc == 2) {
        captureDirectory = argv[1];
        CreateDirectoryW(argv[1], nullptr);
    }
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "UI COM initialization");
    HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
    require(SetTimer(nullptr, 0, 100, inspect) != 0, "UI inspection timer");
    require(myswy::runSettings(myswy::module, SW_SHOW) == 0 && stage == 13, "all settings pages painted");
    HWND owner = CreateWindowW(L"STATIC", L"澄音 visual regression", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               10, 10, 400, 200, nullptr, nullptr, myswy::module, nullptr);
    MyswySession *session = myswy_session_new();
    require(owner && session, "candidate fixture");
    require(myswy_session_configure(session, 5, 3) == 0, "five candidate layout");
    for (char c : std::string("zhongguo"))
        myswy_session_process(session, static_cast<uint32_t>(c), 0);
    {
        myswy::CandidateWindow candidates;
        myswy::Preferences prefs;
        candidates.show(session, owner, RECT{100, 150, 101, 170}, false, nullptr, nullptr, 1, prefs);
        HWND popup = FindWindowW(L"Myswy.Candidates.Preview1", nullptr);
        require(popup && IsWindowVisible(popup), "vertical candidate layout paints");
        capture(popup, L"candidate-light");
        prefs.theme = 2;
        prefs.candidatePinyin = true;
        candidates.show(session, owner, RECT{100, 150, 101, 170}, false, nullptr, nullptr, 2, prefs);
        capture(popup, L"candidate-dark-pinyin");
        prefs.layout = 1;
        prefs.density = 0;
        candidates.show(session, owner, RECT{100, 150, 101, 170}, false, nullptr, nullptr, 3, prefs);
        capture(popup, L"candidate-horizontal");
    }
    myswy_session_free(session);
    DestroyWindow(owner);
    inspectTestpad();
    if (richEdit)
        FreeLibrary(richEdit);
    CoUninitialize();
    require(myswy::objects == 0, "UI lifetimes released");
    std::puts("PASS: six native settings tabs, font/DPI/theme/scroll, candidate layouts and embedded input test and settings process startup/exit; no user settings written.");
    return 0;
}
