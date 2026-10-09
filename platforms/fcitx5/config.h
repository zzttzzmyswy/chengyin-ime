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
#include "chengyin_ime.h"

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

// `chengyin_session_configure` takes the learning bit alongside the width, but
// Linux has no persistent learning yet (that is the next batch), so this batch
// ships no learning switch. The bit is therefore always sent as the core's own
// default — `Session::learning_enabled`, which `Session::new` sets to true — so
// configuring a session never turns learning off as a side effect. When the
// learning switch arrives, this constant is what it replaces.
inline constexpr bool kDefaultLearning = true;

// Candidate widths a user may choose, matching the Windows settings page. This is
// an enum rather than an IntConstrain range on purpose: the config tool then
// offers exactly these three choices, and a hand-edited value outside the set
// keeps the option's default instead of being silently clamped to a width the
// user never picked.
enum class PageSize { Five = 0, Seven = 1, Nine = 2 };
FCITX_CONFIG_ENUM_NAME(PageSize, "5", "7", "9")

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
    explicit EngineConfig(std::string defaultDictionaryPath = CHENGYIN_DEFAULT_DICTIONARY_PATH)
        : dictionaryPath{this, "DictionaryPath",
              "词典 TSV 绝对路径（默认使用随包安装的完整词典；清空则使用内置演示词典）",
              std::move(defaultDictionaryPath)},
          pageSize{this, "PageSize", "每页候选数", PageSize::Five},
          associations{this, "Associations",
              "提交中文后显示联想词；Tab 或鼠标确认，空格、数字和 Enter 交给应用", true},
          fuzzy{this, "Fuzzy", "模糊音（双向匹配）", FuzzyConfig{}},
          correction{this, "Correction", "常见键盘失误", CorrectionConfig{}} {}
    const char *typeName() const override { return "ChengyinConfig"; }

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
    fcitx::Option<FuzzyConfig> fuzzy;
    fcitx::Option<CorrectionConfig> correction;
};

// Everything the adapter pushes into a session, in a form that is cheap to
// compare. A save that changes none of it leaves running sessions untouched, so
// an association list is not discarded for nothing.
struct EngineSettings {
    int pageSize = kUnconfiguredPageSize;
    bool associations = true;
    uint32_t matching = 0;
    bool operator==(const EngineSettings &other) const {
        return pageSize == other.pageSize && associations == other.associations &&
               matching == other.matching;
    }
    bool operator!=(const EngineSettings &other) const { return !(*this == other); }
};

inline EngineSettings settingsOf(const EngineConfig &config) {
    return EngineSettings{config.pageSizeValue(), *config.associations, config.matchingFlags()};
}
} // namespace chengyin
