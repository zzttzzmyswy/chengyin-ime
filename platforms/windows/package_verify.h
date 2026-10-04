#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <set>
#include <string>
#include <filesystem>

// Manifest paths are deliberately flat and restricted. Verification never
// loads code or follows a manifest-controlled path outside the payload folder.
inline bool verifyPackage(const std::filesystem::path& folder,
                          const std::filesystem::path& manifest) {
    const std::set<std::string> required{
        "myswy_tsf.dll", "myswy_probe.exe", "myswy_settings.exe", "README.md",
        "LICENSE", "THIRD_PARTY.md", "RUNTIME_LICENSES.zip", "BUILD_INFO.json",
        "INSTALLER_LICENSE.txt", "CHANGELOG.md"
    };
    std::error_code error;
    const auto size = std::filesystem::file_size(manifest, error);
    if (error || size > 4096) return false;
    std::ifstream list(manifest, std::ios::binary);
    if (!list) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    std::set<std::string> seen;
    std::string line;
    bool valid = true;
    while (valid && std::getline(list, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 67 || line.size() > 128 || line.substr(64, 2) != "  ") { valid = false; break; }
        const auto name = line.substr(66);
        if (!required.count(name) || !seen.insert(name).second) { valid = false; break; }
        const auto path = folder / name;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            valid = false; break;
        }
        std::ifstream file(path, std::ios::binary);
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (!file || BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) { valid = false; break; }
        std::array<char, 65536> buffer{};
        while (file.read(buffer.data(), buffer.size()) || file.gcount()) {
            if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(file.gcount()), 0) < 0) {
                valid = false; break;
            }
        }
        if (!file.eof()) valid = false;
        std::array<UCHAR, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) valid = false;
        BCryptDestroyHash(hash);
        constexpr char hex[] = "0123456789ABCDEF";
        for (size_t i = 0; i < digest.size(); ++i) {
            if (line[2 * i] != hex[digest[i] >> 4] || line[2 * i + 1] != hex[digest[i] & 15]) valid = false;
        }
    }
    if (list.bad() || seen != required) valid = false;
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return valid;
}
