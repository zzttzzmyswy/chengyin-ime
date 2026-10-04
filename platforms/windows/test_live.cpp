#include "configuration.h"
#include "language_bar.h"
#include "punctuation.h"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <cwchar>
namespace myswy {
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
    myswy::Ptr<myswy::LanguageBarItem> item;
    unsigned toggles = 0;
    item.attach(new myswy::LanguageBarItem([&] {++toggles;}));
    myswy::Ptr<myswy::LanguageButton> button;
    require(SUCCEEDED(myswy::query(static_cast<myswy::LanguageButton *>(item.get()), myswy::kLanguageButton,
                                   button)), "public button interface");
    TF_LANGBARITEMINFO info{};
    require(SUCCEEDED(item->GetInfo(&info)) && info.clsidService == myswy::kService
            && info.guidItem == myswy::kInputModeButton
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
int main() {
    myswy::module = GetModuleHandleW(nullptr);
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM apartment");
    wchar_t root[MAX_PATH] {};
    require(GetTempPathW(MAX_PATH, root) > 0, "temporary root");
    auto file = std::wstring(root) + L"chengyin-live-" + std::to_wstring(GetCurrentProcessId()) + L".ini";
    auto name = L"Local\\Chengyin.Live.Test." + std::to_wstring(GetCurrentProcessId());
    const DWORD mainThread = GetCurrentThreadId();
    {
        myswy::LearningEpoch published(myswy::kConfigurationEpoch);
        myswy::LearningEpoch epoch(name.c_str());
        std::atomic<unsigned> loads{0};
        unsigned received[2] {};
        int sizes[2] {5, 5};
        auto load = [&] {
            require(GetCurrentThreadId() != mainThread, "file parsing stays off input thread");
            ++loads;
            auto snapshot = std::make_shared<myswy::ConfigurationUpdate>();
            snapshot->preferencesValid = myswy::tryLoadPreferences(file, snapshot->preferences);
            return snapshot;
        };
        auto apply = [&](int index, myswy::ConfigurationWatcher::Snapshot snapshot) {
            require(GetCurrentThreadId() == mainThread, "configuration delivered in TSF apartment");
            ++received[index];
            if (snapshot->preferencesValid)
                sizes[index] = snapshot->preferences.pageSize;
        };
        myswy::ConfigurationWatcher first(epoch.current(), load, [&](auto s) {
            apply(0, std::move(s));
        }, name.c_str());
        myswy::ConfigurationWatcher second(epoch.current(), load, [&](auto s) {
            apply(1, std::move(s));
        }, name.c_str());
        require(first.valid() && second.valid(), "two existing app watchers");
        myswy::Preferences prefs;
        prefs.pageSize = 9;
        prefs.chinesePunctuation = false;
        require(myswy::savePreferences(file, prefs), "atomic preferences publish");
        epoch.advance();
        until([&] {return received[0] && received[1];});
        require(sizes[0] == 9 && sizes[1] == 9, "all existing applications refresh");
        require(!myswy::loadPreferences(file).chinesePunctuation, "punctuation setting persists");
        unsigned before = loads.load();
        for (int i = 0; i < 100; ++i)
            epoch.advance();
        prefs.pageSize = 7;
        require(myswy::savePreferences(file, prefs), "latest config");
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
        require(!myswy::savePreferences(file, prefs)
                && generation == published.current(), "failed save does not signal update");
    }
    require(myswy::objects == 0, "watchers and pending snapshots released");
    // Closing while a worker owns an undelivered update must also drain its resources.
    {
        myswy::LearningEpoch epoch(name.c_str());
        std::atomic<bool> loaded{false};
        myswy::ConfigurationWatcher watcher(epoch.current(), [&] {auto s = std::make_shared<myswy::ConfigurationUpdate>(); loaded = true; return s;}, [](
        auto) {
            require(false, "closed watcher has no callback");
        }, name.c_str());
        epoch.advance();
        DWORD begin = GetTickCount();
        while (!loaded && GetTickCount() - begin < 2000)
            Sleep(5);
        require(loaded, "pending snapshot created before close");
    }
    languageBar();
    myswy::PunctuationState p;
    wchar_t out[3] {};
    require(p.render('^', true, out) == 2 && out[0] == L'…' && out[1] == L'…', "Chinese ellipsis pair");
    require(p.render('_', true, out) == 2 && out[0] == L'—', "Chinese dash pair");
    require(p.render(',', false, out) == 1 && out[0] == L',', "ASCII punctuation preference");
    DeleteFileW(file.c_str());
    CoUninitialize();
    require(myswy::objects == 0, "all module lifetimes released");
    std::puts("PASS: multiple live config recipients, coalescing, invalid updates, shutdown, language bar and punctuation.");
}
