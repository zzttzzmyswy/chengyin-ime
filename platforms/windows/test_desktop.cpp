// SPDX-License-Identifier: GPL-3.0-or-later
// Build-only settings host: process-local COM activation of the new fixture.
// No system registration or user history changes.
#include "common.h"
#include "settings.h"
#include <cstdio>
namespace chengyin { HINSTANCE module = nullptr; LONG objects = 0; }
namespace {
class DesktopFactory final : public IClassFactory {
    chengyin::Ptr<IClassFactory> source_;
    ULONG refs_ = 1;
  public:
    chengyin::Ptr<chengyin::ProcessorEx> latest;
    explicit DesktopFactory(IClassFactory *source): source_(source) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG result = --refs_; if (!result) delete this; return result; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **out) override {
        HRESULT result = source_->CreateInstance(outer, iid, out);
        if (SUCCEEDED(result) && out && *out)
            chengyin::query(static_cast<IUnknown *>(*out), chengyin::kProcessorEx, latest);
        std::fprintf(stderr, "real TSF factory activation=%lx\n", static_cast<unsigned long>(result));
        return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { return source_->LockServer(lock); }
};
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2 || FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    chengyin::module = GetModuleHandleW(nullptr);
    HMODULE library = LoadLibraryW(argv[1]);
    using Factory = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
    auto factoryFunction = chengyin::procedureAddress<Factory>(library, "DllGetClassObject");
    int result = 1;
    {
        chengyin::Ptr<IClassFactory> source;
        if (!factoryFunction || FAILED(factoryFunction(chengyin::kService, IID_IClassFactory, reinterpret_cast<void **>(source.put())))) return 1;
        chengyin::Ptr<DesktopFactory> factory; factory.attach(new DesktopFactory(source.get()));
        DWORD registration = 0;
        HRESULT registered = CoRegisterClassObject(chengyin::kService, factory.get(), CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, &registration);
        if (FAILED(registered)) { std::fprintf(stderr, "apartment factory registration=%lx\n", static_cast<unsigned long>(registered)); return 1; }
        chengyin::Ptr<ITfThreadMgr> manager;
        chengyin::Ptr<ITfMessagePump> pump;
        chengyin::Ptr<ITfKeystrokeMgr> keys;
        chengyin::Ptr<ITfInputProcessorProfiles> profiles;
        chengyin::Ptr<ITfInputProcessorProfileMgr> profileManager;
        TfClientId client = TF_CLIENTID_NULL;
        if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, reinterpret_cast<void **>(manager.put())))
            || FAILED(manager->Activate(&client))
            || FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, reinterpret_cast<void **>(profiles.put())))
            || FAILED(chengyin::query(profiles.get(), IID_ITfInputProcessorProfileMgr, profileManager))) return 1;
        profiles->ChangeCurrentLanguage(chengyin::kLanguage);
        HRESULT activated = profileManager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, chengyin::kLanguage, chengyin::kService, chengyin::kProfile, nullptr, TF_IPPMF_FORPROCESS);
        std::fprintf(stderr, "real TSF profile activation=%lx\n", static_cast<unsigned long>(activated));
        if (FAILED(activated)) return 1;
        chengyin::query(manager.get(), IID_ITfMessagePump, pump);
        chengyin::query(manager.get(), IID_ITfKeystrokeMgr, keys);
        HMODULE rich = LoadLibraryW(L"Msftedit.dll");
        result = chengyin::runSettings(chengyin::module, SW_SHOW, pump.get(), keys.get(), 5, false);
        profileManager->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, chengyin::kLanguage, chengyin::kService, chengyin::kProfile, nullptr, TF_IPPMF_FORPROCESS);
        manager->Deactivate();
        factory->latest.reset();
        CoRevokeClassObject(registration);
        if (rich) FreeLibrary(rich);
    }
    if (library) FreeLibrary(library);
    CoUninitialize();
    return result;
}
