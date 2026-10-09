// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "chengyin_ime.h"
#include <windows.h>
#include <msctf.h>
#include <string>
#include <vector>
namespace chengyin {
std::wstring customDictionaryPath(bool create);
bool readDictionaryFile(const std::wstring &, std::vector<uint8_t> &);
ChengyinDictionary *loadDictionaryBytes(const std::vector<uint8_t> &);
bool installCustomDictionary(const std::wstring &source, const std::wstring &target, bool append = false);
struct DictionaryEntry {
    std::wstring name;
    bool enabled = true;
    std::vector<uint8_t> binary;
};
bool readDictionaryLibrary(const std::wstring &, std::vector<DictionaryEntry> &);
bool addDictionaryLibrary(const std::wstring &source, const std::wstring &target);
bool changeDictionaryLibrary(const std::wstring &target, const DictionaryEntry &expected, bool remove);
ChengyinDictionary *loadEffectiveDictionary(const std::vector<uint8_t> &, ChengyinDictionary *base);
int runSettings(HINSTANCE, int, ITfMessagePump * = nullptr, ITfKeystrokeMgr * = nullptr, int = 0, bool = true);
}
