#include "update.h"
#include "preferences.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
void require(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "update FAIL: %s\n", why); std::exit(1); }
}
bool parse(const char *json, myswy::ReleaseUpdate &result) {
    return myswy::parseReleases(std::vector<uint8_t>(json, json + std::strlen(json)), result);
}
}
int main(int argc, char **argv) {
    using namespace myswy;
    if (argc == 2 && std::strcmp(argv[1], "--live-check") == 0) {
        const auto live = checkReleaseUpdate();
        std::wprintf(L"%ls\n", live.message.c_str());
        return live.message.find(L"无法") == std::wstring::npos ? 0 : 1;
    }
    ReleaseUpdate result;
    require(parse("[]", result) && !result.available, "no published releases is a normal result");
    const char fixture[] = R"([{"draft":false,"tag_name":"v0.1.0-preview14","body":"New\n\u4e2d\u6587","assets":[{"name":"chengyin-windows-x64-0.1.0-preview14-msvc.exe","browser_download_url":"https://github.com/zzttzzmyswy/myswyIm/releases/download/v0.1.0-preview14/chengyin-windows-x64-0.1.0-preview14-msvc.exe","digest":"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}]}])";
    require(parse(fixture, result) && result.available && result.version == L"v0.1.0-preview14"
            && result.notes == L"New\n中文", "newer release with verified repository asset and Unicode notes");
    ReleaseUpdate imageRelease = result;
    imageRelease.digest = L"sha256:228f47e1d41177fb11a95b237d5738131b6e44a586f217e8438b1a0b5fae3ae4";
    std::vector<uint8_t> image(256);
    image[0] = 'M'; image[1] = 'Z'; image[60] = 64;
    image[64] = 'P'; image[65] = 'E'; image[68] = 0x64; image[69] = 0x86;
    image[88] = 0x0b; image[89] = 0x02;
    require(verifyReleaseImage(imageRelease, image), "x64 EXE and SHA256 verified before installation");
    image[255] ^= 1;
    require(!verifyReleaseImage(imageRelease, image), "modified download rejected by digest");
    image[255] ^= 1; image[69] = 0;
    require(!verifyReleaseImage(imageRelease, image), "wrong PE architecture rejected");
    image[69] = 0x86; image[87] = 0x20;
    require(!verifyReleaseImage(imageRelease, image), "DLL cannot be offered as installer");
    image[60] = 255;
    require(!verifyReleaseImage(imageRelease, image), "out-of-bounds PE header rejected");
    std::string unsafe = fixture;
    auto at = unsafe.find("https://github.com/zzttzzmyswy/");
    unsafe.replace(at, std::strlen("https://github.com/zzttzzmyswy/"), "https://example.com/zzttzzmyswy/");
    require(parse(unsafe.c_str(), result) && !result.available, "external executable URL cannot become an update");
    unsafe = fixture; at = unsafe.find("sha256:"); unsafe.replace(at, 7, "sha512:");
    require(parse(unsafe.c_str(), result) && !result.available, "missing or wrong digest algorithm rejected");
    unsafe = fixture; at = unsafe.find("preview14\""); unsafe.replace(at, 10, "preview7\"");
    require(parse(unsafe.c_str(), result) && !result.available, "old version cannot downgrade installation");
    unsafe = fixture; at = unsafe.find("false"); unsafe.replace(at, 5, "true");
    require(parse(unsafe.c_str(), result) && !result.available, "draft release never offered");
    require(!parse("[{\"draft\":false,\"draft\":true}]", result), "ambiguous duplicate JSON keys rejected");
    require(!parse("[{\"body\":\"\\uXX00\"}]", result), "invalid JSON escape rejected");
    require(!parse("[] trailing", result), "trailing JSON input rejected");
    require(!parse("[{\"body\":", result), "truncated network response rejected");
    const COLORREF accent = RGB(0,100,200), selectedText = RGB(255,255,255);
    const auto light = themePalette(0, false, accent, selectedText), dark = themePalette(0, true, accent, selectedText);
    require(light.surface != dark.surface && light.text != dark.text, "system light/dark uses different readable palettes");
    require(themePalette(1, true, accent, selectedText).surface == light.surface
            && themePalette(2, false, accent, selectedText).surface == dark.surface, "explicit white/black remain fixed");
    for (int theme = 0; theme < 3; ++theme) {
        Preferences prefs; prefs.theme = theme;
        require(validPreferences(prefs), "all three themes have valid persisted identifiers");
        const auto colors = themePalette(theme, false, accent, selectedText);
        require(colors.surface != colors.text && colors.selected != colors.selectedText, "theme text contrasts with its surface");
    }
    for (int theme : {3,4,5,6}) {
        Preferences prefs; prefs.theme = theme;
        require(!validPreferences(prefs), "retired and unknown themes cannot be saved");
        require(themePalette(theme,true,accent,selectedText).surface == dark.surface,
                "unavailable theme uses system palette without retained character colors");
    }
    // Override HKCU in this process only; never change the user's real theme.
    const std::wstring keyName = L"Software\\MyswyIME\\TestTheme-" + std::to_wstring(GetCurrentProcessId());
    HKEY isolated = nullptr, personalize = nullptr;
    require(RegCreateKeyExW(HKEY_CURRENT_USER, keyName.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &isolated, nullptr) == ERROR_SUCCESS,
            "create process-private registry fixture");
    require(RegOverridePredefKey(HKEY_CURRENT_USER, isolated) == ERROR_SUCCESS, "override fixture HKCU");
    require(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, nullptr,
            0, KEY_ALL_ACCESS, nullptr, &personalize, nullptr) == ERROR_SUCCESS, "fixture personalization");
    for (DWORD value : {1u, 0u, 1u}) {
        require(RegSetValueExW(personalize, L"AppsUseLightTheme", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value)) == ERROR_SUCCESS,
                "set fixture theme");
        require(systemDarkTheme() == (value == 0), "system theme re-read after light/dark/light changes");
    }
    RegCloseKey(personalize);
    require(RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) == ERROR_SUCCESS, "restore HKCU");
    RegCloseKey(isolated);
    require(RegDeleteTreeW(HKEY_CURRENT_USER, keyName.c_str()) == ERROR_SUCCESS, "remove registry fixture");
    std::puts("PASS: release version/channel parsing, Unicode notes, repository/digest restrictions, malformed responses and three theme palettes; offline fixtures only.");
}
