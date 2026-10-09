// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcitx-config/configuration.h>
#include <fcitx-config/option.h>
#include <string>
#include <utility>

namespace chengyin {
// Distribution packages compile in the lexicon they install, so a profile with
// no saved conf/chengyin.conf starts on the full vocabulary rather than the
// 98-entry demo. Plain source builds leave this empty and keep the demo.
#ifndef CHENGYIN_DEFAULT_DICTIONARY_PATH
#define CHENGYIN_DEFAULT_DICTIONARY_PATH ""
#endif

class EngineConfig final : public fcitx::Configuration {
public:
    // The initial value doubles as the option's default, so a configuration file
    // that omits DictionaryPath resolves to it and never silently falls back to
    // another lexicon. Clearing the field explicitly selects the demo.
    explicit EngineConfig(std::string defaultDictionaryPath = CHENGYIN_DEFAULT_DICTIONARY_PATH)
        : dictionaryPath{this, "DictionaryPath",
              "词典 TSV 绝对路径（默认使用随包安装的完整词典；清空则使用内置演示词典）",
              std::move(defaultDictionaryPath)} {}
    const char *typeName() const override { return "ChengyinConfig"; }
    fcitx::Option<std::string> dictionaryPath;
};
} // namespace chengyin
