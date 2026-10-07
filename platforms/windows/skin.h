#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
namespace chengyin {
struct SkinImage;
struct Skin {
    std::wstring name = L"自定义皮肤";
    COLORREF background = RGB(229,241,237), surface = RGB(248,252,248), text = RGB(28,61,54),
        muted = RGB(75,107,97), border = RGB(174,202,188), accent = RGB(36,112,91),
        selected = RGB(36,99,82), selectedText = RGB(255,255,249);
    int radius = 12, padding = 6, ornament = 1; // none / porcelain / navigation / sea
    std::vector<unsigned char> png;
    std::shared_ptr<SkinImage> image;
};
bool validSkin(const Skin &);
bool parseSkin(const std::vector<unsigned char> &, Skin &);
std::vector<unsigned char> encodeSkin(const Skin &);
std::shared_ptr<const Skin> loadSkin(const std::wstring &);
std::wstring skinName(const std::wstring &);
std::shared_ptr<const Skin> builtinSkin(int);
void drawSkinImage(HDC, const RECT &, const Skin &);
}
