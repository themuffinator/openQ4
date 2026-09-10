// Actual pure shadow-document methods; no COM, native input or engine state.
#include "ui/retained/NativeTextDocument.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace openq4::ui;
namespace {
int checks=0;
std::string error;
constexpr NativeTextIdentity Owner{7,11}, Other{7,12};
void Check(bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(std::string(message)+": "+error); }
void Open(NativeTextDocument& doc,std::string_view text="ab",std::size_t anchor=0,std::size_t caret=0) {
	Check(doc.Open(Owner,100,text,anchor,caret,error),"open");
}
NativeTextLockScope Lock(NativeTextDocument& doc,NativeTextAccess access=NativeTextAccess::ReadWrite) {
	NativeTextLockScope scope;
	Check(doc.RequestLock(Owner,access,true,scope,error)==NativeTextLockResult::Granted,"grant callback scope"); return scope;
}
std::uint64_t Finish(NativeTextDocument& doc,const NativeTextLockScope& scope,std::uint64_t dispatch=77) {
	std::uint64_t published=999;
	Check(doc.FinishLock(scope,dispatch,published,error),"finish callback scope"); return published;
}
NativeTextSnapshot State(NativeTextDocument& doc) {
	auto scope=Lock(doc,NativeTextAccess::Read); NativeTextSnapshot result;
	Check(doc.Read(scope,result,error),"locked read"); Check(Finish(doc,scope)==0,"read publishes nothing"); return result;
}
NativeTextOffer Offer(NativeTextDocument& doc) { NativeTextOffer offer; Check(doc.PeekOffer(Owner,offer,error),"front offer"); return offer; }
void Ack(NativeTextDocument& doc,std::uint64_t advance=1) {
	auto offer=Offer(doc);
	Check(doc.Acknowledge(Owner,offer.transaction.sequence,offer.transaction.shadowAfter,offer.expectedEngineRevision,
		offer.expectedEngineRevision+advance,error),"acknowledge front");
}
void Mapping() {
	std::vector<NativeTextBoundary> map{{99,99}};
	const std::string text="a\xce\xbb\xf0\x9f\x99\x82" "z";
	Check(BuildNativeTextMap(text,map,error),"strict mixed scalar map");
	Check(map==std::vector<NativeTextBoundary>{{0,0},{1,1},{3,2},{7,4},{8,5}},"exact byte and UTF16 endpoints");
	const auto saved=map;
	for (const auto& bad:std::vector<std::string>{std::string("a\0b",3),"\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82",std::string(65537,'a')})
		Check(!BuildNativeTextMap(bad,map,error)&&map==saved,"invalid complete UTF8 leaves map unchanged");
	Check(BuildNativeTextMap("",map,error)&&map==std::vector<NativeTextBoundary>{{0,0}},"empty endpoint");
	Check(BuildNativeTextMap("\1\n\xe2\x80\x8e",map,error)&&map.size()==4,"controls and bidi mark preserved without normalization");
	NativeTextDocument doc; Open(doc,text,8,1); auto lock=Lock(doc,NativeTextAccess::Read);
	std::size_t byte=99; std::uint32_t acp=99;
	Check(doc.AcpToByte(lock,4,byte,error)&&byte==7,"ACP after nonBMP");
	Check(!doc.AcpToByte(lock,3,byte,error)&&byte==7,"middle surrogate rejected atomically");
	Check(!doc.AcpToByte(lock,6,byte,error)&&byte==7,"out of range ACP rejected");
	Check(doc.ByteToAcp(lock,7,acp,error)&&acp==4,"byte after nonBMP");
	Check(!doc.ByteToAcp(lock,4,acp,error)&&acp==4,"interior UTF8 rejected atomically");
	Check(!doc.ReplaceACP(lock,0,1,"b",error),"read lock cannot edit");
	Check(Finish(doc,lock)==0,"refused write does not poison read scope");
	NativeTextSnapshot output; output.text="unchanged";
	Check(!doc.Read(lock,output,error)&&output.text=="unchanged","released scope cannot read");
}
void OpenAndScope() {
	NativeTextDocument doc;
	Check(!doc.Open({0,1},100,"ab",0,0,error)&&doc.Identity()==NativeTextIdentity{},"failed open preserves empty identity");
	Check(!doc.Open(Owner,100,"\xf0\x9f\x99\x82",1,4,error)&&!doc.Active(),"initial interior byte selection refused");
	Open(doc); Check(doc.ShadowRevision()==1&&doc.EngineRevision()==100,"distinct initial revisions");
	Check(!doc.Open(Other,101,"other",0,0,error)&&doc.Identity()==Owner,"immutable one-shot identity");
	auto scope=Lock(doc); NativeTextLockScope output{{99,99},99,NativeTextAccess::Read}; const auto original=output;
	Check(doc.RequestLock(Other,NativeTextAccess::Read,true,output,error)==NativeTextLockResult::Refused&&output==original,"wrong owner cannot get or leak lock");
	Check(doc.RequestLock(Owner,static_cast<NativeTextAccess>(99),true,output,error)==NativeTextLockResult::Refused&&output==original,"invalid lock access unchanged");
	Check(doc.RequestLock(Owner,NativeTextAccess::ReadWrite,true,output,error)==NativeTextLockResult::Refused&&output==original,"nested synchronous lock refused");
	std::uint64_t published=999; auto bad=scope; bad.identity=Other;
	Check(!doc.FinishLock(bad,1,published,error)&&published==999,"wrong owner cannot finish callback");
	Check(!doc.AbortLock(bad,error),"wrong owner cannot abort callback");
	Check(doc.ReplaceACP(scope,0,1,"x",error),"valid scope survives foreign calls");
	Check(Finish(doc,scope)==1,"first actual transaction identity");
	auto next=Lock(doc); Check(next.serial!=scope.serial,"callback serial is not reused");
	Check(!doc.AbortLock(scope,error),"old scope cannot release a new callback");
	Check(!doc.SelectACP(scope,0,0,error),"old scope cannot edit new callback");
	Check(Finish(doc,next)==0,"new callback survives stale callers");
	NativeTextOffer hidden; hidden.expectedEngineRevision=777;
	Check(!doc.PeekOffer(Other,hidden,error)&&hidden.expectedEngineRevision==777,"wrong owner cannot read pending offer");
}
void AtomicTransactions() {
	NativeTextDocument doc; Open(doc,"ab",2,0); const auto baseline=State(doc);
	auto scope=Lock(doc);
	Check(doc.ReplaceACP(scope,0,1,"\xf0\x9f\x99\x82",error),"first staged valid nonBMP replacement");
	NativeTextSnapshot staged; Check(doc.Read(scope,staged,error)&&staged.text=="\xf0\x9f\x99\x82" "b","TSF reads its own staged write");
	Check(!doc.ReplaceACP(scope,1,2,"bad",error),"invalid middle replacement poisons candidate");
	Check(!doc.SelectACP(scope,0,0,error),"poisoned candidate cannot continue");
	staged.text="sentinel"; Check(!doc.Read(scope,staged,error)&&staged.text=="sentinel","poisoned candidate cannot leak partial readback");
	std::uint64_t published=99; Check(!doc.FinishLock(scope,1,published,error)&&published==99,"failed transaction publishes no receipt");
	Check(doc.ShadowRevision()==1&&doc.PendingCount()==0&&State(doc)==baseline,"invalid middle restores whole original document");
	scope=Lock(doc); Check(doc.ReplaceACP(scope,0,1,"\xf0\x9f\x99\x82",error),"replacement retried without consuming identity");
	Check(doc.ReplaceACP(scope,2,3,"xy",error),"second replacement uses updated ACP coordinates");
	Check(doc.SelectACP(scope,4,0,error),"reversed native selection preserved");
	Check(Finish(doc,scope,91)==1,"first publish after failed candidate");
	auto offer=Offer(doc);
	Check(offer.transaction.after.text=="\xf0\x9f\x99\x82xy"&&offer.transaction.after.anchor==6&&offer.transaction.after.caret==0,"complete ordered range result");
	Check(offer.transaction.operations.size()==3&&offer.transaction.operations[1].first==2&&offer.transaction.nativeDispatch==91,"ordered operations and dispatch identity retained");
	Check(offer.transaction.classification==NativeTextClassification::Unclassified,"noncomposition write is not invented Direct");
	Check(doc.EngineRevision()==100&&doc.ShadowRevision()==2,"native publication does not advance engine revision");
	const auto unchanged=offer; scope=Lock(doc); Check(doc.SelectACP(scope,0,0,error),"later candidate changes selection");
	Check(doc.AbortLock(scope,error),"explicit abort"); Check(Offer(doc)==unchanged,"aborting later candidate preserves published offer bytes");
}
void CompositionClassification() {
	NativeTextDocument doc; Open(doc,"ab",1,1); auto scope=Lock(doc);
	Check(doc.ReplaceACP(scope,1,1,"\xce\xbb",error),"text inserted before composition notification");
	Check(doc.BeginComposition(scope,10,1,2,error),"later Begin names inserted range");
	Check(doc.UpdateComposition(scope,10,0,2,error),"composition range update ordered");
	Check(Finish(doc,scope)==1,"composition transaction published at boundary");
	auto first=Offer(doc);
	Check(first.transaction.classification==NativeTextClassification::CompositionRelated,"insertion classified only after later Begin");
	Check(first.transaction.after.compositions.at(10)==NativeTextRange{0,3},"composition range projected to UTF8");
	Check(first.transaction.operations.front().kind==NativeTextOperationKind::Replace&&first.transaction.operations[1].kind==NativeTextOperationKind::BeginComposition,"no early insertion promotion");
	Ack(doc); scope=Lock(doc);
	Check(doc.ReplaceACP(scope,1,2,"\xf0\x9f\x99\x82",error),"active composition replacement");
	NativeTextSnapshot changed; Check(doc.Read(scope,changed,error)&&changed.compositions.at(10)==NativeTextRange{0,5},"tracked range follows replacement");
	Check(doc.SelectACP(scope,3,3,error),"selection after replacement");
	Check(doc.EndComposition(scope,10,error),"composition end remains metadata");
	Finish(doc,scope); Check(Offer(doc).transaction.classification==NativeTextClassification::CompositionRelated,"End does not erase transaction classification"); Ack(doc);
	scope=Lock(doc); Check(!doc.BeginComposition(scope,10,0,0,error),"retired composition ID never reused"); Check(doc.AbortLock(scope,error),"abort failed composition candidate");
	scope=Lock(doc); Check(!doc.EndComposition(scope,9,error),"late unknown composition cannot attach"); Check(doc.AbortLock(scope,error),"abort late composition");
	scope=Lock(doc); Check(doc.BeginComposition(scope,11,0,0,error)&&doc.EndComposition(scope,11,error),"begin and end can be metadata only");
	Finish(doc,scope); const auto meta=Offer(doc); Check(!meta.transaction.documentChanged,"metadata only is distinct from document mutation"); Ack(doc,0);
	scope=Lock(doc); Check(doc.BeginComposition(scope,12,0,0,error)&&doc.EndComposition(scope,12,error),"later metadata transaction"); Finish(doc,scope); Ack(doc,4);
}
void AcknowledgementAndSync() {
	NativeTextDocument doc; Open(doc);
	auto scope=Lock(doc); Check(doc.ReplaceACP(scope,0,1,"x",error),"first pending edit"); Finish(doc,scope); const auto first=Offer(doc);
	scope=Lock(doc); Check(doc.ReplaceACP(scope,1,2,"y",error),"second pending edit before acknowledgement"); Finish(doc,scope);
	Check(doc.PendingCount()==2&&Offer(doc)==first,"front immutable while later native writes queue");
	const auto bytes=doc.PendingBytes();
	Check(!doc.Acknowledge(Owner,999,first.transaction.shadowAfter,100,101,error),"wrong transaction cannot acknowledge front");
	Check(!doc.Acknowledge(Owner,1,999,100,101,error),"wrong shadow revision cannot acknowledge front");
	Check(!doc.Acknowledge(Owner,1,2,99,101,error),"wrong expected engine revision refused");
	Check(!doc.Acknowledge(Other,1,2,100,101,error),"wrong acknowledgement owner refused");
	Check(!doc.Acknowledge(Owner,1,2,100,100,error),"changed text cannot acknowledge unchanged engine revision");
	Check(!doc.Acknowledge(Owner,1,2,100,99,error),"acknowledgement cannot regress engine revision");
	Check(doc.PendingCount()==2&&doc.PendingBytes()==bytes&&Offer(doc)==first,"invalid acknowledgements are atomic");
	Check(!doc.SyncEngine(Owner,100,3,101,"foreign",0,0,error),"engine sync refuses pending native dependencies");
	Check(doc.Acknowledge(Owner,1,2,100,107,error),"first exact acknowledgement advances engine independently");
	auto second=Offer(doc);
	Check(second.transaction.sequence==2&&second.transaction.shadowBefore==2&&second.transaction.shadowAfter==3&&second.expectedEngineRevision==107,"FIFO front chains acknowledged engine revision");
	Check(second.transaction.after.text=="xy","queued native result stays immutable");
	Check(!doc.Acknowledge(Owner,1,2,100,108,error)&&Offer(doc)==second,"stale ack cannot clear later pending edit");
	Ack(doc,2); Check(doc.PendingCount()==0&&doc.PendingBytes()==0&&doc.EngineRevision()==109,"final ack releases accounted pending bytes");
	Check(!doc.SyncEngine(Owner,109,2,110,"new",0,0,error),"sync rejects stale shadow revision");
	Check(!doc.SyncEngine(Owner,108,3,110,"new",0,0,error),"sync rejects stale engine revision");
	Check(!doc.SyncEngine(Owner,109,3,109,"new",0,0,error),"changed engine buffer needs new revision");
	Check(doc.SyncEngine(Owner,109,3,110,"new",3,0,error)&&doc.ShadowRevision()==4,"valid exact engine sync advances shadow independently");
	Check(doc.SyncEngine(Owner,110,4,111,"new",3,0,error)&&doc.ShadowRevision()==4,"metadata engine revision does not fake a shadow text change");
	scope=Lock(doc); Check(doc.SelectACP(scope,3,0,error),"same selection staged"); Check(Finish(doc,scope)==0&&doc.PendingCount()==0,"no-op lock publishes nothing");
}
void DeferredAndRetirement() {
	NativeTextDocument doc; Open(doc); auto read=Lock(doc,NativeTextAccess::Read);
	NativeTextLockScope queued{{99,99},99,NativeTextAccess::Read}; const auto unchanged=queued;
	Check(doc.RequestLock(Owner,NativeTextAccess::ReadWrite,false,queued,error)==NativeTextLockResult::Deferred&&queued==unchanged,"asynchronous upgrade queued without new callback");
	Check(!doc.GrantDeferredWrite(Owner,queued,error)&&queued==unchanged,"upgrade cannot enter until original callback ends");
	Check(!doc.GrantDeferredWrite(Other,queued,error)&&queued==unchanged,"deferred write belongs original identity");
	Finish(doc,read);
	Check(doc.RequestLock(Owner,NativeTextAccess::Read,true,queued,error)==NativeTextLockResult::Refused,"ordinary request cannot steal queued upgrade");
	Check(doc.GrantDeferredWrite(Owner,queued,error)&&queued.serial!=read.serial&&queued.access==NativeTextAccess::ReadWrite,"fresh write callback granted after read exits");
	Check(!doc.AbortLock(read,error),"late read callback cannot abort upgraded write");
	Check(doc.ReplaceACP(queued,0,1,"x",error),"deferred write edits"); Finish(doc,queued);
	auto lock=Lock(doc); Check(doc.ReplaceACP(lock,1,2,"y",error),"pending candidate before retirement");
	Check(!doc.Retire(Other,error)&&doc.Active(),"wrong retirement owner cannot cancel work");
	Check(doc.Retire(Owner,error)&&!doc.Active()&&doc.PendingCount()==0&&doc.PendingBytes()==0,"retirement drops candidate and dependent work");
	std::uint64_t receipt=999; Check(!doc.FinishLock(lock,1,receipt,error)&&receipt==999,"late callback after retirement cannot publish");
	Check(!doc.GrantDeferredWrite(Owner,queued,error),"retired document cannot grant callback");
	Check(doc.RequestLock(Owner,NativeTextAccess::Read,true,queued,error)==NativeTextLockResult::Refused,"retired document cannot reopen native reads");
	Check(!doc.Open(Other,1,"other",0,0,error)&&doc.Identity()==Owner,"retired document identity never rebound");
	Check(doc.Retire(Owner,error),"matching retirement idempotent");
	NativeTextDocument pending; Open(pending); auto a=Lock(pending); Check(pending.SelectACP(a,1,1,error),"first rejection dependency"); Finish(pending,a);
	a=Lock(pending); Check(pending.SelectACP(a,2,2,error),"second rejection dependency"); Finish(pending,a);
	Check(!pending.Reject(Owner,2,100,error)&&pending.Active()&&pending.PendingCount()==2,"only FIFO offer may be rejected");
	Check(pending.Reject(Owner,1,100,error)&&!pending.Active()&&pending.PendingCount()==0,"rejection retires all dependent native work");
	NativeTextDocument deferred; Open(deferred); a=Lock(deferred,NativeTextAccess::Read);
	Check(deferred.RequestLock(Owner,NativeTextAccess::ReadWrite,false,queued,error)==NativeTextLockResult::Deferred,"queued upgrade before retirement");
	Check(deferred.Retire(Owner,error)&&!deferred.GrantDeferredWrite(Owner,queued,error),"retirement cancels deferred ownership too");
}
void Budgets() {
	NativeTextLimits invalid; invalid.operations=257; NativeTextDocument tooLarge(invalid);
	Check(!tooLarge.Open(Owner,100,"a",0,0,error),"host cannot make operation budget unbounded");
	NativeTextLimits limits; limits.operations=2; NativeTextDocument operations(limits); Open(operations); auto scope=Lock(operations);
	Check(operations.SelectACP(scope,1,1,error)&&operations.SelectACP(scope,0,0,error),"two operations permitted");
	Check(!operations.SelectACP(scope,2,2,error),"even noop churn consumes operation bound");
	std::uint64_t published=99; Check(!operations.FinishLock(scope,1,published,error)&&published==99&&operations.PendingCount()==0,"operation overflow cannot publish partial transaction");
	limits={}; limits.retainedCompositions=1; NativeTextDocument ids(limits); Open(ids);
	scope=Lock(ids); Check(ids.BeginComposition(scope,1,0,0,error)&&ids.EndComposition(scope,1,error),"one ended identity in candidate");
	Check(!ids.BeginComposition(scope,2,0,0,error),"begin/end churn cannot bypass retained identity bound"); Check(ids.AbortLock(scope,error),"discard identity-overbudget candidate");
	for (std::uint64_t token=1;token<=80;++token) {
		scope=Lock(ids); Check(ids.BeginComposition(scope,token,0,0,error)&&ids.EndComposition(scope,token,error),"acknowledged lifetime allows next fresh identity"); Finish(ids,scope);
		auto blocked=Lock(ids); Check(!ids.BeginComposition(blocked,token+1,0,0,error),"pending ended ID remains budgeted"); Check(ids.AbortLock(blocked,error),"abort pending identity refusal"); Ack(ids,0);
	}
	limits={}; limits.pendingTransactions=1; NativeTextDocument queue(limits); Open(queue);
	scope=Lock(queue); Check(queue.SelectACP(scope,1,1,error),"queue first selection"); Finish(queue,scope); const auto before=Offer(queue);
	scope=Lock(queue); Check(queue.SelectACP(scope,2,2,error),"staged edit before queue refusal");
	Check(!queue.FinishLock(scope,1,published,error)&&queue.ShadowRevision()==2&&Offer(queue)==before,"full queue refuses new state atomically");
	limits={}; limits.pendingBytes=1024; NativeTextDocument bytes(limits); Open(bytes,std::string(500,'a'));
	scope=Lock(bytes); Check(bytes.SelectACP(scope,1,1,error),"bounded first large snapshot"); Finish(bytes,scope); const auto firstBytes=bytes.PendingBytes();
	Check(firstBytes>0&&firstBytes<=1024,"pending snapshot and operation payload accounted");
	scope=Lock(bytes); Check(bytes.SelectACP(scope,2,2,error),"second candidate within individual budget");
	Check(!bytes.FinishLock(scope,1,published,error)&&bytes.PendingBytes()==firstBytes&&bytes.PendingCount()==1,"aggregate queued byte bound refuses second complete snapshot");
	limits={}; limits.documentBytes=4; NativeTextDocument document(limits); Open(document,"abcd"); scope=Lock(document);
	Check(!document.ReplaceACP(scope,0,0,"x",error)&&!document.FinishLock(scope,1,published,error),"document growth refusal atomic");
	Check(State(document).text=="abcd","document bound preserved original text");
	limits={}; limits.sequence=2; NativeTextDocument sequence(limits); Open(sequence); scope=Lock(sequence);
	Check(sequence.SelectACP(scope,1,1,error),"last available shadow revision candidate"); Finish(sequence,scope); Ack(sequence);
	scope=Lock(sequence); Check(sequence.SelectACP(scope,2,2,error),"candidate at exhausted revision");
	Check(!sequence.FinishLock(scope,1,published,error)&&sequence.ShadowRevision()==2&&sequence.PendingCount()==0,"shadow sequence does not wrap");
	NativeTextLockScope unchanged{{99,99},99,NativeTextAccess::Read}, next=unchanged;
	Check(sequence.RequestLock(Owner,NativeTextAccess::Read,true,next,error)==NativeTextLockResult::Refused&&next==unchanged,"callback serial lifetime bound never reuses identity");
}
} // namespace
int main() { try {
	Mapping(); OpenAndScope(); AtomicTransactions(); CompositionClassification(); AcknowledgementAndSync(); DeferredAndRetirement(); Budgets();
	std::cout<<"PASS "<<checks<<" checks\n"; return 0;
} catch (const std::exception& e) { std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n'; return 1; } }
