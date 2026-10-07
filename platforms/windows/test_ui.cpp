// Read-only native UI regression. No Save/import/clear action is dispatched.
#include "settings.h"
#include <commctrl.h>
#include <algorithm>
#include "preferences.h"
#include "candidate.h"
#include "theme_art.h"
#include <cstdio>
#include <psapi.h>
#include <imm.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
namespace chengyin {
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
// Process/apartment-local COM binding; no registry or personal installation edits.
// Windows can auto-activate a TIP for Edit/RichEdit even after NOACTIVATETIP.
HMODULE bindFixtureTip(DWORD &cookie) {
    wchar_t executable[32768]{};
    require(GetModuleFileNameW(nullptr,executable,32768)!=0,"fixture module location");
    std::wstring path=executable;
    path=path.substr(0,path.find_last_of(L'\\')+1)+L"chengyin_tsf_fixture.dll";
    HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    require(module!=nullptr,"load isolated TSF implementation");
    using GetClass=HRESULT(WINAPI *)(REFCLSID,REFIID,void **);
    auto getClass=chengyin::procedureAddress<GetClass>(module,"DllGetClassObject");
    require(getClass!=nullptr,"fixture class factory export");
    chengyin::Ptr<IClassFactory> factory;
    require(SUCCEEDED(getClass(chengyin::kService,IID_IClassFactory,reinterpret_cast<void **>(factory.put()))),"fixture class factory");
    require(SUCCEEDED(CoRegisterClassObject(chengyin::kService,factory.get(),CLSCTX_INPROC_SERVER,
                        REGCLS_MULTIPLEUSE,&cookie)),"process-local fixture COM binding");
    return module;
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
    require(PrintWindow(window, memory, PW_RENDERFULLCONTENT) != FALSE,
            "render own fixture window without desktop occlusion");
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
    require(tab && SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0) == 8, "eight native tabs accessible");
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
void checkFonts(HWND window) {
    EnumChildWindows(window, [](HWND child, LPARAM) -> BOOL {
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
        if (font) {
            LOGFONTW before{}, after{};
            require(GetObjectW(font, sizeof(before), &before) == sizeof(before), "every child uses a live font handle");
            SendMessageW(child, WM_MOUSEMOVE, 0, MAKELPARAM(4,4));
            RedrawWindow(child, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            HFONT hovered = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
            require(GetObjectW(hovered, sizeof(after), &after) == sizeof(after)
                    && before.lfHeight == after.lfHeight && !std::wcscmp(before.lfFaceName, after.lfFaceName),
                    "hover preserves font family and height after recreation");
        }
        return TRUE;
    }, 0);
}
void CALLBACK inspect(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    HWND window = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM target) -> BOOL {
        wchar_t name[64] {}; GetClassNameW(candidate, name, 64);
        if (!std::wcscmp(name, L"Chengyin.Settings")) *reinterpret_cast<HWND *>(target) = candidate;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&window));
    require(window != nullptr, "settings created");
    HWND pane = FindWindowExW(window, nullptr, L"Chengyin.Settings.Content", nullptr);
    require(pane && GetDlgItem(window, 100) && GetDlgItem(window, 101), "apply and defaults available");
    require(IsWindowVisible(pane), "content pane is visible");
    RECT paneRect{};
    GetWindowRect(pane, &paneRect);
    POINT point{paneRect.left + 20, paneRect.top + 20};
    ScreenToClient(window, &point);
    require(ChildWindowFromPointEx(window, point, CWP_SKIPINVISIBLE) == pane,
            "content pane stays above tab background");
    if (stage < 8) {
        choosePage(window, stage);
        SendMessageW(window, WM_THEMECHANGED, 0, 0);
        checkFonts(window);
        const int expected[] {204, 301, 303, 401, 501, 701, 601, 801};
        require(GetDlgItem(pane, expected[stage]), "all settings groups available");
        if (stage == 0) {
            // The mode-hint checkbox lives on the input page and toggles the draft.
            HWND hint = GetDlgItem(pane, 205);
            require(hint != nullptr, "mode hint switch available");
            require(SendMessageW(hint, BM_GETCHECK, 0, 0) == BST_CHECKED, "mode hint defaults on");
            SendMessageW(hint, BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(pane, WM_COMMAND, MAKEWPARAM(205, BN_CLICKED), reinterpret_cast<LPARAM>(hint));
            choosePage(window, 1);
            choosePage(window, 0);
            require(SendMessageW(GetDlgItem(pane, 205), BM_GETCHECK, 0, 0) == BST_UNCHECKED,
                    "mode hint draft survives a tab switch");
            SendMessageW(GetDlgItem(pane, 205), BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(pane, WM_COMMAND, MAKEWPARAM(205, BN_CLICKED), reinterpret_cast<LPARAM>(hint));
        }
        if (stage==7) {
            for (int id : {801,811,820,823}) {
                HWND check=GetDlgItem(pane,id); require(check!=nullptr,"fuzzy and error switches available");
                SendMessageW(check,BM_SETCHECK,BST_CHECKED,0);
                SendMessageW(pane,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(check));
            }
        }
        if (stage == 5) {
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
        const wchar_t *names[] {L"settings-input", L"settings-candidates", L"settings-themes", L"settings-dictionary", L"settings-learning", L"settings-input-test", L"settings-about", L"settings-fuzzy"};
        capture(window, names[stage]);
        if (stage == 2) {
            require(SendMessageW(GetDlgItem(pane,303),CB_GETCOUNT,0,0) >= 6,
                    "theme page exposes system, white, black and three designed skins");
            const auto original = SendMessageW(GetDlgItem(pane,303),CB_GETCURSEL,0,0);
            for (int theme = 0; theme < 6; ++theme) {
                HWND combo = GetDlgItem(pane,303);
                SendMessageW(combo,CB_SETCURSEL,theme,0);
                SendMessageW(pane,WM_COMMAND,MAKEWPARAM(303,CBN_SELCHANGE),reinterpret_cast<LPARAM>(combo));
                checkLayout(pane); checkFonts(window);
                const auto name = L"settings-theme-" + std::to_wstring(theme);
                capture(window,name.c_str());
            }
            for(int id:{318,319,320}) {
                HWND option=GetDlgItem(pane,id);
                SendMessageW(option,CB_SETCURSEL,id==318?5:id==319?0:2,0);
                SendMessageW(pane,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(option));
                require(SendMessageW(GetDlgItem(pane,303),CB_GETCURSEL,0,0)>=6,"skin editing creates an unsaved draft rather than changing the built-in preset");
                checkFonts(window);checkLayout(pane);
            }
            capture(window,L"settings-custom-skin-draft");
            HWND combo = GetDlgItem(pane,303); SendMessageW(combo,CB_SETCURSEL,original,0);
            SendMessageW(pane,WM_COMMAND,MAKEWPARAM(303,CBN_SELCHANGE),reinterpret_cast<LPARAM>(combo));
        }
    } else if (stage < 40) {
        // All pages at 125/150/200/300%, narrow work areas and both scroll axes.
        const UINT dpis[]{120,144,192,288};
        const UINT dpi = dpis[(stage - 8) / 8];
        RECT next{10, 10, 1090, 850};
        SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&next));
        const int page = (stage - 8) % 8;
        choosePage(window, page);
        if (page==7) for (int id : {801,811,820,823})
            require(SendMessageW(GetDlgItem(pane,id),BM_GETCHECK,0,0)==BST_CHECKED,"fuzzy draft survives tab and DPI recreation");
        checkFonts(window);
        checkLayout(pane);
        SendMessageW(pane, WM_VSCROLL, SB_BOTTOM, 0);
        checkLayout(pane);
        SendMessageW(pane, WM_HSCROLL, SB_RIGHT, 0);
        SendMessageW(pane, WM_HSCROLL, SB_LEFT, 0);
        if (page == 5) {
            for (int i = 0; i < 3; ++i)
                require(GetDlgItem(pane, 701 + i) == embedded[i], "input controls survive tab and DPI changes");
            wchar_t text[64] {};
            GetWindowTextW(embedded[0], text, 64);
            require(!std::wcscmp(text, L"retained test text"), "test text survives relayout");
            const auto name = L"settings-input-test-" + std::to_wstring(dpi) + L"dpi";
            capture(window, name.c_str());
        }
    } else
        DestroyWindow(window);
    ++stage;
    if (stage < 41)
        require(SetTimer(nullptr, timer, 100, inspect) != 0, "next UI inspection");
}
void inspectTestpad() {
    wchar_t executable[32768] {};
    require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "locate testpad executable");
    std::wstring path = executable;
    std::wstring command = L"\"" + path + L"\" --input-test-fixture";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                           &startup, &process) != FALSE, "start isolated settings input pane");
    struct Search {
        DWORD process;
        HWND window = nullptr;
    } search{process.dwProcessId};
    for (int i = 0; i < 120; ++i) {
        EnumWindows([](HWND window, LPARAM target) -> BOOL {
            auto *search = reinterpret_cast<Search *>(target);
            DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
            wchar_t name[64] {}; GetClassNameW(window, name, 64);
            if (pid == search->process && std::wcscmp(name, L"Chengyin.Settings") == 0) search->window = window;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        HWND pane = search.window ? FindWindowExW(search.window, nullptr, L"Chengyin.Settings.Content",
                    nullptr) : nullptr;
        if (pane && IsWindowVisible(search.window) && GetDlgItem(pane, 701) && GetDlgItem(pane, 702)
                && GetDlgItem(pane, 703))
            break;
        if (WaitForSingleObject(process.hProcess, 25) == WAIT_OBJECT_0)
            break;
    }
    HWND pane = search.window ? FindWindowExW(search.window, nullptr, L"Chengyin.Settings.Content",
                nullptr) : nullptr;
    bool valid = pane && IsWindowVisible(search.window) && GetDlgItem(pane, 701) && GetDlgItem(pane, 702)
                 && GetDlgItem(pane, 703)
                 && (GetWindowLongPtrW(GetDlgItem(pane, 703), GWL_STYLE)&ES_PASSWORD);
    if (valid)
        capture(search.window, L"settings-process-input-test");
    if (search.window)
        PostMessageW(search.window, WM_CLOSE, 0, 0);
    // Keep this STA responsive while child COM/TSF teardown dispatches calls.
    // A plain process wait can deadlock shutdown across the two apartments.
    DWORD completed=0;
    const bool exited = SUCCEEDED(CoWaitForMultipleHandles(
        COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,5000,1,&process.hProcess,&completed))
        && completed==0;
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(process.hProcess, &exitCode);
    if (!valid || !exited || exitCode != 0)
        std::fprintf(stderr, "Testpad: window=%p visible=%d valid=%d exited=%d exit=%lu\n",
                     search.window, search.window ? IsWindowVisible(search.window) : 0,
                     valid, exited, static_cast<unsigned long>(exitCode));
    if (!exited) {
        HMODULE modules[256]{};DWORD needed=0;
        if(K32EnumProcessModules(process.hProcess,modules,sizeof(modules),&needed)) {
            for(DWORD i=0;i<std::min<DWORD>(needed/sizeof(HMODULE),256);++i) {
                wchar_t modulePath[32768]{};
                if(K32GetModuleFileNameExW(process.hProcess,modules[i],modulePath,32768)
                    && std::wcsstr(modulePath,L"chengyin_tsf"))
                {
                    char utf8[98304]{};WideCharToMultiByte(CP_UTF8,0,modulePath,-1,utf8,sizeof(utf8),nullptr,nullptr);
                    std::fprintf(stderr,"Unexpected installed TIP in UI fixture: %s\n",utf8);
                }
            }
        }
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    require(valid && exited && exitCode == 0,
            "isolated input pane, Edit/RichEdit/password controls and clean process exit");
}

}
int wmain(int argc, wchar_t **argv) {
    // UI geometry fixtures do not route physical input through installed IMEs.
    ImmDisableIME(static_cast<DWORD>(-1));
    chengyin::module = GetModuleHandleW(nullptr);
    // Run the same settings pane in an isolated child, without activating the
    // user's registered input method or reading personal settings/history.
    // Real installed TSF input remains desktop validation, not this UI fixture.
    if (argc == 2 && std::wcscmp(argv[1], L"--input-test-fixture") == 0) {
        require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "child UI COM initialization");
        DWORD fixtureCookie=0;HMODULE fixtureModule=bindFixtureTip(fixtureCookie);
        chengyin::Ptr<chengyin::ThreadManagerEx> manager;
        require(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,chengyin::kThreadManagerEx,
                        reinterpret_cast<void **>(manager.put()))),"child isolated thread manager");
        TfClientId client=TF_CLIENTID_NULL;
        require(SUCCEEDED(manager->ActivateEx(&client,1 /* TF_TMAE_NOACTIVATETIP */)),"child isolated activation");
        HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
        int result = chengyin::runSettings(chengyin::module, SW_SHOW, nullptr, nullptr, 5, false);
        manager->Deactivate();manager.reset();
        CoRevokeClassObject(fixtureCookie);
        CoUninitialize();
        if (richEdit) FreeLibrary(richEdit);
        FreeLibrary(fixtureModule);
        return result;
    }
    if (argc == 2) {
        captureDirectory = argv[1];
        CreateDirectoryW(argv[1], nullptr);
    }
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "UI COM initialization");
    DWORD fixtureCookie=0;HMODULE fixtureModule=bindFixtureTip(fixtureCookie);
    chengyin::Ptr<chengyin::ThreadManagerEx> isolatedManager;
    require(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,chengyin::kThreadManagerEx,
                    reinterpret_cast<void **>(isolatedManager.put()))),"UI isolated thread manager");
    TfClientId isolatedClient=TF_CLIENTID_NULL;
    require(SUCCEEDED(isolatedManager->ActivateEx(&isolatedClient,1 /* TF_TMAE_NOACTIVATETIP */)),"UI isolated activation");
    // Vector-art DPI simulation is separate from physical monitor validation.
    for (UINT dpi : {96u,120u,144u,192u,288u}) for (int theme : {0,1,2,10,11,12}) {
        auto scale = [dpi](int value) { return MulDiv(value,static_cast<int>(dpi),96); };
        HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen,scale(260),scale(180)); auto previous = SelectObject(dc,bitmap);
        chengyin::Preferences preferences;preferences.theme=theme;preferences.skin=chengyin::builtinSkin(theme);
        const auto colors = chengyin::palette(preferences);
        const int style = chengyin::visualTheme(theme);
        RECT surface{0,0,scale(260),scale(180)}, row{scale(3),scale(60),scale(257),scale(87)}, badge{scale(6),scale(60),scale(20),scale(87)};
        chengyin::drawSkinSurface(dc,surface,colors,preferences,dpi,chengyin::skinRail(preferences,dpi));
        chengyin::drawThemeSelection(dc,row,colors,style,dpi,true,false);
        chengyin::drawThemeBadge(dc,badge,colors,style,dpi,true);
        GdiFlush();
        const COLORREF center = GetPixel(dc,scale(120),scale(73)), outside = GetPixel(dc,scale(120),scale(100));
        require(center != CLR_INVALID && outside != CLR_INVALID && center != outside,
                "all themes render visible selection and vector surfaces at 100/125/150/200/300 percent");
        SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr,screen);
    }
    HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
    require(SetTimer(nullptr, 0, 100, inspect) != 0, "UI inspection timer");
    require(chengyin::runSettings(chengyin::module, SW_SHOW, nullptr, nullptr, 0, false) == 0 && stage == 41, "all settings pages painted");
    HWND owner = CreateWindowW(L"STATIC", L"澄音 visual regression", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               10, 10, 400, 200, nullptr, nullptr, chengyin::module, nullptr);
    ChengyinSession *session = chengyin_session_new();
    require(owner && session, "candidate fixture");
    require(chengyin_session_configure(session, 5, 3) == 0, "five candidate layout");
    for (char c : std::string("zhongguo"))
        chengyin_session_process(session, static_cast<uint32_t>(c), 0);
    {
        chengyin::CandidateWindow candidates;
        chengyin::Preferences prefs;
        candidates.show(session, owner, RECT{100, 150, 101, 170}, false, nullptr, nullptr, 1, prefs);
        HWND popup = candidates.handle();
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
        prefs.layout = 0;
        prefs.candidatePinyin = true;
        candidates.show(session, owner, RECT{100,150,101,170}, false, nullptr,nullptr,4,prefs,true);
        RECT compact{}, comfortable{};
        GetWindowRect(popup, &compact);
        require(compact.right - compact.left < 240, "inline compact candidates have no 240px minimum");
        capture(popup, L"candidate-inline-compact");
        prefs.density = 1;
        candidates.show(session,owner,RECT{100,150,101,170},false,nullptr,nullptr,5,prefs,true);
        GetWindowRect(popup,&comfortable);
        require(compact.bottom - compact.top < comfortable.bottom - comfortable.top
                && compact.right - compact.left < comfortable.right - comfortable.left,
                "compact reduces both dimensions without changing text size");
        for (int theme : {0,1,2,10,11,12}) {
            prefs.theme = theme;
            prefs.density = 0;
            candidates.show(session,owner,RECT{100,150,101,170},false,nullptr,nullptr,6+theme,prefs,false);
            SendMessageW(popup,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
            const auto name = L"candidate-theme-" + std::to_wstring(theme);
            capture(popup,name.c_str());
            prefs.layout = 1;
            candidates.show(session,owner,RECT{100,150,101,170},false,nullptr,nullptr,20+theme,prefs,true);
            const auto horizontal = name + L"-horizontal-inline";
            capture(popup,horizontal.c_str());
            prefs.fontSize = 36;
            candidates.show(session,owner,RECT{100,150,101,170},false,nullptr,nullptr,30+theme,prefs,true);
            const auto large = name + L"-large";
            capture(popup,large.c_str());
            prefs.layout = 0; prefs.fontSize = 18;
        }
    }
    chengyin_session_free(session);
    const char spelling[]="zhang\t\xe5\xbc\xa0\t1000\n";
    auto dictionary=chengyin_dictionary_new_tsv(reinterpret_cast<const uint8_t *>(spelling),sizeof(spelling)-1);
    require(dictionary!=nullptr,"correction rendering fixture dictionary");
    session=chengyin_session_new_with_dictionary(dictionary); chengyin_dictionary_free(dictionary);
    require(session && chengyin_session_configure_matching(session,CHENGYIN_MATCHING_MASK)==0,"correction rendering options");
    {
        chengyin::CandidateWindow candidates;
        chengyin::Preferences prefs; prefs.density=0; prefs.candidatePinyin=true;
        for (const char *raw : {"zhnag","zhng","zhsng","zhaang","zang"}) {
            chengyin_session_reset(session);
            for (const char *c=raw;*c;++c) chengyin_session_process(session,static_cast<uint32_t>(*c),0);
            uint8_t marks[256]{};
            require(chengyin_session_candidate_marks(session,0,marks,256)>0 &&
                std::any_of(marks,marks+256,[](uint8_t mark){return mark!=0;}),"corrected candidate marks reach renderer");
            for (int layout=0;layout<2;++layout) {
                prefs.layout=layout;
                candidates.show(session,owner,RECT{100,150,101,170},false,nullptr,nullptr,100+layout,prefs,true);
                const auto name=L"candidate-correction-"+std::wstring(raw,raw+std::strlen(raw))+L"-"+std::to_wstring(layout);
                capture(candidates.handle(),name.c_str());
                RECT shown{}, hidden{};
                GetWindowRect(candidates.handle(), &shown);
                const int selected = chengyin_session_selected(session);
                prefs.candidatePinyin = false;
                candidates.refreshPreferences(session, prefs);
                GetWindowRect(candidates.handle(), &hidden);
                require(hidden.bottom - hidden.top < shown.bottom - shown.top || hidden.right - hidden.left < shown.right - shown.left,
                        "pinyin toggle hides corrected spelling and immediately shrinks visible popup");
                require(chengyin_session_selected(session) == selected, "appearance refresh preserves selection");
                const auto hiddenName = name + L"-pinyin-off";
                capture(candidates.handle(), hiddenName.c_str());
                prefs.candidatePinyin = true;
                candidates.refreshPreferences(session, prefs);
            }
        }
    }
    chengyin_session_free(session);
    // The actual embedded production vocabulary, without reading personal files.
    const HRSRC dailyResource = FindResourceW(chengyin::module, MAKEINTRESOURCEW(101), RT_RCDATA);
    const HGLOBAL dailyLoaded = dailyResource ? LoadResource(chengyin::module, dailyResource) : nullptr;
    const auto *dailyBytes = dailyLoaded ? static_cast<const uint8_t *>(LockResource(dailyLoaded)) : nullptr;
    dictionary = dailyBytes ? chengyin_dictionary_new_binary(dailyBytes, SizeofResource(chengyin::module, dailyResource)) : nullptr;
    require(dictionary != nullptr, "embedded production dictionary for mapping candidates");
    session = chengyin_session_new_with_dictionary(dictionary);
    chengyin_dictionary_free(dictionary);
    require(session && chengyin_session_configure_matching(session, CHENGYIN_MATCHING_MASK) == 0,
            "mapping candidate matching options");
    {
        chengyin::CandidateWindow candidates;
        chengyin::Preferences prefs; prefs.density = 0; prefs.candidatePinyin = true;
        for (const char *raw : {"yingshe", "yinshe", "yin'she", "paizhao", "pai'zhao"}) {
            chengyin_session_reset(session);
            for (const char *c = raw; *c; ++c) chengyin_session_process(session, static_cast<uint32_t>(*c), 0);
            const bool photo = std::strncmp(raw, "pai", 3) == 0;
            bool mapping = false;
            for (int i = 0; i < chengyin_session_candidate_count(session); ++i) {
                uint8_t text[257]{};
                require(chengyin_session_text(session, CHENGYIN_TEXT_CANDIDATE, i, text, sizeof(text)) > 0,
                        "mapping candidate text");
                const auto *word = reinterpret_cast<const char *>(text);
                mapping |= std::strcmp(word, photo ? "拍照" : "映射") == 0;
                if (photo && i < 2)
                    require(std::strcmp(word, i == 0 ? "拍照" : "牌照") == 0,
                        "photo complete words precede corrected alternatives");
                require(std::strcmp(word, "应设") != 0 && std::strcmp(word, "因设") != 0 && std::strcmp(word, "银设") != 0,
                        "no unattested single character cross product in production vocabulary");
                for (const char *unsupported : {"拍找", "派找", "排找", "牌找", "拍赵"})
                    require(std::strcmp(word, unsupported) != 0, "no unsupported photo character pair");
                require(chengyin_session_candidate_consumed(session, i) == static_cast<int>(std::strlen(raw)),
                        "whole word line does not consume just a prefix character");
            }
            require(mapping, "mapping is visible in exact and fuzzy first pages");
            candidates.show(session, owner, RECT{100, 150, 101, 170}, false, nullptr, nullptr, 200, prefs, true);
            const auto name = L"candidate-mapping-" + std::wstring(raw, raw + std::strlen(raw));
            capture(candidates.handle(), name.c_str());
            if (photo) {
                uint8_t text[257]{};
                chengyin_session_process(session, CHENGYIN_KEY_SELECT_1, 0);
                require(chengyin_session_text(session, CHENGYIN_TEXT_COMMIT, 0, text, sizeof(text)) > 0
                    && std::strcmp(reinterpret_cast<const char *>(text), "拍照") == 0,
                    "photo selection commits the full word through ABI");
            }
        }
    }
    chengyin_session_free(session);
    // One-off wrong learned phrase must not displace the complete technical term.
    dictionary = chengyin_dictionary_new_binary(dailyBytes, SizeofResource(chengyin::module, dailyResource));
    require(dictionary != nullptr, "embedded initials fixture dictionary");
    session = chengyin_session_new_with_dictionary(dictionary);
    chengyin_dictionary_free(dictionary);
    require(session && chengyin_session_configure(session, 5, 1) == 0
        && chengyin_session_configure_matching(session, CHENGYIN_MATCHING_MASK) == 0
        && chengyin_session_configure_incremental(session, 1) == 0, "production initials matching fixture");
    {
        chengyin::CandidateWindow candidates;
        chengyin::Preferences prefs; prefs.candidatePinyin = true;
        for (const char *raw : {"bao", "shi"}) {
            chengyin_session_reset(session);
            for (const char *c = raw; *c; ++c) chengyin_session_process(session, static_cast<uint32_t>(*c), 0);
            for (size_t i = 0; i < chengyin_session_candidate_count(session); ++i) {
                uint8_t text[257]{};
                require(chengyin_session_text(session, CHENGYIN_TEXT_CANDIDATE, i, text, sizeof(text)) > 0,
                        "single syllable candidate text");
                wchar_t wide[257]{};
                require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            reinterpret_cast<const char *>(text), -1, wide, 257) == 2,
                        "complete syllable first page contains single characters");
                require(chengyin_session_candidate_consumed(session, i) == static_cast<int>(std::strlen(raw)),
                        "single character consumes complete spelling");
            }
            uint8_t chosen[257]{}, committed[257]{};
            chengyin_session_text(session, CHENGYIN_TEXT_CANDIDATE, 0, chosen, sizeof(chosen));
            for (int layout = 0; layout < 2; ++layout) {
                prefs.layout = layout;
                candidates.show(session, owner, RECT{100,150,101,170}, false, nullptr, nullptr, 410, prefs, true);
                const auto name = L"candidate-syllable-" + std::wstring(raw, raw + std::strlen(raw))
                    + L"-" + std::to_wstring(layout);
                capture(candidates.handle(), name.c_str());
            }
            chengyin_session_process(session, CHENGYIN_KEY_SELECT_1, 0);
            require(chengyin_session_text(session, CHENGYIN_TEXT_COMMIT, 0, committed, sizeof(committed)) > 0
                    && std::strcmp(reinterpret_cast<const char *>(committed), reinterpret_cast<const char *>(chosen)) == 0,
                    "single syllable commits the displayed character through ABI");
        }
        for (const char *raw : {"ssdd", "zgrm", "zhrm"}) {
            chengyin_session_reset(session);
            for (const char *c = raw; *c; ++c) chengyin_session_process(session, static_cast<uint32_t>(*c), 0);
            uint8_t text[257]{};
            const char *expected = std::strcmp(raw, "ssdd") == 0 ? "世世代代"
                : std::strcmp(raw, "zgrm") == 0 ? "中国人民" : "走火入魔";
            require(chengyin_session_text(session, CHENGYIN_TEXT_CANDIDATE, 0, text, sizeof(text)) > 0
                && std::strcmp(reinterpret_cast<const char *>(text), expected) == 0,
                "four-character initials term precedes shorter corrected words");
            require(chengyin_session_candidate_consumed(session, 0) == 4,
                "initials choice consumes all four heads");
            for (int layout = 0; layout < 2; ++layout) {
                prefs.layout = layout;
                candidates.show(session, owner, RECT{100,150,101,170}, false, nullptr, nullptr, 400, prefs, true);
                const auto name = L"candidate-initials-" + std::wstring(raw, raw + std::strlen(raw))
                    + L"-" + std::to_wstring(layout);
                capture(candidates.handle(), name.c_str());
            }
            chengyin_session_process(session, CHENGYIN_KEY_SELECT_1, 0);
            require(chengyin_session_text(session, CHENGYIN_TEXT_COMMIT, 0, text, sizeof(text)) > 0
                && std::strcmp(reinterpret_cast<const char *>(text), expected) == 0,
                "initials selection commits the complete word through ABI");
        }
    }
    chengyin_session_free(session);
    dictionary = chengyin_dictionary_new_binary(dailyBytes, SizeofResource(chengyin::module, dailyResource));
    require(dictionary != nullptr, "embedded regex fixture dictionary");
    session = chengyin_session_new_with_dictionary(dictionary);
    chengyin_dictionary_free(dictionary);
    auto history = chengyin_profile_new(nullptr, 0);
    constexpr char regexRaw[] = "zhengzebiaodashi";
    constexpr char wrongRegex[] = "正则表达是";
    require(session && history && chengyin_profile_record(history,
        reinterpret_cast<const uint8_t *>(regexRaw), sizeof(regexRaw) - 1,
        reinterpret_cast<const uint8_t *>(wrongRegex), sizeof(wrongRegex) - 1) == 0,
        "synthetic one-off regex history");
    require(chengyin_session_set_profile(session, history) == 0 && chengyin_session_configure(session, 5, 3) == 0
        && chengyin_session_configure_matching(session, CHENGYIN_MATCHING_MASK) == 0, "regex history and matching fixture");
    require(chengyin_session_configure_incremental(session, 1) == 0, "production prefix candidate mode");
    chengyin_profile_free(history);
    for (const char *c = regexRaw; *c; ++c) chengyin_session_process(session, static_cast<uint32_t>(*c), 0);
    uint8_t firstRegex[257]{};
    require(chengyin_session_text(session, CHENGYIN_TEXT_CANDIDATE, 0, firstRegex, sizeof(firstRegex)) > 0
        && std::strcmp(reinterpret_cast<const char *>(firstRegex), "正则表达式") == 0,
        "technical term wins despite one wrong historical selection");
    require(chengyin_session_candidate_count(session) > 1, "complete term plus manual prefix choices");
    for (size_t i = 1; i < chengyin_session_candidate_count(session); ++i)
        require(chengyin_session_candidate_consumed(session, i) < static_cast<int>(sizeof(regexRaw) - 1),
                "extra regex choices consume only an initial dictionary prefix");
    {
        chengyin::CandidateWindow candidates;
        chengyin::Preferences prefs; prefs.candidatePinyin = true;
        for (int layout = 0; layout < 2; ++layout) {
            prefs.layout = layout;
            candidates.show(session, owner, RECT{100, 150, 101, 170}, false, nullptr, nullptr, 300, prefs, true);
            capture(candidates.handle(), layout == 0 ? L"candidate-regex-vertical" : L"candidate-regex-horizontal");
        }
    }
    int prefixIndex = -1;
    for (size_t i = 0; i < chengyin_session_candidate_count(session); ++i) {
        uint8_t word[257]{};
        chengyin_session_text(session, CHENGYIN_TEXT_CANDIDATE, i, word, sizeof(word));
        if (std::strcmp(reinterpret_cast<const char *>(word), "正则") == 0) prefixIndex = static_cast<int>(i);
    }
    require(prefixIndex >= 0, "regex first page has selectable prefix phrase");
    require(chengyin_session_process(session, CHENGYIN_KEY_SELECT_1 + prefixIndex, 0) > 0, "select regex prefix phrase");
    uint8_t remaining[257]{};
    require(chengyin_session_text(session, CHENGYIN_TEXT_PREEDIT, 0, remaining, sizeof(remaining)) > 0
            && std::strcmp(reinterpret_cast<const char *>(remaining), "biaodashi") == 0, "regex prefix leaves only suffix pinyin");
    {
        chengyin::CandidateWindow candidates;
        chengyin::Preferences prefs; prefs.candidatePinyin = true;
        candidates.show(session, owner, RECT{100,150,101,170}, false, nullptr, nullptr, 301, prefs, true);
        capture(candidates.handle(), L"candidate-regex-remaining");
    }
    chengyin_session_free(session);
    DestroyWindow(owner);
    inspectTestpad();
    if (richEdit)
        FreeLibrary(richEdit);
    isolatedManager->Deactivate();isolatedManager.reset();
    CoRevokeClassObject(fixtureCookie);
    CoUninitialize();
    FreeLibrary(fixtureModule);
    require(chengyin::objects == 0, "UI lifetimes released");
    std::puts("PASS: eight tabs, live fonts after hover/theme recreation, 96/120/144/192/288 DPI, narrow viewport/scroll, six candidate themes, letter correction marks, compact geometry and embedded test lifetime; no user settings written.");
    return 0;
}
