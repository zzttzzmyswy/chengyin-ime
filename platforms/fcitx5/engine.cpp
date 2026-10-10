// SPDX-License-Identifier: GPL-3.0-or-later
// The version gate must come before engine.h: it pulls in config.h ->
// fcitx-config/configuration.h, whose own headers need the Utils stack (flags.h
// among them) to have been seen already, so engine.h is included last.
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

#ifndef CHENGYIN_FCITX_VERSION
#error "CHENGYIN_FCITX_VERSION must be provided by CMake from Fcitx5Core_VERSION"
#endif

// Fcitx 5.1.13 introduced StandardPaths and deprecated StandardPath; the
// headers themselves export no version macro, so the numeric value comes from
// CMake. The two APIs are not interchangeable: standardpaths.h only exists from
// 5.1.13 on, and iniparser.h stopped pulling in standardpath.h at the same
// point, which is what made this file fail to compile on newer distributions.
#define CHENGYIN_FCITX_VERSION_AT_LEAST(major, minor, patch) \
    (CHENGYIN_FCITX_VERSION >= ((major) * 10000 + (minor) * 100 + (patch)))

#if CHENGYIN_FCITX_VERSION_AT_LEAST(5, 1, 13)
#include <fcitx-utils/standardpaths.h>
#else
#include <fcitx-utils/standardpath.h>
#endif

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>

#include "engine.h"

namespace chengyin {
// The file this feature owns, relative to the package's own Fcitx data
// directory. A learner's selections are the user's own text, they never leave
// the machine, and the path is built from StandardPaths rather than from a
// hand-rolled XDG lookup.
const char *const kProfileFileName = "chengyin/profile.bin";

namespace {
// An unwritten temporary next to conf/chengyin.conf plus the final path it belongs
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
#if CHENGYIN_FCITX_VERSION_AT_LEAST(5, 1, 13)
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
    if (!file.open("conf/chengyin.conf")) { return false; }
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
    dictionary->dictionary.reset(chengyin_dictionary_new_demo());
    if (!dictionary->dictionary) { throw std::runtime_error("Cannot load Chengyin demo dictionary"); }
    return dictionary;
}

std::string text(ChengyinSession *session, uint32_t field, size_t index = 0) {
    std::array<uint8_t, CHENGYIN_MAX_TEXT_BYTES + 1> buffer{};
    const auto required = chengyin_session_text(session, field, index, buffer.data(), buffer.size());
    if (required <= 0 || static_cast<size_t>(required) > buffer.size()) { return {}; }
    return {reinterpret_cast<const char *>(buffer.data()), static_cast<size_t>(required - 1)};
}

// The status-area action names, unique across the whole framework because
// UserInterfaceManager::registerAction() keys on them.
const char *const kModeActionName = "chengyin-mode";
const char *const kPunctuationActionName = "chengyin-punctuation";

bool sensitive(fcitx::InputContext *ic) {
    return bool(ic->capabilityFlags() & fcitx::CapabilityFlag::PasswordOrSensitive);
}

// The codepoints a rendered punctuation key produces, as UTF-8.
std::string renderPunctuation(const PunctuationState &state, char c, bool chinese) {
    char32_t codepoints[3] = {};
    const int length = state.render(c, chinese, codepoints);
    std::string output;
    for (int i = 0; i < length; ++i) { output += fcitx::utf8::UCS4ToUTF8(codepoints[i]); }
    return output;
}

// The ASCII punctuation key this event carries, or 0 when it is not one at all.
// Only the symbols the shared table covers are reported, so a letter, a control
// key or an unsupported symbol can never be turned into Chinese punctuation.
//
// Fcitx has already normalised the key by the time an engine sees it: a shifted
// symbol such as Shift+';' arrives as the colon keysym with no Shift state bit.
// The table is therefore keyed on the character the key actually types, which is
// exactly what mapKey() hands the core.
char punctuationKey(const fcitx::Key &key) {
    const auto unicode = fcitx::Key::keySymToUnicode(key.sym());
    if (unicode == 0 || unicode > 0x7f) { return 0; }
    const auto c = static_cast<char>(unicode);
    return PunctuationState::supported(c) ? c : 0;
}

// Whether the event is a shortcut chord. Shift is deliberately not part of this:
// a shifted symbol is a punctuation key in its own right on Fcitx, and the
// Windows adapter draws the same line (its `shortcut` ignores the Shift bit).
bool shortcutChord(fcitx::KeyStates states) {
    return states.testAny(fcitx::KeyStates({fcitx::KeyState::Ctrl, fcitx::KeyState::Alt,
                                            fcitx::KeyState::Super, fcitx::KeyState::Super2,
                                            fcitx::KeyState::Hyper, fcitx::KeyState::Hyper2,
                                            fcitx::KeyState::Meta, fcitx::KeyState::Mod5}));
}

// Whether Caps Lock is on, and whether this press is an auto-repeat. Both are
// read from the RAW key: Key::normalize() keeps only the modifier bits it treats
// as part of the key identity and drops everything else, so a CapsLock-on press
// and a repeated press are indistinguishable from plain ones by the time
// event.key() is read. Windows refuses to convert punctuation under Caps Lock and
// never arms the Shift gesture on a repeat (platforms/windows/service.cpp:235 and
// keymap.h ShiftSwitch::down), so the raw key is what has to be consulted.
bool capsLock(const fcitx::KeyEvent &event) {
    return event.rawKey().states().test(fcitx::KeyState::CapsLock) ||
           event.origKey().states().test(fcitx::KeyState::CapsLock);
}

bool repeated(const fcitx::KeyEvent &event) {
    return event.rawKey().states().test(fcitx::KeyState::Repeat) ||
           event.origKey().states().test(fcitx::KeyState::Repeat);
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
    case FcitxKey_Tab: return CHENGYIN_KEY_TAB;
    case FcitxKey_space: return CHENGYIN_KEY_SPACE;
    case FcitxKey_BackSpace: return CHENGYIN_KEY_BACKSPACE;
    case FcitxKey_Escape: return CHENGYIN_KEY_ESCAPE;
    case FcitxKey_Return:
    case FcitxKey_KP_Enter: return CHENGYIN_KEY_ENTER;
    case FcitxKey_Up: return CHENGYIN_KEY_UP;
    case FcitxKey_Down: return CHENGYIN_KEY_DOWN;
    case FcitxKey_Left: return CHENGYIN_KEY_LEFT;
    case FcitxKey_Right: return CHENGYIN_KEY_RIGHT;
    case FcitxKey_Home: return CHENGYIN_KEY_HOME;
    case FcitxKey_End: return CHENGYIN_KEY_END;
    case FcitxKey_Delete: return CHENGYIN_KEY_DELETE;
    case FcitxKey_Page_Up:
    case FcitxKey_minus: return CHENGYIN_KEY_PAGE_UP;
    case FcitxKey_Page_Down:
    case FcitxKey_equal: return CHENGYIN_KEY_PAGE_DOWN;
    default: return fcitx::Key::keySymToUnicode(sym);
    }
}
} // namespace

std::string Engine::DefaultProfilePath() {
    // StandardPaths rather than reading XDG_DATA_HOME by hand, so the file lands
    // beside everything else this package owns and honours the same overrides. An
    // empty answer means the framework has no user data directory at all (which is
    // what SKIP_FCITX_USER_PATH produces): persistence is then simply unavailable
    // and no path is invented.
#if CHENGYIN_FCITX_VERSION_AT_LEAST(5, 1, 13)
    const auto directory = fcitx::StandardPaths::global().userDirectory(fcitx::StandardPathsType::PkgData);
    if (directory.empty()) { return {}; }
    return (directory / kProfileFileName).string();
#else
    const auto directory = fcitx::StandardPath::global().userDirectory(fcitx::StandardPath::Type::PkgData);
    if (directory.empty()) { return {}; }
    return directory + "/" + kProfileFileName;
#endif
}

Engine::Engine(fcitx::InputContextManager &manager, fcitx::EventLoop &loop, std::string defaultDictionaryPath,
               std::string profilePath, LearningRetryPolicy policy, fcitx::UserInterfaceManager *uiManager)
    : manager_(manager), uiManager_(uiManager), config_(std::move(defaultDictionaryPath)),
      settings_(settingsOf(config_)),
      store_(std::make_unique<ProfileStore>(std::move(profilePath), policy)),
      dictionary_(demoDictionary()),
      factory_([this](fcitx::InputContext &) {
          return new State(dictionary_, settings_.defaultEnglish, settings_.chinesePunctuation);
      }) {
    if (chengyin_ime_abi_version() != CHENGYIN_ABI_VERSION || !manager.registerProperty("chengyinState", &factory_)) {
        throw std::runtime_error("Chengyin IM ABI mismatch or duplicate property");
    }
    registerActions();
    // The master profile is built from exactly the bytes the store adopted: the
    // file's own content, or empty for an absent or unusable one. An empty profile
    // is still a working profile, so a damaged file leaves learning enabled in
    // memory -- the store refuses to write, which is what keeps the damaged file
    // itself intact, and the warning below says so once.
    const auto &initial = store_->initialBytes();
    master_.reset(chengyin_profile_new(initial.empty() ? nullptr : initial.data(), initial.size()));
    if (!master_ || !store_->available()) {
        // Nothing can be learned without a profile and a place to keep it, so the
        // switch is forced off rather than left claiming behaviour this run cannot
        // deliver. The user still sees the option for what it is; what changes is
        // only that this process does not pretend to honour it.
        config_.learning.setValue(false);
        settings_ = settingsOf(config_);
        profileWarning_ = store_->available() ? "学习档案无法载入，本次运行关闭学习"
                                              : "没有可写入的学习档案位置，本次运行关闭学习";
    } else if (!store_->warning().empty()) {
        profileWarning_ = store_->warning();
        FCITX_WARN() << "Chengyin IM: " << profileWarning_;
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
    // Seed with this build's packaged lexicon, so a profile that never saved a
    // configuration keeps it instead of resolving to the empty (demo) default.
    EngineConfig config(config_.dictionaryPath.defaultValue());
    fcitx::readAsIni(config, "conf/chengyin.conf");
    // fcitx5-remote -r and a hand-edited conf/chengyin.conf both arrive here, so
    // the file's own settings are adopted before the (possibly new) lexicon is
    // requested. A file written before these options existed leaves them at this
    // build's defaults, which is what makes an old profile keep working.
    config_.pageSize.setValue(*config.pageSize);
    config_.associations.setValue(*config.associations);
    config_.learning.setValue(*config.learning);
    config_.defaultEnglish.setValue(*config.defaultEnglish);
    config_.shiftSwitch.setValue(*config.shiftSwitch);
    config_.chinesePunctuation.setValue(*config.chinesePunctuation);
    config_.fuzzy.setValue(*config.fuzzy);
    config_.correction.setValue(*config.correction);
    // Every path that re-reads settings also re-reads the profile: this is the one
    // entry point for `fcitx5-remote -r`, which is how a user clears learning (by
    // deleting the file) or imports one (by replacing it), even though the config
    // tool never displays either action.
    adoptReloadedProfile();
    if (adoptSettings()) { synchronizeAll(); }
    loadDictionary(*config.dictionaryPath, false);
}

void Engine::setConfig(const fcitx::RawConfig &config) {
    config_.load(config, true);
    const bool settingsChanged = adoptSettings();
    // Only a save that changed a non-dictionary option *and* left DictionaryPath
    // alone can skip the reload. Anything else — a new path, or a save that
    // differs in no option at all — keeps the long-standing behaviour of
    // re-reading the lexicon. That last case is what a repeated Apply and a
    // hand-edited file followed by `fcitx5-remote -r` look like, and re-reading
    // the same path is how a user replaces a TSV in place.
    if (settingsChanged && *config_.dictionaryPath == dictionary_->path) {
        // Nothing but the settings moved, so the shipped lexicon is left alone:
        // rebuilding the 184,173-entry vocabulary for a candidate-width change
        // would cost about 0.4 s and buy nothing.
        if (saveConfig(config_)) {
            settingsError_.clear();
        } else {
            // The running engine already uses the new settings; what is missing is
            // only restart durability, which the user has to be told about.
            settingsError_ = "设置已应用，但配置保存失败；重启后可能恢复旧设置";
        }
        synchronizeAll();
        return;
    }
    loadDictionary(*config_.dictionaryPath, true);
}

bool Engine::adoptSettings() {
    const auto next = settingsOf(config_);
    if (next == settings_) { return false; }
    settings_ = next;
    ++settingsRevision_;
    return true;
}

void Engine::applySettings(State &state) {
    if (state.settingsRevision == settingsRevision_) { return; }
    auto *session = state.session.get();
    // The core only accepts a page width and matching rules on an idle session.
    // A busy one keeps its old settings and is retried on the next key, so a
    // settings save can never disturb an active composition or drop a keystroke.
    // Learning is on only when the option says so AND there is somewhere to
    // persist to. A run that cannot write must not learn into memory that dies
    // with the process: the user would see the ordering change and then silently
    // lose it on restart. This is also what makes the learning bit part of the
    // compared settings snapshot below.
    const uint32_t learning = (learningEnabled() ? 1u : 0u) | (settings_.associations ? 2u : 0u);
    if (chengyin_session_configure(session, static_cast<uint32_t>(settings_.pageSize), learning) != 0) { return; }
    if (chengyin_session_configure_matching(session, settings_.matching) != 0) { return; }
    state.settingsRevision = settingsRevision_;
    // The panel is built at the width the core now pages at, so the two cannot
    // disagree about which row a digit selects.
    state.pageSize = settings_.pageSize;
    // The 中文标点 switch is re-seeded from the option on every settings change.
    // The status-area action is an override that lives until the next save, which
    // is what makes the option authoritative when the user actually presses Apply.
    // The 中/英 mode is deliberately NOT re-seeded: the Windows settings text says
    // changing the default mode "不会改变当前的中英文状态", and this matches.
    state.punctuation = settings_.chinesePunctuation;
    ++state.revision;
}

void Engine::synchronizeAll() {
    manager_.foreach([this](fcitx::InputContext *ic) {
        auto *state = ic->propertyFor(&factory_);
        synchronize(*state);
        applySettings(*state);
        applyProfile(*state);
        // Do not touch an idle context's panel: it may belong to another IME.
        if (ic->hasFocus() && !text(state->session.get(), CHENGYIN_TEXT_PREEDIT).empty()) { refresh(ic); }
        return true;
    });
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
    if (!dictionaryError_.empty()) { FCITX_WARN() << "Chengyin IM: " << dictionaryError_; }
    synchronizeAll();
}

void Engine::synchronize(State &state) {
    if (state.dictionary == dictionary_) { return; }
    if (chengyin_session_set_dictionary(state.session.get(), dictionary_->dictionary.get()) == 0) {
        state.dictionary = dictionary_;
        ++state.revision;
    }
}

// Learning is on when the option says so, there is a master profile to record
// into, and there is somewhere for the result to live. A run with nowhere to
// persist cannot learn: the user would watch the ordering change and then lose
// it at restart, which is worse than not changing it. StandardPaths answers with
// an empty user directory under SKIP_FCITX_USER_PATH, so a framework that has
// been told not to touch $HOME is exactly the case this covers.
bool Engine::learningEnabled() const {
    return settings_.learning && master_ != nullptr && store_ && store_->available();
}

void Engine::applyProfile(State &state) {
    if (state.profileRevision == profileRevision_) { return; }
    // Idle only: set_profile refuses a session mid-composition and leaves it
    // completely unchanged, so a session that was busy when learning advanced
    // keeps its older snapshot and is retried on the next key. A session is never
    // left half-updated, and an active composition is never disturbed -- the same
    // rule applySettings() follows for the settings snapshot.
    //
    // The core's set_profile clears the composition and, with it, any association
    // list the previous commit produced. Re-pushing the snapshot on every commit
    // would therefore erase the continuation list the user is looking at, so the
    // snapshot is published only when the session is showing nothing at all. The
    // association list is a post-commit state whose preedit is empty but whose
    // results are not, which is exactly the case `is_association` reports.
    if (chengyin_session_is_association(state.session.get()) > 0) { return; }
    if (chengyin_session_set_profile(state.session.get(), master_.get()) != 0) { return; }
    state.profileRevision = profileRevision_;
    ++state.revision;
}

void Engine::adoptReloadedProfile() {
    if (!store_) { return; }
    std::vector<uint8_t> adopted;
    // reload() drops whatever was still queued as well, so a selection confirmed
    // before the user deleted or replaced the file can never be replayed onto the
    // new content. The state the call returns is therefore the whole of what the
    // engine now knows about learning.
    store_->reload(adopted);
    auto replacement = std::unique_ptr<ChengyinProfile, decltype(&chengyin_profile_free)>(
        chengyin_profile_new(adopted.empty() ? nullptr : adopted.data(), adopted.size()),
        chengyin_profile_free);
    if (!replacement) {
        // No profile can be built from what is on disk, so there is nothing to
        // learn into either. The store has already stopped writing, which is what
        // leaves the file it could not read untouched.
        profileWarning_ = "学习档案无法载入，本次运行关闭学习";
        FCITX_WARN() << "Chengyin IM: " << profileWarning_;
        config_.learning.setValue(false);
        settings_ = settingsOf(config_);
        ++settingsRevision_;
        return;
    }
    master_ = std::move(replacement);
    // A new revision is what makes every idle session pick the snapshot up; a
    // session that is composing right now keeps its own and is retried later.
    // Deleting the file therefore returns every session to an unlearned ordering,
    // and restoring it adopts the imported habits -- both without any GUI action.
    ++profileRevision_;
    profileWarning_ = store_->warning();
    if (!profileWarning_.empty()) { FCITX_WARN() << "Chengyin IM: " << profileWarning_; }
}

bool Engine::learn(fcitx::InputContext *ic, State &state) {
    // The keyEvent entry already forwards every key out of a sensitive context,
    // so this is a second, independent guard rather than the only one: learning
    // must never be trained on a password or a private field even if a future
    // caller reaches process() through another path.
    if (!learningEnabled() || sensitive(ic)) { return false; }
    // The two texts are read BEFORE learn_commit, which clears the learning key
    // and is documented as the single call that consumes the pair.
    uint8_t key[CHENGYIN_MAX_INPUT_BYTES + 1] = {};
    uint8_t committed[CHENGYIN_MAX_TEXT_BYTES + 1] = {};
    const int keySize = chengyin_session_text(state.session.get(), CHENGYIN_TEXT_LEARNING_KEY, 0, key, sizeof(key));
    const int textSize =
        chengyin_session_text(state.session.get(), CHENGYIN_TEXT_COMMIT, 0, committed, sizeof(committed));
    // Only a positive return means the core recorded a selection; zero is the
    // ordinary "this commit trained nothing" answer.
    if (chengyin_session_learn_commit(state.session.get()) <= 0) { return false; }
    const auto keyLength = static_cast<size_t>(keySize - 1);
    const auto textLength = static_cast<size_t>(textSize - 1);
    // Where the new master comes from depends on whether this session's baseline is
    // still the master's own revision, and the two cases are not interchangeable.
    //
    // The session has already applied the selection to its OWN copy of the snapshot,
    // so when that copy is current it is taken as the master rather than recording
    // the pair a second time here. Recording into both would count every selection
    // twice -- and the core measures hit rate over trials, so a doubled count
    // promotes a candidate in half the selections Windows needs. This is the same
    // handover the Windows adapter performs (platforms/windows/service.cpp), and it
    // is also what carries the phrase-level pair a multi-segment composition learns
    // alongside the row.
    //
    // The snapshot is immutable and shared, so taking it is a refcount bump, not a
    // copy: sessions already holding the previous one are unaffected until they are
    // handed the new one.
    //
    // A session whose baseline is BEHIND the master must not donate its snapshot,
    // because that snapshot was built on the older revision and would silently drop
    // every pair learned since -- pairs other contexts are already using. That
    // happens whenever a context stays busy across another context's commit:
    // applyProfile only publishes to a session that is showing nothing. Such a
    // session's own copy is stale, so the pair is recorded into the master instead.
    // There is still no double count: the stale copy never becomes the master, so
    // the selection is written down exactly once, in the master.
    const bool baselineCurrent = state.profileRevision == profileRevision_;
    std::unique_ptr<ChengyinProfile, decltype(&chengyin_profile_free)> snapshot(
        baselineCurrent ? chengyin_session_profile(state.session.get()) : nullptr,
        chengyin_profile_free);
    if (snapshot) {
        master_ = std::move(snapshot);
    } else if (chengyin_profile_record_selection(master_.get(), key, keyLength, committed, textLength,
                                                 settings_.matching) != 0) {
        // The fallback also covers a snapshot that simply could not be taken; it is
        // the same pair either way.
        //
        // ChengyinProfile::record_selection is copy-on-write: master_'s Profile is
        // shared by reference with every session that was handed it, so the ABI
        // clones it here before mutating. The sessions holding the previous snapshot
        // keep the old content until applyProfile publishes the new one, which is
        // exactly the immutability the sessions rely on.
        return false;
    }
    ++profileRevision_;
    // Queueing is best-effort: a full queue or an unusable store drops the event
    // rather than delaying the keystroke. The selection is already in memory.
    store_->enqueue(key, keyLength, committed, textLength, settings_.matching);
    return true;
}

std::string Engine::profileError() const { return store_ ? store_->lastError() : std::string(); }

ProfileStoreStats Engine::profileStats() const { return store_ ? store_->stats() : ProfileStoreStats{}; }

bool Engine::flushLearning(uint64_t timeoutMs) { return store_ ? store_->flush(timeoutMs) : true; }

void Engine::registerActions() {
    // A click is the framework's, not a key event's: the signal carries the context
    // whose status area was clicked. Wired before the registration guard below,
    // because a click works whether or not this build has a framework Instance.
    modeAction_.connect<fcitx::SimpleAction::Activated>(
        [this](fcitx::InputContext *ic) { toggleEnglish(ic); });
    punctuationAction_.connect<fcitx::SimpleAction::Activated>(
        [this](fcitx::InputContext *ic) { togglePunctuation(ic); });
    // Registering the names is what needs the UserInterfaceManager. A plain source
    // build's test has none and drives the actions directly.
    if (!uiManager_) { return; }
    // Both actions are the same shape: a checkable entry whose text and icon
    // describe the state of whichever context the status area is asking about.
    // registerAction() keys on the name across the whole framework, so the names
    // carry this addon's own prefix.
    for (const auto *name : {kModeActionName, kPunctuationActionName}) {
        fcitx::SimpleAction &action = name == kModeActionName ? modeAction_ : punctuationAction_;
        action.setCheckable(true);
        if (!uiManager_->registerAction(name, &action)) {
            FCITX_WARN() << "Chengyin IM: status action " << name << " already registered";
        }
    }
}

void Engine::activate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) {
    // Instance::activateInputMethod() clears StatusGroup::InputMethod immediately
    // before this call, so re-adding here is what keeps both actions in the status
    // area for the input method that is now active.
    auto *ic = event.inputContext();
    if (!uiManager_ || !ic) { return; }
    auto *state = ic->propertyFor(&factory_);
    updateActions(ic, *state);
    ic->statusArea().addAction(fcitx::StatusGroup::InputMethod, &modeAction_);
    ic->statusArea().addAction(fcitx::StatusGroup::InputMethod, &punctuationAction_);
}

void Engine::updateActions(fcitx::InputContext *ic, State &state) {
    // The text is what the input status area and the tray menu actually render;
    // the icons are Fcitx's own punctuation glyphs, shipped by
    // fcitx5-chinese-addons, whose own punctuation toggle uses the same pair.
    modeAction_.setShortText(state.english ? "英" : "中");
    modeAction_.setLongText(state.english ? "当前为英文模式，点击切换到中文" : "当前为中文模式，点击切换到英文");
    modeAction_.setChecked(!state.english);
    punctuationAction_.setShortText(state.punctuation ? "中文标点" : "英文标点");
    punctuationAction_.setLongText("中文模式下按 ASCII 标点输入对应中文标点");
    punctuationAction_.setIcon(state.punctuation ? "fcitx-punc-active" : "fcitx-punc-inactive");
    punctuationAction_.setChecked(state.punctuation);
    // setShortText/setChecked do not notify, so the repaint has to be asked for.
    // Without a UI manager there is nothing listening and update() would only walk
    // an empty action registry.
    if (!uiManager_) { return; }
    modeAction_.update(ic);
    punctuationAction_.update(ic);
}

bool Engine::english(fcitx::InputContext *ic) { return ic->propertyFor(&factory_)->english; }

bool Engine::chinesePunctuation(fcitx::InputContext *ic) { return ic->propertyFor(&factory_)->punctuation; }

void Engine::toggleEnglish(fcitx::InputContext *ic) {
    if (sensitive(ic)) { return; }
    auto *state = ic->propertyFor(&factory_);
    // Windows drops the composition here rather than committing its raw spelling:
    // toggleEnglish() calls unbind() before it flips the flag
    // (platforms/windows/service.cpp:1190), and unbind() resets the session and the
    // paired-quote state (service.cpp:1224-1227). Deliberately the same here, so a
    // half-typed syllable is discarded rather than typed out as Latin letters.
    state->english = !state->english;
    state->punctuationState.reset();
    state->shift.reset();
    // clear() resets the core session and repaints the panel, so the preedit and
    // candidates of the discarded composition go away in the same step.
    clear(ic);
    updateActions(ic, *state);
}

void Engine::togglePunctuation(fcitx::InputContext *ic) {
    if (sensitive(ic)) { return; }
    auto *state = ic->propertyFor(&factory_);
    state->punctuation = !state->punctuation;
    updateActions(ic, *state);
}

void Engine::retire(State &state) {
    // What the Windows adapter's unbind() clears besides the composition itself.
    state.punctuationState.reset();
    state.shift.reset();
}

void Engine::keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &event) {
    auto *ic = event.inputContext();
    if (sensitive(ic)) {
        retire(*ic->propertyFor(&factory_));
        clear(ic);
        return;
    }
    auto *state = ic->propertyFor(&factory_);
    const auto sym = event.key().sym();
    const auto states = event.key().states();
    // A chord belongs to the application: the Windows adapter draws the same line
    // (its `shortcut` ignores the Shift bit, because a shifted symbol is a
    // punctuation key in its own right).
    const bool shortcut = shortcutChord(states);
    const auto setting = settings_.shiftSwitch;
    if (event.isRelease()) {
        // The only release that means anything here is the end of a Shift tap.
        // Everything else is left to the application, exactly as before.
        if (state->shift.up(sym, setting, shortcut)) {
            toggleEnglish(ic);
            event.filterAndAccept();
        }
        return;
    }
    // Every press is offered to the tap machine first: another key arriving while
    // Shift is held is precisely what turns the gesture into a chord, and the
    // machine has to see that key to know.
    state->shift.down(sym, setting, shortcut, repeated(event));
    if (ShiftTap::matches(sym, setting)) {
        // The Shift key itself is a modifier, never input: it passes through so the
        // application keeps its own modifier state.
        return;
    }
    if (state->english) {
        // English mode forwards everything, letters included, untouched.
        return;
    }
    // Windows' conversion conditions in full: Chinese mode, no shortcut, no Caps
    // Lock, the punctuation switch on, and the character in the shared table
    // (platforms/windows/service.cpp:236). The Shift bit is deliberately not
    // tested: Fcitx has already folded it into the keysym, so Shift+';' arrives as
    // the colon keysym with no Shift state, which is the character the user meant.
    const char ascii = punctuationKey(event.key());
    const bool convertible = ascii != 0 && state->punctuation && !shortcut && !capsLock(event);
    // Whether the core is currently holding something the user is looking at: a
    // composition, or the association list a commit produced.
    const bool association = chengyin_session_is_association(state->session.get()) > 0;
    if (convertible && (!active(*state) || association)) {
        // Windows takes its standalone punctuation action here, for exactly this
        // pair of situations: nothing on screen, or an association list to close
        // (`!active || association`). The punctuation is inserted on its own and
        // the session is retired, with no candidate involved.
        ic->commitString(renderPunctuation(state->punctuationState, ascii, true));
        state->punctuationState.accepted(ascii, true);
        resetSession(ic);
        event.filterAndAccept();
        return;
    }
    // With a composition on screen the key goes to the core instead, which is what
    // keeps `'` a syllable separator: planKey() routes the apostrophe to the core
    // with no punctuation attached (platforms/windows/keymap.h:135), so `xi'an`
    // still splits syllables rather than becoming ‘. Every other supported
    // punctuation character is handed over with itself attached, and process()
    // appends it only once the core has actually committed.
    const char appended = convertible && !association && ascii != '\'' ? ascii : 0;
    // Consumed when the core handled the key OR when this event actually converted
    // its punctuation. The second half is why a key that ended a composition is not
    // handed to the application on top of the text it just produced, and it is also
    // why a key whose conversion was switched off still passes through even though
    // the core committed the candidate first -- exactly the split the Windows
    // adapter makes with `eaten = (result & CHENGYIN_HANDLED) || hasCommit` for the
    // converted case and a bare passthrough for the other.
    const auto outcome = process(ic, mapKey(sym), modifiersOf(states), appended);
    if (outcome.result >= 0 && ((outcome.result & CHENGYIN_HANDLED) || outcome.converted)) {
        event.filterAndAccept();
    }
}

// Whether the core is showing the user something right now: a composition, or the
// association list a commit produced. This is the Windows adapter's `active(context)`
// (platforms/windows/service.cpp:1186), and it is what decides whether punctuation
// is inserted on its own or handed to the core.
bool Engine::active(State &state) {
    return chengyin_session_is_association(state.session.get()) > 0 ||
           !text(state.session.get(), CHENGYIN_TEXT_PREEDIT).empty();
}

void Engine::resetSession(fcitx::InputContext *ic) {
    auto *state = ic->propertyFor(&factory_);
    chengyin_session_reset(state->session.get());
    synchronize(*state);
    ++state->revision;
    refresh(ic);
}

uint32_t Engine::modifiersOf(fcitx::KeyStates states) {
    uint32_t modifiers = 0;
    if (states & fcitx::KeyState::Ctrl) { modifiers |= CHENGYIN_MOD_CONTROL; }
    if (states & (fcitx::KeyStates(fcitx::KeyState::Alt) | fcitx::KeyState::Mod5)) { modifiers |= CHENGYIN_MOD_ALT; }
    if (states & (fcitx::KeyStates(fcitx::KeyState::Super) | fcitx::KeyState::Super2 | fcitx::KeyState::Meta |
                  fcitx::KeyState::Hyper | fcitx::KeyState::Hyper2)) { modifiers |= CHENGYIN_MOD_SUPER; }
    return modifiers;
}

Engine::Outcome Engine::process(fcitx::InputContext *ic, uint32_t key, uint32_t modifiers, char punctuation) {
    auto *state = ic->propertyFor(&factory_);
    synchronize(*state);
    // Same place as the dictionary switch, and for the same reason: a session is
    // only ever re-configured between compositions. A pending settings change is
    // applied here before the key is processed, so the very next keystroke after
    // a save already uses the new rules, and a session that was busy at save time
    // picks the change up as soon as its composition ends.
    applySettings(*state);
    applyProfile(*state);
    const auto result = chengyin_session_process(state->session.get(), key, modifiers);
    ++state->revision;
    if (result < 0) { clear(ic); return {result, false}; }
    bool converted = false;
    const auto commit = text(state->session.get(), CHENGYIN_TEXT_COMMIT);
    // Commit is delivered even when punctuation is forwarded to the application.
    //
    // `punctuation` is a character from the shared table to render and append to
    // the core's own commit. Appending it here makes candidate and punctuation a
    // single commitString, so the application never sees the original key apart
    // from the text it produced -- the same reason the Windows adapter fills
    // plan.punctuation into the TSF edit it is already writing
    // (platforms/windows/service.cpp:831-835) rather than letting the host's own
    // key race an asynchronous commit.
    //
    // Three things make this conditional, and all three match Windows:
    //  * only when the core really committed (`hasCommit`), so the apostrophe that
    //    merely splits a syllable is neither converted nor eaten;
    //  * only then does the quote pair state advance, which is what keeps a
    //    half-written pair from desynchronising after a failed write;
    //  * never for a passthrough, which is why the appended form is reserved for
    //    the composition path.
    if (!commit.empty()) {
        if (punctuation != 0) {
            ic->commitString(commit + renderPunctuation(state->punctuationState, punctuation, true));
            state->punctuationState.accepted(punctuation, true);
            converted = true;
        } else {
            // A passthrough: the core committed its candidate, and the punctuation
            // key itself is left to the application. The caller must not consume it,
            // or the character the user typed would be swallowed.
            ic->commitString(commit);
        }
    }
    // The host has accepted the text, so this is the one moment learning may be
    // acknowledged. Two conditions beyond the commit itself:
    //
    // * Only an accepted key is a selection. The core also commits when a
    //   punctuation key forces the current candidate out and is then passed to
    //   the application; that is a passthrough, not a choice, so it trains
    //   nothing even though it produced text.
    // * learn() itself re-checks the sensitive flag and the learning switch.
    //
    // The order inside learn() matters: LEARNING_KEY and COMMIT are read before
    // learn_commit, which is the call documented to consume them.
    if (!commit.empty() && result >= 0 && (result & CHENGYIN_HANDLED)) { learn(ic, *state); }
    synchronize(*state); // only switches after an active composition has finished
    // After both, so this event's own outcome -- a fresh composition, a finished
    // commit, an association list -- decides whether the session may be
    // re-snapshotted. A selection just recorded lands here and takes effect from
    // the next input, exactly as a settings change does.
    applyProfile(*state);
    refresh(ic, (result & CHENGYIN_LIMITED) != 0);
    return {result, converted};
}

void Engine::refresh(fcitx::InputContext *ic, bool limited) {
    auto *state = ic->propertyFor(&factory_);
    auto *session = state->session.get();
    auto &panel = ic->inputPanel();
    panel.reset();
    const auto preedit = text(session, CHENGYIN_TEXT_PREEDIT);
    if (!preedit.empty()) {
        fcitx::Text display(preedit);
        display.setCursor(chengyin_session_preedit_cursor(session)); // UTF-8 byte offset, including confirmed Chinese segments
        if (ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
            panel.setClientPreedit(display);
        } else {
            panel.setPreedit(display);
        }
    }
    const auto count = chengyin_session_candidate_count(session);
    if (count > 0) {
        auto list = std::make_unique<fcitx::CommonCandidateList>();
        // Both the list's page width and the digits it answers to follow the
        // width the core was actually configured with, never a fixed 9: the panel
        // must not offer a row the core does not have on this page, and a digit
        // must not select a row the panel is not showing.
        const int width = state->pageSize;
        fcitx::KeyList keys;
        for (int i = 0; i < width; ++i) { keys.emplace_back(static_cast<fcitx::KeySym>(FcitxKey_1 + i)); }
        if (chengyin_session_is_association(session)<=0) {list->setSelectionKey(keys);}
        list->setPageSize(width);
        for (int32_t i = 0; i < count; ++i) {
            list->append<Word>(text(session, CHENGYIN_TEXT_CANDIDATE, static_cast<size_t>(i)),
                               this, static_cast<size_t>(i), state->revision);
        }
        list->setGlobalCursorIndex(chengyin_session_selected(session));
        panel.setCandidateList(std::move(list));
    }
    if (chengyin_session_is_association(session)>0) {panel.setAuxUp(fcitx::Text("联想 · Tab / 鼠标确认"));}
    if (!preedit.empty() && !settingsError_.empty()) {
        panel.setAuxDown(fcitx::Text(settingsError_ + "；当前设置仍可继续使用"));
    } else if (limited || chengyin_session_budget_limited(session)>0) { panel.setAuxDown(fcitx::Text("输入达到长度或歧义上限，请分段输入")); }
    else if (!preedit.empty() && !dictionaryError_.empty()) {
        panel.setAuxDown(fcitx::Text(dictionaryError_ + "；当前词典仍可使用"));
    } else if (!preedit.empty() && reloadState_ == ReloadState::Loading) {
        panel.setAuxDown(fcitx::Text("正在加载词典，继续使用当前词典"));
    } else if (!preedit.empty() && !profileWarning_.empty()) {
        // Shown once per run rather than once per key: the warning describes how
        // this process started, and repeating it would push the composition hint
        // off the panel. It carries no spelling and no text.
        panel.setAuxDown(fcitx::Text(profileWarning_ + "；本次选择仍会影响当前排序"));
    }
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void Engine::clear(fcitx::InputContext *ic) {
    auto *state = ic->propertyFor(&factory_);
    chengyin_session_reset(state->session.get());
    // The pair state and a half-finished Shift tap are transient in exactly the
    // same way the composition is, so they go with it. The 中/英 mode and the
    // punctuation switch do NOT: Windows keeps both across a reset and a focus
    // change (its unbind() clears punctuation_ and shift_ and leaves english_
    // alone, service.cpp:1224-1227), and a user who switched to English does not
    // expect the next window to switch back.
    //
    // The standalone punctuation path deliberately does NOT come through here: it
    // retires the session through resetSession() alone, because the quote it just
    // inserted is the first half of a pair whose second half has to follow. That is
    // the same split Windows makes between endLocked() and unbind().
    retire(*state);
    synchronize(*state);
    // A reset leaves the session showing nothing, so this is the second place a
    // pending snapshot can land -- and the one every key of a sensitive context
    // reaches, which is how such a session stays current without training.
    applyProfile(*state);
    ++state->revision;
    refresh(ic);
}

void Engine::reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) { clear(event.inputContext()); }

void Engine::select(fcitx::InputContext *ic, size_t index, uint64_t revision) {
    if (sensitive(ic) || !ic->hasFocus()) { clear(ic); return; }
    auto *state = ic->propertyFor(&factory_);
    if (state->revision != revision || index >= static_cast<size_t>(state->pageSize)) { return; }
    process(ic, CHENGYIN_KEY_SELECT_1 + static_cast<uint32_t>(index), 0);
}
} // namespace chengyin
