#pragma once
#include <cstdint>
#include <memory>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx-utils/eventdispatcher.h>
#include "config.h"
#include "dictionary_loader.h"
#include "myswy_ime.h"

namespace myswy {
class State final : public fcitx::InputContextProperty {
public:
    explicit State(DictionaryPtr source)
        : dictionary(std::move(source)),
          session(myswy_session_new_with_dictionary(dictionary->dictionary.get()), myswy_session_free) {}
    // Declare before session: destruction releases the Rust session first.
    DictionaryPtr dictionary;
    std::unique_ptr<MyswySession, decltype(&myswy_session_free)> session;
    uint64_t revision = 0;
};

class Engine final : public fcitx::InputMethodEngine {
public:
    enum class ReloadState { Ready, Loading, Failed };
    Engine(fcitx::InputContextManager &manager, fcitx::EventLoop &loop);
    ~Engine() override;
    void reloadConfig() override;
    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &config) override;
    ReloadState reloadState() const { return reloadState_; }
    const std::string &dictionaryError() const { return dictionaryError_; }
    const std::string &dictionaryPath() const { return dictionary_->path; }
    void keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &event) override;
    void reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) override;
    void select(fcitx::InputContext *ic, size_t index, uint64_t revision);
private:
    int32_t process(fcitx::InputContext *ic, uint32_t key, uint32_t modifiers);
    void refresh(fcitx::InputContext *ic, bool limited = false);
    void clear(fcitx::InputContext *ic);
    void synchronize(State &state);
    void loadDictionary(std::string path, bool persist);
    void loaded(uint64_t request, DictionaryPtr dictionary, std::string error);
    fcitx::InputContextManager &manager_;
    EngineConfig config_;
    DictionaryPtr dictionary_;
    ReloadState reloadState_ = ReloadState::Ready;
    std::string dictionaryError_;
    uint64_t request_ = 0;
    bool persist_ = false;
    fcitx::EventDispatcher dispatcher_;
    std::unique_ptr<DictionaryLoader> loader_;
    fcitx::FactoryFor<State> factory_;
};
} // namespace myswy
