// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx/action.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include "config.h"
#include "dictionary_loader.h"
#include "profile_store.h"
#include "punctuation.h"
#include "chengyin_ime.h"

namespace fcitx {
class UserInterfaceManager;
}

namespace chengyin {
// The tap-to-switch gesture: press one Shift key and release it with nothing in
// between. It is the same state machine the Windows adapter runs
// (platforms/windows/keymap.h ShiftSwitch), driven by key syms instead of virtual
// keys, so both platforms agree on what "单独轻按" means:
//
//  * a second key pressed while Shift is down, a Shift chord (Ctrl/Alt/Super) or
//    an auto-repeat cancels the gesture before the release arrives;
//  * the release itself only toggles when the key it belongs to is one the
//    setting covers and the gesture was still armed.
//
// The side is tracked so that tapping both Shift keys cannot count as one tap.
class ShiftTap final {
public:
    // Which key syms the setting covers. Fcitx frontends deliver the two Shift
    // keys as their own syms, so 左 Shift means the left key literally. A frontend
    // that reported only a side-less Shift sym would never satisfy 左 Shift: a
    // guess about which key the user pressed is worse than not switching.
    static bool matches(fcitx::KeySym sym, ShiftSwitch setting) {
        switch (setting) {
        case ShiftSwitch::Disabled: return false;
        case ShiftSwitch::Left: return sym == FcitxKey_Shift_L;
        case ShiftSwitch::Both: break;
        }
        return sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R;
    }

    bool pending() const { return down_; }

    void down(fcitx::KeySym sym, ShiftSwitch setting, bool shortcut, bool repeat) {
        if (matches(sym, setting)) {
            const int side = sym == FcitxKey_Shift_R ? 2 : 1;
            if (!down_) {
                down_ = true;
                side_ = side;
                used_ = shortcut || repeat;
            } else {
                used_ = used_ || side != side_ || shortcut || repeat;
            }
        } else if (down_) {
            // Any other key turning up while Shift is held makes the release that
            // follows a chord, not a tap.
            used_ = true;
        }
    }

    // Returns whether this release completes a tap. Resets either way: a cancelled
    // gesture must not stay armed for the next press.
    bool up(fcitx::KeySym sym, ShiftSwitch setting, bool shortcut) {
        if (!matches(sym, setting)) { return false; }
        const bool toggle = down_ && !used_ && !shortcut;
        reset();
        return toggle;
    }

    void reset() {
        down_ = false;
        used_ = false;
        side_ = 0;
    }

private:
    bool down_ = false, used_ = false;
    int side_ = 0;
};

class State final : public fcitx::InputContextProperty {
public:
    // startEnglish and startPunctuation are the configuration's own defaults, read
    // once when the context is created. Neither is "the current setting pushed
    // down": DefaultEnglish only decides where a NEW context starts, and the
    // punctuation switch is the per-context value the status-area action flips,
    // which a later settings save re-seeds (see Engine::applySettings).
    State(DictionaryPtr source, bool startEnglish, bool startPunctuation)
        : dictionary(std::move(source)),
          session(chengyin_session_new_with_dictionary(dictionary->dictionary.get()), chengyin_session_free) {
        english = startEnglish;
        punctuation = startPunctuation;
    }
    // Declare before session: destruction releases the Rust session first.
    DictionaryPtr dictionary;
    std::unique_ptr<ChengyinSession, decltype(&chengyin_session_free)> session;
    uint64_t revision = 0;
    // 中/英 mode of this context. Unlike every other switch this one is not
    // re-pushed from the configuration: the Windows settings text says changing
    // the default "不会改变当前的中英文状态", and the same holds here, so only a
    // new context, the Shift tap or the status-area action moves it.
    bool english = false;
    // The 中文标点 switch as this context currently has it. Starts at the
    // configured value; the status-area action flips it for this context alone.
    bool punctuation = true;
    // Which half of the paired quotes comes next, and the tap-to-switch gesture
    // in progress. Both are transient: they are cleared whenever the composition
    // is retired the way the Windows adapter's unbind() retires it.
    PunctuationState punctuationState;
    ShiftTap shift;
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
    //
    // uiManager is where the two status-area actions are registered. It stays
    // optional so a test can build an Engine without a framework Instance; the
    // actions are then still created and driven, only not published to a UI.
    explicit Engine(fcitx::InputContextManager &manager, fcitx::EventLoop &loop,
                    std::string defaultDictionaryPath = CHENGYIN_DEFAULT_DICTIONARY_PATH,
                    std::string profilePath = DefaultProfilePath(),
                    LearningRetryPolicy policy = {}, fcitx::UserInterfaceManager *uiManager = nullptr);
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
    // The two status-area actions, exposed so a test can read back the text and
    // icon the input status area would render, and activate them the way a click
    // does, without needing a user interface addon.
    fcitx::SimpleAction &modeAction() { return modeAction_; }
    fcitx::SimpleAction &punctuationAction() { return punctuationAction_; }
    // The 中/英 mode and 中文标点 switch of one context, exposed for the same
    // reason settings() is: a test asserts what a key event did to them.
    bool english(fcitx::InputContext *ic);
    bool chinesePunctuation(fcitx::InputContext *ic);
    // Clicking the status-area actions. Public because a click reaches them
    // through the framework rather than through a key event.
    void toggleEnglish(fcitx::InputContext *ic);
    void togglePunctuation(fcitx::InputContext *ic);
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
    // Where learning is persisted: the package's own Fcitx data directory followed
    // by the file this feature owns. Empty when the framework cannot name a user
    // directory at all, which is the case under SKIP_FCITX_USER_PATH. Public
    // because the addon factory passes it to the constructor explicitly.
    static std::string DefaultProfilePath();
    void select(fcitx::InputContext *ic, size_t index, uint64_t revision);
    // Instance::activateInputMethod() clears StatusGroup::InputMethod right before
    // this call, so re-adding the two actions here is what keeps them in the status
    // area for the input method that is now active.
    void activate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) override;
private:
    // Publish both actions to the framework and wire their click handlers. A no-op
    // when the engine has no UserInterfaceManager.
    void registerActions();
    // What one key produced. `result` is the core's own CHENGYIN_HANDLED / LIMITED
    // word; `converted` says whether the Chinese punctuation was actually appended,
    // which is one of the two reasons the caller consumes the key.
    struct Outcome {
        int32_t result = 0;
        bool converted = false;
    };
    // Runs one key through the core and delivers whatever it committed. A non-zero
    // `punctuation` is a character from the shared table to render and append to
    // that commit, so a candidate and the Chinese punctuation that ended it reach
    // the application as one write instead of racing the host's own key.
    Outcome process(fcitx::InputContext *ic, uint32_t key, uint32_t modifiers, char punctuation = 0);
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
    // The transient state the Windows adapter's unbind() clears besides the
    // composition itself: the half-finished quote pair and the tap in progress.
    void retire(State &state);
    // The core's modifier word for one key event.
    static uint32_t modifiersOf(fcitx::KeyStates states);
    // The 中/英 key path. A release of a covered Shift key completes the tap and
    // switches the mode; every other release is ignored. Returns true when the
    // event was handled and must not reach the application.
    bool shiftEvent(fcitx::InputContext *ic, fcitx::KeyEvent &event);
    // Whether the core is showing the user something right now: a composition, or
    // the association list a commit produced. This is the Windows adapter's
    // `active(context)`, and it decides whether punctuation is inserted on its own
    // or handed to the core.
    static bool active(State &state);
    // Retire the composition without touching the 中/英 mode, the punctuation
    // switch or the quote pair. The standalone punctuation path uses this rather
    // than clear(), because the quote it just inserted is the first half of a pair
    // whose second half has to follow.
    void resetSession(fcitx::InputContext *ic);
    // Republish both actions for one context, so the input status area shows the
    // current mode and punctuation switch. Called wherever either can change.
    void updateActions(fcitx::InputContext *ic, State &state);
    fcitx::InputContextManager &manager_;
    // Where the two actions are registered. Null in a plain source build's test,
    // where no framework Instance exists; the actions still exist and still work,
    // they are just not published to a UI.
    fcitx::UserInterfaceManager *uiManager_ = nullptr;
    // The status-area actions. They are engine-wide objects, not per-context: the
    // text and icon they carry describe the CURRENT context, which is the one the
    // status area is asking about, and the click handler reads that context back
    // out of the signal.
    fcitx::SimpleAction modeAction_;
    fcitx::SimpleAction punctuationAction_;
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
