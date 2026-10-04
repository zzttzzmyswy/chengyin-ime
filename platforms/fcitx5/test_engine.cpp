#include "engine.h"
#include <cassert>
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
    explicit Context(fcitx::InputContextManager &manager) : InputContext(manager, "myswy-test") { created(); }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "myswy-test"; }
    void commitStringImpl(const std::string &value) override { committed += value; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
};

int main() {
    fcitx::InputContextManager manager;
    fcitx::EventLoop loop;
    myswy::Engine engine(manager, loop);
    Context a(manager), b(manager);
    a.setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    a.focusIn();
    b.focusIn();
    const fcitx::InputMethodEntry entry("myswy", "Myswy", "zh_CN", "myswy");
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
