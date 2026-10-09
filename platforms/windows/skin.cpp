// SPDX-License-Identifier: GPL-3.0-or-later
#include "skin.h"
#include <objidl.h>
#include <gdiplus.h>
#include <wincrypt.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace chengyin {
namespace {
struct Runtime {
    ULONG_PTR token = 0;
    Runtime() { Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&token,&input,nullptr); }
    ~Runtime() { if (token) Gdiplus::GdiplusShutdown(token); }
};
// GDI+ must finish before returning to the host, never in DLL static destruction.
struct Stream { IStream *value=nullptr; ~Stream() { if(value) value->Release(); } };
double luminance(COLORREF c) {
    auto linear=[](BYTE b) { double x=b/255.0; return x<=0.04045 ? x/12.92 : std::pow((x+0.055)/1.055,2.4); };
    return .2126*linear(GetRValue(c))+.7152*linear(GetGValue(c))+.0722*linear(GetBValue(c));
}
bool contrast(COLORREF a,COLORREF b) {
    double x=luminance(a),y=luminance(b); return (std::max(x,y)+.05)/(std::min(x,y)+.05)>=4.5;
}
}
struct SkinImage {
    std::mutex mutex;
    int width=0,height=0;
    std::vector<unsigned char> pixels;
    struct Scaled { int width=0,height=0;HBITMAP bitmap=nullptr; };
    std::array<Scaled,4> scaled{};
    size_t next=0;
    ~SkinImage() { for(auto &item:scaled) if(item.bitmap) DeleteObject(item.bitmap); }
};
static std::shared_ptr<SkinImage> decode(const std::vector<unsigned char> &bytes) {
    if (bytes.empty()) return {};
    // Check PNG IHDR before allowing any decoder allocation (max 1024 x 1024).
    const unsigned char signature[]{137,80,78,71,13,10,26,10};
    if (bytes.size()<33 || bytes.size()>2*1024*1024 || !std::equal(std::begin(signature),std::end(signature),bytes.begin())
        || bytes[12]!='I' || bytes[13]!='H' || bytes[14]!='D' || bytes[15]!='R') return {};
    auto dimension=[&](size_t at) { return (uint32_t(bytes[at])<<24)|(uint32_t(bytes[at+1])<<16)|(uint32_t(bytes[at+2])<<8)|bytes[at+3]; };
    if (!dimension(16) || !dimension(20) || dimension(16)>1024 || dimension(20)>1024) return {};
    Runtime runtime;
    if(!runtime.token) return {};
    Stream stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream.value))) return {};
    ULONG written=0;
    if (FAILED(stream.value->Write(bytes.data(),static_cast<ULONG>(bytes.size()),&written)) || written!=bytes.size()) return {};
    LARGE_INTEGER zero{}; stream.value->Seek(zero,STREAM_SEEK_SET,nullptr);
    std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromStream(stream.value,FALSE));
    if (!bitmap || bitmap->GetLastStatus()!=Gdiplus::Ok || bitmap->GetWidth()!=dimension(16)
        || bitmap->GetHeight()!=dimension(20)) return {};
    auto image=std::make_shared<SkinImage>();
    image->width=static_cast<int>(bitmap->GetWidth());image->height=static_cast<int>(bitmap->GetHeight());
    image->pixels.resize(static_cast<size_t>(image->width)*image->height*4);
    Gdiplus::Rect rect(0,0,image->width,image->height);Gdiplus::BitmapData locked{};
    if(bitmap->LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&locked)!=Gdiplus::Ok) return {};
    for(int y=0;y<image->height;++y)
        std::memcpy(image->pixels.data()+static_cast<size_t>(y)*image->width*4,
                    static_cast<unsigned char *>(locked.Scan0)+static_cast<ptrdiff_t>(y)*locked.Stride,image->width*4);
    bitmap->UnlockBits(&locked);
    return image;
}
bool validSkin(const Skin &s) {
    return !s.name.empty() && s.name.size()<=48 && s.name.find_first_of(L"\r\n=\0",0,4)==std::wstring::npos
        && s.radius>=0 && s.radius<=24 && s.padding>=3 && s.padding<=12 && s.ornament>=0 && s.ornament<=3
        && contrast(s.text,s.surface) && contrast(s.muted,s.surface) && contrast(s.text,s.background)
        && contrast(s.muted,s.background) && contrast(s.accent,s.surface) && contrast(s.accent,s.background)
        && contrast(s.selectedText,s.selected) && s.png.size()<=2*1024*1024;
}
bool parseSkin(const std::vector<unsigned char> &bytes,Skin &output) {
    if (bytes.empty() || bytes.size()>3*1024*1024) return false;
    std::string text(bytes.begin(),bytes.end());
    if (text.find('\0')!=std::string::npos) return false;
    std::istringstream input(text); std::string line; std::map<std::string,std::string> fields;
    while(std::getline(input,line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty() || line[0]=='#') continue;
        auto eq=line.find('=');
        if (eq==std::string::npos || !fields.emplace(line.substr(0,eq),line.substr(eq+1)).second) return false;
    }
    const std::vector<std::string> allowed{"Version","Name","Background","Surface","Text","Muted","Border","Accent","Selected","SelectedText","Radius","Padding","Ornament","Image"};
    for (const auto &field:fields) if (std::find(allowed.begin(),allowed.end(),field.first)==allowed.end()) return false;
    if (fields["Version"]!="1") return false;
    Skin s;
    auto name=fields["Name"];
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0);
    if (length<=0 || length>48) return false;
    s.name.resize(length); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),s.name.data(),length);
    auto number=[&](const char *key,int &target) {
        auto at=fields.find(key); if (at==fields.end()) return true;
        const auto &v=at->second; if(v.empty() || v.size()>2 || v.find_first_not_of("0123456789")!=std::string::npos) return false;
        target=std::stoi(v); return true;
    };
    auto color=[&](const char *key,COLORREF &target) {
        auto at=fields.find(key); if(at==fields.end()) return true;
        const auto &v=at->second;
        if(v.size()!=7 || v[0]!='#' || v.find_first_not_of("0123456789ABCDEFabcdef",1)!=std::string::npos) return false;
        unsigned long n=std::stoul(v.substr(1),nullptr,16); target=RGB((n>>16)&255,(n>>8)&255,n&255); return true;
    };
    if (!number("Radius",s.radius)||!number("Padding",s.padding)||!number("Ornament",s.ornament)
        ||!color("Background",s.background)||!color("Surface",s.surface)||!color("Text",s.text)||!color("Muted",s.muted)
        ||!color("Border",s.border)||!color("Accent",s.accent)||!color("Selected",s.selected)||!color("SelectedText",s.selectedText)) return false;
    auto image=fields.find("Image");
    if(image!=fields.end() && !image->second.empty()) {
        DWORD size=0;
        if(!CryptStringToBinaryA(image->second.c_str(),static_cast<DWORD>(image->second.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,nullptr,&size,nullptr,nullptr)
            || size>2*1024*1024) return false;
        s.png.resize(size);
        if(!CryptStringToBinaryA(image->second.c_str(),static_cast<DWORD>(image->second.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,s.png.data(),&size,nullptr,nullptr)) return false;
        s.image=decode(s.png); if(!s.image) return false;
    }
    if(!validSkin(s)) return false;
    output=std::move(s); return true;
}
std::vector<unsigned char> encodeSkin(const Skin &s) {
    if(!validSkin(s)) return {};
    std::ostringstream out;
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.name.data(),static_cast<int>(s.name.size()),nullptr,0,nullptr,nullptr);
    if(n<=0) return {};
    std::string name(n,' '); WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.name.data(),static_cast<int>(s.name.size()),name.data(),n,nullptr,nullptr);
    out<<"Version=1\nName="<<name<<"\n";
    auto color=[&](const char *key,COLORREF c) { char hex[8]{}; std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",GetRValue(c),GetGValue(c),GetBValue(c)); out<<key<<'='<<hex<<'\n'; };
    color("Background",s.background);color("Surface",s.surface);color("Text",s.text);color("Muted",s.muted);color("Border",s.border);
    color("Accent",s.accent);color("Selected",s.selected);color("SelectedText",s.selectedText);
    out<<"Radius="<<s.radius<<"\nPadding="<<s.padding<<"\nOrnament="<<s.ornament<<"\n";
    if(!s.png.empty()) {
        DWORD size=0; CryptBinaryToStringA(s.png.data(),static_cast<DWORD>(s.png.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&size);
        std::string encoded(size,'\0');
        if(!CryptBinaryToStringA(s.png.data(),static_cast<DWORD>(s.png.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,encoded.data(),&size)) return {};
        out<<"Image="<<encoded.c_str()<<'\n';
    }
    auto text=out.str(); return {text.begin(),text.end()};
}
std::shared_ptr<const Skin> loadSkin(const std::wstring &path) {
    std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    if(!file) return {};
    auto size=file.tellg(); if(size<=0 || size>3*1024*1024) return {};
    std::vector<unsigned char> bytes(static_cast<size_t>(size)); file.seekg(0);
    if(!file.read(reinterpret_cast<char *>(bytes.data()),size)) return {};
    auto skin=std::make_shared<Skin>(); return parseSkin(bytes,*skin) ? skin : nullptr;
}
std::wstring skinName(const std::wstring &path) {
    std::ifstream file(std::filesystem::path(path),std::ios::binary);
    char header[4096]{};file.read(header,sizeof(header));
    std::string text(header,static_cast<size_t>(file.gcount()));
    if(text.rfind("Version=1\n",0)!=0 && text.rfind("Version=1\r\n",0)!=0) return {};
    auto start=text.find("\nName=");if(start==std::string::npos) return {};start+=6;
    auto end=text.find_first_of("\r\n",start);if(end==std::string::npos || end-start>192) return {};
    auto name=text.substr(start,end-start);
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0);
    if(n<=0 || n>48) return {};
    std::wstring value(n,L' ');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),value.data(),n);
    return value;
}
static std::shared_ptr<const Skin> makeBuiltinSkin(int theme) {
    if(theme<10 || theme>12) return {};
    auto s=std::make_shared<Skin>();
    if(theme==10) s->name=L"青瓷 · 雨后";
    if(theme==11) {
        s->name=L"夜航 · 星图"; s->background=RGB(21,29,46);s->surface=RGB(28,38,57);s->text=RGB(235,241,247);
        s->muted=RGB(170,191,209);s->border=RGB(66,85,107);s->accent=RGB(219,187,121);
        s->selected=RGB(61,79,103);s->selectedText=RGB(255,246,224);s->ornament=2;s->radius=8;
    }
    if(theme==12) {
        s->name=L"Q 版大肥鱼";s->background=RGB(224,237,253);s->surface=RGB(247,251,255);s->text=RGB(28,49,88);
        s->muted=RGB(71,96,132);s->border=RGB(169,193,225);s->accent=RGB(53,97,182);s->selected=RGB(44,83,154);
        s->selectedText=RGB(255,255,255);s->ornament=3;s->radius=16;
        // User-authorized cutout is linked as immutable bytes, with provenance in assets/skins.
        extern const unsigned char whalePng[]; extern const size_t whalePngSize;
        s->png.assign(whalePng,whalePng+whalePngSize);s->image=decode(s->png);
    }
    return s;
}
std::shared_ptr<const Skin> builtinSkin(int theme) {
    if(theme<10 || theme>12) return {};
    static const std::shared_ptr<const Skin> skins[]{makeBuiltinSkin(10),makeBuiltinSkin(11),makeBuiltinSkin(12)};
    return skins[theme-10];
}
void drawSkinImage(HDC dc,const RECT &r,const Skin &s) {
    if(!s.image) return;
    std::lock_guard<std::mutex> lock(s.image->mutex);
    auto &image=*s.image;
    if(r.right<=r.left || r.bottom<=r.top) return;
    const float scale=std::min({float(r.right-r.left)/image.width,float(r.bottom-r.top)/image.height,512.0f/std::max(image.width,image.height)});
    int w=std::max(1,static_cast<int>(image.width*scale)),h=std::max(1,static_cast<int>(image.height*scale));
    SkinImage::Scaled *cached=nullptr;
    for(auto &item:image.scaled) if(item.bitmap && item.width==w && item.height==h) {cached=&item;break;}
    if(!cached) {
        Runtime runtime;
        if(!runtime.token) return;
        Gdiplus::Bitmap original(image.width,image.height,image.width*4,PixelFormat32bppPARGB,image.pixels.data());
        Gdiplus::Bitmap resized(w,h,PixelFormat32bppPARGB);
        if(resized.GetLastStatus()!=Gdiplus::Ok) return;
        { Gdiplus::Graphics resample(&resized);resample.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
          if(resample.DrawImage(&original,Gdiplus::Rect(0,0,w,h))!=Gdiplus::Ok) return; }
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;
        info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void *pixels=nullptr;HBITMAP dib=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!dib) return;
        Gdiplus::Rect rect(0,0,w,h);Gdiplus::BitmapData locked{};
        if(resized.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&locked)!=Gdiplus::Ok) {DeleteObject(dib);return;}
        for(int y=0;y<h;++y) std::memcpy(static_cast<unsigned char *>(pixels)+static_cast<size_t>(y)*w*4,
            static_cast<unsigned char *>(locked.Scan0)+static_cast<ptrdiff_t>(y)*locked.Stride,w*4);
        resized.UnlockBits(&locked);
        cached=&image.scaled[image.next++%image.scaled.size()];
        if(cached->bitmap) DeleteObject(cached->bitmap);
        cached->width=w;cached->height=h;cached->bitmap=dib;
    }
    HDC source=CreateCompatibleDC(dc);
    if(!source) return;
    auto old=SelectObject(source,cached->bitmap);
    AlphaBlend(dc,r.left+(r.right-r.left-w)/2,r.top+(r.bottom-r.top-h)/2,w,h,source,0,0,w,h,{AC_SRC_OVER,0,255,AC_SRC_ALPHA});
    SelectObject(source,old);DeleteDC(source);
}
}
