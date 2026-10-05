#include "skin.h"
#include "preferences.h"
#include <cstdio>
#include <cstdlib>
namespace myswy { HINSTANCE module=nullptr; LONG objects=0; }
static void require(bool ok,const char *message) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main() {
    using namespace myswy;
    for(int id:{10,11,12}) {
        auto skin=builtinSkin(id);require(skin && validSkin(*skin),"built-in skin contrast and bounds");
        auto bytes=encodeSkin(*skin);Skin copy;
        require(parseSkin(bytes,copy),"self-contained skin round trip");
        require(copy.name==skin->name && copy.png==skin->png && copy.radius==skin->radius && copy.selected==skin->selected,"round trip preserves artwork and style");
        if(id==12) require(copy.image && !copy.png.empty(),"whale PNG decoded and retained");
        bytes.insert(bytes.end(),{'R','a','d','i','u','s','=','2','\n'});
        require(!parseSkin(bytes,copy),"duplicate fields rejected");
    }
    auto parse=[](const char *text,Skin &out) {std::string s=text;return parseSkin({s.begin(),s.end()},out);};
    Skin prior;prior.name=L"unchanged";
    require(!parse("Version=1\nName=bad\nRadius=99\n",prior) && prior.name==L"unchanged","bad bounds leave caller state untouched");
    require(!parse("Version=1\nName=bad\nText=#FFFFFF\nSurface=#FFFFFF\n",prior),"unreadable text rejected");
    require(!parse("Version=1\nName=bad\nImage=../../private.png\n",prior),"external file references rejected");
    require(!parse("Version=1\nName=bad\nScript=launch.exe\n",prior),"unsupported behavior rejected");
    require(!parse("Version=1\nName=bad\nImage=AAAA\n",prior),"non-PNG bytes rejected");
    require(!parse("Version=2\nName=bad\n",prior),"future incompatible version rejected");
    Skin bomb=*builtinSkin(12);bomb.png[16]=255;
    require(!parseSkin(encodeSkin(bomb),prior),"oversized decoded dimensions rejected before decoder allocation");
    require(!parseSkin(std::vector<unsigned char>(3*1024*1024+1,'a'),prior),"skin package size bounded");
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);
    auto dir=std::wstring(temp)+L"MyswySkin-"+std::to_wstring(GetCurrentProcessId());
    require(CreateDirectoryW(dir.c_str(),nullptr)!=0,"isolated skin fixture directory");
    const auto file=dir+L"\\skin-test.cyskin",settings=dir+L"\\preferences.ini";
    require(atomicWrite(file,encodeSkin(*builtinSkin(12))),"write isolated image package");
    require(skinName(file)==builtinSkin(12)->name,"library reads only bounded name header");
    Preferences p;p.theme=13;p.skinFile=L"skin-test.cyskin";p.skin=builtinSkin(12);p.fontSize=25;
    require(savePreferences(settings,p),"save skin reference");
    Preferences loaded;require(tryLoadPreferences(settings,loaded) && loaded.theme==13 && loaded.skin && loaded.skin->image && loaded.fontSize==25,"load immutable custom skin snapshot");
    p.skinFile=L"../private.cyskin";require(!validPreferences(p),"path traversal prohibited");
    require(DeleteFileW(file.c_str())!=0,"remove isolated skin");
    require(tryLoadPreferences(settings,loaded) && loaded.theme==0 && !loaded.skin && loaded.fontSize==25,"missing skin gracefully falls back preserving unrelated preferences");
    DeleteFileW(settings.c_str());RemoveDirectoryW(dir.c_str());
    std::puts("PASS: skin PNG/UTF-8 roundtrip, limits, contrast, malformed input, immutable preference snapshot and missing-file fallback");
}
