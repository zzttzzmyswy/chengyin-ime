#pragma once
#include <fcitx-config/configuration.h>
#include <fcitx-config/option.h>

namespace myswy {
class EngineConfig final : public fcitx::Configuration {
public:
    const char *typeName() const override { return "MyswyConfig"; }
    fcitx::Option<std::string> dictionaryPath{this, "DictionaryPath",
        "词典 TSV 绝对路径（留空使用演示词典）", ""};
};
} // namespace myswy
