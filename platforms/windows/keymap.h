#pragma once
#include <cstdint>
#include "myswy_ime.h"

namespace myswy {
enum class Action { pass, core, finish, toggle, punctuation };
struct KeyPlan {
    Action action = Action::pass;
    uint32_t key = 0;
    // ASCII punctuation is inserted in the same TSF edit as its candidate.
    // This avoids an asynchronous commit racing the host's original key.
    char punctuation = 0;
};
// Track only real dispatched events, never toggle from OnTestKeyDown. A Shift
// chord or repeat cannot toggle when released. VK_SHIFT uses its physical scan.
class ShiftSwitch {
  public:
    static bool matches(uint32_t vk, uint64_t lparam, int setting) {
        if (!setting)
            return false;
        if (vk == 0xa0)
            return true;
        if (vk == 0xa1)
            return setting == 2;
        return vk == 0x10 && (setting == 2 || ((lparam >> 16) & 0xff) == 0x2a);
    }
    bool pending() const {
        return down_;
    }
    void reset() {
        down_ = false;
        used_ = false;
        side_ = 0;
    }
    void down(uint32_t vk, uint64_t lparam, int setting, bool shortcut) {
        if (matches(vk, lparam, setting)) {
            if (!down_) {
                down_ = true;
                side_ = vk == 0xa1 || (vk == 0x10 && ((lparam >> 16) & 0xff) == 0x36) ? 2 : 1;
                used_ = shortcut || (lparam & (1ULL << 30));
            } else {
                const int side = vk == 0xa1 || (vk == 0x10 && ((lparam >> 16) & 0xff) == 0x36) ? 2 : 1;
                used_ = used_ || side != side_ || shortcut || (lparam & (1ULL << 30));
            }
        } else if (down_)
            used_ = true;
    }
    bool up(uint32_t vk, uint64_t lparam, int setting, bool shortcut) {
        if (!matches(vk, lparam, setting))
            return false;
        const bool toggle = down_ && !used_ && !shortcut;
        reset();
        return toggle;
    }
  private:
    bool down_ = false, used_ = false;
    int side_ = 0;
};
// Pure planning: safe to call repeatedly from OnTestKeyDown. VK values are stable.
inline KeyPlan planKey(uint32_t vk, char ascii, bool shortcut, bool caps, bool active, bool english = false,
                       bool association = false, bool shifted = false) {
    if (vk == 0x10 || vk == 0x11 || vk == 0x12 || vk == 0x5b || vk == 0x5c ||
            (vk >= 0xa0 && vk <= 0xa5))
        return {};
    if (shortcut || caps || english || shifted || (ascii >= 'A' && ascii <= 'Z') || vk == 0x14)
        return {active ? Action::finish : Action::pass, 0, 0};
    if (ascii >= 'a' && ascii <= 'z')
        return {Action::core, static_cast<uint32_t>(ascii), 0};
    if (!active)
        return {};
    if (association) {
        switch (vk) {
        case 0x09:
            return {Action::core, MYSWY_KEY_TAB, 0};
        case 0x1b:
            return {Action::core, MYSWY_KEY_ESCAPE, 0};
        case 0x26:
            return {Action::core, MYSWY_KEY_UP, 0};
        case 0x28:
            return {Action::core, MYSWY_KEY_DOWN, 0};
        case 0x21:
            return {Action::core, MYSWY_KEY_PAGE_UP, 0};
        case 0x22:
            return {Action::core, MYSWY_KEY_PAGE_DOWN, 0};
        default:
            break;
        }
        if (ascii == '-')
            return {Action::core, MYSWY_KEY_PAGE_UP, 0};
        if (ascii == '=')
            return {Action::core, MYSWY_KEY_PAGE_DOWN, 0};
        return {Action::finish, 0, 0};
    }
    switch (vk) {
    case 0x20:
        return {Action::core, MYSWY_KEY_ENTER, 0};
    case 0x08:
        return {Action::core, MYSWY_KEY_BACKSPACE, 0};
    case 0x1b:
        return {Action::core, MYSWY_KEY_ESCAPE, 0};
    case 0x0d:
        return {Action::core, MYSWY_KEY_ENTER, 0};
    case 0x26:
        return {Action::core, MYSWY_KEY_UP, 0};
    case 0x28:
        return {Action::core, MYSWY_KEY_DOWN, 0};
    case 0x25:
        return {Action::core, MYSWY_KEY_LEFT, 0};
    case 0x27:
        return {Action::core, MYSWY_KEY_RIGHT, 0};
    case 0x24:
        return {Action::core, MYSWY_KEY_HOME, 0};
    case 0x23:
        return {Action::core, MYSWY_KEY_END, 0};
    case 0x2e:
        return {Action::core, MYSWY_KEY_DELETE, 0};
    case 0x21:
        return {Action::core, MYSWY_KEY_PAGE_UP, 0};
    case 0x22:
        return {Action::core, MYSWY_KEY_PAGE_DOWN, 0};
    default:
        break;
    }
    if (ascii == '-')
        return {Action::core, MYSWY_KEY_PAGE_UP, 0};
    if (ascii == '=')
        return {Action::core, MYSWY_KEY_PAGE_DOWN, 0};
    if ((ascii >= '1' && ascii <= '9') || ascii == '\'')
        return {Action::core, static_cast<uint32_t>(ascii), 0};
    if (ascii >= 0x21 && ascii <= 0x7e)
        return {Action::core, static_cast<uint32_t>(ascii), ascii};
    // Tab and unknown input finish the visible preedit first.
    return {Action::finish, 0, 0};
}
}
