#include "common.h"
#include "language_bar.h"
#include <cwchar>
#include <initializer_list>

namespace myswy {
HINSTANCE module = nullptr;
LONG objects = 0;
namespace {
class Factory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_); if (!n) delete this; return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return outer ? CLASS_E_NOAGGREGATION : createService(iid, out);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&objects); else InterlockedDecrement(&objects);
        return S_OK;
    }
private:
    ModuleLifetime lifetime_;
    LONG refs_ = 1;
};
HRESULT setString(HKEY key, const wchar_t* name, const wchar_t* value) {
    // Some SDKs define HRESULT_FROM_WIN32 as a macro that evaluates its argument
    // more than once. Never put a Win32 mutation or output parameter in it.
    const LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value), static_cast<DWORD>((std::wcslen(value) + 1) * sizeof(wchar_t)));
    return HRESULT_FROM_WIN32(status);
}
struct ComInit {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
    bool ok() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};
HRESULT unregisterService(bool rollback = false, bool profileStarted = true, bool categoryStarted = true) {
    if (!rollback) {
        wchar_t registered[32768]{}, path[32768]{};
        DWORD bytes = sizeof(registered);
        constexpr wchar_t serverPath[] = L"Software\\Classes\\CLSID\\{65C32A54-219A-4F0A-B44C-B963D7BA532F}\\InprocServer32";
        const LSTATUS read = RegGetValueW(HKEY_LOCAL_MACHINE, serverPath, nullptr, RRF_RT_REG_SZ, nullptr, registered, &bytes);
        if (read != ERROR_SUCCESS && read != ERROR_FILE_NOT_FOUND) return HRESULT_FROM_WIN32(read);
        const DWORD length = GetModuleFileNameW(module, path, 32768);
        if (!length || length >= 32768) return E_FAIL;
        if (read == ERROR_SUCCESS && _wcsicmp(registered, path) != 0) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    ComInit com;
    if (!com.ok()) return com.hr;
    HRESULT result = S_OK;
    Ptr<ITfCategoryMgr> categories;
    HRESULT hr = S_OK;
    if (categoryStarted) {
        hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITfCategoryMgr, reinterpret_cast<void**>(categories.put()));
        if (SUCCEEDED(hr)) hr = categories->UnregisterCategory(kService, GUID_TFCAT_TIP_KEYBOARD, kService);
        if (FAILED(hr)) result = hr;
        if (categories) {
            for(const GUID* category : {&kUIElementCategory,&kImmersiveCategory,&kComlessCategory,&kInputModeCategory,&kSystrayCategory}) {
                const HRESULT removed=categories->UnregisterCategory(kService,*category,kService);
                if(FAILED(removed) && SUCCEEDED(result))result=removed;
            }
        }
    }
    Ptr<ITfInputProcessorProfiles> profiles;
    if (profileStarted) {
        hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(profiles.put()));
        if (SUCCEEDED(hr)) hr = profiles->Unregister(kService);
        if (FAILED(hr) && SUCCEEDED(result)) result = hr;
    }
    if (SUCCEEDED(result)) {
        const LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidPath);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) result = HRESULT_FROM_WIN32(status);
    }
    return result;
}
HRESULT registerService(bool repair = false) {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    ComInit com;
    if (!com.ok()) return com.hr;
    HKEY key = nullptr, server = nullptr;
    DWORD disposition = 0;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kClsidPath, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, &disposition);
    HRESULT hr = HRESULT_FROM_WIN32(status);
    if (FAILED(hr)) return hr;
    const bool fresh = disposition == REG_CREATED_NEW_KEY;
    if (!fresh) {
        wchar_t registered[32768]{};
        DWORD bytes = sizeof(registered);
        const LSTATUS read = RegGetValueW(key, L"InprocServer32", nullptr, RRF_RT_REG_SZ, nullptr, registered, &bytes);
        if (!repair || read != ERROR_SUCCESS || _wcsicmp(registered, path) != 0) {
            RegCloseKey(key); return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        }
    }
    hr = setString(key, nullptr, kName);
    if (SUCCEEDED(hr)) {
        status = RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &server, nullptr);
        hr = HRESULT_FROM_WIN32(status);
    }
    if (SUCCEEDED(hr)) hr = setString(server, nullptr, path);
    if (SUCCEEDED(hr)) hr = setString(server, L"ThreadingModel", L"Apartment");
    if (server) RegCloseKey(server);
    RegCloseKey(key);
    Ptr<ITfInputProcessorProfiles> profiles;
    bool profileStarted = false, categoryStarted = false;
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(profiles.put()));
    if (SUCCEEDED(hr)) { profileStarted = true; hr = profiles->Register(kService); }
    if (SUCCEEDED(hr)) hr = profiles->AddLanguageProfile(kService, kLanguage, kProfile,
        kName, static_cast<ULONG>(std::wcslen(kName)), path, length, 0);
    if (SUCCEEDED(hr)) hr = profiles->EnableLanguageProfile(kService, kLanguage, kProfile, TRUE);
    Ptr<ITfCategoryMgr> categories;
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, reinterpret_cast<void**>(categories.put()));
    if (SUCCEEDED(hr)) { categoryStarted = true; hr = categories->RegisterCategory(kService, GUID_TFCAT_TIP_KEYBOARD, kService); }
    if(SUCCEEDED(hr))hr=categories->RegisterCategory(kService,kUIElementCategory,kService);
    if(SUCCEEDED(hr))hr=categories->RegisterCategory(kService,kImmersiveCategory,kService);
    if(SUCCEEDED(hr))hr=categories->RegisterCategory(kService,kComlessCategory,kService);
    if(SUCCEEDED(hr))hr=categories->RegisterCategory(kService,kInputModeCategory,kService);
    if(SUCCEEDED(hr))hr=categories->RegisterCategory(kService,kSystrayCategory,kService);
    if (FAILED(hr) && fresh) unregisterService(true, profileStarted, categoryStarted);
    return hr;
}
}
}
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) myswy::module = instance;
    // No COM, core initialization, disk access or window work under the loader lock.
    return TRUE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != myswy::kService) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) myswy::Factory;
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(iid, out);
    factory->Release(); return hr;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return InterlockedCompareExchange(&myswy::objects, 0, 0) == 0 ? S_OK : S_FALSE; }
extern "C" HRESULT WINAPI DllRegisterServer() {
#ifdef MYSWY_TEST_REGISTRATION_FAILURE
    return E_FAIL;
#else
    return myswy::registerService();
#endif
}
extern "C" HRESULT WINAPI DllUnregisterServer() { return myswy::unregisterService(); }
extern "C" HRESULT WINAPI DllInstall(BOOL install, LPCWSTR command) {
    if (!install || !command || std::wcscmp(command, L"repair") != 0) return E_INVALIDARG;
    return myswy::registerService(true);
}
