#pragma once
#include "preferences.h"
namespace chengyin {
// Candidate theme drawing, shared by the actual popup and settings preview.
int visualTheme(int requested);
int themeRadius(int theme, UINT dpi);
void drawThemeSurface(HDC, const RECT &, const Palette &, int theme, UINT dpi, int radius = -1);
void drawThemeSelection(HDC, const RECT &, const Palette &, int theme, UINT dpi, bool selected, bool hover);
void drawThemeBadge(HDC, const RECT &, const Palette &, int theme, UINT dpi, bool selected);
const Skin *activeSkin(const Preferences &);
int skinSelectionStyle(const Preferences &);
int skinRail(const Preferences &, UINT dpi);
void drawSkinSurface(HDC,const RECT &,const Palette &,const Preferences &,UINT dpi,int rail);
}
