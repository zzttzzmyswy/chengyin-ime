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
    if ((requested < 0 || requested > 2) && (requested<10 || requested>13)) requested = 0;
    HIGHCONTRASTW contrast{sizeof(contrast),0,nullptr};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags & HCF_HIGHCONTRASTON) ? 0 : requested;
}
int themeRadius(int theme, UINT dpi) { return theme ? MulDiv(8, static_cast<int>(dpi),96) : 0; }
void drawThemeSurface(HDC dc, const RECT &r, const Palette &p, int theme, UINT dpi,int radius) {
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
    GraphicsPath shape; rounded(shape,box,static_cast<REAL>(radius>=0 ? radius : themeRadius(theme,dpi)));
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
    GraphicsPath path; rounded(path,box,(theme==10 ? 9 : theme==12 ? 7 : 4)*s);
    if (selected) {
        LinearGradientBrush brush(PointF(box.X,box.Y),PointF(box.GetRight(),box.GetBottom()),color(p.selected),color(p.selected));
        g.FillPath(&brush,&path);
        Pen outline(color(p.accent,90),s); g.DrawPath(&outline,&path);
        if (theme == 2 || theme==11) { Pen gold(Color(230,211,184,126),2*s); g.DrawLine(&gold,box.X+2*s,box.Y+4*s,box.X+2*s,box.GetBottom()-4*s); }
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
const Skin *activeSkin(const Preferences &p) {
    if(!visualTheme(p.theme)) return nullptr;
    return p.theme==13 ? p.skin.get() : builtinSkin(p.theme).get();
}
int skinRail(const Preferences &p,UINT dpi) {
    if(!p.skinDecorations) return 0;
    auto s=activeSkin(p);
    return s && (s->image || s->ornament) ? MulDiv(s->image ? (p.layout ? 68 : 94) : 38,static_cast<int>(dpi),96) : 0;
}
int skinSelectionStyle(const Preferences &p) {
    const auto *skin=activeSkin(p);
    return skin ? (skin->ornament ? skin->ornament+9 : 1) : visualTheme(p.theme);
}
void drawSkinSurface(HDC dc,const RECT &r,const Palette &p,const Preferences &prefs,UINT dpi,int rail) {
    const auto *skin=activeSkin(prefs);
    drawThemeSurface(dc,r,p,visualTheme(prefs.theme),dpi,skin ? MulDiv(skin->radius,static_cast<int>(dpi),96) : -1);
    if(!skin || !ensureRuntime()) return;
    const REAL s=static_cast<REAL>(dpi)/96;
    {
    Graphics g(dc);prepare(g);
    // Quiet ornament rail is disjoint from all candidate text and pointer targets.
    const REAL left=static_cast<REAL>(r.right-rail),right=static_cast<REAL>(r.right)-8*s;
    if(rail>0) {
        Pen divider(color(p.accent,40),s);g.DrawLine(&divider,left+2*s,static_cast<REAL>(r.top)+12*s,left+2*s,static_cast<REAL>(r.bottom)-12*s);
        if(skin->ornament==1) {
            Pen stem(color(p.accent,110),s);
            REAL x=left+rail*.52f,y=static_cast<REAL>(r.bottom)-16*s;
            g.DrawBezier(&stem,x,y,x-12*s,y-24*s,x+12*s,y-50*s,x-3*s,y-76*s);
            SolidBrush leaf(color(p.accent,85));
            for(int i=0;i<4;++i) g.FillEllipse(&leaf,x+(i%2 ? -12 : 1)*s,y-(16+i*14)*s,11*s,4*s);
        } else if(skin->ornament==2) {
            Pen line(color(p.accent,90),s);
            REAL x=left+rail*.52f,y=static_cast<REAL>(r.top)+28*s;
            g.DrawLine(&line,x,y,x-8*s,y+25*s);g.DrawLine(&line,x-8*s,y+25*s,x+6*s,y+47*s);
            SolidBrush star(color(p.accent,210));
            for(int i=0;i<3;++i) g.FillEllipse(&star,x+(i==1?-8:i==2?6:0)*s-2*s,y+(i==1?25:i==2?47:0)*s-2*s,4*s,4*s);
        } else {
            Pen wave(color(p.accent,70),s);
            REAL y=static_cast<REAL>(r.bottom)-10*s;
            g.DrawBezier(&wave,left+6*s,y,left+rail*.4f,y-8*s,right-12*s,y+6*s,right,y-3*s);
        }
    }
    }
    // Leave the GDI+ drawing scope before the image uses the same HDC.
    if(rail>0 && skin->image) {
        RECT image{r.right-rail+MulDiv(5,static_cast<int>(dpi),96),r.top+MulDiv(5,static_cast<int>(dpi),96),r.right-MulDiv(5,static_cast<int>(dpi),96),r.bottom-MulDiv(8,static_cast<int>(dpi),96)};
        drawSkinImage(dc,image,*skin);
    }
}
}
