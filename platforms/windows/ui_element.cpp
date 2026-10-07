#include "ui_element.h"
#include <algorithm>
namespace chengyin {
void CandidateElement::update(ChengyinSession *session, ITfContext *context, uint64_t generation) {
    generation_ = generation;
    count_ = static_cast<UINT>(std::clamp(chengyin_session_candidate_count(session), 0, 9));
    selected_ = static_cast<UINT>(std::max(0, chengyin_session_selected(session)));
    if (!count_)
        pageCount_ = 0;
    else if (!pageCount_ || pages_[pageCount_ - 1] >= count_) {
        pageCount_ = 1;
        pages_[0] = 0;
    }
    for (UINT i = 0; i < count_; ++i)
        readText(session, CHENGYIN_TEXT_CANDIDATE, i, rows_[i]);
    Ptr<ITfDocumentMgr> incoming;
    if (context)
        context->GetDocumentMgr(incoming.put());
    if (target_)
        document_.attach(incoming.detach());
}
HRESULT CandidateElement::QueryInterface(REFIID iid, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfUIElement && iid != kCandidateUI && iid != kCandidateBehavior)
        return E_NOINTERFACE;
    *out = static_cast<CandidateBehavior *>(this);
    AddRef();
    return S_OK;
}
ULONG CandidateElement::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&refs_));
}
ULONG CandidateElement::Release() {
    const LONG count = InterlockedDecrement(&refs_);
    if (!count)
        delete this;
    return static_cast<ULONG>(count);
}
HRESULT CandidateElement::GetDescription(BSTR *out) {
    if (!out)
        return E_POINTER;
    *out = SysAllocString(L"澄音 全拼候选");
    return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT CandidateElement::GetGUID(GUID *out) {
    if (!out)
        return E_POINTER;
    *out = kProfile;
    return S_OK;
}
HRESULT CandidateElement::Show(BOOL show) {
    shown_ = show != FALSE;
    if (callback_ && target_)
        callback_(target_, generation_, shown_ ? 12 : 13, false);
    return S_OK;
}
HRESULT CandidateElement::IsShown(BOOL *out) {
    if (!out)
        return E_POINTER;
    *out = shown_;
    return S_OK;
}
HRESULT CandidateElement::GetUpdatedFlags(DWORD *out) {
    if (!out)
        return E_POINTER;
    *out = 0x3f;
    return S_OK;
}
HRESULT CandidateElement::GetDocumentMgr(ITfDocumentMgr **out) {
    if (!out)
        return E_POINTER;
    *out = document_.get();
    if (*out)
        (*out)->AddRef();
    return S_OK;
}
HRESULT CandidateElement::GetCount(UINT *out) {
    if (!out)
        return E_POINTER;
    *out = count_;
    return S_OK;
}
HRESULT CandidateElement::GetSelection(UINT *out) {
    if (!out)
        return E_POINTER;
    *out = selected_;
    return S_OK;
}
HRESULT CandidateElement::GetString(UINT index, BSTR *out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (index >= count_)
        return E_INVALIDARG;
    *out = SysAllocStringLen(rows_[index].data, static_cast<UINT>(rows_[index].length));
    return *out ? S_OK : E_OUTOFMEMORY;
}
// The current core page is an atomic candidate list, replaced on PgUp/PgDn.
// Hosts may draw/reflow its <=9 strings; keyboard paging continues through TSF.
HRESULT CandidateElement::GetPageIndex(UINT *indexes, UINT capacity, UINT *count) {
    if (!count)
        return E_POINTER;
    *count = pageCount_;
    if (capacity && !indexes)
        return E_POINTER;
    for (UINT i = 0; i < std::min(capacity, pageCount_); ++i)
        indexes[i] = pages_[i];
    return capacity >= pageCount_ ? S_OK : S_FALSE;
}
HRESULT CandidateElement::SetPageIndex(UINT *indexes, UINT count) {
    if (!count_ && !count) {
        pageCount_ = 0;
        return S_OK;
    }
    if (!indexes || !count || count > 9 || indexes[0] != 0)
        return E_INVALIDARG;
    for (UINT i = 0; i < count; ++i)
        if (indexes[i] >= count_ || (i && indexes[i] <= indexes[i - 1]))
            return E_INVALIDARG;
    std::copy(indexes, indexes + count, pages_);
    pageCount_ = count;
    return S_OK;
}
HRESULT CandidateElement::GetCurrentPage(UINT *out) {
    if (!out)
        return E_POINTER;
    *out = 0;
    for (UINT i = 1; i < pageCount_; ++i) {
        if (pages_[i] > selected_)
            break;
        *out = i;
    }
    return S_OK;
}
HRESULT CandidateElement::SetSelection(UINT index) {
    if (index >= count_)
        return E_INVALIDARG;
    if (!target_ || !callback_)
        return E_UNEXPECTED;
    selected_ = index;
    callback_(target_, generation_, static_cast<int>(index), false);
    return S_OK;
}
HRESULT CandidateElement::Finalize() {
    if (!target_ || !callback_)
        return E_UNEXPECTED;
    callback_(target_, generation_, static_cast<int>(selected_), true);
    return S_OK;
}
HRESULT CandidateElement::Abort() {
    if (!target_ || !callback_)
        return E_UNEXPECTED;
    callback_(target_, generation_, 11, true);
    return S_OK;
}
}
