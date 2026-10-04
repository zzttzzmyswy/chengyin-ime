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
// The worker polls one shared integer; files are read only after a successful
// configuration publish. The input thread receives the latest owned snapshot.
class ConfigurationWatcher {
  public:
    using Snapshot = std::shared_ptr<ConfigurationUpdate>;
    ConfigurationWatcher(DWORD observed, std::function<Snapshot()> load,
                         std::function<void(Snapshot)> apply, const wchar_t *name = kConfigurationEpoch);
    ~ConfigurationWatcher();
    bool valid() const {
        return window_ && stop_ && epoch_.valid() && worker_.joinable();
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
