// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx-utils/eventdispatcher.h>
#include "config.h"
#include "dictionary_loader.h"
#include "profile_store.h"
#include "chengyin_ime.h"

namespace chengyin {
class State final : public fcitx::InputContextProperty {
public:
    explicit State(DictionaryPtr source)
        : dictionary(std::move(source)),
          session(chengyin_session_new_with_dictionary(dictionary->dictionary.get()), chengyin_session_free) {}
    // Declare before session: destruction releases the Rust session first.
    DictionaryPtr dictionary;
    std::unique_ptr<ChengyinSession, decltype(&chengyin_session_free)> session;
    uint64_t revision = 0;
    // The candidate width this session's core was actually configured with. The
    // candidate list is built at this width and the digit keys are bound to it,
    // so the panel and the core can never disagree about the page layout.
    // kUnconfiguredPageSize until the first successful configure(), which is the
    // width the core itself starts at.
    int pageSize = kUnconfiguredPageSize;
    // The Engine settingsRevision this session has applied. Zero means nothing
    // has been applied yet, so a brand-new session is configured on first use.
    uint64_t settingsRevision = 0;
    // The Engine profileRevision this session's core was told about. Zero means
    // none yet; a session that was busy when learning advanced keeps the older
    // snapshot and is retried once its composition ends.
    uint64_t profileRevision = 0;
};

class Engine final : public fcitx::InputMethodEngine {
public:
    enum class ReloadState { Ready, Loading, Failed };
    // defaultDictionaryPath is the lexicon a profile with no conf/chengyin.conf
    // starts from, and the value the option resets to. Distribution builds pass
    // their packaged lexicon here; it stays empty for plain source builds.
    //
    // profilePath is where learning is persisted. It is injectable because a test
    // must never touch the real profile under $XDG_DATA_HOME, and because a caller
    // that has nowhere to persist to passes an empty path -- learning is then
    // switched off entirely rather than kept in memory that dies with the process.
    explicit Engine(fcitx::InputContextManager &manager, fcitx::EventLoop &loop,
                    std::string defaultDictionaryPath = CHENGYIN_DEFAULT_DICTIONARY_PATH,
                    std::string profilePath = DefaultProfilePath(),
                    LearningRetryPolicy policy = {});
    ~Engine() override;
    void reloadConfig() override;
    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &config) override;
    ReloadState reloadState() const { return reloadState_; }
    const std::string &dictionaryError() const { return dictionaryError_; }
    const std::string &settingsError() const { return settingsError_; }
    const std::string &dictionaryPath() const { return dictionary_->path; }
    // The settings every idle session has been told to use. Exposed so a test can
    // assert what a settings save actually pushed down without reaching into a
    // session's private state.
    const EngineSettings &settings() const { return settings_; }
    // The master profile, exposed for the same reason settings() is: a test asserts
    // what learning actually did without reaching into a session's private state.
    const ChengyinProfile *profile() const { return master_.get(); }
    int32_t profileCount() const { return chengyin_profile_count(master_.get()); }
    int32_t profileRevision() const { return static_cast<int32_t>(profileRevision_); }
    // The store's warning and last error, never containing any spelling or text.
    const std::string &profileWarning() const { return profileWarning_; }
    std::string profileError() const;
    ProfileStoreStats profileStats() const;
    // Blocks until everything confirmed so far has reached the disk, bounded by
    // timeoutMs. Only a test needs this: the input path never waits for the store.
    bool flushLearning(uint64_t timeoutMs);
    void keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &event) override;
    void reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) override;
    void select(fcitx::InputContext *ic, size_t index, uint64_t revision);
private:
    int32_t process(fcitx::InputContext *ic, uint32_t key, uint32_t modifiers);
    void refresh(fcitx::InputContext *ic, bool limited = false);
    void clear(fcitx::InputContext *ic);
    void synchronize(State &state);
    // Push settings_ into one session. A composition in progress keeps the old
    // settings; the next call retries once it has finished.
    void applySettings(State &state);
    // Adopt the settings of the configuration just loaded. Returns true when the
    // effective settings changed, so the caller only wakes the contexts then.
    bool adoptSettings();
    // Bring every context up to date with the published dictionary and settings,
    // and repaint the focused ones that have a composition on screen.
    void synchronizeAll();
    void loadDictionary(std::string path, bool persist);
    void loaded(uint64_t request, DictionaryPtr dictionary, std::string error);
    // Push the master profile into one session. Idle only, so a composition in
    // progress is never disturbed; the next call retries once it has ended.
    void applyProfile(State &state);
    // Read the master profile again after a reload replaced or removed the file,
    // and hand the result to every context through applyProfile.
    void adoptReloadedProfile();
    // Record one host-confirmed selection in the master profile and queue it for
    // persistence. Returns true when the selection was learned in memory.
    bool learn(fcitx::InputContext *ic, State &state);
    // Learning is on only when the option says so and a master profile exists.
    bool learningEnabled() const;
    // Object name of the persistent Fcitx 5 policy: the package's own Fcitx data
    // directory followed by the file this feature owns. Empty when the framework
    // cannot name a user directory at all, which is the case under SKIP_FCITX_USER_PATH.
    static std::string DefaultProfilePath();
    fcitx::InputContextManager &manager_;
    EngineConfig config_;
    // Declared right after config_ because the constructor seeds it from config_
    // in the init list, and declaration order is initialization order.
    EngineSettings settings_;
    // Starts at one so a brand-new State, whose own settingsRevision is zero, is
    // configured on first use even when the settings never changed. It then only
    // advances when the snapshot really changes, so saving an unchanged
    // configuration does not needlessly discard a session's association list.
    uint64_t settingsRevision_ = 1;
    std::unique_ptr<ProfileStore> store_;
    DictionaryPtr dictionary_;
    ReloadState reloadState_ = ReloadState::Ready;
    std::string dictionaryError_;
    std::string settingsError_;
    uint64_t request_ = 0;
    bool persist_ = false;
    // The single in-memory profile every context's session is handed a snapshot of.
    // Learning is applied here first and only then pushed to sessions, so several
    // input contexts share one set of preferences instead of each learning alone.
    std::unique_ptr<ChengyinProfile, decltype(&chengyin_profile_free)> master_{nullptr, chengyin_profile_free};
    // Advanced on every recorded selection. A State records the revision its
    // session's core was told about, so an unchanged profile costs one integer
    // comparison per key rather than a set_profile call.
    uint64_t profileRevision_ = 1;
    // Empty until the store has been constructed. Constructed in the initializer
    // list so a damaged stored file is known before the first key arrives.
    std::string profileWarning_;
    fcitx::EventDispatcher dispatcher_;
    std::unique_ptr<DictionaryLoader> loader_;
    fcitx::FactoryFor<State> factory_;
};
} // namespace chengyin
