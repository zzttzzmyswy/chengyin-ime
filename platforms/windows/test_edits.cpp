// A deterministic in-memory TSF host. This tests our actual COM service, not
// Windows' text store or GUI. Faults are injected before/after text mutation.
#include "test_stubs.h"
#include "ui_element.h"
#include <textstor.h>
#include <inputscope.h>
#include "test_configuration.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace myswy::test {
HWND testCandidateWindow() {
    struct Search { HWND any = nullptr, visible = nullptr; } found;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM parameter) -> BOOL {
        auto *result = reinterpret_cast<Search *>(parameter);
        wchar_t name[64]{}; GetClassNameW(window, name, 64);
        if (!std::wcscmp(name, L"Myswy.Candidates.Preview1")) {
            result->any = window;
            if (IsWindowVisible(window)) result->visible = window;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found.visible ? found.visible : found.any;
}
void require(bool ok, const char *name) {
    if (!ok) {
        std::fprintf(stderr, "TSF edit FAIL: %s\n", name);
        std::exit(1);
    }
}
struct Document {
    std::wstring text;
    LONG start = 0, end = 0;
    bool locked = false, denyLock = false, denyWrite = false, denySelection = false, readOnly = false,
         queueReads = false;
    int writes = 0, ends = 0, reads = 0, viewQueries = 0;
    int inputScope = -1;
    HWND owner = nullptr;
    bool queueChoices = false, failExt = false, collapsedExtFails = false, clipped = false, noViewWindow = false;
};
class EditRecord final : public ITfEditRecord {
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditRecord)
            return E_NOINTERFACE;
        *out = static_cast<ITfEditRecord *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return --refs_;
    }
    HRESULT STDMETHODCALLTYPE GetSelectionStatus(BOOL *changed) override {
        *changed = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextAndPropertyUpdates(DWORD, const GUID **, ULONG, IEnumTfRanges **) override {
        return E_NOTIMPL;
    }
  private:
    ULONG refs_ = 1;
};
class ScopeProperty final : public ITfReadOnlyProperty, public ITfInputScope {
  public:
    explicit ScopeProperty(int value) : scope_(static_cast<InputScope>(value)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        constexpr GUID scopeId = {0xfde1eaee, 0x6924, 0x4cdf, {0x91, 0xe7, 0xda, 0x38, 0xcf, 0xf5, 0x55, 0x9d}};
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfReadOnlyProperty)
            *out = static_cast<ITfReadOnlyProperty *>(this);
        else if (iid == scopeId)
            *out = static_cast<ITfInputScope *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetType(GUID *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EnumRanges(TfEditCookie, IEnumTfRanges **, ITfRange *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetContext(ITfContext **) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetValue(TfEditCookie, ITfRange *, VARIANT *out) override {
        VariantInit(out);
        out->vt = VT_UNKNOWN;
        out->punkVal = static_cast<ITfInputScope *>(this);
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInputScopes(InputScope **out, UINT *count) override {
        *out = static_cast<InputScope *>(CoTaskMemAlloc(sizeof(InputScope)));
        if (!*out)
            return E_OUTOFMEMORY;
        **out = scope_;
        *count = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPhrase(BSTR **, UINT *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetRegularExpression(BSTR *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetSRGS(BSTR *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetXML(BSTR *) override {
        return E_NOTIMPL;
    }
  private:
    ULONG refs_ = 1;
    InputScope scope_;
};
// Test ranges reference their Context-owned document; tests keep the Context
// alive until all service/edit/composition references have been detached.
class Range final : public RangeStub {
  public:
    Range(Document &doc, LONG begin, LONG finish) : doc(doc), begin(begin), finish(finish) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfRange)
            return E_NOINTERFACE;
        *out = static_cast<ITfRange *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetText(TfEditCookie, DWORD, WCHAR *out, ULONG maximum, ULONG *count) override {
        const auto size = std::min<size_t>(maximum, static_cast<size_t>(std::max<LONG>(0, finish - begin)));
        if (static_cast<size_t>(begin) + size > doc.text.size())
            return E_FAIL;
        std::copy_n(doc.text.data() + begin, size, out);
        *count = static_cast<ULONG>(size);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetText(TfEditCookie cookie, DWORD, const WCHAR *text, LONG size) override {
        require(doc.locked && cookie == 123, "SetText under write lock");
        if (doc.denyWrite)
            return E_FAIL;
        doc.text.replace(static_cast<size_t>(begin), static_cast<size_t>(finish - begin), text,
                         static_cast<size_t>(size));
        finish = begin + size;
        ++doc.writes;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(ITfRange **out) override {
        *out = new Range(doc, begin, finish);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Collapse(TfEditCookie, TfAnchor anchor) override {
        if (anchor == TF_ANCHOR_END)
            begin = finish;
        else
            finish = begin;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ShiftEnd(TfEditCookie, LONG count, LONG *shifted, const TF_HALTCOND *) override {
        const LONG target = std::max(begin, std::min(static_cast<LONG>(doc.text.size()), finish + count));
        *shifted = target - finish;
        finish = target;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CompareStart(TfEditCookie, ITfRange *range, TfAnchor anchor, LONG *out) override {
        auto *other = static_cast<Range *>(range);
        *out = begin - (anchor == TF_ANCHOR_START ? other->begin : other->finish);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ShiftEndToRange(TfEditCookie, ITfRange *range, TfAnchor anchor) override {
        auto *other = static_cast<Range *>(range);
        finish = anchor == TF_ANCHOR_START ? other->begin : other->finish;
        return finish >= begin ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE CompareEnd(TfEditCookie, ITfRange *range, TfAnchor anchor, LONG *out) override {
        auto *other = static_cast<Range *>(range);
        *out = finish - (anchor == TF_ANCHOR_START ? other->begin : other->finish);
        return S_OK;
    }
    Document &doc;
    LONG begin, finish;
  private:
    ULONG refs = 1;
};
class Composition final : public ITfComposition {
  public:
    Composition(ITfRange *range, Document &doc, ITfCompositionSink *sink) : range(range), doc(doc), sink(sink) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfComposition)
            return E_NOINTERFACE;
        *out = static_cast<ITfComposition *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetRange(ITfRange **out) override {
        *out = range.get();
        (*out)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ShiftStart(TfEditCookie, ITfRange *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ShiftEnd(TfEditCookie, ITfRange *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EndComposition(TfEditCookie cookie) override {
        require(doc.locked && cookie == 123, "EndComposition under write lock");
        ++doc.ends;
        // Exercise a synchronous reentrant termination notification.
        if (sink)
            sink->OnCompositionTerminated(cookie, this);
        sink.reset();
        return S_OK;
    }
  private:
    ULONG refs = 1;
    Ptr<ITfRange> range;
    Document &doc;
    Ptr<ITfCompositionSink> sink;
};
class View final : public ITfContextView {
  public:
    explicit View(Document &doc) : doc_(doc) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfContextView)
            return E_NOINTERFACE;
        *out = static_cast<ITfContextView *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetRangeFromPoint(TfEditCookie, const POINT *, DWORD, ITfRange **) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetTextExt(TfEditCookie, ITfRange *range, RECT *rect, BOOL *clipped) override {
        auto *value = static_cast<Range *>(range);
        if (doc_.failExt || (doc_.collapsedExtFails && value->begin == value->finish))
            return TF_E_NOLAYOUT;
        *rect = {100, 100, 101, 120};
        *clipped = doc_.clipped;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetScreenExt(RECT *rect) override {
        *rect = {100, 100, 400, 300};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetWnd(HWND *out) override {
        *out = doc_.noViewWindow ? nullptr : doc_.owner;
        return S_OK;
    }
  private:
    ULONG refs_ = 1;
    Document &doc_;
};
class Context final : public ContextStub, public CompositionContextStub, public ITfSource,
    public ITfInsertAtSelection {
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfContext)
            *out = static_cast<ITfContext *>(this);
        else if (iid == IID_ITfContextComposition)
            *out = static_cast<ITfContextComposition *>(this);
        else if (iid == IID_ITfSource)
            *out = static_cast<ITfSource *>(this);
        else if (iid == IID_ITfInsertAtSelection)
            *out = static_cast<ITfInsertAtSelection *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId, ITfEditSession *edit, DWORD flags,
            HRESULT *result) override {
        require((flags & (TF_ES_READWRITE | TF_ES_READ)) != 0, "valid edit requested");
        const bool writing = (flags & TF_ES_READWRITE) == TF_ES_READWRITE;
        if (!writing)
            ++doc.reads;
        if (!writing && doc.queueReads) {
            require(!pendingRead, "at most one pending UI read per context");
            pendingRead.attach(edit);
            edit->AddRef();
            *result = TF_S_ASYNC;
            return S_OK;
        }
        if (writing && !(flags & TF_ES_SYNC) && doc.queueChoices) {
            require(!pendingChoice, "at most one pending candidate write");
            pendingChoice.attach(edit);
            edit->AddRef();
            *result = TF_S_ASYNC;
            return S_OK;
        }
        if (doc.denyLock) {
            *result = TF_E_SYNCHRONOUS;
            return S_OK;
        }
        doc.locked = true;
        *result = edit->DoEditSession(123);
        doc.locked = false;
        if (writing && sink)
            sink->OnEndEdit(this, 456, nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION *out,
                                           ULONG *fetched) override {
        *out = {new Range(doc, doc.start, doc.end), {TF_AE_NONE, FALSE}};
        *fetched = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie, ULONG, const TF_SELECTION *selection) override {
        require(doc.locked, "SetSelection under write lock");
        if (doc.denySelection)
            return E_FAIL;
        auto *range = static_cast<Range *>(selection->range);
        doc.start = range->begin;
        doc.end = range->finish;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS *status) override {
        *status = {doc.readOnly ? TS_SD_READONLY : 0u, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView **out) override {
        ++doc.viewQueries;
        if (!doc.owner)
            return E_NOTIMPL;
        *out = new View(doc);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAppProperty(REFGUID, ITfReadOnlyProperty **out) override {
        if (doc.inputScope < 0)
            return E_NOTIMPL;
        *out = new ScopeProperty(doc.inputScope);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID iid, IUnknown *value, DWORD *cookie) override {
        if (iid == kLayoutSink) {
            require(!layout, "one layout sink per context");
            *cookie = 43;
            return query(value, kLayoutSink, layout);
        }
        require(iid == IID_ITfTextEditSink && !sink, "one text sink per context");
        const HRESULT hr = query(value, IID_ITfTextEditSink, sink);
        *cookie = 42;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD cookie) override {
        if (cookie == 43)
            layout.reset();
        else {
            require(cookie == 42, "sink cookie");
            sink.reset();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InsertTextAtSelection(TfEditCookie, DWORD flags, const WCHAR *, LONG,
            ITfRange **out) override {
        require(flags == TF_IAS_QUERYONLY && doc.locked, "query insertion range before mutation");
        *out = new Range(doc, doc.start, doc.end);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(TfEditCookie, DWORD, IDataObject *,
            ITfRange **) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE StartComposition(TfEditCookie, ITfRange *range, ITfCompositionSink *value,
            ITfComposition **out) override {
        *out = new Composition(range, doc, value);
        return S_OK;
    }
    void externalMove(LONG position) {
        doc.start = doc.end = position;
        // Hold a reference because the service unadvises itself during the callback.
        Ptr<ITfTextEditSink> current(sink.get());
        if (current)
            current->OnEndEdit(this, 456, nullptr);
    }
    void notifyAcceptedEdit() {
        EditRecord record;
        Ptr<ITfTextEditSink> current(sink.get());
        if (current)
            current->OnEndEdit(this, 456, &record);
    }
    void flushRead() {
        Ptr<ITfEditSession> edit;
        edit.attach(pendingRead.detach());
        require(static_cast<bool>(edit), "a UI read was queued");
        doc.locked = true;
        require(SUCCEEDED(edit->DoEditSession(123)), "queued UI read completion");
        doc.locked = false;
    }
    void flushChoice() {
        Ptr<ITfEditSession> edit;
        edit.attach(pendingChoice.detach());
        require(static_cast<bool>(edit), "a candidate edit was queued");
        doc.locked = true;
        require(SUCCEEDED(edit->DoEditSession(123)), "queued candidate edit completion");
        doc.locked = false;
    }
    Document doc;
    Ptr<ITfTextEditSink> sink;
    Ptr<LayoutSink> layout;
    Ptr<ITfEditSession> pendingRead;
    Ptr<ITfEditSession> pendingChoice;
    ULONG refs = 1;
};
bool key(ITfKeyEventSink *keys, Context *context, UINT vk) {
    const LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16;
    BOOL eaten = FALSE;
    for (int i = 0; i < 3; ++i) {
        const int before = context->doc.writes;
        require(SUCCEEDED(keys->OnTestKeyDown(context, vk, scan, &eaten)), "test key success");
        require(before == context->doc.writes, "test key never writes");
    }
    require(SUCCEEDED(keys->OnKeyDown(context, vk, scan, &eaten)), "key success");
    const bool consumed = eaten != FALSE;
    require(SUCCEEDED(keys->OnTestKeyUp(context, vk, scan, &eaten))
            && (eaten != FALSE) == consumed, "matching test keyup");
    require(SUCCEEDED(keys->OnKeyUp(context, vk, scan, &eaten))
            && (eaten != FALSE) == consumed, "matching keyup");
    return consumed;
}
void type(ITfKeyEventSink *keys, Context *context, const char *text) {
    for (; *text; ++text)
        if (!key(keys, context, static_cast<UINT>(*text - 'a' + 'A'))) {
            std::fprintf(stderr, "TSF letter failed: %c\n", *text);
            require(false, "letter consumed");
        }
}
}
void runEditTests(ITfKeyEventSink *keys) {
    using namespace myswy;
    using namespace myswy::test;
    BYTE previous[256] {}, neutral[256] {};
    require(GetKeyboardState(previous) && SetKeyboardState(neutral), "neutral keyboard state");
    HKL layout = LoadKeyboardLayoutW(L"00000409", 0);
    HKL oldLayout = layout ? ActivateKeyboardLayout(layout, 0) : nullptr;
    Ptr<Context> first, second;
    first.attach(new Context);
    second.attach(new Context);
    Context *a = first.get();
    Context *b = second.get();
    a->doc.denyLock = true;
    require(!key(keys, a, 'N')
            && a->doc.text.empty(), "denied synchronous lock passes key without core mutation");
    a->doc.denyLock = false;
    {
        Ptr<Context> editing;
        editing.attach(new Context);
        type(keys, editing.get(), "nhao");
        require(key(keys, editing.get(), VK_HOME)
                && key(keys, editing.get(), VK_RIGHT), "composition caret navigation");
        require(key(keys, editing.get(), 'I') && editing->doc.text == L"nihao"
                && editing->doc.end == 2, "insert at middle in TSF write lock");
        require(key(keys, editing.get(), VK_DELETE) && editing->doc.text == L"niao", "Delete at caret");
        require(key(keys, editing.get(), 'H') && key(keys, editing.get(), '1')
                && editing->doc.text == L"你好", "edited pinyin commits once");
        editing->doc.text.clear();
        editing->doc.start = editing->doc.end = 0;
        type(keys, editing.get(), "woxihuanzhongwen");
        require(key(keys, editing.get(), '1')
                && editing->doc.text == L"我喜欢中文", "daily whole-sentence TSF commit");
        const auto prefix = editing->doc.text;
        type(keys, editing.get(), "shi");
        require(key(keys, editing.get(), VK_NEXT)
                && key(keys, editing.get(), VK_PRIOR), "candidate next and previous page consumed");
        require(editing->doc.text == prefix + L"shi", "paging never changes composition text");
        key(keys, editing.get(), VK_ESCAPE);
        type(keys, editing.get(), "wovvvv");
        require(key(keys, editing.get(), '1')
                && editing->doc.text == prefix + L"我vvvv",
                "prefix selection preserves untranslated remainder in one composition");
        require(key(keys, editing.get(), VK_HOME)
                && editing->doc.end == static_cast<LONG>(prefix.size()) + 1,
                "UTF-8 core cursor maps to Chinese UTF-16 caret");
        require(key(keys, editing.get(), VK_BACK)
                && editing->doc.text == prefix + L"wovvvv", "Backspace unlocks confirmed segment");
        key(keys, editing.get(), VK_ESCAPE);

    }
    {
        Ptr<Context> raw;
        raw.attach(new Context);
        type(keys, raw.get(), "nihao");
        raw->doc.start = raw->doc.end = 2;
        raw->notifyAcceptedEdit();
        require(key(keys, raw.get(), VK_DELETE) && raw->doc.text == L"niao", "mouse caret edits existing inline pinyin");
        require(key(keys, raw.get(), 'H') && raw->doc.text == L"nihao", "insert follows mouse caret");
        const int writes = raw->doc.writes;
        require(key(keys, raw.get(), VK_SPACE) && raw->doc.text == L"nihao" && raw->doc.ends == 1
                && raw->doc.writes == writes + 1, "space commits raw spelling once, no candidate conversion");
        require(!key(keys, raw.get(), VK_SPACE), "idle space passes through to application");
        type(keys, raw.get(), "xi");
        key(keys, raw.get(), VK_OEM_7); type(keys, raw.get(), "an");
        require(key(keys, raw.get(), VK_SPACE) && raw->doc.text == L"nihaoxi'an", "raw space preserves explicit apostrophe");
        for (UINT vk : {0x36u, 0x37u, 0x4au, 0x31u, static_cast<UINT>(VK_OEM_PLUS)}) {
            BYTE shifted[256]{}; shifted[VK_SHIFT] = shifted[VK_LSHIFT] = 0x80;
            require(SetKeyboardState(shifted), "shift chord keyboard state");
            require(!key(keys, raw.get(), vk), "idle shifted symbol/capital is passed to host");
            require(SetKeyboardState(neutral), "clear shift chord");
            type(keys, raw.get(), "ni");
            const auto text = raw->doc.text;
            require(SetKeyboardState(shifted), "shift held during composition");
            require(!key(keys, raw.get(), vk) && raw->doc.text == text,
                    "shift finishes raw preedit and leaves actual character to host");
            require(SetKeyboardState(neutral), "restore neutral keyboard");
        }
    }
    {
        Ptr<Context> modern;
        modern.attach(new Context);
        type(keys, modern.get(), "zg");
        require(key(keys, modern.get(), '2')
                && modern->doc.text == L"中国", "first-letter abbreviation TSF commit");
        const int ended = modern->doc.ends;
        require(key(keys, modern.get(), VK_DOWN) && modern->doc.text == L"中国"
                && modern->doc.ends == ended, "idle association navigation creates no composition");
        require(key(keys, modern.get(), VK_ESCAPE)
                && modern->doc.text == L"中国", "association cancel preserves committed text");
        type(keys, modern.get(), "nihao");
        require(key(keys, modern.get(), '1'), "association source commit");
        require(key(keys, modern.get(), VK_TAB)
                && modern->doc.text == L"中国你好世界", "Tab inserts only the continuation");
        type(keys, modern.get(), "nihao");
        require(key(keys, modern.get(), '1'), "new association source");
        require(!key(keys, modern.get(), VK_SPACE)
                && modern->doc.text == L"中国你好世界你好", "space dismisses continuation and is left to the host");
        type(keys, modern.get(), "wxhzw");
        require(key(keys, modern.get(), '1')
                && modern->doc.text == L"中国你好世界你好我喜欢中文", "continuous initials sentence TSF commit");
        type(keys, modern.get(), "zhongg");
        require(key(keys, modern.get(), '1')
                && modern->doc.text == L"中国你好世界你好我喜欢中文中国", "mixed full and initial spelling");
        const auto original = modern->doc.text;
        modern->doc.start = modern->doc.end = 0;
        require(!key(keys, modern.get(), VK_TAB)
                && modern->doc.text == original, "moved caret cannot accept stale association");
    }
    {
        Ptr<Context> delayed;
        delayed.attach(new Context);
        type(keys, delayed.get(), "ni");
        delayed->notifyAcceptedEdit();
        type(keys, delayed.get(), "hao");
        delayed->notifyAcceptedEdit();
        require(key(keys, delayed.get(), '1')
                && delayed->doc.text == L"你好", "delayed own edit notifications preserve composition");
        type(keys, delayed.get(), "ni");
        delayed->doc.start = delayed->doc.end = 0;
        delayed->notifyAcceptedEdit();
        require(!delayed->sink, "delayed foreign caret change retires composition");
        delayed->doc.start = delayed->doc.end = static_cast<LONG>(delayed->doc.text.size());
        type(keys, delayed.get(), "ni");
        delayed->doc.text.back() = L'x';
        delayed->notifyAcceptedEdit();
        require(!delayed->sink, "foreign composition text mutation retires session");
    }
    {
        Ptr<Context> manual;
        manual.attach(new Context);
        type(keys, manual.get(), "xi");
        require(key(keys, manual.get(), VK_OEM_7), "manual apostrophe consumed");
        type(keys, manual.get(), "an");
        require(manual->doc.text == L"xi'an" && key(keys, manual.get(), '1')
                && manual->doc.text == L"西安", "explicit syllable split survives host editing");
        const LPARAM left = static_cast<LPARAM>(0x2a) << 16;
        BOOL eaten = FALSE;
        for (int i = 0; i < 3; ++i)
            require(SUCCEEDED(keys->OnTestKeyDown(manual.get(), VK_SHIFT, left, &eaten))
                    && eaten, "Shift tests request dispatch without toggling");
        require(SUCCEEDED(keys->OnKeyDown(manual.get(), VK_SHIFT, left, &eaten))
                && !eaten, "Shift modifier down passes host");
        require(SUCCEEDED(keys->OnKeyUp(manual.get(), VK_SHIFT, left, &eaten))
                && eaten, "Shift alone toggles on release");
        require(!key(keys, manual.get(), 'N'), "Shift switches to English");
        keys->OnKeyDown(manual.get(), VK_SHIFT, left, &eaten);
        keys->OnKeyUp(manual.get(), VK_SHIFT, left, &eaten);
        type(keys, manual.get(), "ni");
        require(key(keys, manual.get(), VK_ESCAPE), "Shift switches back to Chinese");
    }
    {
        Ptr<Context> punctuation;
        punctuation.attach(new Context);
        require(key(keys, punctuation.get(), VK_OEM_COMMA) && punctuation->doc.text == L"，", "idle Chinese comma");
        punctuation->doc.denyWrite = true;
        require(!key(keys, punctuation.get(), VK_OEM_7), "failed quote insertion passes original key");
        punctuation->doc.denyWrite = false;
        require(key(keys, punctuation.get(), VK_OEM_7)
                && punctuation->doc.text == L"，‘", "failed host write cannot advance quote pair");
        require(key(keys, punctuation.get(), VK_OEM_7)
                && punctuation->doc.text == L"，‘’", "quote pair follows successful writes");
        BOOL eaten = FALSE;
        require(SUCCEEDED(keys->OnPreservedKey(punctuation.get(), kToggleKey, &eaten)) && eaten, "toggle to English");
        require(!key(keys, punctuation.get(), VK_OEM_COMMA)
                && punctuation->doc.text == L"，‘’", "English punctuation passes through");
        keys->OnPreservedKey(punctuation.get(), kToggleKey, &eaten);
        punctuation->doc.inputScope = IS_PASSWORD;
        require(!key(keys, punctuation.get(), VK_OEM_COMMA), "password punctuation passes through");
    }
    {
        Ptr<ConfigurationTest> configuration;
        require(SUCCEEDED(query(keys, kConfigurationTest, configuration)),
                "isolated live-service configuration fixture");
        Ptr<Context> live;
        live.attach(new Context);
        type(keys, live.get(), "ni");
        const char replacement[] = "nihao\t热更新\t100\n";
        require(SUCCEEDED(configuration->Update(5, FALSE, FALSE, FALSE, reinterpret_cast<const uint8_t *>(replacement),
                                                sizeof(replacement) - 1)), "live configuration accepted during composition");
        require(live->doc.text == L"ni", "configuration refresh cannot discard raw input");
        type(keys, live.get(), "hao");
        require(key(keys, live.get(), '1')
                && live->doc.text == L"你好", "active composition retains original dictionary");
        type(keys, live.get(), "nihao");
        require(key(keys, live.get(), '1')
                && live->doc.text == L"你好热更新", "next composition in same app uses new dictionary");
        require(!key(keys, live.get(), VK_OEM_COMMA), "punctuation preference changes without reopening app");
        require(SUCCEEDED(configuration->Update(9, TRUE, TRUE, TRUE, nullptr, 0)), "restore default fixture");
        require(key(keys, live.get(), VK_OEM_COMMA)
                && live->doc.text == L"你好热更新，", "live punctuation enable applies immediately");
    }
    type(keys, a, "nihao");
    require(a->doc.text == L"nihao", "preedit exactly once");
    const int writes = a->doc.writes;
    require(a->layout
            && SUCCEEDED(a->layout->OnLayoutChange(a, LayoutCode::change, nullptr)), "layout notification");
    require(a->doc.reads == 1
            && a->doc.writes == writes, "layout refresh acquires a read lock without mutating text");
    require(key(keys, a, '1') && a->doc.text == L"你好"
            && a->doc.ends == 1, "commit ends composition once");
    type(keys, a, "nihao");
    require(key(keys, a, VK_OEM_COMMA) && a->doc.text == L"你好你好，", "punctuation and commit in order");
    type(keys, a, "ni");
    require(key(keys, a, VK_ESCAPE) && a->doc.text == L"你好你好，", "Escape removes only active preedit");
    type(keys, a, "ni");
    a->doc.queueReads = true;
    a->layout->OnLayoutChange(a, LayoutCode::change, nullptr);
    a->layout->OnLayoutChange(a, LayoutCode::change, nullptr);
    const int pendingReads = a->doc.reads;
    type(keys, a, "h");
    a->layout->OnLayoutChange(a, LayoutCode::change, nullptr);
    require(a->doc.reads == pendingReads, "typing does not queue a second UI read while one remains pending");
    const int oldViews = a->doc.viewQueries;
    type(keys, b, "hao");
    a->flushRead();
    require(a->doc.viewQueries == oldViews,
            "a retired context's queued layout read cannot update the new candidate UI");
    require(a->doc.text == L"你好你好，nih"
            && b->doc.text == L"hao", "context switch preserves old raw text and isolates new session");
    require(key(keys, b, VK_RETURN) && b->doc.text == L"hao", "Enter raw commit");
    type(keys, b, "ni");
    require(key(keys, b, VK_LEFT) && b->doc.text == L"haoni"
            && b->doc.end == 4, "Left moves inside composition without committing");
    require(key(keys, b, VK_RETURN), "finish edited raw preedit");
    type(keys, b, "ni");
    b->externalMove(0);
    type(keys, b, "hao");
    require(b->doc.text == L"haohaonini", "external selection changes cannot replace old composition");
    key(keys, b, VK_ESCAPE);
    b->doc.denyWrite = true;
    const auto before = b->doc.text;
    require(!key(keys, b, 'N')
            && b->doc.text == before, "failed SetText passes original key and leaves host text");
    b->doc.denyWrite = false;
    b->doc.denySelection = true;
    require(key(keys, b, 'H')
            && b->doc.text == L"h" + before, "failed caret move after insertion must not replay key");
    b->doc.denySelection = false;
    b->doc.readOnly = true;
    require(!key(keys, b, 'N'), "read-only bypass");
    b->doc.readOnly = false;
    for (int scope : {
                31, 61, 63, 64, 65, 66
            }) {
        b->doc.inputScope = scope;
        const auto unchanged = b->doc.text;
        require(!key(keys, b, 'N') && b->doc.text == unchanged, "password/private/PIN input bypass");
    }
    b->doc.inputScope = -1;
    HWND password = CreateWindowW(L"EDIT", L"", WS_POPUP | WS_VISIBLE | ES_PASSWORD,
                                  0, 0, 150, 40, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(password != nullptr, "legacy password Edit fixture");
    HWND previousFocus = SetFocus(password);
    require(GetFocus() == password, "legacy password field focused");
    const auto beforePassword = b->doc.text;
    require(!key(keys, b, 'N')
            && b->doc.text == beforePassword, "Win32 password style bypasses even without TSF InputScope");
    SetFocus(previousFocus);
    DestroyWindow(password);
    // Click the actual passive popup; delayed choices may not apply to changed
    // input or a retired context. This is still a simulated text store.
    b->doc.owner = CreateWindowW(L"STATIC", L"TSF fixture", WS_OVERLAPPEDWINDOW,
                                 0, 0, 300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(b->doc.owner != nullptr, "candidate owner");
    b->externalMove(static_cast<LONG>(b->doc.text.size()));
    type(keys, b, "nihao");
    HWND stablePopup = testCandidateWindow();
    require(stablePopup && IsWindowVisible(stablePopup), "layout baseline candidate");
    b->doc.failExt = true;
    require(key(keys, b, VK_LEFT), "typing continues while TSF layout unavailable");
    require(stablePopup == testCandidateWindow()
            && IsWindowVisible(stablePopup), "temporary NOLAYOUT retains candidate anchor");
    b->doc.failExt = false;
    b->doc.collapsedExtFails = true;
    require(key(keys, b, VK_END) && IsWindowVisible(stablePopup), "nonempty range extent fallback");
    b->doc.clipped = true;
    b->layout->OnLayoutChange(b, LayoutCode::change, nullptr);
    require(!IsWindowVisible(stablePopup), "clipped view hides candidate instead of stale placement");
    b->doc.clipped = false;
    b->doc.collapsedExtFails = false;
    b->layout->OnLayoutChange(b, LayoutCode::change, nullptr);
    require(IsWindowVisible(stablePopup), "layout recovery restores candidate");
    const auto prefix = b->doc.text.substr(0, b->doc.text.size() - 5);
    const LPARAM row = MAKELPARAM(20, 15);
    auto click = [&] {
        HWND popup = testCandidateWindow();
        require(popup &&IsWindowVisible(popup), "actual candidate popup available");
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, row);
        SendMessageW(popup, WM_LBUTTONUP, 0, row);
    };
    b->doc.denyLock = true;
    const auto beforeClick = b->doc.text;
    click();
    require(b->doc.text == beforeClick, "denied mouse edit lock does not mutate the core or host");
    b->doc.denyLock = false;
    click();
    require(b->doc.text == prefix + L"你好", "mouse candidate commits exactly once");
    type(keys, b, "nihao");
    b->doc.queueChoices = true;
    click();
    click();
    require(static_cast<bool>(b->pendingChoice), "mouse write queued and duplicate click coalesced");
    type(keys, b, "ma");
    const auto changed = b->doc.text;
    b->flushChoice();
    require(b->doc.text == changed, "delayed click cannot select a candidate after further typing");
    click();
    b->layout->OnLayoutChange(b, LayoutCode::destroy, nullptr);
    b->flushChoice();
    require(b->doc.text == changed, "layout destruction invalidates queued candidate choices");
    key(keys, b, VK_ESCAPE);
    type(keys, b, "nihao");
    click();
    b->doc.queueChoices = false;
    keys->OnSetFocus(FALSE);
    const auto retired = b->doc.text;
    b->flushChoice();
    require(b->doc.text == retired, "delayed click cannot write after focus loss");
    b->doc.owner = nullptr;
    DestroyWindow(FindWindowW(L"STATIC", L"TSF fixture"));
    type(keys, b, "ni");
    BOOL switched = FALSE;
    require(keys->OnPreservedKey(b, kToggleKey, &switched) == S_OK && switched, "preserved toggle key");
    const auto raw = b->doc.text;
    require(!key(keys, b, 'H') && b->doc.text == raw, "English mode passes letters and preserves raw preedit");
    BYTE control[256] {};
    control[VK_CONTROL] = 0x80;
    require(SetKeyboardState(control), "Ctrl held for fallback toggle");
    require(key(keys, b, VK_SPACE), "Ctrl+Space toggles back to Chinese");
    require(SetKeyboardState(neutral), "clear Ctrl");
    type(keys, b, "nihao");
    require(key(keys, b, '1') && b->doc.text == raw + L"你好", "Chinese restored after toggle");
    BOOL eaten = FALSE;
    keys->OnKeyDown(b, 'N', 0, &eaten);
    require(eaten, "held key before focus loss");
    keys->OnSetFocus(FALSE);
    keys->OnTestKeyUp(a, 'N', 0, &eaten);
    require(!eaten, "old consumed keyup is not carried to another context");
    require(!a->sink && !b->sink && !a->layout && !b->layout && a->refs == 1
            && b->refs == 1, "focus cleanup releases contexts and sinks");
    if (oldLayout)
        ActivateKeyboardLayout(oldLayout, 0);
    SetKeyboardState(previous);
    std::puts("PASS: TSF edits, lock denial, commit order, focus isolation, cancellation, host failures, cleanup.");
}

namespace myswy::test {
class Manager final : public ThreadStub, public KeystrokeStub, public ITfSource, public ITfUIElementMgr {
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfThreadMgr)
            *out = static_cast<ITfThreadMgr *>(this);
        else if (iid == IID_ITfKeystrokeMgr)
            *out = static_cast<ITfKeystrokeMgr *>(this);
        else if (iid == IID_ITfSource)
            *out = static_cast<ITfSource *>(this);
        else if (iid == IID_ITfUIElementMgr && uiEnabled)
            *out = static_cast<ITfUIElementMgr *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE AdviseKeyEventSink(TfClientId client, ITfKeyEventSink *value, BOOL) override {
        require(client == 7 && !keys, "service key sink client and lifetime");
        keys.attach(value);
        value->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnadviseKeyEventSink(TfClientId client) override {
        require(client == 7 && keys, "service unadvise keys");
        keys.reset();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreserveKey(TfClientId client, REFGUID guid, const TF_PRESERVEDKEY *key,
                                          const WCHAR *, ULONG) override {
        require(client == 7 && guid == kToggleKey && key->uVKey == VK_SPACE && key->uModifiers == TF_MOD_CONTROL,
                "preserved Ctrl+Space registration");
        preserved = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnpreserveKey(REFGUID guid, const TF_PRESERVEDKEY *) override {
        require(guid == kToggleKey && preserved, "preserved key cleanup");
        preserved = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID iid, IUnknown *value, DWORD *cookie) override {
        require(iid == IID_ITfThreadMgrEventSink && !thread, "one thread sink");
        *cookie = 88;
        return query(value, IID_ITfThreadMgrEventSink, thread);
    }
    HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD cookie) override {
        require(cookie == 88 && thread, "service unadvise thread");
        thread.reset();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BeginUIElement(ITfUIElement *value, BOOL *show, DWORD *id) override {
        require(!ui, "one active UI element");
        ui.attach(value);
        value->AddRef();
        *id = 77;
        *show = FALSE;
        ++begins;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateUIElement(DWORD id) override {
        require(id == 77 && ui, "update active UI element");
        ++updates;
        if (deactivateOnUpdate) {
            deactivateOnUpdate = false;
            processor->Deactivate();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EndUIElement(DWORD id) override {
        require(id == 77 && ui, "end active UI element");
        ui.reset();
        ++ends;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetUIElement(DWORD id, ITfUIElement **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != 77 || !ui)
            return E_INVALIDARG;
        *out = ui.get();
        (*out)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumUIElements(IEnumTfUIElements **) override {
        return E_NOTIMPL;
    }
    bool deactivateOnUpdate = false;
    ProcessorEx *processor = nullptr;
    bool uiEnabled = false;
    int begins = 0, updates = 0, ends = 0;
    Ptr<ITfUIElement> ui;
    ULONG refs = 1;
    bool preserved = false;
    Ptr<ITfKeyEventSink> keys;
    Ptr<ITfThreadMgrEventSink> thread;
};
}
void runServiceTests(myswy::ProcessorEx *service, ITfKeyEventSink *keys) {
    using namespace myswy;
    using namespace myswy::test;
    Ptr<Manager> manager;
    manager.attach(new Manager);
    require(service->ActivateEx(manager.get(), 7, TF_TMAE_SECUREMODE) == E_NOTIMPL, "secure mode bypass");
    require(service->ActivateEx(manager.get(), 7, TF_TMAE_UIELEMENTENABLEDONLY) == E_NOINTERFACE,
            "UI-element-only requires manager interface");
    require(service->Activate(manager.get(), 7) == S_OK, "service activation");
    require(service->Activate(manager.get(), 7) == E_UNEXPECTED, "double activation");
    runEditTests(keys);
    require(service->Deactivate() == S_OK && service->Deactivate() == S_OK, "idempotent deactivation");
    require(manager->refs == 1 && !manager->keys && !manager->thread
            && !manager->preserved, "all manager sinks detached");
    BYTE previous[256] {}, neutral[256] {};
    require(GetKeyboardState(previous) && SetKeyboardState(neutral), "lifecycle neutral keyboard");
    for (int i = 0; i < 100; ++i) {
        require(service->Activate(manager.get(), 7) == S_OK, "repeated activation");
        Ptr<Context> context;
        context.attach(new Context);
        type(keys, context.get(), "nihao");
        require(key(keys, context.get(), '1')
                && context->doc.text == L"你好", "repeated activation dictionary remains valid");
        require(service->Deactivate() == S_OK && context->refs == 1 && manager->refs == 1 &&
                !manager->keys && !manager->thread
                && !manager->preserved, "repeated activation releases all sinks and contexts");
    }
    manager->uiEnabled = true;
    require(service->ActivateEx(manager.get(), 7, TF_TMAE_UIELEMENTENABLEDONLY) == S_OK,
            "UI-element-only activation");
    Ptr<Context> modern;
    modern.attach(new Context);
    modern->doc.queueChoices = true;
    type(keys, modern.get(), "shi");
    require(manager->begins == 1 && manager->updates > 0, "UI element begins once and updates per edit");
    Ptr<CandidateBehavior> candidate;
    require(SUCCEEDED(query(manager->ui.get(), kCandidateBehavior, candidate)),
            "host candidate behavior interface");
    UINT count = 0;
    require(candidate->GetCount(&count) == S_OK && count == 9, "host sees current candidate page");
    UINT layoutPages[3] {0, 4, 8}, pageCount = 0, pageIndexes[3] {};
    require(candidate->SetPageIndex(layoutPages, 3) == S_OK
            && candidate->GetPageIndex(pageIndexes, 3, &pageCount) == S_OK && pageCount == 3
            && pageIndexes[2] == 8, "host can reflow candidate rows");
    require(candidate->SetSelection(8) == S_OK && candidate->GetCurrentPage(&pageCount) == S_OK
            && pageCount == 2, "host page follows selected row");
    BSTR wanted = nullptr;
    require(candidate->GetString(1, &wanted) == S_OK, "host UTF-16 candidate string");
    const std::wstring chosen(wanted);
    SysFreeString(wanted);
    require(candidate->SetSelection(1) == S_OK && candidate->Finalize() == S_OK
            && candidate->Finalize() == S_OK, "host selection and duplicate finalize");
    modern->flushChoice();
    require(modern->doc.text == chosen && manager->ends == 1
            && manager->ui, "host choice commits once and replaces preedit UI with association UI");
    require(candidate->Finalize() == E_UNEXPECTED, "retired UI element cannot commit");
    candidate.reset();
    type(keys, modern.get(), "nihao");
    require(SUCCEEDED(query(manager->ui.get(), kCandidateBehavior, candidate)), "new UI element");
    candidate->Finalize();
    type(keys, modern.get(), "x");
    modern->flushChoice();
    require(modern->doc.text == chosen + L"nihaox", "stale host choice is rejected after typing");
    candidate->Abort();
    modern->flushChoice();
    require(modern->doc.text == chosen && !manager->ui, "host abort cancels current preedit");
    require(candidate->Abort() == E_UNEXPECTED, "late host abort cannot change text");
    candidate.reset();
    require(service->Deactivate() == S_OK && manager->refs == 1
            && modern->refs == 1, "modern UI lifecycle releases contexts and manager");
    require(manager->begins == manager->ends, "all host UI elements retired");
    require(service->ActivateEx(manager.get(), 7, TF_TMAE_UIELEMENTENABLEDONLY) == S_OK,
            "association host UI activation");
    {
        Ptr<Context> associationContext;
        associationContext.attach(new Context);
        associationContext->doc.queueChoices = true;
        type(keys, associationContext.get(), "nihao");
        require(key(keys, associationContext.get(), '1'), "host UI association source commit");
        Ptr<CandidateBehavior> association;
        require(SUCCEEDED(query(manager->ui.get(), kCandidateBehavior, association)),
                "host receives postcommit association UI");
        require(association->Finalize() == S_OK, "queue association choice");
        type(keys, associationContext.get(), "x");
        associationContext->flushChoice();
        require(associationContext->doc.text == L"你好x", "new typing invalidates queued association choice");
        key(keys, associationContext.get(), VK_ESCAPE);
        type(keys, associationContext.get(), "nihao");
        require(key(keys, associationContext.get(), '1'), "second association source");
        association.reset();
        require(SUCCEEDED(query(manager->ui.get(), kCandidateBehavior, association)), "second host association UI");
        require(association->Finalize() == S_OK
                && association->Finalize() == S_OK, "coalesced host association choices");
        associationContext->flushChoice();
        require(associationContext->doc.text == L"你好你好世界",
                "host association inserts once at collapsed caret");
        require(association->Finalize() == E_UNEXPECTED, "retired association UI rejects late choice");
        type(keys, associationContext.get(), "nihao");
        require(key(keys, associationContext.get(), '1'), "sensitive association source");
        association.reset();
        require(SUCCEEDED(query(manager->ui.get(), kCandidateBehavior, association)), "privacy test association UI");
        associationContext->doc.inputScope = IS_PASSWORD;
        const auto committed = associationContext->doc.text;
        association->Finalize();
        associationContext->flushChoice();
        require(associationContext->doc.text == committed
                && !manager->ui, "sensitive scope discards association before write");
        require(service->Deactivate() == S_OK
                && associationContext->refs == 1, "association host UI lifecycle cleanup");
    }
    require(manager->begins == manager->ends, "all association UI elements retired");
    require(service->ActivateEx(manager.get(), 7, TF_TMAE_UIELEMENTENABLEDONLY) == S_OK,
            "reentrant UI test activation");
    type(keys, modern.get(), "n");
    manager->processor = service;
    manager->deactivateOnUpdate = true;
    BOOL consumed = FALSE;
    require(keys->OnKeyDown(modern.get(), 'I', 0, &consumed) == S_OK
            && consumed, "text write remains consumed during reentrant deactivation");
    require(manager->refs == 1 && modern->refs == 1 && !manager->ui && !manager->keys
            && !manager->thread, "UI update may deactivate without dangling context or callbacks");
    BOOL lateRelease = TRUE;
    keys->OnTestKeyUp(modern.get(), 'I', 0, &lateRelease);
    require(!lateRelease, "reentrant deactivation cannot carry consumed keyup into a later context");
    SetKeyboardState(previous);
    std::puts("PASS: 100 activation/input/deactivation cycles.");
}
