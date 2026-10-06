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
    // Ordinary (non-destructive) learning revision. A newer revision replaces the
    // in-memory snapshot without invalidating anything the service already queued.
    DWORD learningRevision = 0;
    bool hasRevision = false;
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
    // `observed` is the configuration generation. `revision` optionally adds the
    // profile's ordinary learning revision, so a plain selection made by another
    // application reloads this service's snapshot without, unlike a destructive
    // generation change, invalidating anything this service has queued.
    ConfigurationWatcher(DWORD observed, std::function<Snapshot()> load,
                         std::function<void(Snapshot)> apply, const wchar_t *name = nullptr,
                         std::wstring preferencesPath = {}, std::wstring revisionName = {});
    ~ConfigurationWatcher();
    bool valid() const {
        return window_ && stop_ && worker_.joinable();
    }
  private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    ModuleLifetime lifetime_;
    LearningEpoch epoch_;
    LearningEpoch revision_;
    HWND window_ = nullptr;
    HANDLE stop_ = nullptr;
    std::thread worker_;
    std::mutex mutex_;
    Snapshot pending_;
    std::function<void(Snapshot)> apply_;
};
}
