#include "theme_art.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
namespace myswy {
namespace {
using namespace Gdiplus;
struct Runtime {
    ULONG_PTR token = 0;
    Runtime() { GdiplusStartupInput input; GdiplusStartup(&token, &input, nullptr); }
    ~Runtime() { if (token) GdiplusShutdown(token); }
};
bool ensureRuntime() {
    static Runtime runtime;
    return runtime.token != 0;
}
void prepare(Graphics &g) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
}
Color color(COLORREF c, BYTE alpha = 255) { return Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c)); }
Color blend(COLORREF a, COLORREF b, float ratio) {
    return Color(255, static_cast<BYTE>(GetRValue(a) * (1 - ratio) + GetRValue(b) * ratio),
                 static_cast<BYTE>(GetGValue(a) * (1 - ratio) + GetGValue(b) * ratio),
                 static_cast<BYTE>(GetBValue(a) * (1 - ratio) + GetBValue(b) * ratio));
}
RectF bounds(const RECT &r) { return RectF(static_cast<REAL>(r.left), static_cast<REAL>(r.top),
    static_cast<REAL>(r.right - r.left), static_cast<REAL>(r.bottom - r.top)); }
void rounded(GraphicsPath &path, const RectF &r, REAL radius) {
    REAL d = std::max(1.0f, std::min(radius * 2, std::min(r.Width, r.Height)));
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}
}

int visualTheme(int requested) {
    if (requested < 0 || requested > 2) requested = 0;
    HIGHCONTRASTW contrast{sizeof(contrast),0,nullptr};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags & HCF_HIGHCONTRASTON) ? 0 : requested;
}
int themeRadius(int theme, UINT dpi) { return theme ? MulDiv(8, static_cast<int>(dpi),96) : 0; }
void drawThemeSurface(HDC dc, const RECT &r, const Palette &p, int theme, UINT dpi) {
    if (!theme) {
        HBRUSH fill = CreateSolidBrush(p.surface), border = CreateSolidBrush(p.border);
        FillRect(dc,&r,fill); FrameRect(dc,&r,border);
        DeleteObject(fill); DeleteObject(border); return;
    }
    // Start GDI+ before constructing Graphics (its constructor needs the runtime).
    if (!ensureRuntime()) { drawThemeSurface(dc,r,p,0,dpi); return; }
    Graphics g(dc); prepare(g);
    const REAL s = static_cast<REAL>(dpi) / 96;
    RectF box = bounds(r); box.Width -= 1; box.Height -= 1;
    SolidBrush base(color(p.surface)); g.FillRectangle(&base,bounds(r));
    GraphicsPath shape; rounded(shape,box,static_cast<REAL>(themeRadius(theme,dpi)));
    LinearGradientBrush gradient(PointF(0,box.Y),PointF(0,std::max(1.0f,box.GetBottom())),
                                 blend(p.surface,p.background,theme == 2 ? 0.1f : 0.05f),blend(p.surface,p.background,theme == 2 ? 0.6f : 0.8f));
    g.FillPath(&gradient,&shape);
    Pen border(color(p.border),std::max(1.0f,s)); g.DrawPath(&border,&shape);
}
void drawThemeSelection(HDC dc,const RECT &r,const Palette &p,int theme,UINT dpi,bool selected,bool hover) {
    if (!selected && !hover) return;
    if (!theme) {
        HBRUSH brush=CreateSolidBrush(selected ? p.selected : p.background);
        FillRect(dc,&r,brush); DeleteObject(brush); return;
    }
    if (!ensureRuntime()) { drawThemeSelection(dc,r,p,0,dpi,selected,hover); return; }
    Graphics g(dc); prepare(g); const REAL s=static_cast<REAL>(dpi)/96;
    RectF box=bounds(r); box.Inflate(-s,-s);
    GraphicsPath path; rounded(path,box,4*s);
    if (selected) {
        LinearGradientBrush brush(PointF(box.X,box.Y),PointF(box.GetRight(),box.GetBottom()),color(p.selected),color(p.selected));
        g.FillPath(&brush,&path);
        Pen outline(color(p.accent,90),s); g.DrawPath(&outline,&path);
        if (theme == 2) { Pen gold(Color(230,211,184,126),2*s); g.DrawLine(&gold,box.X+2*s,box.Y+4*s,box.X+2*s,box.GetBottom()-4*s); }
    } else { SolidBrush brush(color(p.accent,theme == 2 ? 35 : 16)); g.FillPath(&brush,&path); }
}
void drawThemeBadge(HDC dc,const RECT &r,const Palette &p,int theme,UINT dpi,bool selected) {
    if (!theme) return;
    if (!ensureRuntime()) return;
    Graphics g(dc); prepare(g); const REAL s=static_cast<REAL>(dpi)/96;
    const REAL size=std::min(14*s,static_cast<REAL>(std::min(r.right-r.left,r.bottom-r.top))-2*s);
    if (size <= 0) return;
    RectF badge(static_cast<REAL>(r.left),(r.top+r.bottom-size)/2,size,size);
    GraphicsPath path; rounded(path,badge,7*s);
    SolidBrush fill(color(selected ? p.selectedText : p.accent,selected ? 23 : 12)); g.FillPath(&fill,&path);
}
}
