// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "ManagedNativeTextOwner.h"
namespace openq4::ui {
bool ManagedNativeTextOwner::Attach(const TextEditorIdentity& owner,NativeTextIdentity native,NativeTextEditorBarrier& out,std::string& error) {
	return OnThread() && UI_NativeTextAttach(probe,context,owner,native,out,error);
}
bool ManagedNativeTextOwner::Refresh(const NativeTextEditorBarrier& expected,NativeTextEditorView& out,std::string& error) {
	return OnThread() && UI_NativeTextRefresh(probe,context,expected,out,error);
}
bool ManagedNativeTextOwner::Current(const NativeTextEditorBarrier& expected) const noexcept {
	return OnThread() && UI_NativeTextCurrent(probe,context,expected);
}
bool ManagedNativeTextOwner::Begin(const NativeTextEditorBarrier& expected,const NativeTextCollection& collection,NativeTextEditorBarrier& out,std::string& error) {
	return OnThread() && UI_NativeTextBegin(probe,context,expected,collection,out,error);
}
bool ManagedNativeTextOwner::Apply(const NativeTextEditorBarrier& expected,const NativeTextOffer& offer,NativeTextEditorReceipt& out,std::string& error) {
	return OnThread() && UI_NativeTextApply(probe,context,expected,offer,out,error);
}
bool ManagedNativeTextOwner::Complete(const NativeTextEditorBarrier& expected,const NativeTextCollection& collection,NativeTextEditorBarrier& out,std::string& error) {
	return OnThread() && UI_NativeTextComplete(probe,context,expected,collection,out,error);
}
std::unique_ptr<Interaction::NativeSettlement> ManagedNativeTextOwner::PrepareSettlement(const NativeTextEditorBarrier& expected,std::string& error) {
	return OnThread()?UI_NativeTextPrepareSettlement(probe,context,expected,error):nullptr;
}
bool ManagedNativeTextOwner::PublishSettlement(Interaction::NativeSettlement& prepared,NativeTextEditorReceipt& out) noexcept {
	return OnThread() && UI_NativeTextPublishSettlement(probe,context,prepared,out);
}
void ManagedNativeTextOwner::RetireExact(const NativeTextIdentity& native,const TextEditorIdentity& owner) noexcept {
	if(OnThread())(void)UI_NativeTextRetireExact(native,owner);
}
} // namespace openq4::ui
