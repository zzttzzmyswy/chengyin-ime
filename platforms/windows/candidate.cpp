#include "candidate.h"
#include "theme_art.h"
#include <algorithm>
#include <cwchar>
namespace myswy {
namespace {
constexpr wchar_t kWindowClass[] = L"Myswy.Candidates.Preview1";
struct DpiContext {
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiContext() { if (previous) SetThreadDpiAwarenessContext(previous); }
};
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
PlacementResult placeCandidates(const PlacementInput &input, PlacementState &state) {
    const int below = static_cast<int>(input.caret.bottom) + input.gap;
    const int belowRoom = static_cast<int>(input.work.bottom) - below;
    const bool fitsBelow = belowRoom >= input.height;
    // The side sticks while the caret top stays put, i.e. within one line of one
    // composition. A caret-height wobble must not read as a move, hence the slack.
    const int shift = static_cast<int>(input.caret.top) - static_cast<int>(state.caretTop);
    const bool sticky = state.valid && shift <= input.slack && -shift <= input.slack;
    Placement side = state.side;
    if (!sticky) {
        side = fitsBelow ? Placement::below : Placement::above;
    } else if (side == Placement::above) {
        // Returning below takes more room than flipping up did: a popup that
        // wobbles by one candidate row at the work-area edge would otherwise
        // bounce on every keystroke. One row of band absorbs exactly that.
        side = belowRoom >= input.height + input.band ? Placement::below : Placement::above;
    } else {
        side = fitsBelow ? Placement::below : Placement::above;
    }
    PlacementResult result;
    result.side = side;
    // Above placement only depends on the caret top, never on the result of the
    // below placement, so a caret-height wobble cannot move the popup.
    result.top = side == Placement::below ? below
                                          : static_cast<int>(input.caret.top) - input.height - input.gap;
    const int leftmost = static_cast<int>(input.work.left);
    const int rightmost = std::max(leftmost, static_cast<int>(input.work.right) - input.width);
    result.left = std::clamp(static_cast<int>(input.caret.left), leftmost, rightmost);
    const int topmost = static_cast<int>(input.work.top);
    const int bottommost = std::max(topmost, static_cast<int>(input.work.bottom) - input.height);
    result.top = std::clamp(result.top, topmost, bottommost);
    state.side = side;
    state.caretTop = input.caret.top;
    state.valid = true;
    return result;
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
    if (boldPinyinFont_) DeleteObject(boldPinyinFont_);
    if (footerFont_)
        DeleteObject(footerFont_);
    UnregisterClassW(kWindowClass, module);
}
void CandidateWindow::refreshPreferences(MyswySession *session, const Preferences &preferences) {
    if (!session || !hwnd_ || !IsWindowVisible(hwnd_))
        return;
    HWND owner = reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT));
    if (!owner || !IsWindow(owner))
        return;
    show(session, owner, requestedCaret_, limited_, target_, choice_, generation_, preferences, inlineEditable_);
}
void CandidateWindow::hide() {
    pressed_ = -1;
    hover_ = -1;
    choice_ = nullptr;
    target_ = nullptr;
    // A new composition starts without a remembered side.
    placement_ = {};
    placementMonitor_ = nullptr;
    if (hwnd_) {
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        ShowWindow(hwnd_, SW_HIDE);
    }
}
void CandidateWindow::show(MyswySession *session, HWND owner, RECT caret, bool limited, void *target,
                           Choice choice, uint64_t generation, const Preferences &preferences, bool inlineEditable) {
    requestedCaret_ = caret;
    limited_ = limited;
    // Convert host-virtualized caret coordinates before switching this thread.
    if (owner && !AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(owner), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        POINT start{caret.left, caret.top}, end{caret.right, caret.bottom};
        LogicalToPhysicalPointForPerMonitorDPI(owner, &start);
        LogicalToPhysicalPointForPerMonitorDPI(owner, &end);
        caret = {start.x, start.y, end.x, end.y};
    }
    DpiContext context;
    if (generation_ != generation)
        scrollOffset_ = 0;
    target_ = target;
    choice_ = choice;
    generation_ = generation;
    preferences_ = preferences;
    inlineEditable_ = inlineEditable;
    showPinyin_ = preferences.candidatePinyin && !inlineEditable;
    {
        theme_ = preferences.theme;
        colors_ = palette(preferences);
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
        // A TIP also runs inside DPI-unaware hosts. Give this popup its own
        // per-monitor context without changing the host's process awareness.
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, kWindowClass, L"澄音 候选",
                                WS_POPUP, caret.left, caret.bottom, 1, 1, owner, nullptr, module, this);
        if (!hwnd_) {
            return;
        }

    }
    const bool visible = IsWindowVisible(hwnd_) != FALSE;
    const bool sameOwner = reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT)) == owner;
    if (!sameOwner)
        SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    // The popup must be created on the caret's monitor so Windows scales it for
    // that monitor's DPI. A visible popup is never moved to the caret here:
    // moving it before the size is known composites it once below the caret and
    // once at its final place, which is the flicker this avoids.
    const HMONITOR caretMonitor = MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST);
    // Hidden for the DPI probe: never yet shown, or hidden for one cross-monitor
    // move. The final placement shows it again at the caret.
    bool needsShow = !visible;
    if (caretMonitor && (!visible || placementMonitor_ != caretMonitor)) {
        MONITORINFO target{};
        target.cbSize = sizeof(target);
        if (GetMonitorInfoW(caretMonitor, &target)) {
            // A cross-monitor DPI change needs the popup on the target monitor
            // before querying it. Hide it for that one move so no frame lands on
            // the old monitor; either way the window is shown only once below.
            if (visible)
                ShowWindow(hwnd_, SW_HIDE);
            needsShow = true;
            // Anywhere inside the target monitor works for the DPI probe; the
            // work-area corner stays off the caret so no frame lands beside it.
            SetWindowPos(hwnd_, nullptr, target.rcWork.left, target.rcWork.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            placementMonitor_ = caretMonitor;
        }
    }
    const UINT dpi = windowDpi(hwnd_);
    auto scale = [dpi](int n) {
        return MulDiv(n, static_cast<int>(dpi), 96);
    };
    padding_ = scale(preferences.density ? 7 : 3);
    const Skin *skin=activeSkin(preferences);
    if(skin && preferences.density) padding_=scale(skin->padding);
    if (fontDpi_ != dpi || fontSize_ != preferences.fontSize || fontFace_ != preferences.font) {
        HFONT font = createUIFont(preferences.fontSize, dpi, preferences.font);
        HFONT nextSmallFont = createUIFont(std::max(16, preferences.fontSize - 2), dpi, preferences.font);
        HFONT nextBoldFont = createUIFont(std::max(16, preferences.fontSize - 2), dpi, preferences.font, FW_BOLD);
        HFONT nextFooterFont = createUIFont(12, dpi, preferences.font);
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
        if (nextFooterFont) {
            if (footerFont_)
                DeleteObject(footerFont_);
            footerFont_ = nextFooterFont;
        }
        if (nextBoldFont) {if (boldPinyinFont_) DeleteObject(boldPinyinFont_); boldPinyinFont_=nextBoldFont;}
        fontDpi_ = dpi;
        fontSize_ = preferences.fontSize;
        fontFace_ = preferences.font;
    }
    readText(session, preferences.separators ? MYSWY_TEXT_DISPLAY_PREEDIT : MYSWY_TEXT_PREEDIT, 0, rows_[0]);
    if (!preferences.candidatePinyin) {
        rows_[0].data[0] = L'\0';
        rows_[0].length = 0;
    }
    association_ = myswy_session_is_association(session) > 0;
    if (association_) {
        constexpr wchar_t label[] = L"联想 · Tab / 鼠标确认";
        std::copy_n(label, std::size(label), rows_[0].data);
        rows_[0].length = static_cast<int>(std::wcslen(label));
    }
    if (limited && rows_[0].length + 12 < static_cast<int>(std::size(rows_[0].data))) {
        const wchar_t *label = rows_[0].length ? L" · 请分段输入" : L"请分段输入";
        std::copy_n(label, std::wcslen(label) + 1, rows_[0].data + rows_[0].length);
        rows_[0].length = static_cast<int>(std::wcslen(rows_[0].data));
    }
    count_ = std::clamp(myswy_session_candidate_count(session), 0, 9);
    selected_ = myswy_session_selected(session);
    hover_ = -1;
    const int page = myswy_session_page(session);
    previous_ = page > 0;
    next_ = myswy_session_has_next_page(session) > 0;
    footer_.length = std::swprintf(footer_.data, std::size(footer_.data), L"%ls  %d  %ls",
                                   previous_ ? L"‹" : L"", page + 1, next_ ? L"›" : L"");
    for (int i = 0; i < count_; ++i) {
        readText(session, MYSWY_TEXT_CANDIDATE, static_cast<size_t>(i), rows_[i + 1]);
        readText(session, MYSWY_TEXT_CANDIDATE_PINYIN, static_cast<size_t>(i), pinyin_[i]);
        std::fill_n(marks_[i],256,uint8_t{0});
        myswy_session_candidate_marks(session,static_cast<size_t>(i),marks_[i],256);
        if (preferences.candidatePinyin && std::any_of(marks_[i],marks_[i]+256,[](uint8_t mark){return mark!=0;})) showPinyin_=true;
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
    TEXTMETRICW candidateMetric{}, pinyinMetric{}, footerMetric{};
    GetTextMetricsW(dc, &candidateMetric);
    SelectObject(dc, smallFont_);
    GetTextMetricsW(dc, &pinyinMetric);
    SelectObject(dc, footerFont_);
    GetTextMetricsW(dc, &footerMetric);
    SIZE number{};
    GetTextExtentPoint32W(dc, L"9", 1, &number);
    numberWidth_ = number.cx + scale(preferences.density ? 9 : 5);
    const int gap = scale(preferences.density ? 12 : 8);
    rowHeight_ = static_cast<int>(candidateMetric.tmHeight) + scale(preferences.density ? 10 : 2);
    if (showPinyin_ && preferences.layout)
        rowHeight_ += pinyinMetric.tmHeight + scale(2);
    else if (showPinyin_)
        rowHeight_ = std::max(rowHeight_, static_cast<int>(pinyinMetric.tmHeight) + scale(2));
    headerHeight_ = (rows_[0].length && (!inlineEditable || association_ || limited) ? static_cast<int>(pinyinMetric.tmHeight) + scale(4) : 0);
    footerHeight_ = previous_ || next_ ? static_cast<int>(footerMetric.tmHeight) + scale(4) : 0;
    int widths[9]{}, textWidths[9]{}, pinyinWidths[9]{};
    int textColumn = 0, pinyinColumn = 0;
    for (int i = 0; i < count_; ++i) {
        SelectObject(dc, font_);
        SIZE extent{};
        GetTextExtentPoint32W(dc, rows_[i + 1].data, rows_[i + 1].length, &extent);
        textWidths[i] = extent.cx;
        textColumn = std::max(textColumn, textWidths[i]);
        if (showPinyin_) {
            for (int at=0;at<pinyin_[i].length;) {
                const bool marked=marks_[i][at]!=0; int end=at+1;
                while (end<pinyin_[i].length && (marks_[i][end]!=0)==marked) ++end;
                SelectObject(dc,marked && boldPinyinFont_ ? boldPinyinFont_ : smallFont_);
                GetTextExtentPoint32W(dc,pinyin_[i].data+at,end-at,&extent);
                pinyinWidths[i]+=extent.cx; at=end;
            }
            pinyinColumn = std::max(pinyinColumn, pinyinWidths[i]);
        }
        widths[i] = numberWidth_ + std::max(textWidths[i], pinyinWidths[i]) + padding_ * 2;
    }
    SelectObject(dc, smallFont_);
    SIZE headerExtent{};
    if (headerHeight_)
        GetTextExtentPoint32W(dc, rows_[0].data, rows_[0].length, &headerExtent);
    SelectObject(dc, footerFont_);
    SIZE footerExtent{};
    if (footerHeight_)
        GetTextExtentPoint32W(dc, footer_.data, footer_.length, &footerExtent);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    int width = std::max(scale(32), std::max(static_cast<int>(headerExtent.cx), static_cast<int>(footerExtent.cx)) + padding_ * 2), height = 0;
    if (preferences.layout == 0) {
        width = std::max(width, numberWidth_ + textColumn + (showPinyin_ ? gap + pinyinColumn : 0) + padding_ * 4);
        width = std::min(width, available);
        for (int i = 0; i < count_; ++i)
            items_[i] = {padding_, padding_ + headerHeight_ + i * rowHeight_, width - padding_, padding_ + headerHeight_ + (i + 1) *rowHeight_};
        height = padding_ * 2 + headerHeight_ + count_ * rowHeight_ + footerHeight_;
    } else {
        const int limit = std::min(scale(720), available);
        int x = padding_, y = padding_ + headerHeight_;
        for (int i = 0; i < count_; ++i) {
            int itemWidth = std::min(widths[i], limit - padding_ * 2);
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
    const int naturalHeight = height;
    rail_=skinRail(preferences,dpi);
    // Decoration yields to actual text on narrow displays; never reduce font size.
    if(width+rail_>available) rail_=0;
    width+=rail_;
    if(rail_ && skin && skin->image) height=std::max(height,scale(preferences.layout ? 64 : 90));
    width = std::min(width, available);
    height = std::min(height, static_cast<int>(work.bottom - work.top));
    if(height>naturalHeight && !headerHeight_)
        for(int i=0;i<count_;++i) OffsetRect(&items_[i],0,(height-naturalHeight)/2);
    scrollMaximum_ = std::max(0,naturalHeight - height);
    scrollOffset_ = std::clamp(scrollOffset_, 0, scrollMaximum_);
    contentRect_ = {padding_, padding_ + headerHeight_, width - padding_ - rail_, height - padding_ - footerHeight_};
    // Keep the keyboard selection visible when work-area height is restricted.
    if (selected_ >= 0 && selected_ < count_) {
        if (items_[selected_].bottom - scrollOffset_ > contentRect_.bottom)
            scrollOffset_ = std::min(scrollMaximum_, static_cast<int>(items_[selected_].bottom - contentRect_.bottom));
        if (items_[selected_].top - scrollOffset_ < contentRect_.top)
            scrollOffset_ = std::max(0, static_cast<int>(items_[selected_].top - contentRect_.top));
    }
    for (int i = 0; i < count_; ++i) {
        textRects_[i] = items_[i];
        textRects_[i].left += numberWidth_ + padding_;
        textRects_[i].right -= padding_;
        pinyinRects_[i] = textRects_[i];
        if (showPinyin_ && preferences.layout == 0) {
            const int start = textRects_[i].left + textColumn + gap;
            textRects_[i].right = std::min(textRects_[i].right, static_cast<LONG>(start - gap));
            pinyinRects_[i].left = start;
        } else if (showPinyin_) {
            textRects_[i].bottom = textRects_[i].top + candidateMetric.tmHeight + scale(2);
            pinyinRects_[i].top = textRects_[i].bottom;
        }
    }
    footerRect_ = {padding_, height - padding_ - footerHeight_, width - padding_ - rail_, height - padding_};
    // One window-free decision for the side (with hysteresis) and the coordinates,
    // so the size correction above cannot flip the popup on its own.
    PlacementInput placement;
    placement.caret = caret;
    placement.work = work;
    placement.width = width;
    placement.height = height;
    placement.gap = scale(4);
    // A caret-height wobble must not count as a move; the row height it can
    // produce is a few pixels, so the threshold only has to absorb that.
    placement.slack = std::max(1, scale(2));
    // One row of band absorbs a single-line candidate-count change at the edge.
    placement.band = rowHeight_;
    const PlacementResult placed = placeCandidates(placement, placement_);
    const int x = placed.left, y = placed.top;
    RECT before{};
    GetWindowRect(hwnd_, &before);
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW;
    if (before.left == x && before.top == y)
        flags |= SWP_NOMOVE;
    if (before.right - before.left == width && before.bottom - before.top == height)
        flags |= SWP_NOSIZE;
    if (needsShow)
        flags |= SWP_SHOWWINDOW;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, flags);
    const int radius = skin ? scale(skin->radius) : themeRadius(visualTheme(preferences.theme), dpi);
    if (shapeWidth_ != width || shapeHeight_ != height || shapeRadius_ != radius) {
        HRGN shape = radius ? CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2) : nullptr;
        if (!SetWindowRgn(hwnd_, shape, FALSE) && shape) DeleteObject(shape);
        shapeWidth_ = width; shapeHeight_ = height; shapeRadius_ = radius;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
int CandidateWindow::hit(LPARAM location) const {
    POINT point{static_cast<short>(LOWORD(location)), static_cast<short>(HIWORD(location))};
    if (!hwnd_ || !IsWindowVisible(hwnd_))
        return -1;
    POINT contentPoint{point.x, point.y + scrollOffset_};
    for (int i = 0; i < count_; ++i)
        if (PtInRect(&contentRect_, point) && PtInRect(&items_[i], contentPoint))
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
        self->shapeWidth_ = self->shapeHeight_ = 0;
        self->shapeRadius_ = -1;
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
    if ((msg == WM_THEMECHANGED || msg == WM_SETTINGCHANGE || msg == WM_SYSCOLORCHANGE) && self) {
        self->colors_ = palette(self->preferences_);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_MOUSEWHEEL && self && self->scrollMaximum_) {
        self->scrollOffset_ = std::clamp(self->scrollOffset_ - static_cast<short>(HIWORD(w)) / WHEEL_DELTA * self->rowHeight_, 0, self->scrollMaximum_);
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
    const int style = skinSelectionStyle(preferences_);
    drawSkinSurface(dc, client, colors, preferences_, fontDpi_, rail_);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, smallFont_ ? smallFont_ : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, colors.muted);
    SetTextColor(dc, colors.muted);
    RECT header{padding_ + 4, padding_, width - padding_ - rail_, padding_ + headerHeight_};
    if (headerHeight_)
        DrawTextW(dc, rows_[0].data, rows_[0].length, &header,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, contentRect_.left, contentRect_.top, contentRect_.right, contentRect_.bottom);
    for (int i = 0; i < count_; ++i) {
        const bool selected = i == selected_, hover = i == hover_;
        RECT row = items_[i];
        OffsetRect(&row, 0, -scrollOffset_);
        drawThemeSelection(dc, row, colors, style, fontDpi_, selected, hover);
        RECT number = row;
        number.left += padding_;
        number.right = number.left + numberWidth_;
        drawThemeBadge(dc, number, colors, style, fontDpi_, selected);
        wchar_t label[4] {association_ ? (selected ? L'›' : L' ') : static_cast<wchar_t>(L'1' + i), 0};
        SelectObject(dc, footerFont_ ? footerFont_ : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(dc, selected ? colors.selectedText : colors.muted);
        DrawTextW(dc, label, 1, &number, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        RECT text = textRects_[i];
        OffsetRect(&text, 0, -scrollOffset_);
        SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(dc, selected ? colors.selectedText : colors.text);
        DrawTextW(dc, rows_[i + 1].data, rows_[i + 1].length, &text,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (showPinyin_ && pinyin_[i].length) {
            RECT pronunciation = pinyinRects_[i];
            OffsetRect(&pronunciation, 0, -scrollOffset_);
            int x=pronunciation.left;
            for (int at=0;at<pinyin_[i].length;) {
                const bool marked=marks_[i][at]!=0;
                int end=at+1;
                while (end<pinyin_[i].length && (marks_[i][end]!=0)==marked) ++end;
                SelectObject(dc,marked && boldPinyinFont_ ? boldPinyinFont_ : smallFont_);
                SetTextColor(dc,selected ? colors.selectedText : (marked ? colors.accent : colors.muted));
                SIZE size{}; GetTextExtentPoint32W(dc,pinyin_[i].data+at,end-at,&size);
                RECT run{x,pronunciation.top,std::min<LONG>(x+size.cx,pronunciation.right),pronunciation.bottom};
                DrawTextW(dc,pinyin_[i].data+at,end-at,&run,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
                x+=size.cx; at=end; if (x>=pronunciation.right) break;
            }
        }
    }
    RestoreDC(dc, saved);
    SelectObject(dc, footerFont_ ? footerFont_ : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, colors.muted);
    if (footerHeight_)
        DrawTextW(dc, footer_.data, footer_.length, &footerRect_,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    if (dc != screen)
        BitBlt(screen, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    EndPaint(hwnd_, &ps);
}
}
