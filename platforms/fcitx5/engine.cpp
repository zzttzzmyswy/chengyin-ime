#include "engine.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#ifndef MYSWY_FCITX_VERSION
#error "MYSWY_FCITX_VERSION must be provided by CMake from Fcitx5Core_VERSION"
#endif

// Fcitx 5.1.13 introduced StandardPaths and deprecated StandardPath; the
// headers themselves export no version macro, so the numeric value comes from
// CMake. The two APIs are not interchangeable: standardpaths.h only exists from
// 5.1.13 on, and iniparser.h stopped pulling in standardpath.h at the same
// point, which is what made this file fail to compile on newer distributions.
#define MYSWY_FCITX_VERSION_AT_LEAST(major, minor, patch) \
    (MYSWY_FCITX_VERSION >= ((major) * 10000 + (minor) * 100 + (patch)))

#if MYSWY_FCITX_VERSION_AT_LEAST(5, 1, 13)
#include <fcitx-utils/standardpaths.h>
#else
#include <fcitx-utils/standardpath.h>
#endif

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterface.h>

namespace myswy {
namespace {
// An unwritten temporary next to conf/myswy.conf plus the final path it belongs
// at. Neither framework helper can be used to publish it: StandardPathTempFile's
// destructor (<=5.1.12) ignores fsync and rename failures, and StandardPaths::
// safeSave() (>=5.1.13) ignores rename failures. saveConfig() checks every step,
// so this only has to create the file and hand over the descriptor.
class UserTempFile {
public:
    UserTempFile() = default;
    ~UserTempFile() { discard(); }
    UserTempFile(const UserTempFile &) = delete;
    UserTempFile &operator=(const UserTempFile &) = delete;

    bool open(const char *pathOrig) {
#if MYSWY_FCITX_VERSION_AT_LEAST(5, 1, 13)
        // StandardPaths exposes no equivalent of the old openUserTemp().
        const auto directory = fcitx::StandardPaths::global().userDirectory(fcitx::StandardPathsType::PkgConfig);
        if (directory.empty()) { return false; }
        const auto target = directory / pathOrig;
        std::error_code createError;
        std::filesystem::create_directories(target.parent_path(), createError);
        path_ = target.string();
        tempPath_ = path_ + "_XXXXXX";
        std::vector<char> buffer(tempPath_.begin(), tempPath_.end());
        buffer.push_back('\0');
        fd_ = ::mkstemp(buffer.data());
        if (fd_ < 0) { return false; }
        tempPath_ = buffer.data(); // mkstemp replaced the X's in place
#else
        auto file = fcitx::StandardPath::global().openUserTemp(fcitx::StandardPath::Type::PkgConfig, pathOrig);
        if (!file.isValid()) { return false; }
        fd_ = file.release(); // take the descriptor so the framework cannot rename
        path_ = file.path();
        tempPath_ = file.tempPath();
#endif
        owned_ = true;
        return true;
    }

    int fd() const { return fd_; }
    const std::string &path() const { return path_; }
    const std::string &tempPath() const { return tempPath_; }

    // Hand the descriptor to the caller, which now closes it and replaces the
    // target itself; the temporary is no longer cleaned up here.
    int release() {
        owned_ = false;
        return std::exchange(fd_, -1);
    }

private:
    void discard() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        if (owned_) {
            ::unlink(tempPath_.c_str());
            owned_ = false;
        }
        path_.clear();
        tempPath_.clear();
    }

    int fd_ = -1;
    bool owned_ = false;
    std::string path_;
    std::string tempPath_;
};

bool saveConfig(const EngineConfig &configuration) {
    UserTempFile file;
    if (!file.open("conf/myswy.conf")) { return false; }
    try {
        fcitx::RawConfig raw;
        configuration.save(raw);
        const auto parent = std::filesystem::path(file.path()).parent_path();
        if (!fcitx::writeAsIni(raw, file.fd()) || ::fsync(file.fd()) != 0) { return false; }
        const int fd = file.release(); // disable the framework's unchecked rename
        if (::close(fd) != 0) { ::unlink(file.tempPath().c_str()); return false; }
        if (::rename(file.tempPath().c_str(), file.path().c_str()) != 0) {
            ::unlink(file.tempPath().c_str());
            return false;
        }
        const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0) { return false; }
        const bool synced = ::fsync(directory) == 0;
        const bool closed = ::close(directory) == 0;
        return synced && closed;
    } catch (...) { return false; }
}

DictionaryPtr demoDictionary() {
    auto dictionary = std::make_shared<DictionarySnapshot>(nullptr, "");
    dictionary->dictionary.reset(myswy_dictionary_new_demo());
    if (!dictionary->dictionary) { throw std::runtime_error("Cannot load Myswy demo dictionary"); }
    return dictionary;
}

std::string text(MyswySession *session, uint32_t field, size_t index = 0) {
    std::array<uint8_t, MYSWY_MAX_TEXT_BYTES + 1> buffer{};
    const auto required = myswy_session_text(session, field, index, buffer.data(), buffer.size());
    if (required <= 0 || static_cast<size_t>(required) > buffer.size()) { return {}; }
    return {reinterpret_cast<const char *>(buffer.data()), static_cast<size_t>(required - 1)};
}

bool sensitive(fcitx::InputContext *ic) {
    return bool(ic->capabilityFlags() & fcitx::CapabilityFlag::PasswordOrSensitive);
}

class Word final : public fcitx::CandidateWord {
public:
    Word(std::string value, Engine *engine, size_t index, uint64_t revision)
        : CandidateWord(fcitx::Text(std::move(value))), engine_(engine), index_(index), revision_(revision) {}
    void select(fcitx::InputContext *ic) const override { engine_->select(ic, index_, revision_); }
private:
    Engine *engine_;
    size_t index_;
    uint64_t revision_;
};

uint32_t mapKey(fcitx::KeySym sym) {
    switch (sym) {
    case FcitxKey_Tab: return MYSWY_KEY_TAB;
    case FcitxKey_space: return MYSWY_KEY_SPACE;
    case FcitxKey_BackSpace: return MYSWY_KEY_BACKSPACE;
    case FcitxKey_Escape: return MYSWY_KEY_ESCAPE;
    case FcitxKey_Return:
    case FcitxKey_KP_Enter: return MYSWY_KEY_ENTER;
    case FcitxKey_Up: return MYSWY_KEY_UP;
    case FcitxKey_Down: return MYSWY_KEY_DOWN;
    case FcitxKey_Left: return MYSWY_KEY_LEFT;
    case FcitxKey_Right: return MYSWY_KEY_RIGHT;
    case FcitxKey_Home: return MYSWY_KEY_HOME;
    case FcitxKey_End: return MYSWY_KEY_END;
    case FcitxKey_Delete: return MYSWY_KEY_DELETE;
    case FcitxKey_Page_Up:
    case FcitxKey_minus: return MYSWY_KEY_PAGE_UP;
    case FcitxKey_Page_Down:
    case FcitxKey_equal: return MYSWY_KEY_PAGE_DOWN;
    default: return fcitx::Key::keySymToUnicode(sym);
    }
}
} // namespace

Engine::Engine(fcitx::InputContextManager &manager, fcitx::EventLoop &loop)
    : manager_(manager), dictionary_(demoDictionary()),
      factory_([this](fcitx::InputContext &) { return new State(dictionary_); }) {
    if (myswy_ime_abi_version() != MYSWY_ABI_VERSION || !manager.registerProperty("myswyState", &factory_)) {
        throw std::runtime_error("Myswy IM ABI mismatch or duplicate property");
    }
    dispatcher_.attach(&loop);
    loader_ = std::make_unique<DictionaryLoader>([this](uint64_t request, DictionaryPtr dictionary, std::string error) {
        dispatcher_.schedule([this, request, dictionary = std::move(dictionary), error = std::move(error)]() mutable {
            loaded(request, std::move(dictionary), std::move(error));
        });
    });
}

Engine::~Engine() {
    loader_->stop(); // no more callbacks can be scheduled after the join
    dispatcher_.detach(); // queued callbacks are discarded when dispatcher is destroyed
}

void Engine::reloadConfig() {
    EngineConfig config;
    fcitx::readAsIni(config, "conf/myswy.conf");
    loadDictionary(*config.dictionaryPath, false);
}

void Engine::setConfig(const fcitx::RawConfig &config) {
    config_.load(config, true);
    loadDictionary(*config_.dictionaryPath, true);
}

void Engine::loadDictionary(std::string path, bool persist) {
    config_.dictionaryPath.setValue(path);
    reloadState_ = ReloadState::Loading;
    dictionaryError_.clear();
    persist_ = persist;
    loader_->request(++request_, std::move(path));
}

void Engine::loaded(uint64_t request, DictionaryPtr dictionary, std::string error) {
    if (request != request_) { loader_->retire(std::move(dictionary)); return; }
    if (dictionary) {
        // Retain old ownership on the worker before any idle session releases it.
        loader_->retire(std::move(dictionary_));
        dictionary_ = std::move(dictionary);
        reloadState_ = ReloadState::Ready;
        if (persist_ && !saveConfig(config_)) {
            error = "词典已启用，但配置保存失败；重启后可能恢复旧设置";
        }
    } else {
        reloadState_ = ReloadState::Failed;
        config_.dictionaryPath.setValue(dictionary_->path);
    }
    dictionaryError_ = std::move(error);
    if (!dictionaryError_.empty()) { FCITX_WARN() << "Myswy IM: " << dictionaryError_; }
    manager_.foreach([this](fcitx::InputContext *ic) {
        auto *state = ic->propertyFor(&factory_);
        synchronize(*state);
        // Do not touch an idle context's panel: it may belong to another IME.
        if (ic->hasFocus() && !text(state->session.get(), MYSWY_TEXT_PREEDIT).empty()) { refresh(ic); }
        return true;
    });
}

void Engine::synchronize(State &state) {
    if (state.dictionary == dictionary_) { return; }
    if (myswy_session_set_dictionary(state.session.get(), dictionary_->dictionary.get()) == 0) {
        state.dictionary = dictionary_;
        ++state.revision;
    }
}

void Engine::keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &event) {
    auto *ic = event.inputContext();
    if (sensitive(ic)) { clear(ic); return; }
    if (event.isRelease()) { return; }
    const auto states = event.key().states();
    uint32_t modifiers = 0;
    if (states & fcitx::KeyState::Ctrl) { modifiers |= MYSWY_MOD_CONTROL; }
    if (states & (fcitx::KeyStates(fcitx::KeyState::Alt) | fcitx::KeyState::Mod5)) { modifiers |= MYSWY_MOD_ALT; }
    if (states & (fcitx::KeyStates(fcitx::KeyState::Super) | fcitx::KeyState::Super2 | fcitx::KeyState::Meta |
                  fcitx::KeyState::Hyper | fcitx::KeyState::Hyper2)) { modifiers |= MYSWY_MOD_SUPER; }
    const auto result = process(ic, mapKey(event.key().sym()), modifiers);
    if (result >= 0 && (result & MYSWY_HANDLED)) { event.filterAndAccept(); }
}

int32_t Engine::process(fcitx::InputContext *ic, uint32_t key, uint32_t modifiers) {
    auto *state = ic->propertyFor(&factory_);
    synchronize(*state);
    const auto result = myswy_session_process(state->session.get(), key, modifiers);
    ++state->revision;
    if (result < 0) { clear(ic); return result; }
    const auto commit = text(state->session.get(), MYSWY_TEXT_COMMIT);
    // Commit is delivered even when punctuation is forwarded to the application.
    if (!commit.empty()) { ic->commitString(commit); }
    synchronize(*state); // only switches after an active composition has finished
    refresh(ic, (result & MYSWY_LIMITED) != 0);
    return result;
}

void Engine::refresh(fcitx::InputContext *ic, bool limited) {
    auto *state = ic->propertyFor(&factory_);
    auto *session = state->session.get();
    auto &panel = ic->inputPanel();
    panel.reset();
    const auto preedit = text(session, MYSWY_TEXT_PREEDIT);
    if (!preedit.empty()) {
        fcitx::Text display(preedit);
        display.setCursor(myswy_session_preedit_cursor(session)); // UTF-8 byte offset, including confirmed Chinese segments
        if (ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
            panel.setClientPreedit(display);
        } else {
            panel.setPreedit(display);
        }
    }
    const auto count = myswy_session_candidate_count(session);
    if (count > 0) {
        auto list = std::make_unique<fcitx::CommonCandidateList>();
        fcitx::KeyList keys;
        for (uint32_t i = 0; i < 9; ++i) { keys.emplace_back(static_cast<fcitx::KeySym>(FcitxKey_1 + i)); }
        if (myswy_session_is_association(session)<=0) {list->setSelectionKey(keys);}
        list->setPageSize(9);
        for (int32_t i = 0; i < count; ++i) {
            list->append<Word>(text(session, MYSWY_TEXT_CANDIDATE, static_cast<size_t>(i)),
                               this, static_cast<size_t>(i), state->revision);
        }
        list->setGlobalCursorIndex(myswy_session_selected(session));
        panel.setCandidateList(std::move(list));
    }
    if (myswy_session_is_association(session)>0) {panel.setAuxUp(fcitx::Text("联想 · Tab / 鼠标确认"));}
    if (limited || myswy_session_budget_limited(session)>0) { panel.setAuxDown(fcitx::Text("输入达到长度或歧义上限，请分段输入")); }
    else if (!preedit.empty() && !dictionaryError_.empty()) {
        panel.setAuxDown(fcitx::Text(dictionaryError_ + "；当前词典仍可使用"));
    } else if (!preedit.empty() && reloadState_ == ReloadState::Loading) {
        panel.setAuxDown(fcitx::Text("正在加载词典，继续使用当前词典"));
    }
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void Engine::clear(fcitx::InputContext *ic) {
    auto *state = ic->propertyFor(&factory_);
    myswy_session_reset(state->session.get());
    synchronize(*state);
    ++state->revision;
    refresh(ic);
}

void Engine::reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) { clear(event.inputContext()); }

void Engine::select(fcitx::InputContext *ic, size_t index, uint64_t revision) {
    if (sensitive(ic) || !ic->hasFocus()) { clear(ic); return; }
    auto *state = ic->propertyFor(&factory_);
    if (state->revision != revision || index >= 9) { return; }
    process(ic, MYSWY_KEY_SELECT_1 + static_cast<uint32_t>(index), 0);
}
} // namespace myswy
