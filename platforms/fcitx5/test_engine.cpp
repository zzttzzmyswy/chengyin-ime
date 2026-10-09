// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <fcitx-utils/key.h>
#include <fcitx-utils/event.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>

class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager) : InputContext(manager, "chengyin-test") { created(); }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "chengyin-test"; }
    void commitStringImpl(const std::string &value) override { committed += value; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
};

// Learning is on by default and the adapter persists it, so this test needs the
// same private-environment discipline the other suites use: every XDG directory
// resolves inside a temporary tree and the profile path is injected explicitly.
// Without both, a run would read and write the developer's own
// $XDG_DATA_HOME/fcitx5/chengyin/profile.bin.
struct TemporaryDirectory {
    TemporaryDirectory() {
        char pattern[] = "/tmp/chengyin-engine-XXXXXX";
        const char *created = ::mkdtemp(pattern);
        assert(created);
        path = created;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    std::string path;
};

int main() {
    TemporaryDirectory directory;
    setenv("XDG_CONFIG_HOME", (directory.path + "/config").c_str(), 1);
    setenv("FCITX_CONFIG_HOME", (directory.path + "/config/fcitx5").c_str(), 1);
    setenv("XDG_DATA_HOME", (directory.path + "/data").c_str(), 1);
    fcitx::InputContextManager manager;
    fcitx::EventLoop loop;
    chengyin::Engine engine(manager, loop, "", directory.path + "/data/chengyin/profile.bin");
    Context a(manager), b(manager);
    a.setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    a.focusIn();
    b.focusIn();
    const fcitx::InputMethodEntry entry("chengyin", "Chengyin", "zh_CN", "chengyin");
    auto press = [&](Context &ic, fcitx::KeySym sym, fcitx::KeyStates mods = fcitx::KeyStates(), bool release = false) {
        fcitx::KeyEvent event(&ic, fcitx::Key(sym, mods), release);
        engine.keyEvent(entry, event);
        return event.accepted();
    };
    auto type = [&](Context &ic, const std::string &input) {
        for (unsigned char c : input) { assert(press(ic, static_cast<fcitx::KeySym>(c))); }
    };
    type(a, "nihao");
    assert(a.inputPanel().clientPreedit().toString() == "nihao");
    assert(a.inputPanel().candidateList()->candidate(0).text().toString() == "你好");
    assert(!press(a, FcitxKey_x, fcitx::KeyState::Ctrl));
    assert(!press(a, FcitxKey_x, fcitx::KeyStates(), true));
    assert(a.inputPanel().clientPreedit().toString() == "nihao");
    type(b, "zhongguo");
    assert(b.inputPanel().preedit().toString() == "zhongguo");
    assert(press(a, FcitxKey_space));
    assert(a.committed == "你好");
    assert(b.committed.empty());
    assert(press(b, FcitxKey_space));
    assert(b.committed == "中国");
    assert(b.inputPanel().candidateList() && b.inputPanel().preedit().empty());
    assert(a.inputPanel().candidateList() && a.inputPanel().preedit().empty());
    assert(press(b,FcitxKey_Tab));assert(b.committed=="中国人民");
    assert(!press(a,FcitxKey_space));assert(a.committed=="你好" && a.inputPanel().empty());
    assert(a.inputPanel().clientPreedit().empty());
    assert(!press(a, FcitxKey_space, fcitx::KeyStates(), true));
    assert(a.committed == "你好");
    type(a, "xi'an");
    assert(!press(a, FcitxKey_comma));
    assert(a.committed == "你好西安");
    type(a, "shi");
    auto stale = a.inputPanel().candidateList();
    assert(press(a, FcitxKey_Down));
    stale->candidate(0).select(&a);
    assert(a.committed == "你好西安"); // stale mouse click cannot commit a changed list
    auto list = a.inputPanel().candidateList();
    list->candidate(1).select(&a);
    assert(a.committed == "你好西安时");
    type(a, "ni");
    fcitx::InputContextEvent reset(&a, fcitx::EventType::InputContextReset);
    engine.reset(entry, reset);
    assert(a.inputPanel().clientPreedit().empty());
    type(a, "ni");
    fcitx::InputContextEvent deactivate(&a, fcitx::EventType::InputContextFocusOut);
    engine.deactivate(entry, deactivate);
    assert(a.inputPanel().clientPreedit().empty());
    type(a, "ni");
    a.setCapabilityFlags(fcitx::CapabilityFlag::Password);
    assert(!press(a, FcitxKey_n));
    assert(a.inputPanel().empty());
    assert(a.committed == "你好西安时");
    a.setCapabilityFlags(fcitx::CapabilityFlag::Sensitive);
    assert(!press(a, FcitxKey_n));
    assert(a.inputPanel().empty());
    std::cout << "Fcitx5 headless integration test passed\n";
}
