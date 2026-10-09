// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only defaults: unimplemented TSF operations must fail explicitly.
#pragma once
#include "common.h"
namespace chengyin::test {
struct RangeStub : ITfRange {
    HRESULT STDMETHODCALLTYPE GetText(TfEditCookie , DWORD , WCHAR *, ULONG , ULONG *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetText(TfEditCookie , DWORD , const WCHAR *, LONG) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFormattedText(TfEditCookie , IDataObject **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEmbedded(TfEditCookie , REFGUID , REFIID , IUnknown **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InsertEmbedded(TfEditCookie , DWORD , IDataObject *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftStart(TfEditCookie , LONG , LONG *, const TF_HALTCOND *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftEnd(TfEditCookie , LONG , LONG *, const TF_HALTCOND *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftStartToRange(TfEditCookie , ITfRange *, TfAnchor) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftEndToRange(TfEditCookie , ITfRange *, TfAnchor) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftStartRegion(TfEditCookie , TfShiftDir , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShiftEndRegion(TfEditCookie , TfShiftDir , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsEmpty(TfEditCookie , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Collapse(TfEditCookie , TfAnchor) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsEqualStart(TfEditCookie , ITfRange *, TfAnchor , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsEqualEnd(TfEditCookie , ITfRange *, TfAnchor , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CompareStart(TfEditCookie , ITfRange *, TfAnchor , LONG *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CompareEnd(TfEditCookie , ITfRange *, TfAnchor , LONG *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AdjustForInsert(TfEditCookie , ULONG , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetGravity(TfGravity *, TfGravity *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetGravity(TfEditCookie , TfGravity , TfGravity) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(ITfRange **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetContext(ITfContext **) override { return E_NOTIMPL; }
};
struct ContextStub : ITfContext {
    HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId , ITfEditSession *, DWORD , HRESULT *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InWriteSession(TfClientId , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSelection(TfEditCookie , ULONG , ULONG , TF_SELECTION *, ULONG *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie , ULONG , const TF_SELECTION *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetStart(TfEditCookie , ITfRange **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEnd(TfEditCookie , ITfRange **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumViews(IEnumTfContextViews **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID , ITfProperty **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetAppProperty(REFGUID , ITfReadOnlyProperty **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TrackProperties(const GUID **, ULONG , const GUID **, ULONG , ITfReadOnlyProperty **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumProperties(IEnumTfProperties **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateRangeBackup(TfEditCookie , ITfRange *, ITfRangeBackup **) override { return E_NOTIMPL; }
};
struct CompositionContextStub : ITfContextComposition {
    HRESULT STDMETHODCALLTYPE StartComposition(TfEditCookie , ITfRange *, ITfCompositionSink *, ITfComposition **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumCompositions(IEnumITfCompositionView **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindComposition(TfEditCookie , ITfRange *, IEnumITfCompositionView **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TakeOwnership(TfEditCookie , ITfCompositionView *, ITfCompositionSink *, ITfComposition **) override { return E_NOTIMPL; }
};
struct ThreadStub : ITfThreadMgr {
    HRESULT STDMETHODCALLTYPE Activate(TfClientId *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Deactivate() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateDocumentMgr(ITfDocumentMgr **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumDocumentMgrs(IEnumTfDocumentMgrs **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFocus(ITfDocumentMgr **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetFocus(ITfDocumentMgr *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AssociateFocus(HWND , ITfDocumentMgr *, ITfDocumentMgr **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsThreadFocus(BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFunctionProvider(REFCLSID , ITfFunctionProvider **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFunctionProviders(IEnumTfFunctionProviders **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetGlobalCompartment(ITfCompartmentMgr **) override { return E_NOTIMPL; }
};
struct KeystrokeStub : ITfKeystrokeMgr {
    HRESULT STDMETHODCALLTYPE AdviseKeyEventSink(TfClientId , ITfKeyEventSink *, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnadviseKeyEventSink(TfClientId) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetForeground(CLSID *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TestKeyDown(WPARAM , LPARAM , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TestKeyUp(WPARAM , LPARAM , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE KeyDown(WPARAM , LPARAM , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE KeyUp(WPARAM , LPARAM , BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPreservedKey(ITfContext *, const TF_PRESERVEDKEY *, GUID *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IsPreservedKey(REFGUID , const TF_PRESERVEDKEY *, BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE PreserveKey(TfClientId , REFGUID , const TF_PRESERVEDKEY *, const WCHAR *, ULONG) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnpreserveKey(REFGUID , const TF_PRESERVEDKEY *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPreservedKeyDescription(REFGUID , const WCHAR *, ULONG) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPreservedKeyDescription(REFGUID , BSTR *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SimulatePreservedKey(ITfContext *, REFGUID , BOOL *) override { return E_NOTIMPL; }
};
}
