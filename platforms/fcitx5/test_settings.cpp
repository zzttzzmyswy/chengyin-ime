// SPDX-License-Identifier: GPL-3.0-or-later
// Settings-alignment suite for the Fcitx 5 adapter (iteration I17).
//
// The adapter used to hardcode a nine-row candidate page and never told the
// shared core about the user's preferences at all. This suite drives the real
// Engine through the path the config tool uses -- a RawConfig handed to
// setConfig() -- and asserts what actually reaches the panel and the core:
// candidate widths, the digit keys that address them, the eleven phonetic
// equivalences, the four keyboard-error rules, the association list, and the
// promise that a settings save never disturbs a composition in progress.
//
// The lexicon is authored here, so no assertion depends on data/daily.tsv or on
// the user's own configuration: the fixture is written into a private directory
// and every fcitx path resolves inside it.
//
// The cases run as a step machine under one event-loop turn. The adapter loads
// its lexicon on a worker thread and publishes through the loop, so a case that
// changes DictionaryPath has to wait for that to settle; a case that only changes
// settings does not. Rather than guess which is which, every step runs only once
// the engine is no longer loading.
#include "engine.h"
#include <algorithm>
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

// Twenty single characters for `ma`, weights strictly decreasing, so which row
// holds which character is a property of this fixture alone.
const char *const kMaChars[] = {
    "马", "妈", "麻", "码", "骂", "嘘", "吗", "玛", "埋", "麦",
    "卖", "买", "脉", "蛮", "满", "慢", "忙", "猫", "毛", "贸",
};

std::string maEntries() {
    std::string text;
    int weight = 2900;
    for (const char *character : kMaChars) {
        text += "ma\t";
        text += character;
        text += "\t" + std::to_string(weight) + "\n";
        weight -= 100;
    }
    return text;
}

// One canonical word per matching rule. Every one is spelled so that the rule
// alone decides whether the word is reachable at all: the phonetic words have two
// syllables, so prefix completion cannot stand in for the rule, and the
// keyboard-error words are matched against a form the core does not otherwise
// accept. That is what makes "one switch at a time" a check of the
// option-to-flag mapping rather than of the dictionary.
std::string dictionary() {
    return maEntries() +
           "zhai'hao\t甲\t900\n"
           "chai'hao\t乙\t900\n"
           "shai'hao\t丙\t900\n"
           "nai'hao\t丁\t900\n"
           "hai'hao\t戊\t900\n"
           "lai'hao\t己\t900\n"
           "an'pai\t安排\t950\n"
           "en'pai\t庚\t900\n"
           "in'pai\t辛\t900\n"
           "ian'pai\t壬\t900\n"
           "uan'pai\t癸\t900\n"
           "hao'zhang\t账好\t900\n"
           "zhuang'zhong\t庄重\t900\n"
           "ping'guo\t苹果\t900\n"
           // The shipped association table has a row for 你好.
           "ni'hao\t你好\t960\n";
}

std::string secondDictionary() { return "ma\t新马\t2900\n"; }

class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager) : InputContext(manager, "chengyin-settings-test") {
        created();
        focusIn();
    }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "chengyin-settings-test"; }
    void commitStringImpl(const std::string &value) override { committed += value; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
};

struct TemporaryDirectory {
    TemporaryDirectory() {
        char pattern[] = "/tmp/chengyin-settings-XXXXXX";
        const char *created = ::mkdtemp(pattern);
        if (!created) { throw std::runtime_error("mkdtemp failed"); }
        path = created;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    std::string path;
};

// Drives the adapter the way a frontend does: one key event per character,
// through Engine::keyEvent.
class Driver {
public:
    Driver(chengyin::Engine &engine, Context &context) : engine_(engine), context_(context) {}

    void type(const std::string &input) const {
        for (unsigned char c : input) { press(static_cast<fcitx::KeySym>(c)); }
    }

    bool press(fcitx::KeySym sym) const {
        fcitx::KeyEvent event(&context_, fcitx::Key(sym));
        engine_.keyEvent(entry_, event);
        return event.accepted();
    }

    void pressDigit(int digit) const {
        press(static_cast<fcitx::KeySym>(FcitxKey_1 + digit - 1));
    }

    void escape() const { press(FcitxKey_Escape); }
    void commit() const { press(FcitxKey_space); }
    void pageForward() const { press(FcitxKey_equal); }

    std::string preedit() const { return context_.inputPanel().preedit().toString(); }
    std::string auxUp() const { return context_.inputPanel().auxUp().toString(); }

    std::shared_ptr<fcitx::CandidateList> candidates() const {
        return context_.inputPanel().candidateList();
    }

    // The rows on the page the core is currently showing.
    std::vector<std::string> page() const {
        std::vector<std::string> rows;
        auto list = candidates();
        if (!list) { return rows; }
        for (int i = 0; i < list->size(); ++i) { rows.push_back(list->candidate(i).text().toString()); }
        return rows;
    }

    // Whether a candidate the core can reach for the current composition exists,
    // searched page by page so the assertion does not depend on ranking. Paging
    // only moves the highlight, so the composition itself is left alone.
    bool reachable(const std::string &text) const {
        for (int at = 0; at < 64; ++at) {
            const auto rows = page();
            if (std::find(rows.begin(), rows.end(), text) != rows.end()) { return true; }
            if (rows.empty()) { return false; }
            pageForward();
            if (page() == rows) { return false; }
        }
        return false;
    }

    std::string firstRow() const {
        const auto rows = page();
        return rows.empty() ? std::string() : rows.front();
    }

    const std::string &committed() const { return context_.committed; }
    void clearCommitted() const { context_.committed.clear(); }

private:
    chengyin::Engine &engine_;
    Context &context_;
    const fcitx::InputMethodEntry entry_{"chengyin", "Chengyin", "zh_CN", "chengyin"};
};

} // namespace

int main() {
    TemporaryDirectory directory;
    // Every fcitx path this run touches resolves inside the private directory, so
    // the user's own configuration, lexicon and input history are never read or
    // written.
    const auto configHome = directory.path + "/config";
    if (::setenv("XDG_CONFIG_HOME", configHome.c_str(), 1) != 0 ||
        ::setenv("FCITX_CONFIG_HOME", (configHome + "/fcitx5").c_str(), 1) != 0 ||
        // Learning is on by default, so the data directory has to be private too:
        // otherwise this suite would write the developer's own profile.
        ::setenv("XDG_DATA_HOME", (directory.path + "/data").c_str(), 1) != 0) {
        std::cerr << "FAIL: cannot point fcitx at a private configuration directory" << std::endl;
        return 1;
    }

    const auto lexicon = directory.path + "/settings.tsv";
    const auto secondLexicon = directory.path + "/second.tsv";
    const auto configFile = configHome + "/fcitx5/conf/chengyin.conf";
    writeFile(lexicon, dictionary());
    writeFile(secondLexicon, secondDictionary());

    fcitx::InputContextManager manager;
    fcitx::EventLoop loop;
    // The profile path is injected, so the test never depends on StandardPaths.
    chengyin::Engine engine(manager, loop, "", directory.path + "/data/chengyin/profile.bin");
    Context context(manager);
    Driver driver(engine, context);

    // One mutable configuration is reused for every save, exactly as the config
    // tool's own page state is.
    chengyin::EngineConfig config(lexicon);
    auto save = [&] {
        fcitx::RawConfig raw;
        config.save(raw);
        engine.setConfig(raw);
    };
    // fcitx::Configuration owns a pimpl and is neither copyable nor movable, so
    // the file on disk is read into the caller's own instance.
    auto readPersisted = [&](chengyin::EngineConfig &stored) {
        fcitx::readAsIni(stored, "conf/chengyin.conf");
    };

    std::vector<std::function<void()>> steps;

    // Step 0 publishes the settings, which is also what moves the engine off the
    // built-in demo lexicon onto the fixture. Every later step therefore observes
    // a session the adapter really configured.
    steps.push_back([&] {
        // The first save also moves the engine off the built-in demo lexicon; the
        // step runner waits for that load before running the next step.
        save();
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready,
              "0.1 the fixture lexicon loaded");
        driver.type("ma");
        checkCount(driver.page().size(), 5, "0.2 the fixture lexicon answers (five rows at the default width)");
        driver.escape();
    });
    steps.push_back([&] {
        const auto &settings = engine.settings();
        checkCount(static_cast<size_t>(settings.pageSize), 5, "A.1 the default candidate width is 5");
        check(settings.associations, "A.2 associations default to on");
        checkCount(settings.matching, 0, "A.3 every fuzzy and correction rule defaults to off");
        checkCount(config.switchCount(), 15, "A.4 there are eleven phonetic and four correction switches");
        chengyin::EngineConfig stored(lexicon);
        readPersisted(stored);
        check(stored.pageSizeValue() == 5, "A.5 the default width is what a fresh save persists");
        check(*stored.associations, "A.6 associations are persisted as on");
        checkCount(stored.matchingFlags(), 0, "A.7 no rule is persisted as enabled");
    });
    steps.push_back([&] {
        // Walk the switch list and prove each entry contributes the flag it names:
        // a table wired to the wrong bit fails here, before any behaviour is
        // involved.
        for (size_t i = 0; i < config.switchCount(); ++i) {
            config.clearMatchingSwitches();
            config.selectMatchingSwitch(i);
            checkEqual(std::to_string(config.matchingFlags()), std::to_string(config.switchFlag(i)),
                       std::string("A.8 switch ") + std::to_string(i) + " (" + config.switchRule(i) +
                           ") carries its own bit");
        }
        config.clearMatchingSwitches();
    });

    // --- Case B: the default five-row page and the digit keys that address it. --
    steps.push_back([&] {
        driver.type("ma");
        checkCount(driver.page().size(), 5, "B.1 the first page holds exactly five rows");
        checkEqual(driver.page().front(), kMaChars[0], "B.2 the first page starts at the first candidate");
        // Six through nine address nothing: the core's page has five rows and the
        // list binds only the digits that reach one.
        for (int digit = 6; digit <= 9; ++digit) {
            driver.clearCommitted();
            driver.pressDigit(digit);
            checkEqual(driver.committed(), "",
                       "B.3 digit " + std::to_string(digit) + " selects nothing on a five-row page");
        }
        checkEqual(driver.preedit(), "ma", "B.4 the rejected digits left the composition alone");
        driver.clearCommitted();
        driver.pressDigit(5);
        checkEqual(driver.committed(), kMaChars[4], "B.5 digit 5 selects the fifth row");
        driver.escape();
    });
    steps.push_back([&] {
        driver.clearCommitted();
        driver.type("ma");
        driver.pageForward();
        checkEqual(driver.firstRow(), kMaChars[5], "B.6 paging forward lands on the sixth candidate");
        driver.clearCommitted();
        driver.pressDigit(1);
        checkEqual(driver.committed(), kMaChars[5], "B.7 a digit after paging selects from the displayed page");
        driver.escape();
    });

    // --- Case C: the same behaviour at seven and nine rows. -------------------
    for (const auto width : {chengyin::PageSize::Seven, chengyin::PageSize::Nine}) {
        const int rows = width == chengyin::PageSize::Seven ? 7 : 9;
        const std::string label = "width " + std::to_string(rows);
        steps.push_back([&, rows, label] {
            config.pageSize.setValue(rows == 7 ? chengyin::PageSize::Seven : chengyin::PageSize::Nine);
            save();
        });
        steps.push_back([&, rows, label] {
            driver.escape();
            driver.type("ma");
            checkCount(driver.page().size(), static_cast<size_t>(rows),
                       "C.1 " + label + ": the first page holds that many rows");
            driver.clearCommitted();
            driver.pressDigit(rows);
            checkEqual(driver.committed(), kMaChars[rows - 1],
                       "C.2 " + label + ": the last digit selects the last row");
            driver.escape();
        });
        steps.push_back([&, rows, label] {
            driver.clearCommitted();
            driver.type("ma");
            driver.pageForward();
            checkEqual(driver.firstRow(), kMaChars[rows],
                       "C.3 " + label + ": paging lands on the row after the page");
            driver.clearCommitted();
            driver.pressDigit(1);
            checkEqual(driver.committed(), kMaChars[rows],
                       "C.4 " + label + ": digit 1 selects the row the page shows");
            driver.escape();
        });
    }

    // --- Case D: every switch, one at a time, walked bit by bit. --------------
    struct RuleCase { const char *typed; const char *expected; };
    const std::vector<RuleCase> cases{
        {"zaihao", "甲"},           // 0  zh/z
        {"caihao", "乙"},           // 1  ch/c
        {"saihao", "丙"},           // 2  sh/s
        {"laihao", "丁"},           // 3  n/l
        {"faihao", "戊"},           // 4  f/h
        {"raihao", "己"},           // 5  l/r
        {"angpai", "安排"},     // 6  an/ang
        {"engpai", "庚"},           // 7  en/eng
        {"ingpai", "辛"},           // 8  in/ing
        {"iangpai", "壬"},          // 9  ian/iang
        {"uangpai", "癸"},          // 10 uan/uang
        {"hoa'zhang", "账好"},  // 11 swap
        {"ho'zhang", "账好"},   // 12 omit
        {"hso'zhang", "账好"},  // 13 neighbor
        {"haoo'zhang", "账好"}, // 14 repeat
    };
    steps.push_back([&] {
        checkCount(cases.size(), config.switchCount(), "D.0 every switch has a fixture");
    });
    for (size_t i = 0; i < cases.size(); ++i) {
        const std::string rule = config.switchRule(i);
        const std::string typed = cases[i].typed;
        const std::string expected = cases[i].expected;
        // Only this switch on: the rule's own word must become reachable.
        steps.push_back([&, i] { config.selectMatchingSwitch(i); save(); });
        steps.push_back([&, i, rule, typed, expected] {
            driver.type(typed);
            check(driver.reachable(expected), "D " + rule + ": this switch alone reaches " + expected);
            driver.escape();
        });
        // Every switch off: it must not be reachable, which is what proves the
        // reachability above came from this rule and not from the lexicon.
        steps.push_back([&] { config.clearMatchingSwitches(); save(); });
        steps.push_back([&, rule, typed, expected] {
            driver.type(typed);
            check(!driver.reachable(expected),
                  "D " + rule + ": with every switch off " + expected + " stays out of reach");
            driver.escape();
        });
        // Only a neighbouring rule on: this rule's word must still stay out of
        // reach, so two options cannot be crossed without the walk noticing.
        const size_t neighbour = (i + 1) % cases.size();
        steps.push_back([&, neighbour] { config.selectMatchingSwitch(neighbour); save(); });
        steps.push_back([&, rule, typed, expected, neighbour] {
            driver.type(typed);
            check(!driver.reachable(expected),
                  "D " + rule + ": enabling " + config.switchRule(neighbour) + " instead does not reach it");
            driver.escape();
        });
    }
    steps.push_back([&] { config.clearMatchingSwitches(); save(); });

    // --- Case E: the two lookups the task card names explicitly. ---------------
    steps.push_back([&] { config.selectMatchingSwitch(6); save(); }); // an/ang
    steps.push_back([&] {
        driver.type("angpai");
        checkEqual(driver.firstRow(), "安排", "E.1 an/ang on: typing angpai leads with 安排");
        driver.escape();
    });
    steps.push_back([&] { config.clearMatchingSwitches(); save(); });
    steps.push_back([&] {
        driver.type("angpai");
        check(!driver.reachable("安排"), "E.2 an/ang off: 安排 stays out of reach");
        driver.escape();
    });
    steps.push_back([&] { config.selectMatchingSwitch(0); save(); }); // zh/z
    steps.push_back([&] {
        driver.type("zaihao");
        checkEqual(driver.firstRow(), "甲",
                   "E.3 zh/z on: typing zaihao leads with the zhai'hao entry");
        driver.escape();
    });
    steps.push_back([&] { config.selectMatchingSwitch(6); save(); }); // only an/ang
    steps.push_back([&] {
        driver.type("zaihao");
        check(!driver.reachable("甲"), "E.4 only an/ang on: the zhai'hao entry stays out of reach");
        driver.escape();
        config.clearMatchingSwitches();
    });

    // --- Case F: the association list follows the switch. ---------------------
    steps.push_back([&] { config.associations.setValue(true); save(); });
    steps.push_back([&] {
        driver.clearCommitted();
        driver.type("nihao");
        driver.commit();
        checkEqual(driver.committed(), "你好", "F.1 the word commits");
        checkEqual(driver.auxUp(), "联想 · Tab / 鼠标确认",
                   "F.2 associations on: the continuation list appears");
        driver.escape();
    });
    steps.push_back([&] { config.associations.setValue(false); save(); });
    steps.push_back([&] {
        driver.clearCommitted();
        driver.type("nihao");
        driver.commit();
        checkEqual(driver.committed(), "你好", "F.3 the word still commits with associations off");
        checkEqual(driver.auxUp(), "", "F.4 associations off: no continuation list");
        checkCount(driver.page().size(), 0, "F.5 associations off: no continuation candidates");
        driver.escape();
        config.associations.setValue(true);
    });

    // --- Case G: a save during a composition changes nothing on screen. -------
    steps.push_back([&] {
        driver.type("nihao");
        checkEqual(driver.preedit(), "nihao", "G.1 composition established before the save");
        const auto before = driver.page();
        check(!before.empty(), "G.2 the composition has candidates before the save");
        // A width and a rule change, both of which the core refuses to apply to a
        // busy session.
        config.pageSize.setValue(chengyin::PageSize::Nine);
        config.selectMatchingSwitch(0);
        save();
        checkEqual(driver.preedit(), "nihao", "G.3 the save did not disturb the composition");
        check(driver.page() == before, "G.4 the candidate rows are unchanged by the save");
        driver.clearCommitted();
        driver.pressDigit(1);
        checkEqual(driver.committed(), "你好", "G.5 the pending composition still commits its own word");
        driver.escape();
    });
    steps.push_back([&] {
        // The pending settings are picked up as soon as the session is idle, so
        // the very next composition uses them.
        driver.type("zaihao");
        check(driver.reachable("甲"), "G.6 input after the save uses the new rule");
        driver.escape();
        driver.type("ma");
        checkCount(driver.page().size(), 9, "G.7 input after the save uses the new page width");
        driver.escape();
        config.pageSize.setValue(chengyin::PageSize::Five);
        config.clearMatchingSwitches();
        save();
    });

    // --- Case H: a settings-only save does not rebuild the lexicon. -----------
    steps.push_back([&] {
        config.associations.setValue(false);
        save();
        check(engine.reloadState() != chengyin::Engine::ReloadState::Loading,
              "H.1 a settings-only save never enters the dictionary loading state");
        checkEqual(engine.dictionaryPath(), lexicon, "H.2 the lexicon is the one still in use");
        checkEqual(engine.dictionaryError(), "", "H.3 and it reports no dictionary error");
    });
    steps.push_back([&] {
        config.associations.setValue(true);
        save();
        check(engine.reloadState() != chengyin::Engine::ReloadState::Loading,
              "H.4 and neither does the save that turns it back");
    });
    steps.push_back([&] {
        // A real dictionary change still loads, so the skips above are not simply
        // "setConfig never loads".
        config.dictionaryPath.setValue(secondLexicon);
        save();
        check(engine.reloadState() == chengyin::Engine::ReloadState::Loading,
              "H.5 a changed dictionary path does load");
    });
    steps.push_back([&] {
        driver.type("ma");
        checkEqual(driver.firstRow(), "新马", "H.6 the new lexicon answers after the reload");
        driver.escape();
        config.dictionaryPath.setValue(lexicon);
        save();
    });

    // --- Case I: a profile written before these options existed. --------------
    steps.push_back([&] {
        // Values that differ from the defaults, so "the old file came back to the
        // defaults" is observable rather than a coincidence.
        config.pageSize.setValue(chengyin::PageSize::Nine);
        config.associations.setValue(false);
        config.selectMatchingSwitch(6);
        save();
    });
    steps.push_back([&] {
        // Rewrite the file as an older release would have left it: DictionaryPath
        // and nothing else.
        writeFile(configFile, "DictionaryPath=" + lexicon + "\n");
        engine.reloadConfig();
    });
    steps.push_back([&] {
        const auto &settings = engine.settings();
        checkCount(static_cast<size_t>(settings.pageSize), 5, "I.1 an old profile falls back to the default width");
        check(settings.associations, "I.2 an old profile falls back to associations on");
        checkCount(settings.matching, 0, "I.3 an old profile falls back to every rule off");
        chengyin::EngineConfig fromDisk(lexicon);
        readPersisted(fromDisk);
        checkEqual(*fromDisk.dictionaryPath, lexicon, "I.4 the one option the old file did carry is kept");
    });
    steps.push_back([&] {
        // Saving from that state persists every new option, and reading the file
        // back returns exactly those values.
        config.pageSize.setValue(chengyin::PageSize::Seven);
        config.associations.setValue(false);
        config.clearMatchingSwitches();
        config.selectMatchingSwitch(14); // repeat
        save();
        chengyin::EngineConfig stored(lexicon);
        readPersisted(stored);
        check(stored.pageSizeValue() == 7, "I.5 the chosen width is persisted");
        check(!*stored.associations, "I.6 a disabled association list is persisted");
        check(!*stored.fuzzy->anAng, "I.7 the phonetic rule turned back off is persisted as off");
        check(!*stored.fuzzy->zhZ, "I.8 a rule left alone is persisted as off");
        check(*stored.correction->repeat, "I.9 the one enabled correction rule is persisted");
        check(!*stored.correction->swap, "I.10 a correction rule left alone is persisted as off");
        checkCount(stored.matchingFlags(), CHENGYIN_CORRECT_REPEAT,
                   "I.11 the persisted switches rebuild exactly the expected flag word");
    });

    size_t step = 0;
    const auto deadline = fcitx::now(CLOCK_MONOTONIC) + 60ULL * 1000000ULL;
    auto timer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0,
        [&](fcitx::EventSourceTime *source, uint64_t) {
            if (fcitx::now(CLOCK_MONOTONIC) >= deadline) {
                check(false, "the whole suite finished within the deadline");
                loop.exit();
                return true;
            }
            // A step only runs once the engine has published its lexicon, so a step
            // that changed DictionaryPath waits and a settings-only step does not.
            if (engine.reloadState() != chengyin::Engine::ReloadState::Loading && step < steps.size()) {
                steps[step++]();
            }
            if (step == steps.size()) {
                loop.exit();
            } else {
                source->setNextInterval(1000);
                source->setOneShot();
            }
            return true;
        });
    loop.exec();
    timer.reset();
    check(step == steps.size(), "the whole suite ran",
          std::to_string(step) + " of " + std::to_string(steps.size()) + " steps");

    std::cout << (failures == 0 ? "PASS " : "FAIL ")
              << "Fcitx5 settings-alignment test: " << failures << " failed assertion(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
