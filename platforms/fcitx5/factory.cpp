// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine.h"
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <fcitx/userinterfacemanager.h>

class ChengyinFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        auto *instance = manager->instance();
        // The user interface manager is handed over because the 中/英 and 中/英标点
        // status-area actions have to be registered with a name the whole framework
        // can look up; the engine cannot reach it from the input context manager
        // alone.
        auto engine = std::make_unique<chengyin::Engine>(instance->inputContextManager(),
                                                         instance->eventLoop(),
                                                         CHENGYIN_DEFAULT_DICTIONARY_PATH,
                                                         chengyin::Engine::DefaultProfilePath(),
                                                         chengyin::LearningRetryPolicy{},
                                                         &instance->userInterfaceManager());
        engine->reloadConfig();
        return engine.release();
    }
};
FCITX_ADDON_FACTORY(ChengyinFactory)
