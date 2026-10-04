#pragma once
#include "preferences.h"
namespace myswy {
// Candidate theme drawing, shared by the actual popup and settings preview.
int visualTheme(int requested);
int themeRadius(int theme, UINT dpi);
void drawThemeSurface(HDC, const RECT &, const Palette &, int theme, UINT dpi);
void drawThemeSelection(HDC, const RECT &, const Palette &, int theme, UINT dpi, bool selected, bool hover);
void drawThemeBadge(HDC, const RECT &, const Palette &, int theme, UINT dpi, bool selected);
}
