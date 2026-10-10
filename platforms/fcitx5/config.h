// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <string>
#include <utility>
#include <vector>
#include "chengyin_ime.h"
#include "dictionary_loader.h"

namespace chengyin {
// Distribution packages compile in the lexicon they install, so a profile with
// no saved conf/chengyin.conf starts on the full vocabulary rather than the
// 98-entry demo. Plain source builds leave this empty and keep the demo.
#ifndef CHENGYIN_DEFAULT_DICTIONARY_PATH
#define CHENGYIN_DEFAULT_DICTIONARY_PATH ""
#endif

// The candidate rows a session pages at before anything configured it, which is
// also the core's own default page width (`chengyin_core::MAX_CANDIDATES`). The
// adapter builds the candidate list at the width the core was actually
// configured with, so this value only ever describes a brand-new session.
inline constexpr int kUnconfiguredPageSize = 9;

// Candidate widths a user may choose, matching the Windows settings page. This is
// an enum rather than an IntConstrain range on purpose: the config tool then
// offers exactly these three choices, and a hand-edited value outside the set
// keeps the option's default instead of being silently clamped to a width the
// user never picked.
enum class PageSize { Five = 0, Seven = 1, Nine = 2 };
FCITX_CONFIG_ENUM_NAME(PageSize, "5", "7", "9")

// Which Shift keys participate in the tap-to-switch gesture. The Windows
// settings page offers exactly these three choices with this wording, and
// "左 Shift" is the default there too. An enum rather than an integer range, for
// the same reason PageSize is one: the config tool only offers these values, and
// a hand-edited value outside the set keeps the option's default.
enum class ShiftSwitch { Disabled = 0, Left = 1, Both = 2 };
FCITX_CONFIG_ENUM_NAME(ShiftSwitch, "不使用 Shift", "左 Shift", "左右 Shift")

// One switch paired with the matching flag it contributes and the rule it names.
// The two sub-configs below write each switch down exactly once, so the option
// list, the flag word and the tests that walk bit by bit cannot drift apart.
struct MatchingSwitch {
    const fcitx::Option<bool> *option;
    uint32_t flag;
    const char *rule;
};

using FuzzySwitchList = std::array<MatchingSwitch, 11>;
using CorrectionSwitchList = std::array<MatchingSwitch, 4>;
using MatchingSwitchList = std::array<MatchingSwitch, 15>;

// The Fcitx config tool renders these through its own generated interface. Every
// text is Chinese because that is the only language this batch ships, and the
// labels are the Windows settings page's own wording so both platforms describe
// the same behaviour; the option holding each sub-config supplies its group
// heading. All of them default to off, exactly as on Windows.
FCITX_CONFIGURATION(FuzzyConfig,
                    fcitx::Option<bool> zhZ{this, "ZhZ", "zh ↔ z（双向匹配）", false};
                    fcitx::Option<bool> chC{this, "ChC", "ch ↔ c（双向匹配）", false};
                    fcitx::Option<bool> shS{this, "ShS", "sh ↔ s（双向匹配）", false};
                    fcitx::Option<bool> nL{this, "NL", "n ↔ l（双向匹配）", false};
                    fcitx::Option<bool> fH{this, "FH", "f ↔ h（双向匹配）", false};
                    fcitx::Option<bool> lR{this, "LR", "l ↔ r（双向匹配）", false};
                    fcitx::Option<bool> anAng{this, "AnAng", "an ↔ ang（双向匹配）", false};
                    fcitx::Option<bool> enEng{this, "EnEng", "en ↔ eng（双向匹配）", false};
                    fcitx::Option<bool> inIng{this, "InIng", "in ↔ ing（双向匹配）", false};
                    fcitx::Option<bool> ianIang{this, "IanIang", "ian ↔ iang（双向匹配）", false};
                    fcitx::Option<bool> uanUang{this, "UanUang", "uan ↔ uang（双向匹配）", false};
                    FuzzySwitchList switches() const {
                        return {{{&zhZ, CHENGYIN_FUZZY_ZH_Z, "zh/z"},
                                {&chC, CHENGYIN_FUZZY_CH_C, "ch/c"},
                                {&shS, CHENGYIN_FUZZY_SH_S, "sh/s"},
                                {&nL, CHENGYIN_FUZZY_N_L, "n/l"},
                                {&fH, CHENGYIN_FUZZY_F_H, "f/h"},
                                {&lR, CHENGYIN_FUZZY_L_R, "l/r"},
                                {&anAng, CHENGYIN_FUZZY_AN_ANG, "an/ang"},
                                {&enEng, CHENGYIN_FUZZY_EN_ENG, "en/eng"},
                                {&inIng, CHENGYIN_FUZZY_IN_ING, "in/ing"},
                                {&ianIang, CHENGYIN_FUZZY_IAN_IANG, "ian/iang"},
                                {&uanUang, CHENGYIN_FUZZY_UAN_UANG, "uan/uang"}}};
                    })

FCITX_CONFIGURATION(CorrectionConfig,
                    fcitx::Option<bool> swap{this, "Swap", "相邻字母按反（示例：zhnag → zhang）", false};
                    fcitx::Option<bool> omit{this, "Omit", "漏按一个字母（示例：png → ping）", false};
                    fcitx::Option<bool> neighbor{this, "Neighbor", "QWERTY 相邻键误按（示例：hso → hao）", false};
                    fcitx::Option<bool> repeat{this, "Repeat", "重复按键（示例：shii → shi）", false};
                    CorrectionSwitchList switches() const {
                        return {{{&swap, CHENGYIN_CORRECT_SWAP, "swap"},
                                {&omit, CHENGYIN_CORRECT_OMIT, "omit"},
                                {&neighbor, CHENGYIN_CORRECT_NEIGHBOR, "neighbor"},
                                {&repeat, CHENGYIN_CORRECT_REPEAT, "repeat"}}};
                    })

// One entry of the attached-lexicon list. This is a sub-configuration rather than
// a plain string so the framework's own list editor renders it as a list of
// entries it can add to and remove from; `Enabled` defaults to true, which is what
// makes a newly added file take effect without a second step. An empty Name falls
// back to the file name, and the file itself is never copied or modified: the
// entry is a reference to a path the user owns.
FCITX_CONFIGURATION(DictionaryEntryConfig,
                    fcitx::Option<std::string> name{this, "Name",
                        "显示名（留空则用文件名）", ""};
                    fcitx::Option<std::string> path{this, "Path",
                        "词库文件绝对路径（SCEL / UTF-8 或 UTF-16 文本 / TSV / 本项目二进制）", ""};
                    fcitx::Option<bool> enabled{this, "Enabled", "启用该词库", true};)

// The 11 + 4 switches in bit order: phonetic pairs first, keyboard-error rules
// after, so the table index and the flag's bit position agree.
inline MatchingSwitchList matchingSwitchList(const FuzzyConfig &fuzzy, const CorrectionConfig &correction) {
    const auto pairs = fuzzy.switches();
    const auto errors = correction.switches();
    MatchingSwitchList all{};
    for (size_t i = 0; i < pairs.size(); ++i) { all[i] = pairs[i]; }
    for (size_t i = 0; i < errors.size(); ++i) { all[pairs.size() + i] = errors[i]; }
    return all;
}

class EngineConfig final : public fcitx::Configuration {
public:
    // The initial value doubles as the option's default, so a configuration file
    // that omits DictionaryPath resolves to it and never silently falls back to
    // another lexicon. Clearing the field explicitly selects the demo.
    //
    // PageSize defaults to 5 for the same reason it does on Windows; Linux used
    // to hardcode 9, and that change is recorded in this platform's README.
    //
    // Learning is the switch the kDefaultLearning constant used to stand in for:
    // its wording is the Windows settings page's own, and it defaults to on there
    // as well. Turning it off stops this build from recording selections and from
    // writing the profile, and tells the core to drop the session-local recency
    // and phrase state it had accumulated.
    //
    // DefaultEnglish, ShiftSwitch and ChinesePunctuation are the Windows 输入模式 /
    // 标点与联想 page's own three settings, with its own wording and defaults. The
    // adapter reads them itself instead of handing them to the core, but they are
    // part of EngineSettings all the same: a save that moves only one of them must
    // still count as a settings change, or setConfig() would rebuild the lexicon
    // for an option that has nothing to do with it.
    explicit EngineConfig(std::string defaultDictionaryPath = CHENGYIN_DEFAULT_DICTIONARY_PATH)
        : dictionaryPath{this, "DictionaryPath",
              "词典 TSV 绝对路径（默认使用随包安装的完整词典；清空则使用内置演示词典）",
              std::move(defaultDictionaryPath)},
          pageSize{this, "PageSize", "每页候选数", PageSize::Five},
          associations{this, "Associations",
              "提交中文后显示联想词；Tab 或鼠标确认，空格、数字和 Enter 交给应用", true},
          learning{this, "Learning", "根据选词习惯排序（本机保存，不联网）", true},
          defaultEnglish{this, "DefaultEnglish", "启动输入服务时默认使用英文", false},
          shiftSwitch{this, "ShiftSwitch", "Shift 切换键", ShiftSwitch::Left},
          chinesePunctuation{this, "ChinesePunctuation", "中文模式使用中文标点", true},
          fuzzy{this, "Fuzzy", "模糊音（双向匹配）", FuzzyConfig{}},
          correction{this, "Correction", "常见键盘失误", CorrectionConfig{}},
          dictionaries{this, "Dictionaries",
              "附加词库：每项一个文件，可单独启用或停用；与基础词库（DictionaryPath）合并使用",
              {}} {}
    const char *typeName() const override { return "ChengyinConfig"; }

    // The attached list in the form the loader takes. An entry whose Path is blank
    // is not a usable lexicon -- there is nothing to open -- but it is still carried
    // through so the configuration round-trips byte for byte; the loader refuses it
    // by name if it is enabled, which is what makes a half-filled entry visible
    // instead of silently ignored.
    std::vector<DictionarySource> dictionarySources() const {
        std::vector<DictionarySource> sources;
        sources.reserve(dictionaries->size());
        for (const auto &entry : *dictionaries) {
            sources.push_back(DictionarySource{*entry.name, *entry.path, *entry.enabled});
        }
        return sources;
    }

    // How many entries the list holds. Compared separately from the list's content
    // so a save that only adds or removes an entry still counts as a lexicon change,
    // and so the 64-entry ceiling can be reported.
    size_t dictionaryCount() const { return dictionaries->size(); }

    bool dictionaryLimitExceeded() const { return dictionaryCount() > kMaxEntries; }

    // The width the core is configured with, counted in candidate rows.
    int pageSizeValue() const {
        switch (*pageSize) {
        case PageSize::Seven: return 7;
        case PageSize::Nine: return 9;
        case PageSize::Five: break;
        }
        return 5;
    }

    MatchingSwitchList matchingSwitches() const { return matchingSwitchList(*fuzzy, *correction); }

    size_t switchCount() const { return matchingSwitches().size(); }
    const char *switchRule(size_t index) const { return matchingSwitches()[index].rule; }
    uint32_t switchFlag(size_t index) const { return matchingSwitches()[index].flag; }

    // Turn every switch off, then turn exactly one back on by its position in the
    // bit-ordered list. Walking this list is what lets a test prove that each
    // option contributes the flag its own entry names: a table wired to the wrong
    // bit enables the wrong rule, and the wrong word is what the test observes.
    void selectMatchingSwitch(size_t index) {
        const auto switches = matchingSwitches();
        for (size_t i = 0; i < switches.size(); ++i) {
            const_cast<fcitx::Option<bool> *>(switches[i].option)->setValue(i == index);
        }
    }

    void clearMatchingSwitches() {
        for (const auto &entry : matchingSwitches()) {
            const_cast<fcitx::Option<bool> *>(entry.option)->setValue(false);
        }
    }

    // The flag word handed to chengyin_session_configure_matching.
    uint32_t matchingFlags() const {
        uint32_t flags = 0;
        for (const auto &entry : matchingSwitches()) {
            if (entry.option->value()) { flags |= entry.flag; }
        }
        return flags;
    }

    fcitx::Option<std::string> dictionaryPath;
    fcitx::Option<PageSize> pageSize;
    fcitx::Option<bool> associations;
    fcitx::Option<bool> learning;
    fcitx::Option<bool> defaultEnglish;
    fcitx::Option<ShiftSwitch> shiftSwitch;
    fcitx::Option<bool> chinesePunctuation;
    fcitx::Option<FuzzyConfig> fuzzy;
    fcitx::Option<CorrectionConfig> correction;
    // The attached-lexicon list. A List of sub-configs is what makes the config
    // tool render it as a list it can add to and remove from rather than one
    // free-text field; a profile written before this option existed simply reads
    // back empty.
    fcitx::Option<std::vector<DictionaryEntryConfig>> dictionaries;
};

// Everything the adapter pushes into a session, in a form that is cheap to
// compare. A save that changes none of it leaves running sessions untouched, so
// an association list is not discarded for nothing.
struct EngineSettings {
    int pageSize = kUnconfiguredPageSize;
    bool associations = true;
    // The learning bit the core is configured with. It is part of the compared
    // snapshot because a save that flips it must reach every idle session -- the
    // core drops its session-local recency and phrase state when it goes off.
    bool learning = true;
    uint32_t matching = 0;
    // The three mode/punctuation settings. They are compared like the rest, so a
    // save that moves one of them is a settings change; unlike the rest they are
    // not forwarded to the core (see applySettings).
    bool defaultEnglish = false;
    ShiftSwitch shiftSwitch = ShiftSwitch::Left;
    bool chinesePunctuation = true;
    bool operator==(const EngineSettings &other) const {
        return pageSize == other.pageSize && associations == other.associations &&
               learning == other.learning && matching == other.matching &&
               defaultEnglish == other.defaultEnglish && shiftSwitch == other.shiftSwitch &&
               chinesePunctuation == other.chinesePunctuation;
    }
    bool operator!=(const EngineSettings &other) const { return !(*this == other); }
};

inline EngineSettings settingsOf(const EngineConfig &config) {
    EngineSettings settings;
    settings.pageSize = config.pageSizeValue();
    settings.associations = *config.associations;
    settings.learning = *config.learning;
    settings.matching = config.matchingFlags();
    settings.defaultEnglish = *config.defaultEnglish;
    settings.shiftSwitch = *config.shiftSwitch;
    settings.chinesePunctuation = *config.chinesePunctuation;
    return settings;
}
} // namespace chengyin
