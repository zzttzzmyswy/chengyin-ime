#pragma once
#include "common.h"
#include <ctfutb.h>
#include <functional>
namespace chengyin {
// SDK-compatible declarations for interfaces absent from older MinGW ctfutb.h.
inline constexpr GUID kLanguageButton = {0x28c7f1d0, 0xde25, 0x11d2, {0xaf, 0xdd, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}};
inline constexpr GUID kInputModeButton = {0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};
inline constexpr GUID kSystrayCategory = {0x25504fb4, 0x7bab, 0x4bc1, {0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5}};
struct LanguageMenu : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE AddMenuItem(UINT, DWORD, HBITMAP, HBITMAP, const WCHAR *, ULONG,
            LanguageMenu **) = 0;
};
struct LanguageButton : ITfLangBarItem {
    virtual HRESULT STDMETHODCALLTYPE OnClick(int, POINT, const RECT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE InitMenu(LanguageMenu *) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIcon(HICON *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetText(BSTR *) = 0;
};
class LanguageBarItem final : public LanguageButton, public ITfSource {
  public:
    explicit LanguageBarItem(std::function<void()> toggle): toggle_(std::move(toggle)) {}
    ~LanguageBarItem();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetInfo(TF_LANGBARITEMINFO *) override;
    HRESULT STDMETHODCALLTYPE GetStatus(DWORD *) override;
    HRESULT STDMETHODCALLTYPE Show(BOOL) override;
    HRESULT STDMETHODCALLTYPE GetTooltipString(BSTR *) override;
    HRESULT STDMETHODCALLTYPE OnClick(int, POINT, const RECT *) override;
    HRESULT STDMETHODCALLTYPE InitMenu(LanguageMenu *) override;
    HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT) override;
    HRESULT STDMETHODCALLTYPE GetIcon(HICON *) override;
    HRESULT STDMETHODCALLTYPE GetText(BSTR *) override;
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID, IUnknown *, DWORD *) override;
    HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD) override;
    void setEnglish(bool);
    void detach() {
        toggle_ = {};
    }
  private:
    void notify();
    ModuleLifetime lifetime_;
    LONG refs_ = 1;
    bool english_ = false, hidden_ = false;
    HICON icons_[2] {};
    Ptr<ITfLangBarItemSink> sink_;
    std::function<void()> toggle_;
};
void openSettings();
}
