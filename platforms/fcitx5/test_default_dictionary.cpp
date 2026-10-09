// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>

// Regression for the packaged default lexicon. A profile that never saved
// conf/chengyin.conf must start on the distribution's full vocabulary, an
// explicit user path must still win, and an unusable default must stay visible
// instead of silently substituting another lexicon.
//
// The packaged file itself is never modified: the engine is pointed at a
// temporary copy, so "the default is gone / is unreadable" is exercised against
// the fixture while the installed lexicon stays intact.
//
// peng'you / 朋友 is deliberately absent from data/demo.tsv, so finding it proves
// the full lexicon is what got loaded.

class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager) : InputContext(manager, "default-dictionary-test") {
        created();
        focusIn();
    }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "default-dictionary-test"; }
    void commitStringImpl(const std::string &text) override { committed += text; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
};

struct TemporaryDirectory {
    TemporaryDirectory() {
        char pattern[] = "/tmp/chengyin-default-XXXXXX";
        const char *created = ::mkdtemp(pattern);
        assert(created);
        path = created;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    std::string path;
};

static void writeFile(const std::string &path, const std::string &contents) {
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
    stream.close();
    assert(stream);
}

int main() {
    TemporaryDirectory directory;
    assert(::setenv("XDG_CONFIG_HOME", (directory.path + "/config").c_str(), 1) == 0);
    assert(::setenv("FCITX_CONFIG_HOME", (directory.path + "/config/fcitx5").c_str(), 1) == 0);
    assert(::setenv("XDG_DATA_HOME", (directory.path + "/data").c_str(), 1) == 0);

    const auto full = directory.path + "/full.tsv";
    const auto custom = directory.path + "/custom.tsv";
    const auto missing = directory.path + "/absent.tsv";
    // The fixture is a private copy of the lexicon the packages ship, so the test
    // proves the full-vocabulary default without ever writing to an installed
    // path. CMake supplies its location.
    const std::string shipped = CHENGYIN_TEST_DICTIONARY;
    assert(!shipped.empty());
    std::filesystem::copy_file(shipped, full);
    writeFile(custom, "peng'you\t朋友朋友\t900\n");
    const auto configFile = directory.path + "/config/fcitx5/conf/chengyin.conf";

    fcitx::InputContextManager manager;
    fcitx::EventLoop loop;
    chengyin::Engine engine(manager, loop, full);
    Context a(manager);
    const fcitx::InputMethodEntry entry("chengyin", "Chengyin", "zh_CN", "chengyin");
    auto type = [&](const std::string &input) {
        for (const auto c : input) {
            fcitx::KeyEvent event(&a, fcitx::Key(static_cast<fcitx::KeySym>(c)));
            engine.keyEvent(entry, event);
            assert(event.accepted());
        }
    };
    auto has = [&](const std::string &value) {
        const auto list = a.inputPanel().candidateList();
        if (!list) { return false; }
        for (int i = 0; i < list->size(); ++i) {
            if (list->candidate(i).text().toString() == value) { return true; }
        }
        return false;
    };
    auto escape = [&] {
        fcitx::KeyEvent event(&a, fcitx::Key(FcitxKey_Escape));
        engine.keyEvent(entry, event);
    };
    auto configure = [&](const std::string &path) {
        fcitx::RawConfig config;
        config.setValueByPath("DictionaryPath", path);
        engine.setConfig(config);
    };
    auto persistedPath = [&] {
        chengyin::EngineConfig config;
        fcitx::readAsIni(config, "conf/chengyin.conf");
        return *config.dictionaryPath;
    };
    // 朋友 exists only in the full lexicon; the demo ships neither character, so
    // this candidate can never be synthesized from it.
    auto fullLexiconLoaded = [&] {
        type("pengyou");
        const bool found = has("朋友");
        escape();
        return found;
    };

    std::vector<std::function<void()>> steps;
    steps.push_back([&] {
        // The addon factory runs reloadConfig on a profile that has never saved a
        // configuration: the packaged lexicon must be adopted, not the demo.
        engine.reloadConfig();
    });
    steps.push_back([&] {
        assert(engine.reloadState() == chengyin::Engine::ReloadState::Ready);
        assert(engine.dictionaryPath() == full);
        assert(engine.dictionaryError().empty());
        assert(!std::filesystem::exists(configFile));
        assert(fullLexiconLoaded());
        configure(custom);
    });
    steps.push_back([&] {
        // An explicit user path still wins and is the value persisted.
        assert(engine.reloadState() == chengyin::Engine::ReloadState::Ready);
        assert(engine.dictionaryPath() == custom);
        assert(persistedPath() == custom);
        type("pengyou");
        assert(has("朋友朋友") && !has("朋友"));
        escape();
        configure("");
    });
    steps.push_back([&] {
        // Clearing the field is an explicit request for the bundled demo.
        assert(engine.reloadState() == chengyin::Engine::ReloadState::Ready);
        assert(engine.dictionaryPath().empty());
        assert(persistedPath().empty());
        assert(!fullLexiconLoaded());
        configure(missing);
    });
    steps.push_back([&] {
        // A configured path that cannot be opened is reported and never saved,
        // and the lexicon already in use keeps answering.
        assert(engine.reloadState() == chengyin::Engine::ReloadState::Failed);
        assert(!engine.dictionaryError().empty());
        assert(engine.dictionaryPath().empty());
        assert(persistedPath().empty());
        type("nihao");
        assert(has("你好"));
        escape();
        writeFile(custom, std::string("\xff", 1)); // valid path, corrupt content
        configure(custom);
    });
    steps.push_back([&] {
        assert(engine.reloadState() == chengyin::Engine::ReloadState::Failed);
        assert(!engine.dictionaryError().empty());
        writeFile(custom, "peng'you\t朋友朋友\t900\n");
        std::filesystem::remove(configFile); // back to "never configured"
        engine.reloadConfig();
    });
    steps.push_back([&] {
        // Dropping the stored file returns to the packaged lexicon rather than
        // leaving the engine parked on the demo for the rest of the session.
        assert(engine.reloadState() == chengyin::Engine::ReloadState::Ready);
        assert(engine.dictionaryPath() == full);
        assert(engine.dictionaryError().empty());
        assert(!std::filesystem::exists(configFile));
        assert(fullLexiconLoaded());
    });
    // A packaged lexicon missing from disk is the fresh-install failure mode: the
    // error must be observable and input must stay usable on the demo.
    fcitx::InputContextManager brokenManager;
    fcitx::EventLoop brokenLoop;
    chengyin::Engine broken(brokenManager, brokenLoop, missing);
    broken.reloadConfig();
    assert(broken.reloadState() == chengyin::Engine::ReloadState::Loading);
    bool brokenChecked = false;
    auto brokenTimer = brokenLoop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0,
        [&](fcitx::EventSourceTime *source, uint64_t) {
            if (broken.reloadState() == chengyin::Engine::ReloadState::Loading) {
                source->setNextInterval(1000);
                source->setOneShot();
                return true;
            }
            assert(broken.reloadState() == chengyin::Engine::ReloadState::Failed);
            assert(!broken.dictionaryError().empty());
            assert(broken.dictionaryPath().empty()); // stayed on the demo
            brokenChecked = true;
            brokenLoop.exit();
            return true;
        });
    brokenLoop.exec();
    assert(brokenChecked);

    size_t step = 0;
    const auto deadline = fcitx::now(CLOCK_MONOTONIC) + 10 * 1000000;
    auto timer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0,
        [&](fcitx::EventSourceTime *source, uint64_t) {
            assert(fcitx::now(CLOCK_MONOTONIC) < deadline);
            if (engine.reloadState() != chengyin::Engine::ReloadState::Loading && step < steps.size()) {
                steps[step++]();
            }
            if (step == steps.size()) { loop.exit(); }
            else { source->setNextInterval(1000); source->setOneShot(); }
            return true;
        });
    loop.exec();
    assert(step == steps.size());
    std::cout << "Fcitx5 packaged default dictionary test passed (" << steps.size() << " stages)\n";
}
