// SPDX-License-Identifier: GPL-3.0-or-later
// Punctuation-table parity test for the Fcitx 5 adapter (iteration I20).
//
// The Linux adapter renders Chinese punctuation from platforms/common/punctuation.h;
// the TSF adapter renders it from platforms/windows/punctuation.h. The two tables
// are written out separately -- the Windows header cannot be shared without
// touching a build this machine cannot compile -- so the only thing keeping them
// from drifting apart is this test: it includes BOTH headers and asserts that
// every supported character, in both quote states, renders to the same code point
// sequence.
//
// The Windows header is a pure header with no Windows dependency, and wchar_t is
// 32 bits on Linux, so every character in the table (all BMP) survives the
// comparison losslessly. That is what makes the assertion meaningful rather than
// an artefact of the platform it runs on.
#include <cstdio>
#include <string>
#include <vector>
#include "../common/punctuation.h"
// The Windows table, included under its own namespace-qualified name. Both files
// declare `chengyin::PunctuationState`, so the Windows one is pulled in through a
// renamed namespace to keep the two types distinct.
#define PunctuationState WindowsPunctuationState
#include "../windows/punctuation.h"
#undef PunctuationState

namespace {

int failures = 0;

void check(bool ok, const std::string &what, const std::string &detail = {}) {
    std::printf("%s %s", ok ? "ok   " : "FAIL ", what.c_str());
    if (!detail.empty()) { std::printf("  [%s]", detail.c_str()); }
    std::printf("\n");
    if (!ok) { ++failures; }
}

// A rendered result as a plain code-point list, so the two implementations can be
// compared without either one's output type leaking into the comparison.
std::vector<char32_t> renderLinux(const chengyin::PunctuationState &state, char c, bool chinese) {
    char32_t out[3] = {};
    const int length = state.render(c, chinese, out);
    return {out, out + length};
}

std::vector<char32_t> renderWindows(const chengyin::WindowsPunctuationState &state, char c, bool chinese) {
    wchar_t out[3] = {};
    const int length = state.render(c, chinese, out);
    std::vector<char32_t> result;
    for (int i = 0; i < length; ++i) { result.push_back(static_cast<char32_t>(out[i])); }
    return result;
}

std::string show(const std::vector<char32_t> &points) {
    std::string text;
    for (auto point : points) { text += "U+" + std::to_string(static_cast<uint32_t>(point)) + " "; }
    return text;
}

} // namespace

int main() {
    // The full 0..127 domain, so supported() is compared everywhere it can
    // answer, not only over the characters either table claims to convert.
    for (int code = 0; code < 128; ++code) {
        const auto c = static_cast<char>(code);
        const bool linuxSupported = chengyin::PunctuationState::supported(c);
        const bool windowsSupported = chengyin::WindowsPunctuationState::supported(c);
        if (linuxSupported != windowsSupported) {
            check(false, "supported() agrees for code " + std::to_string(code),
                  std::string("linux=") + (linuxSupported ? "true" : "false") + " windows=" +
                      (windowsSupported ? "true" : "false"));
        }
    }
    check(true, "supported() agrees across the whole ASCII domain (0..127)");

    // Both pair states, over every character, in Chinese and in English mode. The
    // pair state is driven through accepted() on both sides rather than by poking
    // the flags, so the walk exercises the same entry point the adapters use.
    for (int code = 0; code < 128; ++code) {
        const auto c = static_cast<char>(code);
        for (bool chinese : {false, true}) {
            for (int pairState = 0; pairState < 2; ++pairState) {
                chengyin::PunctuationState a;
                chengyin::WindowsPunctuationState b;
                if (pairState == 1) {
                    a.accepted('"', true);
                    a.accepted('\'', true);
                    b.accepted('"', true);
                    b.accepted('\'', true);
                }
                const auto fromLinux = renderLinux(a, c, chinese);
                const auto fromWindows = renderWindows(b, c, chinese);
                if (fromLinux != fromWindows) {
                    check(false,
                          std::string("code ") + std::to_string(code) + (chinese ? " (Chinese)" : " (English)") +
                              (pairState ? " paired" : " unpaired") + " renders identically",
                          "linux=[" + show(fromLinux) + "] windows=[" + show(fromWindows) + "]");
                }
            }
        }
    }
    check(true, "every ASCII code renders identically in both tables, Chinese and English, both pair states");

    // The named conversions the task card lists, asserted on the Linux table so a
    // silent change to the shared table is caught by name rather than only by the
    // comparison above.
    struct Expect { char c; const char *chinese; };
    const Expect expectations[] = {
        {',', "，"}, {'.', "。"}, {'!', "！"}, {'?', "？"}, {':', "："}, {';', "；"},
        {'(', "（"}, {')', "）"}, {'[', "【"}, {']', "】"}, {'<', "《"}, {'>', "》"},
        {'/', "、"}, {'\\', "、"}, {'"', "“"}, {'\'', "‘"}, {'_', "——"}, {'^', "……"},
        {'~', "～"}, {'$', "￥"},
    };
    for (const auto &expectation : expectations) {
        chengyin::PunctuationState state;
        const auto points = renderLinux(state, expectation.c, true);
        std::string utf8;
        for (auto point : points) {
            // The expected strings are UTF-8, so the comparison decodes the same way
            // the adapter encodes: through the code point.
            if (point < 0x80) {
                utf8 += static_cast<char>(point);
            } else if (point < 0x800) {
                utf8 += static_cast<char>(0xc0 | (point >> 6));
                utf8 += static_cast<char>(0x80 | (point & 0x3f));
            } else {
                utf8 += static_cast<char>(0xe0 | (point >> 12));
                utf8 += static_cast<char>(0x80 | ((point >> 6) & 0x3f));
                utf8 += static_cast<char>(0x80 | (point & 0x3f));
            }
        }
        check(utf8 == expectation.chinese,
              std::string("'") + expectation.c + "' becomes " + expectation.chinese,
              "got '" + utf8 + "'");
    }

    // The pairing walk: two double quotes alternate, two single quotes alternate,
    // and the two states are independent of each other.
    {
        chengyin::PunctuationState state;
        const auto first = renderLinux(state, '"', true);
        state.accepted('"', true);
        const auto second = renderLinux(state, '"', true);
        state.accepted('"', true);
        const auto third = renderLinux(state, '"', true);
        check(first == std::vector<char32_t>{U'“'} && second == std::vector<char32_t>{U'”'} &&
                  third == std::vector<char32_t>{U'“'},
              "double quotes alternate “ ” “");

        chengyin::PunctuationState singles;
        const auto singleFirst = renderLinux(singles, '\'', true);
        singles.accepted('\'', true);
        const auto singleSecond = renderLinux(singles, '\'', true);
        check(singleFirst == std::vector<char32_t>{U'‘'} && singleSecond == std::vector<char32_t>{U'’'},
              "single quotes alternate ‘ ’");

        // English mode never advances the pair, so an English `"` cannot desync one.
        chengyin::PunctuationState english;
        english.accepted('"', false);
        check(renderLinux(english, '"', true) == std::vector<char32_t>{U'“'},
              "an English-mode quote leaves the pair state alone");

        // reset() clears both.
        chengyin::PunctuationState cleared;
        cleared.accepted('"', true);
        cleared.accepted('\'', true);
        cleared.reset();
        check(renderLinux(cleared, '"', true) == std::vector<char32_t>{U'“'} &&
                  renderLinux(cleared, '\'', true) == std::vector<char32_t>{U'‘'},
              "reset() returns both pairs to their opening half");
    }

    std::printf("%s Fcitx5 punctuation-parity test: %d failed assertion(s)\n",
                failures == 0 ? "PASS " : "FAIL ", failures);
    return failures == 0 ? 0 : 1;
}
