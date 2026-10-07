#include "engine.h"
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>

class ChengyinFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        auto engine = std::make_unique<chengyin::Engine>(manager->instance()->inputContextManager(),
                                                     manager->instance()->eventLoop());
        engine->reloadConfig();
        return engine.release();
    }
};
FCITX_ADDON_FACTORY(ChengyinFactory)
