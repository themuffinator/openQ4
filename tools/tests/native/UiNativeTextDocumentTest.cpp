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
void PendingCollectionWatermarks() {
	NativeTextDocument doc;NativeTextPendingSnapshot out{{99,98},97,96,95,94,93,92,91};const auto sentinel=out;
	Check(!doc.QueryPendingCollection(Owner,100,0,1,77,out,error)&&out==sentinel,"unopened query leaves output untouched");
	Open(doc);
	const NativeTextDocument& readOnly=doc;
	Check(readOnly.QueryPendingCollection(Owner,100,0,1,77,out,error),"empty read-only pending query");
	Check((out==NativeTextPendingSnapshot{Owner,100,0,1,1,77,0,0}),"empty query exact copied values");
	for(int field=0;field<5;++field) {
		out=sentinel;auto id=Owner;std::uint64_t revision=100,sequence=0,shadow=1,dispatch=77;
		if(field==0)id=Other;
		if(field==1)revision=101;
		if(field==2)sequence=1;
		if(field==3)shadow=2;
		if(field==4)dispatch=0;
		Check(!doc.QueryPendingCollection(id,revision,sequence,shadow,dispatch,out,error)&&out==sentinel,"empty query checks full owner and ACKed state");
	}
	auto scope=Lock(doc);Check(doc.SelectACP(scope,1,1,error),"first queued selection");Finish(doc,scope,77);
	scope=Lock(doc);Check(doc.SelectACP(scope,2,2,error),"second queued selection");Finish(doc,scope,77);
	const auto bytes=doc.PendingBytes();const auto copied=Offer(doc);NativeTextPendingSnapshot saved;
	Check(doc.QueryPendingCollection(Owner,100,0,1,77,saved,error),"copied immutable offer does not create a lease");
	Check((saved==NativeTextPendingSnapshot{Owner,100,0,1,3,77,2,2}),"all pending offers observed before first ACK");
	Check(Offer(doc)==copied && doc.PendingCount()==2 && doc.PendingBytes()==bytes && doc.EngineRevision()==100 && doc.ShadowRevision()==3,"query did not offer, ACK, rebind or advance model");
	out=sentinel;Check(!doc.QueryPendingCollection(Owner,101,1,2,77,out,error)&&out==sentinel,"applied but unacknowledged editor cannot query advanced state");
	Ack(doc);Check(doc.QueryPendingCollection(Owner,101,1,2,77,out,error),"query after exact first ACK");
	Check((out==NativeTextPendingSnapshot{Owner,101,1,2,3,77,2,1}),"remaining queue retains exact final watermark");
	Check(saved.count==2 && saved.acknowledgedSequence==0 && copied.transaction.sequence==1,"copied query and offer remain immutable after ACK");
	out=sentinel;Check(!doc.QueryPendingCollection(Owner,100,0,1,77,out,error)&&out==sentinel,"old ACKed barrier refused after progress");
	Ack(doc);Check(doc.QueryPendingCollection(Owner,102,2,3,999,out,error)&&out.count==0&&out.lastSequence==2,"empty query accepts observational dispatch without claiming participation");
	Check(doc.SyncEngine(Owner,102,3,103,"xyz",2,0,error),"application shadow synchronization");
	out=sentinel;Check(!doc.QueryPendingCollection(Owner,103,2,3,77,out,error)&&out==sentinel,"app synchronization invalidates old ACKed shadow");
	Check(doc.QueryPendingCollection(Owner,103,2,4,77,out,error)&&out.shadowRevision==4&&out.acknowledgedSequence==2,"shadow progress is independent from transaction sequence");
	Check(doc.Retire(Owner,error),"retire queried document");out=sentinel;
	Check(!doc.QueryPendingCollection(Owner,103,2,4,77,out,error)&&out==sentinel,"retired document query refused");
	NativeTextDocument mixed;Open(mixed);scope=Lock(mixed);Check(mixed.SelectACP(scope,1,1,error),"first mixed dispatch");Finish(mixed,scope,77);
	scope=Lock(mixed);Check(mixed.SelectACP(scope,2,2,error),"second mixed dispatch");Finish(mixed,scope,78);
	for(auto dispatch:{77u,78u}) {out=sentinel;Check(!mixed.QueryPendingCollection(Owner,100,0,1,dispatch,out,error)&&out==sentinel,"mixed pending dispatch rejected unchanged");}
	Ack(mixed);Check(mixed.QueryPendingCollection(Owner,101,1,2,78,out,error)&&out.count==1,"next homogeneous queue matches new acknowledged barrier");
	NativeTextDocument zero;Open(zero);scope=Lock(zero);Check(zero.SelectACP(scope,1,1,error),"unscoped queued transaction");Finish(zero,scope,0);out=sentinel;
	Check(!zero.QueryPendingCollection(Owner,100,0,1,77,out,error)&&out==sentinel,"zero-dispatch offer cannot borrow external collection identity");
}
void PendingCollectionLocksAndBounds() {
	NativeTextDocument doc;Open(doc);NativeTextPendingSnapshot out{{9,8},7,6,5,4,3,2,1};const auto sentinel=out;
	auto scope=Lock(doc,NativeTextAccess::Read);NativeTextLockScope deferred;
	Check(!doc.QueryPendingCollection(Owner,100,0,1,77,out,error)&&out==sentinel,"query refuses active read callback");
	Check(doc.RequestLock(Owner,NativeTextAccess::ReadWrite,false,deferred,error)==NativeTextLockResult::Deferred,"defer native write callback");Finish(doc,scope);
	Check(!doc.QueryPendingCollection(Owner,100,0,1,77,out,error)&&out==sentinel,"query refuses deferred write before grant");
	Check(doc.GrantDeferredWrite(Owner,deferred,error),"grant deferred callback");
	Check(!doc.QueryPendingCollection(Owner,100,0,1,77,out,error)&&out==sentinel,"query refuses active deferred write callback");
	Check(doc.SelectACP(deferred,1,1,error),"query failure did not poison candidate");Finish(doc,deferred,77);
	Check(doc.QueryPendingCollection(Owner,100,0,1,77,out,error)&&out.count==1,"query after completed write callback");
	NativeTextDocument full;Open(full);
	for(unsigned i=0;i<32;++i){scope=Lock(full);Check(full.SelectACP(scope,i%2?0:1,i%2?0:1,error),"bounded queued selection");Finish(full,scope,99);}
	Check(full.QueryPendingCollection(Owner,100,0,1,99,out,error)&&out.count==32&&out.lastSequence==32&&out.shadowRevision==33,"maximum32 copied queue count");
	scope=Lock(full);Check(full.SelectACP(scope,1,1,error),"overflow candidate");std::uint64_t receipt=88;
	Check(!full.FinishLock(scope,99,receipt,error)&&receipt==88,"producer refuses33rd pending offer");
	Check(full.QueryPendingCollection(Owner,100,0,1,99,out,error)&&out.count==32,"failed overflow preserves query watermark");
	NativeTextLimits limits;limits.sequence=1;NativeTextDocument exhausted(limits);Open(exhausted);
	for(unsigned i=0;i<16;++i)Check(exhausted.QueryPendingCollection(Owner,100,0,1,55,out,error)&&out.count==0,"read-only query consumes no callback/revision serial");
	scope=Lock(exhausted,NativeTextAccess::Read);Finish(exhausted,scope);
	NativeTextLockScope another;Check(exhausted.RequestLock(Owner,NativeTextAccess::Read,true,another,error)==NativeTextLockResult::Refused,"single callback budget exhausted");
	Check(exhausted.QueryPendingCollection(Owner,100,0,1,55,out,error)&&out.count==0,"read-only query available at exhausted lock budget");
}
NativeTextMetadataObservation Observe(NativeTextDocument& doc,std::uint64_t dispatch=77) {
	NativeTextMetadataObservation result;Check(doc.CaptureCompositionObservation(Owner,dispatch,result,error),"capture native composition observation");return result;
}
std::uint64_t Metadata(NativeTextDocument& doc,NativeTextOperationKind kind,std::uint64_t token,std::uint32_t first=0,std::uint32_t last=0,std::uint64_t dispatch=77) {
	const auto observation=Observe(doc,dispatch);std::uint64_t published=999;
	Check(doc.PublishCompositionObservation(observation,{kind,first,last,{},token},published,error),"publish observed composition metadata");return published;
}
void ObservedCompositionLifecycle() {
	NativeTextDocument doc;Open(doc,"A\xf0\x9f\x98\x80Z",6,1);
	const auto before=Observe(doc);Check((before==NativeTextMetadataObservation{Owner,100,1,0,0,77}),"complete initial copied observation");
	Check(Metadata(doc,NativeTextOperationKind::BeginComposition,1,1,3)==1,"idle Begin gets first published identity");
	const auto first=Offer(doc);Check(!first.transaction.documentChanged && first.transaction.after.text=="A\xf0\x9f\x98\x80Z" &&
		first.transaction.after.anchor==6 && first.transaction.after.caret==1,"metadata never changes text or directional selection");
	Check(first.transaction.classification==NativeTextClassification::CompositionRelated && first.transaction.after.compositions.at(1)==NativeTextRange{1,5},"nonBMP ACP composition extent");
	Check(first.transaction.operations.size()==1 && first.transaction.operations[0].kind==NativeTextOperationKind::BeginComposition,"one observed callback is one ordered metadata operation");
	std::uint64_t output=999;Check(!doc.PublishCompositionObservation(before,{NativeTextOperationKind::EndComposition,0,0,{},1},output,error)&&output==999,"old shadow observation cannot terminate new composition");
	Check(Metadata(doc,NativeTextOperationKind::UpdateComposition,1,0,1)==2,"idle Update ordered after Begin");
	Check(Offer(doc)==first,"later metadata does not rewrite original offer");
	auto lock=Lock(doc);Check(lock.serial==1,"metadata captures and publishes no text-store lock serial");
	Check(doc.ReplaceACP(lock,0,1,"XY",error) && doc.EndComposition(lock,1,error),"real write scope owns range edit and End");
	Check(Finish(doc,lock)==3,"in-lock text and metadata share one publication");
	NativeTextPendingSnapshot queue;Check(doc.QueryPendingCollection(Owner,100,0,1,77,queue,error)&&queue.count==3&&queue.lastSequence==3,"pending collection sees idle and locked metadata together");
	const auto preAck=Observe(doc);Ack(doc,0);
	output=999;Check(!doc.PublishCompositionObservation(preAck,{NativeTextOperationKind::BeginComposition,0,0,{},2},output,error)&&output==999,"same-revision metadata ACK invalidates observation");
	const auto second=Offer(doc);Check(second.transaction.operations[0].first==0 && second.transaction.operations[0].last==1,"Update extent preserved in immutable offer");Ack(doc,0);
	const auto third=Offer(doc);Check(third.transaction.documentChanged && third.transaction.after.text=="XY\xf0\x9f\x98\x80Z" && third.transaction.after.compositions.empty(),"real lock ends composition without metadata flattening");Ack(doc);
	Check(Metadata(doc,NativeTextOperationKind::BeginComposition,2,0,0)==4,"healthy later composition gets distinct token and empty range");
	Check(Metadata(doc,NativeTextOperationKind::BeginComposition,3,0,2)==5,"second concurrent composition retained");
	Check(Metadata(doc,NativeTextOperationKind::EndComposition,2)==6 && Metadata(doc,NativeTextOperationKind::EndComposition,3)==7,"concurrent terminations ordered independently");
	const auto active=State(doc);Check(active.compositions.empty() && active.text==third.transaction.after.text,"End is metadata only, never accepts or rolls back text");
	const auto saved=Observe(doc);output=999;
	Check(!doc.PublishCompositionObservation(saved,{NativeTextOperationKind::BeginComposition,0,0,{},2},output,error)&&output==999,"ended identities never reused");
	Check(doc.Retire(Owner,error),"explicit cancellation authority retires native document");
	Check(!doc.PublishCompositionObservation(saved,{NativeTextOperationKind::BeginComposition,0,0,{},4},output,error)&&output==999,"late callback cannot resurrect canceled owner");
}
void ObservedCompositionFailures() {
	NativeTextDocument doc;NativeTextMetadataObservation sentinel{{90,91},92,93,94,95,96},out=sentinel;
	Check(!doc.CaptureCompositionObservation(Owner,77,out,error)&&out==sentinel,"unopened observation unchanged");Open(doc,"a\xf0\x9f\x98\x80z");
	Check(!doc.CaptureCompositionObservation(Other,77,out,error)&&out==sentinel,"wrong observation owner unchanged");
	Check(!doc.CaptureCompositionObservation(Owner,0,out,error)&&out==sentinel,"unobserved dispatch refused");
	const auto original=Observe(doc);const auto initial=State(doc);
	for(int field=0;field<6;++field) {
		auto bad=original;
		if(field==0)bad.identity=Other;
		if(field==1)++bad.engineRevision;
		if(field==2)++bad.shadowRevision;
		if(field==3)++bad.transactionSequence;
		if(field==4)++bad.acknowledgedSequence;
		if(field==5)bad.dispatch=0;
		std::uint64_t published=999;Check(!doc.PublishCompositionObservation(bad,{NativeTextOperationKind::BeginComposition,0,1,{},1},published,error)&&published==999,"wrong exact metadata observation rejected");
	}
	const std::vector<NativeTextOperation> invalid{
		{NativeTextOperationKind::Replace,0,1,"x",0},{NativeTextOperationKind::Select,0,1,{},0},
		{static_cast<NativeTextOperationKind>(99),0,0,{},1},{NativeTextOperationKind::BeginComposition,0,1,"x",1},
		{NativeTextOperationKind::BeginComposition,0,1,{},0},{NativeTextOperationKind::BeginComposition,2,3,{},1},
		{NativeTextOperationKind::BeginComposition,4,1,{},1},{NativeTextOperationKind::BeginComposition,0,99,{},1},
		{NativeTextOperationKind::UpdateComposition,0,1,{},1},{NativeTextOperationKind::EndComposition,0,0,{},1},
		{NativeTextOperationKind::EndComposition,0,1,{},1}};
	for(const auto& operation:invalid) {
		std::uint64_t published=999;Check(!doc.PublishCompositionObservation(original,operation,published,error)&&published==999,"malformed metadata cannot publish a partial candidate");
		Check(State(doc)==initial && doc.PendingCount()==0 && Observe(doc)==original,"failed metadata preserves all public state and token budget");
	}
	Metadata(doc,NativeTextOperationKind::BeginComposition,1,0,1);const auto current=Observe(doc);
	const auto live=Offer(doc);std::uint64_t rejected=999;
	Check(!doc.PublishCompositionObservation(current,{NativeTextOperationKind::UpdateComposition,2,3,{},1},rejected,error)&&rejected==999&&Offer(doc)==live,"bad Update cannot alter a live range or queued Begin");
	auto scope=Lock(doc,NativeTextAccess::Read);std::uint64_t published=999;
	Check(!doc.CaptureCompositionObservation(Owner,77,out,error)&&out==sentinel,"active read prevents idle observation");
	Check(!doc.PublishCompositionObservation(current,{NativeTextOperationKind::EndComposition,0,0,{},1},published,error)&&published==999,"metadata does not upgrade read scope");
	NativeTextLockScope waiting;Check(doc.RequestLock(Owner,NativeTextAccess::ReadWrite,false,waiting,error)==NativeTextLockResult::Deferred,"stage deferred callback");Finish(doc,scope);
	Check(!doc.CaptureCompositionObservation(Owner,77,out,error)&&out==sentinel,"pending deferred write prevents idle observation");
	Check(!doc.PublishCompositionObservation(current,{NativeTextOperationKind::EndComposition,0,0,{},1},published,error)&&published==999,"pending deferred callback owns next change");
	Check(doc.GrantDeferredWrite(Owner,waiting,error),"grant original deferred callback");
	Check(!doc.CaptureCompositionObservation(Owner,77,out,error)&&out==sentinel,"active write prevents separate observation");
	Check(doc.EndComposition(waiting,1,error),"blocked idle metadata has not poisoned real callback");Finish(doc,waiting);
	NativeTextLimits limits;limits.pendingTransactions=1;NativeTextDocument bounded(limits);Open(bounded);Metadata(bounded,NativeTextOperationKind::BeginComposition,1,0,1);
	const auto full=Observe(bounded);const auto saved=Offer(bounded);published=999;
	Check(!bounded.PublishCompositionObservation(full,{NativeTextOperationKind::EndComposition,0,0,{},1},published,error)&&published==999&&Offer(bounded)==saved,"queue overflow cannot partly end composition");
	Ack(bounded,0);Metadata(bounded,NativeTextOperationKind::EndComposition,1);
	limits={};limits.retainedCompositions=1;NativeTextDocument retained(limits);Open(retained);Metadata(retained,NativeTextOperationKind::BeginComposition,1,0,1);Metadata(retained,NativeTextOperationKind::EndComposition,1);
	const auto ended=Observe(retained);published=999;
	Check(!retained.PublishCompositionObservation(ended,{NativeTextOperationKind::BeginComposition,0,0,{},2},published,error)&&published==999,"pending ended tokens still consume composition budget");
	Ack(retained,0);Ack(retained,0);Metadata(retained,NativeTextOperationKind::BeginComposition,2,0,0);
	limits={};limits.sequence=2;NativeTextDocument exhausted(limits);Open(exhausted);Metadata(exhausted,NativeTextOperationKind::BeginComposition,1,0,0);Ack(exhausted,0);published=999;
	Check(!exhausted.PublishCompositionObservation(Observe(exhausted),{NativeTextOperationKind::EndComposition,0,0,{},1},published,error)&&published==999,"metadata shadow revision budget does not wrap");
	limits={};limits.pendingBytes=1;NativeTextDocument bytes(limits);Open(bytes);const auto byteBarrier=Observe(bytes);published=999;
	Check(!bytes.PublishCompositionObservation(byteBarrier,{NativeTextOperationKind::BeginComposition,0,1,{},1},published,error)&&published==999,"metadata payload bound refuses publication");
	Check(bytes.PendingCount()==0 && bytes.PendingBytes()==0 && Observe(bytes)==byteBarrier && State(bytes).compositions.empty(),"payload refusal keeps text, composition and all counters unchanged");
}
} // namespace
int main() { try {
	Mapping(); OpenAndScope(); AtomicTransactions(); CompositionClassification(); AcknowledgementAndSync(); DeferredAndRetirement(); Budgets(); PendingCollectionWatermarks(); PendingCollectionLocksAndBounds(); ObservedCompositionLifecycle(); ObservedCompositionFailures();
	std::cout<<"PASS "<<checks<<" checks\n"; return 0;
} catch (const std::exception& e) { std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n'; return 1; } }
