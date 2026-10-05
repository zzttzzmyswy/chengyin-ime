#include "update.h"
#include "preferences.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <map>
#include <memory>
#include <tuple>
namespace myswy {
namespace {
struct Json {
    wchar_t kind = 0;
    std::wstring text;
    std::map<std::wstring, Json> object;
    std::vector<Json> array;
    const Json &get(const wchar_t *key) const {
        static const Json empty;
        auto at = object.find(key);
        return at == object.end() ? empty : at->second;
    }
};
class Parser {
    const std::wstring &s;
    size_t at = 0;
    void space() { while (at < s.size() && (s[at] == L' ' || s[at] == L'\t' || s[at] == L'\r' || s[at] == L'\n')) ++at; }
    bool take(wchar_t c) { space(); if (at < s.size() && s[at] == c) { ++at; return true; } return false; }
    bool string(std::wstring &out) {
        if (!take(L'"')) return false;
        while (at < s.size()) {
            wchar_t c = s[at++];
            if (c == L'"') return true;
            if (c < 32) return false;
            if (c == L'\\') {
                if (at == s.size()) return false;
                c = s[at++];
                switch (c) {
                case L'"': case L'\\': case L'/': break;
                case L'b': c = L'\b'; break; case L'f': c = L'\f'; break;
                case L'n': c = L'\n'; break; case L'r': c = L'\r'; break; case L't': c = L'\t'; break;
                case L'u': {
                    if (s.size() - at < 4) return false;
                    unsigned value = 0;
                    for (int i = 0; i < 4; ++i) {
                        wchar_t h = s[at++];
                        int digit = h >= L'0' && h <= L'9' ? h - L'0' : h >= L'a' && h <= L'f' ? h - L'a' + 10 : h >= L'A' && h <= L'F' ? h - L'A' + 10 : -1;
                        if (digit < 0) return false;
                        value = value * 16 + digit;
                    }
                    c = static_cast<wchar_t>(value); break;
                }
                default: return false;
                }
            }
            out.push_back(c);
        }
        return false;
    }
    bool value(Json &out, int depth) {
        if (depth > 32) return false;
        space(); if (at == s.size()) return false;
        out.kind = s[at];
        if (s[at] == L'"') return string(out.text);
        if (take(L'{')) {
            if (take(L'}')) return true;
            do {
                std::wstring key; Json child;
                if (!string(key) || !take(L':') || !value(child, depth + 1) || !out.object.emplace(key, std::move(child)).second) return false;
            } while (take(L','));
            return take(L'}');
        }
        if (take(L'[')) {
            if (take(L']')) return true;
            do { Json child; if (!value(child, depth + 1)) return false; out.array.push_back(std::move(child)); } while (take(L','));
            return take(L']');
        }
        for (const auto *literal : {L"true", L"false", L"null"}) {
            size_t size = std::wcslen(literal);
            if (s.compare(at, size, literal) == 0) { out.text = literal; at += size; return true; }
        }
        const size_t start = at;
        if (s[at] == L'-') ++at;
        if (at == s.size() || s[at] < L'0' || s[at] > L'9') return false;
        if (s[at] == L'0') ++at;
        else while (at < s.size() && s[at] >= L'0' && s[at] <= L'9') ++at;
        if (at < s.size() && s[at] == L'.') {
            size_t begin = ++at;
            while (at < s.size() && s[at] >= L'0' && s[at] <= L'9') ++at;
            if (at == begin) return false;
        }
        if (at < s.size() && (s[at] == L'e' || s[at] == L'E')) {
            ++at; if (at < s.size() && (s[at] == L'+' || s[at] == L'-')) ++at;
            size_t begin = at;
            while (at < s.size() && s[at] >= L'0' && s[at] <= L'9') ++at;
            if (at == begin) return false;
        }
        out.text = s.substr(start, at - start); return true;
    }
  public:
    explicit Parser(const std::wstring &input): s(input) {}
    bool parse(Json &out) { if (!value(out, 0)) return false; space(); return at == s.size(); }
};
using Version = std::array<int,4>;
bool version(std::wstring text, Version &out) {
    if (!text.empty() && text[0] == L'v') text.erase(0, 1);
    out = {0,0,0,1000000};
    size_t at = 0;
    auto number = [&](int &n) {
        n = 0; const auto begin = at;
        while (at < text.size() && text[at] >= L'0' && text[at] <= L'9') {
            n = n * 10 + (text[at++] - L'0'); if (n > 999999) return false;
        }
        return at > begin;
    };
    for (int i = 0; i < 3; ++i) {
        if (!number(out[i])) return false;
        if (i < 2 && (at == text.size() || text[at++] != L'.')) return false;
    }
    if (at == text.size()) return true;
    if (text.compare(at, 8, L"-preview") != 0) return false;
    at += 8;
    return number(out[3]) && at == text.size();
}
struct Internet {
    HINTERNET h = nullptr;
    ~Internet() { if (h) WinHttpCloseHandle(h); }
};
bool fetch(const std::wstring &url, std::vector<uint8_t> &out, size_t maximum) {
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return false;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet session{WinHttpOpen(L"Chengyin/0.1.0-preview20", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0)};
    if (!session.h) return false;
    WinHttpSetTimeouts(session.h, 5000, 5000, 10000, 10000);
    Internet connection{WinHttpConnect(session.h, host.c_str(), parts.nPort, 0)};
    Internet request{connection.h ? WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) : nullptr};
    if (!request.h) return false;
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    if (!WinHttpSendRequest(request.h, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1), nullptr, 0, 0, 0)
        || !WinHttpReceiveResponse(request.h, nullptr)) return false;
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr) || status != 200) return false;
    out.clear();
    std::array<uint8_t,65536> chunk{};
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        DWORD read = 0;
        if (GetTickCount64() - start > 60000 || !WinHttpReadData(request.h, chunk.data(), static_cast<DWORD>(chunk.size()), &read)) return false;
        if (!read) return true;
        if (read > maximum - out.size()) return false;
        out.insert(out.end(), chunk.begin(), chunk.begin() + read);
    }
}
bool validAsset(const ReleaseUpdate &release) {
    const std::wstring prefix = L"https://github.com/zzttzzmyswy/myswyIm/releases/download/";
    Version parsed{};
    if (!version(release.version, parsed)) return false;
    const auto tag = !release.version.empty() && release.version[0] == L'v'
        ? release.version.substr(1) : release.version;
    const auto filename = L"chengyin-windows-x64-" + tag + L"-msvc.exe";
    if (release.url != prefix + release.version + L"/" + filename) return false;
    if (release.digest.size() != 71 || release.digest.substr(0, 7) != L"sha256:") return false;
    return std::all_of(release.digest.begin() + 7, release.digest.end(), [](wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); });
}
}
bool parseReleases(const std::vector<uint8_t> &bytes, ReleaseUpdate &out) {
    out = {};
    if (bytes.empty() || bytes.size() > 2 * 1024 * 1024) return false;
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()), nullptr, 0);
    if (!size) return false;
    std::wstring text(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()), text.data(), size);
    Json root;
    if (!Parser(text).parse(root) || root.kind != L'[') return false;
    Version current{}, newest{}; version(kVersion, current); newest = current;
    for (const auto &release : root.array) {
        Version next{};
        if (release.kind != L'{' || release.get(L"draft").kind != L'f'
            || release.get(L"draft").text != L"false" || release.get(L"tag_name").kind != L'"'
            || release.get(L"assets").kind != L'[' || !version(release.get(L"tag_name").text, next) || next <= newest) continue;
        for (const auto &asset : release.get(L"assets").array) {
            const auto &name = asset.get(L"name").text;
            const auto &releaseTag = release.get(L"tag_name").text;
            const auto tag = releaseTag[0] == L'v' ? releaseTag.substr(1) : releaseTag;
            if (asset.kind != L'{' || asset.get(L"name").kind != L'"'
                || asset.get(L"browser_download_url").kind != L'"' || asset.get(L"digest").kind != L'"'
                || name != L"chengyin-windows-x64-" + tag + L"-msvc.exe") continue;
            ReleaseUpdate candidate;
            candidate.available = true;
            candidate.version = release.get(L"tag_name").text;
            candidate.notes = release.get(L"body").text;
            candidate.url = asset.get(L"browser_download_url").text;
            candidate.digest = asset.get(L"digest").text;
            if (!validAsset(candidate)) continue;
            newest = next; out = std::move(candidate);
        }
    }
    out.message = out.available ? L"发现新版本 " + out.version + L"。点击“下载并更新”安装。" : L"没有比当前版本更新且可校验的 Windows x64 安装包。";
    return true;
}
ReleaseUpdate checkReleaseUpdate() {
    ReleaseUpdate result; std::vector<uint8_t> bytes;
    if (!fetch(L"https://api.github.com/repos/zzttzzmyswy/myswyIm/releases?per_page=20", bytes, 2 * 1024 * 1024) || !parseReleases(bytes, result))
        result.message = L"无法检查更新，请检查网络或打开发行页面。离线输入仍可使用。";
    return result;
}
bool verifyReleaseImage(const ReleaseUpdate &release, const std::vector<uint8_t> &bytes) {
    if (!release.available || !validAsset(release) || bytes.size() < 64 || bytes.size() > 32 * 1024 * 1024 || bytes[0] != 'M' || bytes[1] != 'Z') return false;
    const size_t offset = static_cast<size_t>(bytes[60]) | (static_cast<size_t>(bytes[61]) << 8)
        | (static_cast<size_t>(bytes[62]) << 16) | (static_cast<size_t>(bytes[63]) << 24);
    if (offset > bytes.size() - 26 || bytes[offset] != 'P' || bytes[offset + 1] != 'E'
        || bytes[offset + 2] || bytes[offset + 3] || bytes[offset + 4] != 0x64 || bytes[offset + 5] != 0x86
        || bytes[offset + 24] != 0x0b || bytes[offset + 25] != 0x02 || (bytes[offset + 23] & 0x20)) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    std::array<UCHAR,32> digest{};
    bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0
        // BCrypt's legacy signature is mutable; pbInput is an input-only buffer.
        && BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) >= 0
        && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    constexpr wchar_t hex[] = L"0123456789abcdef";
    for (size_t i = 0; i < digest.size(); ++i)
        if (release.digest[7 + 2 * i] != hex[digest[i] >> 4] || release.digest[8 + 2 * i] != hex[digest[i] & 15]) ok = false;
    return ok;
}
bool downloadReleaseUpdate(const ReleaseUpdate &release, const std::wstring &path) {
    std::vector<uint8_t> bytes;
    return release.available && validAsset(release) && fetch(release.url, bytes, 32 * 1024 * 1024)
        && verifyReleaseImage(release, bytes) && atomicWrite(path, bytes);
}
}
