// SPDX-License-Identifier: GPL-3.0-or-later
// Mode and Chinese-punctuation suite for the Fcitx 5 adapter (iteration I20).
//
// Before this batch the Linux adapter had no 中/英 mode and no Chinese punctuation
// of its own: a non-letter key went to the core and was then passed through to the
// application, so an ASCII comma stayed an ASCII comma. This suite drives the real
// Engine -- through key events, setConfig(), reloadConfig() and the two status-area
// actions -- and asserts what the feature actually does: the mode and the
// punctuation switch the status area shows, the Chinese punctuation a key produces
// with and without a composition on screen, the Shift tap that switches modes, and
// the promise that none of it trains learning or touches a sensitive field.
//
// The lexicon and every Fcitx path are authored here and resolve inside a private
// temporary directory, so no assertion depends on data/daily.tsv and the
// developer's own configuration, profile and input history are never read or
// written.
#include "engine.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>

namespace {

int failures = 0;

void check(bool ok, const std::string &what, const std::string &detail = {}) {
    std::cout << (ok ? "ok   " : "FAIL ") << what;
    if (!detail.empty()) { std::cout << "  [" << detail << "]"; }
    std::cout << std::endl;
    if (!ok) { ++failures; }
}

void checkEqual(const std::string &actual, const std::string &expected, const std::string &what) {
    check(actual == expected, what, "expected '" + expected + "', got '" + actual + "'");
}

void checkCount(long long actual, long long expected, const std::string &what) {
    check(actual == expected, what,
          "expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

void writeFile(const std::string &path, const std::string &contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
    stream.close();
    if (!stream) { throw std::runtime_error("cannot write " + path); }
}

struct TemporaryDirectory {
    TemporaryDirectory() {
        char pattern[] = "/tmp/chengyin-mode-XXXXXX";
        const char *created = ::mkdtemp(pattern);
        if (!created) { throw std::runtime_error("mkdtemp failed"); }
        path = created;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    std::string path;
};


// The words this suite commits. `ni'hao` is the two-spelling case whose leading row
// the composition assertions end, so which row a commit picks is a property of this
// fixture alone.
std::string dictionary() {
    return "ni'hao\t你好\t3000\n"
           "ni'hao\t拟好\t2000\n"
           "ni\t你\t900\n"
           "hao\t好\t900\n"
           "ma\t马\t2900\n"
           "ma\t妈\t2800\n"
           "shi\t是\t900\n"
           "xi'an\t西安\t800\n";
}

class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager, const char *name)
        : InputContext(manager, name) {
        created();
        focusIn();
    }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "chengyin-mode-test"; }
    // The commit COUNT is what makes "candidate and punctuation arrive as ONE
    // commit" observable: two separate writes that concatenate to the same text
    // would satisfy a string comparison while still letting the host race its own
    // key against the second one, which is exactly what one write avoids.
    void commitStringImpl(const std::string &value) override {
        committed += value;
        ++commitCount;
    }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
    int commitCount = 0;
};

// The key syms for the ASCII punctuation the shared table converts. Fcitx normalises
// a symbol to its unshifted keysym, so these are the forms an XKB-aware frontend
// delivers even for the symbols that need Shift on a US layout.
constexpr fcitx::KeySym kComma = FcitxKey_comma;
constexpr fcitx::KeySym kPeriod = FcitxKey_period;
constexpr fcitx::KeySym kApostrophe = FcitxKey_apostrophe;
constexpr fcitx::KeySym kDoubleQuote = FcitxKey_quotedbl;
constexpr fcitx::KeySym kUnderscore = FcitxKey_underscore;
constexpr fcitx::KeySym kCaret = FcitxKey_asciicircum;
constexpr fcitx::KeySym kColon = FcitxKey_colon;
constexpr fcitx::KeySym kSlash = FcitxKey_slash;

// Drives the adapter the way a frontend does: one key event per press or release,
// through Engine::keyEvent. A release is what completes a Shift tap, so both
// directions have to be expressible.
class Driver {
public:
    Driver(chengyin::Engine &engine, Context &context) : engine_(engine), context_(context) {}

    bool press(fcitx::KeySym sym, fcitx::KeyStates states = fcitx::KeyStates()) const {
        fcitx::KeyEvent event(&context_, fcitx::Key(sym, states));
        engine_.keyEvent(entry_, event);
        return event.accepted();
    }

    bool release(fcitx::KeySym sym, fcitx::KeyStates states = fcitx::KeyStates()) const {
        fcitx::KeyEvent event(&context_, fcitx::Key(sym, states), true);
        engine_.keyEvent(entry_, event);
        return event.accepted();
    }

    // A tap: press and release with nothing in between, which is the gesture the
    // Shift switch is defined by. Returns what the release itself did, which is
    // what "the tap was consumed" means.
    bool tap(fcitx::KeySym sym) const {
        press(sym);
        return release(sym);
    }

    bool type(const std::string &input) const {
        bool accepted = true;
        for (unsigned char c : input) {
            if (!press(static_cast<fcitx::KeySym>(c))) { accepted = false; }
        }
        return accepted;
    }

    std::string preedit() const { return context_.inputPanel().preedit().toString(); }
    std::string auxUp() const { return context_.inputPanel().auxUp().toString(); }
    const std::string &committed() const { return context_.committed; }
    // How many separate writes the adapter made. A merged commit is one; a commit
    // followed by a second one carrying the punctuation is two.
    int commitCount() const { return context_.commitCount; }
    void clearCommitted() const {
        context_.committed.clear();
        context_.commitCount = 0;
    }

    std::shared_ptr<fcitx::CandidateList> candidates() const {
        return context_.inputPanel().candidateList();
    }

    // Bounded, like every other store wait in this tree: the input path must never
    // block on the learning store, so only the test ever waits for it.
    void flushLearning(uint64_t timeoutMs) const { engine_.flushLearning(timeoutMs); }

private:
    chengyin::Engine &engine_;
    Context &context_;
    const fcitx::InputMethodEntry entry_{"chengyin", "Chengyin", "zh_CN", "chengyin"};
};

} // namespace

int main() {
    TemporaryDirectory directory;
    // Every Fcitx path this run touches resolves inside the private directory, so
    // the user's own configuration, lexicon, profile and input history are never
    // read or written. Learning is on by default, so the data directory has to be
    // private too.
    const auto configHome = directory.path + "/config";
    if (::setenv("XDG_CONFIG_HOME", configHome.c_str(), 1) != 0 ||
        ::setenv("FCITX_CONFIG_HOME", (configHome + "/fcitx5").c_str(), 1) != 0 ||
        ::setenv("XDG_DATA_HOME", (directory.path + "/data").c_str(), 1) != 0) {
        std::cerr << "FAIL: cannot point fcitx at a private configuration directory" << std::endl;
        return 1;
    }
    const auto lexicon = directory.path + "/mode.tsv";
    const auto configFile = configHome + "/fcitx5/conf/chengyin.conf";
    writeFile(lexicon, dictionary());

    fcitx::InputContextManager manager;
    fcitx::EventLoop loop;
    chengyin::Engine engine(manager, loop, "", directory.path + "/data/chengyin/profile.bin");
    Context context(manager, "chengyin-mode");
    Driver driver(engine, context);

    // One mutable configuration reused for every save, exactly as the config tool's
    // own page state is.
    chengyin::EngineConfig config(lexicon);
    auto save = [&] {
        fcitx::RawConfig raw;
        config.save(raw);
        engine.setConfig(raw);
    };
    auto readPersisted = [&](chengyin::EngineConfig &stored) {
        fcitx::readAsIni(stored, "conf/chengyin.conf");
    };

    std::vector<std::function<void()>> steps;

    // --- Case A: the defaults, and the two status-area actions on them. --------
    steps.push_back([&] {
        // Learning is off for the whole suite except the two cases that are about
        // it. The reason is that learning reorders candidates, and a case asserting
        // "this key commits that row" must not depend on what an earlier case
        // trained -- exactly the coupling the persistent-learning suite avoids by
        // making one case per spelling.
        config.learning.setValue(false);
        save();
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready,
              "A.1 the fixture lexicon loaded");
        driver.clearCommitted();
        check(driver.press(kComma), "A.2 a comma with no composition is consumed");
        checkEqual(driver.committed(), "，", "A.3 and commits the Chinese comma");
        driver.clearCommitted();
        check(driver.press(kPeriod), "A.4b a period with no composition is consumed");
        checkEqual(driver.committed(), "。", "A.4c and commits the Chinese period");
        // The three options and their Windows defaults, read back off the settings
        // snapshot the adapter actually published.
        const auto &settings = engine.settings();
        check(!settings.defaultEnglish, "A.4 DefaultEnglish defaults to off");
        check(settings.shiftSwitch == chengyin::ShiftSwitch::Left,
              "A.5 ShiftSwitch defaults to 左 Shift");
        check(settings.chinesePunctuation, "A.6 ChinesePunctuation defaults to on");
        check(!engine.english(&context), "A.7 a fresh context starts in Chinese mode");
        check(engine.chinesePunctuation(&context), "A.8 and with Chinese punctuation on");
    });

    // --- Case B: the standalone punctuation path, with nothing on screen. ------
    steps.push_back([&] {
        struct Case { fcitx::KeySym sym; const char *expected; const char *label; };
        const Case cases[] = {
            {kComma, "，", "comma"},
            {kPeriod, "。", "period"},
            {kColon, "：", "colon"},
            {kSlash, "、", "slash"},
            {kUnderscore, "——", "underscore"},
            {kCaret, "……", "caret"},
        };
        for (const auto &one : cases) {
            driver.clearCommitted();
            check(driver.press(one.sym), std::string("B ") + one.label + " is consumed");
            checkEqual(driver.committed(), one.expected,
                       std::string("B ") + one.label + " commits its Chinese form");
        }
        // The paired quotes alternate, and the two pairs are independent.
        const fcitx::KeySym quotes[] = {kDoubleQuote, kDoubleQuote, kDoubleQuote};
        const char *expected[] = {"“", "”", "“"};
        for (int i = 0; i < 3; ++i) {
            driver.clearCommitted();
            check(driver.press(quotes[i]), std::string("B double quote ") + std::to_string(i) + " is consumed");
            checkEqual(driver.committed(), expected[i],
                       std::string("B double quote ") + std::to_string(i) + " alternates");
        }
        driver.clearCommitted();
        driver.press(kApostrophe);
        checkEqual(driver.committed(), "‘", "B the single quote opens the single pair");
        driver.clearCommitted();
        driver.press(kApostrophe);
        checkEqual(driver.committed(), "’", "B the single quote closes it");
        // A fresh context, so "independent" means the double pair rather than the
        // state the double-quote walk above left behind.
        Context pairs(manager, "chengyin-mode-pairs");
        Driver pairDriver(engine, pairs);
        pairDriver.press(kApostrophe);
        pairDriver.clearCommitted();
        pairDriver.press(kDoubleQuote);
        checkEqual(pairDriver.committed(), "“", "B the double pair is independent of the single one");
    });

    // --- Case C: a composition, and the punctuation that ends it. --------------
    steps.push_back([&] {
        driver.clearCommitted();
        driver.type("nihao");
        checkEqual(driver.preedit(), "nihao", "C.1 the composition is established");
        check(driver.press(kComma), "C.2 a comma during a composition is consumed");
        checkEqual(driver.committed(), "你好，", "C.3 candidate and punctuation arrive together");
        checkCount(driver.commitCount(), 1,
                   "C.3b and in exactly ONE write, not a commit followed by a second one");
        checkEqual(driver.preedit(), "", "C.4 the composition is cleared");
        checkCount(static_cast<long long>(driver.candidates() ? driver.candidates()->size() : 0), 0,
                   "C.5 and the candidate list is gone");
    });
    steps.push_back([&] {
        // The same for a period, and the raw spelling is not what was committed.
        driver.clearCommitted();
        driver.type("ma");
        driver.press(kPeriod);
        checkEqual(driver.committed(), "马。", "C.6 a period ends the composition the same way");
        checkCount(driver.commitCount(), 1, "C.6b and in one write");
        checkCount(driver.commitCount(), 1, "C.6b also in one write");
        driver.clearCommitted();
        // The second row is reachable, so the commit above came from the core's own
        // selection and not from a hardcoded candidate.
        driver.type("nihao");
        driver.press(static_cast<fcitx::KeySym>(FcitxKey_2));
        checkEqual(driver.committed(), "拟好", "C.7 the second row still commits on its own");
    });
    steps.push_back([&] {
        // An association list counts as "something on screen" too: the punctuation
        // closes it and commits on its own, with no candidate dragged along.
        driver.clearCommitted();
        driver.type("nihao");
        driver.press(FcitxKey_space);
        checkEqual(driver.committed(), "你好", "C.8 the word commits");
        checkEqual(driver.auxUp(), "联想 · Tab / 鼠标确认", "C.9 the association list is showing");
        driver.clearCommitted();
        check(driver.press(kComma), "C.10 a comma closes the association list");
        checkEqual(driver.committed(), "，", "C.11 and commits only the punctuation");
        checkCount(driver.commitCount(), 1, "C.11b in one write");
        checkEqual(driver.auxUp(), "", "C.12 the association list is gone");
    });
    steps.push_back([&] {
        // The apostrophe inside a composition is the syllable separator, never ‘.
        driver.clearCommitted();
        driver.type("xi'an");
        checkEqual(driver.preedit(), "xi'an", "C.13 the apostrophe splits syllables, not the quote pair");
        checkEqual(driver.committed(), "", "C.14 and commits nothing on its own");
        driver.press(FcitxKey_space);
        checkEqual(driver.committed(), "西安", "C.15 the split spelling commits its own word");
    });

    // --- Case D: the switches that turn the conversion off. -------------------
    steps.push_back([&] {
        // Chinese punctuation off: the punctuation passes through untouched, and the
        // composition is still ended by the core (which commits its candidate).
        config.chinesePunctuation.setValue(false);
        save();
    });
    steps.push_back([&] {
        driver.clearCommitted();
        check(!driver.press(kComma), "D.1 punctuation off: a standalone comma passes through");
        checkEqual(driver.committed(), "", "D.2 and commits nothing");
        driver.clearCommitted();
        driver.type("nihao");
        check(!driver.press(kComma), "D.3 punctuation off: the comma during a composition passes through");
        checkEqual(driver.committed(), "你好",
                   "D.4 the candidate is still committed by the core, the comma is the host's");
        checkEqual(driver.preedit(), "", "D.5 and the composition ended");
        config.chinesePunctuation.setValue(true);
        save();
    });
    steps.push_back([&] {
        // English mode: nothing at all is processed, letters included.
        engine.toggleEnglish(&context);
        check(engine.english(&context), "D.6 the status action switched to English");
        driver.clearCommitted();
        check(!driver.press(kComma), "D.7 English: the punctuation passes through");
        checkEqual(driver.committed(), "", "D.8 English: nothing is committed");
        check(!driver.type("nihao"), "D.9 English: letters are not accepted either");
        checkEqual(driver.preedit(), "", "D.10 English: no composition is established");
        // Back to Chinese, and the conversion works again.
        engine.toggleEnglish(&context);
        check(!engine.english(&context), "D.11 the status action switched back to Chinese");
        driver.clearCommitted();
        driver.press(kComma);
        checkEqual(driver.committed(), "，", "D.12 Chinese punctuation works again");
    });
    steps.push_back([&] {
        // A shortcut chord is the application's, not ours, in either mode.
        struct Chord { fcitx::KeyState state; const char *name; };
        for (const auto &chord : {Chord{fcitx::KeyState::Ctrl, "Ctrl"},
                                  Chord{fcitx::KeyState::Alt, "Alt"},
                                  Chord{fcitx::KeyState::Super, "Super"}}) {
            driver.clearCommitted();
            check(!driver.press(kComma, fcitx::KeyStates(chord.state)),
                  std::string("D.13 ") + chord.name + "+comma passes through");
            checkEqual(driver.committed(), "", "D.14 and commits nothing");
        }
        // Caps Lock likewise: Windows refuses to convert under the lock.
        driver.clearCommitted();
        check(!driver.press(kComma, fcitx::KeyState::CapsLock), "D.15 Caps Lock + comma passes through");
        checkEqual(driver.committed(), "", "D.16 and commits nothing");
    });

    // --- Case E: DefaultEnglish, and what a settings change does to a context. -
    steps.push_back([&] {
        config.defaultEnglish.setValue(true);
        save();
    });
    steps.push_back([&] {
        // A NEW context starts in English; the one already alive keeps whatever mode
        // it is in, which is what the Windows settings text promises.
        check(!engine.english(&context), "E.1 the existing context keeps its mode across the save");
        Context fresh(manager, "chengyin-mode-fresh");
        Driver freshDriver(engine, fresh);
        check(engine.english(&fresh), "E.2 a new context starts in English");
        check(!freshDriver.type("ni"), "E.3 a new English context does not compose");
        checkEqual(freshDriver.preedit(), "", "E.4 no preedit is established");
        // The context that was already alive is untouched by the save.
        driver.clearCommitted();
        check(driver.type("ni"), "E.5 the existing context still composes");
        checkEqual(driver.preedit(), "ni", "E.6 with its own mode");
        driver.press(FcitxKey_Escape);
        config.defaultEnglish.setValue(false);
        save();
    });

    // --- Case F: the Shift tap. -----------------------------------------------
    steps.push_back([&] {
        // The default setting is 左 Shift.
        check(engine.settings().shiftSwitch == chengyin::ShiftSwitch::Left,
              "F.1 the setting is 左 Shift");
        check(!engine.english(&context), "F.2 starting in Chinese");
        check(driver.tap(FcitxKey_Shift_L), "F.3 the tap release is consumed");
        check(engine.english(&context), "F.4 a tapped left Shift switches to English");
        driver.tap(FcitxKey_Shift_L);
        check(!engine.english(&context), "F.5 tapping it again switches back");
    });
    steps.push_back([&] {
        // A Shift chord does not switch: press Shift, press a letter, release both.
        check(!engine.english(&context), "F.6 Chinese to start");
        driver.press(FcitxKey_Shift_L);
        // Shift+N still composes: the letter's own path is untouched, and the
        // framework integration suite records the same behaviour (case 15.7).
        check(driver.press(FcitxKey_n, fcitx::KeyState::Shift), "F.7 Shift+N still composes");
        checkEqual(driver.preedit(), "n", "F.8 the letter extends the preedit");
        driver.release(FcitxKey_n);
        check(!driver.release(FcitxKey_Shift_L), "F.9 the Shift release does not switch");
        check(!engine.english(&context), "F.10 a Shift chord leaves the mode alone");
        driver.press(FcitxKey_Escape);
    });
    steps.push_back([&] {
        // A repeat cancels the tap, the way Windows never arms on a held key.
        driver.press(FcitxKey_Shift_L);
        driver.press(FcitxKey_Shift_L, fcitx::KeyState::Repeat);
        check(!driver.release(FcitxKey_Shift_L), "F.11 a repeated Shift release does not switch");
        check(!engine.english(&context), "F.12 the mode is unchanged");
    });
    steps.push_back([&] {
        // Under 左 Shift the right key is not part of the gesture at all.
        driver.tap(FcitxKey_Shift_R);
        check(!engine.english(&context), "F.13 the right Shift does not switch under 左 Shift");
        // Under 左右 Shift it does.
        config.shiftSwitch.setValue(chengyin::ShiftSwitch::Both);
        save();
    });
    steps.push_back([&] {
        driver.tap(FcitxKey_Shift_R);
        check(engine.english(&context), "F.14 under 左右 Shift the right key switches");
        driver.tap(FcitxKey_Shift_R);
        check(!engine.english(&context), "F.15 and switches back");
        driver.tap(FcitxKey_Shift_L);
        check(engine.english(&context), "F.16 the left key still works there");
        driver.tap(FcitxKey_Shift_L);
        check(!engine.english(&context), "F.17 back to Chinese");
        // 不使用 Shift: neither key switches.
        config.shiftSwitch.setValue(chengyin::ShiftSwitch::Disabled);
        save();
    });
    steps.push_back([&] {
        driver.tap(FcitxKey_Shift_L);
        check(!engine.english(&context), "F.18 不使用 Shift: the left key does not switch");
        driver.tap(FcitxKey_Shift_R);
        check(!engine.english(&context), "F.19 nor does the right one");
        driver.clearCommitted();
        driver.press(kComma);
        checkEqual(driver.committed(), "，", "F.20 punctuation is unaffected by the Shift setting");
        config.shiftSwitch.setValue(chengyin::ShiftSwitch::Left);
        save();
    });
    steps.push_back([&] {
        // A tap with a composition on screen discards it: Windows' toggleEnglish()
        // calls unbind(), which resets the session, so the half-typed syllable is
        // dropped rather than committed as Latin letters.
        driver.clearCommitted();
        driver.type("nihao");
        checkEqual(driver.preedit(), "nihao", "F.21 composition established before the tap");
        driver.tap(FcitxKey_Shift_L);
        check(engine.english(&context), "F.22 the tap switched to English");
        checkEqual(driver.preedit(), "", "F.23 and the composition was discarded");
        checkEqual(driver.committed(), "", "F.24 no raw spelling was typed out");
        driver.tap(FcitxKey_Shift_L);
        check(!engine.english(&context), "F.25 back to Chinese");
    });
    steps.push_back([&] {
        // The status-area actions read the context's own state back out, and clicking
        // them is what a status-area click does. F.25 just switched back to Chinese,
        // so the actions were republished by that switch and describe it now.
        checkEqual(engine.modeAction().shortText(&context), "中", "F.26 the mode action reads 中");
        check(engine.modeAction().isChecked(&context), "F.27 and reports itself checked");
        engine.modeAction().activate(&context);
        check(engine.english(&context), "F.28 activating the action switches to English");
        checkEqual(engine.modeAction().shortText(&context), "英", "F.29 the action now reads 英");
        engine.modeAction().activate(&context);
        check(!engine.english(&context), "F.30 activating it again switches back");

        checkEqual(engine.punctuationAction().shortText(&context), "中文标点",
                   "F.31 the punctuation action reads 中文标点");
        checkEqual(engine.punctuationAction().icon(&context), "fcitx-punc-active",
                   "F.32 with the active icon");
        engine.punctuationAction().activate(&context);
        check(!engine.chinesePunctuation(&context), "F.33 activating it turns the conversion off");
        checkEqual(engine.punctuationAction().shortText(&context), "英文标点",
                   "F.34 the action now reads 英文标点");
        checkEqual(engine.punctuationAction().icon(&context), "fcitx-punc-inactive",
                   "F.35 with the inactive icon");
        driver.clearCommitted();
        check(!driver.press(kComma), "F.36 and a comma now passes through");
        engine.punctuationAction().activate(&context);
        check(engine.chinesePunctuation(&context), "F.37 activating it again turns it back on");
    });

    // --- Case G: learning, and the words it must never record. ----------------
    steps.push_back([&] { config.learning.setValue(true); save(); });
    steps.push_back([&] {
        // A punctuation key that ends a composition is not a selection, so it must
        // not be learned. The profile therefore stays exactly as it was.
        const auto before = engine.profileCount();
        const auto revision = engine.profileRevision();
        driver.clearCommitted();
        driver.type("nihao");
        driver.press(kComma);
        checkEqual(driver.committed(), "你好，", "G.1 the composition commits with its punctuation");
        checkCount(driver.commitCount(), 1, "G.1b merged into a single write");
        checkCount(engine.profileCount(), before,
                   "G.2 punctuation ending a composition records nothing");
        checkCount(engine.profileRevision(), revision, "G.3 and does not advance the profile");
        driver.flushLearning(2000);
        // The comparison the case is really about: choosing the same row with the
        // keyboard DOES record, so G.2 is not simply "learning never happens here".
        driver.clearCommitted();
        driver.type("nihao");
        driver.press(FcitxKey_space);
        checkEqual(driver.committed(), "你好", "G.4 the word commits by selection");
        check(engine.profileCount() > before, "G.5 a real selection is recorded",
              std::to_string(before) + " -> " + std::to_string(engine.profileCount()));
    });

    // --- Case H: a sensitive field. -------------------------------------------
    steps.push_back([&] {
        context.setCapabilityFlags(fcitx::CapabilityFlag::Password);
        const auto before = engine.profileCount();
        driver.clearCommitted();
        check(!driver.press(kComma), "H.1 a password field never converts punctuation");
        checkEqual(driver.committed(), "", "H.2 and commits nothing");
        check(!driver.type("nihao"), "H.3 nor composes");
        checkEqual(driver.preedit(), "", "H.4 no preedit in a password field");
        // A Shift tap in a sensitive field is not a mode switch either.
        const bool mode = engine.english(&context);
        driver.tap(FcitxKey_Shift_L);
        check(engine.english(&context) == mode, "H.5 a Shift tap in a password field does not switch");
        driver.tap(FcitxKey_Shift_R);
        check(engine.english(&context) == mode, "H.6 neither does the right key");
        checkCount(engine.profileCount(), before, "H.7 a password field records nothing");
        checkCount(static_cast<long long>(driver.candidates() ? driver.candidates()->size() : 0), 0,
                   "H.8 and shows no candidates");
        context.setCapabilityFlags(fcitx::CapabilityFlag());
        driver.clearCommitted();
        driver.press(kComma);
        checkEqual(driver.committed(), "，", "H.9 conversion works again once the field is not sensitive");
    });

    // --- Case I: reset and focus loss. ----------------------------------------
    steps.push_back([&] {
        // Establish a half-open pair, then reset: the pair state goes with the
        // composition, but the mode does not.
        driver.clearCommitted();
        driver.press(kDoubleQuote);
        checkEqual(driver.committed(), "“", "I.1 the pair is open");
        engine.toggleEnglish(&context);
        check(engine.english(&context), "I.2 switched to English before the reset");
        fcitx::InputContextEvent reset(&context, fcitx::EventType::InputContextReset);
        engine.reset(fcitx::InputMethodEntry("chengyin", "Chengyin", "zh_CN", "chengyin"), reset);
        check(engine.english(&context), "I.3 reset does not change the mode");
        engine.toggleEnglish(&context);
        check(!engine.english(&context), "I.4 back to Chinese");
        driver.clearCommitted();
        driver.press(kDoubleQuote);
        checkEqual(driver.committed(), "“", "I.5 the pair state was reset to the opening half");
        // And the punctuation switch survives a focus change too.
        engine.punctuationAction().activate(&context);
        check(!engine.chinesePunctuation(&context), "I.6 the punctuation switch is off");
        fcitx::InputContextEvent out(&context, fcitx::EventType::InputContextFocusOut);
        engine.deactivate(fcitx::InputMethodEntry("chengyin", "Chengyin", "zh_CN", "chengyin"), out);
        check(!engine.chinesePunctuation(&context), "I.7 focus loss does not change the punctuation switch");
        check(!engine.english(&context), "I.8 nor the mode");
        engine.punctuationAction().activate(&context);
        check(engine.chinesePunctuation(&context), "I.9 switched back on");
    });

    // --- Case J: an old profile, and what a settings-only save skips. ---------
    steps.push_back([&] {
        // Values that differ from the defaults, so "the old file fell back to the
        // defaults" is observable rather than a coincidence.
        config.defaultEnglish.setValue(true);
        config.shiftSwitch.setValue(chengyin::ShiftSwitch::Both);
        config.chinesePunctuation.setValue(false);
        save();
    });
    steps.push_back([&] {
        // Rewrite the file as an older release would have left it: DictionaryPath and
        // nothing else.
        writeFile(configFile, "DictionaryPath=" + lexicon + "\n");
        engine.reloadConfig();
    });
    steps.push_back([&] {
        const auto &settings = engine.settings();
        check(!settings.defaultEnglish, "J.1 an old profile falls back to DefaultEnglish off");
        check(settings.shiftSwitch == chengyin::ShiftSwitch::Left,
              "J.2 and to 左 Shift");
        check(settings.chinesePunctuation, "J.3 and to Chinese punctuation on");
        chengyin::EngineConfig fromDisk(lexicon);
        readPersisted(fromDisk);
        checkEqual(*fromDisk.dictionaryPath, lexicon, "J.4 the one option the old file did carry is kept");
        // Saving from that state persists all three, and reading the file back
        // returns exactly those values.
        config.defaultEnglish.setValue(true);
        config.shiftSwitch.setValue(chengyin::ShiftSwitch::Disabled);
        config.chinesePunctuation.setValue(false);
        save();
        chengyin::EngineConfig stored(lexicon);
        readPersisted(stored);
        check(*stored.defaultEnglish, "J.5 DefaultEnglish is persisted");
        check(*stored.shiftSwitch == chengyin::ShiftSwitch::Disabled, "J.6 the Shift setting is persisted");
        check(!*stored.chinesePunctuation, "J.7 the punctuation switch is persisted");
    });
    steps.push_back([&] {
        // A save that moves only these three options is a settings-only save: the
        // lexicon is not rebuilt, which is what keeps a punctuation toggle from
        // costing the 0.4 s vocabulary reload.
        check(engine.reloadState() != chengyin::Engine::ReloadState::Loading,
              "J.8 the settings-only save never entered the loading state");
        checkEqual(engine.dictionaryPath(), lexicon, "J.9 the lexicon is the one still in use");
        checkEqual(engine.dictionaryError(), "", "J.10 and reports no dictionary error");
        config.defaultEnglish.setValue(false);
        config.shiftSwitch.setValue(chengyin::ShiftSwitch::Left);
        config.chinesePunctuation.setValue(true);
        save();
    });
    steps.push_back([&] {
        // A real dictionary change still loads, so J.8 is not simply "setConfig never
        // loads".
        config.dictionaryPath.setValue(directory.path + "/other.tsv");
        save();
        check(engine.reloadState() == chengyin::Engine::ReloadState::Loading,
              "J.11 a changed dictionary path does load");
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Failed,
              "J.12 the missing lexicon failed the load");
        check(!engine.dictionaryError().empty(), "J.12b and reports why");
        config.dictionaryPath.setValue(lexicon);
        save();
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready,
              "J.13 the lexicon is back");
        driver.clearCommitted();
        driver.press(kComma);
        checkEqual(driver.committed(), "，", "J.14 punctuation still works at the end of the run");
    });

    // The step machine: one step per event-loop turn, and a step only runs once the
    // background lexicon load has published. Several steps change DictionaryPath and
    // call reloadConfig(), so this is what keeps every assertion looking at the
    // engine the step believes it is looking at.
    size_t step = 0;
    const auto deadline = fcitx::now(CLOCK_MONOTONIC) + 90ULL * 1000000ULL;
    auto timer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0,
        [&](fcitx::EventSourceTime *source, uint64_t) {
            if (fcitx::now(CLOCK_MONOTONIC) >= deadline) {
                check(false, "the whole suite finished within the deadline");
                loop.exit();
                return true;
            }
            if (engine.reloadState() != chengyin::Engine::ReloadState::Loading && step < steps.size()) {
                steps[step++]();
            }
            if (step == steps.size()) {
                loop.exit();
            } else {
                source->setNextInterval(2000);
                source->setOneShot();
            }
            return true;
        });
    loop.exec();
    timer.reset();
    check(step == steps.size(), "the whole suite ran",
          std::to_string(step) + " of " + std::to_string(steps.size()) + " steps");

    std::cout << (failures == 0 ? "PASS " : "FAIL ")
              << "Fcitx5 mode/punctuation test: " << failures << " failed assertion(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
