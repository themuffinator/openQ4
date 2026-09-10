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

enum class WindowsTextCollectionKind { Pump, Lifecycle };
struct WindowsTextCollection {
	ui::NativeTextIdentity identity;
	std::uint64_t serial = 0, dispatch = 0;
	WindowsTextCollectionKind kind = WindowsTextCollectionKind::Pump;
	bool operator==(const WindowsTextCollection&) const = default;
};
struct WindowsTextCollectionReceipt {
	WindowsTextCollection collection;
	ui::NativeTextPendingSnapshot pending;
	std::uint64_t admittedCallbacks = 0;
	bool operator==(const WindowsTextCollectionReceipt&) const = default;
};
struct WindowsTextLifecycle {
	ui::NativeTextIdentity identity;
	std::uint64_t engineRevision = 0, shadowRevision = 0;
	std::uint64_t acknowledgedSequence = 0, acknowledgedShadowRevision = 0;
	std::uint64_t lastDispatch = 0, lastScopeSerial = 0, admittedCallbacks = 0;
	std::uint32_t pending = 0, retainedCompositions = 0, liveCompositions = 0;
	bool healthy = false, collectionOpen = false, renewalRequired = false, safeToRenew = false;
	WindowsTextCollection collection;
	bool operator==(const WindowsTextLifecycle&) const = default;
};

// STA-owned standalone store, not TSF activation or a native input route.
// The owner creates/binds/retires it on one thread. All COM calls except the
// atomic IUnknown refcount require that thread; final Release must also occur
// there because retained sink/context/composition references are apartment-bound.
// HWND is observational only. This object never calls a GUI, renderer, SDL,
// thread manager, clipboard, native window or message-pump API.
//
// Composition metadata inside an actual OnLockGranted write callback remains
// in that atomic transaction. Idle native composition callbacks append separate
// observed metadata offers, without inventing a lock or relabeling earlier text.
// OnEndEdit is a checked read-only receipt, not a completed collection/fence.
// Metadata during read/deferred/application-notification callbacks remains
// unsupported: Begin is declined; unrepresentable active Update/End retires.
// The owner must still supply the verified dispatch/collection boundary before
// editor settlement; composition End alone gives no accept/cancel authority.
// Failed callbacks/Finish/allocation retire the document: a pure rollback cannot
// undo changes already acknowledged synchronously to a text service.
// Native writes and composition metadata additionally require an explicit open
// collection. The descriptor binds callback lifetime, not a physical input or
// verified provider fence. Ordinary read locks remain available outside it.
// Outside collections, application notices permit reads but refuse all writes;
// lifecycle collections permit writes deferred until the whole notice returns.
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
	// Read-only copied queue boundary, with no sink calls or native fence proof.
	// The caller separately verifies provider health and the observed dispatch.
	HRESULT QueryPendingCollection(ui::NativeTextIdentity, std::uint64_t expectedEngineRevision,
		std::uint64_t expectedAcknowledgedSequence, std::uint64_t expectedAcknowledgedShadowRevision,
		std::uint64_t externallyObservedDispatch, ui::NativeTextPendingSnapshot& out) noexcept;
	HRESULT Acknowledge(ui::NativeTextIdentity, std::uint64_t transaction,
		std::uint64_t shadowAfter, std::uint64_t expectedEngineRevision,
		std::uint64_t acceptedEngineRevision) noexcept;
	HRESULT Reject(ui::NativeTextIdentity, std::uint64_t transaction,
		std::uint64_t expectedEngineRevision) noexcept;
	HRESULT Retire(ui::NativeTextIdentity) noexcept;
	// Open requires a fully acknowledged idle document, an exact editor barrier,
	// and a strictly newer nonzero dispatch. All outputs stay unchanged on failure.
	HRESULT OpenCollection(ui::NativeTextIdentity, std::uint64_t expectedEngineRevision,
		std::uint64_t expectedAcknowledgedSequence, std::uint64_t expectedAcknowledgedShadowRevision,
		std::uint64_t externallyObservedDispatch, WindowsTextCollectionKind,
		WindowsTextCollection& out) noexcept;
	// Close removes callback authority and copies the complete pending watermark.
	// No ACK, text insertion, provider receipt or editor settlement is performed.
	HRESULT CloseCollection(const WindowsTextCollection&, WindowsTextCollectionReceipt& out) noexcept;
	// Exact-scope abort retires without allocation, including during a callback.
	HRESULT AbortCollection(const WindowsTextCollection&) noexcept;
	// No callbacks or live references. Fails during callbacks/locks/notifications.
	// safeToRenew means native quiescence only: caller must also prove the editor
	// is settled and the provider fence is complete. Never clears retained IDs.
	HRESULT QueryLifecycle(ui::NativeTextIdentity, WindowsTextLifecycle& out) noexcept;
	// Observation only: does not open a collection or authorize later callbacks.
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
