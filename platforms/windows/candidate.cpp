#include "candidate.h"
#include <algorithm>
#include <cwchar>
namespace myswy {
namespace {
constexpr wchar_t kWindowClass[] = L"Myswy.Candidates.Preview1";
void fillSelection(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
}
bool readText(MyswySession *session, uint32_t field, size_t index, WideText &out) {
    uint8_t bytes[MYSWY_MAX_TEXT_BYTES + 65] {};
    const int size = myswy_session_text(session, field, index, bytes, sizeof(bytes));
    out = {};
    if (size <= 0 || size > static_cast<int>(sizeof(bytes)))
        return false;
    if (size == 1)
        return true;
    out.length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(bytes),
                                     size - 1, out.data, static_cast<int>(std::size(out.data) - 1));
    return out.length > 0;
}
CandidateWindow::~CandidateWindow() {
    if (hwnd_)
        DestroyWindow(hwnd_);
    if (buffer_) {
        if (originalBitmap_)
            SelectObject(buffer_, originalBitmap_);
        if (bitmap_)
            DeleteObject(bitmap_);
        DeleteDC(buffer_);
    }
    if (font_)
        DeleteObject(font_);
    if (smallFont_)
        DeleteObject(smallFont_);
    UnregisterClassW(kWindowClass, module);
}
void CandidateWindow::hide() {
    pressed_ = -1;
    hover_ = -1;
    choice_ = nullptr;
    target_ = nullptr;
    if (hwnd_) {
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        ShowWindow(hwnd_, SW_HIDE);
    }
}
void CandidateWindow::show(MyswySession *session, HWND owner, RECT caret, bool limited, void *target,
                           Choice choice, uint64_t generation, const Preferences &preferences) {
    target_ = target;
    choice_ = choice;
    generation_ = generation;
    preferences_ = preferences;
    if (theme_ != preferences.theme) {
        theme_ = preferences.theme;
        colors_ = palette(theme_);
    }
    if (!hwnd_) {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.hInstance = module;
        cls.lpfnWndProc = procedure;
        cls.lpszClassName = kWindowClass;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.style = CS_DROPSHADOW;
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, kWindowClass, L"澄音 候选",
                                WS_POPUP, 0, 0, 0, 0, owner, nullptr, module, this);
        if (!hwnd_)
            return;

    }
    const bool visible = IsWindowVisible(hwnd_) != FALSE;
    const bool sameOwner = reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT)) == owner;
    if (!sameOwner)
        SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    const UINT dpi = windowDpi(owner);
    auto scale = [dpi](int n) {
        return MulDiv(n, static_cast<int>(dpi), 96);
    };
    padding_ = scale(12);
    headerHeight_ = scale(32);
    footerHeight_ = scale(26);
    rowHeight_ = scale(std::max(preferences.fontSize + 12,
                                preferences.density ? 36 : 28) + (preferences.candidatePinyin ? 18 : 0));
    if (fontDpi_ != dpi || fontSize_ != preferences.fontSize || fontFace_ != preferences.font) {
        HFONT font = createUIFont(preferences.fontSize, dpi, preferences.font);
        HFONT nextSmallFont = createUIFont(13, dpi, preferences.font);
        if (font) {
            if (font_)
                DeleteObject(font_);
            font_ = font;
        }
        if (nextSmallFont) {
            if (smallFont_)
                DeleteObject(smallFont_);
            smallFont_ = nextSmallFont;
        }
        fontDpi_ = dpi;
        fontSize_ = preferences.fontSize;
        fontFace_ = preferences.font;
    }
    readText(session, preferences.separators ? MYSWY_TEXT_DISPLAY_PREEDIT : MYSWY_TEXT_PREEDIT, 0, rows_[0]);
    association_ = myswy_session_is_association(session) > 0;
    if (association_) {
        constexpr wchar_t label[] = L"联想 · Tab / 鼠标确认";
        std::copy_n(label, std::size(label), rows_[0].data);
        rows_[0].length = static_cast<int>(std::wcslen(label));
    }
    if (limited && rows_[0].length + 12 < static_cast<int>(std::size(rows_[0].data))) {
        constexpr wchar_t label[] = L" · 请分段输入";
        std::copy_n(label, std::size(label), rows_[0].data + rows_[0].length);
        rows_[0].length = static_cast<int>(std::wcslen(rows_[0].data));
    }
    count_ = std::clamp(myswy_session_candidate_count(session), 0, 9);
    selected_ = myswy_session_selected(session);
    hover_ = -1;
    const int page = myswy_session_page(session);
    previous_ = page > 0;
    next_ = myswy_session_has_next_page(session) > 0;
    footer_.length = std::swprintf(footer_.data, std::size(footer_.data), L"%ls     第 %d 页     %ls",
                                   previous_ ? L"‹ PgUp" : L"", page + 1, next_ ? L"PgDn ›" : L"");
    for (int i = 0; i < count_; ++i) {
        readText(session, MYSWY_TEXT_CANDIDATE, static_cast<size_t>(i), rows_[i + 1]);
        readText(session, MYSWY_TEXT_CANDIDATE_PINYIN, static_cast<size_t>(i), pinyin_[i]);
    }
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST), &monitor)) {
        hide();
        return;
    }
    const RECT work = monitor.rcWork;
    const int available = static_cast<int>(work.right - work.left);
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    int widths[9] {};
    for (int i = 0; i < count_; ++i) {
        SIZE extent{};
        GetTextExtentPoint32W(dc, rows_[i + 1].data, rows_[i + 1].length, &extent);
        widths[i] = static_cast<int>(extent.cx) + scale(52);
        if (preferences.candidatePinyin) {
            SelectObject(dc, smallFont_ ? smallFont_ : GetStockObject(DEFAULT_GUI_FONT));
            GetTextExtentPoint32W(dc, pinyin_[i].data, pinyin_[i].length, &extent);
            widths[i] = std::max(widths[i], static_cast<int>(extent.cx) + scale(52));
            SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
        }
    }
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    int width = scale(240), height = 0;
    if (preferences.layout == 0) {
        for (int i = 0; i < count_; ++i)
            width = std::max(width, widths[i] + padding_ * 2);
        width = std::min(width, std::min(scale(520), available));
        RECT before{};
        if (visible && sameOwner && GetWindowRect(hwnd_, &before))
            width = std::max(width, std::min(static_cast<int>(before.right - before.left), available));
        for (int i = 0; i < count_; ++i)
            items_[i] = {padding_, padding_ + headerHeight_ + i * rowHeight_, width - padding_, padding_ + headerHeight_ + (i + 1) *rowHeight_};
        height = padding_ * 2 + headerHeight_ + count_ * rowHeight_ + footerHeight_;
    } else {
        const int limit = std::min(scale(720), available);
        int x = padding_, y = padding_ + headerHeight_;
        for (int i = 0; i < count_; ++i) {
            int itemWidth = std::min(std::max(scale(70), widths[i]), limit - padding_ * 2);
            if (x > padding_ && x + itemWidth > limit - padding_) {
                x = padding_;
                y += rowHeight_;
            }
            items_[i] = {x, y, x + itemWidth, y + rowHeight_};
            x += itemWidth;
            width = std::max(width, x + padding_);
        }
        height = y + (count_ ? rowHeight_ : 0) + padding_ + footerHeight_;
    }
    width = std::min(width, available);
    height = std::min(height, static_cast<int>(work.bottom - work.top));
    footerRect_ = {padding_, height - padding_ - footerHeight_, width - padding_, height - padding_};
    const int x = std::clamp(static_cast<int>(caret.left), static_cast<int>(work.left),
                             static_cast<int>(work.right) - width);
    int y = static_cast<int>(caret.bottom) + scale(4);
    if (y + height > work.bottom)
        y = static_cast<int>(caret.top) - height - scale(4);
    y = std::clamp(y, static_cast<int>(work.top), static_cast<int>(work.bottom) - height);
    RECT before{};
    GetWindowRect(hwnd_, &before);
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW;
    if (before.left == x && before.top == y)
        flags |= SWP_NOMOVE;
    if (before.right - before.left == width && before.bottom - before.top == height)
        flags |= SWP_NOSIZE;
    if (!visible)
        flags |= SWP_SHOWWINDOW;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, flags);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
int CandidateWindow::hit(LPARAM location) const {
    POINT point{static_cast<short>(LOWORD(location)), static_cast<short>(HIWORD(location))};
    if (!hwnd_ || !IsWindowVisible(hwnd_))
        return -1;
    for (int i = 0; i < count_; ++i)
        if (PtInRect(&items_[i], point))
            return i;
    if (PtInRect(&footerRect_, point))
        return point.x < (footerRect_.left + footerRect_.right) / 2 ? (previous_ ? 9 : -1) : (next_ ? 10 : -1);
    return -1;
}
LRESULT CALLBACK CandidateWindow::procedure(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE) {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(l);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto *self = reinterpret_cast<CandidateWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCDESTROY && self) {
        self->hwnd_ = nullptr;
        self->pressed_ = -1;
        self->choice_ = nullptr;
        self->target_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    if (msg == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    if (msg == WM_NCHITTEST)
        return HTCLIENT;
    if (msg == WM_ERASEBKGND)
        return 1;
    if ((msg == WM_THEMECHANGED || msg == WM_SETTINGCHANGE) && self) {
        self->colors_ = palette(self->preferences_.theme);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_MOUSEMOVE && self) {
        int hit = self->hit(l);
        if (hit != self->hover_) {
            self->hover_ = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    if (msg == WM_MOUSELEAVE && self) {
        self->hover_ = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN && self && self->choice_) {
        self->pressed_ = self->hit(l);
        self->pressedGeneration_ = self->generation_;
        if (self->pressed_ >= 0)
            SetCapture(hwnd);
        return 0;
    }
    if (msg == WM_LBUTTONUP && self) {
        const int index = self->pressed_;
        const bool valid = index >= 0 && index == self->hit(l) && self->pressedGeneration_ == self->generation_;
        auto choice = self->choice_;
        void *target = self->target_;
        const auto generation = self->generation_;
        self->pressed_ = -1;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        if (valid && choice)
            choice(target, generation, index);
        return 0;
    }
    if (msg == WM_CAPTURECHANGED && self)
        self->pressed_ = -1;
    if (msg == WM_PAINT && self) {
        self->hwnd_ = hwnd;
        self->paint();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
void CandidateWindow::paint() {
    PAINTSTRUCT ps{};
    HDC screen = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = static_cast<int>(client.right), height = static_cast<int>(client.bottom);
    if (!buffer_)
        buffer_ = CreateCompatibleDC(screen);
    if (buffer_ && (bufferWidth_ != width || bufferHeight_ != height)) {
        HBITMAP next = CreateCompatibleBitmap(screen, std::max(width, 1), std::max(height, 1));
        if (next) {
            HGDIOBJ old = SelectObject(buffer_, next);
            if (!originalBitmap_)
                originalBitmap_ = old;
            if (bitmap_)
                DeleteObject(bitmap_);
            bitmap_ = next;
            bufferWidth_ = width;
            bufferHeight_ = height;
        }
    }
    HDC dc = buffer_ && bitmap_ && bufferWidth_ == width && bufferHeight_ == height ? buffer_ : screen;
    const auto colors = colors_;
    HBRUSH surface = CreateSolidBrush(colors.surface);
    FillRect(dc, &client, surface);
    DeleteObject(surface);
    HPEN pen = CreatePen(PS_SOLID, 1, colors.border);
    HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, width, height);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, smallFont_ ? smallFont_ : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, colors.muted);
    RECT header{padding_ + 4, padding_, width - padding_, padding_ + headerHeight_};
    DrawTextW(dc, rows_[0].data, rows_[0].length, &header,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    for (int i = 0; i < count_; ++i) {
        const bool selected = i == selected_, hover = i == hover_;
        RECT row = items_[i];
        row.top += 2;
        row.bottom -= 2;
        if (selected || hover)
            fillSelection(dc, row, selected ? colors.selected : colors.background);
        RECT number = row;
        number.left += 8;
        number.right = number.left + padding_ * 2;
        wchar_t label[4] {association_ ? (selected ? L'›' : L' ') : static_cast<wchar_t>(L'1' + i), 0};
        SelectObject(dc, smallFont_ ? smallFont_ : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(dc, selected ? colors.selectedText : colors.muted);
        DrawTextW(dc, label, 1, &number, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        RECT text = row;
        text.left += padding_ * 2 + 8;
        text.right -= 8;
        if (preferences_.candidatePinyin && pinyin_[i].length) {
            text.bottom -= MulDiv(18, static_cast<int>(fontDpi_), 96);
        }
        SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(dc, selected ? colors.selectedText : colors.text);
        DrawTextW(dc, rows_[i + 1].data, rows_[i + 1].length, &text,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (preferences_.candidatePinyin && pinyin_[i].length) {
            RECT pronunciation = text;
            pronunciation.top = text.bottom - 1;
            pronunciation.bottom = row.bottom;
            SelectObject(dc, smallFont_ ? smallFont_ : GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(dc, selected ? colors.selectedText : colors.muted);
            DrawTextW(dc, pinyin_[i].data, pinyin_[i].length, &pronunciation,
                      DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }
    SelectObject(dc, smallFont_ ? smallFont_ : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, colors.muted);
    DrawTextW(dc, footer_.data, footer_.length, &footerRect_,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    if (dc != screen)
        BitBlt(screen, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    EndPaint(hwnd_, &ps);
}
}
