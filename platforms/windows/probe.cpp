#include "common.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include "package_verify.h"
void runServiceTests(myswy::ProcessorEx *service, ITfKeyEventSink *keys);

namespace {
int printRegistryString(const wchar_t *key, const wchar_t *value) {
    HKEY opened = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_64KEY, &opened);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
        return 1;
    if (status != ERROR_SUCCESS)
        return 2;
    wchar_t text[32768] {};
    DWORD bytes = sizeof(text);
    status = RegGetValueW(opened, nullptr, value[0] ? value : nullptr, RRF_RT_REG_SZ,
                          nullptr, text, &bytes);
    RegCloseKey(opened);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
        return 1;
    if (status != ERROR_SUCCESS)
        return 2;
    char utf8[98304] {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                          utf8, sizeof(utf8), nullptr, nullptr);
    if (count <= 0)
        return 2;
    return std::fwrite(utf8, 1, static_cast<size_t>(count - 1), stdout)
           == static_cast<size_t>(count - 1) ? 0 : 2;
}
bool readableRegistration(const wchar_t *key, const wchar_t *subkey, const wchar_t *value) {
    HKEY opened = nullptr;
    const LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_64KEY, &opened);
    if (status == ERROR_FILE_NOT_FOUND)
        return true;
    if (status != ERROR_SUCCESS)
        return false;
    wchar_t text[32768] {};
    DWORD bytes = sizeof(text);
    const LSTATUS read = RegGetValueW(opened, subkey, value, RRF_RT_REG_SZ, nullptr, text, &bytes);
    RegCloseKey(opened);
    return read == ERROR_SUCCESS && text[0] != 0;
}
void check(HRESULT actual, HRESULT expected, const char *name) {
    if (actual != expected) {
        std::fprintf(stderr, "%s: HRESULT 0x%08lx (expected 0x%08lx)\n", name,
                     static_cast<unsigned long>(actual), static_cast<unsigned long>(expected));
        std::exit(1);
    }
}
}
int wmain(int argc, wchar_t **argv) {
    if (argc == 4 && std::wcscmp(argv[1], L"--read-registry") == 0)
        return printRegistryString(argv[2], argv[3]);
    if (argc == 4 && std::wcscmp(argv[1], L"--hold-dll") == 0) {
        HMODULE held = LoadLibraryExW(argv[2], nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!held)
            return 2;
        HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, argv[3]);
        if (!stop) {
            FreeLibrary(held);
            return 2;
        }
        std::puts("READY");
        std::fflush(stdout);
        const DWORD wait = WaitForSingleObject(stop, 45000);
        CloseHandle(stop);
        FreeLibrary(held);
        return wait == WAIT_OBJECT_0 ? 0 : 2;
    }
    if (argc == 3 && std::wcscmp(argv[1], L"--release-dll-hold") == 0) {
        HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2]);
        if (!stop)
            return 2;
        const BOOL ok = SetEvent(stop);
        CloseHandle(stop);
        return ok ? 0 : 2;
    }
    if (argc == 2 && std::wcscmp(argv[1], L"--check-registry") == 0) {
        return readableRegistration(myswy::kClsidPath, L"InprocServer32", nullptr) &&
               readableRegistration(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MyswyIME",
                                    nullptr, L"InstallLocation") ? 0 : 1;
    }
    if (argc == 3 && std::wcscmp(argv[1], L"--uninstall-exe") == 0) {
        // NSIS requires _?= as the unquoted final command-line suffix. Standard
        // argv quoting would launch a temporary child and hide its exit code.
        wchar_t full[32768] {};
        const DWORD length = GetFullPathNameW(argv[2], 32768, full, nullptr);
        if (!length || length >= 32768)
            return 2;
        const std::filesystem::path exe(full);
        std::wstring command = L"\"" + exe.wstring() + L"\" /S _?=" + exe.parent_path().wstring();
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(full, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process))
            return 2;
        const DWORD wait = WaitForSingleObject(process.hProcess, 45000);
        DWORD code = 2;
        if (wait == WAIT_OBJECT_0)
            GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<int>(code);
    }
    if (argc == 4 && std::wcscmp(argv[1], L"--verify-files") == 0) {
        const bool ok = verifyPackage(argv[2], argv[3]);
        std::puts(ok ? "PASS: complete payload SHA-256 manifest." : "FAIL: payload manifest or file hash.");
        return ok ? 0 : 1;
    }
    if (argc != 2 && argc != 3) {
        std::fputs("Usage: myswy_probe.exe <absolute DLL path> [--edits|--registered]\n", stderr);
        return 2;
    }
    const bool editTests = argc == 3 && std::wcscmp(argv[2], L"--edits") == 0;
    const bool registered = argc == 3 && std::wcscmp(argv[2], L"--registered") == 0;
    if (argc == 3 && !editTests && !registered)
        return 2;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
        return 1;
    HMODULE module = LoadLibraryExW(argv[1], nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
        CoUninitialize();
        return 1;
    }
    using GetClass = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
    using Unload = HRESULT(WINAPI *)();
    auto getClass = myswy::procedureAddress<GetClass>(module, "DllGetClassObject");
    auto canUnload = myswy::procedureAddress<Unload>(module, "DllCanUnloadNow");
    if (!getClass || !canUnload || !GetProcAddress(module, "DllRegisterServer")
            || !GetProcAddress(module, "DllUnregisterServer"))
        return 1;
    check(canUnload(), S_OK, "initial unload");
    {
        myswy::Ptr<IClassFactory> factory;
        check(getClass(CLSID_NULL, IID_IClassFactory, reinterpret_cast<void **>(factory.put())),
              CLASS_E_CLASSNOTAVAILABLE, "unknown class");
        check(getClass(myswy::kService, IID_IClassFactory, nullptr), E_POINTER, "null output");
        check(getClass(myswy::kService, IID_IClassFactory, reinterpret_cast<void **>(factory.put())), S_OK,
              "factory");
        check(canUnload(), S_FALSE, "factory lifetime");
        myswy::Ptr<myswy::ProcessorEx> service;
        check(factory->CreateInstance(factory.get(), myswy::kProcessorEx, reinterpret_cast<void **>(service.put())),
              CLASS_E_NOAGGREGATION, "aggregation");
        check(factory->CreateInstance(nullptr, myswy::kProcessorEx, reinterpret_cast<void **>(service.put())), S_OK,
              "processor");
        myswy::Ptr<ITfKeyEventSink> keys;
        check(myswy::query(service.get(), IID_ITfKeyEventSink, keys), S_OK, "key sink");
        if (!myswy::same(service.get(), keys.get()))
            return 1;
        BOOL eaten = TRUE;
        check(keys->OnTestKeyDown(nullptr, 'N', 0, &eaten), S_OK, "inactive test");
        if (eaten)
            return 1;
        check(keys->OnKeyDown(nullptr, 'N', 0, &eaten), S_OK, "inactive key");
        if (eaten)
            return 1;
        check(service->Deactivate(), S_OK, "inactive deactivate");
        check(service->ActivateEx(nullptr, 1, 0), E_INVALIDARG, "invalid manager");
        myswy::Ptr<ITfThreadMgr> manager;
        check(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr,
                               reinterpret_cast<void **>(manager.put())), S_OK, "thread manager");
        TfClientId client = TF_CLIENTID_NULL;
        check(manager->Activate(&client), S_OK, "thread activation");
        // Application client IDs are not service IDs. The OS activates installed TIPs.
        if (editTests)
            runServiceTests(service.get(), keys.get());
        if (registered) {
            myswy::Ptr<ITfTextInputProcessor> installed;
            check(CoCreateInstance(myswy::kService, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfTextInputProcessor, reinterpret_cast<void **>(installed.put())), S_OK, "registered COM processor");
            myswy::Ptr<ITfInputProcessorProfiles> profiles;
            check(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles, reinterpret_cast<void **>(profiles.put())), S_OK, "installed profile manager");
            BSTR description = nullptr;
            HRESULT described = profiles->GetLanguageProfileDescription(myswy::kService, myswy::kLanguage,
                                myswy::kProfile, &description);
            if (described == E_NOTIMPL && GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) {
                // Wine 10 stores AddLanguageProfile's description but leaves the
                // getter unimplemented. Verify that exact stored value instead;
                // native Windows must still pass the public TSF getter above.
                wchar_t text[160] {};
                DWORD bytes = sizeof(text);
                const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE,
                                                    L"Software\\Microsoft\\CTF\\TIP\\{65C32A54-219A-4F0A-B44C-B963D7BA532F}"
                                                    L"\\LanguageProfile\\0x00000804\\{E4F616AA-8696-4378-9B9E-7498786A2940}",
                                                    L"Description", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                                                    nullptr, text, &bytes);
                SysFreeString(description);
                description = status == ERROR_SUCCESS ? SysAllocString(text) : nullptr;
                described = description ? S_OK : E_FAIL;
                std::puts("Wine: profile Description checked in registry; public TSF getter is unimplemented.");
            }
            check(described, S_OK, "installed profile description");
            const bool brand = description && std::wcscmp(description, myswy::kName) == 0;
            SysFreeString(description);
            check(brand ? S_OK : E_FAIL, S_OK, "installed Chengyin input method name");
        }
        check(manager->Deactivate(), S_OK, "thread deactivation");
        factory->LockServer(TRUE);
        factory->LockServer(FALSE);
    }
    CoFreeUnusedLibraries();
    check(canUnload(), S_OK, "final unload (all sinks detached)");
    FreeLibrary(module);
    CoUninitialize();
    std::puts("PASS: DLL exports, COM identity/lifetime, system TSF availability. No registration changed.");
    return 0;
}
