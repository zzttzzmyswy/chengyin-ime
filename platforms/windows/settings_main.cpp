#include "common.h"
#include "settings.h"
#include <cwchar>
namespace myswy {
HINSTANCE module = nullptr;
LONG objects = 0;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    myswy::module = instance;
    int result;
    {
        myswy::Ptr<ITfThreadMgr> manager;
        myswy::Ptr<ITfMessagePump> pump;
        myswy::Ptr<ITfKeystrokeMgr> keys;
        TfClientId client = TF_CLIENTID_NULL;
        bool activated = SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfThreadMgr, reinterpret_cast<void **>(manager.put()))) && SUCCEEDED(manager->Activate(&client));
        if (activated) {
            myswy::query(manager.get(), IID_ITfMessagePump, pump);
            myswy::query(manager.get(), IID_ITfKeystrokeMgr, keys);
        }
        HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
        result = myswy::runSettings(instance, show, pump.get(), keys.get(),
                                    std::wcscmp(command, L"--input-test") == 0 ? 4 : 0);
        if (activated)
            manager->Deactivate();
        keys.reset();
        pump.reset();
        manager.reset();
        if (richEdit)
            FreeLibrary(richEdit);
    }
    CoUninitialize();
    return result;
}
