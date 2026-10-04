#include "common.h"
#include "settings.h"
#include "preferences.h"
#include "update.h"
#include "theme_art.h"
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <imm.h>
#include <inputscope.h>
#include <richedit.h>
#include <algorithm>
#include <atomic>
#include <cwchar>
#include <sstream>
#include <thread>
#include <vector>
namespace myswy {
namespace {
constexpr UINT kImported = WM_APP + 31;
constexpr UINT kUpdateReady = WM_APP + 32, kUpdateDownloaded = WM_APP + 33;
constexpr int kSave = 100, kDefaults = 101;
const wchar_t *kNames[] {L"输入", L"候选", L"主题", L"词库", L"学习", L"输入测试", L"关于", L"模糊音"};
struct Control {
    HWND window;
    int x, y, width, height;
};
class Settings {
  public:
    Settings(HINSTANCE instance, int page): instance_(instance),
        draft_(loadPreferences(userFile(L"preferences.ini"))), page_(page) {}
    ~Settings() {
        if (import_.joinable())
            import_.join();
        if (updateWorker_.joinable())
            updateWorker_.join();
        releaseFonts();
        if (surface_)
            DeleteObject(surface_);
        if (background_)
            DeleteObject(background_);
    }
    HWND window_ = nullptr, pane_ = nullptr;
    LRESULT message(HWND, UINT, WPARAM, LPARAM, bool);
  private:
    int scaled(int n)const {
        return MulDiv(n, static_cast<int>(dpi_), 96);
    }
    void fonts();
    void releaseFonts();
    void buildPage();
    void layout();
    void scroll(int);
    HWND control(const wchar_t *, const wchar_t *, DWORD, int, int, int, int, int, bool = false);
    void label(const wchar_t *, int, int, int, int, bool = false);
    HWND combo(int, int, int, int, const std::vector<std::wstring> &, int);
    void check(int, const wchar_t *, int, bool);
    void command(int, int);
    void collect(int);
    void paint(HWND, bool);
    bool chooseFile(std::wstring &, bool, const wchar_t *);
    void importDictionary(bool);
    void finishImport(bool);
    void notify(const std::wstring &, bool = false);
    std::wstring diagnostics();
    void copyDiagnostics();
    void testFields(int &);
    void checkUpdate(bool download = false);
    HINSTANCE instance_;
    Preferences draft_;
    HFONT body_ = nullptr, sample_ = nullptr;
    HBRUSH surface_ = nullptr, background_ = nullptr;
    Palette colors_{};
    UINT dpi_ = 96;
    int page_ = 0, scroll_ = 0, horizontal_ = 0, totalHeight_ = 0, paneWidth_ = 700;
    bool dirty_ = false, closing_ = false;
    std::wstring notification_ = L"修改后点击“应用”，已打开的应用会自动接收新设置。";
    std::vector<Control> controls_, testControls_;
    std::thread import_;
    std::thread updateWorker_;
    ReleaseUpdate release_;
    ReleaseUpdate pendingRelease_;
    std::wstring updateMessage_ = L"尚未检查更新。", updatePath_;
    bool updating_ = false;
    std::vector<DictionaryEntry> dictionaries_;
    std::atomic<bool> importing_{false};
};
void Settings::releaseFonts() {
    if (body_)
        DeleteObject(body_);
    if (sample_)
        DeleteObject(sample_);
    body_ = sample_ = nullptr;
}
void Settings::fonts() {
    // The settings dialog follows native Windows colors. The theme option only
    // changes the candidate window, so common controls stay visually consistent.
    const HFONT oldBody = body_, oldSample = sample_;
    HFONT body = createUIFont(14, dpi_);
    HFONT sample = createUIFont(draft_.fontSize, dpi_, draft_.font);
    if (!body || !sample) {
        if (body) DeleteObject(body);
        if (sample) DeleteObject(sample);
        return;
    }
    body_ = body;
    sample_ = sample;
    // All HWNDs must stop referring to the old HFONT before DeleteObject.
    // Native themes can defer their next paint until the pointer hovers a control.
    if (window_)
        EnumChildWindows(window_, [](HWND child, LPARAM target) -> BOOL {
            auto *self = reinterpret_cast<Settings *>(target);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetDlgCtrlID(child) == 308 ? self->sample_ : self->body_), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(this));
    if (oldBody) DeleteObject(oldBody);
    if (oldSample) DeleteObject(oldSample);
    colors_ = {GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNTEXT), GetSysColor(COLOR_GRAYTEXT), GetSysColor(COLOR_3DSHADOW), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT)};
    if (surface_)
        DeleteObject(surface_);
    if (background_)
        DeleteObject(background_);
    surface_ = CreateSolidBrush(colors_.surface);
    background_ = CreateSolidBrush(colors_.background);
    for (auto &c : testControls_)
        SendMessageW(c.window, WM_SETFONT, reinterpret_cast<WPARAM>(body_), FALSE);
}
HWND Settings::control(const wchar_t *kind, const wchar_t *text, DWORD style, int id, int x, int y, int width,
                       int height, bool muted) {
    DWORD extra = (!std::wcscmp(kind, L"EDIT") || !std::wcscmp(kind, L"RICHEDIT50W")) ? WS_EX_CLIENTEDGE : 0;
    HWND hwnd = CreateWindowExW(extra, kind, text, WS_CHILD | WS_VISIBLE | style, scaled(x), scaled(y - scroll_),
                                scaled(width), scaled(height), pane_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(body_), FALSE);
    if (muted)
        SetPropW(hwnd, L"Myswy.Muted", reinterpret_cast<HANDLE>(1));
    controls_.push_back({hwnd, x, y, width, height});
    return hwnd;
}
void Settings::label(const wchar_t *text, int x, int y, int width, int height, bool muted) {
    control(L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, x, y, width, height, muted);
}
HWND Settings::combo(int id, int x, int y, int width, const std::vector<std::wstring> &choices,
                     int selected) {
    HWND hwnd = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, id, x, y, width, 220);
    for (auto &text : choices)
        SendMessageW(hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(hwnd, CB_SETCURSEL, selected, 0);
    return hwnd;
}
void Settings::check(int id, const wchar_t *text, int y, bool selected) {
    HWND hwnd = control(L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE, id, 20, y, paneWidth_ - 48,
                        28);
    SendMessageW(hwnd, BM_SETCHECK, selected ? BST_CHECKED : BST_UNCHECKED, 0);
}
void Settings::testFields(int &y) {
    const wchar_t *texts[] {L"普通编辑框", L"RichEdit 编辑框", L"密码框（不显示候选，不参与学习）"};
    if (testControls_.empty()) {
        for (int i = 0; i < 3; ++i) {
            HWND label = CreateWindowW(L"STATIC", texts[i], WS_CHILD | SS_NOPREFIX, 0, 0, 0, 0, pane_, nullptr, instance_,
                                       nullptr);
            testControls_.push_back({label, 20, 0, 0, 24});
            DWORD style = WS_CHILD | WS_TABSTOP | (i == 2 ? ES_PASSWORD | ES_AUTOHSCROLL : ES_MULTILINE | ES_AUTOVSCROLL |
                                                   WS_VSCROLL);
            HWND field = CreateWindowExW(WS_EX_CLIENTEDGE, i == 1 ? L"RICHEDIT50W" : L"EDIT", L"", style, 0, 0, 0, 0,
                                         pane_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(701 + i)), instance_, nullptr);
            if (!field && i == 1)
                field = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style, 0, 0, 0, 0, pane_,
                                        reinterpret_cast<HMENU>(702), instance_, nullptr);
            testControls_.push_back({field, 20, 0, 0, i == 2 ? 30 : 130});
            if (i == 1)
                SendMessageW(field, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
            ImmAssociateContextEx(field, nullptr, IACE_DEFAULT);
            if (i == 2) {
                HMODULE library = LoadLibraryW(L"msctf.dll");
                using Scope = HRESULT(WINAPI *)(HWND, InputScope);
                auto scope = procedureAddress<Scope>(library, "SetInputScope");
                if (scope)
                    scope(field, IS_PASSWORD);
                if (library)
                    FreeLibrary(library);
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        auto &label = testControls_[i * 2], &field = testControls_[i * 2 + 1];
        label.y = y;
        label.width = paneWidth_ - 48;
        y += 26;
        field.y = y;
        field.width = paneWidth_ - 48;
        y += field.height + 16;
        for (auto *c : {
                    &label, &field
                }) {
            SendMessageW(c->window, WM_SETFONT, reinterpret_cast<WPARAM>(body_), FALSE);
            SetWindowPos(c->window, nullptr, scaled(c->x), scaled(c->y - scroll_), scaled(c->width), scaled(c->height),
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
}
void Settings::buildPage() {
    SendMessageW(pane_, WM_SETREDRAW, FALSE, 0);
    for (auto &c : controls_)
        DestroyWindow(c.window);
    controls_.clear();
    for (auto &c : testControls_)
        ShowWindow(c.window, SW_HIDE);
    int y = 12;
    size_t group = 0;
    auto begin = [&](const wchar_t *text) {
        group = controls_.size();
        control(L"BUTTON", text, BS_GROUPBOX | WS_CLIPSIBLINGS, 0, 8, y, paneWidth_ - 24, 30);
        y += 28;
    };
    auto end = [&] {auto &c = controls_[group]; c.height = y - c.y + 10; SetWindowPos(c.window, HWND_BOTTOM, scaled(c.x), scaled(c.y - scroll_), scaled(c.width), scaled(c.height), SWP_NOACTIVATE); y += 26;};
    auto paragraph = [&](const wchar_t *text) {
        RECT measured{0, 0, scaled(paneWidth_ - 48), 0};
        HDC dc = GetDC(pane_);
        auto old = SelectObject(dc, body_);
        DrawTextW(dc, text, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        ReleaseDC(pane_, dc);
        int height = std::max(20, MulDiv(measured.bottom, 96, static_cast<int>(dpi_)) + 6);
        label(text, 20, y, paneWidth_ - 48, height);
        y += height + 8;
    };
    auto checkbox = [&](int id, const wchar_t *text, bool value) {
        check(id, text, y, value);
        y += 34;
    };
    auto buttons = [&](int a, const wchar_t *ta, int b, const wchar_t *tb, int c, const wchar_t *tc) {
        int width = (paneWidth_ - 68) / 3;
        control(L"BUTTON", ta, WS_TABSTOP | BS_PUSHBUTTON, a, 20, y, width, 30);
        control(L"BUTTON", tb, WS_TABSTOP | BS_PUSHBUTTON, b, 30 + width, y, width, 30);
        control(L"BUTTON", tc, WS_TABSTOP | BS_PUSHBUTTON, c, 40 + 2 * width, y, width, 30);
        y += 40;
    };
    switch (page_) {
    case 7: {
        begin(L"模糊音（双向匹配）");
        paragraph(L"按需要勾选容易混淆的声母和韵母。正确拼写候选优先，发生匹配的标准拼音字母加粗显示。各项默认关闭。");
        const wchar_t *pairs[]{L"zh ↔ z",L"ch ↔ c",L"sh ↔ s",L"n ↔ l",L"f ↔ h",L"l ↔ r",L"an ↔ ang",L"en ↔ eng",L"in ↔ ing",L"ian ↔ iang",L"uan ↔ uang"};
        for (int i=0;i<11;++i) checkbox(801+i,pairs[i],(draft_.matchingOptions&(1u<<i))!=0);
        end();
        begin(L"常见键盘失误");
        paragraph(L"以下是四类通用纠错规则，适用于词库中的拼音，不限于所列示例。");
        const wchar_t *errors[]{L"相邻字母按反（示例：zhnag → zhang）",L"漏按一个字母（示例：png → ping）",L"QWERTY 相邻键误按（示例：hso → hao）",L"重复按键（示例：shii → shi）"};
        for (int i=0;i<4;++i) checkbox(820+i,errors[i],(draft_.matchingOptions&(1u<<(16+i)))!=0);
        paragraph(L"键盘纠错用于至少三个输入字母；每音节最多一次，每个词最多两次。交换只限相邻字母，不作任意乱排。候选显示标准拼音并加粗修正位置；原始拼音仍可编辑，空格提交原始输入。应用后从下一段输入生效。");
        end();
        break;
    }
    case 0:
        begin(L"输入模式");
        checkbox(202, L"启动输入服务时默认使用英文", draft_.defaultEnglish);
        paragraph(
            L"修改默认模式不会改变当前的中英文状态。可随时用左 Shift 或 Ctrl+Space 切换。");
        label(L"Shift 切换键：", 20, y, 160, 24);
        combo(201, 180, y, 260, {L"不使用 Shift", L"左 Shift", L"左右 Shift"}, draft_.shiftSwitch);
        y += 36;
        paragraph(L"单独轻按 Shift 才切换；Shift+字母、组合键和长按不切换。");
        end();
        begin(L"标点与联想");
        checkbox(204, L"中文模式使用中文标点", draft_.chinesePunctuation);
        paragraph(
            L"中文模式输入 ，。？！ 和成对引号；英文模式保留英文标点。拼音中的 ' 仍用来分隔音节。");
        checkbox(203, L"提交中文后显示联想词", draft_.associations);
        paragraph(L"Tab 或鼠标选择联想词；空格、数字和 Enter 继续交给应用。");
        end();
        begin(L"输入操作");
        paragraph(
            L"空格提交原始拼音；数字或鼠标选词。PgUp/PgDn 或 -/= 翻页。\n左右键、Home、End 编辑拼音；Esc 取消。Shift 组合键直接输入大写和特殊字符。\n输入 xi'an 可明确选择“西安”。");
        end();
        break;
    case 1: {
        begin(L"字体与候选数量");
        paragraph((L"候选字体：" + draft_.font).c_str());
        control(L"BUTTON", L"选择字体…", WS_TABSTOP | BS_PUSHBUTTON, 301, 20, y, 130, 30);
        label(L"字号：", 170, y + 4, 60, 24);
        std::vector<std::wstring> sizes;
        for (int n = 12; n <= 32; ++n)
            sizes.push_back(std::to_wstring(n));
        combo(302, 240, y, 90, sizes, draft_.fontSize - 12);
        y += 40;
        label(L"每页候选数：", 20, y, 150, 24);
        combo(200, 180, y, 160, {L"5 个", L"7 个", L"9 个"}, (draft_.pageSize - 5) / 2);
        y += 36;
        end();
        begin(L"排列与显示");
        label(L"排列：", 20, y, 100, 24);
        combo(304, 130, y, 250, {L"纵向列表", L"横向排列，自动换行"}, draft_.layout);
        y += 36;
        label(L"间距：", 20, y, 100, 24);
        combo(305, 130, y, 250, {L"紧凑", L"标准"}, draft_.density);
        y += 36;
        checkbox(306, L"显示拼音音节分隔符，如 ni'hao", draft_.separators);
        checkbox(307, L"无法在编辑框直接编辑时显示候选拼音", draft_.candidatePinyin);
        paragraph(L"可在编辑框直接编辑拼音时，候选窗自动隐藏重复拼音。其他情况下，纵向拼音放在右侧，横向放在下方。");
        end();
        begin(L"字体预览");
        std::wstring text;
        if (draft_.layout)
            text += L"1. 你好    2. 拟好    3. 你号";
        else
            text += L"1. 你好\r\n2. 拟好\r\n3. 你号";
        int height = (draft_.fontSize + 8) * (draft_.layout ? 2 : 4) + 8;
        HWND preview = control(L"STATIC", text.c_str(), SS_LEFT | SS_NOPREFIX | WS_BORDER, 308, 20, y,
                               paneWidth_ - 48, height);
        SendMessageW(preview, WM_SETFONT, reinterpret_cast<WPARAM>(sample_), FALSE);
        y += height + 8;
        end();
        break;
    }
    case 2:
        begin(L"候选主题");
        label(L"主题：", 20, y + 4, 100, 24);
        combo(303, 130, y, 300, {L"Windows 系统（自动亮 / 暗）", L"白", L"黑"}, draft_.theme);
        y += 40;
        paragraph(L"候选框可选择 Windows 系统、白或黑主题。系统主题自动跟随 Windows 亮暗切换；高对比度优先采用系统可读颜色。更多主题后续再开发。");
        end();
        begin(L"主题预览");
        control(L"STATIC", L"", SS_OWNERDRAW, 309, 20, y, paneWidth_ - 48, std::max(130, (draft_.fontSize + 18) * 3 + 12));
        y += std::max(130, (draft_.fontSize + 18) * 3 + 12) + 10;
        end();
        break;
    case 3: {
        begin(L"当前词库");
        paragraph(L"内置 87,540 条开源字词，来源为 Rime 与 jieba。");
        paragraph(L"内置词库始终保留。每个导入文件为一个独立条目，可以手动启用、停用或删除。删除条目不会删除原始文件或学习数据。");
        end();
        begin(L"自定义词库列表");
        HWND list = control(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                            405, 20, y, paneWidth_ - 48, 160);
        if (readDictionaryLibrary(customDictionaryPath(false), dictionaries_)) {
            int extent = 0;
            HDC dc = GetDC(list); auto old = SelectObject(dc, body_);
            for (const auto &entry : dictionaries_) {
                std::wstring text = (entry.enabled ? L"[启用]  " : L"[停用]  ") + entry.name;
                SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
                SIZE size{}; GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
                extent = std::max(extent, static_cast<int>(size.cx) + scaled(16));
            }
            SelectObject(dc, old); ReleaseDC(list, dc);
            SendMessageW(list, LB_SETHORIZONTALEXTENT, extent, 0);
            if (!dictionaries_.empty()) SendMessageW(list, LB_SETCURSEL, 0, 0);
        } else {
            dictionaries_.clear();
            paragraph(L"现有自定义词库损坏或无法读取。请备份数据目录中的 dictionary.custom 后修复；原文件保留。");
        }
        y += 172;
        paragraph(
            L"支持搜狗 SCEL、UTF-8 / UTF-16 / GBK 文本、TSV 和本项目二进制词库。最多管理 64 个文件，总容量 64 MiB。");
        buttons(401, L"添加词库…", 406, L"启用 / 停用", 407, L"删除所选");
        for (int id : {401,406,407}) EnableWindow(GetDlgItem(pane_, id), !importing_);
        paragraph(
            L"导入后自动同步到已打开的应用；正在输入的拼音使用原词库完成。无效文件或保存失败会保留原词库。");
        end();
        break;
    }
    case 4: {
        begin(L"选词学习");
        checkbox(501, L"根据选词习惯排序", draft_.learning);
        paragraph(
            L"成功提交中文后记录拼写、选词、频次与顺序。常用候选会优先出现，数据只保存在本机。\n密码和私密输入不参与学习；关闭此项会停止记录和个性化排序。");
        end();
        begin(L"学习数据");
        auto *profile = loadProfile(userFile(L"learning.profile"));
        paragraph(profile ? (L"已保存 " + std::to_wstring(myswy_profile_count(profile)) +
                             L" 项选词偏好，最多 4,096 项。").c_str() :
                  L"学习文件无法读取。可以导入有效备份或清除数据。");
        if (profile)
            myswy_profile_free(profile);
        buttons(502, L"备份…", 503, L"导入备份…", 504, L"清除学习数据");
        paragraph(L"清除或导入后自动同步。词库、设置与学习数据在升级和卸载后保留。");
        end();
        break;
    }
    case 5:
        begin(L"测试输入");
        paragraph(
            L"用 Win+Space 选择澄音，在下面输入 nihao、xi'an 或整句。左 Shift 切换中英文。\n本页不会保存测试内容；密码框应只输入英文且不显示候选。");
        testFields(y);
        end();
        break;
    case 6:
        begin(L"澄音输入法");
        paragraph(
            L"版本：0.1.0-preview11 · Windows x64\n本机离线输入；采用共享 Rust 核心与 Windows TSF。");
        paragraph(
            L"代码开源协议：MIT License · Copyright 2026 Myswy IM contributors\n允许使用、修改和分发，须保留版权和许可声明；软件按现状提供。词库及运行库有各自许可，随安装包提供。");
        buttons(605, L"开源协议", 606, L"仓库链接", 607, L"发行说明");
        end();
        begin(L"自动更新");
        checkbox(608, L"打开设置时自动检查更新", draft_.autoUpdate);
        paragraph(L"仅访问本项目 GitHub Releases，不上传输入或学习数据。发现更新后，点击下载并更新；校验 SHA-256 后启动安装器。未发布安装包时可查看本版发行说明。");
        paragraph(updateMessage_.c_str());
        control(L"BUTTON", L"检查更新", WS_TABSTOP | BS_PUSHBUTTON, 609, 20, y, 150, 30);
        control(L"BUTTON", L"下载并更新", WS_TABSTOP | BS_PUSHBUTTON, 610, 190, y, 150, 30);
        EnableWindow(GetDlgItem(pane_, 609), !updating_);
        EnableWindow(GetDlgItem(pane_, 610), !updating_ && release_.available);
        y += 40;
        end();
        begin(L"诊断与数据目录");
        paragraph(
            L"诊断信息包含版本、架构、字体、配置和注册路径，不包含输入正文或候选词。");
        control(L"BUTTON", L"复制诊断信息", WS_TABSTOP | BS_PUSHBUTTON, 603, 20, y, 160, 30);
        control(L"BUTTON", L"打开数据目录", WS_TABSTOP | BS_PUSHBUTTON, 604, 200, y, 160, 30);
        y += 40;
        end();
        begin(L"传统输入框");
        checkbox(601, L"候选无法定位时尝试系统光标和当前输入视图", draft_.caretFallback);
        paragraph(
            L"本版本提供 x64 输入服务。32 位应用及 ARM64 需要对应架构版本。实际 Windows 11 任务栏与应用兼容性须分别验证。");
        end();
        break;
    }
    totalHeight_ = y;
    RECT rect{};
    GetClientRect(pane_, &rect);
    int visible = MulDiv(rect.bottom, 96, static_cast<int>(dpi_));
    scroll_ = std::min(scroll_, std::max(0, totalHeight_ - visible));
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS, 0, totalHeight_ - 1, static_cast<UINT>(visible), scroll_, 0};
    SetScrollInfo(pane_, SB_VERT, &info, TRUE);
    const int viewportWidth = MulDiv(rect.right, 96, static_cast<int>(dpi_));
    horizontal_ = std::min(horizontal_, std::max(0, paneWidth_ - viewportWidth));
    SCROLLINFO horizontal{sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS, 0, paneWidth_ - 1, static_cast<UINT>(viewportWidth), horizontal_, 0};
    SetScrollInfo(pane_, SB_HORZ, &horizontal, TRUE);
    scroll(scroll_);
    SendMessageW(pane_, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(pane_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    SendMessageW(GetDlgItem(window_, 102), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(notification_.c_str()));
}
void Settings::layout() {
    RECT client{};
    GetClientRect(window_, &client);
    int width = MulDiv(client.right, 96, static_cast<int>(dpi_)), height = MulDiv(client.bottom, 96,
                static_cast<int>(dpi_));
    HWND tabs = GetDlgItem(window_, 10);
    SetWindowPos(tabs, nullptr, scaled(10), scaled(10), scaled(width - 20), scaled(height - 90),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    RECT inner{};
    GetClientRect(tabs, &inner);
    SendMessageW(tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&inner));
    MapWindowPoints(tabs, window_, reinterpret_cast<POINT *>(&inner), 2);
    inner.left += scaled(8);
    inner.top += scaled(4);
    inner.right -= scaled(8);
    inner.bottom -= scaled(4);
    paneWidth_ = std::max(560, MulDiv(inner.right - inner.left, 96, static_cast<int>(dpi_)));
    SetWindowPos(pane_, HWND_TOP, inner.left, inner.top, inner.right - inner.left, inner.bottom - inner.top,
                 SWP_NOACTIVATE);
    for (int id : {
                10, 100, 101, 102, 103
            })
        SendMessageW(GetDlgItem(window_, id), WM_SETFONT, reinterpret_cast<WPARAM>(body_), FALSE);
    SetWindowPos(GetDlgItem(window_, 102), nullptr, scaled(12), scaled(height - 74), scaled(width - 24),
                 scaled(32), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(window_, kDefaults), nullptr, scaled(12), scaled(height - 36), scaled(130),
                 scaled(28), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(window_, kSave), nullptr, scaled(width - 218), scaled(height - 36), scaled(96),
                 scaled(28), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(window_, 103), nullptr, scaled(width - 110), scaled(height - 36), scaled(96),
                 scaled(28), SWP_NOZORDER | SWP_NOACTIVATE);
    buildPage();
}
void Settings::scroll(int position) {
    RECT rect{};
    GetClientRect(pane_, &rect);
    int visible = MulDiv(rect.bottom, 96, static_cast<int>(dpi_));
    scroll_ = std::clamp(position, 0, std::max(0, totalHeight_ - visible));
    for (auto &c : controls_)
        SetWindowPos(c.window, nullptr, scaled(c.x - horizontal_), scaled(c.y - scroll_), scaled(c.width), scaled(c.height),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    if (page_ == 5)
        for (auto &c : testControls_)
            SetWindowPos(c.window, nullptr, scaled(c.x - horizontal_), scaled(c.y - scroll_), scaled(c.width), scaled(c.height),
                         SWP_NOZORDER | SWP_NOACTIVATE);
    SetScrollPos(pane_, SB_VERT, scroll_, TRUE);
    InvalidateRect(pane_, nullptr, FALSE);
}
void Settings::notify(const std::wstring &text, bool error) {
    notification_ = text;
    SendMessageW(GetDlgItem(window_, 102), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
    if (error)
        MessageBoxW(window_, text.c_str(), L"澄音", MB_OK | MB_ICONERROR);
}
bool Settings::chooseFile(std::wstring &path, bool save, const wchar_t *filter) {
    wchar_t file[32768] {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file;
    dialog.nMaxFile = static_cast<DWORD>(std::size(file));
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT :
                   OFN_FILEMUSTEXIST);
    dialog.lpstrDefExt = save ? L"chengyinuser" : nullptr;
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)))
        return false;
    path = file;
    return true;
}
void Settings::collect(int id) {
    auto choice = [&](int controlId) {
        return static_cast<int>(SendMessageW(GetDlgItem(pane_, controlId), CB_GETCURSEL, 0, 0));
    };
    auto checked = [&](int controlId) {
        return SendMessageW(GetDlgItem(pane_, controlId), BM_GETCHECK, 0, 0) == BST_CHECKED;
    };
    if ((id>=801 && id<=811) || (id>=820 && id<=823)) {
        const uint32_t bit=1u<<(id<820?id-801:id-820+16);
        if (checked(id)) draft_.matchingOptions|=bit; else draft_.matchingOptions&=~bit;
        dirty_=true; notify(L"有未保存的更改。点击“应用”后从下一段输入生效。"); return;
    }
    switch (id) {
    case 200:
        draft_.pageSize = 5 + choice(id) * 2;
        break;
    case 201:
        draft_.shiftSwitch = choice(id);
        break;
    case 202:
        draft_.defaultEnglish = checked(id);
        break;
    case 204:
        draft_.chinesePunctuation = checked(id);
        break;
    case 203:
        draft_.associations = checked(id);
        break;
    case 302:
        draft_.fontSize = choice(id) + 12;
        break;
    case 303:
        draft_.theme = choice(id);
        break;
    case 304:
        draft_.layout = choice(id);
        break;
    case 305:
        draft_.density = choice(id);
        break;
    case 306:
        draft_.separators = checked(id);
        break;
    case 307:
        draft_.candidatePinyin = checked(id);
        break;
    case 501:
        draft_.learning = checked(id);
        break;
    case 601:
        draft_.caretFallback = checked(id);
        break;
    case 608:
        draft_.autoUpdate = checked(id);
        break;
    default:
        return;
    }
    dirty_ = true;
    notification_ = L"有未保存的更改。外观预览已更新，点击“应用”后自动同步。";
    if (id >= 302 && id <= 307) {
        fonts();
        buildPage();
        SetFocus(GetDlgItem(pane_, id));
    }
    notify(notification_);
    InvalidateRect(window_, nullptr, FALSE);
    InvalidateRect(pane_, nullptr, FALSE);
}
void Settings::importDictionary(bool) {
    if (importing_)
        return;
    std::wstring source;
    if (!chooseFile(source, false, L"词库文件\0*.scel;*.txt;*.tsv;*.mswydict\0所有文件\0*.*\0\0"))
        return;
    const auto target = customDictionaryPath(true);
    if (target.empty()) {
        notify(L"无法创建本地词库目录。", true);
        return;
    }
    if (import_.joinable())
        import_.join();
    importing_ = true;
    notify(L"正在校验并导入词库，输入法仍可继续使用…");
    buildPage();
    try {
        import_ = std::thread([this, source, target] {bool ok = false;
        try {
            ok = addDictionaryLibrary(source, target);
        } catch (...) {
            ok = false;
        }
        PostMessageW(window_, kImported, ok ? 1 : 0, 0);
                                                             })
        ;
    } catch (...) {
        importing_ = false;
        notify(L"无法启动词库导入，请稍后重试。", true);
        buildPage();
    }
}
void Settings::finishImport(bool ok) {
    if (import_.joinable())
        import_.join();
    importing_ = false;
    if (closing_ && !updating_) {
        DestroyWindow(window_);
        return;
    }
    notify(ok ? L"词库列表已更新。正在输入的拼音完成后自动使用新词库。" :
           L"导入未完成：格式、容量或保存校验失败；原词库保留。", !ok);
    buildPage();
}
void Settings::checkUpdate(bool download) {
    if (updating_ || (download && !release_.available)) return;
    if (updateWorker_.joinable()) updateWorker_.join();
    if (download) {
        updatePath_ = userFile((L"update-" + release_.version + L"-" + std::to_wstring(GetCurrentProcessId()) + L".exe").c_str(), true);
        if (updatePath_.empty()) { notify(L"无法创建更新文件。", true); return; }
    }
    updating_ = true;
    updateMessage_ = download ? L"正在下载并校验安装包…" : L"正在检查 GitHub Releases…";
    if (page_ == 6) buildPage();
    try {
        const auto release = release_;
        const auto path = updatePath_;
        updateWorker_ = std::thread([this, download, release, path] {
            bool ok = false;
            try {
                if (download) ok = downloadReleaseUpdate(release, path);
                else pendingRelease_ = checkReleaseUpdate();
            } catch (...) { pendingRelease_.message = L"更新检查失败，请稍后重试。"; }
            PostMessageW(window_, download ? kUpdateDownloaded : kUpdateReady, ok ? 1 : 0, 0);
        });
    } catch (...) { updating_ = false; updateMessage_ = L"无法启动更新检查。"; if (page_ == 6) buildPage(); }
}
std::wstring Settings::diagnostics() {
    std::wostringstream text;
    wchar_t executable[32768] {}, registered[32768] {};
    GetModuleFileNameW(nullptr, executable, 32768);
    DWORD size = sizeof(registered);
    LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE,
                                  L"Software\\Classes\\CLSID\\{65C32A54-219A-4F0A-B44C-B963D7BA532F}\\InprocServer32", nullptr, RRF_RT_REG_SZ,
                                  nullptr, registered, &size);
    text << L"澄音 0.1.0-preview11\r\nArchitecture: x64\r\nExecutable: " << executable << L"\r\nTSF server: " <<
         (status == ERROR_SUCCESS ? registered : L"not registered") << L"\r\nDPI: " << dpi_ << L"\r\nFont: " <<
         draft_.font << L" / " << draft_.fontSize << L"\r\nPage size: " << draft_.pageSize << L"\r\nLearning: " <<
         draft_.learning << L"\r\nAssociation: " << draft_.associations << L"\r\nCaret fallback: " <<
         draft_.caretFallback << L"\r\nShift switch: " << draft_.shiftSwitch << L"\r\n";
    auto *profile = loadProfile(userFile(L"learning.profile"));
    text << L"Saved preferences: " << (profile ? myswy_profile_count(profile) : -1) << L"\r\n";
    if (profile)
        myswy_profile_free(profile);
    return text.str();
}
void Settings::copyDiagnostics() {
    auto text = diagnostics();
    if (!OpenClipboard(window_)) {
        notify(L"剪贴板暂不可用，请稍后重试。");
        return;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    bool ok = false;
    if (memory) {
        void *data = GlobalLock(memory);
        if (data) {
            std::copy_n(text.c_str(), text.size() + 1, static_cast<wchar_t *>(data));
            GlobalUnlock(memory);
            EmptyClipboard();
            ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        }
        if (!ok)
            GlobalFree(memory);
    }
    CloseClipboard();
    notify(ok ? L"诊断信息已复制，不含输入正文。" : L"无法复制诊断信息。");
}
void Settings::command(int id, int event) {
    if (id >= 11 && id < 18) {
        page_ = id - 10;
        scroll_ = 0;
        buildPage();
        SendMessageW(GetDlgItem(window_, 10), TCM_SETCURSEL, page_, 0);
        return;
    }
    if ((event == CBN_SELCHANGE && (id == 200 || id == 201 || (id >= 302 && id <= 305))) || (event == BN_CLICKED
            && (id == 202 || id == 203 || id == 204 || id == 306 || id == 307 || id == 501 || id == 601 || id == 608
                || (id>=801 && id<=811) || (id>=820 && id<=823)))) {
        collect(id);
        return;
    }
    if (event != BN_CLICKED)
        return;
    if (id == IDCANCEL) {
        SendMessageW(window_, WM_CLOSE, 0, 0);
        return;
    }
    if (id == IDOK)
        id = kSave;
    if (id == kSave) {
        if (savePreferences(userFile(L"preferences.ini", true), draft_)) {
            dirty_ = false;
            notify(L"设置已应用，已打开的应用会自动更新。");
        } else
            notify(L"无法保存设置，磁盘上的原配置保留。", true);
        return;
    }
    if (id == kDefaults) {
        draft_ = Preferences{};
        dirty_ = true;
        fonts();
        buildPage();
        notify(L"已恢复默认选项；点击“应用”保存。词库与学习数据保留。");
        return;
    }
    if (id == 301) {
        LOGFONTW font{};
        font.lfHeight = -scaled(draft_.fontSize);
        std::wcsncpy(font.lfFaceName, draft_.font.c_str(), LF_FACESIZE - 1);
        CHOOSEFONTW picker{};
        picker.lStructSize = sizeof(picker);
        picker.hwndOwner = window_;
        picker.lpLogFont = &font;
        picker.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_LIMITSIZE | CF_NOSCRIPTSEL | CF_NOSTYLESEL;
        picker.nSizeMin = 9;
        picker.nSizeMax = 24;
        if (ChooseFontW(&picker)) {
            draft_.font = font.lfFaceName;
            draft_.fontSize = std::clamp(MulDiv(picker.iPointSize, 96, 720), 12, 32);
            dirty_ = true;
            fonts();
            buildPage();
            notify(L"字体预览已更新；保存后应用。");
        }
        return;
    }
    if (id == 401) {
        importDictionary(true);
        return;
    }
    if (id == 406 || id == 407) {
        if (importing_) return;
        const int selected = static_cast<int>(SendMessageW(GetDlgItem(pane_, 405), LB_GETCURSEL, 0, 0));
        if (selected < 0 || static_cast<size_t>(selected) >= dictionaries_.size()) return;
        const auto entry = dictionaries_[selected];
        const auto path = customDictionaryPath(false);
        if (id == 407 && MessageBoxW(window_, (L"从词库列表删除“" + entry.name + L"”？原文件和学习数据保留。").c_str(),
                                    L"删除自定义词库", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
        if (import_.joinable()) import_.join();
        importing_ = true;
        buildPage();
        try {
            import_ = std::thread([this, path, entry, id] {
                bool ok = false;
                try { ok = changeDictionaryLibrary(path, entry, id == 407); } catch (...) {}
                PostMessageW(window_, kImported, ok ? 1 : 0, 0);
            });
        } catch (...) { importing_ = false; notify(L"无法启动词库管理。", true); buildPage(); }
        return;
    }
    if (id == 605) {
        // Show the full bundled license even when installed offline.
        MessageBoxW(window_,
            L"MIT License\n\nCopyright (c) 2026 Myswy IM contributors\n\n"
            L"Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the Software), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\n\n"
            L"The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\n\n"
            L"THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.",
            L"澄音开源协议 · MIT", MB_OK);
        return;
    }
    if (id == 606) {
        ShellExecuteW(window_, L"open", kRepository, nullptr, nullptr, SW_SHOWNORMAL); return;
    }
    if (id == 607) {
        MessageBoxW(window_, L"0.1.0-preview11 · 2026-10-05\n\n"
                    L"• 键盘纠错明确标注示例，并用不同拼音展示通用规则。\n"
                    L"• 候选按文字尺寸布局，紧凑间距；可编辑临时拼音时隐藏重复拼音。\n"
                    L"• 删除三个角色主题，保留系统、白、黑及主题设置页。\n"
                    L"• 新增 11 组模糊音与四类键盘纠错，标准拼音按字母加粗修正位置。\n"
                    L"• 设置预览共用实际绘制代码；Windows 亮暗自动切换。\n"
                    L"• 修复传统宿主 Shift 组合键测试阶段的误拦截。\n"
                    L"• 修复设置字体生命周期，改进 DPI、工作区和滚动。\n"
                    L"• 自定义词库列表支持添加、启用、停用和删除。\n"
                    L"• 空格提交原始拼音；数字和鼠标选词；Shift 组合键透传。\n"
                    L"• 关于页包含 MIT、仓库、发行说明和可校验更新。\n\n完整发行历史见项目 GitHub Releases。",
                    L"澄音发行说明", MB_OK);
        if (release_.available && !release_.notes.empty())
            MessageBoxW(window_, release_.notes.c_str(), release_.version.c_str(), MB_OK);
        return;
    }
    if (id == 609 || id == 610) { checkUpdate(id == 610); return; }
    if (id == 502) {
        std::wstring target;
        if (chooseFile(target, true, L"澄音学习备份\0*.chengyinuser;*.myswyuser\0所有文件\0*.*\0\0")) {
            auto *profile = loadProfile(userFile(L"learning.profile"));
            bool ok = profile && saveProfile(target, profile);
            if (profile)
                myswy_profile_free(profile);
            notify(ok ? L"学习数据已备份。" : L"无法备份学习数据；原数据保留。", !ok);
        }
        return;
    }
    if (id == 503) {
        std::wstring source;
        if (chooseFile(source, false, L"澄音学习备份\0*.chengyinuser;*.myswyuser\0所有文件\0*.*\0\0")) {
            bool ok = importProfile(source, userFile(L"learning.profile", true));
            notify(ok ? L"学习备份已导入，会自动同步到已打开的应用。" :
                   L"导入失败：文件损坏或无法保存；原学习数据保留。", !ok);
            buildPage();
        }
        return;
    }
    if (id == 504) {
        if (MessageBoxW(window_,
                        L"清除已保存的选词偏好？建议先使用“备份学习”。此操作不会删除词库。",
                        L"清除学习数据", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
            bool ok = clearProfile(userFile(L"learning.profile"));
            notify(ok ? L"学习数据已清除，已打开的应用会自动更新。" :
                   L"无法清除学习数据，请稍后重试。", !ok);
            buildPage();
        }
        return;
    }
    if (id == 602) {
        page_ = 5;
        scroll_ = 0;
        SendMessageW(GetDlgItem(window_, 10), TCM_SETCURSEL, page_, 0);
        buildPage();
        return;
    }
    if (id == 603) {
        copyDiagnostics();
        return;
    }
    if (id == 604) {
        auto path = userFile(L"preferences.ini", true);
        path = path.substr(0, path.find_last_of(L'\\'));
        ShellExecuteW(window_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
}

void Settings::paint(HWND hwnd, bool) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    FillRect(dc, &rect, background_);
    EndPaint(hwnd, &ps);
}
LRESULT Settings::message(HWND hwnd, UINT message, WPARAM w, LPARAM l, bool pane) {
    switch (message) {
    case WM_CREATE:
        if (!pane) {
            window_ = hwnd;
            dpi_ = windowDpi(hwnd);
            fonts();
            HWND tabs = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | TCS_MULTILINE, 0, 0, 0,
                                      0, hwnd, reinterpret_cast<HMENU>(10), instance_, nullptr);
            SendMessageW(tabs, WM_SETFONT, reinterpret_cast<WPARAM>(body_), FALSE);
            for (int i = 0; i < static_cast<int>(std::size(kNames)); ++i) {
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<wchar_t *>(kNames[i]);
                SendMessageW(tabs, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item));
            }
            SendMessageW(tabs, TCM_SETCURSEL, page_, 0);
            pane_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"Myswy.Settings.Content", L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, instance_,
                                    this);
            CreateWindowW(L"BUTTON", L"应用", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd,
                          reinterpret_cast<HMENU>(100), instance_, nullptr);
            CreateWindowW(L"BUTTON", L"恢复默认值", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
                          hwnd, reinterpret_cast<HMENU>(101), instance_, nullptr);
            CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, hwnd,
                          reinterpret_cast<HMENU>(102), instance_, nullptr);
            CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                          reinterpret_cast<HMENU>(103), instance_, nullptr);
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadIconW(instance_,
                         MAKEINTRESOURCEW(201))));
            layout();
        }
        return 0;
    case WM_NOTIFY:
        if (!pane && reinterpret_cast<NMHDR *>(l)->idFrom == 10
                && reinterpret_cast<NMHDR *>(l)->code == TCN_SELCHANGE) {
            page_ = static_cast<int>(SendMessageW(GetDlgItem(hwnd, 10), TCM_GETCURSEL, 0, 0));
            scroll_ = 0;
            buildPage();
        }
        return 0;
    case WM_GETMINMAXINFO:
        if (!pane) {
            MONITORINFO monitor{sizeof(monitor), {}, {}, 0};
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {
                std::min<LONG>(scaled(620), monitor.rcWork.right - monitor.rcWork.left),
                std::min<LONG>(scaled(420), monitor.rcWork.bottom - monitor.rcWork.top)};
        }
        return 0;
    case WM_SIZE:
        if (!pane && pane_)
            layout();
        return 0;
    case WM_DPICHANGED:
        if (!pane) {
            dpi_ = HIWORD(w);
            fonts();
            auto *rect = reinterpret_cast<const RECT *>(l);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == 103) {
            SendMessageW(window_, WM_CLOSE, 0, 0);
            return 0;
        }
        command(LOWORD(w), HIWORD(w));
        return 0;
    case WM_DRAWITEM:
        if (pane && w == 309) {
            auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(l);
            const auto colors = palette(draft_.theme);
            const int style=visualTheme(draft_.theme);
            drawThemeSurface(item->hDC,item->rcItem,colors,style,dpi_);
            RECT selected = item->rcItem;
            selected.left += scaled(6); selected.right -= scaled(6);
            auto old = SelectObject(item->hDC, sample_);
            TEXTMETRICW metrics{}; GetTextMetricsW(item->hDC, &metrics);
            const int rowHeight = metrics.tmHeight + scaled(6);
            selected.top+=scaled(6); selected.bottom = selected.top + rowHeight;
            SetBkMode(item->hDC, TRANSPARENT);
            for (int i=0;i<3;++i) {
                RECT row=selected; OffsetRect(&row,0,i*rowHeight);
                drawThemeSelection(item->hDC,row,colors,style,dpi_,i==0,false);
                RECT number=row; number.left+=scaled(5); number.right=number.left+scaled(18);
                drawThemeBadge(item->hDC,number,colors,style,dpi_,i==0);
                SelectObject(item->hDC,body_); SetTextColor(item->hDC,i==0 ? colors.selectedText : colors.muted);
                wchar_t digit[2]{static_cast<wchar_t>(L'1'+i),0};
                DrawTextW(item->hDC,digit,1,&number,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
                row.left+=scaled(30); SelectObject(item->hDC,sample_);
                SetTextColor(item->hDC,i==0 ? colors.selectedText : colors.text);
                DrawTextW(item->hDC,i==0 ? L"你好" : i==1 ? L"拟好" : L"你号",-1,&row,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
            }
            SelectObject(item->hDC, old);
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, colors_.text);
        SetBkColor(dc, colors_.background);
        return reinterpret_cast<LRESULT>(background_);
    }
    case WM_ERASEBKGND: {
        // Native group boxes ask their parent to paint the background. Fill
        // their interiors as well, including areas formerly occupied by controls.
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(w), &rect, background_);
        return 1;
    }
    case WM_PAINT:
        paint(hwnd, pane);
        return 0;
    case WM_MOUSEWHEEL:
        scroll(scroll_ - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 48);
        return 0;
    case WM_VSCROLL:
        if (pane) {
            SCROLLINFO info{sizeof(info), SIF_ALL, 0, 0, 0, 0, 0};
            GetScrollInfo(pane_, SB_VERT, &info);
            int at = scroll_;
            switch (LOWORD(w)) {
            case SB_LINEUP:
                at -= 24;
                break;
            case SB_LINEDOWN:
                at += 24;
                break;
            case SB_PAGEUP:
                at -= static_cast<int>(info.nPage);
                break;
            case SB_PAGEDOWN:
                at += static_cast<int>(info.nPage);
                break;
            case SB_TOP:
                at = 0;
                break;
            case SB_BOTTOM:
                at = info.nMax;
                break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                at = info.nTrackPos;
                break;
            default:
                break;
            }
            scroll(at);
        }
        return 0;
    case WM_HSCROLL:
        if (pane) {
            SCROLLINFO info{sizeof(info), SIF_ALL, 0,0,0,0,0};
            GetScrollInfo(pane_, SB_HORZ, &info);
            int at = horizontal_;
            switch (LOWORD(w)) {
            case SB_LINELEFT: at -= 24; break; case SB_LINERIGHT: at += 24; break;
            case SB_PAGELEFT: at -= info.nPage; break; case SB_PAGERIGHT: at += info.nPage; break;
            case SB_LEFT: at = 0; break; case SB_RIGHT: at = info.nMax; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: at = info.nTrackPos; break;
            default: break;
            }
            horizontal_ = std::clamp(at, 0, std::max(0, info.nMax + 1 - static_cast<int>(info.nPage)));
            SetScrollPos(pane_, SB_HORZ, horizontal_, TRUE);
            scroll(scroll_);
        }
        return 0;
    case kImported:
        finishImport(w != 0);
        return 0;
    case kUpdateReady:
    case kUpdateDownloaded:
        if (updateWorker_.joinable()) updateWorker_.join();
        updating_ = false;
        if (message == kUpdateReady) {
            release_ = std::move(pendingRelease_);
            updateMessage_ = release_.message;
        } else {
            updateMessage_ = w ? L"安装包校验成功，正在启动更新安装器。" : L"下载或 SHA-256 校验失败，未运行安装包。请稍后重试。";
            if (w && reinterpret_cast<INT_PTR>(ShellExecuteW(window_, L"open", updatePath_.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                updateMessage_ = L"安装包已校验，但启动失败。请稍后重试。";
        }
        if (closing_ && !importing_) DestroyWindow(window_);
        else if (page_ == 6) buildPage();
        return 0;
    case WM_CLOSE:
        if (!pane) {
            if (importing_ || updating_) {
                closing_ = true;
                notify(L"正在完成后台操作，完成后关闭。");
                return 0;
            }
            if (dirty_) {
                int answer = MessageBoxW(window_, L"应用尚未保存的设置？", L"澄音设置",
                                         MB_YESNOCANCEL | MB_ICONQUESTION);
                if (answer == IDCANCEL)
                    return 0;
                if (answer == IDYES && !savePreferences(userFile(L"preferences.ini", true), draft_)) {
                    notify(L"保存失败，原设置保留。", true);
                    return 0;
                }
            }
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        if (!pane)
            PostQuitMessage(0);
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        if (!pane) {
            fonts();
            layout();
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
LRESULT CALLBACK settingsProcedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(l);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto *settings = reinterpret_cast<Settings *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    wchar_t name[64] {};
    GetClassNameW(hwnd, name, 64);
    return settings ? settings->message(hwnd, message, w, l, !std::wcscmp(name,
                                        L"Myswy.Settings.Content")) : DefWindowProcW(hwnd, message, w, l);
}
}
int runSettings(HINSTANCE instance, int show, ITfMessagePump *pump, ITfKeystrokeMgr *keys, int initialPage, bool automaticUpdates) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    for (const wchar_t *name : {
                L"Myswy.Settings", L"Myswy.Settings.Content"
            }) {
        WNDCLASSW cls{};
        cls.hInstance = instance;
        cls.lpfnWndProc = settingsProcedure;
        cls.lpszClassName = name;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return 1;
    }
    Settings settings(instance, std::clamp(initialPage, 0, 7));
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    HWND window = CreateWindowExW(0, L"Myswy.Settings", L"澄音输入法设置",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, std::min(860,
                                          static_cast<int>(work.right - work.left) - 24), std::min(760, static_cast<int>(work.bottom - work.top) - 24),
                                  nullptr, nullptr, instance, &settings);
    if (!window)
        return 1;
    UINT dpi = windowDpi(window);
    SetWindowPos(window, nullptr, 0, 0, std::min(MulDiv(860, static_cast<int>(dpi), 96),
                 static_cast<int>(work.right - work.left) - 24), std::min(MulDiv(760, static_cast<int>(dpi), 96),
                         static_cast<int>(work.bottom - work.top) - 24), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(window, show);
    // Select a real child after the window is shown. An implicit first-key
    // focus change can otherwise leave TSF attached to its floating context.
    HWND pane = FindWindowExW(window, nullptr, L"Myswy.Settings.Content", nullptr);
    SetFocus(initialPage == 5 ? GetDlgItem(pane, 701) : GetDlgItem(window, 10));
    if (automaticUpdates && loadPreferences(userFile(L"preferences.ini")).autoUpdate)
        PostMessageW(window, WM_COMMAND, MAKEWPARAM(609, BN_CLICKED), 0);
    MSG message{};
    for (;;) {
        BOOL received = FALSE;
        HRESULT hr = pump ? pump->GetMessageW(&message, nullptr, 0, 0, &received) : E_FAIL;
        if (FAILED(hr))
            received = GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0)
            break;
        BOOL tested = FALSE, eaten = FALSE;
        if (keys) {
            bool down = message.message == WM_KEYDOWN
                        || message.message == WM_SYSKEYDOWN, up = message.message == WM_KEYUP || message.message == WM_SYSKEYUP;
            if (down && SUCCEEDED(keys->TestKeyDown(message.wParam, message.lParam, &tested)) && tested)
                keys->KeyDown(message.wParam, message.lParam, &eaten);
            else if (up && SUCCEEDED(keys->TestKeyUp(message.wParam, message.lParam, &tested)) && tested)
                keys->KeyUp(message.wParam, message.lParam, &eaten);
        }
        if (eaten)
            continue;
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return 0;
}
}
