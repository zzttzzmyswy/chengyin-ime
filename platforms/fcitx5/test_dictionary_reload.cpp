#include "engine.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>

class Context final : public fcitx::InputContext {
public:
    explicit Context(fcitx::InputContextManager &manager) : InputContext(manager, "reload-test") { created(); focusIn(); }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "reload-test"; }
    void commitStringImpl(const std::string &text) override { committed += text; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
};

struct TemporaryDirectory {
    TemporaryDirectory() {
        char pattern[] = "/tmp/myswy-reload-XXXXXX";
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
    const auto configHome = directory.path + "/config";
    assert(::setenv("XDG_CONFIG_HOME", configHome.c_str(), 1) == 0);
    // Fcitx-specific overrides must not send fixture writes to a user's config.
    assert(::setenv("FCITX_CONFIG_HOME", (configHome + "/fcitx5").c_str(), 1) == 0);
    const auto first = directory.path + "/first.tsv";
    const auto second = directory.path + "/second.tsv";
    const auto bad = directory.path + "/bad.tsv";
    const auto fifo = directory.path + "/pipe.tsv";
    const auto huge = directory.path + "/huge.tsv";
    writeFile(first, "ni'hao\t新词一\t10\n");
    writeFile(second, "ni'hao\t新词二\t20\n");
    writeFile(bad, "nihao\t坏词典\t0\n");
    writeFile(huge, "");
    std::filesystem::resize_file(huge, 64 * 1024 * 1024 + 1);
    assert(::mkfifo(fifo.c_str(), 0600) == 0);

    fcitx::InputContextManager manager;
    fcitx::EventLoop loop;
    myswy::Engine engine(manager, loop);
    Context a(manager), b(manager);
    const fcitx::InputMethodEntry entry("myswy", "Myswy", "zh_CN", "myswy");
    auto press = [&](Context &ic, fcitx::KeySym sym) {
        fcitx::KeyEvent event(&ic, fcitx::Key(sym));
        engine.keyEvent(entry, event);
        assert(event.accepted());
    };
    auto type = [&](Context &ic) {
        for (const auto c : std::string("nihao")) { press(ic, static_cast<fcitx::KeySym>(c)); }
    };
    auto candidate = [&](Context &ic) { return ic.inputPanel().candidateList()->candidate(0).text().toString(); };
    auto configure = [&](const std::string &path) {
        fcitx::RawConfig config;
        config.setValueByPath("DictionaryPath", path);
        engine.setConfig(config);
        assert(engine.reloadState() == myswy::Engine::ReloadState::Loading);
    };
    auto persistedPath = [&] {
        myswy::EngineConfig config;
        fcitx::readAsIni(config, "conf/myswy.conf");
        return *config.dictionaryPath;
    };
    auto state = [&](Context &ic) { return ic.propertyAs<myswy::State>("myswyState"); };
    std::weak_ptr<const myswy::DictionarySnapshot> retired;

    std::vector<std::function<void()>> steps;
    steps.push_back([&] {
        type(a);
        assert(candidate(a) == "你好");
        configure(first);
        // File I/O never runs here: even while the load is pending, input works.
        type(b);
        assert(candidate(b) == "你好");
        press(b, FcitxKey_space);
    });
    steps.push_back([&] {
        assert(engine.reloadState() == myswy::Engine::ReloadState::Ready);
        assert(engine.dictionaryPath() == first);
        assert(persistedPath() == first);
        assert(candidate(a) == "你好");
        assert(state(a)->dictionary != state(b)->dictionary);
        assert(std::filesystem::remove(first)); // no subsequent per-context/key reads
        type(b);
        assert(candidate(b) == "新词一");
        press(b, FcitxKey_space);
        press(a, FcitxKey_space);
        assert(a.committed == "你好");
        assert(state(a)->dictionary == state(b)->dictionary);
        type(a);
        assert(candidate(a) == "新词一");
        retired = state(a)->dictionary;
        configure(second);
    });
    steps.push_back([&] {
        assert(engine.dictionaryPath() == second);
        assert(candidate(a) == "新词一");
        auto oldList = a.inputPanel().candidateList();
        oldList->candidate(0).select(&a);
        assert(a.committed == "你好新词一");
        type(a);
        assert(candidate(a) == "新词二");
        press(a, FcitxKey_Escape);
        type(b);
        assert(candidate(b) == "新词二");
        configure(bad);
    });
    auto checkFailure = [&] {
        assert(engine.reloadState() == myswy::Engine::ReloadState::Failed);
        assert(!engine.dictionaryError().empty());
        assert(engine.dictionaryPath() == second);
        assert(persistedPath() == second); // rejected config is never saved
        assert(candidate(b) == "新词二");
        fcitx::RawConfig displayed;
        engine.getConfig()->save(displayed);
        assert(displayed.get("DictionaryPath")->value() == second);
    };
    for (const auto &path : {directory.path + "/missing.tsv", fifo, huge, directory.path, std::string("relative.tsv")}) {
        steps.push_back([&, path] { checkFailure(); configure(path); });
    }
    steps.push_back([&] {
        checkFailure();
        writeFile(bad, std::string("\xff", 1));
        configure(bad);
    });
    steps.push_back([&] {
        checkFailure();
        writeFile(bad, "ni'hao\t重复\t1\nni'hao\t重复\t2\n");
        configure(bad);
    });
    steps.push_back([&] {
        checkFailure();
        // All callbacks happen after this event: only the last request may win.
        configure(second);
        configure(bad);
        configure("");
    });
    steps.push_back([&] {
        assert(engine.reloadState() == myswy::Engine::ReloadState::Ready);
        assert(engine.dictionaryPath().empty());
        assert(persistedPath().empty());
        assert(candidate(b) == "新词二");
        press(b, FcitxKey_space);
        type(b);
        assert(candidate(b) == "你好");
        press(b, FcitxKey_Escape);
        // Manual edits plus reloadConfig use the same background loading path.
        writeFile(second, "ni'hao\t重新加载\t1\n");
        myswy::EngineConfig disk;
        disk.dictionaryPath.setValue(second);
        assert(fcitx::safeSaveAsIni(disk, "conf/myswy.conf"));
        engine.reloadConfig();
    });
    steps.push_back([&] {
        assert(engine.dictionaryPath() == second);
        type(b);
        assert(candidate(b) == "重新加载");
        press(b, FcitxKey_space);
        assert(state(a)->dictionary == state(b)->dictionary);
        // A valid dictionary can still fail to persist. Preserve runtime input
        // and report that restart durability was not achieved.
        const auto file = configHome + "/fcitx5/conf/myswy.conf";
        std::filesystem::rename(file, file + ".saved");
        std::filesystem::create_directory(file);
        configure("");
    });
    steps.push_back([&] {
        assert(engine.reloadState() == myswy::Engine::ReloadState::Ready);
        assert(engine.dictionaryPath().empty());
        assert(engine.dictionaryError().find("保存失败") != std::string::npos);
        type(a);
        assert(candidate(a) == "你好");
        press(a, FcitxKey_space);
        const auto file = configHome + "/fcitx5/conf/myswy.conf";
        std::filesystem::remove(file);
        std::filesystem::rename(file + ".saved", file);
    });

    size_t step = 0;
    const auto deadline = fcitx::now(CLOCK_MONOTONIC) + 10 * 1000000;
    auto timer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC), 0,
        [&](fcitx::EventSourceTime *source, uint64_t) {
            assert(fcitx::now(CLOCK_MONOTONIC) < deadline);
            if (engine.reloadState() != myswy::Engine::ReloadState::Loading && step < steps.size()) {
                steps[step++]();
            }
            if (step == steps.size() && retired.expired()) { loop.exit(); }
            else { source->setNextInterval(1000); source->setOneShot(); }
            return true;
        });
    loop.exec();
    assert(step == steps.size());
    assert(retired.expired());
    // Shutdown with pending work/queued callbacks must not access a dead Engine.
    configure(second);
    std::cout << "Fcitx5 asynchronous dictionary reload test passed (" << steps.size() << " stages)\n";
}
