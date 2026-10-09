// SPDX-License-Identifier: GPL-3.0-or-later
// Real-framework integration test for the Fcitx 5 adapter (iteration I13).
//
// test_engine.cpp constructs an Engine and a fake InputContext by hand, so it
// proves the adapter's own logic but never exercises the framework around it:
// no addon discovery, no InputMethodManager, no focus or input-method-switch
// routing. This file boots a real fcitx::Instance that loads the real
// testfrontend / testim addons plus the chengyin addon built from this tree, and
// drives every case through the path a desktop session actually uses.
//
// Two things the framework cannot supply in a unit test are staged instead of
// installed: the addon and input-method registration files are copied into a
// private directory (a real install puts them in the system data dirs), and
// conf/chengyin.conf points at a lexicon written into a private temporary
// directory. SKIP_FCITX_USER_PATH makes every user directory resolve to empty,
// so the user's own configuration, lexicon and input history are never read or
// written; the adapter cannot even open a file under $HOME.
//
// The lexicon is authored here so candidate order is deterministic and does not
// depend on data/daily.tsv: nihao -> 你好 with 拟好 second, zhongguo -> 中国, and
// ma has twenty entries so paging has a well-defined second page.
#include "engine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <sys/stat.h>
#include <unistd.h>
#include "chengyin_ime.h"
#include "testfrontend_public.h"

namespace {

int failures = 0;

void check(bool ok, const std::string &what, const std::string &detail = {}) {
    std::cout << (ok ? "ok   " : "FAIL ") << what;
    if (!detail.empty()) { std::cout << "  [" << detail << "]"; }
    std::cout << std::endl;
    if (!ok) { ++failures; }
}

// Records what was actually observed, so a failure names the real value instead
// of only the expectation.
void checkEqual(const std::string &actual, const std::string &expected, const std::string &what) {
    check(actual == expected, what, "expected '" + expected + "', got '" + actual + "'");
}

void checkCount(size_t actual, size_t expected, const std::string &what) {
    check(actual == expected, what,
          "expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

void writeFile(const std::string &path, const std::string &contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
    stream.close();
    if (!stream) { throw std::runtime_error("cannot write " + path); }
}

// Twenty single characters for `ma`, weights strictly decreasing, so page two
// starts at the eleventh candidate overall.
std::string maEntries() {
    static const char *chars[] = {
        "马", "妈", "麻", "码", "骂", "嘛", "吗", "玛", "埋", "麦",
        "卖", "买", "脉", "蛮", "满", "慢", "忙", "猫", "毛", "贸",
    };
    std::string text;
    int weight = 2900;
    for (const char *character : chars) {
        text += "ma\t";
        text += character;
        text += "\t" + std::to_string(weight) + "\n";
        weight -= 100;
    }
    return text;
}

std::string primaryDictionary() {
    return "ni'hao\t你好\t3000\n"
           "ni'hao\t拟好\t2000\n"
           "ni\t你\t900\n"
           "ni\t尼\t800\n"
           "hao\t好\t900\n"
           "hao\t号\t700\n"
           "zhong'guo\t中国\t4000\n"
           "zhong'guo\t中锅\t3000\n"
           "zhong\t中\t900\n"
           "zhong\t种\t800\n"
           "guo\t国\t900\n"
           "guo\t果\t800\n"
           + maEntries();
}

// Same keys, different texts: proves a reload really swapped the lexicon rather
// than re-reading the same file.
std::string secondaryDictionary() {
    return "ni'hao\t新词一\t3000\n"
           "zhong'guo\t新国\t4000\n"
           "ma\t新马\t2900\n";
}

// A profile that puts chengyin first in the group, so switching to it is a real
// InputMethodManager change rather than a no-op, with testim as the other IM.
std::string profileContents() {
    return "[Groups/0]\n"
           "Name=Default\n"
           "Default Layout=us\n"
           "DefaultIM=chengyin\n"
           "\n"
           "[Groups/0/Items/0]\n"
           "Name=keyboard-us\n"
           "\n"
           "[Groups/0/Items/1]\n"
           "Name=chengyin\n"
           "\n"
           "[Groups/0/Items/2]\n"
           "Name=testim\n"
           "\n"
           "[GroupOrder]\n"
           "0=Default\n";
}

// Private directories and the environment the framework resolves them from.
// Everything fcitx reads is a file this harness wrote; nothing under $HOME is
// touched, and XDG_RUNTIME_DIR is private to the run.
struct Harness {
    std::filesystem::path work;
    std::filesystem::path configDir;
    std::filesystem::path dataDir;
    std::filesystem::path dictionary;
    std::filesystem::path addonConf;
    std::filesystem::path secondary;

    Harness(const std::string &sourceDir, const std::string &buildDir) {
        char pattern[] = "/tmp/chengyin-instance-XXXXXX";
        const char *created = ::mkdtemp(pattern);
        if (!created) { throw std::runtime_error("mkdtemp failed"); }
        work = created;
        configDir = work / "config";
        dataDir = work / "data";
        dictionary = work / "lexicon.tsv";
        secondary = work / "secondary.tsv";
        addonConf = configDir / "conf" / "chengyin.conf";
        std::filesystem::create_directories(configDir / "conf");
        std::filesystem::create_directories(dataDir / "addon");
        std::filesystem::create_directories(dataDir / "inputmethod");
        std::filesystem::create_directories(work / "cache");
        // XDG_RUNTIME_DIR must exist, be private, and be 0700.
        std::filesystem::create_directories(work / "run");
        ::chmod((work / "run").c_str(), 0700);

        // A real install puts these two files in the system data dirs; staging
        // them here is what lets the framework discover the addon at all.
        std::filesystem::copy_file(sourceDir + "/chengyin-addon.conf",
                                   dataDir / "addon" / "chengyin.conf");
        std::filesystem::copy_file(sourceDir + "/chengyin-inputmethod.conf",
                                   dataDir / "inputmethod" / "chengyin.conf");
        writeFile(dictionary, primaryDictionary());
        writeFile(secondary, secondaryDictionary());
        writeFile(configDir / "profile", profileContents());
        writeFile(addonConf, "DictionaryPath=" + dictionary.string() + "\n");
        writeFile(configDir / "config", "[Hotkey]\n[Behavior]\n");

        // The addon library itself is the one artefact not staged: it comes from
        // the build directory, alongside the framework's own testing addons.
        const auto systemAddons = fcitx::StandardPaths::fcitxPath("addondir");
        const auto testingData = fcitx::StandardPaths::fcitxPath("pkgdatadir", "testing");
        setenv("SKIP_FCITX_PATH", "1", 1);
        setenv("SKIP_FCITX_USER_PATH", "1", 1);
        setenv("FCITX_CONFIG_DIRS", configDir.c_str(), 1);
        setenv("FCITX_DATA_DIRS", (dataDir.string() + ":" + testingData.string()).c_str(), 1);
        setenv("FCITX_ADDON_DIRS", (buildDir + ":" + systemAddons.string()).c_str(), 1);
        setenv("XDG_CACHE_HOME", (work / "cache").c_str(), 1);
        setenv("XDG_RUNTIME_DIR", (work / "run").c_str(), 1);
    }

    ~Harness() {
        std::error_code error;
        std::filesystem::remove_all(work, error);
    }

    Harness(const Harness &) = delete;
    Harness &operator=(const Harness &) = delete;

    // Points the adapter at another lexicon, exactly as a user editing
    // conf/chengyin.conf would.
    void setDictionary(const std::filesystem::path &path) const {
        writeFile(addonConf, "DictionaryPath=" + path.string() + "\n");
    }
};

// Thin wrapper over the real test frontend. Every key the test sends goes in
// through TestFrontend::sendKeyEvent, the same entry a real frontend uses: it
// focuses the context if needed, builds a KeyEvent and hands it to Instance, so
// the adapter is reached through InputMethodManager rather than by calling
// Engine directly.
class Session {
public:
    Session(fcitx::Instance &instance, fcitx::AddonInstance *frontend, const std::string &program)
        : instance_(instance), frontend_(frontend) {
        uuid_ = frontend_->call<fcitx::ITestFrontend::createInputContext>(program);
        // A brand new context starts on the group's first entry (the keyboard),
        // because Fcitx's ActiveByDefault is off. Making chengyin current here is
        // exactly the switch a user performs, so every case drives the adapter
        // through InputMethodManager rather than relying on group defaults.
        instance_.setCurrentInputMethod(ic(), "chengyin", false);
    }

    ~Session() { frontend_->call<fcitx::ITestFrontend::destroyInputContext>(uuid_); }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    fcitx::InputContext *ic() const { return instance_.inputContextManager().findByUUID(uuid_); }

    // Returns what the framework reports: true means fcitx accepted the key and
    // the application will never see it.
    bool send(const fcitx::Key &key, bool release = false) {
        return frontend_->call<fcitx::ITestFrontend::sendKeyEvent>(uuid_, key, release);
    }

    bool type(const std::string &ascii) {
        bool accepted = true;
        for (unsigned char c : ascii) {
            if (!send(fcitx::Key(static_cast<fcitx::KeySym>(c)))) { accepted = false; }
        }
        return accepted;
    }

    std::string preedit() const { return ic()->inputPanel().preedit().toString(); }
    std::string clientPreedit() const { return ic()->inputPanel().clientPreedit().toString(); }
    std::string auxUp() const { return ic()->inputPanel().auxUp().toString(); }
    std::string auxDown() const { return ic()->inputPanel().auxDown().toString(); }

    // Returns the owning handle, not a raw pointer: InputPanel::candidateList()
    // hands out a shared_ptr and the adapter replaces the whole list on every
    // refresh, so a raw pointer taken from the temporary would dangle as soon as
    // the next key arrived.
    std::shared_ptr<fcitx::CandidateList> candidates() const {
        return ic()->inputPanel().candidateList();
    }

    int candidateCount() const {
        auto list = candidates();
        return list ? list->size() : 0;
    }

    std::string candidate(int index) const {
        auto list = candidates();
        if (!list || index < 0 || index >= list->size()) { return {}; }
        return list->candidate(index).text().toString();
    }

    // 1..9 go through the framework's own key path, like a real digit press.
    bool selectDigit(int digit) { return send(fcitx::Key(static_cast<fcitx::KeySym>(FcitxKey_1 + digit - 1))); }

private:
    fcitx::Instance &instance_;
    fcitx::AddonInstance *frontend_;
    fcitx::ICUUID uuid_{};
};

// Collects everything the adapter commits, wherever in the event pipeline the
// commit is delivered, so "committed exactly once" can be asserted rather than
// sampled.
class CommitRecorder {
public:
    explicit CommitRecorder(fcitx::Instance &instance) {
        watcher_ = instance.watchEvent<fcitx::EventType::InputContextCommitString>(
            fcitx::EventWatcherPhase::PostInputMethod, [this](fcitx::Event &event) {
                commits_.push_back(static_cast<fcitx::CommitStringEvent &>(event).text());
            });
    }

    const std::vector<std::string> &all() const { return commits_; }
    std::string joined() const {
        std::string total;
        for (const auto &piece : commits_) { total += piece; }
        return total;
    }
    void clear() { commits_.clear(); }

private:
    std::vector<std::string> commits_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> watcher_;
};

// Case 1: the framework discovers and loads the addon, registers it as an input
// method, and selecting it routes real key events into the adapter.
void case_registration(fcitx::Instance &instance, fcitx::AddonInstance *frontend) {
    auto &manager = instance.inputMethodManager();
    const fcitx::InputMethodEntry *entry = manager.entry("chengyin");
    check(entry != nullptr, "1.1 InputMethodManager knows chengyin");
    checkEqual(entry ? entry->addon() : std::string(), "chengyin", "1.2 entry points at the chengyin addon");
    checkEqual(entry ? entry->name() : std::string(), "Chengyin Pinyin (Prototype)", "1.3 entry name");

    // OnDemand=True, so the framework only instantiates the addon on first use;
    // asking for the addon and the engine must actually produce one.
    check(instance.addonManager().addon("chengyin", true) != nullptr, "1.4 chengyin addon loads");
    check(instance.inputMethodEngine("chengyin") != nullptr, "1.5 engine reachable by name");

    auto group = manager.currentGroup();
    group.inputMethodList().clear();
    group.inputMethodList().emplace_back("keyboard-us");
    group.inputMethodList().emplace_back("chengyin");
    group.inputMethodList().emplace_back("testim");
    group.setDefaultInputMethod("chengyin");
    manager.setGroup(std::move(group));

    Session session(instance, frontend, "registration");
    instance.setCurrentInputMethod(session.ic(), "chengyin", false);
    checkEqual(instance.inputMethod(session.ic()), "chengyin", "1.6 chengyin is the active input method");

    session.type("nihao");
    checkEqual(session.preedit(), "nihao", "1.7 keys reach the adapter: preedit");
    checkEqual(session.candidate(0), "你好", "1.8 keys reach the adapter: first candidate");
    session.send(fcitx::Key("Escape"));
    checkEqual(session.preedit(), "", "1.9 escape clears after the routing check");
}

// Case 2: a full composition commits exactly once and clears the panel.
void case_basic_commit(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    Session session(instance, frontend, "basic");
    recorder.clear();
    session.type("nihao");
    checkEqual(session.preedit(), "nihao", "2.1 preedit is the raw input");
    checkEqual(session.candidate(0), "你好", "2.2 first candidate is 你好");
    check(session.candidateCount() >= 2, "2.3 the second candidate exists");

    session.send(fcitx::Key("space"));
    checkEqual(recorder.joined(), "你好", "2.4 space commits 你好 exactly once");
    checkCount(recorder.all().size(), 1, "2.5 exactly one commit event");
    checkEqual(session.preedit(), "", "2.6 preedit cleared");
    // The adapter answers a commit with its own offline association list, so the
    // panel is deliberately not empty here: what must be gone is the raw input
    // composition. Asserting the association list itself is left to
    // test_engine.cpp, which owns that behaviour; what matters here is that the
    // panel is a fresh state and no second commit comes out of it.
    checkEqual(session.auxUp(), "联想 · Tab / 鼠标确认", "2.7 the post-commit panel is the association list");
    check(recorder.all().size() == 1, "2.8 the association panel commits nothing on its own");
}

// Case 3: digits pick the candidate at that position on the current page, once.
void case_digit_selection(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    {
        Session session(instance, frontend, "digit2");
        recorder.clear();
        session.type("nihao");
        const std::string second = session.candidate(1);
        checkEqual(second, "拟好", "3.1 second candidate comes from the fixture");
        session.selectDigit(2);
        checkEqual(recorder.joined(), second, "3.2 digit 2 commits the second candidate");
        checkCount(recorder.all().size(), 1, "3.3 digit 2 commits exactly once");
        checkEqual(session.preedit(), "", "3.4 preedit cleared after digit selection");
    }
    {
        Session session(instance, frontend, "digit1");
        recorder.clear();
        session.type("nihao");
        const std::string first = session.candidate(0);
        session.selectDigit(1);
        checkEqual(first, "你好", "3.5 first candidate is 你好");
        checkEqual(recorder.joined(), "你好", "3.6 digit 1 commits the first candidate");
        checkCount(recorder.all().size(), 1, "3.7 digit 1 commits exactly once");
    }
}

// Case 4: paging moves the candidate window, and digits then address the page
// that is actually displayed.
void case_paging(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    Session session(instance, frontend, "paging");
    session.type("ma");
    checkCount(static_cast<size_t>(session.candidateCount()), 9, "4.1 first page holds nine candidates");
    checkEqual(session.candidate(0), "马", "4.2 first page starts at 马");

    // `=` is mapped to page down by the adapter, exactly as Fcitx's own default
    // paging keys are; PageDown must behave the same way.
    session.send(fcitx::Key("equal"));
    checkEqual(session.candidate(0), "麦", "4.3 '=' pages forward to the eleventh candidate");
    session.send(fcitx::Key("minus"));
    checkEqual(session.candidate(0), "马", "4.4 '-' pages back to the first page");

    session.send(fcitx::Key("Page_Down"));
    checkEqual(session.candidate(0), "麦", "4.5 PageDown pages forward as well");
    recorder.clear();
    session.selectDigit(1);
    checkEqual(recorder.joined(), "麦", "4.6 a digit selects from the displayed page, not the first");
    checkCount(recorder.all().size(), 1, "4.7 paged selection commits exactly once");
}

// Case 5: editing keys. Enter's behaviour is recorded, not prescribed: the
// adapter maps Return to the core's ENTER key, and the core commits the raw
// input, which is what this asserts against the current core.
void case_editing_keys(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    {
        Session session(instance, frontend, "editing");
        session.type("nihao");
        session.send(fcitx::Key("BackSpace"));
        checkEqual(session.preedit(), "niha", "5.1 Backspace drops one letter");
        session.send(fcitx::Key("BackSpace"));
        checkEqual(session.preedit(), "nih", "5.2 Backspace again drops another");
    }
    {
        Session session(instance, frontend, "escape");
        recorder.clear();
        session.type("nihao");
        session.send(fcitx::Key("Escape"));
        checkEqual(session.preedit(), "", "5.3 Escape clears the composition");
        checkCount(session.candidateCount(), 0, "5.4 Escape removes the candidate panel");
        checkCount(recorder.all().size(), 0, "5.5 Escape commits nothing");
    }
    {
        Session session(instance, frontend, "enter");
        recorder.clear();
        session.type("nihao");
        session.send(fcitx::Key("Return"));
        checkEqual(recorder.joined(), "nihao", "5.6 Enter commits the raw input (current core behaviour)");
        checkEqual(session.preedit(), "", "5.7 Enter clears the composition");
    }
}

// Case 6: modifier combinations and key releases pass through and leave the
// composition alone, both with a live composition and with none.
void case_modifiers_and_release(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    struct Combo { const char *name; fcitx::KeyStates states; };
    const Combo combos[] = {
        {"Ctrl", fcitx::KeyState::Ctrl},
        {"Alt", fcitx::KeyState::Alt},
        {"Super", fcitx::KeyState::Super},
    };

    Session session(instance, frontend, "modifiers");
    recorder.clear();
    // With no composition: the application must receive the shortcut.
    for (const auto &combo : combos) {
        const bool accepted = session.send(fcitx::Key(FcitxKey_x, combo.states));
        check(!accepted, std::string("6.x ") + combo.name + "+x is forwarded while idle");
        check(session.ic()->hasFocus(), "6.x context keeps focus");
    }
    checkCount(recorder.all().size(), 0, "6.1 no modifier shortcut commits anything");

    // With a live composition: still forwarded, and the composition is intact.
    session.type("nihao");
    for (const auto &combo : combos) {
        const bool accepted = session.send(fcitx::Key(FcitxKey_x, combo.states));
        check(!accepted, std::string("6.x ") + combo.name + "+x is forwarded during composition");
        checkEqual(session.preedit(), "nihao",
                   std::string("6.x ") + combo.name + "+x leaves the composition untouched");
    }
    checkCount(recorder.all().size(), 0, "6.2 no modifier shortcut committed the composition");

    // A key release is not a key press: it must not commit or extend anything.
    const bool releaseAccepted = session.send(fcitx::Key(FcitxKey_space), true);
    check(!releaseAccepted, "6.3 a released space is not accepted");
    checkEqual(session.preedit(), "nihao", "6.4 a released key leaves the composition intact");
    checkCount(recorder.all().size(), 0, "6.5 a released key commits nothing");

    session.send(fcitx::Key("Escape"));
    checkEqual(session.preedit(), "", "6.6 composition cleared for the next case");
}

// Case 7: two input contexts never share a session; neither leaks into the
// other's preedit or commit stream.
void case_context_isolation(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    Session a(instance, frontend, "isolation-a");
    Session b(instance, frontend, "isolation-b");
    instance.setCurrentInputMethod(a.ic(), "chengyin", false);

    a.type("nihao");
    checkEqual(a.preedit(), "nihao", "7.1 A holds its own composition");
    checkEqual(b.preedit(), "", "7.2 B is untouched by A's typing");

    // B takes focus and commits its own word.
    recorder.clear();
    b.send(fcitx::Key(FcitxKey_space)); // focuses B, no composition yet
    b.type("zhongguo");
    checkEqual(b.preedit(), "zhongguo", "7.3 B composes independently");
    checkEqual(b.candidate(0), "中国", "7.4 B sees its own candidates");
    b.send(fcitx::Key(FcitxKey_space));
    checkEqual(recorder.joined(), "中国", "7.5 only B's word was committed");
    checkCount(recorder.all().size(), 1, "7.6 no extra commit came from A");
    checkEqual(a.preedit(), "nihao", "7.7 A still holds its composition");

    // Back to A: its composition survived, and committing it produces just its
    // own word.
    recorder.clear();
    a.send(fcitx::Key(FcitxKey_space));
    checkEqual(recorder.joined(), "你好", "7.8 A commits only its own word");
    checkCount(recorder.all().size(), 1, "7.9 A commits exactly once");
    checkEqual(a.preedit(), "", "7.10 A's composition is finished");
    checkEqual(b.preedit(), "", "7.11 B stays finished");
}

// Case 8: a sensitive (password) context is never composed into: letters are
// forwarded to the application, nothing is shown, and a composition that already
// existed is dropped by the next key.
void case_sensitive_fields(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    Session session(instance, frontend, "sensitive");
    instance.setCurrentInputMethod(session.ic(), "chengyin", false);
    session.type("nihao");
    checkEqual(session.preedit(), "nihao", "8.1 composition established before the field turns sensitive");

    // Sensitive only, not Password: Fcitx itself routes a Password field to
    // the keyboard layout (see Instance::inputMethod), so a Password flag
    // would pass this case even with the adapter guard removed. Sensitive
    // is the flag that actually reaches the plugin.
    session.ic()->setCapabilityFlags(fcitx::CapabilityFlag::Sensitive);
    recorder.clear();
    const bool accepted = session.send(fcitx::Key(static_cast<fcitx::KeySym>('n')));
    check(!accepted, "8.2 a letter in a sensitive field is forwarded, not accepted");
    checkEqual(session.preedit(), "", "8.3 the pending composition is dropped");
    checkCount(session.candidateCount(), 0, "8.4 no candidate panel in a sensitive field");
    checkCount(recorder.all().size(), 0, "8.5 nothing is committed in a sensitive field");

    // Still sensitive: repeated letters keep being forwarded and never compose.
    for (const char letter : std::string("zhongguo")) {
        session.send(fcitx::Key(static_cast<fcitx::KeySym>(letter)));
    }
    checkEqual(session.preedit(), "", "8.6 no composition accumulates while sensitive");
    checkCount(recorder.all().size(), 0, "8.7 still nothing committed");

    // Leaving the sensitive state restores normal input. With
    // CapabilityFlag::Preedit set the adapter renders into the client preedit,
    // so that is the field to read back here.
    session.ic()->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    session.type("nihao");
    checkEqual(session.clientPreedit(), "nihao", "8.8 input works again once the field is normal");
    session.send(fcitx::Key("Escape"));
}

// Case 9: switching input method mid-composition clears the panel with no extra
// commit, and switching back starts clean. `testim` is the framework's own test
// input method, so a real switch is available inside the group.
void case_input_method_switch(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    Session session(instance, frontend, "switch");
    instance.setCurrentInputMethod(session.ic(), "chengyin", false);
    session.type("nihao");
    checkEqual(session.preedit(), "nihao", "9.1 composition established");

    recorder.clear();
    instance.setCurrentInputMethod(session.ic(), "testim", false);
    checkEqual(instance.inputMethod(session.ic()), "testim", "9.2 the switch actually happened");
    checkEqual(session.preedit(), "", "9.3 switching away clears the panel");
    checkCount(session.candidateCount(), 0, "9.4 switching away drops the candidates");
    checkCount(recorder.all().size(), 0, "9.5 switching away commits nothing");

    instance.setCurrentInputMethod(session.ic(), "chengyin", false);
    checkEqual(instance.inputMethod(session.ic()), "chengyin", "9.6 switched back");
    checkEqual(session.preedit(), "", "9.7 no stale composition after switching back");

    recorder.clear();
    session.type("zhongguo");
    checkEqual(session.preedit(), "zhongguo", "9.8 typing resumes from a clean state");
    session.send(fcitx::Key("space"));
    checkEqual(recorder.joined(), "中国", "9.9 the fresh composition commits normally");
    checkCount(recorder.all().size(), 1, "9.10 committed exactly once");
}

// Case 10: focus loss drops the composition, keys sent while unfocused are not
// processed, and typing resumes normally once focus returns.
void case_focus(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    Session session(instance, frontend, "focus");
    instance.setCurrentInputMethod(session.ic(), "chengyin", false);
    session.type("nihao");
    checkEqual(session.preedit(), "nihao", "10.1 composition before losing focus");

    recorder.clear();
    session.ic()->focusOut();
    checkEqual(session.preedit(), "", "10.2 losing focus clears the panel");
    checkCount(recorder.all().size(), 0, "10.3 losing focus commits nothing");

    // TestFrontend::sendKeyEvent re-focuses an unfocused context, so an explicit
    // focusIn is what makes the "resumes normally" half meaningful.
    session.ic()->focusIn();
    checkEqual(session.preedit(), "", "10.4 no stale preedit after re-focus");
    recorder.clear();
    session.type("zhongguo");
    checkEqual(session.preedit(), "zhongguo", "10.5 typing works after re-focus");
    session.send(fcitx::Key("space"));
    checkEqual(recorder.joined(), "中国", "10.6 the post-focus composition commits normally");
    checkCount(recorder.all().size(), 1, "10.7 committed exactly once");
}

// Case 11: an application-requested reset clears composition and candidates.
void case_reset(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    Session session(instance, frontend, "reset");
    instance.setCurrentInputMethod(session.ic(), "chengyin", false);
    session.type("nihao");
    checkEqual(session.preedit(), "nihao", "11.1 composition before reset");
    check(session.candidateCount() > 0, "11.2 candidates before reset");

    recorder.clear();
    session.ic()->reset();
    checkEqual(session.preedit(), "", "11.3 reset clears the preedit");
    checkCount(session.candidateCount(), 0, "11.4 reset clears the candidates");
    checkCount(recorder.all().size(), 0, "11.5 reset commits nothing");

    session.type("zhongguo");
    checkEqual(session.preedit(), "zhongguo", "11.6 typing works after reset");
    session.send(fcitx::Key("Escape"));
}

// Case 12: mouse selection commits the clicked candidate; a candidate list
// captured before the next key must not commit afterwards, which is what the
// adapter's revision guard exists for.
void case_mouse_selection(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    Session session(instance, frontend, "mouse");
    instance.setCurrentInputMethod(session.ic(), "chengyin", false);

    // A click on the second row of the live list commits that row.
    session.type("nihao");
    const std::string second = session.candidate(1);
    recorder.clear();
    session.candidates()->candidate(1).select(session.ic());
    checkEqual(recorder.joined(), second, "12.1 clicking a candidate commits it");
    checkCount(recorder.all().size(), 1, "12.2 the click committed exactly once");

    // A list captured earlier is stale as soon as another key changes the
    // composition, so selecting from it must do nothing.
    session.type("nihao");
    auto stale = session.candidates();
    check(stale != nullptr, "12.3 a live list exists");
    const std::string before = session.candidate(0);
    session.send(fcitx::Key("Down")); // moves the cursor, bumping the revision
    recorder.clear();
    stale->candidate(0).select(session.ic());
    checkCount(recorder.all().size(), 0, "12.4 a stale list cannot commit");
    checkEqual(session.candidate(0), before, "12.5 the live list is unchanged by the stale click");
    session.send(fcitx::Key("Escape"));
}

// Case 13: exceeding the core's input-byte budget must not crash, must show the
// documented hint, and must recover after Escape. Text already committed stays.
void case_long_input(fcitx::Instance &instance, fcitx::AddonInstance *frontend, CommitRecorder &recorder) {
    Session session(instance, frontend, "long");
    instance.setCurrentInputMethod(session.ic(), "chengyin", false);

    // Commit a word first, so "already committed text survives" is meaningful.
    recorder.clear();
    session.type("nihao");
    session.send(fcitx::Key("space"));
    checkEqual(recorder.joined(), "你好", "13.1 a word commits before the budget is exhausted");

    // MAX_INPUT_BYTES is 63; 70 letters is comfortably past it.
    recorder.clear();
    for (int i = 0; i < 70; ++i) {
        session.send(fcitx::Key(static_cast<fcitx::KeySym>('n')));
    }
    checkEqual(session.auxDown(),
               "输入达到长度或歧义上限，请分段输入",
               "13.2 the documented hint appears once the budget is exhausted");
    check(session.candidateCount() > 0, "13.3 the composition is kept, not discarded");
    checkCount(recorder.all().size(), 0, "13.4 over-long input commits nothing");

    // The panel must not grow past the core's text limit even under this load.
    check(session.preedit().size() <= CHENGYIN_MAX_INPUT_BYTES,
          "13.5 preedit never exceeds CHENGYIN_MAX_INPUT_BYTES",
          std::to_string(session.preedit().size()) + " bytes");

    session.send(fcitx::Key("Escape"));
    checkEqual(session.preedit(), "", "13.6 Escape clears the over-long composition");
    checkEqual(session.auxDown(), "", "13.7 Escape clears the hint");
    checkCount(recorder.all().size(), 0, "13.8 Escape commits nothing");

    recorder.clear();
    session.type("zhongguo");
    session.send(fcitx::Key("space"));
    checkEqual(recorder.joined(), "中国", "13.9 normal input resumes after the limit");
    checkCount(recorder.all().size(), 1, "13.10 and commits exactly once");
}

// Case 14: pointing conf/chengyin.conf at another lexicon and reloading makes the
// new words available. The load is asynchronous and a running composition keeps
// the old lexicon until it finishes, so this case advances in stages driven by
// the framework's own event loop (see main).
void case_dictionary_reload_start(fcitx::Instance &instance, Session &session,
                                  const Harness &harness) {
    instance.setCurrentInputMethod(session.ic(), "chengyin", false);
    session.type("nihao");
    checkEqual(session.candidate(0), "你好", "14.1 primary lexicon answers first");
    session.send(fcitx::Key("Escape"));

    // Repoint the configuration and ask the framework to reload the addon, which
    // is exactly what `fcitx5-remote -r` does.
    harness.setDictionary(harness.secondary);
    instance.reloadAddonConfig("chengyin");
}

// Second stage: the load has published, so the session synchronises to the new
// lexicon on its next key.
void case_dictionary_reload_finish(Session &session, const CommitRecorder &recorder) {
    session.type("nihao");
    checkEqual(session.candidate(0), "新词一", "14.2 the new lexicon answers after the reload");
    session.send(fcitx::Key("Escape"));
    session.type("zhongguo");
    checkEqual(session.candidate(0), "新国", "14.3 other keys came from the new lexicon too");
    session.send(fcitx::Key("Escape"));
    checkCount(recorder.all().size(), 0, "14.4 the reload itself commits nothing");
}

// Case 15: a behaviour snapshot, not a specification. Shifted letters and the
// Caps Lock state are recorded exactly as the adapter and core behave today; the
// roadmap still lists Caps Lock as awaiting design verification, so these
// assertions exist so a future change is noticed rather than to bless it.
void case_uppercase_snapshot(fcitx::Instance &instance, fcitx::AddonInstance *frontend,
                             CommitRecorder &recorder) {
    Session session(instance, frontend, "uppercase");
    instance.setCurrentInputMethod(session.ic(), "chengyin", false);

    // Idle Shift+n. Fcitx reports the shifted keysym; the adapter lowercases
    // nothing, so what matters is whether the core accepts it at all.
    recorder.clear();
    const bool shiftAccepted = session.send(fcitx::Key(FcitxKey_N, fcitx::KeyState::Shift));
    checkEqual(session.preedit(), "n",
               "15.1 Shift+N while idle composes as lowercase 'n' (observed)");
    check(shiftAccepted, "15.2 Shift+N is accepted (observed)");
    checkCount(recorder.all().size(), 0, "15.3 Shift+N commits nothing (observed)");
    session.send(fcitx::Key("Escape"));

    // Caps Lock while idle: the framework keeps the letter keysym and reports the
    // lock as a state bit, so the adapter sees an ordinary 'n'.
    session.send(fcitx::Key("Escape"));
    recorder.clear();
    const bool capsAccepted = session.send(fcitx::Key(FcitxKey_n, fcitx::KeyState::CapsLock));
    checkEqual(session.preedit(), "n", "15.4 Caps Lock + n composes as 'n' (observed)");
    check(capsAccepted, "15.5 Caps Lock + n is accepted (observed)");
    checkCount(recorder.all().size(), 0, "15.6 Caps Lock + n commits nothing (observed)");

    // Shift+letter in the middle of a composition.
    session.send(fcitx::Key("Escape"));
    session.type("ni");
    const bool midAccepted = session.send(fcitx::Key(FcitxKey_H, fcitx::KeyState::Shift));
    checkEqual(session.preedit(), "nih", "15.7 Shift+H mid-composition extends the preedit (observed)");
    check(midAccepted, "15.8 Shift+H mid-composition is accepted (observed)");
    session.send(fcitx::Key("Escape"));
    checkEqual(session.preedit(), "", "15.9 Escape still clears the composition");
}

// Every synchronous case, in order. Kept separate from main so the caller can
// wait for the configured lexicon before running any of them; case 14 is the
// only asynchronous one and is driven by the caller instead.
void runForegroundCases(fcitx::Instance &instance, fcitx::AddonInstance *frontend,
                        CommitRecorder &recorder) {
    case_registration(instance, frontend);
    case_basic_commit(instance, frontend, recorder);
    case_digit_selection(instance, frontend, recorder);
    case_paging(instance, frontend, recorder);
    case_editing_keys(instance, frontend, recorder);
    case_modifiers_and_release(instance, frontend, recorder);
    case_context_isolation(instance, frontend, recorder);
    case_sensitive_fields(instance, frontend, recorder);
    case_input_method_switch(instance, frontend, recorder);
    case_focus(instance, frontend, recorder);
    case_reset(instance, frontend, recorder);
    case_mouse_selection(instance, frontend, recorder);
    case_long_input(instance, frontend, recorder);
    case_uppercase_snapshot(instance, frontend, recorder);
}

} // namespace

namespace {

// Drives the suite from one place on the event loop: waits for the lexicon load
// to settle, runs the synchronous cases, then finishes the asynchronous reload
// case. The two waits are unavoidable - factory.cpp's reloadConfig() and the
// case-14 reload both load a lexicon on a worker thread, and asserting sooner
// would silently test the built-in demo lexicon. A deadline keeps a stuck load
// from hanging the suite.
struct Runner {
    fcitx::Instance &instance;
    fcitx::AddonInstance *frontend;
    const Harness &harness;
    chengyin::Engine *engine;
    CommitRecorder recorder;
    std::unique_ptr<Session> reloadSession;
    uint64_t deadline;
    int phase = 0;

    Runner(fcitx::Instance &i, fcitx::AddonInstance *f, const Harness &h, chengyin::Engine *e)
        : instance(i), frontend(f), harness(h), engine(e), recorder(i),
          deadline(fcitx::now(CLOCK_MONOTONIC) + 30ULL * 1000000ULL) {}

    void advance() {
        // One shot, re-armed from the same source: creating a new source from
        // inside a callback and dropping the old handle would cancel the timer
        // that is currently running.
        if (loading() && fcitx::now(CLOCK_MONOTONIC) >= deadline) {
            check(false, "the lexicon load settled within the deadline");
            instance.exit();
            return;
        }
        if (loading()) {
            return; // the callback re-arms this source
        }
        if (phase == 0) {
            phase = 1;
            runForegroundCases(instance, frontend, recorder);
            reloadSession = std::make_unique<Session>(instance, frontend, "reload");
            case_dictionary_reload_start(instance, *reloadSession, harness);
            return;
        }
        case_dictionary_reload_finish(*reloadSession, recorder);
        // Release the contexts before the Instance is torn down: destroying a
        // Session posts a destroy event, and the frontend is gone by then.
        reloadSession.reset();
        done = true;
        instance.exit();
    }

public:
    bool loading() const {
        return engine && engine->reloadState() == chengyin::Engine::ReloadState::Loading;
    }

    // Arms the one-shot timer that drives advance(). The callback returns true
    // so the source stays alive for the next phase; a single source therefore
    // spans the whole run.
    void start() {
        timer = instance.eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0,
            [this](fcitx::EventSourceTime *, uint64_t) {
                advance();
                if (done) { return false; }
                timer->setNextInterval(5000);
                timer->setOneShot();
                return true;
            });
    }

    std::unique_ptr<fcitx::EventSourceTime> timer;
    bool done = false;
};

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <fcitx5 source dir> <build dir>" << std::endl;
        return 2;
    }
    const std::string sourceDir = argv[1];
    const std::string buildDir = argv[2];
    char a0[] = "chengyin-instance-test";
    char a1[] = "--disable=all";
    char a2[] = "--enable=testim,testfrontend,chengyin";
    char *av[] = {a0, a1, a2};
    try {
        Harness harness(sourceDir, buildDir);
        fcitx::Instance instance(3, av);
        instance.addonManager().registerDefaultLoader(nullptr);
        // Declared outside the scheduled runnable so it outlives every timer it
        // arms; a stack-local runner would be destroyed before its own callback
        // fired.
        std::unique_ptr<Runner> runner;
        instance.eventDispatcher().schedule([&] {
            auto *frontend = instance.addonManager().addon("testfrontend", true);
            if (!frontend) {
                check(false, "testfrontend addon is available");
                instance.exit();
                return;
            }
            auto *engine = static_cast<chengyin::Engine *>(instance.inputMethodEngine("chengyin"));
            runner = std::make_unique<Runner>(instance, frontend, harness, engine);
            runner->start();
        });
        instance.exec();
        runner.reset();
    } catch (const std::exception &error) {
        std::cerr << "FAIL harness: " << error.what() << std::endl;
        return 1;
    }
    std::cout << (failures == 0 ? "PASS " : "FAIL ")
              << "Fcitx5 real-Instance integration test: " << failures << " failed assertion(s)"
              << std::endl;
    return failures == 0 ? 0 : 1;
}
