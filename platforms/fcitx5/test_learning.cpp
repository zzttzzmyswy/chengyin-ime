// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent-learning suite for the Fcitx 5 adapter (iteration I19).
//
// Until this batch the Linux adapter never recorded a selection: the learning bit
// of chengyin_session_configure was sent as the core's own default and
// chengyin_session_learn_commit was never called, so candidate ordering was the
// same on every run. This suite drives the real Engine -- through key events,
// setConfig(), reloadConfig() and the injected profile path -- and asserts what
// learning actually does: the ordering follows the user's choices, survives a
// restart, is shared between input contexts, is switched off by the Learning
// option, never trains a sensitive field, and never overwrites a stored file it
// could not read.
//
// Every path this run touches is private: the profile path is injected into a
// temporary directory and the fcitx XDG directories are redirected into it, so
// the developer's own profile is never read or written.
#include "engine.h"
#include <algorithm>
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
#include <vector>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <sys/stat.h>

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

std::string readFile(const std::string &path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

struct TemporaryDirectory {
    TemporaryDirectory() {
        char pattern[] = "/tmp/chengyin-learning-XXXXXX";
        const char *created = ::mkdtemp(pattern);
        if (!created) { throw std::runtime_error("mkdtemp failed"); }
        path = created;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    std::string path;
};

// Four words spelled `ma`, plus two spelled `ni'hao`, so a selection can change
// the leading row without any of them being reachable by prefix completion. The
// weight order decides the unlearned ranking and is a property of this fixture
// alone.
std::string dictionary() {
    return "ma\t马\t2900\n"
           "ma\t妈\t2800\n"
           "ma\t麻\t2700\n"
           "ma\t码\t2600\n"
           // 骂 is trained by no other case, so a promotion assertion never has
           // to guess how many selections an already-trained word needs.
           "ma\t骂\t2500\n"
           // A spelling no earlier case trains: promotion among learned rows is
           // decided by (count, sequence), so re-testing a spelling an earlier case
           // already selected many times would make the assertion depend on that
           // count rather than on the switch this case is about.
           // Two spellings only the stale-baseline case uses, so a promotion there
           // can never be confused with what an earlier case trained.
           "lan\t蓝\t2400\n"
           "lan\t兰\t2390\n"
           "yun\t云\t2380\n"
           "yun\t运\t2370\n"
           // Case J commits this key from a stale baseline, so it must be a key
           // no other case has ever learned a row for.
           "wu\t吴\t2360\n"
           "wu\t武\t2350\n"
           "hao\t好\t2450\n"
           "hao\t号\t2440\n"
           "ni'hao\t你好\t950\n"
           "ni'hao\t拟好\t940\n";
}

long long modeOf(const std::string &path) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) { return -1; }
    return static_cast<long long>(info.st_mode & 07777);
}

class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager, const char *program)
        : InputContext(manager, program) {
        created();
        focusIn();
    }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "chengyin-learning-test"; }
    void commitStringImpl(const std::string &value) override { committed += value; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
};

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

    void escape() const { press(FcitxKey_Escape); }

    std::vector<std::string> page() const {
        std::vector<std::string> rows;
        auto list = context_.inputPanel().candidateList();
        if (!list) { return rows; }
        for (int i = 0; i < list->size(); ++i) { rows.push_back(list->candidate(i).text().toString()); }
        return rows;
    }

    std::string firstRow() const {
        const auto rows = page();
        return rows.empty() ? std::string() : rows.front();
    }

    // Selects a word by name rather than by row: once learning moves it, the row it
    // sits on is no longer the point of the test. Pages forward when the word is not
    // on the first page, so a fifth `ma` word is still reachable.
    bool choose(const std::string &word) const {
        for (int turn = 0; turn < 8; ++turn) {
            const auto rows = page();
            const auto at = std::find(rows.begin(), rows.end(), word);
            if (at != rows.end()) {
                type(std::to_string(static_cast<int>(at - rows.begin()) + 1));
                return true;
            }
            if (rows.empty()) { return false; }
            press(FcitxKey_equal); // the adapter maps '=' to page forward
            if (page() == rows) { return false; }
        }
        return false;
    }

    void clearCommitted() const { context_.committed.clear(); }
    const std::string &committed() const { return context_.committed; }

private:
    chengyin::Engine &engine_;
    Context &context_;
    const fcitx::InputMethodEntry entry_{"chengyin", "Chengyin", "zh_CN", "chengyin"};
};

} // namespace

int main() {
    TemporaryDirectory directory;
    const auto configHome = directory.path + "/config";
    const auto dataHome = directory.path + "/data";
    const auto profilePath = dataHome + "/chengyin/profile.bin";
    const auto lexicon = directory.path + "/learning.tsv";
    const auto configFile = configHome + "/fcitx5/conf/chengyin.conf";
    // Every fcitx path this run touches resolves inside the temporary tree, and the
    // profile path is injected besides, so neither the engine nor StandardPaths can
    // reach the developer's own data directory.
    if (::setenv("XDG_CONFIG_HOME", configHome.c_str(), 1) != 0 ||
        ::setenv("FCITX_CONFIG_HOME", (configHome + "/fcitx5").c_str(), 1) != 0 ||
        ::setenv("XDG_DATA_HOME", dataHome.c_str(), 1) != 0) {
        std::cerr << "FAIL: cannot point fcitx at a private data directory" << std::endl;
        return 1;
    }
    writeFile(lexicon, dictionary());

    fcitx::InputContextManager manager;
    fcitx::EventLoop loop;
    chengyin::Engine engine(manager, loop, "", profilePath);
    Context a(manager, "learning-a"), b(manager, "learning-b"), c(manager, "learning-c");
    Driver driverA(engine, a), driverB(engine, b), driverC(engine, c);

    chengyin::EngineConfig config(lexicon);
    auto save = [&] {
        fcitx::RawConfig raw;
        config.save(raw);
        engine.setConfig(raw);
    };
    auto storedBytes = [&] { return readFile(profilePath); };
    // The engine learns into memory immediately and persists on its own thread, so
    // the one thing a test must not do is read the file before that thread has been
    // given its turn.
    auto flush = [&] { return engine.flushLearning(5000); };
    // Types a spelling, picks a named candidate (never a row number -- learning is
    // what moves rows), and commits it the way a user does.
    auto chooseWord = [&](Driver &driver, const std::string &spelling, const std::string &word) {
        driver.type(spelling);
        driver.clearCommitted();
        driver.choose(word);
        driver.press(FcitxKey_space);
    };
    auto firstRowOf = [&](Driver &driver, const std::string &spelling) {
        driver.type(spelling);
        const auto row = driver.firstRow();
        driver.escape();
        return row;
    };

    std::vector<std::function<void()>> steps;
    // --- Case 0: the fixture lexicon is in use and nothing is learned yet. -----
    steps.push_back([&] { save(); });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready,
              "0.1 the fixture lexicon loaded");
        checkEqual(firstRowOf(driverA, "ma"), "马", "0.2 the unlearned order leads with 马");
        checkCount(engine.profileCount(), 0, "0.3 nothing has been learned yet");
        check(!std::filesystem::exists(profilePath), "0.4 and no profile file exists yet");
        check(engine.profileWarning().empty(), "0.5 a valid start reports no warning");
    });

    // --- Case A: repeated selections promote a candidate, and reach the disk. ---
    // The core promotes a dictionary-attested row as soon as it has any recent
    // evidence for the exact spelling/text pair (crates/ime-core/src/profile.rs:
    // `reliable` accepts `attested || has_repeated_evidence`, and every `ma` word in
    // this fixture is in the lexicon). So the SECOND candidate of the unlearned
    // order takes the top row on the first confirmed selection -- and the observable
    // proof that this is learning and not the lexicon is that the row it displaced is
    // still a better-weighted one.
    steps.push_back([&] {
        driverA.type("ma");
        const auto unlearned = driverA.page();
        check(unlearned.size() >= 2 && unlearned[0] == "马" && unlearned[1] == "妈",
              "A.1 the unlearned order is 马 then 妈",
              unlearned.size() >= 2 ? unlearned[0] + unlearned[1] : std::string("too few rows"));
        driverA.escape();
        chooseWord(driverA, "ma", "妈");
        checkEqual(driverA.committed(), "妈", "A.2 the second candidate commits");
        checkEqual(firstRowOf(driverA, "ma"), "妈",
                   "A.3 the selection promotes it over the heavier 马");
    });
    steps.push_back([&] {
        // A second spelling/text pair follows the same rule, so promotion is not a
        // one-off tied to one word.
        chooseWord(driverA, "ma", "码");
        checkEqual(firstRowOf(driverA, "ma"), "码", "A.4 choosing 码 makes it lead in turn");
    });
    steps.push_back([&] {
        driverA.type("ma");
        const auto rows = driverA.page();
        check(rows.size() >= 2 && rows[0] == "码" && rows[1] == "妈",
              "A.5 both learned words lead, most recent first",
              rows.size() >= 2 ? rows[0] + rows[1] : std::string("too few rows"));
        driverA.escape();
    });
    steps.push_back([&] {
        // One more selection of 妈 makes it lead again, which is the "the ordering
        // really follows the user" half of the acceptance criterion: with both pairs
        // already promoted, the more recently chosen one is the one in front.
        chooseWord(driverA, "ma", "妈");
        checkEqual(firstRowOf(driverA, "ma"), "妈", "A.6 reselecting 妈 brings it back to the top");
    });

    // --- Case B: the profile reaches the disk, private, with the right modes. ---
    steps.push_back([&] {
        check(flush(), "B.1 the writer drained within the bound");
        check(std::filesystem::exists(profilePath), "B.2 the profile file now exists");
        checkCount(modeOf(profilePath), 0600, "B.3 the profile file is 0600");
        checkCount(modeOf(std::filesystem::path(profilePath).parent_path()), 0700,
                   "B.4 the profile directory is 0700");
        checkCount(engine.profileCount(), 2, "B.5 the master profile holds both learned pairs");
        check(engine.profileRevision() > 0, "B.6 the profile revision advanced");
        check(engine.profileStats().saved > 0, "B.7 the store counted a save");
        checkEqual(engine.profileError(), "", "B.8 and recorded no failure");
    });

    // --- Case C: several input contexts share one learned order. ---------------
    steps.push_back([&] {
        // B's context was created before any learning happened, so this also proves
        // an already-existing session is re-snapshotted rather than only new ones.
        checkEqual(firstRowOf(driverB, "ma"), "妈",
                   "C.1 a context created before learning sees the learned order");
    });
    steps.push_back([&] {
        // A selection made in B is visible in A: one master profile, not one per
        // context, so the two cannot drift apart. A separate spelling keeps this
        // case independent of how often case A trained the `ma` pairs.
        chooseWord(driverB, "ni'hao", "拟好");
        checkEqual(firstRowOf(driverA, "ni'hao"), "拟好", "C.2 a selection in B reaches A");
    });
    steps.push_back([&] {
        // A composition in progress is never disturbed: the snapshot is published
        // only to a session that is showing nothing at all, so B keeps the ordering
        // its own composition started with even while A learns past it.
        driverB.type("ma");
        const auto composing = driverB.page();
        check(!composing.empty() && composing.front() == "妈", "C.3 B is composing");
        // Every context is cleared before the A-side commits, so "nothing reached B"
        // is about this case and not about what B committed earlier.
        driverA.clearCommitted();
        driverB.clearCommitted();
        for (int i = 0; i < 4; ++i) { chooseWord(driverA, "ma", "码"); }
        checkEqual(driverB.page().front(), "妈", "C.4 B's live composition is untouched");
        checkEqual(driverB.committed(), "", "C.5 the commit went to A, not to B");
        checkEqual(driverA.committed(), "码", "C.6 A is the context that committed");
        driverB.escape();
    });
    steps.push_back([&] {
        checkEqual(firstRowOf(driverB, "ma"), "码", "C.7 B picks the shared order up once idle again");
        check(flush(), "C.8 the writer drained again");
    });

    // --- Case D: the learned order survives a restart. -------------------------
    // A real second run means a new Engine over the same path, which also needs its
    // own InputContextManager: the first Engine stays registered on the original
    // one for the rest of the suite, so a fresh manager is what keeps the two apart
    // exactly as two processes would be.
    // The restarted engine lives at main scope on purpose: a step runs during a
    // later event-loop turn, so a block-scoped engine would be destroyed before
    // its own step ever ran.
    auto restartManager = std::unique_ptr<fcitx::InputContextManager>();
    auto restarted = std::unique_ptr<chengyin::Engine>();
    auto restartedContext = std::unique_ptr<Context>();
    auto restartedDriver = std::unique_ptr<Driver>();
    steps.push_back([&] {
            // A second manager keeps this engine's property registration apart from the
            // first one's, while the run's loop is shared so its lexicon load can
            // actually publish. That is what makes this a second run as far as the
            // storage layer is concerned.
            restartManager = std::make_unique<fcitx::InputContextManager>();
            // The first engine is not destroyed here: what this case proves is that a
            // second reader of the same file sees the learned order, which is the part
            // a restart actually depends on.
            restarted = std::make_unique<chengyin::Engine>(*restartManager, loop, "", profilePath);
            restartedContext = std::make_unique<Context>(*restartManager, "learning-restart");
            restartedDriver = std::make_unique<Driver>(*restarted, *restartedContext);
            // The same lexicon as the first run: a restart means the same
            // configuration, and without this the assertions below would be about the
            // bundled demo dictionary rather than about the file that was read.
            fcitx::RawConfig restartRaw;
            chengyin::EngineConfig restartConfig(lexicon);
            restartConfig.save(restartRaw);
            restarted->setConfig(restartRaw);
            checkCount(restarted->profileCount(), 3, "D.1 the restarted engine reads the learned pairs");
        });
        steps.push_back([&] {
            // 码 was chosen three times, so it leads; the point is that the order came
            // from the file rather than from the unlearned weights.
            restartedDriver->type("ma");
            checkEqual(restartedDriver->firstRow(), "码", "D.2 the learned order survived the restart");
            restartedDriver->escape();
            checkCount(restarted->profileStats().saved, 0, "D.3 a fresh run has saved nothing of its own");
        });
        steps.push_back([&] {
            // And the restarted engine keeps learning on top of what it read. This is
            // the case that fails if the store's own profile, rather than the file,
            // were the source of the master snapshot.
            // 码 leads because the file says so, which proves the file (not an empty
            // default) was adopted. `hao` is a spelling no case has trained, so the
            // promotion below can only come from the restarted engine learning on top
            // of what it read.
            checkEqual(firstRowOf(*restartedDriver, "ma"), "码",
                       "D.4 the imported order leads before the new selection");
            checkEqual(firstRowOf(*restartedDriver, "hao"), "好",
                       "D.5 the untrained spelling starts on its lexicon order");
            chooseWord(*restartedDriver, "hao", "号");
            checkEqual(firstRowOf(*restartedDriver, "hao"), "号",
                       "D.6 learning continues on top of the imported file");
            check(restarted->flushLearning(5000), "D.7 the restarted engine persisted");
            check(restarted->profileStats().saved > 0, "D.8 and counted its own saves");
            restartedDriver.reset();
            restartedContext.reset();
            restarted.reset();
            restartManager.reset();
        });


    // --- Case E: Learning=false stops recording, writing and reordering. --------
    steps.push_back([&] {
        config.learning.setValue(false);
        save();
        check(!engine.settings().learning, "E.1 the save turned learning off");
    });
    steps.push_back([&] {
        // The file must not move at all while learning is off: same bytes, same mtime.
        // Comparing both is what makes "did not write" an assertion rather than an
        // inference from the ordering staying put.
        const auto before = storedBytes();
        struct stat info {};
        check(::stat(profilePath.c_str(), &info) == 0, "E.2 the profile exists before the case");
        const auto mtimeBefore = info.st_mtim.tv_sec;
        const auto orderBefore = firstRowOf(driverA, "ma");
        for (int i = 0; i < 4; ++i) { chooseWord(driverA, "ma", "麻"); }
        check(flush(), "E.3 nothing was queued, so the writer drained at once");
        checkEqual(storedBytes(), before, "E.4 learning off: the file's bytes are unchanged");
        check(::stat(profilePath.c_str(), &info) == 0 && info.st_mtim.tv_sec == mtimeBefore,
              "E.5 learning off: the file was not even touched");
        checkEqual(firstRowOf(driverA, "ma"), orderBefore,
                   "E.6 learning off: the ordering is unchanged");
        checkCount(engine.profileStats().rejected, 0, "E.7 and nothing was refused either");
    });
    steps.push_back([&] {
        // The master profile still holds what it held: the switch stops new training,
        // it is not a clear. Clearing is the file operation Case H covers.
        checkCount(engine.profileCount(), 3, "E.8 learning off: the existing pairs are kept");
        config.learning.setValue(true);
        save();
    });
    steps.push_back([&] {
        // 骂 is still untrained at this point, so this is the same "one selection of a
        // dictionary-attested pair promotes it" rule the off-case could not apply.
        chooseWord(driverA, "hao", "号");
        checkEqual(firstRowOf(driverA, "hao"), "号", "E.9 learning on again: selections count again");
        check(flush(), "E.10 and they reach the disk again");
        check(engine.profileStats().saved > 0, "E.11 the store counted the new saves");
    });

    // --- Case F: a sensitive context never trains. -----------------------------
    // The adapter refuses a sensitive context at two independent places: the key
    // entry point, and again where a selection would be acknowledged. This case
    // drives the whole path a real password field uses -- letters, then the key that
    // would otherwise select a candidate -- so a regression at either place shows up
    // as a profile that grew when it must not have.
    std::string sensitiveBefore;
    int32_t sensitivePairs = 0;
    uint64_t sensitiveSaved = 0;
    steps.push_back([&] {
        sensitiveBefore = storedBytes();
        sensitivePairs = engine.profileCount();
        sensitiveSaved = engine.profileStats().saved;
        // Composed while the context is normal, then the field turns sensitive: the
        // pending composition must be dropped rather than carried in.
        driverA.type("hao");
        check(!driverA.page().empty(), "F.1 a composition exists while the context is normal");
        a.setCapabilityFlags(fcitx::CapabilityFlag::Sensitive);
        driverA.clearCommitted();
        driverA.press(static_cast<fcitx::KeySym>('h'));
        checkEqual(driverA.committed(), "", "F.2 turning sensitive drops the composition");
        check(driverA.page().empty(), "F.3 and its candidate list");
    });
    steps.push_back([&] {
        // A whole spelling and the selecting key, all inside the sensitive field. In
        // a normal field exactly this sequence learns the pair (Case A), so it is the
        // right probe: a guard that is missing shows up as a larger profile.
        driverA.clearCommitted();
        driverA.type("hao");
        driverA.choose("号");
        driverA.press(FcitxKey_space);
        checkEqual(driverA.committed(), "", "F.4 a sensitive field commits nothing");
        check(flush(), "F.5 nothing was queued by the sensitive context");
        checkEqual(storedBytes(), sensitiveBefore, "F.6 a sensitive field writes nothing");
        checkCount(engine.profileCount(), sensitivePairs, "F.7 a sensitive field learns nothing");
        checkCount(engine.profileStats().saved, sensitiveSaved, "F.8 and saved nothing more");
    });
    steps.push_back([&] {
        // Back to a normal field: the same sequence learns, which is what proves the
        // assertions above are about the sensitive flag and not about a sequence that
        // never trains anything in the first place.
        a.setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
        check(flush(), "F.9 the writer drained");
        // `ma/骂` is the one pair no other case trains, so a profile that grows here
        // can only be this selection -- which is what makes the two assertions above
        // meaningful rather than a sequence that trains nothing anywhere.
        chooseWord(driverA, "ma", "骂");
        checkEqual(driverA.committed(), "骂", "F.10 the same sequence commits in a normal field");
        check(engine.profileCount() > sensitivePairs, "F.11 and it does learn there",
              std::to_string(engine.profileCount()) + " vs " + std::to_string(sensitivePairs));
    });

    // --- Case G: a damaged stored file is never overwritten. --------------------
    auto damaged = std::unique_ptr<chengyin::Engine>();
    auto damagedContext = std::unique_ptr<Context>();
    auto damagedDriver = std::unique_ptr<Driver>();
    auto damagedManager = std::unique_ptr<fcitx::InputContextManager>();
    const auto damagedPath = directory.path + "/damaged/chengyin/profile.bin";
    const std::string garbage = "not-a-profile-at-all, deliberately";
    std::filesystem::create_directories(std::filesystem::path(damagedPath).parent_path());
    writeFile(damagedPath, garbage);
    steps.push_back([&] {
        // A separate manager, but the run's event loop: the adapter publishes a
        // finished lexicon load through its dispatcher, and a private loop that never
        // turns would leave this engine in Loading for the whole case.
        damagedManager = std::make_unique<fcitx::InputContextManager>();
        damaged = std::make_unique<chengyin::Engine>(*damagedManager, loop, "", damagedPath);
        damagedContext = std::make_unique<Context>(*damagedManager, "learning-damaged");
        damagedDriver = std::make_unique<Driver>(*damaged, *damagedContext);
        check(!damaged->profileWarning().empty(), "G.1 a damaged file is reported once",
              damaged->profileWarning());
        check(damaged->profileWarning().find("ma") == std::string::npos,
              "G.2 the warning carries no spelling");
        // The fixture lexicon, so the assertions below observe this adapter rather
        // than the built-in demo vocabulary.
        fcitx::RawConfig raw;
        chengyin::EngineConfig damagedConfig(lexicon);
        damagedConfig.save(raw);
        damaged->setConfig(raw);
    });
    steps.push_back([&] {
        // Input still works, on an empty profile, and the selection is learned in
        // memory: refusing to write is not the same as refusing to learn.
        damagedDriver->type("ma");
        checkCount(static_cast<long long>(damagedDriver->page().size()), 5,
                   "G.3 a damaged file still leaves a working engine");
        damagedDriver->clearCommitted();
        damagedDriver->choose("妈");
        damagedDriver->press(FcitxKey_space);
        checkEqual(damagedDriver->committed(), "妈", "G.4 and input still commits");
        checkCount(damaged->profileCount(), 1, "G.5 the selection is learned in memory");
        checkEqual(readFile(damagedPath), garbage, "G.6 the damaged file's bytes are untouched");
        checkCount(damaged->profileStats().saved, 0, "G.7 nothing was written over it");
    });
    steps.push_back([&] {
        // The panel notice is what tells the user why their ordering will not survive
        // a restart. It must appear on the hint line of the next composition.
        damagedDriver->type("ma");
        check(damagedContext->inputPanel().auxDown().toString().find("学习档案") != std::string::npos,
              "G.8 the panel carries the notice",
              damagedContext->inputPanel().auxDown().toString());
        damagedDriver->escape();
        checkCount(damaged->profileStats().saved, 0, "G.9 still nothing written");
        checkEqual(readFile(damagedPath), garbage, "G.10 and the file is still untouched");
        damagedDriver.reset();
        damagedContext.reset();
        damaged.reset();
        damagedManager.reset();
    });

    // --- Case H: deleting the file clears learning; restoring it imports one. ----
    // Both are captured from the running suite rather than hard-coded: how much the
    // earlier cases trained is their business, and this case is about the clear.
    std::string learnedBefore;
    int32_t pairCountBefore = 0;
    const auto backup = directory.path + "/imported/profile.bin";
    std::filesystem::create_directories(std::filesystem::path(backup).parent_path());
    steps.push_back([&] {
        check(flush(), "H.1 the main store is quiet before the clear");
        std::filesystem::copy_file(profilePath, backup);
        // The exact leading word is whatever earlier cases trained; what matters here
        // is that it is a learned one, so it is captured rather than assumed.
        learnedBefore = firstRowOf(driverA, "ma");
        check(learnedBefore != "马", "H.2 a learned order is in place", learnedBefore);
        pairCountBefore = engine.profileCount();
    });
    steps.push_back([&] {
        // Deleting the file and reloading is how a user clears learning: there is no
        // GUI button for it, by design.
        std::filesystem::remove(profilePath);
        engine.reloadConfig();
    });
    steps.push_back([&] {
        checkCount(engine.profileCount(), 0, "H.3 a deleted file clears the master profile");
        checkEqual(firstRowOf(driverA, "ma"), "马",
                   "H.4 the session's ordering is back to the unlearned one");
        checkEqual(firstRowOf(driverA, "hao"), "好",
                   "H.5 and so is the other learned spelling");
    });
    steps.push_back([&] {
        // A selection made after the clear is written to a fresh file rather than
        // being refused just because the file it replaced is gone.
        check(engine.profileWarning().empty(), "H.6 no warning after a clean clear");
        chooseWord(driverA, "ma", "码");
        check(flush(), "H.7 the writer is quiet again");
        check(std::filesystem::exists(profilePath), "H.8 a new profile file was created");
        checkCount(engine.profileCount(), 1, "H.9 holding only the post-clear pair");
    });
    steps.push_back([&] {
        // Putting the saved file back and reloading is how a user imports one.
        std::filesystem::copy_file(backup, profilePath,
                                   std::filesystem::copy_options::overwrite_existing);
        engine.reloadConfig();
    });
    steps.push_back([&] {
        checkCount(engine.profileCount(), pairCountBefore,
                   "H.10 the imported file's pairs are adopted");
        checkEqual(firstRowOf(driverA, "ma"), learnedBefore, "H.11 the imported ordering is back");
    });


    // --- Case J: a session whose baseline is stale must not erase a fresher one. -
    // applyProfile only publishes to a session that is showing nothing, so a context
    // that stays busy across another context's commit keeps an older snapshot. When
    // that stale session later commits, its own copy must NOT become the master: it
    // was built on an older revision and would silently drop every pair learned
    // since -- pairs the other contexts are already using. The pair is recorded into
    // the master instead, so it is still written down exactly once.
    //
    // `lan`, `yun` and `wu` are trained by no other case, and each has exactly one
    // learned row, so "both pairs are in effect" is observable as ordering rather
    // than inferred from a count: if A's stale snapshot became the master, B's `lan`
    // row would be gone and `lan` would fall back to its lexicon order.
    int32_t stalePairsBefore = 0;
    steps.push_back([&] {
        // A starts composing and deliberately does NOT finish. Its baseline stays at
        // whatever revision it last applied -- which is what makes it stale later.
        driverA.type("wu");
        check(!driverA.page().empty(), "J.1 A is composing and therefore not idle");
        stalePairsBefore = engine.profileCount();
    });
    steps.push_back([&] {
        // B learns a pair while A is still busy. The master moves on, but A cannot be
        // handed the new snapshot: it is not idle.
        chooseWord(driverB, "lan", "兰");
        checkEqual(firstRowOf(driverB, "lan"), "兰", "J.2 B learned its pair");
        checkCount(engine.profileCount(), stalePairsBefore + 1,
                   "J.3 the master holds B's pair");
    });
    steps.push_back([&] {
        // A finishes its own composition and commits. A's session snapshot predates
        // B's selection, so taking it as the master here is exactly what the bug did.
        driverA.clearCommitted();
        driverA.choose("武");
        driverA.press(FcitxKey_space);
        checkEqual(driverA.committed(), "武", "J.4 A committed its own word");
        checkCount(engine.profileCount(), stalePairsBefore + 2,
                   "J.5 both B's pair and A's pair are in the master");
    });
    steps.push_back([&] {
        // B's pair must still be in effect. With the bug, A's stale snapshot became
        // the master and this row was gone, so `lan` fell back to the lexicon order.
        checkEqual(firstRowOf(driverB, "lan"), "兰",
                   "J.6 B's pair still leads after A's stale commit");
        // A brand-new context is the second, independent witness: it has never been
        // handed any snapshot, so it can only ever see the master the engine holds
        // right now. Both pairs must show up there.
        driverC.type("lan");
        checkEqual(driverC.firstRow(), "兰", "J.7 a new context sees B's pair");
        driverC.escape();
        driverC.type("wu");
        checkEqual(driverC.firstRow(), "武", "J.8 and A's pair from the stale baseline");
        driverC.escape();
    });
    steps.push_back([&] {
        // A third pair, so the file on disk carries one more row than A's stale
        // snapshot ever knew about.
        chooseWord(driverB, "yun", "运");
        check(flush(), "J.9 the writer drained");
        checkEqual(firstRowOf(driverB, "yun"), "运", "J.10 the third pair was learned");
    });
    steps.push_back([&] {
        // A restart reads the file: every pair the two contexts learned must be there,
        // including the one A learned from a stale baseline.
        restartManager = std::make_unique<fcitx::InputContextManager>();
        restarted = std::make_unique<chengyin::Engine>(*restartManager, loop, "", profilePath);
        restartedContext = std::make_unique<Context>(*restartManager, "learning-stale");
        restartedDriver = std::make_unique<Driver>(*restarted, *restartedContext);
        // The fixture lexicon, or the restarted engine would answer from the bundled
        // demo and the words this case learned would not be reachable at all. The step
        // runner waits for that load to publish before the assertions below run.
        fcitx::RawConfig staleRaw;
        chengyin::EngineConfig staleConfig(lexicon);
        staleConfig.save(staleRaw);
        restarted->setConfig(staleRaw);
    });
    steps.push_back([&] {
        checkEqual(firstRowOf(*restartedDriver, "lan"), "兰",
                   "J.11 B's pair survived the restart");
        checkEqual(firstRowOf(*restartedDriver, "yun"), "运",
                   "J.12 B's third pair survived the restart");
        checkCount(restarted->profileCount(), stalePairsBefore + 3,
                   "J.13 the imported file carries every pair both contexts learned");
        restartedDriver.reset();
        restartedContext.reset();
        restarted.reset();
        restartManager.reset();
    });

    // --- Case I: a store that cannot write never blocks the key thread. ---------
    // The target path is a DIRECTORY, so every publish fails and the retry budget is
    // spent while events pile up behind it. This is the failing-storage case, and it
    // is deterministic: no timing assumption is involved, only that renaming a file
    // over a directory never succeeds.
    auto blocked = std::unique_ptr<chengyin::Engine>();
    auto blockedContext = std::unique_ptr<Context>();
    auto blockedDriver = std::unique_ptr<Driver>();
    auto blockedManager = std::unique_ptr<fcitx::InputContextManager>();
    const auto blockedPath = directory.path + "/blocked/chengyin/profile.bin";
    std::filesystem::create_directories(blockedPath);
    steps.push_back([&] {
        // A separate manager keeps this engine's property registration apart from the
        // others, but it shares the run's event loop on purpose: the adapter publishes
        // a finished lexicon load through its dispatcher, and a private loop that is
        // never executed would leave this engine stuck in Loading forever.
        blockedManager = std::make_unique<fcitx::InputContextManager>();
        // The retry budget is injected small on purpose: the production budget
        // would spend seconds per batch here, and what this case is about is the
        // bound holding, not how long the production window is.
        blocked = std::make_unique<chengyin::Engine>(*blockedManager, loop, "", blockedPath,
                                                     chengyin::LearningRetryPolicy{2, 200, 1});
        blockedContext = std::make_unique<Context>(*blockedManager, "learning-blocked");
        blockedDriver = std::make_unique<Driver>(*blocked, *blockedContext);
        // The fixture lexicon, so this case tests the store rather than the demo
        // dictionary: without it the keys would not even compose.
        fcitx::RawConfig raw;
        chengyin::EngineConfig blockedConfig(lexicon);
        blockedConfig.save(raw);
        blocked->setConfig(raw);
        // The load is asynchronous, so the first composition waits for it.
    });
    steps.push_back([&] {
        blockedDriver->type("ma");
        checkEqual(blockedDriver->firstRow(), "马", "I.1 a store that cannot write still starts");
        blockedDriver->escape();
    });
    steps.push_back([&] {
        // Far more selections than the queue holds, back to back. The keys are all
        // processed and the ordering still moves, so the queue is drained or refused,
        // never waited on.
        for (int i = 0; i < 40; ++i) { chooseWord(*blockedDriver, "ma", "妈"); }
        checkEqual(blockedDriver->committed(), "妈", "I.2 every key was processed");
        checkEqual(firstRowOf(*blockedDriver, "ma"), "妈", "I.3 in-memory learning still reordered");
        // The retrying happens on the store's own thread, so the accounting below is
        // only final once nothing is queued and no batch is still being retried.
        // flush() is exactly that wait -- and on a failing store it only returns
        // after the retry budget is spent and the batch has been counted as dropped.
        // Asserting without it is what made this case pass on a fast machine and
        // fail on CI: the counters were simply read before the worker had run.
        check(blocked->flushLearning(30000), "I.4 the writer reached a terminal state on a failing store");
        const auto stats = blocked->profileStats();
        check(stats.rejected + stats.dropped + stats.exhausted > 0,
              "I.5 the failures are counted rather than swallowed",
              "rejected=" + std::to_string(stats.rejected) +
                  " dropped=" + std::to_string(stats.dropped) +
                  " exhausted=" + std::to_string(stats.exhausted));
        // A 128-slot bounded queue plus a publish that never succeeds is what these
        // counters describe, and nothing here may grow without limit.
        check(stats.rejected <= 128, "I.6 the queue stayed bounded");
        check(!blocked->profileError().empty(), "I.7 the last failure is reported");
    });
    steps.push_back([&] {
        const auto error = blocked->profileError();
        check(error.find("妈") == std::string::npos && error.find("ma") == std::string::npos,
              "I.8 the error text carries no spelling or text", error);
        check(error.find("学习档案") != std::string::npos, "I.9 and it names the learning store", error);
        check(std::filesystem::is_directory(blockedPath),
              "I.10 the path it could not write is still a directory");
        // A flush still returns inside its own bound even though nothing can be
        // persisted: the bound is what has to hold, not the outcome.
        const auto flushStart = fcitx::now(CLOCK_MONOTONIC);
        blocked->flushLearning(1000);
        const auto flushMs = (fcitx::now(CLOCK_MONOTONIC) - flushStart) / 1000;
        check(flushMs <= 3000, "I.11 flush honours its bound while the store fails",
              std::to_string(flushMs) + " ms");
    });
    steps.push_back([&] {
        // Shutdown with a full queue must not hang: this step runs inside the run's
        // deadline, so a blocking destructor fails the deadline rather than hanging
        // the whole suite.
        blockedDriver.reset();
        blockedContext.reset();
        blocked.reset();
        blockedManager.reset();
        check(true, "I.12 the engine shut down with a failing store and a full queue");
    });

    // The step machine: one step per event-loop turn, and a step is only taken once
    // the background lexicon load has published. Several steps swap the lexicon or
    // call reloadConfig(), so this is what keeps every assertion looking at the
    // engine the step believes it is looking at.
    size_t step = 0;
    // Generous but finite: the adapter loads its lexicon on one worker and persists
    // on another, so a stuck one has to fail the suite rather than hang it.
    const auto deadline = fcitx::now(CLOCK_MONOTONIC) + 120ULL * 1000000ULL;
    auto timer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0,
        [&](fcitx::EventSourceTime *source, uint64_t) {
            if (fcitx::now(CLOCK_MONOTONIC) >= deadline) {
                check(false, "the suite finished within the deadline");
                loop.exit();
                return true;
            }
            // Every engine this suite starts loads its lexicon on its own worker, so
            // a step is only taken once none of them is still loading. The reason is
            // the same for all of them: asserting sooner would silently test the
            // built-in demo lexicon instead of the fixture.
            const bool loading = engine.reloadState() == chengyin::Engine::ReloadState::Loading ||
                                 (restarted && restarted->reloadState() == chengyin::Engine::ReloadState::Loading) ||
                                 (blocked && blocked->reloadState() == chengyin::Engine::ReloadState::Loading);
            if (!loading && step < steps.size()) {
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
              << "Fcitx5 persistent-learning test: " << failures << " failed assertion(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
