// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Real Runtime/history and exact extracted RetainedUI service bodies. Only
// engine host/resource scalar sources are counted doubles; no engine activation.
#define main CanvasRuntimeMain
#include "UiDocumentCanvasTest.cpp"
#undef main
#include "src/ui/RetainedUI.h"
#include <algorithm>
#include <thread>
#include <stdexcept>
using EngineHost=CanvasHost;
struct Renderer {int generation=0;int GetVideoRestartCount() const noexcept{return generation;}} renderer,*renderSystem=&renderer;
struct Common {
    void Warning(const char*,...){}
    void Printf(const char*,...){}
    void FatalError(const char* text){throw std::runtime_error(text);}
} commonObject,*common=&commonObject;
static int codePageGeneration=1;
static int LangDict_GetCodePageGeneration(){return codePageGeneration;}
static double PresentationTime(){return 1;}
static void Close() {} // No preview registered in this fixture.
struct Commands {void RemoveCommand(const char*){}} commands,*cmdSystem=&commands;
#include "ViewMethods.inc"
#include <RmlUi/Core/ElementDocument.h>
#include "UiDocumentPopupFixture.h"
struct ViewFixture {
    DocumentEdit history;
    retainedUIView_t* view=nullptr;
    std::vector<Diagnostic> diagnostics;
    RuntimeDocumentOptions options;
    std::array<DocumentEditOperation,1> operations{ReplaceDocumentValue{"panel","/properties/left/value","70"}};
    ViewFixture(){
        Check(history.Open(Source,{},diagnostics),"view history opens");
        view=RetainedUI_CreateView();Check(view!=nullptr,"real view registration");
        Check(RetainedUI_LoadView(view,Source,"canvas.q4ui",diagnostics),"actual view load + resource entry");
        options.sourcePath="edited.q4ui";options.seconds=1;
    }
    ~ViewFixture(){RetainedUI_DestroyView(view);}
    retainedUIPreparedEdit_t* Prepare(){retainedUIEditIdentity_t expected;Check(RetainedUI_QueryEditIdentity(view,expected),"exact view query");return RetainedUI_PrepareEdit(view,expected,history,history.Identity(),operations,options,diagnostics);}
};
static void ViewPublication(){
    ViewFixture f;auto* runtime=f.view->runtime.get();auto* prepared=f.Prepare();Check(prepared!=nullptr,"actual service prepares edit");
    Check(f.view->source==Source&&f.view->path=="canvas.q4ui"&&f.history.UndoCount()==0,"all cached source remains old");
    const auto calls=host.calls;DocumentEditReceipt out;bool accepted=false;
    deny=true;accepted=RetainedUI_PublishEdit(f.view,prepared,out);deny=false;
    Check(accepted&&host.calls==calls,"actual service publication has no allocation/callback");
    Check(f.view->runtime.get()==runtime&&f.view->source==f.history.Current()->Source()&&f.view->path=="edited.q4ui","runtime/source/path publish together");
    Check(runtime->SourceMatches(f.view->source,f.view->path),"resource cache matches exact loaded runtime");
    Check(!RetainedUI_PublishEdit(f.view,prepared,out),"consumed publication never repeats");
    RetainedUI_DestroyPreparedEdit(prepared);Check(ContextCount()==1,"service eventually disposes old context");
    ++renderer.generation;Check(RetainedUI_PrepareView(f.view),"resource reset uses newly published source/path");
    Check(runtime->SourceMatches(f.history.Current()->Source(),"edited.q4ui"),"new source survives actual service reload");
}
static void PendingResources(){
    ViewFixture f;auto* prepared=f.Prepare();Check(prepared!=nullptr,"pending resource fixture");
    const auto identity=f.history.Identity();const auto resets=host.resets;
    unsigned notifications=0;f.view->owner=&notifications;f.view->callback=[](void* owner,retainedUIViewEvent_t event){
        if(event==retainedUIViewEvent_t::BeforeResourceReset){++*static_cast<unsigned*>(owner);Check(ContextCount()==1,"pending candidate closes before external resource notification");}
    };
    ++renderer.generation;DocumentEditReceipt out;
    Check(!RetainedUI_PublishEdit(f.view,prepared,out)&&!out.after.document,"changed real renderer generation refuses publication");
    Check(RetainedUI_PrepareView(f.view)&&host.resets==resets+1,"resource refresh is not suppressed by a retained proposal");
    Check(f.history.Identity()==identity&&f.view->source==Source&&ContextCount()==1,"candidate cleaned before reset; old source recovered");
    RetainedUI_DestroyPreparedEdit(prepared);Check(notifications==1,"actual resource notification boundary observed");f.view->callback=nullptr;f.view->owner=nullptr;
    host.callback=[&]{++renderer.generation;Check(!RetainedUI_PrepareView(f.view),"resource refresh refuses an active callback stack");};
    prepared=f.Prepare();Check(!prepared,"deferred resource change invalidates in-flight proposal");
    Check(editWorkDepth==0&&RetainedUI_PrepareView(f.view),"next normal prepare completes deferred resource refresh");
}
static void OwnerDeathAndReentry(){
    ViewFixture f;auto* old=f.view;const auto oldToken=old->edit->lifetime;
    old->owner=&f;old->callback=[](void*,retainedUIViewEvent_t){};
    host.callback=[&]{RetainedUI_DestroyView(old);Check(RegisteredView(old)&&!RetainedUI_ViewRuntime(old)&&!old->callback&&!old->owner,"destruction deferred only through callback; old owner immediately disabled");};
    auto* prepared=f.Prepare();Check(!prepared&&!RegisteredView(old)&&editWorkDepth==0&&ContextCount()==0,"deferred view destruction completes when stack unwinds");f.view=nullptr;
    f.view=RetainedUI_CreateView();Check(f.view&&f.view->edit->lifetime!=oldToken,"replacement view has nonreused lifetime");
    Check(RetainedUI_LoadView(f.view,Source,"canvas.q4ui",f.diagnostics),"replacement loads");
    retainedUIEditIdentity_t expected;Check(RetainedUI_QueryEditIdentity(f.view,expected),"reentry fixture identity");
    host.callback=[&]{Check(!RetainedUI_PrepareEdit(f.view,expected,f.history,f.history.Identity(),f.operations,f.options,f.diagnostics),"nested prepare refuses");};
    Check(!RetainedUI_PrepareEdit(f.view,expected,f.history,f.history.Identity(),f.operations,f.options,f.diagnostics),"nested attempt invalidates outer envelope");
    auto history=std::make_unique<DocumentEdit>();Check(history->Open(Source,{},f.diagnostics),"separate history owner");
    Check(RetainedUI_QueryEditIdentity(f.view,expected),"identity after refused callback");
    prepared=RetainedUI_PrepareEdit(f.view,expected,*history,history->Identity(),f.operations,f.options,f.diagnostics);Check(prepared!=nullptr,"proposal before history destruction");
    history.reset();DocumentEditReceipt out;Check(!RetainedUI_PublishEdit(f.view,prepared,out),"dead history checked before any raw owner dereference");
    RetainedUI_DestroyView(f.view);f.view=nullptr;RetainedUI_DestroyPreparedEdit(prepared);
    Check(ContextCount()==0&&editPrepared==nullptr,"all old owner contexts and proposal storage eventually released");
}

static void NoOpAndCopiedEnvelope(){
    ViewFixture f;auto before=f.view->runtime->CanvasIdentity();const auto calls=host.calls;const auto contexts=ContextCount();
    f.operations[0]=ReplaceDocumentValue{"panel","/properties/left/value","10"};
    auto* prepared=f.Prepare();Check(prepared!=nullptr&&host.calls==calls&&ContextCount()==contexts,"source no-op creates no canvas or host work");
    DocumentEditReceipt out;out.before={9,8};const auto identity=f.history.Identity();
    Check(RetainedUI_PublishEdit(f.view,prepared,out)&&!out.sourceChanged&&out.after==identity,"checked source no-op has no history revision");
    Check(f.view->path=="canvas.q4ui"&&f.view->runtime->SourceMatches(Source,"canvas.q4ui")&&ContextCount()==contexts,"source no-op preserves source/path and canvas");
    RetainedUI_DestroyPreparedEdit(prepared);
    f.operations[0]=ReplaceDocumentValue{"panel","/properties/left/value","70"};
    host.callback=[&]{f.options.sourcePath="../invalid-after-copy";f.options.viewport.width=0;};
    prepared=f.Prepare();Check(prepared!=nullptr,"caller options copied before first host callback");
    deny=true;const bool accepted=RetainedUI_PublishEdit(f.view,prepared,out);deny=false;
    Check(accepted&&f.view->path=="edited.q4ui","publication uses immutable copied metadata");
    RetainedUI_DestroyPreparedEdit(prepared);f.options.sourcePath="undo.q4ui";f.options.viewport.width=1280;
    retainedUIEditIdentity_t expected;Check(RetainedUI_QueryEditIdentity(f.view,expected),"undo cached view identity");
    prepared=RetainedUI_PrepareHistory(f.view,expected,f.history,f.history.Identity(),false,f.options,f.diagnostics);
    Check(prepared!=nullptr&&RetainedUI_PublishEdit(f.view,prepared,out),"actual service prepared undo");
    Check(f.view->source==Source&&f.view->path=="undo.q4ui"&&f.history.RedoCount()==1,"undo publishes exact cache/history together");
    RetainedUI_DestroyPreparedEdit(prepared);
    Check(RetainedUI_QueryEditIdentity(f.view,expected),"current before stale identity rejection");const auto prior=expected;
    f.view->runtime->CancelInput(2);Check(!RetainedUI_PrepareEdit(f.view,prior,f.history,f.history.Identity(),f.operations,f.options,f.diagnostics),"stale canvas inside unchanged view refused");
    f.view->path="divergent-cache.q4ui";Check(RetainedUI_QueryEditIdentity(f.view,expected),"view protocol query remains separate from source equality");
    Check(!RetainedUI_PrepareEdit(f.view,expected,f.history,f.history.Identity(),f.operations,f.options,f.diagnostics),"exact cached path/runtime join required");
    f.view->path="undo.q4ui";
}
static void CrossEnvelopeAndAbort(){
    ViewFixture a,b;auto* prepared=a.Prepare();Check(prepared!=nullptr,"cross-view fixture");DocumentEditReceipt out;out.before={12,13};
    Check(!RetainedUI_PublishEdit(b.view,prepared,out)&&out.before==DocumentEditIdentity{12,13},"wrong view preserves caller receipt");
    Check(RetainedUI_PublishEdit(a.view,prepared,out),"wrong view refusal cannot consume original authority");
    RetainedUI_DestroyPreparedEdit(prepared);
    auto* peer=b.Prepare();Check(peer!=nullptr,"pending peer candidate");
    a.operations[0]=ReplaceDocumentValue{"panel","/properties/left/value","90"};
    host.callback=[&]{RetainedUI_DestroyPreparedEdit(peer);Check(editPrepared!=nullptr,"prepared destruction deferred within active host callback");};
    prepared=a.Prepare();Check(prepared!=nullptr&&editWorkDepth==0,"prepared cleanup drains after callback stack");
    Check(ContextCount()==3,"peer discarded context cleaned, current candidate retained");
    RetainedUI_DestroyPreparedEdit(prepared);
}


static void DeferredShutdown(){
    ViewFixture f;const auto identity=f.history.Identity();const auto resets=host.resets;
    host.callback=[&]{RetainedUI_Shutdown();Check(editShutdownPending&&editWorkDepth==1&&host.resets==resets,"shutdown obligation retained through active callback");};
    auto* prepared=f.Prepare();Check(!prepared&&editWorkDepth==0&&!editShutdownPending,"failed preparation drains deferred shutdown");
    Check(host.resets==resets+1&&ContextCount()==0&&f.view->failed&&f.history.Identity()==identity&&f.view->source==Source,"shutdown waits for candidate stack then closes source contexts");
    Check(!RetainedUI_PrepareView(f.view),"shutdown does not silently reload failed owner");
    Check(RetainedUI_LoadView(f.view,Source,"canvas.q4ui",f.diagnostics),"explicit later initialization occurs after complete cleanup");
    host.callback=[&]{RetainedUI_DestroyView(f.view);RetainedUI_Shutdown();};
    auto* old=f.view;prepared=f.Prepare();f.view=nullptr;
    Check(!prepared&&!RegisteredView(old)&&ContextCount()==0&&!editShutdownPending,"owner death plus deferred shutdown releases exact resources");
}

static void PopupCallbackLifetimes(){
    for(unsigned mode=0;mode<8;++mode){
        const auto source=PopupFixture::Text(PopupFixture::BoundedChoice());
        DocumentEdit history;std::vector<Diagnostic> diagnostics;Check(history.Open(source,{},diagnostics),"popup history opens");
        auto* view=RetainedUI_CreateView();Check(view&&RetainedUI_LoadView(view,source,"popup.q4ui",diagnostics),"actual registered constrained popup");
        auto* live=view->runtime.get();Viewport viewport;
        if(mode==7){
            retainedUIEditIdentity_t current;Check(RetainedUI_QueryEditIdentity(view,current),"popup prepared view association");RuntimeDocumentOptions options;options.sourcePath="popup.q4ui";
            auto* prepared=RetainedUI_PrepareEdit(view,current,history,history.Identity(),std::array<DocumentEditOperation,1>{ReplaceDocumentValue{"choice","/properties/width/value","355"}},options,diagnostics);
            DocumentEditReceipt receipt;Check(prepared&&RetainedUI_PublishEdit(view,prepared,receipt),"popup context publication retains original Runtime control");RetainedUI_DestroyPreparedEdit(prepared);
        }
        const auto stableSource=view->source;const auto undo=history.UndoCount();live->Frame(viewport,1);
        Check(live->FocusControl("choice",1)&&live->OpenChoicePopup("choice",1),"constrained popup opens by semantic operation");live->Frame(viewport,1);
        Check(live->GetWidgetState("choice")->popupOpen,"constrained popup painted before callback");
        const auto identity=history.Identity();const auto lifetime=view->edit->lifetime;const auto resets=host.resets;
        bool called=false;unsigned callsAtCallback=0;
        host.callback=[&]{
            called=true;callsAtCallback=host.calls;Check(live->HasActiveCanvasCallback(),"exact original runtime holds callback scope");
            Check(!RetainedUI_CreateView(),"callback cannot register replacement before old scope unwinds");
            retainedUIEditIdentity_t unavailable;Check(!RetainedUI_QueryEditIdentity(view,unavailable),"busy canvas has no publishable view identity");
            if(mode==0||mode==7)live->CancelInput(2);
            if(mode==1||mode==6){RetainedUI_DestroyView(view);Check(RegisteredView(view)&&!RetainedUI_ViewRuntime(view),"native callback immediately retires but retains exact view storage");}
            if(mode==2||mode==6){RetainedUI_Shutdown();Check(editShutdownPending&&host.resets==resets,"callback shutdown retains cleanup obligation");}
            if(mode==3){++renderer.generation;Check(!RetainedUI_PrepareView(view),"callback resource refresh refuses before old context reset");}
            if(mode==5)Check(!live->OpenChoicePopup("choice",2),"reentrant input refuses exact busy runtime");
            if(mode==4||mode==6)throw std::runtime_error("layout callback failure");
        };
        {EditCall call;live->MenuAction(MenuInput::Accept,true,2);Check(called,"input-time public Rml layout invokes counted original host");
         Check(editCallDepth==1&&RegisteredView(view),"view deletion remains deferred through complete outer service call");
         Check(host.calls==callsAtCallback,"input refresh stops after callback invalidates owner or throws");}
        Check(editCallDepth==0&&!editShutdownPending&&history.Identity()==identity&&history.UndoCount()==undo,"normal or refused callback unwinds service and preserves history");
        if(mode==1||mode==6){
            Check(!RegisteredView(view)&&ContextCount()==0,"outermost service return drains deleted original without later entry");
            auto* replacement=RetainedUI_CreateView();Check(replacement&&replacement->edit->lifetime!=lifetime,"replacement receives new lifetime after complete drain");RetainedUI_DestroyView(replacement);
        }else{
            Check(RegisteredView(view)&&view->source==stableSource&&view->path=="popup.q4ui","refused callback preserves original cache/source metadata");
            Check(live->TakeActions().empty(),"stale callback cannot accept a popup row");
            Check(!live->HasActiveCanvasCallback(),"runtime callback scope always clears after refusal");
            if(mode==2)Check(view->failed&&ContextCount()==0&&host.resets==resets+1,"queued shutdown finishes before outer return");
            if(mode==3)Check(RetainedUI_PrepareView(view)&&ContextCount()==1,"resource reset recovers at next safe explicit prepare");
            RetainedUI_DestroyView(view);
        }
        Check(ContextCount()==0,"each popup callback case releases all contexts");
    }
}

int main(){std::setvbuf(stdout,nullptr,_IONBF,0);std::fprintf(stderr,"canvas baseline\n");CanvasRuntimeMain();std::fprintf(stderr,"view publication\n");ViewPublication();std::fprintf(stderr,"pending resources\n");PendingResources();std::fprintf(stderr,"owner death\n");OwnerDeathAndReentry();NoOpAndCopiedEnvelope();CrossEnvelopeAndAbort();DeferredShutdown();PopupCallbackLifetimes();std::printf("PASS %u checks; actual RetainedUI publication/resource methods + real Runtime/Rml/history.\n",checks);}
