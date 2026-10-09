// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <msctf.h>
#include <new>
#include <utility>
#include <cstring>
#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL 0
#endif

namespace chengyin {
inline constexpr GUID kService = {0x65c32a54, 0x219a, 0x4f0a, {0xb4, 0x4c, 0xb9, 0x63, 0xd7, 0xba, 0x53, 0x2f}};
inline constexpr GUID kProfile = {0xe4f616aa, 0x8696, 0x4378, {0x9b, 0x9e, 0x74, 0x98, 0x78, 0x6a, 0x29, 0x40}};
inline constexpr GUID kProcessorEx = {0x6e4e2102, 0xf9cd, 0x433d, {0xb4, 0x96, 0x30, 0x3c, 0xe0, 0x3a, 0x65, 0x07}};
inline constexpr GUID kLayoutSink = {0x2af2d06a, 0xdd5b, 0x4927, {0xa0, 0xb4, 0x54, 0xf1, 0x9c, 0x91, 0xfa, 0xde}};
inline constexpr GUID kToggleKey = {0xae1e87cf, 0xdc8d, 0x4f7c, {0xb1, 0x1c, 0x7d, 0x70, 0x83, 0xa6, 0xe1, 0x6f}};
// Windows SDK category GUIDs (also verified against windows-rs 0.58.0 metadata).
inline constexpr GUID kUIElementCategory = {0x49d2f9cf, 0x1f5e, 0x11d7, {0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
inline constexpr GUID kImmersiveCategory = {0x13a016df, 0x560b, 0x46cd, {0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d}};
inline constexpr GUID kComlessCategory = {0x364215d9, 0x75bc, 0x11d7, {0xa6, 0xef, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
inline constexpr GUID kInputModeCategory = {0xccf05dd7, 0x4a87, 0x11d7, {0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
inline constexpr GUID kConversionMode = {0xccf05dd8, 0x4a87, 0x11d7, {0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
inline constexpr LANGID kLanguage = 0x0804;
inline constexpr wchar_t kName[] = L"澄音输入法（全拼预览）";
inline constexpr wchar_t kClsidPath[] = L"Software\\Classes\\CLSID\\{65C32A54-219A-4F0A-B44C-B963D7BA532F}";
// This SDK-compatible interface declaration also works with MinGW's older msctf.h.
struct ProcessorEx : ITfTextInputProcessor {
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr *, TfClientId, DWORD) = 0;
};
// SDK-compatible ITfThreadMgrEx for older MinGW headers used by fixture probes.
inline constexpr GUID kThreadManagerEx = {0x3e90ade3,0x7594,0x4cb0,{0xbb,0x58,0x69,0x62,0x8f,0x5f,0x45,0x8c}};
struct ThreadManagerEx : ITfThreadMgr {
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(TfClientId *, DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetActiveFlags(DWORD *) = 0;
};
// Same vtable and enum ABI as ITfTextLayoutSink (not present in older MinGW).
enum class LayoutCode { create = 0, change = 1, destroy = 2 };
struct LayoutSink : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext *, LayoutCode, ITfContextView *) = 0;
};
extern HINSTANCE module;
extern LONG objects;

// Put this before other data members: the module stays loaded until all COM
// references and windows owned by the object have been destroyed.
struct ModuleLifetime {
    ModuleLifetime() {
        InterlockedIncrement(&objects);
    }
    ~ModuleLifetime() {
        InterlockedDecrement(&objects);
    }
    ModuleLifetime(const ModuleLifetime &) = delete;
    ModuleLifetime &operator=(const ModuleLifetime &) = delete;
};
template<class T> T procedureAddress(HMODULE library, const char *name) {
    FARPROC address = GetProcAddress(library, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

template<class T> class Ptr {
  public:
    Ptr() = default;
    explicit Ptr(T *value) : p_(value) {
        if (p_)
            p_->AddRef();
    }
    ~Ptr() {
        reset();
    }
    Ptr(const Ptr &) = delete;
    Ptr &operator=(const Ptr &) = delete;
    T *get() const {
        return p_;
    }
    T *operator->() const {
        return p_;
    }
    explicit operator bool() const {
        return p_ != nullptr;
    }
    T **put() {
        reset();
        return &p_;
    }
    T *detach() {
        return std::exchange(p_, nullptr);
    }
    void attach(T *p) {
        reset();
        p_ = p;
    }
    void reset() {
        T *old = std::exchange(p_, nullptr);
        if (old)
            old->Release();
    }
  private:
    T *p_ = nullptr;
};

template<class T> HRESULT query(IUnknown *source, REFIID iid, Ptr<T> &out) {
    return source ? source->QueryInterface(iid, reinterpret_cast<void **>(out.put())) : E_INVALIDARG;
}
inline bool same(IUnknown *a, IUnknown *b) {
    if (!a || !b)
        return a == b;
    Ptr<IUnknown> x, y;
    return SUCCEEDED(query(a, IID_IUnknown, x)) && SUCCEEDED(query(b, IID_IUnknown, y)) && x.get() == y.get();
}
HRESULT createService(REFIID iid, void **out);
}
