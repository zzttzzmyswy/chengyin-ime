#pragma once
#include "myswy_ime.h"
#include <windows.h>
#include <msctf.h>
#include <string>
#include <vector>
namespace myswy {
std::wstring customDictionaryPath(bool create);
bool readDictionaryFile(const std::wstring &, std::vector<uint8_t> &);
MyswyDictionary *loadDictionaryBytes(const std::vector<uint8_t> &);
bool installCustomDictionary(const std::wstring &source, const std::wstring &target, bool append = false);
int runSettings(HINSTANCE, int, ITfMessagePump * = nullptr, ITfKeystrokeMgr * = nullptr, int = 0);
}
