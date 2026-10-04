#include "theme_art.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
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
void ellipse(Graphics &g, Color c, REAL x, REAL y, REAL w, REAL h) {
    SolidBrush brush(c); g.FillEllipse(&brush, x, y, w, h);
}
void polygon(Graphics &g, Color c, std::initializer_list<PointF> points) {
    SolidBrush brush(c); g.FillPolygon(&brush, points.begin(), static_cast<INT>(points.size()));
}
void music(Graphics &g, Color c, REAL x, REAL y, REAL s) {
    Pen pen(c, std::max(1.0f, s * 1.4f));
    g.DrawLine(&pen, x + 5*s, y + 11*s, x + 5*s, y);
    g.DrawLine(&pen, x + 5*s, y, x + 11*s, y - 2*s);
    g.DrawLine(&pen, x + 11*s, y - 2*s, x + 11*s, y + 9*s);
    ellipse(g,c,x,y + 9*s,5*s,3*s); ellipse(g,c,x + 6*s,y + 7*s,5*s,3*s);
}
void whale(Graphics &g) {
    // A plump whale with a light belly, smiling eye, tail and water spout.
    polygon(g,Color(255,52,112,226),{{28,11},{36,5},{35,13},{39,15},{29,18}});
    ellipse(g,Color(255,55,115,235),2,6,30,19);
    ellipse(g,Color(255,208,233,255),5,17,22,7);
    polygon(g,Color(255,35,87,187),{{15,16},{22,15},{21,22}});
    ellipse(g,Color(255,20,49,100),9,13,2.7f,3);
    ellipse(g,Color(255,255,255,255),9.7f,13.4f,0.9f,0.9f);
    Pen smile(Color(255,28,64,127),1.1f); g.DrawArc(&smile,4,13,6,6,15,105);
    Pen water(Color(255,89,176,240),1.5f);
    g.DrawBezier(&water,16,7,16,2,12,0,10,3); g.DrawBezier(&water,16,7,17,1,22,0,23,3);
    ellipse(g,Color(255,115,203,255),27,1,2,3);
}
void singer(Graphics &g, bool miku) {
    const Color hair = miku ? Color(255,24,174,164) : Color(255,155,166,189);
    const Color darkHair = miku ? Color(255,12,121,117) : Color(255,104,122,155);
    const Color accent = miku ? Color(255,245,99,155) : Color(255,51,178,177);
    if (miku) {
        polygon(g,hair,{{7,5},{3,4},{1,25},{7,22},{10,6}});
        polygon(g,hair,{{26,5},{30,4},{33,25},{27,22},{23,6}});
        polygon(g,darkHair,{{2,16},{1,25},{6,22},{6,11}});
        polygon(g,darkHair,{{31,16},{33,25},{28,22},{28,11}});
    } else {
        ellipse(g,darkHair,5,4,25,22);
        polygon(g,hair,{{7,8},{4,21},{10,26},{11,13}});
        polygon(g,hair,{{26,8},{30,21},{23,26},{22,13}});
    }
    ellipse(g,hair,8,2,19,20);
    ellipse(g,Color(255,255,232,216),10,7,15,14);
    polygon(g,hair,{{9,6},{25,5},{24,12},{21,9},{19,12},{16,8},{13,12},{10,10}});
    ellipse(g,miku ? Color(255,16,107,119) : Color(255,35,140,105),13,13,2.1f,3);
    ellipse(g,miku ? Color(255,16,107,119) : Color(255,35,140,105),20,13,2.1f,3);
    ellipse(g,Color(255,255,255,255),13.3f,13,0.8f,0.9f); ellipse(g,Color(255,255,255,255),20.3f,13,0.8f,0.9f);
    ellipse(g,Color(110,241,143,145),11,16,3,1.6f); ellipse(g,Color(110,241,143,145),21,16,3,1.6f);
    Pen mouth(Color(255,175,115,106),0.8f); g.DrawArc(&mouth,16,16,3,3,10,160);
    polygon(g,miku ? Color(255,49,63,72) : Color(255,231,243,255),{{12,21},{22,21},{27,28},{7,28}});
    polygon(g,accent,{{16,21},{18,21},{19,28},{15,28}});
    if (miku) {
        SolidBrush head(Color(255,49,54,64)); g.FillRectangle(&head,7,9,3,8); g.FillRectangle(&head,25,9,3,8);
        SolidBrush pink(accent); g.FillRectangle(&pink,7.0f,10.0f,1.2f,5.0f); g.FillRectangle(&pink,26.8f,10.0f,1.2f,5.0f);
    } else {
        polygon(g,accent,{{26,3},{30,7},{26,11},{22,7}});
        ellipse(g,Color(255,249,248,214),25,6,2,2);
    }
}
}
int visualTheme(int requested) {
    HIGHCONTRASTW contrast{sizeof(contrast),0,nullptr};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags & HCF_HIGHCONTRASTON) ? 0 : requested;
}
int themeRadius(int theme, UINT dpi) { return theme ? MulDiv(theme >= 3 ? 12 : 8, static_cast<int>(dpi),96) : 0; }
int themeBannerHeight(int theme, UINT dpi, bool compact) { return theme >= 3 ? MulDiv(compact ? 30 : 36,static_cast<int>(dpi),96) : 0; }
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
    auto clip = g.Save(); g.SetClip(&shape,CombineModeIntersect);
    if (theme == 3) {
        GraphicsPath wave; wave.StartFigure();
        wave.AddBezier(0.0f,box.GetBottom()-12*s,box.Width*.35f,box.GetBottom()-34*s,box.Width*.68f,box.GetBottom()+8*s,box.Width,box.GetBottom()-15*s);
        wave.AddLine(box.Width,box.GetBottom()-15*s,box.Width,box.GetBottom()); wave.AddLine(box.Width,box.GetBottom(),0.0f,box.GetBottom()); wave.CloseFigure();
        SolidBrush water(color(p.accent,19)); g.FillPath(&water,&wave);
        Pen bubble(color(p.accent,45),s); g.DrawEllipse(&bubble,box.GetRight()-18*s,box.GetBottom()-40*s,7*s,7*s);
        g.DrawEllipse(&bubble,box.GetRight()-28*s,box.GetBottom()-25*s,4*s,4*s);
    } else if (theme == 4) {
        Pen circuit(color(p.accent,26),s);
        g.DrawLine(&circuit,box.GetRight()-35*s,box.GetBottom()-5*s,box.GetRight()-35*s,box.GetBottom()-16*s);
        g.DrawLine(&circuit,box.GetRight()-35*s,box.GetBottom()-16*s,box.GetRight()-8*s,box.GetBottom()-16*s);
        music(g,Color(42,228,91,148),box.GetRight()-25*s,box.GetBottom()-37*s,s);
        for (int i=0;i<4;++i) ellipse(g,color(p.accent,36),box.GetRight()-(9+i*6)*s,box.GetBottom()-6*s,2*s,2*s);
    } else if (theme == 5) {
        Pen cloud(color(p.accent,40),s);
        g.DrawArc(&cloud,box.GetRight()-50*s,box.GetBottom()-20*s,25*s,20*s,175,190);
        g.DrawArc(&cloud,box.GetRight()-32*s,box.GetBottom()-27*s,28*s,27*s,180,180);
        GraphicsPath ribbon; ribbon.AddBezier(5*s,box.GetBottom()-3*s,box.Width*.3f,box.GetBottom()-20*s,box.Width*.6f,box.GetBottom()+8*s,box.Width,box.GetBottom()-8*s);
        Pen line(Color(48,60,174,165),s*1.5f); g.DrawPath(&line,&ribbon);
    }
    g.Restore(clip);
    Pen border(color(p.border,theme >= 3 ? 185 : 255),std::max(1.0f,s)); g.DrawPath(&border,&shape);
    if (theme == 4) {
        Pen pink(Color(220,230,95,145),s*2); g.DrawLine(&pink,14*s,1*s,std::min(box.Width-14*s,32*s),1*s);
    }
}
void drawThemeBanner(HDC dc, const RECT &r, const Palette &p, int theme, UINT dpi, HFONT font) {
    if (theme < 3 || r.bottom <= r.top) return;
    if (!ensureRuntime()) return;
    const REAL s = static_cast<REAL>(dpi)/96;
    const REAL iconScale = std::min((r.bottom-r.top)/(theme == 3 ? 27.0f : 30.0f),s*1.15f);
    const bool title = r.right-r.left >= MulDiv(136,static_cast<int>(dpi),96);
    const REAL iconWidth = (theme == 3 ? 40 : 34)*iconScale;
    const REAL x = title ? static_cast<REAL>(r.left)+4*s : r.left+(r.right-r.left-iconWidth)*.5f;
    {
        Graphics g(dc); prepare(g);
        auto state = g.Save(); g.TranslateTransform(x,static_cast<REAL>(r.top)+s); g.ScaleTransform(iconScale,iconScale);
        if (theme == 3) whale(g); else singer(g,theme == 4);
        g.Restore(state);
        Pen rule(color(p.accent,40),s); g.DrawLine(&rule,static_cast<REAL>(r.left)+5*s,static_cast<REAL>(r.bottom)-s,static_cast<REAL>(r.right)-5*s,static_cast<REAL>(r.bottom)-s);
    }
    if (title) {
        RECT text{static_cast<LONG>(x+iconWidth+7*s),r.top,r.right-MulDiv(5,static_cast<int>(dpi),96),r.bottom};
        const auto old = SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,p.accent);
        DrawTextW(dc,theme == 3 ? L"Deepseek 大肥鱼" : theme == 4 ? L"初音未来 · 01" : L"洛天依 · 云音",-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        SelectObject(dc,old);
    }
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
    GraphicsPath path; rounded(path,box,theme >= 3 ? 6*s : 4*s);
    if (selected) {
        LinearGradientBrush brush(PointF(box.X,box.Y),PointF(box.GetRight(),box.GetBottom()),color(p.selected),blend(p.selected,p.accent,theme >= 3 ? .35f : .0f));
        g.FillPath(&brush,&path);
        Pen outline(color(theme == 4 ? RGB(241,127,166) : p.accent,theme >= 3 ? 120 : 90),s); g.DrawPath(&outline,&path);
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
    GraphicsPath path; rounded(path,badge,(theme == 4 ? 2 : 7)*s);
    SolidBrush fill(color(selected ? p.selectedText : p.accent,selected ? 23 : 12)); g.FillPath(&fill,&path);
    if (theme == 5) { Pen rim(color(selected ? p.selectedText : p.accent,90),s*.6f); g.DrawPath(&rim,&path); }
}
}
