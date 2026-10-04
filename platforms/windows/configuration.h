#pragma once
#include "common.h"
#include "preferences.h"
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace myswy {
inline constexpr wchar_t kConfigurationEpoch[] = L"Local\\MyswyIME.ConfigurationGeneration";
struct ConfigurationUpdate {
    ModuleLifetime lifetime;
    Preferences preferences;
    bool preferencesValid = false;
    MyswyDictionary *dictionary = nullptr;
    void (*releaseDictionary)(MyswyDictionary *) = nullptr;
    MyswyProfile *profile = nullptr;
    DWORD learningGeneration = 0;
    ~ConfigurationUpdate() {
        if (dictionary && releaseDictionary)
            releaseDictionary(dictionary);
        if (profile)
            myswy_profile_free(profile);
    }
};
// Shared notification plus a background preference-file stamp fallback. No
// file I/O on the input thread; delivery returns to the owning TSF apartment.
class ConfigurationWatcher {
  public:
    using Snapshot = std::shared_ptr<ConfigurationUpdate>;
    ConfigurationWatcher(DWORD observed, std::function<Snapshot()> load,
                         std::function<void(Snapshot)> apply, const wchar_t *name = kConfigurationEpoch,
                         std::wstring preferencesPath = {});
    ~ConfigurationWatcher();
    bool valid() const {
        return window_ && stop_ && worker_.joinable();
    }
  private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    ModuleLifetime lifetime_;
    LearningEpoch epoch_;
    HWND window_ = nullptr;
    HANDLE stop_ = nullptr;
    std::thread worker_;
    std::mutex mutex_;
    Snapshot pending_;
    std::function<void(Snapshot)> apply_;
};
}
