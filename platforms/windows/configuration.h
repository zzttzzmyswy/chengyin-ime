// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common.h"
#include "preferences.h"
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace chengyin {
inline constexpr wchar_t kConfigurationEpoch[] = L"Local\\ChengyinIME.ConfigurationGeneration";
struct ConfigurationUpdate {
    ModuleLifetime lifetime;
    Preferences preferences;
    bool preferencesValid = false;
    ChengyinDictionary *dictionary = nullptr;
    void (*releaseDictionary)(ChengyinDictionary *) = nullptr;
    ChengyinProfile *profile = nullptr;
    DWORD learningGeneration = 0;
    // Ordinary (non-destructive) learning revision. A newer revision replaces the
    // in-memory snapshot without invalidating anything the service already queued.
    DWORD learningRevision = 0;
    bool hasRevision = false;
    ~ConfigurationUpdate() {
        if (dictionary && releaseDictionary)
            releaseDictionary(dictionary);
        if (profile)
            chengyin_profile_free(profile);
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
    // generation change, invalidating anything this service has queued. `due`,
    // when given, is polled so work that no file or generation change announces is
    // still noticed: an unfulfilled custom vocabulary waiting out its retry
    // backoff is reloaded without any further edit to the file (review R09).
    ConfigurationWatcher(DWORD observed, std::function<Snapshot()> load,
                         std::function<void(Snapshot)> apply, const wchar_t *name = nullptr,
                         std::wstring preferencesPath = {}, std::wstring revisionName = {},
                         std::function<bool()> due = {});
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
    std::function<bool()> due_;
};
}
