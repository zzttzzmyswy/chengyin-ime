#pragma once
namespace myswy {
struct PunctuationState {
    bool single = false, quoted = false;
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
    int render(char c, bool chinese, wchar_t (&out)[3]) const {
        if (!chinese) {
            out[0] = static_cast<unsigned char>(c);
            return 1;
        }
        switch (c) {
        case ',':
            out[0] = L'，';
            break;
        case '.':
            out[0] = L'。';
            break;
        case '!':
            out[0] = L'！';
            break;
        case '?':
            out[0] = L'？';
            break;
        case ':':
            out[0] = L'：';
            break;
        case ';':
            out[0] = L'；';
            break;
        case '(':
            out[0] = L'（';
            break;
        case ')':
            out[0] = L'）';
            break;
        case '[':
            out[0] = L'【';
            break;
        case ']':
            out[0] = L'】';
            break;
        case '<':
            out[0] = L'《';
            break;
        case '>':
            out[0] = L'》';
            break;
        case '/':
        case '\\':
            out[0] = L'、';
            break;
        case '"':
            out[0] = quoted ? L'”' : L'“';
            break;
        case '\'':
            out[0] = single ? L'’' : L'‘';
            break;
        case '_':
            out[0] = out[1] = L'—';
            return 2;
        case '^':
            out[0] = out[1] = L'…';
            return 2;
        case '~':
            out[0] = L'～';
            break;
        case '$':
            out[0] = L'￥';
            break;
        default:
            out[0] = static_cast<unsigned char>(c);
            break;
        }
        return 1;
    }
    void accepted(char c, bool chinese) {
        if (chinese && c == '"')
            quoted = !quoted;
        if (chinese && c == '\'')
            single = !single;
    }
    void reset() {
        single = quoted = false;
    }
};
}
