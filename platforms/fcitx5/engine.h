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
};

class Engine final : public fcitx::InputMethodEngine {
public:
    enum class ReloadState { Ready, Loading, Failed };
    // defaultDictionaryPath is the lexicon a profile with no conf/chengyin.conf
    // starts from, and the value the option resets to. Distribution builds pass
    // their packaged lexicon here; it stays empty for plain source builds.
    explicit Engine(fcitx::InputContextManager &manager, fcitx::EventLoop &loop,
                    std::string defaultDictionaryPath = CHENGYIN_DEFAULT_DICTIONARY_PATH);
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
    DictionaryPtr dictionary_;
    ReloadState reloadState_ = ReloadState::Ready;
    std::string dictionaryError_;
    std::string settingsError_;
    uint64_t request_ = 0;
    bool persist_ = false;
    fcitx::EventDispatcher dispatcher_;
    std::unique_ptr<DictionaryLoader> loader_;
    fcitx::FactoryFor<State> factory_;
};
} // namespace chengyin
