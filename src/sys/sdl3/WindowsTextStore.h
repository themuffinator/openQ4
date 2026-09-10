// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

// Engine-private Windows SDK boundary. SDL neither includes nor links this object.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <textstor.h>
#include <msctf.h>
#include <olectl.h>
#include "../../ui/retained/NativeTextDocument.h"
#include <memory>

namespace openq4::sys {
struct WindowsTextCell {
	LONG first = 0, last = 0; // Complete UTF-16 scalar interval.
	RECT ink{};              // Already clipped screen-space bounds.
	POINT before{}, after{}; // Supplied visual boundaries; no shaping inference.
	bool clipped = false;
};
struct WindowsTextLayout {
	ui::NativeTextIdentity identity;
	std::uint64_t engineRevision = 0, shadowRevision = 0, layoutRevision = 0;
	std::string text;
	RECT screen{};
	bool visible = false;
	std::vector<WindowsTextCell> cells;
};

// STA-owned standalone store, not TSF activation or a native input route.
// The owner creates/binds/retires it on one thread. All COM calls except the
// atomic IUnknown refcount require that thread; final Release must also occur
// there because retained sink/context/composition references are apartment-bound.
// HWND is observational only. This object never calls a GUI, renderer, SDL,
// thread manager, clipboard, native window or message-pump API.
//
// This initial adapter accepts composition mutations only in OnLockGranted's
// write callback. OnEndEdit is a checked read-only receipt, not a publication
// barrier or permission to change/reclassify an already published transaction.
// Out-of-lock Begin is declined; unexpected active Update/End faults and retires
// the store. Arbitrary multi-lock TSF edit-session aggregation remains unsupported.
// Failed callbacks/Finish/allocation retire the document: a pure rollback cannot
// undo changes already acknowledged synchronously to a text service.
class WindowsTextStore final : public ITextStoreACP,
	public ITfContextOwnerCompositionSink, public ITfTextEditSink {
public:
	static HRESULT Create(ui::NativeTextIdentity, std::uint64_t engineRevision,
		std::string_view, std::size_t anchor, std::size_t caret, HWND,
		WindowsTextStore** out, ui::NativeTextLimits limits = {}) noexcept;
	HRESULT BindContext(ui::NativeTextIdentity, IUnknown*) noexcept;
	HRESULT SetLayout(const WindowsTextLayout&) noexcept;
	// Application-origin snapshot only: never use this to acknowledge native
	// edits. Requires no pending offer/lock/composition and exact revisions.
	// Text/selection notifications allow reads; asynchronous writes wait until
	// the complete notification batch has returned. Callback failure retires.
	HRESULT SyncEngine(ui::NativeTextIdentity, std::uint64_t expectedEngineRevision,
		std::uint64_t expectedShadowRevision, std::uint64_t engineRevision,
		std::string_view text, std::size_t anchor, std::size_t caret) noexcept;
	HRESULT Peek(ui::NativeTextIdentity, ui::NativeTextOffer&) noexcept;
	HRESULT Acknowledge(ui::NativeTextIdentity, std::uint64_t transaction,
		std::uint64_t shadowAfter, std::uint64_t expectedEngineRevision,
		std::uint64_t acceptedEngineRevision) noexcept;
	HRESULT Reject(ui::NativeTextIdentity, std::uint64_t transaction,
		std::uint64_t expectedEngineRevision) noexcept;
	HRESULT Retire(ui::NativeTextIdentity) noexcept;
	// Observational dispatch ID: no proof of a native fence or physical input.
	HRESULT SetDispatch(ui::NativeTextIdentity, std::uint64_t) noexcept;
	bool Healthy() const noexcept;
	std::uint64_t EditReceipts() const noexcept;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;
	HRESULT STDMETHODCALLTYPE AdviseSink(REFIID, IUnknown*, DWORD) override;
	HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown*) override;
	HRESULT STDMETHODCALLTYPE RequestLock(DWORD, HRESULT*) override;
	HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS*) override;
	HRESULT STDMETHODCALLTYPE QueryInsert(LONG, LONG, ULONG, LONG*, LONG*) override;
	HRESULT STDMETHODCALLTYPE GetSelection(ULONG, ULONG, TS_SELECTION_ACP*, ULONG*) override;
	HRESULT STDMETHODCALLTYPE SetSelection(ULONG, const TS_SELECTION_ACP*) override;
	HRESULT STDMETHODCALLTYPE GetText(LONG, LONG, WCHAR*, ULONG, ULONG*, TS_RUNINFO*, ULONG, ULONG*, LONG*) override;
	HRESULT STDMETHODCALLTYPE SetText(DWORD, LONG, LONG, const WCHAR*, ULONG, TS_TEXTCHANGE*) override;
	HRESULT STDMETHODCALLTYPE GetFormattedText(LONG, LONG, IDataObject**) override;
	HRESULT STDMETHODCALLTYPE GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) override;
	HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL*) override;
	HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override;
	HRESULT STDMETHODCALLTYPE InsertTextAtSelection(DWORD, const WCHAR*, ULONG, LONG*, LONG*, TS_TEXTCHANGE*) override;
	HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override;
	HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) override;
	HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override;
	HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override;
	HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG, LONG, ULONG, const TS_ATTRID*, DWORD, LONG*, BOOL*, LONG*) override;
	HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG*) override;
	HRESULT STDMETHODCALLTYPE GetEndACP(LONG*) override;
	HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie*) override;
	HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) override;
	HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie, LONG, LONG, RECT*, BOOL*) override;
	HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie, RECT*) override;
	HRESULT STDMETHODCALLTYPE GetWnd(TsViewCookie, HWND*) override;
	HRESULT STDMETHODCALLTYPE OnStartComposition(ITfCompositionView*, BOOL*) override;
	HRESULT STDMETHODCALLTYPE OnUpdateComposition(ITfCompositionView*, ITfRange*) override;
	HRESULT STDMETHODCALLTYPE OnEndComposition(ITfCompositionView*) override;
	HRESULT STDMETHODCALLTYPE OnEndEdit(ITfContext*, TfEditCookie, ITfEditRecord*) override;
private:
	HRESULT Notify(DWORD mask, const TS_TEXTCHANGE* change);
	friend struct std::default_delete<WindowsTextStore>;
	struct Impl;
	explicit WindowsTextStore(ui::NativeTextLimits);
	~WindowsTextStore();
	std::unique_ptr<Impl> impl;
	template<class F> HRESULT Safe(F&&) noexcept;
};
} // namespace openq4::sys
#endif
