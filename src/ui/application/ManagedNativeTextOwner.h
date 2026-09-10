// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "NativeTextCollectionCoordinator.h"
#include "../UserInterfaceNativeText.h"

namespace openq4::ui {
// Engine adapter for one constructing thread. It owns no GUI/backend pointer,
// native store or Session route; every endpoint re-observes the injected route
// and manager allocation registry. The engine supplies the route probe when it
// eventually integrates native activation. This class does not grant it.
class ManagedNativeTextOwner final : public NativeTextCollectionOwner {
public:
	ManagedNativeTextOwner(uiNativeTextRouteProbe_t source,void* user) noexcept : probe(source),context(user) {}
	bool Attach(const TextEditorIdentity&,NativeTextIdentity,NativeTextEditorBarrier&,std::string&);
	bool Refresh(const NativeTextEditorBarrier&,NativeTextEditorView&,std::string&) override;
	bool Current(const NativeTextEditorBarrier&) const noexcept override;
	bool Begin(const NativeTextEditorBarrier&,const NativeTextCollection&,NativeTextEditorBarrier&,std::string&) override;
	bool Apply(const NativeTextEditorBarrier&,const NativeTextOffer&,NativeTextEditorReceipt&,std::string&) override;
	bool Complete(const NativeTextEditorBarrier&,const NativeTextCollection&,NativeTextEditorBarrier&,std::string&) override;
	std::unique_ptr<Interaction::NativeSettlement> PrepareSettlement(const NativeTextEditorBarrier&,std::string&) override;
	bool PublishSettlement(Interaction::NativeSettlement&,NativeTextEditorReceipt&) noexcept override;
	void RetireExact(const NativeTextIdentity&,const TextEditorIdentity&) noexcept override;
private:
	bool OnThread() const noexcept { return std::this_thread::get_id()==thread; }
	uiNativeTextRouteProbe_t probe;
	void* context;
	const std::thread::id thread=std::this_thread::get_id();
};
} // namespace openq4::ui
