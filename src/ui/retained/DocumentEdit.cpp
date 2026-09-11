// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "DocumentEdit.h"
#include "DocumentSource.h"
#include <json/json.h>
#include <algorithm>
#include <atomic>
#include <string_view>

namespace openq4::ui {
namespace {
struct Refused { const char* reason; };
void Require(bool condition,const char* reason) { if(!condition)throw Refused{reason}; }
bool Report(std::vector<Diagnostic>& diagnostics,const char* message) noexcept {
    try { diagnostics.clear();diagnostics.push_back({"",message,0,1,1}); } catch(...) {}
    return false;
}
std::uint64_t DocumentToken() noexcept {
    static std::atomic<std::uint64_t> next{1};
    auto token=next.load(std::memory_order_relaxed);
    while(token!=(std::numeric_limits<std::uint64_t>::max)())
        if(next.compare_exchange_weak(token,token+1,std::memory_order_relaxed))return token;
    return 0;
}
bool Add(std::size_t& total,std::size_t count,std::size_t limit) noexcept {
    if(total>limit || count>limit-total)return false;
    total+=count;return true;
}
bool Limits(const DocumentEditLimits& l) noexcept {
    return l.sourceBytes && l.sourceBytes<=16*1024*1024 && l.historyBytes && l.historyBytes<=256*1024*1024 &&
        l.workingSourceBytes && l.workingSourceBytes<=512*1024*1024 && l.states && l.states<=1024 &&
        l.steps && l.steps<=1024 && l.revision;
}
struct Budget {
    const DocumentEditLimits& limits;
    std::size_t retained;
    void Check(std::size_t source,std::size_t fragment=0) const {
        Require(source<=limits.sourceBytes && fragment<=limits.sourceBytes,"Document source byte limit exceeded");
        // Covers retained immutable sources, staging, candidate/BOM source and
        // a value fragment plus its parsed-span copy. DOM/container allocation
        // overhead is separately subject to allocation failure, not byte proof.
        std::size_t total=retained;
        Require(Add(total,source,limits.workingSourceBytes) && Add(total,source,limits.workingSourceBytes) &&
            Add(total,source,limits.workingSourceBytes) && Add(total,fragment,limits.workingSourceBytes) &&
            Add(total,fragment,limits.workingSourceBytes),"Document working source byte limit exceeded");
    }
};
struct Span {std::size_t start,end;};
Span Location(const Json::Value& value,const std::string& source) {
    const auto a=value.getOffsetStart(),b=value.getOffsetLimit();
    Require(a>=0 && b>a && static_cast<std::size_t>(b)<=source.size(),"Invalid parser source span");
    return {static_cast<std::size_t>(a),static_cast<std::size_t>(b)};
}
struct Position {const Json::Value* node;const Json::Value* parent;Json::ArrayIndex index;};
using Index=std::map<std::string,Position>;
void IndexNode(const Json::Value& node,const Json::Value* parent,Json::ArrayIndex index,Index& result,unsigned depth=0) {
    Require(depth<=48 && result.size()<65536,"Document hierarchy exceeds the node/depth limit");
    Require(node.isObject() && node["id"].isString(),"Structural edit requires an identified node object");
    auto id=node["id"].asString();
    Require(!id.empty() && id.size()<=128 && result.emplace(id,Position{&node,parent,index}).second,"Structural edit has missing or ambiguous node ID");
    if(node.isMember("children")) {
        Require(node["children"].isArray(),"Structural children must remain an array");
        const auto& children=node["children"];
        for(Json::ArrayIndex i=0;i<children.size();++i)IndexNode(children[i],&node,i,result,depth+1);
    }
}
const Position& Find(const Index& nodes,const std::string& id) {
    const auto found=nodes.find(id);Require(found!=nodes.end(),"Structural node ID does not exist");return found->second;
}
void Splice(std::string& source,Span at,std::string_view replacement,const Budget& budget) {
    Require(at.start<=at.end && at.end<=source.size(),"Invalid structural splice span");
    std::size_t next=source.size()-(at.end-at.start);
    Require(Add(next,replacement.size(),budget.limits.sourceBytes),"Document source byte limit exceeded");
    budget.Check(next,replacement.size());
    source.replace(at.start,at.end-at.start,replacement);
}
std::size_t Comma(const std::string& source,std::size_t begin,std::size_t end) {
    std::size_t found=std::string::npos;
    for(std::size_t i=begin;i<end;) {
        if(source[i]==' ' || source[i]=='\t' || source[i]=='\r' || source[i]=='\n'){++i;continue;}
        if(source[i]=='/' && i+1<end && source[i+1]=='/') {i+=2;while(i<end && source[i]!='\n')++i;continue;}
        if(source[i]=='/' && i+1<end && source[i+1]=='*') {
            const auto close=source.find("*/",i+2);Require(close!=std::string::npos && close+2<=end,"Invalid comment at child boundary");i=close+2;continue;
        }
        Require(source[i]==',' && found==std::string::npos,"Invalid comma at child boundary");found=i++;
    }
    Require(found!=std::string::npos,"Missing comma at child boundary");return found;
}
void Remove(std::string& source,const Position& position,const Budget& budget) {
    Require(position.parent!=nullptr,"Root cannot be removed or moved");
    const auto node=Location(*position.node,source);const auto& siblings=(*position.parent)["children"];
    if(position.index+1<siblings.size()) {
        const auto comma=Comma(source,node.end,Location(siblings[position.index+1],source).start);
        Splice(source,{comma,comma+1},{},budget);Splice(source,node,{},budget);
    } else if(position.index) {
        const auto comma=Comma(source,Location(siblings[position.index-1],source).end,node.start);
        Splice(source,node,{},budget);Splice(source,{comma,comma+1},{},budget);
    } else Splice(source,node,{},budget);
}
void InsertAdded(std::string& source,std::size_t at,std::string_view prefix,const std::string& value,std::string_view suffix,const Budget& budget) {
    std::size_t addedSize=value.size(),next=source.size();
    Require(Add(addedSize,prefix.size(),budget.limits.sourceBytes) && Add(addedSize,suffix.size(),budget.limits.sourceBytes) &&
        Add(next,addedSize,budget.limits.sourceBytes),"Document source byte limit exceeded");
    budget.Check(next,addedSize);
    // Preflight the complete syntax before allocating. Reserve once instead
    // of relying on chained operator+ temporary/reallocation behaviour.
    std::string added(0,'\0');added.reserve(addedSize);
    added.append(prefix);added.append(value);added.append(suffix);
    Splice(source,{at,at},added,budget);
}
void Insert(std::string& source,const Json::Value& parent,std::size_t index,const std::string& value,const Budget& budget) {
    const auto count=parent.isMember("children")?parent["children"].size():0;
    Require(index<=count,"Child insertion index is out of range");
    if(!parent.isMember("children")) {
        const auto end=Location(parent,source).end;
        Require(source[end-1]=='}',"Invalid parent object boundary");
        InsertAdded(source,end-1,",\n\"children\":[",value,"]",budget);return;
    }
    const auto& children=parent["children"];
    if(index<count) {
        const auto start=Location(children[static_cast<Json::ArrayIndex>(index)],source).start;
        InsertAdded(source,start,{},value,",\n",budget);
    } else if(count) {
        const auto end=Location(children[count-1],source).end;
        InsertAdded(source,end,",\n",value,{},budget);
    } else {
        const auto end=Location(children,source).end;Require(source[end-1]==']',"Invalid children array boundary");
        Splice(source,{end-1,end-1},value,budget);
    }
}
bool Parse(const std::string& source,Json::Value& parsed,std::vector<Diagnostic>& diagnostics) {
    return detail::ParseDocumentSource(source,parsed,diagnostics);
}
std::string Fragment(const std::string& supplied,const Budget& budget,std::size_t staged,std::vector<Diagnostic>& diagnostics) {
    budget.Check(staged,supplied.size());Json::Value value;
    if(!Parse(supplied,value,diagnostics))throw Refused{nullptr};
    const auto span=Location(value,supplied);return supplied.substr(span.start,span.end-span.start);
}
bool Edit(std::string& source,const DocumentEditOperation& operation,const Budget& budget,std::vector<Diagnostic>& diagnostics) {
    budget.Check(source.size());Json::Value parsed;
    if(!Parse(source,parsed,diagnostics))return false;
    Index nodes;IndexNode(parsed["root"],nullptr,0,nodes);
    if(const auto* insert=std::get_if<InsertDocumentNode>(&operation)) {
        const auto& parent=Find(nodes,insert->parent);
        auto value=Fragment(insert->source,budget,source.size(),diagnostics);
        Json::Value fragment;if(!Parse(value,fragment,diagnostics))return false;
        Require(fragment.isObject(),"Inserted subtree must be an object");
        Insert(source,*parent.node,insert->index,value,budget);
    } else if(const auto* remove=std::get_if<RemoveDocumentNode>(&operation)) {
        Remove(source,Find(nodes,remove->node),budget);
    } else if(const auto* move=std::get_if<MoveDocumentNode>(&operation)) {
        const auto& position=Find(nodes,move->node);const auto& parent=Find(nodes,move->parent);
        Require(position.parent!=nullptr,"Root cannot be removed or moved");
        const auto from=Location(*position.node,source),to=Location(*parent.node,source);
        Require(to.start<from.start || to.start>=from.end,"Cannot move a node inside its own subtree");
        const auto targetCount=parent.node->isMember("children")?(*parent.node)["children"].size():0;
        const bool same=position.parent==parent.node;
        Require(move->index<=targetCount-(same?1:0),"Child move index is out of range after removal");
        if(same && move->index==position.index)return true;
        budget.Check(source.size(),from.end-from.start);
        const std::string value=source.substr(from.start,from.end-from.start);
        Remove(source,position,budget);
        Json::Value after;if(!Parse(source,after,diagnostics))return false;
        Index remaining;IndexNode(after["root"],nullptr,0,remaining);
        Insert(source,*Find(remaining,move->parent).node,move->index,value,budget);
    } else if(const auto* replace=std::get_if<ReplaceDocumentValue>(&operation)) {
        Require(replace->pointer.size()<=4096,"Replacement pointer limit exceeded");
        const auto& scope=replace->node.empty()?parsed:*Find(nodes,replace->node).node;
        const auto* target=detail::ResolveDocumentSource(scope,replace->pointer);
        Require(target!=nullptr,"Replacement pointer does not resolve to an existing value");
        auto value=Fragment(replace->source,budget,source.size(),diagnostics);
        Splice(source,Location(*target,source),value,budget);
    } else throw Refused{"Unknown structural operation"};
    return true;
}
} // namespace
struct DocumentEdit::Impl {
    DocumentEditLimits limits;
    DocumentEditIdentity identity;
    std::vector<std::shared_ptr<const Document>> states;
    std::size_t cursor=0,bytes=0;
    bool busy=false;
    bool alive=true;
    std::weak_ptr<Prepared::Data> outstanding;
    struct Guard {Impl& state;~Guard(){state.busy=false;}};
    DocumentEditReceipt Receipt(DocumentEditIdentity before,bool changed) const noexcept {
        return {before,identity,changed,cursor,states.empty()?0:states.size()-cursor-1,bytes};
    }
};
struct DocumentEdit::Prepared::Data {
    std::weak_ptr<DocumentEdit::Impl> owner;
    std::shared_ptr<const Document> origin,target;
    std::vector<std::shared_ptr<const Document>> states;
    DocumentEditReceipt receipt;
    bool ready=true,replaceHistory=false;
};
DocumentEdit::Prepared::Prepared(std::shared_ptr<Data> value):data(std::move(value)){}
DocumentEdit::Prepared::~Prepared(){data->ready=false;}
const Document& DocumentEdit::Prepared::Target() const noexcept{return *data->target;}
const Document& DocumentEdit::Prepared::Origin() const noexcept{return *data->origin;}
const DocumentEditReceipt& DocumentEdit::Prepared::Receipt() const noexcept{return data->receipt;}
bool DocumentEdit::Prepared::OwnerCurrent() const noexcept {
    const auto owner=data->owner.lock();
    return owner && owner->alive && !owner->busy && data->ready && owner->identity==data->receipt.before &&
        owner->outstanding.lock()==data && !owner->states.empty() && owner->states[owner->cursor]==data->origin;
}
DocumentEdit::DocumentEdit():impl(std::make_shared<Impl>()){}
DocumentEdit::~DocumentEdit(){impl->alive=false;}
DocumentEditIdentity DocumentEdit::Identity() const noexcept{return impl->identity;}
const Document* DocumentEdit::Current() const noexcept{return impl->states.empty()?nullptr:impl->states[impl->cursor].get();}
std::size_t DocumentEdit::UndoCount() const noexcept{return impl->cursor;}
std::size_t DocumentEdit::RedoCount() const noexcept{return impl->states.empty()?0:impl->states.size()-impl->cursor-1;}
std::size_t DocumentEdit::HistorySourceBytes() const noexcept{return impl->bytes;}
bool DocumentEdit::Open(const std::string& source,DocumentEditLimits limits,std::vector<Diagnostic>& diagnostics) noexcept {
    if(impl->busy)return Report(diagnostics,"Reentrant document edit refused");
    impl->busy=true;Impl::Guard guard{*impl};
    try {
        diagnostics.clear();Require(impl->states.empty(),"Document editor already has a document");
        Require(Limits(limits),"Invalid document edit limits");Budget{limits,0}.Check(source.size());
        Require(source.size()<=limits.historyBytes,"Document history byte limit exceeded");
        auto candidate=std::make_shared<Document>();if(!candidate->Load(source,diagnostics))return false;
        std::vector<std::shared_ptr<const Document>> states;states.push_back(candidate);
        const auto token=DocumentToken();Require(token!=0,"Document lifetime tokens exhausted");
        impl->states.swap(states);impl->limits=limits;impl->identity={token,1};impl->bytes=Current()->Source().size();return true;
    }catch(const Refused& failure){return failure.reason?Report(diagnostics,failure.reason):false;}
    catch(...){return Report(diagnostics,"Document edit allocation or parser failure");}
}
std::unique_ptr<DocumentEdit::Prepared> DocumentEdit::PrepareEdit(DocumentEditIdentity expected,std::span<const DocumentEditOperation> operations,std::vector<Diagnostic>& diagnostics) noexcept {
    if(impl->busy){Report(diagnostics,"Reentrant document edit refused");return {};}
    impl->busy=true;Impl::Guard guard{*impl};
    try {
        diagnostics.clear();Require(Current() && expected==impl->identity,"Stale document edit identity");
        Require(impl->outstanding.expired(),"Document edit already has a retained preparation");
        Require(operations.size()<=impl->limits.steps,"Document operation count exceeded");
        const Budget budget{impl->limits,impl->bytes};budget.Check(Current()->Source().size());
        std::string source=Current()->Source();
        for(const auto& operation:operations)if(!Edit(source,operation,budget,diagnostics))return {};
        auto data=std::make_shared<Prepared::Data>();data->owner=impl;
        data->origin=impl->states[impl->cursor];data->target=data->origin;
        data->receipt=impl->Receipt(expected,false);
        if(source==Current()->Source()) {
            auto result=std::unique_ptr<Prepared>(new Prepared(data));impl->outstanding=data;return result;
        }
        Require(impl->identity.revision<impl->limits.revision,"Document revisions exhausted");
        Require(impl->cursor+2<=impl->limits.states,"Document history state limit exceeded");
        std::size_t bytes=source.size();
        for(std::size_t i=0;i<=impl->cursor;++i)Require(Add(bytes,impl->states[i]->Source().size(),impl->limits.historyBytes),"Document history byte limit exceeded");
        budget.Check(source.size());auto candidate=std::make_shared<Document>();
        if(!candidate->Load(source,diagnostics))return {};
        Require(candidate->Model().id==Current()->Model().id,"Document ID cannot change within an editor lifetime");
        std::vector<std::shared_ptr<const Document>> states(impl->states.begin(),impl->states.begin()+impl->cursor+1);
        states.push_back(std::move(candidate));
        data->target=states.back();data->states.swap(states);data->replaceHistory=true;
        data->receipt={expected,{expected.document,expected.revision+1},true,impl->cursor+1,0,bytes};
        auto result=std::unique_ptr<Prepared>(new Prepared(data));impl->outstanding=data;return result;
    }catch(const Refused& failure){if(failure.reason)Report(diagnostics,failure.reason);return {};}
    catch(...){Report(diagnostics,"Document edit allocation or parser failure");return {};}
}
std::unique_ptr<DocumentEdit::Prepared> DocumentEdit::PrepareUndo(DocumentEditIdentity expected,bool redo,std::vector<Diagnostic>& diagnostics) noexcept {
    if(impl->busy){Report(diagnostics,"Reentrant document edit refused");return {};}
    impl->busy=true;Impl::Guard guard{*impl};
    try {
        diagnostics.clear();Require(Current() && expected==impl->identity,"Stale document edit identity");
        Require(impl->outstanding.expired(),"Document edit already has a retained preparation");
        Require(impl->identity.revision<impl->limits.revision,"Document revisions exhausted");
        Require(redo?RedoCount()!=0:UndoCount()!=0,"Requested history state does not exist");
        const auto cursor=redo?impl->cursor+1:impl->cursor-1;
        auto data=std::make_shared<Prepared::Data>();data->owner=impl;
        data->origin=impl->states[impl->cursor];data->target=impl->states[cursor];
        data->receipt={expected,{expected.document,expected.revision+1},true,cursor,impl->states.size()-cursor-1,impl->bytes};
        auto result=std::unique_ptr<Prepared>(new Prepared(data));impl->outstanding=data;return result;
    }catch(const Refused& failure){Report(diagnostics,failure.reason);return {};}
    catch(...){Report(diagnostics,"Document history preparation allocation failure");return {};}
}
bool DocumentEdit::CanPublish(const Prepared& prepared) const noexcept {
    const auto& data=*prepared.data;
    return impl->alive && !impl->busy && data.ready && data.owner.lock()==impl &&
        impl->outstanding.lock()==prepared.data && data.receipt.before==impl->identity && Current()==data.origin.get();
}
void DocumentEdit::PublishPrepared(Prepared& prepared,DocumentEditReceipt& out) noexcept {
    auto& data=*prepared.data;
    if(data.replaceHistory)impl->states.swap(data.states);
    impl->identity=data.receipt.after;impl->cursor=data.receipt.undo;impl->bytes=data.receipt.historySourceBytes;
    data.ready=false;out=data.receipt;
}
bool DocumentEdit::Publish(Prepared& prepared,DocumentEditReceipt& out) noexcept {
    if(!CanPublish(prepared))return false;
    PublishPrepared(prepared,out);return true;
}
bool DocumentEdit::Apply(DocumentEditIdentity expected,std::span<const DocumentEditOperation> operations,DocumentEditReceipt& out,std::vector<Diagnostic>& diagnostics) noexcept {
    auto prepared=PrepareEdit(expected,operations,diagnostics);
    return prepared && Publish(*prepared,out);
}
bool DocumentEdit::Travel(DocumentEditIdentity expected,bool redo,DocumentEditReceipt& out,std::vector<Diagnostic>& diagnostics) noexcept {
    if(impl->busy)return Report(diagnostics,"Reentrant document edit refused");
    impl->busy=true;Impl::Guard guard{*impl};
    if(!Current() || expected!=impl->identity)return Report(diagnostics,"Stale document edit identity");
    if(impl->identity.revision>=impl->limits.revision)return Report(diagnostics,"Document revisions exhausted");
    if(redo?!RedoCount():!UndoCount())return Report(diagnostics,"Requested history state does not exist");
    diagnostics.clear();if(redo)++impl->cursor;else --impl->cursor;++impl->identity.revision;
    out=impl->Receipt(expected,true);return true;
}
bool DocumentEdit::Undo(DocumentEditIdentity id,DocumentEditReceipt& out,std::vector<Diagnostic>& diagnostics) noexcept{return Travel(id,false,out,diagnostics);}
bool DocumentEdit::Redo(DocumentEditIdentity id,DocumentEditReceipt& out,std::vector<Diagnostic>& diagnostics) noexcept{return Travel(id,true,out,diagnostics);}
} // namespace openq4::ui
