// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration.h"
#include "language_bar.h"
#include "punctuation.h"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <cwchar>
namespace chengyin {
HINSTANCE module = nullptr;
LONG objects = 0;
}
namespace {
void require(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "live FAIL: %s\n", why);
        std::exit(1);
    }
}
template<class Predicate> void until(Predicate ready) {
    DWORD begin = GetTickCount();
    while (!ready() && GetTickCount() - begin < 4000) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(5);
    }
    require(ready(), "background delivery completes");
}
struct Sink : ITfLangBarItemSink {
    ULONG refs = 1;
    unsigned changes = 0;
    DWORD flags = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfLangBarItemSink)
            return E_NOINTERFACE;
        *out = static_cast<ITfLangBarItemSink *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return --refs;
    }
    HRESULT STDMETHODCALLTYPE OnUpdate(DWORD value) override {
        ++changes;
        flags = value;
        return S_OK;
    }
};
void languageBar() {
    chengyin::Ptr<chengyin::LanguageBarItem> item;
    unsigned toggles = 0;
    item.attach(new chengyin::LanguageBarItem([&] {++toggles;}));
    chengyin::Ptr<chengyin::LanguageButton> button;
    require(SUCCEEDED(chengyin::query(static_cast<chengyin::LanguageButton *>(item.get()), chengyin::kLanguageButton,
                                   button)), "public button interface");
    TF_LANGBARITEMINFO info{};
    require(SUCCEEDED(item->GetInfo(&info)) && info.clsidService == chengyin::kService
            && info.guidItem == chengyin::kInputModeButton
            && (info.dwStyle & 2), "system input-mode button uses our service identity");
    Sink sink;
    DWORD cookie = 0;
    require(SUCCEEDED(item->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie))
            && sink.refs == 2, "status sink retained");
    BSTR text = nullptr;
    require(SUCCEEDED(button->GetText(&text)) && std::wcscmp(text, L"澄音 中") == 0, "Chinese text");
    SysFreeString(text);
    HICON chinese = nullptr, english = nullptr;
    require(SUCCEEDED(button->GetIcon(&chinese)), "Chinese icon owned by host");
    item->setEnglish(true);
    require(sink.changes == 1
            && (sink.flags & 0x10007) == 0x10007, "mode changes notify icon, text, tooltip, status");
    item->setEnglish(true);
    require(sink.changes == 1, "same mode does not repaint shell");
    require(SUCCEEDED(button->GetText(&text)) && std::wcscmp(text, L"澄音 英") == 0, "English text");
    SysFreeString(text);
    require(SUCCEEDED(button->GetIcon(&english)) && chinese != english, "host gets separate mode icons");
    DWORD status = 0;
    item->GetStatus(&status);
    require(!(status & 0x10000), "English unchecks native mode");
    button->OnClick(2, POINT{}, nullptr);
    require(toggles == 1, "click triggers mode switch");
    button->OnMenuSelect(1);
    require(toggles == 2, "mode menu triggers switch");
    item->Show(FALSE);
    item->GetStatus(&status);
    require(status & 1, "host visibility honored");
    item->detach();
    button->OnClick(2, POINT{}, nullptr);
    require(toggles == 2, "retained shell item cannot call inactive processor");
    require(SUCCEEDED(item->UnadviseSink(cookie)) && sink.refs == 1, "sink released");
    button.reset();
    item.reset();
    ICONINFO copied{};
    require(GetIconInfo(chinese, &copied) != FALSE, "returned icon survives item destruction");
    DeleteObject(copied.hbmColor);
    DeleteObject(copied.hbmMask);
    DestroyIcon(chinese);
    DestroyIcon(english);
}
}
int wmain(int argc, wchar_t **argv) {
    chengyin::module = GetModuleHandleW(nullptr);
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM apartment");
    if (argc == 3 && !std::wcscmp(argv[1], L"--publish")) {
        chengyin::Preferences prefs;
        prefs.fontSize = 26; prefs.layout = 1; prefs.pageSize = 9;
        require(chengyin::savePreferences(argv[2], prefs), "separate publisher saves isolated preferences");
        CoUninitialize();
        return 0;
    }
    if (argc == 4 && !std::wcscmp(argv[1], L"--watch")) {
        bool complete = false;
        const auto name = L"Local\\Chengyin.Remote.Test." + std::to_wstring(GetCurrentProcessId());
        {
            chengyin::ConfigurationWatcher watcher(0, [&] {
                auto snapshot = std::make_shared<chengyin::ConfigurationUpdate>();
                snapshot->preferencesValid = chengyin::tryLoadPreferences(argv[2], snapshot->preferences);
                return snapshot;
            }, [&](auto snapshot) {
                if (!snapshot->preferencesValid) return;
                complete = snapshot->preferences.fontSize == 26 && !snapshot->preferences.candidatePinyin;
                const std::string report = complete ? "PASS" : "READY";
                require(chengyin::atomicWrite(argv[3], std::vector<uint8_t>(report.begin(), report.end())), "remote consumer report");
            }, name.c_str(), argv[2]);
            require(watcher.valid(), "separate existing consumer file watcher");
            until([&] {return complete;});
        }
        CoUninitialize();
        return 0;
    }
    wchar_t root[MAX_PATH] {};
    require(GetTempPathW(MAX_PATH, root) > 0, "temporary root");
    auto file = std::wstring(root) + L"chengyin-live-" + std::to_wstring(GetCurrentProcessId()) + L".ini";
    auto name = L"Local\\Chengyin.Live.Test." + std::to_wstring(GetCurrentProcessId());
    const DWORD mainThread = GetCurrentThreadId();
    {
        const auto isolatedPublication = chengyin::configurationEpochName();
        require(isolatedPublication != chengyin::kConfigurationEpoch
            && isolatedPublication.find(std::to_wstring(GetCurrentProcessId())) != std::wstring::npos,
            "fixture publication never uses the production configuration channel");
        chengyin::LearningEpoch published(isolatedPublication.c_str());
        chengyin::LearningEpoch epoch(name.c_str());
        std::atomic<unsigned> loads{0};
        unsigned received[2] {};
        int sizes[2] {5, 5};
        auto load = [&] {
            require(GetCurrentThreadId() != mainThread, "file parsing stays off input thread");
            ++loads;
            auto snapshot = std::make_shared<chengyin::ConfigurationUpdate>();
            snapshot->preferencesValid = chengyin::tryLoadPreferences(file, snapshot->preferences);
            return snapshot;
        };
        auto apply = [&](int index, chengyin::ConfigurationWatcher::Snapshot snapshot) {
            require(GetCurrentThreadId() == mainThread, "configuration delivered in TSF apartment");
            ++received[index];
            if (snapshot->preferencesValid)
                sizes[index] = snapshot->preferences.pageSize;
        };
        chengyin::ConfigurationWatcher first(epoch.current(), load, [&](auto s) {
            apply(0, std::move(s));
        }, name.c_str());
        chengyin::ConfigurationWatcher second(epoch.current(), load, [&](auto s) {
            apply(1, std::move(s));
        }, name.c_str());
        require(first.valid() && second.valid(), "two existing app watchers");
        chengyin::Preferences prefs;
        prefs.pageSize = 9;
        prefs.chinesePunctuation = false;
        require(chengyin::savePreferences(file, prefs), "atomic preferences publish");
        epoch.advance();
        until([&] {return received[0] && received[1];});
        require(sizes[0] == 9 && sizes[1] == 9, "all existing applications refresh");
        require(!chengyin::loadPreferences(file).chinesePunctuation, "punctuation setting persists");
        unsigned before = loads.load();
        for (int i = 0; i < 100; ++i)
            epoch.advance();
        prefs.pageSize = 7;
        require(chengyin::savePreferences(file, prefs), "latest config");
        epoch.advance();
        until([&] {return sizes[0] == 7 && sizes[1] == 7;});
        require(loads.load() - before < 20, "rapid saves coalesce");
        HANDLE invalid = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        require(invalid != INVALID_HANDLE_VALUE, "corrupt isolated config");
        DWORD written = 0;
        WriteFile(invalid, "broken", 6, &written, nullptr);
        CloseHandle(invalid);
        auto a = received[0], b = received[1];
        epoch.advance();
        until([&] {return received[0] > a && received[1] > b;});
        require(sizes[0] == 7 && sizes[1] == 7, "invalid updates retain usable configuration");
        auto generation = published.current();
        prefs.fontSize = 100;
        require(!chengyin::savePreferences(file, prefs)
                && generation == published.current(), "failed save does not signal update");
    }
    require(chengyin::objects == 0, "watchers and pending snapshots released");
    // Closing while a worker owns an undelivered update must also drain its resources.
    {
        chengyin::LearningEpoch epoch(name.c_str());
        std::atomic<bool> loaded{false};
        chengyin::ConfigurationWatcher watcher(epoch.current(), [&] {auto s = std::make_shared<chengyin::ConfigurationUpdate>(); loaded = true; return s;}, [](
        auto) {
            require(false, "closed watcher has no callback");
        }, name.c_str());
        epoch.advance();
        DWORD begin = GetTickCount();
        while (!loaded && GetTickCount() - begin < 2000)
            Sleep(5);
        require(loaded, "pending snapshot created before close");
    }
    // Separate process + deliberately unavailable named mapping: file changes
    // must still reach existing recipients without input or reactivation.
    {
        const auto blockedName = name + L".Blocked";
        HANDLE blocker = CreateEventW(nullptr, TRUE, FALSE, blockedName.c_str());
        require(blocker != nullptr, "private incompatible notification object");
        chengyin::LearningEpoch unavailable(blockedName.c_str());
        require(!unavailable.valid(), "shared mapping is actually unavailable for fallback regression");
        chengyin::Preferences prefs; prefs.pageSize = 5; prefs.candidatePinyin = true;
        require(chengyin::savePreferences(file, prefs), "file fallback baseline");
        int font[2]{}, page[2]{}; bool pinyin[2]{true,true};
        auto load = [&] {
            require(GetCurrentThreadId() != mainThread, "fallback disk I/O stays off input thread");
            auto snapshot = std::make_shared<chengyin::ConfigurationUpdate>();
            snapshot->preferencesValid = chengyin::tryLoadPreferences(file, snapshot->preferences);
            return snapshot;
        };
        auto apply = [&](int index, auto snapshot) {
            require(GetCurrentThreadId() == mainThread, "fallback delivered to owning apartment");
            if (snapshot->preferencesValid) {
                font[index] = snapshot->preferences.fontSize;
                page[index] = snapshot->preferences.pageSize;
                pinyin[index] = snapshot->preferences.candidatePinyin;
            }
        };
        {
            chengyin::ConfigurationWatcher first(0, load, [&](auto s) {apply(0,std::move(s));}, blockedName.c_str(), file);
            chengyin::ConfigurationWatcher second(0, load, [&](auto s) {apply(1,std::move(s));}, blockedName.c_str(), file);
            require(first.valid() && second.valid(), "watchers survive unavailable shared mapping");
            until([&] {return font[0] == 18 && font[1] == 18;});
            wchar_t executable[32768]{};
            require(GetModuleFileNameW(nullptr, executable, 32768) > 0, "self executable for separate processes");
            auto spawn = [&](const std::wstring &arguments) {
                std::wstring command = L"\"" + std::wstring(executable) + L"\" " + arguments;
                STARTUPINFOW startup{}; startup.cb = sizeof(startup);
                startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION process{};
                require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE,
                        "launch private existing consumer/publisher");
                CloseHandle(process.hThread);
                return process.hProcess;
            };
            const auto report = file + L".report";
            HANDLE consumer = spawn(L"--watch \"" + file + L"\" \"" + report + L"\"");
            auto reportIs = [&](const std::string &expected) {
                std::vector<uint8_t> data;
                return chengyin::readSmallFile(report,data,16) && std::string(data.begin(),data.end()) == expected;
            };
            until([&] {return reportIs("READY");});
            HANDLE publisher = spawn(L"--publish \"" + file + L"\"");
            until([&] {return font[0] == 26 && font[1] == 26 && page[0] == 9 && page[1] == 9 && !pinyin[0] && !pinyin[1];});
            until([&] {return reportIs("PASS");});
            for (HANDLE process : {publisher,consumer}) {
                require(WaitForSingleObject(process,4000) == WAIT_OBJECT_0, "separate process finished");
                DWORD exit = 1;
                require(GetExitCodeProcess(process,&exit) && exit == 0, "separate process succeeded");
                CloseHandle(process);
            }
            DeleteFileW(report.c_str());
        }
        CloseHandle(blocker);
    }
    languageBar();
    chengyin::PunctuationState p;
    wchar_t out[3] {};
    require(p.render('^', true, out) == 2 && out[0] == L'…' && out[1] == L'…', "Chinese ellipsis pair");
    require(p.render('_', true, out) == 2 && out[0] == L'—', "Chinese dash pair");
    require(p.render(',', false, out) == 1 && out[0] == L',', "ASCII punctuation preference");
    DeleteFileW(file.c_str());
    CoUninitialize();
    require(chengyin::objects == 0, "all module lifetimes released");
    std::puts("PASS: multiple live config recipients, coalescing, invalid updates, shutdown, language bar and punctuation.");
}
