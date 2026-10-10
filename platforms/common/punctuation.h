// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace chengyin {
// The ASCII punctuation an IME maps to its Chinese counterparts, plus the paired
// quote state that decides which half of a pair comes out next.
//
// platforms/windows/punctuation.h is the same table expressed in wchar_t for the
// TSF adapter. Sharing the header is not possible without touching the Windows
// build, so the two are pinned together by the fcitx5-punctuation-parity test:
// it compiles both and asserts every supported character and both quote states
// render to the same code points. That test is what keeps the tables from
// drifting; this file deliberately has no dependency on the Windows one.
//
// The output is char32_t rather than wchar_t so the result is the same width on
// every platform this build targets.
struct PunctuationState {
    bool single = false, quoted = false;

    // Whether this ASCII character has a Chinese counterpart at all. Characters
    // outside the set are left to the application untouched.
    static bool supported(char c) {
        switch (c) {
        case ',':
        case '.':
        case '!':
        case '?':
        case ':':
        case ';':
        case '(':
        case ')':
        case '[':
        case ']':
        case '<':
        case '>':
        case '\\':
        case '/':
        case '"':
        case '\'':
        case '_':
        case '^':
        case '~':
        case '$':
            return true;
        default:
            return false;
        }
    }

    // Renders `c` into `out` and returns how many code points were written (1,
    // or 2 for the two-character `——` and `……`). `out` must hold three entries.
    // With `chinese` false the ASCII character itself comes back, which is what
    // makes this usable for both modes.
    int render(char c, bool chinese, char32_t (&out)[3]) const {
        if (!chinese) {
            out[0] = static_cast<unsigned char>(c);
            return 1;
        }
        switch (c) {
        case ',':
            out[0] = U'，';
            break;
        case '.':
            out[0] = U'。';
            break;
        case '!':
            out[0] = U'！';
            break;
        case '?':
            out[0] = U'？';
            break;
        case ':':
            out[0] = U'：';
            break;
        case ';':
            out[0] = U'；';
            break;
        case '(':
            out[0] = U'（';
            break;
        case ')':
            out[0] = U'）';
            break;
        case '[':
            out[0] = U'【';
            break;
        case ']':
            out[0] = U'】';
            break;
        case '<':
            out[0] = U'《';
            break;
        case '>':
            out[0] = U'》';
            break;
        case '/':
        case '\\':
            out[0] = U'、';
            break;
        case '"':
            out[0] = quoted ? U'”' : U'“';
            break;
        case '\'':
            out[0] = single ? U'’' : U'‘';
            break;
        case '_':
            out[0] = out[1] = U'—';
            return 2;
        case '^':
            out[0] = out[1] = U'…';
            return 2;
        case '~':
            out[0] = U'～';
            break;
        case '$':
            out[0] = U'￥';
            break;
        default:
            out[0] = static_cast<unsigned char>(c);
            break;
        }
        return 1;
    }

    // Advance the pair state. Only a Chinese-mode quote changes it, so an
    // English-mode `"` cannot desynchronise a pair the user is halfway through.
    void accepted(char c, bool chinese) {
        if (chinese && c == '"') { quoted = !quoted; }
        if (chinese && c == '\'') { single = !single; }
    }

    void reset() { single = quoted = false; }
};
} // namespace chengyin
