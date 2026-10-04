#pragma once
#include "candidate.h"
#include <oleauto.h>
namespace myswy {
inline constexpr GUID kCandidateUI = {0xea1ea138, 0x19df, 0x11d7, {0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
inline constexpr GUID kCandidateBehavior = {0x85fad185, 0x58ce, 0x497a, {0x94, 0x60, 0x35, 0x53, 0x66, 0xb6, 0x4b, 0x9a}};
// SDK-compatible vtable also available with older MinGW headers.
struct CandidateUI: ITfUIElement {
    virtual HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSelection(UINT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetString(UINT, BSTR *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPageIndex(UINT *, UINT, UINT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPageIndex(UINT *, UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT *) = 0;
};
struct CandidateBehavior: CandidateUI {
    virtual HRESULT STDMETHODCALLTYPE SetSelection(UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE Finalize() = 0;
    virtual HRESULT STDMETHODCALLTYPE Abort() = 0;
};
class CandidateElement final: public CandidateBehavior {
  public:
    using Callback = void(*)(void *, uint64_t, int, bool);
    CandidateElement(void *target, Callback callback): target_(target), callback_(callback) {}
    void update(MyswySession *, ITfContext *, uint64_t);
    void detach() {
        target_ = nullptr;
        callback_ = nullptr;
        document_.reset();
        shown_ = false;
    }
    bool shown()const {
        return shown_;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetDescription(BSTR *) override;
    HRESULT STDMETHODCALLTYPE GetGUID(GUID *) override;
    HRESULT STDMETHODCALLTYPE Show(BOOL) override;
    HRESULT STDMETHODCALLTYPE IsShown(BOOL *) override;
    HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD *) override;
    HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr **) override;
    HRESULT STDMETHODCALLTYPE GetCount(UINT *) override;
    HRESULT STDMETHODCALLTYPE GetSelection(UINT *) override;
    HRESULT STDMETHODCALLTYPE GetString(UINT, BSTR *) override;
    HRESULT STDMETHODCALLTYPE GetPageIndex(UINT *, UINT, UINT *) override;
    HRESULT STDMETHODCALLTYPE SetPageIndex(UINT *, UINT) override;
    HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT *) override;
    HRESULT STDMETHODCALLTYPE SetSelection(UINT) override;
    HRESULT STDMETHODCALLTYPE Finalize() override;
    HRESULT STDMETHODCALLTYPE Abort() override;
  private:
    ModuleLifetime lifetime_;
    LONG refs_ = 1;
    void *target_;
    Callback callback_;
    uint64_t generation_ = 0;
    WideText rows_[9] {};
    UINT count_ = 0, selected_ = 0;
    bool shown_ = true;
    UINT pages_[9]{},pageCount_=1;
    Ptr<ITfDocumentMgr> document_;
};
}
