// SPDX-License-Identifier: GPL-3.0-or-later
#include "language_bar.h"
#include "preferences.h"
#include <shellapi.h>
#include <olectl.h>
#include <algorithm>
#include <vector>
#include <cwchar>
namespace chengyin {
namespace {
HRESULT string(BSTR *out, const wchar_t *text) {
    if (!out)
        return E_POINTER;
    *out = SysAllocString(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}
HICON stateIcon(bool english) {
    const int size = std::max(16, GetSystemMetrics(SM_CXSMICON));
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void *bits = nullptr;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HICON icon = nullptr;
    if (dc && bitmap && bits) {
        auto old = SelectObject(dc, bitmap);
        HBRUSH background = CreateSolidBrush(RGB(0, 70, 127));
        RECT rect{0, 0, size, size};
        FillRect(dc, &rect, background);
        DeleteObject(background);
        HFONT font = createUIFont(size - 2, 96, L"Microsoft YaHei UI", FW_SEMIBOLD);
        auto oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, english ? L"英" : L"中", 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);
        DeleteObject(font);
        GdiFlush();
        auto pixels = static_cast<DWORD *>(bits);
        for (int i = 0; i < size * size; ++i)
            pixels[i] |= 0xff000000;
        SelectObject(dc, old);
        std::vector<uint8_t> maskBits(static_cast<size_t>((size + 15) / 16 * 2) * size, 0);
        HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
        if (mask) {
            ICONINFO source{TRUE, 0, 0, mask, bitmap};
            icon = CreateIconIndirect(&source);
            DeleteObject(mask);
        }
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (dc)
        DeleteDC(dc);
    if (screen)
        ReleaseDC(nullptr, screen);
    return icon;
}
}
LanguageBarItem::~LanguageBarItem() {
    for (auto icon : icons_)
        if (icon)
            DestroyIcon(icon);
}
HRESULT LanguageBarItem::QueryInterface(REFIID id, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_ITfLangBarItem || id == kLanguageButton)
        *out = static_cast<LanguageButton *>(this);
    else if (id == IID_ITfSource)
        *out = static_cast<ITfSource *>(this);
    else
        return E_NOINTERFACE;
    AddRef();
    return S_OK;
}
ULONG LanguageBarItem::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&refs_));
}
ULONG LanguageBarItem::Release() {
    LONG n = InterlockedDecrement(&refs_);
    if (!n)
        delete this;
    return static_cast<ULONG>(n);
}
HRESULT LanguageBarItem::GetInfo(TF_LANGBARITEMINFO *out) {
    if (!out)
        return E_POINTER;
    *out = {};
    out->clsidService = kService;
    out->guidItem = kInputModeButton;
    out->dwStyle = 0x00010000 | 0x00040000 | 0x2;
    out->ulSort = 0;
    std::wcscpy(out->szDescription, L"澄音输入法");
    return S_OK;
}
HRESULT LanguageBarItem::GetStatus(DWORD *out) {
    if (!out)
        return E_POINTER;
    *out = (hidden_ ? 1 : 0) | (!english_ ? 0x10000 : 0);
    return S_OK;
}
HRESULT LanguageBarItem::Show(BOOL show) {
    hidden_ = !show;
    notify();
    return S_OK;
}
HRESULT LanguageBarItem::GetTooltipString(BSTR *out) {
    return string(out, english_ ? L"澄音：英文模式（单击切换为中文）" :
                  L"澄音：中文模式（单击切换为英文）");
}
HRESULT LanguageBarItem::GetText(BSTR *out) {
    return string(out, english_ ? L"澄音 英" : L"澄音 中");
}
HRESULT LanguageBarItem::OnClick(int click, POINT point, const RECT *) {
    if (click == 2 && toggle_) {
        auto toggle = toggle_;
        toggle();
    } else if (click == 1) {
        HMENU menu = CreatePopupMenu();
        if (!menu)
            return E_OUTOFMEMORY;
        AppendMenuW(menu, MF_STRING, 1, english_ ? L"切换为中文" : L"切换为英文");
        AppendMenuW(menu, MF_STRING, 2, L"澄音设置…");
        HWND owner = GetForegroundWindow();
        UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, owner, nullptr);
        DestroyMenu(menu);
        if (choice)
            OnMenuSelect(choice);
    }
    return S_OK;
}
HRESULT LanguageBarItem::InitMenu(LanguageMenu *menu) {
    if (!menu)
        return E_POINTER;
    const wchar_t *mode = english_ ? L"切换为中文" : L"切换为英文";
    HRESULT hr = menu->AddMenuItem(1, 0, nullptr, nullptr, mode, static_cast<ULONG>(std::wcslen(mode)), nullptr);
    if (SUCCEEDED(hr))
        hr = menu->AddMenuItem(2, 0, nullptr, nullptr, L"澄音设置…", 5, nullptr);
    return hr;
}
HRESULT LanguageBarItem::OnMenuSelect(UINT id) {
    if (id == 1 && toggle_) {
        auto toggle = toggle_;
        toggle();
    } else if (id == 2)
        openSettings();
    return S_OK;
}
HRESULT LanguageBarItem::GetIcon(HICON *out) {
    if (!out)
        return E_POINTER;
    auto &icon = icons_[english_ ? 1 : 0];
    if (!icon)
        icon = stateIcon(english_);
    *out = icon ? CopyIcon(icon) : nullptr;
    return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT LanguageBarItem::AdviseSink(REFIID id, IUnknown *sink, DWORD *cookie) {
    if (!sink || !cookie)
        return E_POINTER;
    *cookie = TF_INVALID_COOKIE;
    if (id != IID_ITfLangBarItemSink)
        return CONNECT_E_CANNOTCONNECT;
    if (sink_)
        return CONNECT_E_ADVISELIMIT;
    HRESULT hr = query(sink, IID_ITfLangBarItemSink, sink_);
    if (SUCCEEDED(hr))
        *cookie = 1;
    return hr;
}
HRESULT LanguageBarItem::UnadviseSink(DWORD cookie) {
    if (cookie != 1 || !sink_)
        return CONNECT_E_NOCONNECTION;
    sink_.reset();
    return S_OK;
}
void LanguageBarItem::notify() {
    Ptr<ITfLangBarItemSink> sink(sink_.get());
    if (sink)
        sink->OnUpdate(0x10007);
}
void LanguageBarItem::setEnglish(bool english) {
    if (english_ != english) {
        english_ = english;
        notify();
    }
}
void openSettings() {
    wchar_t path[32768] {};
    if (!GetModuleFileNameW(module, path, 32768))
        return;
    std::wstring file(path);
    file = file.substr(0, file.find_last_of(L'\\') + 1) + L"chengyin_settings.exe";
    ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
}
