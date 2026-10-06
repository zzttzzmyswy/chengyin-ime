#pragma once
#include "version_generated.h"
#include <string>
#include <vector>
#include <cstdint>
namespace myswy {
inline constexpr wchar_t kRepository[] = L"https://github.com/zzttzzmyswy/myswyIm";
inline constexpr wchar_t kReleases[] = L"https://github.com/zzttzzmyswy/myswyIm/releases";
struct ReleaseUpdate {
    bool available = false;
    std::wstring version, notes, url, digest, message;
};
bool parseReleases(const std::vector<uint8_t> &, ReleaseUpdate &);
bool verifyReleaseImage(const ReleaseUpdate &, const std::vector<uint8_t> &);
ReleaseUpdate checkReleaseUpdate();
bool downloadReleaseUpdate(const ReleaseUpdate &, const std::wstring &path);
}
