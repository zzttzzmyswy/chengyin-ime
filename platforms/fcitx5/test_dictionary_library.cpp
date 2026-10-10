// SPDX-License-Identifier: GPL-3.0-or-later
// Attached-lexicon suite for the Fcitx 5 adapter (iteration I21).
//
// The adapter used to know exactly one lexicon: DictionaryPath replaced the
// bundled demo, and importing a Sogou export, a UTF-16 file or a project binary
// was something only the Windows settings page could do. This suite drives the
// real Engine through the same setConfig() entry the config tool uses and asserts
// what the attached list actually does: every supported file format is
// recognised, several entries merge into one lexicon, a disabled entry is neither
// read nor validated, a failure of any enabled entry keeps the previous lexicon
// and names the entry that caused it, the 250,000-row and 64-entry ceilings are
// reported rather than applied silently, and an active composition is never
// disturbed by any of it.
//
// Every lexicon here is authored in this file, and the classic-SCEL sample is
// built byte by byte the way crates/ime-core/tests/modern.rs builds its own, so
// no assertion depends on the user's data or on a network download. Every fcitx
// path resolves inside a private temporary tree.
#include "engine.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/utf8.h>
#include <fcitx-utils/log.h>
#include <fcitx-config/iniparser.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <sys/stat.h>
#include <unistd.h>

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

template <typename T>
void checkCount(T actual, T expected, const std::string &what) {
    check(actual == expected, what,
          "expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

// A message contains a phrase. The fixed wording is asserted this way rather than
// as a whole sentence, so a reworded prefix does not have to be chased here.
void checkHas(const std::string &haystack, const std::string &needle, const std::string &what) {
    check(haystack.find(needle) != std::string::npos, what, "looking for '" + needle + "' in '" + haystack + "'");
}

void checkLacks(const std::string &haystack, const std::string &needle, const std::string &what) {
    check(haystack.find(needle) == std::string::npos, what, "did not expect '" + needle + "' in '" + haystack + "'");
}

void writeFile(const std::string &path, const std::string &contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
    stream.close();
    if (!stream) { throw std::runtime_error("cannot write " + path); }
}

std::string readFile(const std::string &path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

struct TemporaryDirectory {
    TemporaryDirectory() {
        char pattern[] = "/tmp/chengyin-library-XXXXXX";
        const char *created = ::mkdtemp(pattern);
        if (!created) { throw std::runtime_error("mkdtemp failed"); }
        path = created;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    std::string path;
};

// A fake input context, exactly as the other suites build one: it records what was
// committed and answers the panel the adapter filled in.
class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager, const std::string &name)
        : InputContext(manager, name) {
        created();
        focusIn();
    }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "chengyin-library-test"; }
    void commitStringImpl(const std::string &value) override { committed += value; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
};

// Drives the adapter the way a frontend does: one key event per character.
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
    void commit() const { press(FcitxKey_space); }

    std::string preedit() const { return context_.inputPanel().preedit().toString(); }
    std::string auxDown() const { return context_.inputPanel().auxDown().toString(); }

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

    // Whether the core can reach a candidate for the current composition, searched
    // page by page so the assertion never depends on ranking.
    bool reachable(const std::string &text) const {
        for (int at = 0; at < 64; ++at) {
            const auto rows = page();
            if (std::find(rows.begin(), rows.end(), text) != rows.end()) { return true; }
            if (rows.empty()) { return false; }
            press(FcitxKey_equal);
            if (page() == rows) { return false; }
        }
        return false;
    }

    // Types a spelling, reports whether the word is reachable, retires the
    // composition. This is the one assertion that proves a lexicon took part in the
    // merge: the word exists in exactly one file.
    bool has(const std::string &spelling, const std::string &word) const {
        type(spelling);
        const bool found = reachable(word);
        escape();
        return found;
    }

    std::string commitTyping(const std::string &spelling) const {
        context_.committed.clear();
        type(spelling);
        commit();
        return context_.committed;
    }

    const std::string &committed() const { return context_.committed; }
    void clearCommitted() const { context_.committed.clear(); }

private:
    chengyin::Engine &engine_;
    Context &context_;
    const fcitx::InputMethodEntry entry_{"chengyin", "Chengyin", "zh_CN", "chengyin"};
};

// --- Lexicon fixtures -------------------------------------------------------
//
// Every file this suite loads is built here, and every spelling is chosen so that
// reaching one of its words proves the file it lives in took part in the merge.
// No fixture is a copy of data/daily.tsv and nothing is downloaded.

// The base lexicon: words on their own spellings, so the base is visibly present
// before and after any attached file is added.
std::string baseLexicon() {
    return "ji'chu\t基楚\t900\n"
           "ji'cu\t基础\t500\n"
           "zhu'ce\t主册\t800\n";
}

// A UTF-8 TSV entry whose word exists nowhere else. 注册表 is reachable only if
// this file was merged, which is what makes it the reachability probe for case B.
const char *const kAttachedTsv = "zhu'ce\t注册表\t950\n";

// A Sogou text export: apostrophe-separated syllables and a space instead of tabs.
const char *const kSogouText = "'sou'gou 搜构 700\n";

// A UTF-16LE TSV document with a byte-order mark, the other text shape the shared
// importer accepts. The text is encoded here rather than through a framework helper,
// which returns the platform's own wide type and would hide what the file's bytes
// actually are; each code unit is little-endian with the low byte first.
std::string utf16Lexicon() {
    std::string bytes = "\xff\xfe";
    for (const uint32_t code : std::u32string(U"shi'liu\t十六\t850\n")) {
        bytes.push_back(static_cast<char>(code & 0xff));
        bytes.push_back(static_cast<char>((code >> 8) & 0xff));
    }
    return bytes;
}

// A classic SCEL file, built the way crates/ime-core/tests/modern.rs builds its
// own: a fixed header, a two-entry pinyin table, then one word group carrying two
// homophones on the spelling ni'hao. 拟好 is the second one and appears in no other
// fixture, so it is the SCEL-specific probe.
std::string scelLexicon() {
    // The header area every classic file reserves, with the pinyin table directly
    // after it: that offset is fixed, and the reader looks there first. The zeroes a
    // 0x44 producer leaves between the table and the word groups are what makes the
    // file's own length differ from the table's end.
    std::string bytes(0x1540, '\0');
    const char header[] = {0x40, 0x15, 0, 0, 0x44, 0x43, 0x53, 1};
    bytes.replace(0, sizeof(header), header, sizeof(header));
    bytes[0x120] = 1; // one word group
    auto half = [&](uint16_t value) {
        bytes.push_back(static_cast<char>(value & 0xff));
        bytes.push_back(static_cast<char>(value >> 8));
    };
    auto word = [&](uint32_t value) {
        for (int i = 0; i < 4; ++i) { bytes.push_back(static_cast<char>((value >> (i * 8)) & 0xff)); }
    };
    word(2); // two syllables follow
    for (const auto &syllable : {std::pair<uint16_t, const char *>{10, "ni"}, {90, "hao"}}) {
        half(syllable.first);
        half(static_cast<uint16_t>(std::strlen(syllable.second) * 2));
        for (const char *at = syllable.second; *at; ++at) {
            bytes.push_back(*at);
            bytes.push_back('\0');
        }
    }
    half(2); // two homophones
    half(4); // their syllable ids, four bytes
    half(10);
    half(90);
    // Distinct weights, so which of the two leads a commit is a property of this
    // fixture rather than of how equal weights happen to break a tie: 拟好 is the
    // one that must lead, and it appears in no other fixture here.
    for (const auto &entry : {std::pair<uint16_t, uint16_t>{0x62df, 0x597d}, {0x4f60, 0x597d}}) {
        half(4); // two UTF-16 code units
        half(entry.first);
        half(entry.second);
        half(10); // extension length, in bytes
        half(entry.first == 0x62df ? 900 : 100); // ... whose first two bytes carry the weight
        bytes.append(8, '\0');
    }
    return bytes;
}

// A project binary v2 lexicon, exported by the core itself from a TSV held in
// memory, so the fixture is a real binary rather than a hand-written header.
std::string binaryLexicon(const std::string &tsv) {
    ChengyinDictionary *dictionary =
        chengyin_dictionary_new_tsv(reinterpret_cast<const uint8_t *>(tsv.data()), tsv.size());
    if (!dictionary) { throw std::runtime_error("cannot build the binary fixture"); }
    const int32_t size = chengyin_dictionary_binary(dictionary, nullptr, 0);
    std::string bytes(static_cast<size_t>(size < 0 ? 0 : size), '\0');
    const int32_t written =
        chengyin_dictionary_binary(dictionary, reinterpret_cast<uint8_t *>(bytes.data()), bytes.size());
    chengyin_dictionary_free(dictionary);
    if (size <= 0 || written != size) { throw std::runtime_error("cannot export the binary fixture"); }
    return bytes;
}

// One entry of the attached list, built the way the config tool builds it.
chengyin::DictionaryEntryConfig entry(const std::string &name, const std::string &path, bool enabled = true) {
    chengyin::DictionaryEntryConfig config;
    config.name.setValue(name);
    config.path.setValue(path);
    config.enabled.setValue(enabled);
    return config;
}

// The largest attached file that can reach the merge: 250,000 rows is the core's
// own per-file ceiling, so this is also the smallest file that can push the union
// past that ceiling on its own.
//
// Each key is `zz` plus four base-26 letters, which is not a real syllable and is
// spelled in a way no ordinary input reaches, so nothing here can shadow a fixture
// word or show up in a page a test inspects.
std::string maximalTsv(size_t rows = 250000) {
    std::string text;
    text.reserve(rows * 20);
    for (size_t i = 0; i < rows; ++i) {
        size_t value = i;
        char key[7] = {'z', 'z', 'a', 'a', 'a', 'a', '\0'};
        for (int at = 5; at >= 2; --at) {
            key[at] = static_cast<char>('a' + value % 26);
            value /= 26;
        }
        text += key;
        text += "\t词";
        text += std::to_string(i);
        text += "\t1\n";
    }
    return text;
}

} // namespace

// The loader's own cancellation rule, driven directly rather than through the
// Engine. The Engine has a second, independent guard -- it drops any answer whose
// request ordinal is not the newest -- so a bug in the loader's own generation
// check would be invisible from there. This case is what pins the loader itself.
//
// The first request names the 250,000-row fixture, whose import takes long enough
// that the second request is certain to arrive while the first is still running --
// on one core as much as on twenty. The second names the bundled demo, which needs
// no file at all. Only the second may be published, and the fixture's own words
// must never become reachable through it.
void loaderCancellation(const std::string &slow, int &failed) {
    // The publish callback runs on the loader's own worker, so a mutex is what makes
    // the fields safe to read from here; the count is what this case waits on, which
    // turns "the answer arrived" into something observable rather than guessed at.
    struct Published {
        std::mutex mutex;
        int count = 0;
        uint64_t last = 0;
        bool hadDictionary = false;
        std::string error;
        int observedCount() {
            std::lock_guard<std::mutex> lock(mutex);
            return count;
        }
    } published;
    chengyin::DictionaryLoader loader([&](uint64_t generation, chengyin::DictionaryPtr dictionary,
                                          std::string error) {
        std::lock_guard<std::mutex> lock(published.mutex);
        ++published.count;
        published.last = generation;
        published.hadDictionary = dictionary != nullptr;
        published.error = std::move(error);
    });
    loader.request(1, slow);
    // Wait for the worker to have taken the request up, so "it was in flight" is a
    // fact rather than a hope.
    for (int spins = 0; loader.started() != 1 && spins < 1000000; ++spins) {
        std::this_thread::yield();
    }
    const bool inFlight = loader.started() == 1;
    loader.request(2, "");
    // Wait for the observable outcome rather than for a clock interval: the second
    // request's publish is what ends this case, and the bounded spin turns "it never
    // arrived" into a failure instead of a hang.
    for (int spins = 0; published.observedCount() == 0 && spins < 5000000; ++spins) {
        std::this_thread::yield();
    }
    // Read back under the same lock, so the wait above happens-before these reads.
    std::lock_guard<std::mutex> lock(published.mutex);
    std::cout << (inFlight ? "ok   " : "FAIL ") << "L.1 the first request was in flight when superseded" << std::endl;
    if (!inFlight) { ++failed; }
    std::cout << (published.count == 1 ? "ok   " : "FAIL ")
              << "L.2 exactly one answer was published  [got " << published.count << "]" << std::endl;
    if (published.count != 1) { ++failed; }
    std::cout << (published.last == 2 ? "ok   " : "FAIL ")
              << "L.3 and it was the newer request's  [got " << published.last << "]" << std::endl;
    if (published.last != 2) { ++failed; }
    std::cout << (published.hadDictionary && published.error.empty() ? "ok   " : "FAIL ")
              << "L.4 with the demo lexicon the newer request asked for" << std::endl;
    if (!published.hadDictionary || !published.error.empty()) { ++failed; }
}

int main() {
    TemporaryDirectory directory;
    const auto configHome = directory.path + "/config";
    const auto dataHome = directory.path + "/data";
    // Every fcitx path this run touches resolves inside the temporary tree, and the
    // profile path is injected besides, so neither the engine nor StandardPaths can
    // reach the developer's own configuration or data directory.
    if (::setenv("XDG_CONFIG_HOME", configHome.c_str(), 1) != 0 ||
        ::setenv("FCITX_CONFIG_HOME", (configHome + "/fcitx5").c_str(), 1) != 0 ||
        ::setenv("XDG_DATA_HOME", dataHome.c_str(), 1) != 0) {
        std::cerr << "FAIL: cannot point fcitx at a private configuration directory" << std::endl;
        return 1;
    }

    // Every fixture file, written before the engine starts.
    const auto base = directory.path + "/base.tsv";
    const auto tsv = directory.path + "/attached.tsv";
    const auto sogou = directory.path + "/sogou.txt";
    const auto utf16 = directory.path + "/utf16.tsv";
    const auto scel = directory.path + "/scel.scel";
    const auto binary = directory.path + "/binary.mswydict";
    const auto missing = directory.path + "/absent.tsv";
    const auto fifo = directory.path + "/pipe.tsv";
    const auto huge = directory.path + "/huge.tsv";
    const auto broken = directory.path + "/broken.tsv";
    const auto maximal = directory.path + "/maximal.tsv";
    const auto configFile = configHome + "/fcitx5/conf/chengyin.conf";
    writeFile(base, baseLexicon());
    writeFile(tsv, kAttachedTsv);
    writeFile(sogou, kSogouText);
    writeFile(utf16, utf16Lexicon());
    writeFile(scel, scelLexicon());
    writeFile(binary, binaryLexicon("er'jin\t二进制\t880\n"));
    writeFile(broken, std::string("\xff\xfe\xff", 3));
    writeFile(huge, "");
    std::filesystem::resize_file(huge, 64 * 1024 * 1024 + 1);
    if (::mkfifo(fifo.c_str(), 0600) != 0) {
        std::cerr << "FAIL: cannot create the FIFO fixture" << std::endl;
        return 1;
    }
    writeFile(maximal, maximalTsv());

    fcitx::InputContextManager manager;
    fcitx::EventLoop loop;
    chengyin::Engine engine(manager, loop, "", dataHome + "/chengyin/profile.bin");
    Context a(manager, "chengyin-library-a"), b(manager, "chengyin-library-b");
    Driver driverA(engine, a), driverB(engine, b);

    // The configuration the config tool edits, reused for every save exactly as the
    // tool's own page state is.
    chengyin::EngineConfig config(base);
    std::vector<chengyin::DictionaryEntryConfig> list;
    auto save = [&] {
        config.dictionaries.setValue(list);
        fcitx::RawConfig raw;
        config.save(raw);
        engine.setConfig(raw);
    };
    auto savedList = [&](const std::vector<chengyin::DictionaryEntryConfig> &entries) {
        list = entries;
        save();
    };
    auto readPersisted = [&](chengyin::EngineConfig &stored) {
        fcitx::readAsIni(stored, "conf/chengyin.conf");
    };
    // What the engine reports about the lexicon in use, which is what the config
    // tool's page and the one-line log both describe.
    auto loadedNames = [&] {
        std::vector<std::string> names;
        for (const auto &source : engine.dictionaryEntries()) { names.push_back(source.name); }
        return names;
    };
    auto loadedEnabled = [&] {
        std::vector<bool> enabled;
        for (const auto &source : engine.dictionaryEntries()) { enabled.push_back(source.enabled); }
        return enabled;
    };

    // The loader's own cancellation rule first: it owns a worker directly, so it
    // needs no event loop.
    loaderCancellation(maximal, failures);

    std::vector<std::function<void()>> steps;

    // The two log streams the last case captures into. fcitx routes FCITX_INFO and
    // FCITX_WARN through one global stream, so pointing it here is the only way to
    // assert on those lines; the default is restored before the suite ends.
    std::ostringstream failing;
    std::ostringstream successful;

    // --- Case 0: an empty list behaves exactly as before. ---------------------
    steps.push_back([&] { save(); });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready, "0.1 the base lexicon loaded");
        checkCount(engine.dictionaryEntries().size(), size_t{0}, "0.2 an empty list stays empty");
        checkCount(engine.loadedEntryCount(), size_t{0}, "0.3 nothing was merged in");
        check(driverA.has("jichu", "基楚"), "0.4 a base word is reachable");
        check(driverA.has("zhuce", "主册"), "0.5 and so is the second one");
        checkCount(engine.mergedEntryCount(), engine.baseEntryCount(),
                   "0.6 with nothing attached the merged total is the base count");
    });

    // --- Case A: one UTF-8 TSV entry, with the base lexicon still there. ------
    // The attached word is 注册表 on an existing spelling. 主册 (the base word on
    // that key) must stay reachable, and 注册表 must become reachable: that pair is
    // what proves the two files were unioned rather than one replacing the other.
    steps.push_back([&] { savedList({entry("附加一", tsv)}); });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready, "A.1 the attached lexicon loaded");
        checkCount(engine.loadedEntryCount(), size_t{1}, "A.2 one entry took part in the merge");
        checkCount(loadedNames().size(), size_t{1}, "A.3 the list holds one entry");
        checkCount(engine.mergedEntryCount(), engine.baseEntryCount() + 1,
                   "A.4 the merged total is the base count plus the one attached word");
        check(driverA.has("zhuce", "注册表"), "A.5 the attached word is reachable");
        check(driverA.has("zhuce", "主册"), "A.6 the base word on that spelling is still reachable");
        check(driverA.has("jichu", "基楚"), "A.7 and so is a base word on another spelling");
        checkEqual(driverA.commitTyping("zhuce"), "注册表",
                   "A.8 the attached word leads its spelling and commits");
    });
    // The same file, saved a second time with nothing changed: the lexicon is
    // re-read (that is how a user replaces a TSV in place) and the result is
    // identical, so the merge really is deterministic.
    steps.push_back([&] { save(); });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready, "A.9 an unchanged save re-reads cleanly");
        checkCount(engine.mergedEntryCount(), engine.baseEntryCount() + 1, "A.10 with the same total");
        check(driverA.has("zhuce", "注册表"), "A.11 and the same words");
    });

    // --- Case B: the four remaining formats, one entry each. ------------------
    // Every one of them carries a word that exists nowhere else, so reaching that
    // word is proof the shared importer recognised the format.
    steps.push_back([&] { savedList({entry("搜狗文本", sogou)}); });
    steps.push_back([&] {
        checkCount(engine.loadedEntryCount(), size_t{1}, "B.1 the Sogou export was recognised");
        check(driverA.has("sougou", "搜构"), "B.2 and its word is reachable");
        check(!driverA.has("zhuce", "注册表"), "B.3 the previous entry is gone with its list");
    });
    steps.push_back([&] { savedList({entry("UTF-16 词库", utf16)}); });
    steps.push_back([&] {
        checkCount(engine.loadedEntryCount(), size_t{1}, "B.4 the UTF-16LE file was recognised");
        check(driverA.has("shiliu", "十六"), "B.5 and its word is reachable");
    });
    steps.push_back([&] { savedList({entry("SCEL 词库", scel)}); });
    steps.push_back([&] {
        checkCount(engine.loadedEntryCount(), size_t{1}, "B.6 the classic SCEL file was recognised");
        check(driverA.has("nihao", "拟好"), "B.7 and one of its two words is reachable");
        check(driverA.has("nihao", "你好"), "B.8 and so is the other one");
    });
    steps.push_back([&] { savedList({entry("二进制词库", binary)}); });
    steps.push_back([&] {
        checkCount(engine.loadedEntryCount(), size_t{1}, "B.9 the project binary was recognised");
        check(driverA.has("erjin", "二进制"), "B.10 and its word is reachable");
    });
    // All four at once, which is the case a user actually builds up: four entries in
    // one list, every format represented, every word reachable from one lexicon.
    steps.push_back([&] {
        savedList({entry("搜狗文本", sogou), entry("UTF-16 词库", utf16), entry("SCEL 词库", scel),
                   entry("二进制词库", binary)});
    });
    steps.push_back([&] {
        checkCount(engine.loadedEntryCount(), size_t{4}, "B.11 all four entries took part in the merge");
        check(driverA.has("sougou", "搜构"), "B.12 the Sogou word is reachable");
        check(driverA.has("shiliu", "十六"), "B.13 the UTF-16 word is reachable");
        check(driverA.has("nihao", "拟好"), "B.14 the SCEL word is reachable");
        check(driverA.has("erjin", "二进制"), "B.15 the binary word is reachable");
        check(driverA.has("jichu", "基楚"), "B.16 and the base word is still reachable");
    });
    // The same words, committed rather than merely listed: each format's entry
    // reaches the application, not just the panel.
    steps.push_back([&] {
        checkEqual(driverA.commitTyping("sougou"), "搜构", "B.17 the Sogou word commits");
        checkEqual(driverA.commitTyping("shiliu"), "十六", "B.18 the UTF-16 word commits");
        checkEqual(driverA.commitTyping("nihao"), "拟好", "B.19 the SCEL word commits");
        checkEqual(driverA.commitTyping("erjin"), "二进制", "B.20 the binary word commits");
        checkEqual(driverA.commitTyping("jichu"), "基楚", "B.21 the base word commits");
    });

    // --- Case C: two attached files merge, and disabling one excludes it. -----
    steps.push_back([&] { writeFile(tsv, kAttachedTsv); savedList({entry("甲", tsv), entry("乙", sogou)}); });
    steps.push_back([&] {
        checkCount(engine.loadedEntryCount(), size_t{2}, "C.1 both entries took part in the merge");
        check(driverA.has("zhuce", "注册表"), "C.2 A's word is reachable");
        check(driverA.has("sougou", "搜构"), "C.3 B's word is reachable at the same time");
        check(driverA.has("jichu", "基楚"), "C.4 and the base word alongside both");
    });
    // A disabled entry is neither read nor validated. The file is REMOVED first, so
    // an implementation that opened it anyway would fail the whole load and the
    // assertion below would see the failure.
    steps.push_back([&] {
        std::filesystem::remove(sogou);
        savedList({entry("甲", tsv), entry("乙（已停用，文件已删）", sogou, false)});
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready,
              "C.5 a disabled entry with no file on disk is not an error");
        checkEqual(engine.dictionaryError(), "", "C.6 and reports nothing");
        checkCount(engine.loadedEntryCount(), size_t{1}, "C.7 only the enabled entry was merged");
        checkCount(loadedNames().size(), size_t{2}, "C.8 the disabled entry is still in the list");
        check(driverA.has("zhuce", "注册表"), "C.9 the enabled entry still applies");
        check(!driverA.has("sougou", "搜构"), "C.10 the disabled entry contributes nothing");
    });
    // Re-enabling it while the file is still missing is a failure, and the failure
    // names the entry: the disabled case above was not simply "errors are ignored".
    steps.push_back([&] { savedList({entry("甲", tsv), entry("乙", sogou)}); });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Failed,
              "C.11 re-enabling a missing file fails the load");
        checkHas(engine.dictionaryError(), "附加词库“乙”", "C.12 the message names the entry");
        checkHas(engine.dictionaryError(), "文件不存在", "C.13 and states the reason");
        check(driverA.has("zhuce", "注册表"), "C.14 the old lexicon is still in use");
    });
    // Put the file back and the same configuration succeeds, with both entries.
    steps.push_back([&] { writeFile(sogou, kSogouText); save(); });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready, "C.15 repairing the file fixes the load");
        checkEqual(engine.dictionaryError(), "", "C.16 with no error left");
        checkCount(engine.loadedEntryCount(), size_t{2}, "C.17 and both entries now take part");
        check(driverA.has("sougou", "搜构"), "C.18 the repaired entry's word is reachable");
    });

    // --- Case D: any enabled entry that fails keeps the previous lexicon. -----
    // Each failure is tried on top of the WORKING two-entry lexicon, so "the old
    // lexicon is still usable" means the words from C.18 rather than an empty one.
    // The failing entry is the SECOND one, so a load that skipped it instead of
    // refusing would succeed and the assertions below would fail.
    auto expectFailure = [&](const std::string &reason, const char *label) {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Failed, std::string(label) + ": the load failed");
        checkHas(engine.dictionaryError(), "附加词库“坏的”", std::string(label) + ": the message names the entry");
        checkHas(engine.dictionaryError(), reason, std::string(label) + ": and states the reason");
        check(driverA.has("zhuce", "注册表"), std::string(label) + ": the old lexicon still answers");
        check(driverA.has("sougou", "搜构"), std::string(label) + ": including its second entry");
        // Both dictionary options fall back to the lexicon in use, so the config
        // tool shows what is actually loaded rather than the rejected save.
        checkEqual(engine.dictionaryPath(), base, std::string(label) + ": DictionaryPath reverted");
        checkCount(loadedNames().size(), size_t{2}, std::string(label) + ": and so did the attached list");
        checkEqual(loadedNames().at(0), "甲", std::string(label) + ": the first entry is unchanged");
        checkEqual(loadedNames().at(1), "乙", std::string(label) + ": and the second one too");
    };
    const std::vector<std::pair<std::string, std::string>> badFiles{
        {missing, "文件不存在"},
        {fifo, "不是普通文件"},
        {directory.path, "不是普通文件"},
        {huge, "文件超过 64 MiB 上限"},
        {broken, "格式无法识别或内容无效"},
        {"relative.tsv", "路径必须是绝对路径"},
        // A valid file whose text is not a usable lexicon: the importer reads the
        // whole document before it fails, so nothing half-built can escape.
        {configFile, "格式无法识别或内容无效"},
    };
    for (const auto &failure : badFiles) {
        steps.push_back([&, failure] { savedList({entry("甲", tsv), entry("坏的", failure.first)}); });
        steps.push_back([&, failure] { expectFailure(failure.second, "D"); });
    }
    // The failure message never quotes the file's content. The broken fixture is an
    // invalid UTF-8 byte sequence whose first bytes are a UTF-16LE mark, so the mark
    // itself is a string a message could leak; neither it nor any word from a
    // lexicon may appear.
    steps.push_back([&] { writeFile(broken, "zhu'ce\t泄密词条\t900\nnot a lexicon\n"); });
    steps.push_back([&] { savedList({entry("甲", tsv), entry("坏的", broken)}); });
    steps.push_back([&] {
        checkHas(engine.dictionaryError(), "附加词库“坏的”", "D.leak the entry is named");
        checkLacks(engine.dictionaryError(), "泄密词条", "D.leak the content is not quoted");
        checkLacks(engine.dictionaryError(), "not a lexicon", "D.leak and neither is any other line");
        check(driverA.has("zhuce", "注册表"), "D.leak the old lexicon still answers");
    });
    // One row past the per-file import limit is refused as an invalid file rather
    // than by the merge, which is the other half of the size rule.
    steps.push_back([&] { savedList({entry("甲", tsv), entry("坏的", maximal)}); });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Failed,
              "D.19 a 250,000-row file on top of a non-empty base exceeds the merge ceiling");
        checkHas(engine.dictionaryError(), "附加词库“坏的”", "D.20 and says which entry did it");
        checkHas(engine.dictionaryError(), "合并后超过 250000 条", "D.21 with the merge ceiling as the reason");
        check(driverA.has("zhuce", "注册表"), "D.22 the old lexicon still answers");
        // The ceiling is the CORE's, and this is the same statement made without the
        // adapter: the union of the two files cannot be built. That is what makes the
        // message above a report of the real limit rather than of a guess.
        ChengyinDictionary *first = chengyin_dictionary_new_tsv(
            reinterpret_cast<const uint8_t *>(kAttachedTsv), std::strlen(kAttachedTsv));
        std::ifstream stream(maximal, std::ios::binary);
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        const auto big = buffer.str();
        ChengyinDictionary *second =
            chengyin_dictionary_new_import(reinterpret_cast<const uint8_t *>(big.data()), big.size());
        check(second != nullptr, "D.23 the 250,000-row file is a valid lexicon on its own");
        const ChengyinDictionary *handles[] = {first, second};
        ChengyinDictionary *merged = chengyin_dictionary_merge_all(handles, 2);
        check(merged == nullptr, "D.24 and the core itself refuses to merge it with another one");
        chengyin_dictionary_free(merged);
        chengyin_dictionary_free(second);
        chengyin_dictionary_free(first);
    });
    // Repairing the entry, on the same list, makes the load succeed again.
    steps.push_back([&] { savedList({entry("甲", tsv), entry("乙", sogou)}); });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready, "D.25 the repaired list loads");
        checkCount(engine.loadedEntryCount(), size_t{2}, "D.26 with both entries");
        checkEqual(engine.dictionaryError(), "", "D.27 and no error left behind");
    });

    // --- Case E: the ceilings are reported, never applied silently. -----------
    // The list may hold 64 entries, but each enabled entry needs a merge slot and
    // the base lexicon holds one, so 64 enabled entries cannot all take part. What
    // matters is that the save says so instead of quietly merging the first 63.
    steps.push_back([&] {
        std::vector<chengyin::DictionaryEntryConfig> full;
        for (int i = 0; i < 64; ++i) { full.push_back(entry("词库 " + std::to_string(i), tsv)); }
        savedList(full);
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Failed,
              "E.1 64 enabled entries do not all fit in one merge");
        checkHas(engine.dictionaryError(), "附加词库", "E.2 the message names the problem");
        check(engine.dictionaryError().find("最多") != std::string::npos, "E.3 and states the ceiling",
              engine.dictionaryError());
        check(driverA.has("zhuce", "注册表"), "E.4 the previous lexicon still answers");
    });
    // A list LONGER than the ceiling is refused before anything is loaded, even
    // when every entry in it is disabled and nothing would actually be merged.
    steps.push_back([&] {
        std::vector<chengyin::DictionaryEntryConfig> over;
        for (int i = 0; i < 65; ++i) { over.push_back(entry("词库 " + std::to_string(i), tsv, false)); }
        savedList(over);
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Failed,
              "E.5 a 65-entry list is refused even with every entry disabled");
        checkHas(engine.dictionaryError(), "最多 64 个", "E.6 and the message states the list ceiling");
        // Refused means not adopted: the engine still describes the lexicon it has.
        checkCount(loadedNames().size(), size_t{2}, "E.7 the running list is unchanged");
        checkEqual(loadedNames().at(1), "乙", "E.8 entry by entry");
        check(driverA.has("sougou", "搜构"), "E.9 and the lexicon still answers");
    });
    // 63 enabled entries plus the base is exactly the core's own 64-handle limit,
    // so this is the largest attached list that can work -- the boundary the
    // refusal above is drawn at.
    steps.push_back([&] {
        std::vector<chengyin::DictionaryEntryConfig> limit;
        for (int i = 0; i < 63; ++i) { limit.push_back(entry("词库 " + std::to_string(i), tsv)); }
        savedList(limit);
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready,
              "E.10 63 enabled entries are exactly the merge limit and do load");
        checkCount(engine.loadedEntryCount(), size_t{63}, "E.11 all of them took part");
        check(driverA.has("zhuce", "注册表"), "E.12 and their words are reachable");
        savedList({entry("甲", tsv), entry("乙", sogou)});
    });
    steps.push_back([&] {
        checkCount(engine.loadedEntryCount(), size_t{2}, "E.13 the list goes back to two entries");
    });

    // --- Case F: rapid saves adopt only the last one. -------------------------
    // The loader keeps one pending request and publishes only the newest
    // generation, so several saves in a row must become several requests and
    // exactly one adoption -- the last one.
    steps.push_back([&] {
        const auto before = engine.dictionaryRequests();
        savedList({entry("甲", tsv)});
        savedList({entry("乙", sogou)});
        savedList({entry("丙", utf16)});
        savedList({entry("丁", binary)});
        check(engine.dictionaryRequests() > before, "F.1 every save became a request",
              std::to_string(engine.dictionaryRequests() - before) + " request(s)");
        checkCount(engine.dictionaryRequests() - before, uint64_t{4}, "F.2 one request per save");
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready, "F.3 the last save settled");
        checkCount(engine.loadedEntryCount(), size_t{1}, "F.4 with exactly one entry");
        checkCount(loadedNames().size(), size_t{1}, "F.5 the list holds one entry");
        checkEqual(loadedNames().at(0), "丁", "F.6 and it is the LAST one saved");
        // The generation that won is the newest request, which is what rules out an
        // earlier answer overwriting a later one.
        checkCount(engine.loadedRequest(), engine.dictionaryRequests(), "F.7 the newest request is the one adopted");
        check(driverA.has("erjin", "二进制"), "F.8 the last entry's word is reachable");
        check(!driverA.has("zhuce", "注册表"), "F.9 an earlier entry's word is not");
        check(!driverA.has("shiliu", "十六"), "F.10 neither is the one before it");
    });
    // The same statement with the saves spaced far enough apart to each settle: the
    // final state is the final list either way.
    steps.push_back([&] { savedList({entry("甲", tsv)}); });
    steps.push_back([&] {
        checkEqual(loadedNames().at(0), "甲", "F.11 a settled intermediate save is adopted in turn");
        savedList({entry("乙", sogou)});
    });
    steps.push_back([&] {
        checkEqual(loadedNames().at(0), "乙", "F.12 and so is the one after it");
        check(!driverA.has("zhuce", "注册表"), "F.13 with the earlier word gone");
    });

    // --- Case G: a save in the middle of a composition changes nothing. -------
    steps.push_back([&] { savedList({entry("甲", tsv), entry("乙", sogou)}); });
    steps.push_back([&] {
        driverA.type("zhuce");
        checkEqual(driverA.preedit(), "zhuce", "G.1 a composition is established");
        const auto before = driverA.page();
        check(!before.empty(), "G.2 with candidates on screen");
        // A save that changes the lexicon and the page width at once, both of which
        // the core refuses to apply to a busy session.
        config.pageSize.setValue(chengyin::PageSize::Nine);
        savedList({entry("丙", utf16)});
        checkEqual(driverA.preedit(), "zhuce", "G.3 the save did not disturb the composition");
        check(driverA.page() == before, "G.4 the candidate rows are unchanged");
        driverA.clearCommitted();
        driverA.commit();
        checkEqual(driverA.committed(), "注册表", "G.5 the pending composition commits its own word");
    });
    steps.push_back([&] {
        // The new lexicon is picked up as soon as the session is idle, so the very
        // next composition uses it.
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready, "G.6 the save settled");
        check(driverA.has("shiliu", "十六"), "G.7 the next input uses the new lexicon");
        check(!driverA.has("zhuce", "注册表"), "G.8 and not the old one");
        check(driverA.has("jichu", "基楚"), "G.9 while the base lexicon stays");
        // A second context sees the same lexicon, so the swap is engine-wide.
        check(driverB.has("shiliu", "十六"), "G.10 another context uses it too");
        check(!driverB.has("zhuce", "注册表"), "G.11 with the same result");
        config.pageSize.setValue(chengyin::PageSize::Five);
    });

    // --- Case H: the configuration round-trips. -------------------------------
    steps.push_back([&] {
        // A display name is free text, so it may hold quotes, spaces and characters a
        // path never would; the file it names is a real one, so this save succeeds.
        savedList({entry("“带引号” 的名字", tsv)});
    });
    steps.push_back([&] {
        chengyin::EngineConfig stored(base);
        readPersisted(stored);
        checkCount(stored.dictionaries->size(), size_t{1}, "H.1 the saved list has one entry");
        checkEqual(*stored.dictionaries->at(0).name, "“带引号” 的名字", "H.2 the display name round-trips");
        checkEqual(*stored.dictionaries->at(0).path, tsv, "H.3 and so does the path");
        check(*stored.dictionaries->at(0).enabled, "H.4 with its enabled flag");
        checkEqual(engine.dictionaryEntries().at(0).name, "“带引号” 的名字",
                   "H.4b and the engine holds the same name");
    });
    // A save whose lexicon cannot be loaded is never written: the file keeps the
    // configuration that is actually in use, which is what makes the round-trip
    // above worth asserting at all.
    std::string beforeSave;
    steps.push_back([&] { beforeSave = readFile(configFile); savedList({entry("坏的", missing)}); });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Failed,
              "H.4c a list naming a missing file fails");
        checkEqual(readFile(configFile), beforeSave, "H.4d and the rejected save never reached the disk");
        chengyin::EngineConfig stored(base);
        readPersisted(stored);
        checkEqual(*stored.dictionaries->at(0).name, "“带引号” 的名字",
                   "H.4e the file still holds the lexicon in use");
    });
    steps.push_back([&] { savedList({entry("甲", tsv), entry("乙", sogou, false)}); });
    steps.push_back([&] {
        chengyin::EngineConfig stored(base);
        readPersisted(stored);
        checkCount(stored.dictionaries->size(), size_t{2}, "H.5 both entries are persisted");
        checkEqual(*stored.dictionaries->at(0).name, "甲", "H.6 the first name");
        checkEqual(*stored.dictionaries->at(1).name, "乙", "H.7 the second name");
        check(*stored.dictionaries->at(0).enabled, "H.8 the first is persisted as enabled");
        check(!*stored.dictionaries->at(1).enabled, "H.9 the disabled one as disabled");
        checkEqual(*stored.dictionaries->at(1).path, sogou, "H.10 the disabled entry's path is persisted too");
        // And the engine agrees with what was written, entry for entry.
        checkCount(engine.dictionaryEntries().size(), size_t{2}, "H.11 the engine holds both entries");
        checkCount(loadedEnabled().size(), size_t{2}, "H.12 with their flags");
        check(!loadedEnabled().at(1), "H.13 the second one disabled");
    });

    // --- Case I: a profile written before the option existed. -----------------
    steps.push_back([&] {
        // Exactly the file an older release would have left: DictionaryPath and the
        // settings, no Dictionaries section at all.
        writeFile(configFile, "DictionaryPath=" + base + "\nPageSize=7\n");
        engine.reloadConfig();
    });
    steps.push_back([&] {
        checkCount(engine.dictionaryEntries().size(), size_t{0}, "I.1 an old profile has no attached entries");
        checkCount(engine.loadedEntryCount(), size_t{0}, "I.2 and merges nothing");
        check(driverA.has("jichu", "基楚"), "I.3 the base lexicon still answers");
        checkCount(engine.settings().pageSize, 7, "I.4 while the settings it did carry are adopted");
        // The old file is not rewritten by reading it: what a user has on disk stays
        // exactly as it was until they save.
        const auto onDisk = readFile(configFile);
        checkHas(onDisk, "DictionaryPath=" + base, "I.5 the old file still holds its own content");
        checkLacks(onDisk, "[Dictionaries/0]", "I.6 and gained no section from being read");
    });
    steps.push_back([&] {
        // Saving from that state writes the option out, and only then does a
        // Dictionaries section appear.
        savedList({entry("甲", tsv)});
    });
    steps.push_back([&] {
        const auto onDisk = readFile(configFile);
        checkHas(onDisk, "[Dictionaries/0]", "I.7 a save writes the attached list out");
        chengyin::EngineConfig stored(base);
        readPersisted(stored);
        checkCount(stored.dictionaries->size(), size_t{1}, "I.8 and reading it back returns the entry");
        checkEqual(*stored.dictionaries->at(0).path, tsv, "I.9 with its path");
    });

    // --- Case J: what reaches the log, and what must not. ---------------------
    // Every message this feature writes is captured into a stream the test owns,
    // which is the only way to assert on an FCITX_INFO/FCITX_WARN line.
    steps.push_back([&] {
        failing.str("");
        fcitx::Log::setLogStream(failing);
        savedList({entry("甲", tsv), entry("坏的", missing)});
    });
    steps.push_back([&] {
        const auto logged = failing.str();
        fcitx::Log::setLogStream(std::cerr);
        checkHas(logged, "附加词库“坏的”", "J.1 the failure is logged once");
        checkHas(logged, "文件不存在", "J.2 with its reason");
        checkLacks(logged, "泄密词条", "J.3 and never any lexicon content");
        checkLacks(logged, "zhu'ce", "J.4 not even a spelling from a file");
        check(engine.dictionaryError().find("泄密词条") == std::string::npos,
              "J.5 and the panel message carries none either", engine.dictionaryError());
    });
    steps.push_back([&] {
        // A successful load logs exactly one line, and it names counts rather than
        // any word: base rows, how many entries took part, and the merged total.
        successful.str("");
        fcitx::Log::setLogStream(successful);
        savedList({entry("甲", tsv), entry("乙", sogou)});
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready, "J.6 the save settled");
        const auto logged = successful.str();
        fcitx::Log::setLogStream(std::cerr);
        checkHas(logged, "基础 " + std::to_string(engine.baseEntryCount()) + " 条 + 附加 2 个，合计 " +
                             std::to_string(engine.mergedEntryCount()) + " 条",
                 "J.7 the one summary line reports base, attached and total");
        checkLacks(logged, "注册表", "J.8 and quotes no word");
        checkLacks(logged, "搜构", "J.9 from any entry");
    });

    // --- Case K: a settings-only save still does not reload. ------------------
    // The lexicon options are the only ones that rebuild anything, and an attached
    // list is exactly why that distinction is worth restating.
    steps.push_back([&] {
        config.associations.setValue(false);
        save();
        check(engine.reloadState() != chengyin::Engine::ReloadState::Loading,
              "K.1 a settings-only save never enters the loading state");
        checkEqual(engine.dictionaryError(), "", "K.2 and reports no lexicon error");
    });
    steps.push_back([&] {
        checkEqual(loadedNames().at(0), "甲", "K.3 the attached list is what is still loaded");
        config.associations.setValue(true);
        save();
    });
    steps.push_back([&] {
        // The other direction of the same rule: moving only an ENTRY does reload.
        const auto before = engine.dictionaryRequests();
        savedList({entry("丙", utf16)});
        checkCount(engine.dictionaryRequests() - before, uint64_t{1}, "K.4 a new entry is one new request");
    });
    steps.push_back([&] {
        check(engine.reloadState() == chengyin::Engine::ReloadState::Ready, "K.5 and it loads");
        checkEqual(loadedNames().at(0), "丙", "K.6 the new entry replaced the old list");
    });

    size_t step = 0;
    const auto deadline = fcitx::now(CLOCK_MONOTONIC) + 120ULL * 1000000ULL;
    auto timer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0,
        [&](fcitx::EventSourceTime *source, uint64_t) {
            if (fcitx::now(CLOCK_MONOTONIC) >= deadline) {
                check(false, "the whole suite finished within the deadline");
                loop.exit();
                return true;
            }
            // A step runs only once the engine has settled, so a step that changed a
            // lexicon waits and a settings-only step does not. The deadline above is
            // what turns a lexicon that never settles into a failure rather than a
            // hang; nothing here sleeps and hopes.
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
              << "Fcitx5 attached-lexicon test: " << failures << " failed assertion(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
