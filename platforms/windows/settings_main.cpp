// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.h"
#include "settings.h"
#include <cwchar>
namespace chengyin {
HINSTANCE module = nullptr;
LONG objects = 0;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    chengyin::module = instance;
    int result;
    {
        chengyin::Ptr<ITfThreadMgr> manager;
        chengyin::Ptr<ITfMessagePump> pump;
        chengyin::Ptr<ITfKeystrokeMgr> keys;
        TfClientId client = TF_CLIENTID_NULL;
        bool activated = SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfThreadMgr, reinterpret_cast<void **>(manager.put()))) && SUCCEEDED(manager->Activate(&client));
        if (activated) {
            chengyin::query(manager.get(), IID_ITfMessagePump, pump);
            chengyin::query(manager.get(), IID_ITfKeystrokeMgr, keys);
        }
        HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
        result = chengyin::runSettings(instance, show, pump.get(), keys.get(),
                                    std::wcscmp(command, L"--input-test") == 0 ? 5 : 0,
                                    std::wcscmp(command, L"--input-test") != 0);
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
