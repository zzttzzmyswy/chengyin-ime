#pragma once
#include "preferences.h"
namespace myswy {
// Original code-drawn artwork, shared by the actual popup and settings preview.
int visualTheme(int requested);
int themeRadius(int theme, UINT dpi);
int themeBannerHeight(int theme, UINT dpi, bool compact);
void drawThemeSurface(HDC, const RECT &, const Palette &, int theme, UINT dpi);
void drawThemeBanner(HDC, const RECT &, const Palette &, int theme, UINT dpi, HFONT);
void drawThemeSelection(HDC, const RECT &, const Palette &, int theme, UINT dpi, bool selected, bool hover);
void drawThemeBadge(HDC, const RECT &, const Palette &, int theme, UINT dpi, bool selected);
}
